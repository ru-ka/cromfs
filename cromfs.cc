/*
cromfs - Copyright (C) 1992,2008 Bisqwit (http://iki.fi/bisqwit/)
Licence: GPL3

cromfs.cc: The cromfs filesystem engine.
The class cromfs should actually be a singleton, because of the signal
handling herein, but that hasn't been written yet...

See doc/FORMAT for the documentation of the filesystem structure.

*/

#define _XOPEN_SOURCE 500
#define _LARGEFILE64_SOURCE
#define __STDC_CONSTANT_MACROS

#include <algorithm>
#include <cstdlib>

#include <unistd.h>
#include <cerrno>
#include <csignal>

#include <sstream>
#include <deque>

#include <sys/time.h>
#include <sys/stat.h>

extern "C" {
#include "lib/lzma/C/LzmaDec.h"
}

#include "lib/longfileread.hh"
#include "lib/cromfs-inodefun.hh"
#include "lib/fadvise.hh"
#include "lib/util.hh"
#include "cromfs.hh"

#define CROMFS_FSIZE (sblock.fsize)
#define CROMFS_BSIZE (sblock.bsize)

#define SBLOCK_DEBUG    0
#define READBLOCK_DEBUG 0
#define FBLOCK_DEBUG    0
#define INODE_DEBUG     0
#define READFILE_DEBUG  0
#define READDIR_DEBUG   0
#define DEBUG_INOTAB    0

#define BLKTAB_CACHE_TIMEOUT 0

unsigned READDIR_CACHE_MAX_SIZE = 5;
unsigned FBLOCK_CACHE_MAX_SIZE = 10;

template<typename T>
static void EraseRandomlyOne(T& container)
{
    if(container.empty()) return;

    typename T::key_type
        smallest = container.begin()->first,
        biggest  = container.rbegin()->first,
        random   = smallest + std::rand() % (biggest-smallest);

    container.erase(container.lower_bound(random));
}

static void *SzAlloc(void*, size_t size)
    { return new unsigned char[size]; }
static void SzFree(void*, void *address)
    { unsigned char*a = (unsigned char*)address; delete[] a; }
static const std::vector<unsigned char> LZMADeCompress
    (const unsigned char* buf, size_t BufSize)
{
    if(BufSize <= LZMA_PROPS_SIZE+8)
    {
    clearly_not_ok:
        throw EBADF;
        return std::vector<unsigned char> ();
    }

    uint_least64_t out_sizemax = R64(&buf[LZMA_PROPS_SIZE]);

    if(out_sizemax >= (size_t)~0ULL)
    {
        // cannot even allocate a vector this large.
        goto clearly_not_ok;
    }

    std::vector<unsigned char> result(out_sizemax);

    ISzAlloc alloc = { SzAlloc, SzFree };

    ELzmaStatus status;
    SizeT out_done = result.size();
    SizeT in_done = BufSize-(LZMA_PROPS_SIZE+8);
    int res = LzmaDecode(
        &result[0], &out_done,
        &buf[LZMA_PROPS_SIZE+8], &in_done,
        &buf[0], LZMA_PROPS_SIZE+8,
        LZMA_FINISH_END,
        &status,
        &alloc);

    /*
    fprintf(stderr, "res=%d, status=%d, in_done=%d (buf=%d), out_done=%d (max=%d)\n",
        res,
        (int)status,
        (int)in_done, (int)(BufSize-(LZMA_PROPS_SIZE+8)),
        (int)out_done, (int)out_sizemax);
    */

    if(res == SZ_OK
    && (status == LZMA_STATUS_FINISHED_WITH_MARK
     || status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)
    && in_done == (BufSize-(LZMA_PROPS_SIZE+8))
    && out_done == out_sizemax)
    {
        return result;
    }
    goto clearly_not_ok;
}

static const std::vector<unsigned char>
    DoLZMALoading(int fd, uint_fast64_t pos, uint_fast64_t size)
        throw(cromfs_exception, std::bad_alloc)
{
    LongFileRead reader(fd, pos, size);
    return LZMADeCompress(reader.GetAddr(), size);
}

const std::string DumpInode(const cromfs_inode_internal& inode)
{
    std::stringstream s;

    s << "mode(";

    if(inode.mode == 0x12345678) s << "inotab";
    else s << "0" << std::oct << inode.mode;

    s << ")time(" << std::dec << inode.time
      << ")links(" << inode.links
      << ")rdev(" << std::hex << inode.rdev
      << ")size(" << std::dec << inode.bytesize
      << ")bsize(" << std::dec << inode.blocksize;

    if(!inode.blocklist.empty())
    {
        s << ")nblocks(" << std::dec << inode.blocklist.size();

        if(inode.blocklist.size() < 10)
            for(unsigned a=0; a<inode.blocklist.size(); ++a)
                s << (a==0 ? ":" : ",") << inode.blocklist[a];
        else
            s << ":<...>";
    }
    s << ")";

    return s.str();
}
const std::string cromfs::DumpBlock(const cromfs_block_internal& block) const
{
    std::stringstream s;

    s << "fblocknum(" << block.get_fblocknum(CROMFS_BSIZE, CROMFS_FSIZE)
      << ")startoffs(" << block.get_startoffs(CROMFS_BSIZE, CROMFS_FSIZE)
      << ")";

    return s.str();
}

void cromfs::reread_superblock()
        throw (cromfs_exception, std::bad_alloc)
{
    /* Note: Don't call this function from constructor,
     * because this function sets up OpenMP teams and
     * if fork() is called after that, it will cause problems.
     */

    cromfs_superblock_internal::BufferType Superblock;
    ( LongFileRead(fd, 0, sizeof(Superblock), Superblock) );

    storage_opts = 0;

    sblock.ReadFromBuffer(Superblock);

    const uint_fast64_t sig  = sblock.sig;
    if(sig != CROMFS_SIGNATURE_01
    && sig != CROMFS_SIGNATURE_02
    && sig != CROMFS_SIGNATURE_03
    ) throw EINVAL;

    fblock_cache.clear();

    FadviseWillNeed(fd, 0, sblock.fblktab_offs); // Will need all data up to first fblock.

#if SBLOCK_DEBUG
    fprintf(stderr,
        "Superblock signature %llX\n"
        "BlockTab at 0x%llX\n"
        "FBlkTab at 0x%llX\n"
        "inotab at 0x%llX (size 0x%llX)\n"
        "rootdir at 0x%llX (size 0x%llX)\n"
        "FSIZE %u  BSIZE %u\n"
        "%u fblocks, %u blocks\n"
        "%llu bytes of files\n",
        (unsigned long long)sblock.sig,
        (unsigned long long)sblock.blktab_offs,
        (unsigned long long)sblock.fblktab_offs,
        (unsigned long long)sblock.inotab_offs,  (unsigned long long)sblock.inotab_size,
        (unsigned long long)sblock.rootdir_offs, (unsigned long long)sblock.rootdir_size,
        (unsigned)CROMFS_FSIZE,
        (unsigned)CROMFS_BSIZE,
        (unsigned)fblktab.size(),
        (unsigned)blktab.size(),
        (unsigned long long)sblock.bytes_of_files);
#endif

    bool need_storage_opts =
       sblock.sig != CROMFS_SIGNATURE_01
    && sblock.sig != CROMFS_SIGNATURE_02;

    /* If we need storage options, we cannot read the block list
     * of an inode before knowing with which options it must be
     * decoded. Hence we first read the inode header, then
     * read the full data after storage_opts is set.
     */
    inotab = read_special_inode(sblock.inotab_offs,  sblock.inotab_size, need_storage_opts);

    storage_opts = inotab.mode;
    if(!need_storage_opts)
    {
        storage_opts = 0; // not supported in these versions
    }

    if(need_storage_opts)
    {
        /* Reread the inode because its interpretation might have changed */
        inotab = read_special_inode(sblock.inotab_offs,  sblock.inotab_size, false);
    }

    #pragma omp parallel sections
    {
     #pragma omp section
     {
        /* This must be done after storage_opts are set. */
        rootdir = read_special_inode(sblock.rootdir_offs, sblock.rootdir_size, false);

        /* As rootdir and inotab have now been read, it's safe to forget
         * them from the page cache. */
        FadviseDontNeed(fd, 0, sblock.blktab_offs);
     }

     #pragma omp section
     {
        FadviseWillNeed(fd, sblock.blktab_offs, sblock.blktab_size);

        rootdir.mode = S_IFDIR | 0555;
        if(sblock.sig == CROMFS_SIGNATURE_01)
            rootdir.links = 2;
        rootdir.uid = 0;
        rootdir.gid = 0;

        forget_blktab();
        reread_blktab(); // must be done after inotab is read
     }

     #pragma omp section
     {
        reread_fblktab();
     }
    } // end sections

    if(fblktab.empty()) throw EINVAL;

#if DEBUG_INOTAB
    fprintf(stderr, "rootdir inode: %s\n", DumpInode(rootdir).c_str());
    fprintf(stderr, "inotab inode: %s\n", DumpInode(inotab).c_str());

    cromfs_inodenum_t maxinode = 2+inotab.bytesize/4;
    for(cromfs_inodenum_t a=2; a<maxinode; )
    {
        cromfs_inode_internal result = read_inode_and_blocks(a);
        fprintf(stderr, "INODE %u: %s\n", (unsigned)a, DumpInode(result).c_str());
        uint_fast64_t nblocks = CalcSizeInBlocks(result.bytesize, result.blocksize);

        unsigned inode_size = INODE_SIZE_BYTES(nblocks);
        inode_size = (inode_size + 3) & ~3;
        a += inode_size / 4;
    }
#endif
}

void cromfs::Initialize()
    throw (cromfs_exception, std::bad_alloc)
{
    reread_superblock();
}

void cromfs::reread_fblktab()
        throw (cromfs_exception, std::bad_alloc)
{
    fblktab.clear();

    uint_fast64_t startpos = sblock.fblktab_offs;
    for(;;)
    {
        unsigned char Buf[17];
        /* TODO: LongFileRead here... but catch EOF somehow? */
        ssize_t r = pread64(fd, Buf, 17, startpos);
        if(r == 0) break;
        if(r < 0) throw errno;
        if(r < 17) throw EINVAL;

        cromfs_fblock_internal fblock;
        fblock.filepos = startpos+4;
        fblock.length  = R32(Buf);
        //uint_fast64_t orig_length = R64(Buf+9); // <- not interesting

        if(fblock.length <= 13)
        {
            throw EINVAL;
        }

#if FBLOCK_DEBUG
        fprintf(stderr, "FBLOCK %u: size(%u)at(%llu (0x%llX))\n",
            (unsigned)fblktab.size(),
            (unsigned)fblock.length,
            (unsigned long long)fblock.filepos,
            (unsigned long long)fblock.filepos);
#endif
        fblktab.push_back(fblock);

        if(storage_opts & CROMFS_OPT_SPARSE_FBLOCKS)
            startpos += 4 + CROMFS_FSIZE;
        else
            startpos += 4 + fblock.length;

        // Let the kernel know that the memory access pattern
        // for a fblock does not have any readahead advantage.
        FadviseRandom(fd, fblock.filepos-4, startpos);
    }
}

void cromfs::forget_blktab()
{
    blktab = std::vector<cromfs_block_internal>();
    // clear() won't free memory, reserve() can only increase size
}

static cromfs* cromfs_alarm_obj = NULL;
static void cromfs_alarm(int sig=0) __attribute__((constructor));
static void cromfs_alarm(int/* sig*/)
{
    if(!cromfs_alarm_obj) return;
    cromfs_alarm_obj->forget_blktab();
    std::signal(SIGALRM, cromfs_alarm);
    std::signal(SIGCONT, cromfs_alarm);
}

static void cromfs_setup_alarm(cromfs& obj)
{
    /*
    static struct itimerval timer;

    // Check the current status of the timer
    getitimer(ITIMER_REAL, &timer);
    if(timer.it_value.tv_sec > BLKTAB_CACHE_TIMEOUT/2)
    {
        return;
    }
    */

    // Will setup a signal that will free the memory consumed by blktab
    // after a period of inuse
    cromfs_alarm_obj = &obj;
    if(BLKTAB_CACHE_TIMEOUT) alarm(BLKTAB_CACHE_TIMEOUT);
    /*
    timer.it_interval.tv_sec  = 0;
    timer.it_interval.tv_usec = 0;
    timer.it_value.tv_sec  = BLKTAB_CACHE_TIMEOUT;
    timer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
    */
}

void cromfs::reread_blktab()
        throw (cromfs_exception, std::bad_alloc)
{
    std::vector<unsigned char> blktab_data =
        DoLZMALoading(fd, sblock.blktab_offs, sblock.blktab_size);

    /* As we normally read blktab only once, it's safe
     * to forget it from page cache now.
     */
    FadviseDontNeed(fd, sblock.blktab_offs, sblock.blktab_size);

    /* Decode the blktab. */

    unsigned onesize = DATALOCATOR_SIZE_BYTES();
    if(storage_opts & CROMFS_OPT_PACKED_BLOCKS)
    {
        blktab.resize(blktab_data.size() / onesize);
        for(unsigned a=0; a<blktab.size(); ++a)
        {
            uint_fast32_t value = R32(&blktab_data[a*onesize]);
            uint_fast32_t fblocknum = value / CROMFS_FSIZE,
                          startoffs = value % CROMFS_FSIZE;

            //fprintf(stderr, "P block %u defined as %u:%u\n", a, (unsigned)fblocknum, (unsigned)startoffs);
            blktab[a].define(fblocknum, startoffs/*, CROMFS_BSIZE,CROMFS_FSIZE*/);
        }
    }
    else
    {
        blktab.resize(blktab_data.size() / onesize);
        for(unsigned a=0; a<blktab.size(); ++a)
        {
            uint_fast32_t fblocknum = R32(&blktab_data[a*onesize+0]),
                          startoffs = R32(&blktab_data[a*onesize+4]);
            //fprintf(stderr, "NP block %u defined as %u:%u\n", a, (unsigned)fblocknum, (unsigned)startoffs);
            blktab[a].define(fblocknum, startoffs/*, CROMFS_BSIZE,CROMFS_FSIZE*/);
        }
    }

#if READBLOCK_DEBUG >= 2
    for(unsigned a=0; a<blktab.size(); ++a)
    {
        fprintf(stderr, "BLOCK %u: %s\n", a, DumpBlock(blktab[a]).c_str());
    }
#endif

    MadviseRandom(&blktab[0], blktab.size()*sizeof(blktab[0]));

    cromfs_setup_alarm(*this);
}

cromfs_inode_internal cromfs::read_special_inode
    (uint_fast64_t offset, uint_fast64_t size,
     bool ignore_blocks)
        throw (cromfs_exception, std::bad_alloc)
{
    cromfs_inode_internal inode;

    switch(sblock.sig)
    {
        default:
        case CROMFS_SIGNATURE_03:
        case CROMFS_SIGNATURE_02:
        {
            const char* inodename = 0;
            if(offset == sblock.rootdir_offs)
                { inodename = "rootdir"; /* ok */ }
            else if(offset == sblock.inotab_offs)
                { inodename = "inotab"; /* ok */ }
            else
            {
                // unknown inode
#if INODE_DEBUG
                fprintf(stderr, "Unknown inode: %llx\n", (unsigned long long)offset);
#endif
                throw EIO;
            }
            std::vector<unsigned char> Buf = DoLZMALoading(fd, offset+0, size);

#if INODE_DEBUG
            fprintf(stderr, "Read inode %s. Ignore blocks = %s\n",
                inodename,
                ignore_blocks ? "true" : "false");
#endif

            /*fprintf(stderr, "inode_buf[");
            for(unsigned c=0; c<32; ++c) fprintf(stderr, "%c", Buf[c]);
            fprintf(stderr, "]\n");
            fflush(stderr);*/

            get_inode(&Buf[0], 0, inode, storage_opts, CROMFS_BSIZE, !ignore_blocks);

            break;
        }
        case CROMFS_SIGNATURE_01:
        {
            unsigned char Buf[MAX_INODE_HEADER_SIZE];
            ( LongFileRead(fd, offset+0, 0x18, Buf) );

            get_inode_header(Buf, 0x18, inode, 0, CROMFS_BSIZE);

#if INODE_DEBUG
            printf("read_special_inode(%lld): %s\n",
                (long long)offset,
                DumpInode(inode).c_str());
#endif

            if(!ignore_blocks)
            {
                uint_fast64_t nblocks = CalcSizeInBlocks(inode.bytesize, inode.blocksize);
                inode.blocklist.resize(nblocks);

                LongFileRead reader(fd, offset+0x18, 4*nblocks);
                for(unsigned n=0; n<nblocks; ++n)
                    inode.blocklist[n] = R32(reader.GetAddr() + n*4);
            }
        }
    }
    return inode;
}

const cromfs_inode_internal cromfs::read_inode(cromfs_inodenum_t inodenum)
    throw (cromfs_exception, std::bad_alloc)
{
    if(inodenum == 1)
    {
#if INODE_DEBUG
        fprintf(stderr, "returning rootdir: %s\n", DumpInode(rootdir).c_str());
#endif
        DumpRAMusage();
        return rootdir;
    }
    if(unlikely(inodenum < 1)) throw EBADF;

    unsigned char Buf[MAX_INODE_HEADER_SIZE];
    read_file_data(inotab, GetInodeOffset(inodenum), Buf, INODE_HEADER_SIZE(), "inode");

    cromfs_inode_internal inode;
    get_inode_header(Buf, INODE_HEADER_SIZE(), inode, storage_opts, CROMFS_BSIZE);

#if INODE_DEBUG
    fprintf(stderr, "returning inode %llu: %s\n",
        (unsigned long long)inodenum,
        DumpInode(inode).c_str());
#endif
    return inode;
}

const cromfs_inode_internal cromfs::read_inode_and_blocks(cromfs_inodenum_t inodenum)
    throw (cromfs_exception, std::bad_alloc)
{
    if(inodenum == 1)
    {
#if INODE_DEBUG
        fprintf(stderr, "returning rootdir: %s\n", DumpInode(rootdir).c_str());
#endif
        return rootdir;
    }

    /* Note: This function is largely a duplicate
     * of get_inode(), except that it uses read_file_data()
     * instead of buffer access. (FIXME, duplicate code)
     */

    cromfs_inode_internal result = read_inode(inodenum);

    uint_fast64_t nblocks = CalcSizeInBlocks(result.bytesize, result.blocksize);

    //fprintf(stderr, "%llu bytes: %llu blocks\n",
    //    (unsigned long long)result.bytesize, (unsigned long long)nblocks);

    uint_fast64_t inode_blocktable_offset = GetInodeOffset(inodenum) + INODE_HEADER_SIZE();

    result.blocklist.resize(nblocks);

    const unsigned b = BLOCKNUM_SIZE_BYTES();
    if(b != 4)
    {
        std::vector<unsigned char> blocklist(b * nblocks);

        read_file_data(inotab, inode_blocktable_offset,
                       &blocklist[0], b * nblocks,
                       "inode block table");

        for(unsigned n=0; n<nblocks; ++n)
            result.blocklist[n] = Rn(&blocklist[n * b], b);
    }
    else
    {
        read_file_data(inotab, inode_blocktable_offset,
                       (unsigned char*)&result.blocklist[0], 4 * nblocks,
                       "inode block table");

        /* Fix endianess after raw read */
        for(unsigned n=0; n<nblocks; ++n)
            result.blocklist[n] = R32(&result.blocklist[n]);
    }

    return result;
}

void cromfs::read_block(cromfs_blocknum_t ind,
                        uint_fast32_t offset,
                        unsigned char* target,
                        uint_fast32_t size)
        throw (cromfs_exception, std::bad_alloc)
{
    if(blktab.empty()) reread_blktab();
    cromfs_setup_alarm(*this);
    cromfs_block_internal& block = blktab[ind];

#if READBLOCK_DEBUG
    fprintf(stderr, "- - read_block(%u,%u,%p,%u): block=%s\n",
        (unsigned)ind,
        (unsigned)offset,
        target, (unsigned)size,
        DumpBlock(block).c_str());
#endif

    cromfs_cached_fblock& fblock =
        read_fblock(block.get_fblocknum(CROMFS_BSIZE,CROMFS_FSIZE));

    const uint_fast32_t startoffs =
        block.get_startoffs(CROMFS_BSIZE,CROMFS_FSIZE)
         + offset;

    if(startoffs < fblock.size())
    {
#if READBLOCK_DEBUG
        fprintf(stderr, "- - - got fblock of %u bytes, reading %u from %u\n",
            (unsigned)fblock.size(), (unsigned)size, (unsigned)(startoffs));
#endif

        uint_fast32_t bytes = std::min(fblock.size() - startoffs, size);
        std::memcpy(target, &fblock[startoffs], bytes);
    }
}

cromfs_cached_fblock& cromfs::read_fblock(cromfs_fblocknum_t fblocknum)
        throw (cromfs_exception, std::bad_alloc)
{
    cromfs_cached_fblock* result = fblock_cache.Find(fblocknum);
    if(result) return *result;

    cromfs_cached_fblock b = read_fblock_uncached(fblocknum);

    result = &fblock_cache.Put(fblocknum, b);

    return *result;
}

cromfs_cached_fblock cromfs::read_fblock_uncached(cromfs_fblocknum_t fblocknum) const
        throw (cromfs_exception, std::bad_alloc)
{
    if(fblocknum >= fblktab.size())
    {
        throw ENODATA;
    }

    const uint_fast32_t comp_size = fblktab[fblocknum].length;
    const uint_fast64_t filepos   = fblktab[fblocknum].filepos;

#if FBLOCK_DEBUG
    fprintf(stderr, "- - - - reading fblock %u (%u bytes) from %llu\n",
        (unsigned)fblocknum, (unsigned)comp_size,
        (unsigned long long)filepos);
#endif
    return DoLZMALoading(fd, filepos, comp_size);
}

int_fast64_t cromfs::read_file_data(
    cromfs_inodenum_t inonum,
    uint_fast64_t offset,
    unsigned char* target, uint_fast64_t size,
    const char*purpose)
    throw (cromfs_exception, std::bad_alloc)
{
    return read_file_data(
        inonum==1 ? get_root_inode () : read_inode_and_blocks(inonum),
        offset, target, size, purpose);
}



int_fast64_t cromfs::read_file_data_from_one_fblock_only(
    const cromfs_inode_internal& inode,
    uint_fast64_t offset,
    unsigned char* target, uint_fast64_t size,
    const cromfs_fblocknum_t allowed_fblocknum
)
    throw (cromfs_exception, std::bad_alloc)
{
    uint_fast64_t result = 0;

#if READFILE_DEBUG >= 2
    fprintf(stderr, "- Accessing fblock %u\n", (unsigned)allowed_fblocknum);
#endif
    const uint_fast64_t bsize = inode.blocksize;

    for(uint_fast64_t pos    = std::min(inode.bytesize, offset),
                      endpos = std::min(inode.bytesize, offset + size);
        pos < endpos; )
    {
        const uint_fast64_t begin_block_index  = pos / bsize;
        const uint_fast32_t begin_block_offset = pos % bsize;
        if(begin_block_index >= inode.blocklist.size()) break;

        const uint_fast64_t remain_block_bytes = bsize - begin_block_offset;

        const uint_fast64_t consume_bytes = std::min(endpos-pos, remain_block_bytes);

        const cromfs_blocknum_t blocknum = inode.blocklist[begin_block_index];
        const cromfs_block_internal& block = blktab[blocknum];
        const cromfs_fblocknum_t fblocknum = block.get_fblocknum(CROMFS_BSIZE,CROMFS_FSIZE);

        if(fblocknum != allowed_fblocknum)
        {
            pos += consume_bytes;
            continue;
        }

#if READFILE_DEBUG >= 2
        fprintf(stderr,
            "- File offset %llu (block %u(b%u f%u) @%u, consume %llu bytes of %llu) -> %p\n",
                (unsigned long long)offset,
                (unsigned)begin_block_index,
                (unsigned)blocknum,
                (unsigned)fblocknum,
                (unsigned)begin_block_offset,
                (unsigned long long)consume_bytes,
                (unsigned long long)size,
                target + (pos-offset));
#endif
        /*
        const uint_fast32_t block_size =
            std::min( (uint_fast64_t)bsize,
                      (uint_fast64_t)(inode.bytesize - (pos - pos % bsize) ));
        */
        read_block(blocknum,
                   begin_block_offset,
                   target + (pos-offset),
                   consume_bytes);

#if READFILE_DEBUG >= 2
        if(consume_bytes <= 50)
        {
            fprintf(stderr, "- - Got:");

            for(unsigned a=0; a<consume_bytes; ++a)
            {
                //if(a == consume_bytes) fprintf(stderr, " [");
                fprintf(stderr, " %02X", (target + (pos-offset)) [a]);
            }
            fprintf(stderr, "\n");
        }
#endif

        pos    += consume_bytes;
        result += consume_bytes;
    }
    return result;
}

int_fast64_t cromfs::read_file_data(
    const cromfs_inode_internal& inode,
    uint_fast64_t offset,
    unsigned char* target, uint_fast64_t size,
    const char*
#if READFILE_DEBUG
                purpose
#endif
)
    throw (cromfs_exception, std::bad_alloc)
{
#if READFILE_DEBUG
    fprintf(stderr,
        "read_file_data, offset=%llu, target=%p, size=%llu = %s\n",
        (unsigned long long)offset,
        target,
        (unsigned long long)size,
        purpose);
#endif
#if READFILE_DEBUG >= 2
    fprintf(stderr, "- source inode: %s\n", DumpInode(inode).c_str());
#endif
    if(blktab.empty()) reread_blktab();
    cromfs_setup_alarm(*this);

    std::vector<cromfs_fblocknum_t> required_fblocks_cached;
    std::vector<cromfs_fblocknum_t> required_fblocks_uncached;

    const uint_fast64_t bsize = inode.blocksize;

    /* Collect a list of fblocks that are required to complete this read. */
    if(true) /* scope */
    {
        const unsigned bitset_bitness = sizeof(unsigned long)*8; //__WORDSIZE;

        /* Using an adhoc bitset here.
         * std::set<cromfs_fblocknum_t>  would also be an option, but bitset
         * is more optimal when we know the maximum value of the entries
         * (which is generally rather small, around 400 at maximum).
         */
        std::vector<unsigned long> required_fblocks_set
            ( (fblktab.size() + bitset_bitness-1) / bitset_bitness );

        for(uint_fast64_t pos    = std::min(inode.bytesize, offset),
                          endpos = std::min(inode.bytesize, offset + size);
            pos < endpos; )
        {
            const uint_fast64_t begin_block_index  = pos / bsize;
            const uint_fast32_t begin_block_offset = pos % bsize;
            if(begin_block_index >= inode.blocklist.size()) break;

            const uint_fast64_t remain_block_bytes = bsize - begin_block_offset;

            const uint_fast64_t consume_bytes = std::min(endpos-pos, remain_block_bytes);

            const cromfs_blocknum_t blocknum = inode.blocklist[begin_block_index];
            const cromfs_block_internal& block = blktab[blocknum];
            const cromfs_fblocknum_t fblocknum = block.get_fblocknum(CROMFS_BSIZE,CROMFS_FSIZE);

            if(fblocknum < fblktab.size())
            {
                const unsigned fblock_bit_index = fblocknum / bitset_bitness;
                const unsigned fblock_bit_value = 1U << (fblocknum % bitset_bitness);

                if(! (required_fblocks_set[fblock_bit_index] & fblock_bit_value) )
                {
                    required_fblocks_set[fblock_bit_index] |= fblock_bit_value;

                    if(fblock_cache.Has(fblocknum))
                    {
                        required_fblocks_cached.push_back(fblocknum);
                    }
                    else
                    {
                        required_fblocks_uncached.push_back(fblocknum);
                        // Initiate background reading for them.
                        FadviseWillNeed(fd, fblktab[fblocknum].filepos,
                                            fblktab[fblocknum].length);
                    }
                }
            }
            pos += consume_bytes;
        }
    }

#if READFILE_DEBUG >= 2
    fprintf(stderr, "- List of fblocks to access:");
    for(unsigned a=0, b=required_fblocks_uncached.size(); a<b; ++a)
    {
        const cromfs_fblocknum_t fblocknum = required_fblocks_uncached[a];
        fprintf(stderr, " %u (cached)", (unsigned)required_fblocks_uncached[a]);
    }
    for(unsigned a=0, b=required_fblocks_cached.size(); a<b; ++a)
    {
        const cromfs_fblocknum_t fblocknum = required_fblocks_cached[a];
        fprintf(stderr, " %u (uncached)", (unsigned)required_fblocks_cached[a]);
    }
    fprintf(stderr, "\n");
#endif

    uint_fast64_t result = 0;

#pragma omp parallel reduction(+:result)
  {
    /* Note: Using ssize_t instead of size_t here because "omp for"
     *       requires a signed iteration variable instead of unsigned.
     */
    const ssize_t n_req_fblocks_cached   = required_fblocks_cached.size();
    const ssize_t n_req_fblocks_uncached = required_fblocks_uncached.size();
  #pragma omp for nowait
    for(ssize_t a=0; a<n_req_fblocks_cached; ++a)
    {
        const cromfs_fblocknum_t allowed_fblocknum = required_fblocks_cached[a];

        uint_fast64_t num_read =
            read_file_data_from_one_fblock_only
                (inode, offset, target, size, allowed_fblocknum);

          result += num_read;
    }

  #pragma omp for nowait
    for(ssize_t a=0; a<n_req_fblocks_uncached; ++a)
    {
        const cromfs_fblocknum_t allowed_fblocknum = required_fblocks_uncached[a];

        uint_fast64_t num_read =
            read_file_data_from_one_fblock_only
                (inode, offset, target, size, allowed_fblocknum);

        result += num_read;
    }
  }

    fblock_cache.CheckAges(-1);

    return result;
}

static const cromfs_dirinfo get_dirinfo_portion(const cromfs_dirinfo& full,
                                                uint_fast32_t dir_offset,
                                                uint_fast32_t dir_count)
{
    cromfs_dirinfo::const_iterator j = full.begin();

    for(; dir_offset > 0 && j != full.end(); ++j, --dir_offset) ;

    cromfs_dirinfo::const_iterator first = j;

    for(; dir_count > 0 && j != full.end(); ++j, --dir_count) ;

    return cromfs_dirinfo(first, j);
}

const cromfs_dirinfo cromfs::read_dir(cromfs_inodenum_t inonum,
                                      uint_fast32_t dir_offset,
                                      uint_fast32_t dir_count)
    throw (cromfs_exception, std::bad_alloc)
{
    cromfs_dirinfo* cached = readdir_cache.Find(inonum);
    if(cached)
    {
        if(dir_offset == 0 && dir_count >= cached->size())
        {
            return *cached;
        }
        return get_dirinfo_portion(*cached, dir_offset, dir_count);
    }

    const cromfs_inode_internal inode = read_inode_and_blocks(inonum);
    if(inonum != 1 && !S_ISDIR(inode.mode))
        { throw ENOTDIR; }

    cromfs_dirinfo result;

    unsigned char DirHeader[4];
    if(read_file_data(inode, 0, DirHeader, 4, "dir size") < 4)
    {
#if READDIR_DEBUG
        fprintf(stderr, "read_dir(%s)(%u,%u)\n",
            DumpInode(inode).c_str(),
            (unsigned)dir_offset, (unsigned)dir_count);
        fprintf(stderr, "- directory has no blocks\n");
#endif
        //fprintf(stderr, "GORE!!! 3\n");
        throw EIO;
    }
    uint_fast32_t num_files = R32(DirHeader);

#if READDIR_DEBUG
    fprintf(stderr, "read_dir(%s)(%u,%u): num_files=%u\n",
        DumpInode(inode).c_str(),
        (unsigned)dir_offset, (unsigned)dir_count,
        (unsigned)num_files);
#endif

    if(dir_offset < num_files)
    {
        unsigned num_to_read = std::min(num_files - dir_offset, dir_count);
        if(num_to_read > 0)
        {
            std::vector<uint_least32_t> entry_offsets(num_to_read+1);

            unsigned num_addrs_to_read = num_to_read;

            if(dir_offset + num_to_read == num_files)
            {
                // End of name region
                entry_offsets[num_to_read] = inode.bytesize;
            }
            else
            {
                ++num_addrs_to_read;
            }

            /* Read the address table */
            read_file_data(
                inode, 4 + dir_offset*4,
                (unsigned char*)&entry_offsets[0],
                4 * num_addrs_to_read,
                "dir offset list");

            /* Fix endianess after raw read */
            for(unsigned n=0; n<num_addrs_to_read; ++n)
                entry_offsets[n] = R32(&entry_offsets[n]);

            const unsigned name_buf_size = entry_offsets[num_to_read] - entry_offsets[0];
            std::vector<unsigned char> name_buf(name_buf_size);

            read_file_data(inode,
                           entry_offsets[0],
                           &name_buf[0],
                           name_buf.size(),
                           "dir entries");

            for(unsigned a=0; a<num_to_read; ++a)
            {
                const int name_offs = entry_offsets[a] - entry_offsets[0];

                if(name_offs < 0
                || (unsigned)name_offs+9 >= name_buf.size()
                ||    (a > 0 && entry_offsets[a] < entry_offsets[a-1])
                  )
                {
                error_entry:
                    fprintf(stderr, "Entry %u: offs is %d, prev is %d, next is %d\n",
                        a,
                        entry_offsets[a],
                        a>0 ? entry_offsets[a-1] : 0,
                        a+1<entry_offsets.size() ? entry_offsets[a+1] : 0);
                    // error
                    throw EIO;
                }

                const unsigned name_room = name_buf.size() - name_offs;

                const unsigned char* name_data = &name_buf[name_offs];

                uint_fast64_t inonumber   = R64(&name_data[0]);
                const unsigned char* filename = &name_data[8];

                /* The name must be terminated by a nul pointer. */
                const unsigned char* nul_pointer =
                    (const unsigned char*)
                    std::memchr(filename,
                                '\0',
                                name_room);
                if(!nul_pointer)
                {
                    fprintf(stderr, "Entry %u has no nul pointer\n", a);
                    goto error_entry;
                }

                result[std::string(filename, nul_pointer)] = inonumber;
            }
        }
    }

    if(dir_offset == 0 && dir_count >= num_files)
    {
        readdir_cache.CheckAges(-1);
        readdir_cache.Put(inonum, result);
    }
    return result;
}

cromfs_inodenum_t cromfs::dir_lookup(cromfs_inodenum_t inonum,
                                     const std::string& search_name)
    throw (cromfs_exception, std::bad_alloc)
{
    cromfs_dirinfo* cached = readdir_cache.Find(inonum);
    if(cached)
    {
        const cromfs_dirinfo& info = *cached;
        cromfs_dirinfo::const_iterator j = info.find(search_name);
        if(j != info.end()) return j->second;
        return 0;
    }

    const cromfs_inode_internal inode = read_inode_and_blocks(inonum);
    if(inonum != 1 && !S_ISDIR(inode.mode))
        { throw ENOTDIR; }

    unsigned char DirHeader[4];
    if(read_file_data(inode, 0, DirHeader, 4, "dir size") < 4)
    {
#if READDIR_DEBUG
        fprintf(stderr, "dir_lookup(%s)\n",
            DumpInode(inode).c_str());
        fprintf(stderr, "- directory has no blocks\n");
#endif
        //fprintf(stderr, "GORE!!! 3\n");
        throw EIO;
    }
    uint_fast32_t num_files = R32(DirHeader);

    std::vector<uint_least32_t> entry_offsets(num_files+1);
    std::vector<bool>           offs_read(num_files);

    entry_offsets[num_files] = inode.bytesize;

#if 0
    /* Read the address table */
    read_file_data(inode, 4, (unsigned char*)&entry_offsets[0],
                   4 * num_files,
                   "dir offset list");
    /* Fix endianess after raw read */
    for(unsigned n=0; n<num_files; ++n)
    {
        entry_offsets[n] = R32(&entry_offsets[n]);
        offs_read[n] = true;
    }
#endif

    /* Use binary search to find the directory entry. */
    unsigned len = num_files, first = 0, last = len;
    while(len > 0)
    {
        const unsigned half   = len / 2;
        const unsigned middle = first + half;

        if(true)
        {
            /* Check if we need to read a pointer or two from the disk image. */
            unsigned read_offs = 0;
            unsigned n_read = 0;

            if(!offs_read[middle]) n_read += 4; else read_offs += 4;
            if(middle+1 < last && !offs_read[middle+1]) n_read += 4;

            if(n_read)
            {
                read_file_data(inode, 4 + read_offs + middle*4,
                               (unsigned char*)&entry_offsets[middle] + read_offs,
                               n_read,
                               "dir offset list");
                if(read_offs == 0)
                {
                    entry_offsets[middle] = R32(&entry_offsets[middle]);
                    offs_read[middle] = true;
                }
                if(n_read == 8 || read_offs == 4)
                {
                    entry_offsets[middle+1] = R32(&entry_offsets[middle+1]);
                    offs_read[middle+1] = true;
                }
            }
        }

        const unsigned offs_this    = entry_offsets[middle];
        const unsigned offs_next    = entry_offsets[middle+1];

        if(offs_this+9 >= offs_next
        || offs_this >= inode.bytesize
        || offs_next > inode.bytesize)
        {
            throw EIO;
        }

        unsigned entry_length = offs_next - offs_this;
        std::vector<unsigned char> entry_buf(entry_length);
        const unsigned name_room = entry_length - 8;

        read_file_data(inode, offs_this,
                       &entry_buf[0],
                       entry_buf.size(),
                       "dir entry");

        uint_fast64_t inonumber   = R64(&entry_buf[0]);
        const unsigned char* filename = &entry_buf[8];

        /* The name must be terminated by a nul pointer. */
        const unsigned char* nul_pointer =
            (const unsigned char*)
            std::memchr(filename,
                        '\0',
                        name_room);
        if(!nul_pointer) throw EIO;

        int c = search_name.compare( (const char*) filename );

        if(!c)
        {
            return inonumber;
        }
        if(c > 0)
        {
            first = middle + 1;
            len = len - half - 1;
        }
        else
            len = half;
    }
    return 0;
}

void cromfs::DumpRAMusage() const
{
    fprintf(stderr,
        "-- cromfs RAM use report --\n"
        "rootdir inode size: %s (%u blocks)\n"
        "inotab inode size: %s (%u blocks)\n"
        "fblktab size: %s (%u fblock locators)\n"
        "blktab size: %s (%u data locators)\n"
        "readdir cache size: %s (estimate, %u directories)\n"
        "fblock cache size: %s (estimate, %u fblocks)\n",
        ReportSize( sizeof(rootdir) + rootdir.blocklist.size() * sizeof(cromfs_blocknum_t) ).c_str(),
        (unsigned)rootdir.blocklist.size(),
        ReportSize( sizeof(inotab) + inotab.blocklist.size() * sizeof(cromfs_blocknum_t) ).c_str(),
        (unsigned)inotab.blocklist.size(),
        ReportSize( fblktab.size() * sizeof(fblktab[0]) ).c_str(),
        (unsigned)fblktab.size(),
        ReportSize( blktab.size() * sizeof(blktab[0]) ).c_str(),
        (unsigned)blktab.size(),
        ReportSize( 60000 * readdir_cache.num_entries() ).c_str(),
        (unsigned)readdir_cache.num_entries(),
        ReportSize( CROMFS_FSIZE * fblock_cache.num_entries() ).c_str(),
        (unsigned)fblock_cache.num_entries()
    );
}

cromfs::cromfs(int fild)
    throw (cromfs_exception, std::bad_alloc)
     : fd(fild),
       rootdir(),inotab(),sblock(),fblktab(),blktab(), // -Weffc++
       readdir_cache(READDIR_CACHE_MAX_SIZE, 0),
       fblock_cache(FBLOCK_CACHE_MAX_SIZE, 0),
       storage_opts()
{
}

cromfs::~cromfs() throw()
{
    cromfs_alarm_obj = NULL;
}
