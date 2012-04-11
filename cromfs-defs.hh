#ifndef bqtCromfsDefsHH
#define bqtCromfsDefsHH

/*
cromfs - Copyright (C) 1992,2011 Bisqwit (http://iki.fi/bisqwit/)
Licence: GPL

cromfs-defs.hh: The structures used by the cromfs filesystem engine
in cromfs.cc and util/main.cc.

See doc/FORMAT for the documentation of the filesystem structure.

*/

#include "lib/endian.hh"

#include <unistd.h>
#include <vector>
#include <string>
#include <map>

#define CROMFS_SIGNATURE_01   UINT64_C(0x313053464d4f5243)
#define CROMFS_SIGNATURE_02   UINT64_C(0x323053464d4f5243)
#define CROMFS_SIGNATURE_03   UINT64_C(0x333053464d4f5243)

#define CROMFS_SIGNATURE    CROMFS_SIGNATURE_03

enum CROMFS_OPTS
{
    CROMFS_OPT_SPARSE_FBLOCKS      = 0x00000001,
    CROMFS_OPT_24BIT_BLOCKNUMS     = 0x00000100,
    CROMFS_OPT_16BIT_BLOCKNUMS     = 0x00000200,
    CROMFS_OPT_PACKED_BLOCKS       = 0x00000400,
    CROMFS_OPT_VARIABLE_BLOCKSIZES = 0x00000800,
    CROMFS_OPT_USE_BWT             = 0x00010000,
    CROMFS_OPT_USE_MTF             = 0x00020000
};


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
    uint_fast32_t blocksize;
    std::vector<cromfs_blocknum_t> blocklist;

    cromfs_inode_internal() // -Weffc++
        : mode(),time(),links(),rdev(),uid(),gid(),
          bytesize(),blocksize(),blocklist() {}
};
struct cromfs_fblock_internal
{
    uint_fast64_t filepos;
    uint_fast32_t length;
};

struct cromfs_block_internal
{
    uint_least32_t fblocknum;
    uint_least32_t startoffs;
public:
    void define(uint_fast32_t fb, uint_fast32_t so)
    {
        fblocknum = fb;
        startoffs = so;
    }
    bool operator==(const cromfs_block_internal& b) const
    {
        return fblocknum == b.fblocknum && startoffs == b.startoffs;
    }
};

struct cromfs_superblock_internal
{
    uint_fast64_t blktab_offs,  blktab_size,  blktab_room;
    uint_fast64_t fblktab_offs;
    uint_fast64_t inotab_offs,  inotab_size,  inotab_room;
    uint_fast64_t rootdir_offs, rootdir_size, rootdir_room;
    uint_fast32_t fsize;
    uint_fast64_t bsize; /* 64-bit to reduce the number of casts */
    uint_fast64_t bytes_of_files;
    uint_fast64_t sig;

    enum { MaxBufferSize = 0x50 };
    typedef unsigned char BufferType[MaxBufferSize];

    void RecalcRoom()
    {
        rootdir_room = inotab_offs  - rootdir_offs;
        inotab_room  = blktab_offs  - inotab_offs;
        blktab_room  = fblktab_offs - blktab_offs;
    }

    void SetOffsets(unsigned headersize)
    {
        rootdir_offs = headersize;
        inotab_offs  = rootdir_offs + rootdir_room;
        blktab_offs  = inotab_offs  + inotab_room;
        fblktab_offs = blktab_offs  + blktab_room;
    }
    void SetOffsets(bool extended_header)
    {
        SetOffsets(extended_header ? 0x50u : 0x38u);
    }
    void SetOffsets()
    {
        SetOffsets(
            rootdir_size != rootdir_room
         || inotab_size != inotab_room
         || blktab_size != blktab_room);
    }

    void ReadFromBuffer(const BufferType& Superblock)
    {
        sig                     = get_64(Superblock+0x0000);
        blktab_offs             = get_64(Superblock+0x0008);
        fblktab_offs            = get_64(Superblock+0x0010);
        inotab_offs             = get_64(Superblock+0x0018);
        rootdir_offs            = get_64(Superblock+0x0020);
        fsize                   = get_32(Superblock+0x0028);
        bsize                   = get_32(Superblock+0x002C);
        bytes_of_files          = get_64(Superblock+0x0030);

        RecalcRoom();

        rootdir_size = rootdir_room;
        inotab_size = inotab_room;
        blktab_size = blktab_room;

        if(GetSize() >= 0x50)
        {
            rootdir_size = get_64(Superblock+0x0038);
            inotab_size  = get_64(Superblock+0x0040);
            blktab_size  = get_64(Superblock+0x0048);
        }
    }
    void WriteToBuffer(BufferType& Superblock)
    {
        put_64(Superblock+0x00, sig);
        put_64(Superblock+0x08, blktab_offs);
        put_64(Superblock+0x10, fblktab_offs);
        put_64(Superblock+0x18, inotab_offs);
        put_64(Superblock+0x20, rootdir_offs);
        put_32(Superblock+0x28, fsize);
        put_32(Superblock+0x2C, bsize);
        put_64(Superblock+0x30, bytes_of_files);

        if(rootdir_offs >= 0x50)
        {
            put_64(Superblock+0x0038, rootdir_size);
            put_64(Superblock+0x0040, inotab_size);
            put_64(Superblock+0x0048, blktab_size);
        }
    }

    unsigned GetSize(bool sparse_mode) const
    {
        if(sig == CROMFS_SIGNATURE_01
        || sig == CROMFS_SIGNATURE_02)
        {
            return 0x38;
        }
        return sparse_mode ? MaxBufferSize : 0x38;
    }
    unsigned GetSize() const
    {
        return GetSize(rootdir_offs >= 0x50);
    }
};

typedef std::map<std::string, cromfs_inodenum_t> cromfs_dirinfo;

#define BLOCKNUM_SIZE_BYTES() \
   (4 - 1*!!(storage_opts & CROMFS_OPT_24BIT_BLOCKNUMS) \
      - 2*!!(storage_opts & CROMFS_OPT_16BIT_BLOCKNUMS) )
#define INODE_HEADER_SIZE() \
    (0x18 + 4*!!(storage_opts & CROMFS_OPT_VARIABLE_BLOCKSIZES))
#define MAX_INODE_HEADER_SIZE 0x1C
#define INODE_SIZE_BYTES(nblocks) \
    (INODE_HEADER_SIZE() + BLOCKNUM_SIZE_BYTES() * nblocks)

#define DATALOCATOR_SIZE_BYTES() \
    ((storage_opts & CROMFS_OPT_PACKED_BLOCKS) ? 4 : 8)

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

#if defined(__GNUC__) && defined(_OPENMP) && \
      ((__GNUC__ == 4 && (__GNUC_MINOR__ >= 3)) \
    || (__GNUC__ > 4))
 #define HAS_GCC_PARALLEL_ALGORITHMS
 #define MAYBE_PARALLEL_NS __gnu_parallel
#else
 #undef HAS_GCC_PARALLEL_ALGORITHMS
 #define MAYBE_PARALLEL_NS std
#endif

#endif
