#include "../cromfs-defs.hh"

#include <vector>
#include <string>

#include "fsballocator.hh"
#include "staticallocator.hh"
#include "rangeset.hh"

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

    template<typename T>
    struct CompressedHashLayer
    {
        static const unsigned n_per_bucket = 0x10000;
        static const unsigned n_buckets    = (UINT64_C(1) << 32) / n_per_bucket;
        static const unsigned bucketsize   = n_per_bucket * sizeof(T);

        rangeset<BlockIndexHashType, StaticAllocator<BlockIndexHashType> > hashbits;

        std::vector<unsigned char> buckets[ n_buckets ];

        CompressedHashLayer();

        void extract(BlockIndexHashType crc, T& result)       const;
        void     set(BlockIndexHashType crc, const T& value);
        void   unset(BlockIndexHashType crc);
    private:
        std::vector<unsigned char> dirtybucket;
        size_t dirtybucketno;
        enum { none, ro, rw } dirtystate;

        void flushdirty();
        void load(size_t bucketno);
    };
    std::vector<CompressedHashLayer<cromfs_blocknum_t>     > realindex;
    std::vector<CompressedHashLayer<cromfs_block_internal> > autoindex;
};

/* This global pointer to block_index is required
 * so that cromfs-fblockfun.cc can call
 * the EmergencyFreeSpace() method across module
 * boundaries when necessary.
 */
extern block_index_type* block_index_global;
