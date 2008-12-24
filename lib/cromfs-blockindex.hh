#include "../cromfs-defs.hh"

#include <google/sparse_hash_map>

#include "fsballocator.hh"

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
    block_index_type() : realindex(), autoindex() { Init(); }

    block_index_type(const block_index_type& b)
        : realindex(b.realindex),
          autoindex(b.autoindex)
    {
        Clone();
    }
    block_index_type& operator= (const block_index_type& b)
    {
        if(&b != this)
        {
            Close();
            realindex = b.realindex;
            autoindex = b.autoindex;
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
        realindex.clear();
        autoindex.clear();
    }

private:
    void Init();
    void Close();
    void Clone();

    typedef uint_least64_t HashIndexType; // a pair of newhash (32-bit) & collision index (32-bit)

    typedef google::sparse_hash_map<
        HashIndexType,
        cromfs_blocknum_t,
        SPARSEHASH_HASH<HashIndexType>,
        std::equal_to<HashIndexType>,
        FSBAllocator<cromfs_blocknum_t>
    > realindex_type;

    realindex_type realindex;

    typedef google::sparse_hash_map<
        HashIndexType,
        cromfs_block_internal,
        SPARSEHASH_HASH<HashIndexType>,
        std::equal_to<HashIndexType>,
        FSBAllocator<cromfs_block_internal>
    > autoindex_type;

    autoindex_type autoindex;
};

/* This global pointer to block_index is required
 * so that cromfs-fblockfun.cc can call
 * the EmergencyFreeSpace() method across module
 * boundaries when necessary.
 */
extern block_index_type* block_index_global;
