/*
cromfs - Copyright (C) 1992,2006 Bisqwit (http://iki.fi/bisqwit/)
Licence: GPL

cromfs.cc: The cromfs filesystem engine.
The class cromfs should actually be a singleton, because of the signal
handling herein, but that hasn't been written yet...

See doc/FORMAT for the documentation of the filesystem structure.

*/

#define _XOPEN_SOURCE 500
#define _LARGEFILE64_SOURCE

#include <algorithm>
#include <cstdlib>

#include <unistd.h>
#include <cerrno>
#include <csignal>

#include <sstream>

#include <sys/time.h>

#include "LzmaDecode.h"
#include "LzmaDecode.c"
#include "cromfs.hh"

#define CROMFS_FSIZE  (sblock.compressed_block_size)
#define CROMFS_BSIZE (sblock.uncompressed_block_size)

#define READBLOCK_DEBUG 0
#define FBLOCK_DEBUG    0
#define INODE_DEBUG     0
#define READFILE_DEBUG  0
#define READDIR_DEBUG   0

#define BLKTAB_CACHE_TIMEOUT 60

static const std::string DumpInode(const cromfs_inode_internal& inode)
{
    std::stringstream s;
    
    s << "mode(" << std::hex << inode.mode
      << ")time(" << inode.time
      << ")links(" << inode.links
      << ")rdev(" << std::hex << inode.rdev
      << ")size(" << inode.bytesize;
   
    if(!inode.blocklist.empty()) s << ")nblocks(" << inode.blocklist.size();
    s << ")";
    
    return s.str();
}
static const std::string DumpBlock(const cromfs_block_storage& block)
{
    std::stringstream s;
    
    s << "fblocknum(" << block.fblocknum
      << ")startoffs(" << block.startoffs
      << ")";
    
    return s.str();
}

static const std::vector<unsigned char> LZMADeCompress
    (const std::vector<unsigned char>& buf)
{
    if(buf.size() <= 5+8) return std::vector<unsigned char> ();
    
    /* FIXME: not endianess-safe */
    uint_least64_t out_sizemax = *(const uint_least64_t*)&buf[5];
    
    std::vector<unsigned char> result(out_sizemax);
    
    CLzmaDecoderState state;
    LzmaDecodeProperties(&state.Properties, &buf[0], LZMA_PROPERTIES_SIZE);
    state.Probs = new CProb[LzmaGetNumProbs(&state.Properties)];
    
    SizeT in_done;
    SizeT out_done;
    LzmaDecode(&state, &buf[13], buf.size()-13, &in_done,
               &result[0], result.size(), &out_done);
    
    delete[] state.Probs;
    
    result.resize(out_done);
    return result;
}

void cromfs::reread_superblock()
{
    char Buf[64];
    if(pread64(fd, Buf, sizeof(Buf), 0) == -1) throw errno;
    
    uint_fast64_t sig  = R64(Buf+0x0000);
    if(sig != CROMFS_SIGNATURE) throw EINVAL;
    
    cache_fblocks.clear();

    sblock.blktab_offs             = R64(Buf+0x0008);
    sblock.fblktab_offs            = R64(Buf+0x0010);
    sblock.inotab_offs             = R64(Buf+0x0018);
    sblock.rootdir_offs            = R64(Buf+0x0020);
    sblock.compressed_block_size   = R32(Buf+0x0028); /* aka. FSIZE */
    sblock.uncompressed_block_size = R32(Buf+0x002C); /* aka. BSIZE */
    sblock.bytes_of_files          = R64(Buf+0x0030);
    
    sblock.blktab_size = sblock.fblktab_offs - sblock.blktab_offs;
    
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
        (unsigned)sblock.compressed_block_size,
        (unsigned)sblock.uncompressed_block_size);
    
    inotab  = read_uncompressed_inode(sblock.inotab_offs);
    rootdir = read_uncompressed_inode(sblock.rootdir_offs);
    
    forget_blktab();
    reread_blktab();
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
    alarm(BLKTAB_CACHE_TIMEOUT);
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
    if(pread64(fd, &blktab_compressed[0], blktab_compressed.size(), sblock.blktab_offs)
    != blktab_compressed.size())
    {
        throw EINVAL;
    }

    blktab_compressed = LZMADeCompress(blktab_compressed);
    
    blktab.resize(blktab_compressed.size() / sizeof(blktab[0]));
    std::memcpy(&blktab[0], &blktab_compressed[0], blktab.size() * sizeof(blktab[0]));
    
#if READBLOCK_DEBUG
    for(unsigned a=0; a<sblock.blktab_size / 8; ++a)
    {
        fprintf(stderr, "block %u: %s\n", a, DumpBlock(blktab[a]).c_str());
    }
#endif

    cromfs_setup_alarm(*this);
}

cromfs_inode_internal cromfs::read_uncompressed_inode(uint_fast64_t offset)
{
    char Buf[0x18];
    
    cromfs_inode_internal result;
    
    if(pread64(fd, Buf, 0x18, offset+0) == -1) throw errno;
    
    result.mode    = R32(Buf+0x0000);
    result.time    = R32(Buf+0x0004);
    result.links   = R32(Buf+0x0008);
    result.rdev    = R32(Buf+0x000C);
    result.bytesize= R64(Buf+0x0010);
    
    printf("read_uncompressed_inode(%lld): %s\n",
        offset, DumpInode(result).c_str());
    
    uint_fast64_t nblocks = (result.bytesize + CROMFS_BSIZE-1) / CROMFS_BSIZE;
    
    result.blocklist.resize(nblocks);
    
    /* FIXME: not endianess-safe */
    if(pread64(fd, &result.blocklist[0], 4 * nblocks, offset+0x18) == -1) throw errno;
    
    return result;
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
    read_file_data(inotab, (inodenum-2)*4ULL, Buf, 0x18);
    
    cromfs_inode_internal result;
    
    result.mode    = R32(Buf+0x0000);
    result.time    = R32(Buf+0x0004);
    result.links   = R32(Buf+0x0008);
    result.rdev    = R32(Buf+0x000C);
    result.bytesize= R64(Buf+0x0010);
    
    return result;
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
    
    uint_fast64_t nblocks = (result.bytesize + CROMFS_BSIZE-1) / CROMFS_BSIZE;
    
    result.blocklist.resize(nblocks);
    
    /* FIXME: not endianess-safe */
    read_file_data(inotab, (inodenum-2)*4ULL + 0x18,
                   (unsigned char*)&result.blocklist[0], 4 * nblocks);
    
    return result;
}

void cromfs::read_block(cromfs_blocknum_t ind, unsigned offset,
                        unsigned char* target, unsigned size)
{
    if(blktab.empty())
    {
        reread_blktab();
    }
    cromfs_block_storage& block = blktab[ind];
    cromfs_setup_alarm(*this);

#if READBLOCK_DEBUG
    fprintf(stderr, "read_block(%u,%u,%p,%u): block=%s\n",
        (unsigned)ind, offset, target, size,
        DumpBlock(block).c_str());
#endif

    cromfs_cached_fblock& fblock = read_fblock(block.fblocknum);
    
    int_fast64_t begin = block.startoffs + offset;
    
#if READBLOCK_DEBUG
    fprintf(stderr, "- got %u bytes, reading from %lld\n", fblock.size(), begin);
#endif
    
    uint_fast64_t bytes =
        std::min((int_fast64_t)size, (int_fast64_t)(fblock.size()) - begin);
    
    /*
    fprintf(stderr, "-");
    for(unsigned a = 0; a < fblock.size()-block.startoffs; ++a)
    {
        fprintf(stderr, "%c", fblock[begin+a]);
        if(a == 64) break;
    }
    fprintf(stderr, "\n");
    */
    
    std::memcpy(target, &fblock[begin], bytes);
                
}

cromfs_cached_fblock& cromfs::read_fblock(cromfs_fblocknum_t ind)
{
    std::map<cromfs_fblocknum_t, cromfs_cached_fblock>::iterator
        i = cache_fblocks.find(ind);
    if(i != cache_fblocks.end())
    {
        return i->second;
    }
    
    if(CROMFS_MAX_CACHE_BLOCKS > 0
    && cache_fblocks.size() >= CROMFS_MAX_CACHE_BLOCKS)
    {
        cromfs_fblocknum_t smallest = cache_fblocks.begin()->first;
        cromfs_fblocknum_t biggest  = cache_fblocks.rbegin()->first;
        cromfs_fblocknum_t random = smallest + std::rand() % (biggest-smallest);
        i = cache_fblocks.lower_bound(random);
        cache_fblocks.erase(i);
    }
    
    unsigned char Buf[CROMFS_FSIZE];
    ssize_t r = pread64(fd, Buf, sizeof(Buf), sblock.fblktab_offs + ind * CROMFS_FSIZE);
    if(r == -1) throw errno;
    
    uint_fast32_t comp_size = R32(Buf+0);   SizeT comp_got;
    uint_fast64_t orig_size = R64(Buf+4+5); SizeT orig_got;
    
#if FBLOCK_DEBUG
    fprintf(stderr,
        "Got a fblock\n"
        "- asked for %u bytes, got %d. comp_size=%u, orig_size=%llu\n",
        sizeof(Buf),
        r, comp_size, orig_size);
#endif

    if(r < (ssize_t)(comp_size+4))
    {
#if FBLOCK_DEBUG
        fprintf(stderr, "- that was a short read\n");
#endif
        throw EIO;
    }
    if(comp_size <= 13)
    {
#if FBLOCK_DEBUG
        fprintf(stderr, "- that is a too small fblock\n");
#endif
        throw EIO;
    }
    
    cromfs_cached_fblock& result = cache_fblocks[ind];
    result.resize(orig_size);

    CLzmaDecoderState state;
    LzmaDecodeProperties(&state.Properties, &Buf[4], LZMA_PROPERTIES_SIZE);
    state.Probs = new CProb[LzmaGetNumProbs(&state.Properties)];
    
    LzmaDecode(&state, Buf+4+5+8,  comp_size-5-8, &comp_got,
                       &result[0], orig_size,     &orig_got);
    
    delete[] state.Probs;
    
    if(orig_got != orig_size)
    {
#if FBLOCK_DEBUG
        fprintf(stderr, "- orig_got=%llu, orig_size=%llu, discrepancy detected (comp_got %llu)\n",
            (uint_fast64_t)orig_got, (uint_fast64_t)orig_size,
            (uint_fast64_t)comp_got);
#endif
        throw EIO;
        result.resize(orig_got);
    }
    
    return result;
}

int_fast64_t cromfs::read_file_data(const cromfs_inode_internal& inode,
                                    uint_fast64_t offset,
                                    unsigned char* target, uint_fast64_t size)
{
    int_fast64_t result = 0;
#if READFILE_DEBUG
    fprintf(stderr, "read_file_data(%s), offset=%llu, target=%p, size=%llu\n",
        DumpInode(inode).c_str(), offset, target, size);
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
            "Reading bytes from offset %llu to %p (%llu bytes):\n"
            "- reading block %llu, offset %u (consume %llu), file remain %lld\n",
                offset, target, size,
                begin_block_index,
                begin_block_offset, consume_bytes,
                remain_file_bytes);
#endif

        read_block(inode.blocklist[begin_block_index], begin_block_offset,
                   target, consume_bytes);

        target += consume_bytes;
        size   -= consume_bytes;
        offset += consume_bytes;
        result += consume_bytes;
    }
    return result;
}

const cromfs_dirinfo cromfs::read_dir(cromfs_inodenum_t inonum,
                                      uint_fast32_t dir_offset, uint_fast32_t dir_count)
{
    static std::map<cromfs_inodenum_t, cromfs_dirinfo> readdir_cache;
    
    if(dir_count == ~0U)
    {
        std::map<cromfs_inodenum_t, cromfs_dirinfo>::const_iterator
            i = readdir_cache.find(inonum);
        if(i != readdir_cache.end())
        {
            return i->second;
        }
    }

    const cromfs_inode_internal inode = read_inode_and_blocks(inonum);
    cromfs_dirinfo result;
    
    unsigned char DirHeader[4];
    if(read_file_data(inode, 0, DirHeader, 4) < 4)
    {
#if READDIR_DEBUG
        fprintf(stderr, "read_dir(%s)(%u,%u)\n",
            DumpInode(inode).c_str(),
            dir_offset, dir_count);
        fprintf(stderr, "- directory has no blocks\n");
#endif
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

        std::vector<uint_least32_t> addr_table(num_to_read);
        
        /* FIXME: not endianess-safe */
        read_file_data(inode, 4 + dir_offset*4, (unsigned char*)&addr_table[0], 4*num_to_read);
        
        for(unsigned a=0; a<addr_table.size(); ++a)
        {
            char Buf[8+4096]; // Max filename size (4096)
            read_file_data(inode, addr_table[a], (unsigned char*)Buf, sizeof(Buf));
            
            uint_fast64_t inonumber = R64(Buf+0);
            Buf[sizeof(Buf)-1] = '\0';
            const char* filename = Buf+8;
            
            result[filename] = inonumber;
        }
    }
    
    if(dir_count == ~0U)
    {
        if(readdir_cache.size() > 3)
        {
            readdir_cache.erase(readdir_cache.begin());
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
