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

#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "LzmaDecode.h"
#include "LzmaDecode.c"
#include "cromfs.hh"

#define CROMFS_FSIZE  (sblock.compressed_block_size)
#define CROMFS_BSIZE (sblock.uncompressed_block_size)

#define SBLOCK_DEBUG    0
#define READBLOCK_DEBUG 0
#define FBLOCK_DEBUG    0
#define INODE_DEBUG     0
#define READFILE_DEBUG  0
#define READDIR_DEBUG   0
#define DEBUG_INOTAB    0

#define BLKTAB_CACHE_TIMEOUT 0

/* How many directories to keep cached at the same time maximum */
static const unsigned READDIR_CACHE_MAX_SIZE = 3;

/* How many decompressed fblocks to cache in RAM at the same time maximum */
static const unsigned FBLOCK_CACHE_MAX_SIZE = 10;

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
    
    s << ")time(" << inode.time
      << ")links(" << inode.links
      << ")rdev(" << std::hex << inode.rdev
      << ")size(" << inode.bytesize;
   
    if(!inode.blocklist.empty())
    {
        s << ")nblocks(" << inode.blocklist.size();
        
        for(unsigned a=0; a<inode.blocklist.size(); ++a)
            s << (a==0 ? ":" : ",") << inode.blocklist[a];
    }
    s << ")";
    
    return s.str();
}
const std::string DumpBlock(const cromfs_block_storage& block)
{
    std::stringstream s;
    
    s << "fblocknum(" << block.fblocknum
      << ")startoffs(" << block.startoffs
      << ")";
    
    return s.str();
}

static const std::vector<unsigned char> LZMADeCompress
    (const unsigned char* buf, size_t BufSize)
{
    if(BufSize <= 5+8) return std::vector<unsigned char> ();
    
    /* FIXME: not endianess-safe */
    uint_least64_t out_sizemax = *(const uint_least64_t*)&buf[5];
    
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
    char Buf[64];
    if(pread64(fd, Buf, sizeof(Buf), 0) == -1) throw errno;
    
    uint_fast64_t sig  = R64(Buf+0x0000);
    if(sig != CROMFS_SIGNATURE_01
    && sig != CROMFS_SIGNATURE_02) throw EINVAL;
    
    cache_fblocks.clear();
    
    sblock.sig = sig;
    sblock.blktab_offs             = R64(Buf+0x0008);
    sblock.fblktab_offs            = R64(Buf+0x0010);
    sblock.inotab_offs             = R64(Buf+0x0018);
    sblock.rootdir_offs            = R64(Buf+0x0020);
    sblock.compressed_block_size   = R32(Buf+0x0028); /* aka. FSIZE */
    sblock.uncompressed_block_size = R32(Buf+0x002C); /* aka. BSIZE */
    sblock.bytes_of_files          = R64(Buf+0x0030);
    
    sblock.blktab_size = sblock.fblktab_offs - sblock.blktab_offs;
    
#if SBLOCK_DEBUG
    fprintf(stderr,
        "BlockTab at %llX\n"
        "FBlkTab at %llX\n"
        "inotab at %llX\n"
        "rootdir at %llX\n"
        "FSIZE %u  BSIZE %u\n",
        sblock.blktab_offs,
        sblock.fblktab_offs,
        sblock.inotab_offs,
        sblock.rootdir_offs,
        (unsigned)CROMFS_FSIZE,
        (unsigned)CROMFS_BSIZE);
#endif
    
    inotab  = read_raw_inode_and_blocks(sblock.inotab_offs);
    rootdir = read_raw_inode_and_blocks(sblock.rootdir_offs);
    
    rootdir.mode = S_IFDIR | 0555;
    rootdir.links = 2;
    rootdir.uid = 0;
    rootdir.gid = 0;
    
    forget_blktab();
    reread_blktab();
    
    reread_fblktab();
    
    if(fblktab.empty()) throw EINVAL;
    
#if DEBUG_INOTAB
    fprintf(stderr, "rootdir inode: %s\n", DumpInode(rootdir).c_str());
    fprintf(stderr, "intab inode: %s\n", DumpInode(inotab).c_str());
    
    cromfs_inodenum_t maxinode = 2+inotab.bytesize/4;
    for(cromfs_inodenum_t a=2; a<maxinode; )
    {
        cromfs_inode_internal result = read_inode_and_blocks(a);
        fprintf(stderr, "INODE %u: %s\n", (unsigned)a, DumpInode(result).c_str());
        uint_fast64_t nblocks = CalcSizeInBlocks(result.bytesize);
        a+=(0x18 + 4*nblocks) / 4;
    }
#endif
}

void cromfs::reread_fblktab()
{
    fblktab.clear();
    
    uint_fast64_t startpos = sblock.fblktab_offs;
    for(;;)
    {
        unsigned char Buf[4];
        ssize_t r = pread64(fd, Buf, 4, startpos);
        if(r == 0) break;
        if(r < 0) throw errno;
        if(r < 4) throw EINVAL;
        
        cromfs_fblock_internal fblock;
        fblock.filepos = startpos+4;
        fblock.length  = R32(Buf);
        
        if(fblock.length <= 13)
        {
            throw EINVAL;
        }

#if FBLOCK_DEBUG
        fprintf(stderr, "FBLOCK %u: size(%u)at(%llu (0x%llX))\n",
            fblktab.size(),
            fblock.length, fblock.filepos, fblock.filepos);
#endif
        fblktab.push_back(fblock);
        
        startpos += 4 + fblock.length;
    }
}

void cromfs::forget_blktab()
{
    blktab = std::vector<cromfs_block_storage>();
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
    std::vector<unsigned char> blktab_compressed(sblock.blktab_size);
    
    ssize_t r = pread64(fd, &blktab_compressed[0], blktab_compressed.size(), sblock.blktab_offs);
    if(r != (ssize_t)blktab_compressed.size())
    {
        throw EINVAL;
    }

    blktab_compressed = LZMADeCompress(blktab_compressed);
    
    blktab.resize(blktab_compressed.size() / sizeof(blktab[0]));
    std::memcpy(&blktab[0], &blktab_compressed[0], blktab.size() * sizeof(blktab[0]));
    
#if READBLOCK_DEBUG
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
}

cromfs_inode_internal cromfs::read_raw_inode_and_blocks(uint_fast64_t offset)
{
    cromfs_inode_internal inode;

    switch(sblock.sig)
    {
        default:
        case CROMFS_SIGNATURE_02:
        {
            uint_fast64_t size = 0;
            if(offset == sblock.rootdir_offs)
                size = sblock.inotab_offs - offset;
            else if(offset == sblock.inotab_offs)
                size = sblock.blktab_offs - offset;
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

            uint_fast64_t nblocks = CalcSizeInBlocks(inode.bytesize);
            inode.blocklist.resize(nblocks);
            
            if(Buf.size() < 4*nblocks+0x18)
            {
                /* Invalid inode */
#if INODE_DEBUG
                fprintf(stderr, "Buf.size=%u, expected %u (%u blocks)\n",
                    (unsigned)Buf.size(),
                    (unsigned)(4*nblocks+0x18),
                    (unsigned)nblocks);
#endif
                throw EIO;
            }

            /* FIXME: not endianess-safe */
            memcpy(&inode.blocklist[0], &Buf[0x18], 4*nblocks);
            
            break;
        }
        case CROMFS_SIGNATURE_01:
        {
            unsigned char Buf[0x18];
            
            if(pread64(fd, Buf, 0x18, offset+0) == -1) throw errno;
            
            ExtractInodeHeader(inode, Buf);
            
#if INODE_DEBUG
            printf("read_raw_inode_and_blocks(%lld): %s\n",
                offset, DumpInode(inode).c_str());
#endif
            
            uint_fast64_t nblocks = CalcSizeInBlocks(inode.bytesize);
            inode.blocklist.resize(nblocks);
            
            /* FIXME: not endianess-safe */
            if(pread64(fd, &inode.blocklist[0], 4 * nblocks, offset+0x18) == -1) throw errno;
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
    
    result.blocklist.resize(nblocks);
    
    /* FIXME: not endianess-safe */
    read_file_data(inotab, (inodenum-2) * UINT64_C(4) + 0x18,
                   (unsigned char*)&result.blocklist[0], 4 * nblocks,
                   "inode block table");
    
    return result;
}

void cromfs::read_block(cromfs_blocknum_t ind, uint_fast32_t offset,
                        unsigned char* target, uint_fast32_t size)
{
    if(blktab.empty())
    {
        reread_blktab();
    }
    cromfs_setup_alarm(*this);

    cromfs_block_storage& block = blktab[ind];

#if READBLOCK_DEBUG
    fprintf(stderr, "- - read_block(%u,%u,%p,%u): block=%s\n",
        (unsigned)ind, offset, target, size,
        DumpBlock(block).c_str());
#endif

    cromfs_cached_fblock& fblock = read_fblock(block.fblocknum);
    uint_fast32_t begin = block.startoffs + offset;
    
    if(begin > fblock.size()) return;
    
#if READBLOCK_DEBUG
    fprintf(stderr, "- - - got %u bytes, reading from %u\n", fblock.size(), (unsigned)begin);
#endif
    
    uint_fast32_t bytes = fblock.size() - begin;
    if(bytes > size) bytes = size;
    std::memcpy(target, &fblock[begin], bytes);
}

cromfs_cached_fblock& cromfs::read_fblock(cromfs_fblocknum_t ind)
{
    fblock_cache_type::iterator i = cache_fblocks.find(ind);

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
    
    return cache_fblocks[ind] = read_fblock_uncached(ind);
}

cromfs_cached_fblock cromfs::read_fblock_uncached(cromfs_fblocknum_t ind)
{
    if(fblktab.empty()) reread_fblktab();
    
    uint_fast32_t comp_size = fblktab[ind].length;
    
    uint_fast64_t filepos_aligned_down = fblktab[ind].filepos & ~4095;
    uint_fast64_t filepos_ignore = fblktab[ind].filepos - filepos_aligned_down;
    size_t map_size = comp_size + filepos_ignore;
    
    void* map_buf = mmap64(NULL, map_size, PROT_READ, MAP_SHARED, fd, filepos_aligned_down);
    if(map_buf != (void*)-1)
    {
        const unsigned char* Buf = filepos_ignore + (unsigned char*)map_buf;
#if FBLOCK_DEBUG
        fprintf(stderr, "- - - - reading fblock %u (%u bytes) from %llu: mmap @ %p\n",
            (unsigned)ind, (unsigned)comp_size, fblktab[ind].filepos, map_buf);
#endif

        /* Exception-safe mechanism that ensures the
         * pointer will be unmapped properly
         */
        struct unmap { unmap(void*&p,size_t s):P(p),S(s){}
                       ~unmap() { munmap(P,S); }
                       void*P; size_t S; } unm(map_buf, map_size);
        
        return LZMADeCompress(Buf, comp_size);
    }
    
    /* mmap failed for some reason, revert to pread64 */

    std::vector<unsigned char> Buf(comp_size);
    
    ssize_t r = pread64(fd, &Buf[0], comp_size, fblktab[ind].filepos);
#if FBLOCK_DEBUG
    fprintf(stderr, "- - - - reading fblock %u (%u bytes) from %llu: got %ld\n",
        (unsigned)ind, (unsigned)comp_size, fblktab[ind].filepos, (long)r);
#endif
    if(r < 0) throw errno;
    if((uint_fast32_t)r < comp_size)
    {
        //fprintf(stderr, "GORE!!! 1\n");
        throw EIO;
    }

    return LZMADeCompress(Buf);
}

int_fast64_t cromfs::read_file_data(const cromfs_inode_internal& inode,
                                    uint_fast64_t offset,
                                    unsigned char* target, uint_fast64_t size,
                                    const char* purpose)
{
    int_fast64_t result = 0;
#if READFILE_DEBUG
    fprintf(stderr,
        "read_file_data, offset=%llu, target=%p, size=%llu = %s\n"
        "- source inode: %s\n",
        offset, target, size, purpose, DumpInode(inode).c_str());
#endif
    while(size > 0)
    {
        int_fast64_t remain_file_bytes = inode.bytesize - offset;
        
        if(remain_file_bytes <= 0) break;
        
        uint_fast64_t begin_block_index  = offset / CROMFS_BSIZE;
        uint_fast32_t begin_block_offset = offset % CROMFS_BSIZE;
        
        if(begin_block_index >= inode.blocklist.size()) break;

        uint_fast32_t remain_block_bytes = CROMFS_BSIZE - begin_block_offset;

        uint_fast64_t consume_bytes =
            std::min((uint_fast64_t)remain_file_bytes,
                std::min(size, (uint_fast64_t)remain_block_bytes));
        
#if READFILE_DEBUG
        fprintf(stderr,
            "- File offset %llu (block %u(%u) @%u, consume %u bytes of %u) -> %p\n",
                offset, (unsigned)begin_block_index,
                        (unsigned)inode.blocklist[begin_block_index],
                begin_block_offset, 
                consume_bytes, size,
                target);
#endif

        read_block(inode.blocklist[begin_block_index], begin_block_offset,
                   target, consume_bytes);
#if READFILE_DEBUG
        if(consume_bytes <= 50)
        {
            fprintf(stderr, "- - Got:");
            
            for(unsigned a=0; a<consume_bytes; ++a)
            {
                //if(a == consume_bytes) fprintf(stderr, " [");
                fprintf(stderr, " %02X", target[a]);
            }
            fprintf(stderr, "\n");
        }
#endif


        target += consume_bytes;
        size   -= consume_bytes;
        offset += consume_bytes;
        result += consume_bytes;
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

const cromfs_dirinfo cromfs::read_dir(cromfs_inodenum_t inonum,
                                      uint_fast32_t dir_offset,
                                      uint_fast32_t dir_count)
{
    static std::map<cromfs_inodenum_t, cromfs_dirinfo> readdir_cache;
    
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
            dir_offset, dir_count);
        fprintf(stderr, "- directory has no blocks\n");
#endif
        //fprintf(stderr, "GORE!!! 3\n");
        throw EIO;
    }
    uint_fast32_t num_files = R32(DirHeader);
    
#if READDIR_DEBUG
    fprintf(stderr, "read_dir(%s)(%u,%u): num_files=%u\n",
        DumpInode(inode).c_str(),
        dir_offset, dir_count,
        num_files);
#endif
    
    if(dir_offset < num_files)
    {
        unsigned num_to_read = std::min(num_files-dir_offset, dir_count);
        if(num_to_read > 0)
        {
            std::vector<uint_least32_t> addr_table(num_to_read);
            
            /* FIXME: not endianess-safe */
            
            /* Guess the first value to reduce I/O. */
            addr_table[0] = 4 + num_files*4;
            
            /* Read the rest of the names. */
            if(num_to_read > 1)
            {
                read_file_data(inode, 4 + (dir_offset+1)*4,
                               (unsigned char*)&addr_table[1], 4*(num_to_read-1),
                               "dir offset list");
            }
            
            for(unsigned a=0; a<addr_table.size(); ++a)
            {
                char Buf[8+4096]; // Max filename size (4096)
                
                unsigned guess_size = sizeof(Buf);
                if(a+1 < addr_table.size())
                    guess_size = std::min(guess_size, addr_table[a+1] - addr_table[a]);
                
                read_file_data(inode, addr_table[a], (unsigned char*)Buf, guess_size,
                               "dir entry");
                
                uint_fast64_t inonumber = R64(Buf+0);
                Buf[sizeof(Buf)-1] = '\0';
                const char* filename = Buf+8;
                
                result[filename] = inonumber;
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

cromfs::cromfs(int fild) : fd(fild)
{
    reread_superblock();
}

cromfs::~cromfs()
{
    cromfs_alarm_obj = NULL;
}
