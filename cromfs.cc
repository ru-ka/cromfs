/*
cromfs - Copyright (C) 1992,2007 Bisqwit (http://iki.fi/bisqwit/)
Licence: GPL

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

#include "lib/LzmaDecode.h"
#include "lib/LzmaDecode.c"
#include "lib/datareadbuf.hh"
#include "lib/bwt.hh"
#include "lib/mtf.hh"
#include "lib/mmapping.hh"
#include "cromfs.hh"

#define CROMFS_FSIZE  (sblock.fsize)
#define CROMFS_BSIZE (sblock.bsize)

#define BLOCKNUM_SIZE_BYTES() \
   (4 - 1*!!(storage_opts & CROMFS_OPT_24BIT_BLOCKNUMS) \
      - 2*!!(storage_opts & CROMFS_OPT_16BIT_BLOCKNUMS) )

#define SBLOCK_DEBUG    0
#define READBLOCK_DEBUG 0
#define FBLOCK_DEBUG    0
#define INODE_DEBUG     0
#define READFILE_DEBUG  0
#define READDIR_DEBUG   0
#define DEBUG_INOTAB    0

#define BLKTAB_CACHE_TIMEOUT 0

/* How many directories to keep cached at the same time maximum */
static const unsigned READDIR_CACHE_MAX_SIZE = 5;

/* How many decompressed fblocks to cache in RAM at the same time maximum */
static const unsigned FBLOCK_CACHE_MAX_SIZE = 20;

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

const std::string DumpInode(const cromfs_inode_internal& inode)
{
    std::stringstream s;
    
    s << "mode(";
    
    if(inode.mode == 0x12345678) s << "inotab";
    else s << "0" << std::oct << inode.mode;
    
    s << ")time(" << std::dec << inode.time
      << ")links(" << inode.links
      << ")rdev(" << std::hex << inode.rdev
      << ")size(" << std::dec << inode.bytesize;
   
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

static const std::vector<unsigned char> LZMADeCompress
    (const unsigned char* buf, size_t BufSize)
{
    if(BufSize <= 5+8) return std::vector<unsigned char> ();
    
    uint_least64_t out_sizemax = R64(&buf[5]);
    
    std::vector<unsigned char> result(out_sizemax);
    
    CLzmaDecoderState state;
    LzmaDecodeProperties(&state.Properties, &buf[0], LZMA_PROPERTIES_SIZE);
    state.Probs = new CProb[LzmaGetNumProbs(&state.Properties)];
    
    SizeT in_done;
    SizeT out_done;
    LzmaDecode(&state, &buf[13], BufSize-13, &in_done,
               &result[0], result.size(), &out_done);
    
    delete[] state.Probs;
    
    result.resize(out_done);
    return result;
}

static const std::vector<unsigned char> LZMADeCompress
    (const std::vector<unsigned char>& buf)
{
    return LZMADeCompress(&buf[0], buf.size());
}

uint_fast64_t cromfs::CalcSizeInBlocks(uint_fast64_t filesize) const
{
    return (filesize + CROMFS_BSIZE-1) / CROMFS_BSIZE;
}

void cromfs::reread_superblock()
{
    cromfs_superblock_internal::BufferType Superblock;
    if(pread64(fd, Superblock, sizeof(Superblock), 0) == -1) throw errno;
    
    storage_opts = 0;
    
    sblock.ReadFromBuffer(Superblock);
    
    const uint_fast64_t sig  = sblock.sig;
    if(sig != CROMFS_SIGNATURE_01
    && sig != CROMFS_SIGNATURE_02
    && sig != CROMFS_SIGNATURE_03
    ) throw EINVAL;
    
    cache_fblocks.clear();
    
#if SBLOCK_DEBUG
    fprintf(stderr,
        "Superblock signature %llX\n"
        "BlockTab at 0x%llX\n"
        "FBlkTab at 0x%llX\n"
        "inotab at 0x%llX (size 0x%llX)\n"
        "rootdir at 0x%llX (size 0x%llX)\n"
        "FSIZE %u  BSIZE %u\n"
        "%u fblocks, %u blocks\n",
        (unsigned long long)sblock.sig,
        (unsigned long long)sblock.blktab_offs,
        (unsigned long long)sblock.fblktab_offs,
        (unsigned long long)sblock.inotab_offs,  (unsigned long long)sblock.inotab_size,
        (unsigned long long)sblock.rootdir_offs, (unsigned long long)sblock.rootdir_size,
        (unsigned)CROMFS_FSIZE,
        (unsigned)CROMFS_BSIZE,
        (unsigned)fblktab.size(),
        (unsigned)blktab.size());
#endif
    
    bool need_storage_opts = 
       sblock.sig != CROMFS_SIGNATURE_01
    && sblock.sig != CROMFS_SIGNATURE_02;
    
    /* If we need storage options, we cannot read the block list
     * of an inode before knowing with which options it must be
     * decoded. Hence we first read the inode header, then
     * read the full data after storage_opts is set.
     */
    inotab  = read_special_inode(sblock.inotab_offs,  sblock.inotab_size, need_storage_opts);
    
    storage_opts = inotab.mode;
    if(!need_storage_opts)
    {
        storage_opts = 0; // not supported in these versions
    }
    
    if(need_storage_opts)
    {
        inotab = read_special_inode(sblock.inotab_offs,  sblock.inotab_size, false);
    }

    rootdir = read_special_inode(sblock.rootdir_offs, sblock.rootdir_size, false);
    
    rootdir.mode = S_IFDIR | 0555;
    if(sig == CROMFS_SIGNATURE_01)
        rootdir.links = 2;
    rootdir.uid = 0;
    rootdir.gid = 0;
    
    forget_blktab();
    reread_blktab();
    
    reread_fblktab();
    
    if(fblktab.empty()) throw EINVAL;
    
#if DEBUG_INOTAB
    fprintf(stderr, "rootdir inode: %s\n", DumpInode(rootdir).c_str());
    fprintf(stderr, "inotab inode: %s\n", DumpInode(inotab).c_str());
    
    cromfs_inodenum_t maxinode = 2+inotab.bytesize/4;
    for(cromfs_inodenum_t a=2; a<maxinode; )
    {
        cromfs_inode_internal result = read_inode_and_blocks(a);
        fprintf(stderr, "INODE %u: %s\n", (unsigned)a, DumpInode(result).c_str());
        uint_fast64_t nblocks = CalcSizeInBlocks(result.bytesize);
        
        unsigned inode_size = 0x18 + BLOCKNUM_SIZE_BYTES()*nblocks;
        inode_size = (inode_size + 3) & ~3;
        a += inode_size / 4;
    }
#endif
}

void cromfs::reread_fblktab()
{
    fblktab.clear();
    
    uint_fast64_t startpos = sblock.fblktab_offs;
    for(;;)
    {
        unsigned char Buf[17];
        ssize_t r = pread64(fd, Buf, 17, startpos);
        if(r == 0) break;
        if(r < 0) throw errno;
        if(r < 17) throw EINVAL;
        
        cromfs_fblock_internal fblock;
        fblock.filepos = startpos+4;
        fblock.length  = R32(Buf);
        uint_fast64_t orig_length = R64(Buf+9);
        
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
    }
}

void cromfs::forget_blktab()
{
    blktab = std::vector<cromfs_block_internal>();
    // clear() won't free memory, reserve() can only increase size
}

static cromfs* cromfs_alarm_obj = NULL;
static void cromfs_alarm(int sig=0) __attribute__((constructor));
static void cromfs_alarm(int sig)
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
{
    std::vector<unsigned char> blktab_data(sblock.blktab_size);
    
    ssize_t r = pread64(fd, &blktab_data[0], blktab_data.size(), sblock.blktab_offs);
    if(r != (ssize_t)blktab_data.size())
    {
        throw EINVAL;
    }

    blktab_data = LZMADeCompress(blktab_data);

    unsigned onesize = (storage_opts & CROMFS_OPT_PACKED_BLOCKS) ? 4 : 8;
    if(storage_opts & CROMFS_OPT_PACKED_BLOCKS)
    {
        blktab.resize(blktab_data.size() / onesize);
        for(unsigned a=0; a<blktab.size(); ++a)
        {
            uint_fast32_t value = R32(&blktab_data[a*onesize]);
            uint_fast32_t fblocknum = value / CROMFS_FSIZE,
                          startoffs = value % CROMFS_FSIZE;

            //fprintf(stderr, "P block %u defined as %u:%u\n", a, (unsigned)fblocknum, (unsigned)startoffs);
            blktab[a].define(fblocknum, startoffs, CROMFS_BSIZE,CROMFS_FSIZE);
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
            blktab[a].define(fblocknum, startoffs, CROMFS_BSIZE,CROMFS_FSIZE);
        }
    }
    
#if FBLOCK_DEBUG
    for(unsigned a=0; a<blktab.size(); ++a)
    {
        fprintf(stderr, "BLOCK %u: %s\n", a, DumpBlock(blktab[a]).c_str());
    }
#endif

    cromfs_setup_alarm(*this);
}

static void ExtractInodeHeader(cromfs_inode_internal& inode, const unsigned char* Buf)
{
    uint_fast32_t rdev_links;
    
    inode.mode    = R32(Buf+0x0000);
    inode.time    = R32(Buf+0x0004);
    rdev_links    = R32(Buf+0x0008);
    inode.uid     = R16(Buf+0x000C);
    inode.gid     = R16(Buf+0x000E);
    inode.bytesize= R64(Buf+0x0010);
    
    if(S_ISCHR(inode.mode) || S_ISBLK(inode.mode))
        { inode.links = 1; inode.rdev = rdev_links; }
    else
        { inode.links = rdev_links; inode.rdev = 0; }
    
    // if(S_ISDIR(inode.mode)) inode.links += 2; /* For . and .. */
}

cromfs_inode_internal cromfs::read_special_inode
    (uint_fast64_t offset, uint_fast64_t size,
     bool ignore_blocks)
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
            std::vector<unsigned char> Buf(size);
            if(pread64(fd, &Buf[0], size, offset+0) == -1) throw errno;
            Buf = LZMADeCompress(&Buf[0], size);
            
            ExtractInodeHeader(inode, &Buf[0]);

#if INODE_DEBUG
            fprintf(stderr, "Read inode %s. Size %llu. Ignore blocks = %s\n",
                inodename,
                (unsigned long long)inode.bytesize,
                ignore_blocks ? "true" : "false");
#endif
            if(!ignore_blocks)
            {
                uint_fast64_t nblocks = CalcSizeInBlocks(inode.bytesize);
                inode.blocklist.resize(nblocks);
                
                unsigned block_bytesize = BLOCKNUM_SIZE_BYTES();
#if INODE_DEBUG
                fprintf(stderr, "block_bytesize=%u\n", block_bytesize);
#endif
                if(Buf.size() < block_bytesize*nblocks+0x18)
                {
                    /* Invalid inode */
#if INODE_DEBUG
                    fprintf(stderr, "Inode offs(0x%llX),size(%llu), buf.size=%u, expected %u (%u blocks)\n",
                        (unsigned long long)offset,
                        (unsigned long long)size,
                        (unsigned)Buf.size(),
                        (unsigned)(block_bytesize*nblocks+0x18),
                        (unsigned)nblocks);
#endif
                    throw EIO;
                }
                
                for(unsigned n=0; n<nblocks; ++n)
                    inode.blocklist[n] = Rn(&Buf[0x18 + block_bytesize*n], block_bytesize);
            }
            break;
        }
        case CROMFS_SIGNATURE_01:
        {
            unsigned char Buf[0x18];
            
            if(pread64(fd, Buf, 0x18, offset+0) == -1) throw errno;
            
            ExtractInodeHeader(inode, Buf);
            
#if INODE_DEBUG
            printf("read_special_inode(%lld): %s\n",
                (long long)offset,
                DumpInode(inode).c_str());
#endif
            
            if(!ignore_blocks)
            {
                uint_fast64_t nblocks = CalcSizeInBlocks(inode.bytesize);
                inode.blocklist.resize(nblocks);
                
                if(pread64(fd, &inode.blocklist[0], 4 * nblocks, offset+0x18) == -1) throw errno;
                
                /* Fix endianess after raw read */
                for(unsigned n=0; n<nblocks; ++n)
                    inode.blocklist[n] = R32(&inode.blocklist[n]);
            }
        }
    }
    return inode;
}

const cromfs_inode_internal cromfs::read_inode(cromfs_inodenum_t inodenum)
{
    if(inodenum == 1)
    {
#if INODE_DEBUG
        fprintf(stderr, "returning rootdir: %s\n", DumpInode(rootdir).c_str());
#endif
        return rootdir;
    }

    unsigned char Buf[0x18];
    read_file_data(inotab, (inodenum-2) * UINT64_C(4), Buf, 0x18, "inode");
    
    cromfs_inode_internal inode;
    ExtractInodeHeader(inode, Buf);
    
#if INODE_DEBUG
    fprintf(stderr, "returning inode %llu: %s\n",
        (unsigned long long)inodenum,
        DumpInode(inode).c_str());
#endif
    return inode;
}

const cromfs_inode_internal cromfs::read_inode_and_blocks(cromfs_inodenum_t inodenum)
{
    if(inodenum == 1)
    {
#if INODE_DEBUG
        fprintf(stderr, "returning rootdir: %s\n", DumpInode(rootdir).c_str());
#endif
        return rootdir;
    }

    cromfs_inode_internal result = read_inode(inodenum);
    
    uint_fast64_t nblocks = CalcSizeInBlocks(result.bytesize);
    
    //fprintf(stderr, "%llu bytes: %llu blocks\n",
    //    (unsigned long long)result.bytesize, (unsigned long long)nblocks);
    
    uint_fast64_t inode_offset = (inodenum-2) * UINT64_C(4) + 0x18;
    
    result.blocklist.resize(nblocks);
    
    const unsigned b = BLOCKNUM_SIZE_BYTES();
    if(b != 4)
    {
        std::vector<unsigned char> blocklist(b * nblocks);

        read_file_data(inotab, inode_offset,
                       &blocklist[0], b * nblocks,
                       "inode block table");

        for(unsigned n=0; n<nblocks; ++n)
            result.blocklist[n] = Rn(&blocklist[n * b], b);
    }
    else
    {
        read_file_data(inotab, inode_offset,
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

    cromfs_cached_fblock& fblock = read_fblock(block.get_fblocknum(CROMFS_BSIZE,CROMFS_FSIZE));
    const uint_fast32_t startoffs =
        block.get_startoffs(CROMFS_BSIZE,CROMFS_FSIZE)
         + offset;
    
    if(startoffs > fblock.size()) return;
    
#if READBLOCK_DEBUG
    fprintf(stderr, "- - - got fblock of %u bytes, reading %u from %u\n",
        (unsigned)fblock.size(), (unsigned)size, (unsigned)(startoffs));
#endif
    
    uint_fast32_t bytes = std::min(fblock.size() - startoffs, size);
    std::memcpy(target, &fblock[startoffs], bytes);
}

cromfs_cached_fblock& cromfs::read_fblock(cromfs_fblocknum_t fblocknum)
{
    fblock_cache_type::iterator i = cache_fblocks.find(fblocknum);

    if(i != cache_fblocks.end())
    {
        return i->second;
    }
    
    if(FBLOCK_CACHE_MAX_SIZE > 0
    && cache_fblocks.size() >= FBLOCK_CACHE_MAX_SIZE)
    {
        /* TODO: instead of the picking one randomly,
         * obsolete the one which was accessed longest time ago
         */
        EraseRandomlyOne(cache_fblocks);
    }
    
    return cache_fblocks[fblocknum] = read_fblock_uncached(fblocknum);
}

cromfs_cached_fblock cromfs::read_fblock_uncached(cromfs_fblocknum_t fblocknum)
{
    if(fblktab.empty()) reread_fblktab();
    
    const uint_fast32_t comp_size = fblktab[fblocknum].length;
    const uint_fast64_t filepos   = fblktab[fblocknum].filepos;
    
    DataReadBuffer Buffer;

#if 1
    MemMapping MMap(fd, filepos, comp_size);
    if(MMap)
    {
        const unsigned char* buf = MMap.get_ptr();
#if FBLOCK_DEBUG
        fprintf(stderr, "- - - - reading fblock %u (%u bytes) from %llu: mmap @ %p\n",
            (unsigned)fblocknum, (unsigned)comp_size,
            (unsigned long long)filepos, buf);
#endif
        Buffer.AssignRefFrom(buf, comp_size);
    }
    else
#endif
    {
        /* mmap failed for some reason, revert to pread64 */
        ssize_t r = Buffer.LoadFrom(fd, comp_size, filepos);
    #if FBLOCK_DEBUG
        fprintf(stderr, "- - - - reading fblock %u (%u bytes) from %llu: got %ld\n",
            (unsigned)fblocknum, (unsigned)comp_size,
            (unsigned long long)filepos, (long)r);
    #endif
        if(r < 0) throw errno;
        if((uint_fast32_t)r < comp_size)
        {
            //fprintf(stderr, "GORE!!! 1\n");
            throw EIO;
        }
    }
    
    std::vector<unsigned char> result = LZMADeCompress(Buffer.Buffer, comp_size);
    if(storage_opts & CROMFS_OPT_USE_MTF) result = MTF_decode(result);
    if(storage_opts & CROMFS_OPT_USE_BWT) result = BWT_decode_embedindex(result);
    return result;
}

int_fast64_t cromfs::read_file_data(const cromfs_inode_internal& inode,
                                    uint_fast64_t offset,
                                    unsigned char* target, uint_fast64_t size,
                                    const char* purpose)
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
    
    std::deque<cromfs_fblocknum_t> required_fblocks;
    
    /* Collect a list of fblocks that are required to complete this read. */
    if(true) /* scope */
    {
        const unsigned bitset_bitness = 32;
        
        /* Using an adhoc bitset here.
         * std::set<cromfs_fblocknum_t>  would also be an option, but bitset
         * is more optimal when we know the maximum value of the entries
         * (which is generally rather small).
         */
        std::vector<uint_least32_t> required_fblocks_set
            ( (fblktab.size() + bitset_bitness-1) / bitset_bitness );

        for(uint_fast64_t pos    = std::min(inode.bytesize, offset),
                          endpos = std::min(inode.bytesize, offset + size);
            pos < endpos; )
        {
            const uint_fast64_t begin_block_index  = pos / CROMFS_BSIZE;
            const uint_fast32_t begin_block_offset = pos % CROMFS_BSIZE;
            if(begin_block_index >= inode.blocklist.size()) break;
            
            const uint_fast64_t remain_block_bytes = CROMFS_BSIZE - begin_block_offset;
            
            const uint_fast64_t consume_bytes = std::min(endpos-pos, remain_block_bytes);

            const cromfs_blocknum_t blocknum = inode.blocklist[begin_block_index];
            const cromfs_block_internal& block = blktab[blocknum];
            const cromfs_fblocknum_t fblocknum = block.get_fblocknum(CROMFS_BSIZE,CROMFS_FSIZE);
            
            const unsigned fblock_bit_index = fblocknum / bitset_bitness;
            const unsigned fblock_bit_value = 1U << (fblocknum % bitset_bitness);
            
            if(! (required_fblocks_set[fblock_bit_index] & fblock_bit_value) )
            {
                required_fblocks_set[fblock_bit_index] |= fblock_bit_value;
                
                fblock_cache_type::iterator i = cache_fblocks.find(fblocknum);
                if(i == cache_fblocks.end())
                    required_fblocks.push_back(fblocknum); // uncached blocks last
                else
                    required_fblocks.push_front(fblocknum); // cached blocks first
            }
            pos += consume_bytes;
        }
    }
    
#if READFILE_DEBUG >= 2
    fprintf(stderr, "- List of fblocks to access:");
    for(unsigned a=0, b=required_fblocks.size(); a<b; ++a)
    {
        const cromfs_fblocknum_t fblocknum = required_fblocks[a];
        fblock_cache_type::iterator i = cache_fblocks.find(fblocknum);
        fprintf(stderr, " %u%s", (unsigned)required_fblocks[a],
            i == cache_fblocks.end() ? "" : "(cached)"
        );
    }
    fprintf(stderr, "\n");
#endif

    uint_fast64_t result = 0;
    for(unsigned a=0, b=required_fblocks.size(); a<b; ++a)
    {
        const cromfs_fblocknum_t allowed_fblocknum = required_fblocks[a];
#if READFILE_DEBUG >= 2
        fprintf(stderr, "- Accessing fblock %u\n", (unsigned)allowed_fblocknum);
#endif
        for(uint_fast64_t pos    = std::min(inode.bytesize, offset),
                          endpos = std::min(inode.bytesize, offset + size);
            pos < endpos; )
        {
            const uint_fast64_t begin_block_index  = pos / CROMFS_BSIZE;
            const uint_fast32_t begin_block_offset = pos % CROMFS_BSIZE;
            if(begin_block_index >= inode.blocklist.size()) break;
            
            const uint_fast64_t remain_block_bytes = CROMFS_BSIZE - begin_block_offset;
            
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
                std::min( (uint_fast64_t)CROMFS_BSIZE,
                          (uint_fast64_t)(inode.bytesize - (pos - pos % CROMFS_BSIZE) ));
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
    }
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

static std::map<cromfs_inodenum_t, cromfs_dirinfo> readdir_cache;

const cromfs_dirinfo cromfs::read_dir(cromfs_inodenum_t inonum,
                                      uint_fast32_t dir_offset,
                                      uint_fast32_t dir_count)
{
    std::map<cromfs_inodenum_t, cromfs_dirinfo>::const_iterator
        i = readdir_cache.find(inonum);
    if(i != readdir_cache.end())
    {
        if(dir_offset == 0 && dir_count >= i->second.size())
        {
            return i->second;
        }
        return get_dirinfo_portion(i->second, dir_offset, dir_count);
    }

    const cromfs_inode_internal inode = read_inode_and_blocks(inonum);
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
                || name_offs+9 >= name_buf.size()
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
        if(READDIR_CACHE_MAX_SIZE > 0
        && readdir_cache.size() > READDIR_CACHE_MAX_SIZE)
        {
            /* TODO: instead of the picking one randomly,
             * obsolete the one which was accessed longest time ago
             */
            EraseRandomlyOne(readdir_cache);
        }
        
        readdir_cache[inonum] = result;
    }
    return result;
}

const cromfs_inodenum_t cromfs::dir_lookup(cromfs_inodenum_t inonum,
                                           const std::string& search_name)
{
    std::map<cromfs_inodenum_t, cromfs_dirinfo>::const_iterator
        i = readdir_cache.find(inonum);
    if(i != readdir_cache.end())
    {
        const cromfs_dirinfo& info = i->second;
        cromfs_dirinfo::const_iterator j = info.find(search_name);
        if(j != info.end()) return j->second;
        return 0;
    }

    const cromfs_inode_internal inode = read_inode_and_blocks(inonum);

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

cromfs::cromfs(int fild) : fd(fild)
{
    reread_superblock();
}

cromfs::~cromfs()
{
    cromfs_alarm_obj = NULL;
}
