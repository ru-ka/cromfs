#include "../cromfs-defs.hh"

#if 1
# include "bucketcontainer.hh"
#else
# if USE_HASHMAP
#  include <ext/hash_map>
#  include "hash.hh"
# endif
#endif

#define NO_BLOCK   ((cromfs_blocknum_t)~UINT64_C(0))

/* Methods for calculating the block hash (used to be CRC-32) */
typedef uint_least32_t BlockIndexHashType;
extern BlockIndexHashType
    BlockIndexHashCalc(const unsigned char* buf, unsigned long size);


/* Block index may contain two different types of things:
 *   crc32 -> cromfs_blocknum_t
 *            (real index)
 *   crc32 -> cromfs_block_internal
 *            (autoindex)
 *
 * The same data structure may serve both of these purposes.
 * Advantage in doing so would be that when an autoindex entry
 * is turned into a real index entry, no deletion&reinsertion
 * is required.
 *
 * However, we now use two separate indexes to conserve resources.
 */

/* Index for reusable material */
/* This structure should be optimized for minimal RAM usage,
 * though its access times should not be very slow either.
 * One important aspect in the design of this structure
 * is knowing you never need to delete anything from it.
 */
class block_index_type
{
public:
    bool FindRealIndex(BlockIndexHashType crc, cromfs_blocknum_t& result,     size_t find_index) const;
    bool FindAutoIndex(BlockIndexHashType crc, cromfs_block_internal& result, size_t find_index) const;

    void AddRealIndex(BlockIndexHashType crc, cromfs_blocknum_t value);
    void AddAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value);

    void DelAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value);

    bool EmergencyFreeSpace(bool Auto=true, bool Real=true);

public:
    block_index_type() : realindex_fds(), autoindex_fds() { }

    block_index_type(const block_index_type& b)
        : realindex_fds(b.realindex_fds),
          autoindex_fds(b.autoindex_fds)
    {
        Clone();
    }
    block_index_type& operator= (const block_index_type& b)
    {
        if(&b != this)
        {
            Close();
            realindex_fds = b.realindex_fds;
            autoindex_fds = b.autoindex_fds;
            Clone();
        }
        return *this;
    }

    ~block_index_type()
    {
        Close();
    }

    void clear()
    {
        Close();
        realindex_fds.clear();
        autoindex_fds.clear();
    }

private:
    void Clone()
    {
        for(size_t a=0; a<realindex_fds.size(); ++a)
            realindex_fds[a] = dup(realindex_fds[a]);
        for(size_t a=0; a<autoindex_fds.size(); ++a)
            autoindex_fds[a] = dup(autoindex_fds[a]);
    }
    void Close()
    {
        for(size_t a=0; a<realindex_fds.size(); ++a)
        {
            close(realindex_fds[a]);
        }
        for(size_t a=0; a<autoindex_fds.size(); ++a)
        {
            close(autoindex_fds[a]);
        }
    }
    size_t new_real();
    size_t new_auto();

    inline uint_fast64_t RealPos(BlockIndexHashType crc) const
    {
        uint_fast64_t res = crc; res *= 4; return res;
    }
    inline uint_fast64_t AutoPos(BlockIndexHashType crc) const
    {
        uint_fast64_t res = crc; res *= 8; return res;
    }

    const std::string GetRealFn(size_t index) const;
    const std::string GetAutoFn(size_t index) const;

private:
    std::vector<int> realindex_fds;
    std::vector<int> autoindex_fds;
};

/* This global pointer to block_index is required
 * so that cromfs-fblockfun.cc can call
 * the EmergencyFreeSpace() method across module
 * boundaries when necessary.
 */
extern block_index_type* block_index_global;
