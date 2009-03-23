#include "../cromfs-defs.hh"
#include <vector>

uint_fast64_t GetInodeSize(const cromfs_inode_internal& inode, uint_fast32_t storage_opts);

uint_fast64_t GetInodeOffset(cromfs_inodenum_t inonum);

cromfs_inodenum_t get_first_free_inode_number(uint_fast64_t inotab_size);

const std::vector<unsigned char> encode_inode
    (const cromfs_inode_internal& inode,
     uint_fast32_t storage_opts);

void put_inode(unsigned char* inodata, const cromfs_inode_internal& inode,
               uint_fast32_t storage_opts,
               bool and_blocks = true);

void get_inode
   (const unsigned char* inodata, uint_fast64_t inodata_size,
    cromfs_inode_internal& inode,
    uint_fast32_t storage_opts,
    uint_fast32_t bsize,
    bool and_blocks);

static inline
void get_inode_header
   (const unsigned char* inodata, uint_fast64_t inodata_size,
    cromfs_inode_internal& inode,
    uint_fast32_t storage_opts,
    uint_fast32_t bsize)
    { get_inode(inodata, inodata_size, inode, storage_opts, bsize, false); }

static inline
void get_inode_and_blocks
   (const unsigned char* inodata, uint_fast64_t inodata_size,
    cromfs_inode_internal& inode,
    uint_fast32_t storage_opts,
    uint_fast32_t bsize)
    { get_inode(inodata, inodata_size, inode, storage_opts, bsize, true); }

void increment_inode_linkcount(unsigned char* inodata, int by_value = 1);

uint_fast32_t CalcEncodedInodeSize(const cromfs_inode_internal& inode, uint_fast32_t storage_opts);

uint_fast64_t CalcSizeInBlocks(uint_fast64_t filesize, uint_fast32_t bsize);

void PutInodeSize(cromfs_inode_internal& inode, uint_fast64_t bytesize);

