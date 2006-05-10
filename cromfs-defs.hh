#include <stdint.h>

#include <vector>
#include <string>
#include <map>



#define CROMFS_SIGNATURE   0x303053464d4f5243ULL
#define CROMFS_MAX_CACHE_BLOCKS 10

typedef uint_least64_t cromfs_inodenum_t;
typedef uint_least32_t cromfs_blocknum_t;
typedef uint_least32_t cromfs_fblocknum_t;

struct cromfs_inode_internal
{
    uint_fast32_t mode;
    uint_fast32_t time;
    uint_fast32_t links;
    uint_fast64_t bytesize;
    std::vector<cromfs_blocknum_t> blocklist;
};
struct cromfs_block_storage
{
    uint_least32_t fblocknum __attribute__((packed));
    uint_least32_t startoffs __attribute__((packed));
} __attribute__((packed));

struct cromfs_superblock_internal
{
    uint_fast64_t blktab_offs;
    uint_fast64_t fblktab_offs;
    uint_fast64_t inotab_offs;
    uint_fast64_t rootdir_offs;
    uint_fast64_t blktab_size;
    uint_fast64_t compressed_block_size;
    uint_fast64_t uncompressed_block_size;
    uint_fast64_t bytes_of_files;
};

typedef std::vector<unsigned char> cromfs_datablock;
typedef std::vector<unsigned char> cromfs_cached_fblock;
typedef std::map<std::string, cromfs_inodenum_t> cromfs_dirinfo;


#define L (uint_fast64_t)
static inline uint_fast64_t R64(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return (L data[0] << 0)  | (L data[1] << 8) | (L data[2] << 16) | (L data[3] << 24)
         | (L data[4] << 32) | (L data[5] <<40) | (L data[6] << 48) | (L data[7] << 56);
}
static inline uint_fast32_t R32(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return (L data[0] << 0)  | (L data[1] << 8) | (L data[2] << 16) | (L data[3] << 24);
}
static void W32(void* p, uint_fast32_t value)
{
    unsigned char* data = (unsigned char*)p;
    data[0] = (value>>0) & 0xFF;
    data[1] = (value>>8) & 0xFF;
    data[2] = (value>>16) & 0xFF;
    data[3] = (value>>24) & 0xFF;
}
static void W64(void* p, uint_fast64_t value)
{
    unsigned char* data = (unsigned char*)p;
    W32(data+0, (value));
    W32(data+4, (value >> 32ULL));
}
#undef L
