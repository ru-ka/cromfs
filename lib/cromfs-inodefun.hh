#include "../cromfs-defs.hh"
#include <vector>

static const size_t INODE_BLOCKLIST_OFFSET = 0x18;

unsigned GetInodeSize(const cromfs_inode_internal& inode, unsigned blocknum_size_bytes);

const uint_fast64_t GetInodeOffset(cromfs_inodenum_t inonum);

const std::vector<unsigned char> encode_inode
    (const cromfs_inode_internal& inode,
     unsigned blocknum_size_bytes);

void put_inode(unsigned char* inodata, const cromfs_inode_internal& inode,
               unsigned blocknum_size_bytes);

void get_inode(const unsigned char* inodata, uint_fast64_t inodata_size,
               cromfs_inode_internal& inode,
               uint_fast32_t bsize = 0,
               uint_fast32_t blocknum_size_bytes = 0);

void get_inode(const unsigned char* inodata, 
               cromfs_inode_internal& inode,
               uint_fast32_t bsize = 0,
               uint_fast32_t blocknum_size_bytes = 0);

const cromfs_inodenum_t get_first_free_inode_number(uint_fast64_t inotab_size);

void increment_inode_linkcount(unsigned char* inodata);

uint_fast64_t CalcSizeInBlocks(uint_fast64_t filesize, uint_fast32_t bsize);

void PutInodeSize(cromfs_inode_internal& inode,
                  uint_fast64_t bytesize,
                  long bsize);
