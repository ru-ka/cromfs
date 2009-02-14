#define DEBUG_APPEND  0
#define DEBUG_OVERLAP 0
#define DEBUG_FBLOCKINDEX 0

#include "../cromfs-defs.hh"

#include "mmapping.hh"
#include "datareadbuf.hh"
#include "append.hh"

#include "threadfun.hh"

#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

class mkcromfs_fblockset;

class mkcromfs_fblock
{
public:
    explicit mkcromfs_fblock(int id);
    ~mkcromfs_fblock();

    MutexType& GetMutex() const { return lock; }
    size_t size() const { Decompress(); return filesize; }

    void EnsureMMapped() const; // For efficient InitDataReadBuffer access
    void EnsureMMapped();

    void InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size,
                            uint_fast32_t req_offset,
                            uint_fast32_t req_size) const;
    void InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size,
                            uint_fast32_t req_offset,
                            uint_fast32_t req_size);

    void InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size) const
        { InitDataReadBuffer(Buffer, size, 0, ~(uint_fast32_t)0); }

    void put_appended_raw(const AppendInfo& append,
                          const unsigned char* data,
                          size_t               length);

    void put_compressed(const std::vector<unsigned char>& data);
    std::vector<unsigned char> get_compressed();

    void Delete();

    uint_fast32_t getfilesize() const { return filesize; }
    bool          is_uncompressed() const { return !is_compressed; }

private:
    std::string getfn() const;
    void Decompress() const
        { (const_cast<mkcromfs_fblock*> (this))->Decompress(); }
    void Decompress();
    void EnsureOpen();

protected:
    friend class mkcromfs_fblockset;
    bool Close();
    void Compress();
    void Unmap();

private:
    mkcromfs_fblock(const mkcromfs_fblock&);
    void operator=(const mkcromfs_fblock&);
private:
    mutable MutexType lock;

    int                  fblock_disk_id;
    uint_fast32_t        filesize; // either compressed or raw size
    MemMappingType<true> mapped;
    int                  fd;
    bool                 is_compressed;
};

/* This is the actual front end for fblocks in mkcromfs.
 * However, references to mkcromfs_fblock are allowed.
 */
class mkcromfs_fblockset
{
public:
    mkcromfs_fblockset();
    ~mkcromfs_fblockset();

    size_t size() const { return fblocks.size(); }

    const mkcromfs_fblock& operator[] (size_t index) const { return *fblocks[index].ptr; }
    mkcromfs_fblock& operator[] (size_t index);

    int FindFblockThatHasAtleastNbytesSpace(size_t howmuch) const;
    void UpdateFreeSpaceIndex(cromfs_fblocknum_t fnum, size_t howmuch);

    void FreeSomeResources();

protected:
    friend class mkcromfs_fblock;
    bool CloseSome();

private:
    mkcromfs_fblockset(const mkcromfs_fblockset&);
    void operator=(const mkcromfs_fblockset&);
public:
    struct fblock_rec
    {
        mkcromfs_fblock* ptr;
        time_t           last_access;
        size_t           space;
    };
private:
    std::vector<fblock_rec> fblocks;
};

extern void set_fblock_name_pattern(const std::string& pat);

/* This global pointer to mkcromfs_fblockset is required
 * so that individual fblocks can ask fellow fblocks to
 * close their lingering fds.
 */
extern mkcromfs_fblockset* fblockset_global;
