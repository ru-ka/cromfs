#define _XOPEN_SOURCE 500
#define _LARGEFILE64_SOURCE

#include <algorithm>
#include <cstdlib>

#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>

#include <sstream>

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

static const std::string DumpInode(const cromfs_inode_internal& inode)
{
    std::stringstream s;
    
    s << "mode(" << std::hex << inode.mode
      << ")time(" << inode.time
      << ")links(" << inode.links
      << ")size(" << inode.bytesize
      << ")nblocks(" << inode.blocklist.size()
      << ")";
    
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

void cromfs::reread_superblock()
{
    char Buf[64];
    if(pread(fd, Buf, sizeof(Buf), 0) == -1) throw errno;
    
    uint_fast64_t sig  = R64(Buf+0x0000);
    if(sig != CROMFS_SIGNATURE) throw EINVAL;
    
    if(blktab != NULL) munmap(blktab, sblock.blktab_size);
    blktab = NULL;
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
    
    void* m = mmap(NULL, sblock.blktab_size, PROT_READ, MAP_SHARED, fd, sblock.blktab_offs);
    if(m == (void*)-1)
    {
        throw errno;
    }
    
    blktab = (cromfs_block_storage*) m;
    
#if READBLOCK_DEBUG
    fprintf(stderr, "blktab is at %p, size %llu bytes\n",
        m, sblock.blktab_size);

    for(unsigned a=0; a<sblock.blktab_size / 8; ++a)
    {
        fprintf(stderr, "block %u: %s\n", a, DumpBlock(blktab[a]).c_str());
    }
#endif
}

cromfs_inode_internal cromfs::read_uncompressed_inode(uint_fast64_t offset)
{
    char Buf[0x18];
    
    cromfs_inode_internal result;
    
    if(pread(fd, Buf, 0x18, offset+0) == -1) throw errno;
    
    result.mode    = R32(Buf+0x0000);
    result.time    = R32(Buf+0x0004);
    result.links   = R32(Buf+0x0008);
    result.bytesize= R64(Buf+0x0010);
    
    printf("read_uncompressed_inode(%lld): %s\n",
        offset, DumpInode(result).c_str());
    
    uint_fast64_t nblocks = (result.bytesize + CROMFS_BSIZE-1) / CROMFS_BSIZE;
    
    result.blocklist.resize(nblocks);
    
    /* FIXME: not endianess-safe */
    if(pread(fd, &result.blocklist[0], 4 * nblocks, offset+0x18) == -1) throw errno;
    
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
    cromfs_block_storage& block = blktab[ind];

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
    ssize_t r = pread(fd, Buf, sizeof(Buf), sblock.fblktab_offs + ind * CROMFS_FSIZE);
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

const cromfs_dirinfo cromfs::read_dir(const cromfs_inode_internal& inode,
                                      uint_fast32_t dir_offset, uint_fast32_t dir_count)
{
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
        read_file_data(inode, 4, (unsigned char*)&addr_table[0], 4*num_to_read);
        
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
    return result;
}

cromfs::cromfs(int fild) : fd(fild), blktab(NULL)
{
    reread_superblock();
}

cromfs::~cromfs()
{
    if(blktab != NULL) munmap(blktab, sblock.blktab_size);
}
