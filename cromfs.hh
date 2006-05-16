#include "cromfs-defs.hh"

#include <string>

typedef int romfs_exception;

class cromfs
{
public:
    cromfs(int fild);
    ~cromfs();
    
    const cromfs_superblock_internal& get_superblock() { return sblock; }
    const cromfs_inode_internal&      get_root_inode() { return rootdir; }

    int_fast64_t read_file_data(const cromfs_inode_internal& inode,
                                uint_fast64_t offset,
                                unsigned char* target, uint_fast64_t size,
                                const char* purpose);

    const cromfs_inode_internal read_inode(cromfs_inodenum_t inonum);
    
    const cromfs_inode_internal read_inode_and_blocks(cromfs_inodenum_t inonum);
    
    const cromfs_dirinfo read_dir(cromfs_inodenum_t inonum,
                                  uint_fast32_t dir_offset, uint_fast32_t dir_count);

    void forget_blktab();

private:
    void reread_superblock();
    void reread_blktab();
    void reread_fblktab();
    
    void read_block(cromfs_blocknum_t ind, unsigned offset,
                    unsigned char* target, unsigned size);
    
    cromfs_inode_internal read_uncompressed_inode(uint_fast64_t offset);
    
    cromfs_cached_fblock& read_fblock(cromfs_fblocknum_t ind);
    
    uint_fast64_t CalcSizeInBlocks(uint_fast64_t filesize) const;

private:
    int fd; // file handle
    
    cromfs_inode_internal rootdir, inotab;
    cromfs_superblock_internal sblock;
    
    std::map<cromfs_fblocknum_t, cromfs_cached_fblock> cache_fblocks;
    
    std::vector<cromfs_fblock_internal> fblktab;
    std::vector<cromfs_block_storage> blktab;
    
private:
    cromfs(cromfs&);
    void operator=(const cromfs&);
};

const std::string DumpInode(const cromfs_inode_internal& inode);
const std::string DumpBlock(const cromfs_block_storage& block);
