/*
cromfs - Copyright (C) 1992,2006 Bisqwit (http://iki.fi/bisqwit/)
Licence: GPL

cromfs-defs.hh: The structures used by the cromfs filesystem engine
in cromfs.cc and util/main.cc.

See doc/FORMAT for the documentation of the filesystem structure.

*/

/* Disable this if you have hash-related compilation problems. */
#define USE_HASHMAP



#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

#include <stdint.h>

#include <vector>
#include <string>
#include <map>



#define CROMFS_SIGNATURE_01   UINT64_C(0x313053464d4f5243)
#define CROMFS_SIGNATURE_02   UINT64_C(0x323053464d4f5243)

#define CROMFS_SIGNATURE    CROMFS_SIGNATURE_02

/* Use "least" instead of "fast" for these types, because they
 * are included in structs and vectors that are directly copied
 * from/to the filesystem image.
 */
typedef uint_least64_t cromfs_inodenum_t;
typedef uint_least32_t cromfs_blocknum_t;
typedef uint_least32_t cromfs_fblocknum_t;

/* Use "fast" in internal types, "least" in storage types. */

struct cromfs_inode_internal
{
    uint_fast32_t mode;
    uint_fast32_t time;
    uint_fast32_t links;
    uint_fast32_t rdev;
    uint_fast16_t uid;
    uint_fast16_t gid;
    uint_fast64_t bytesize;
    std::vector<cromfs_blocknum_t> blocklist;
};
struct cromfs_fblock_internal
{
    uint_fast64_t filepos;
    uint_fast32_t length;
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
    uint_fast32_t compressed_block_size;
    uint_fast64_t uncompressed_block_size; /* 64-bit to reduce the number of casts */
    uint_fast64_t bytes_of_files;
    uint_fast64_t sig;
};

typedef std::vector<unsigned char> cromfs_datablock;
typedef std::vector<unsigned char> cromfs_cached_fblock;
typedef std::map<std::string, cromfs_inodenum_t> cromfs_dirinfo;


static inline uint_fast16_t R16(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return (data[0] << 0)  | (data[1] << UINT16_C(8));
}
static inline uint_fast32_t R32(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return R16(data) | (R16(data+2) << UINT32_C(16));
}

#define L (uint_fast64_t)

static inline uint_fast64_t R64(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return (L R32(data)) | ((L R32(data+4)) << UINT64_C(32));
}

#undef L

static void W16(void* p, uint_fast16_t value)
{
    unsigned char* data = (unsigned char*)p;
    data[0] = (value>>0) & 0xFF;
    data[1] = (value>>8) & 0xFF;
}
static void W32(void* p, uint_fast32_t value)
{
    unsigned char* data = (unsigned char*)p;
    W16(data+0, value);
    W16(data+2, value >> UINT32_C(16));
}
static void W64(void* p, uint_fast64_t value)
{
    unsigned char* data = (unsigned char*)p;
    W32(data+0, (value));
    W32(data+4, (value >> UINT64_C(32)));
}


#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
