#include "cromfs-defs.hh"

#include "lib/datacache.hh"

#include <string>
#include <exception>

typedef int cromfs_exception;

/* How many directories to keep cached at the same time maximum */
extern unsigned READDIR_CACHE_MAX_SIZE;

/* How many decompressed fblocks to cache in RAM at the same time maximum */
extern unsigned FBLOCK_CACHE_MAX_SIZE;

typedef std::vector<unsigned char> cromfs_cached_fblock;

class cromfs
{
public:
    /* cromfs methods report errors by throwing an exception.
     * The exception is an "int" type value that is to be interpreted
     * the same way as errno.
     *
     *  Any function may throw an exception if they attempt to do
     *  disk access and the disk access function returns an error
     *  code.
     *  EIO is a special case; it may denote a broken cromfs volume.
     *  Other special cases are listed below where applicable.
     *
     * In addition, they may throw an std::bad_alloc in case of
     * shortage of memory etc.
     */

    /* Opens the filesystem pointed by the given file descriptor.
     * May throw:
     *   EINVAL = Not a cromfs volume or broken cromfs volume
     *   other  = errno reported by system
     */
    cromfs(int fild)
        throw (cromfs_exception, std::bad_alloc);

    /* Deallocates the structures associated with this filesystem. */
    ~cromfs() throw();

    /* To be called after the constructor, before any real
     * file access. Separated from the constructor so that
     * it may use threads in the same context as the rest
     * of the program (fork() in fuse_main confuses otherwise).
     */
    void Initialize() throw (cromfs_exception, std::bad_alloc);

    /* Returns the superblock of the filesystem, indicating various
     * statistics about the filesystem.
     */
    const cromfs_superblock_internal& get_superblock() { return sblock; }

    /* Returns the inode corresponding to the root directory.
     * It is equivalent to read_inode_and_blocks(1), except
     * that it's a bit faster.
     */
    const cromfs_inode_internal&      get_root_inode() { return rootdir; }

    /* Reads data from the file denoted by the inode number. The inode
     * may also point to a directory or a symlink; this is not an error.
     */
    int_fast64_t read_file_data(const cromfs_inodenum_t inonum,
                                uint_fast64_t offset,
                                unsigned char* target, uint_fast64_t size,
                                const char* purpose)
        throw (cromfs_exception, std::bad_alloc);

    /* Reads the inode based on inode number. */
    /* It only reads the inode header, useful for returning data
     * for stat(), but cannot be used by read_file_data().
     * Not const, because it calls read_file_data which will affect fblock_cache.
     */
    const cromfs_inode_internal read_inode(cromfs_inodenum_t inonum)
        throw (cromfs_exception, std::bad_alloc);

    /* Reads the inode based on inode number. */
    /* It reads the inode header and the block table.
     * Use this function when you are going to pass the data
     * to read_file_data() next, or you're interested of the
     * block table for some other reason.
     * Not const, because it calls read_file_data which will affect fblock_cache.
     */
    const cromfs_inode_internal read_inode_and_blocks(cromfs_inodenum_t inonum)
        throw (cromfs_exception, std::bad_alloc);

    /* Returns the contents of the directory denoted by the given inode number.
     *
     * May throw: ENOTDIR = not a directory
     */
    const cromfs_dirinfo read_dir(cromfs_inodenum_t inonum,
                                  uint_fast32_t dir_offset,
                                  uint_fast32_t dir_count)
        throw (cromfs_exception, std::bad_alloc);

    /* Checks if the directory denoted by the inode number contains the
     * given file (denoted by filename). If it does, it returns the file's
     * inode number; if it doesn't, it returns 0.
     *
     * May throw: ENOTDIR = not a directory
     */
    cromfs_inodenum_t dir_lookup(cromfs_inodenum_t inonum,
                                       const std::string& filename)
        throw (cromfs_exception, std::bad_alloc);

    // debugging...
    void forget_blktab(); // frees some RAM, use if cromfs is going to be idle for a while
    const std::string DumpBlock(const cromfs_block_internal& block) const;
    void DumpRAMusage() const;

    /*
     * A variant of read_file_data that takes the inode instead of the
     * inode number.
     * Note: The inode passed to this function must contain the block
     * table (i.e. it must be the result of read_inode_and_blocks().
     */
    int_fast64_t read_file_data(const cromfs_inode_internal& inode,
                                uint_fast64_t offset,
                                unsigned char* target, uint_fast64_t size,
                                const char* purpose)
        throw (cromfs_exception, std::bad_alloc);

    /* A variant of read_file_data that restricts the read
     * to a single fblock. */
    int_fast64_t read_file_data_from_one_fblock_only
        (const cromfs_inode_internal& inode,
         uint_fast64_t offset,
         unsigned char* target, uint_fast64_t size,
         const cromfs_fblocknum_t allowed_fblocknum)
        throw (cromfs_exception, std::bad_alloc);

protected:
    void reread_superblock()
        throw (cromfs_exception, std::bad_alloc);
    void reread_blktab()
        throw (cromfs_exception, std::bad_alloc);
    void reread_fblktab()
        throw (cromfs_exception, std::bad_alloc);

    void read_block(cromfs_blocknum_t ind, uint_fast32_t offset,
                    unsigned char* target,
                    uint_fast32_t size)
        throw (cromfs_exception, std::bad_alloc);

    cromfs_inode_internal read_special_inode
        (uint_fast64_t offset, uint_fast64_t size,
         bool ignore_blocks)
        throw (cromfs_exception, std::bad_alloc);

    cromfs_cached_fblock& read_fblock(cromfs_fblocknum_t ind)
        throw (cromfs_exception, std::bad_alloc);
    cromfs_cached_fblock read_fblock_uncached(cromfs_fblocknum_t ind) const
        throw (cromfs_exception, std::bad_alloc);

protected:
    int fd; // file handle

    cromfs_inode_internal rootdir, inotab;
    cromfs_superblock_internal sblock;

    std::vector<cromfs_fblock_internal> fblktab;
    std::vector<cromfs_block_internal> blktab;

    DataCache<cromfs_inodenum_t, cromfs_dirinfo> readdir_cache;
    DataCache<cromfs_fblocknum_t, cromfs_cached_fblock> fblock_cache;

    uint_fast32_t storage_opts;

private:
    cromfs(cromfs&);
    void operator=(const cromfs&);
};

const std::string DumpInode(const cromfs_inode_internal& inode);
