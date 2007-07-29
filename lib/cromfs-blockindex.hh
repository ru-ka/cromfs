#include "../cromfs-defs.hh"
#include "crc32.h"

#if 1
# include "bucketcontainer.hh"
#else
# ifdef USE_HASHMAP
#  include <ext/hash_map>
#  include "hash.hh"
# endif
#endif

#define NO_BLOCK   ((cromfs_blocknum_t)~UINT64_C(0))

struct mkcromfs_block_location: public cromfs_block_internal
{
public:
    cromfs_blocknum_t  blocknum __attribute__((packed));

public:
    mkcromfs_block_location()
        : cromfs_block_internal(), blocknum()
    {
    }

    mkcromfs_block_location(
        cromfs_fblocknum_t fbnum, uint_fast32_t sofs,
        cromfs_blocknum_t b)
            : cromfs_block_internal(), blocknum(b)
    {
        cromfs_block_internal::define(fbnum, sofs);
    }

    mkcromfs_block_location(
        const cromfs_block_internal& bi,
        cromfs_blocknum_t b)
            : cromfs_block_internal(bi), blocknum(b)
    {
    }

} __attribute__((packed));

/* Index for reusable material */
/* This structure should be optimized for minimal RAM usage,
 * though its access times should not be very slow either.
 * One important aspect in the design of this structure
 * is knowing you never need to delete anything from it.
 */
class block_index_type
{
public:
    block_index_type() { }
    
    void clear() { last_search.reset(); index.clear(); }

    bool FindByCRC(crc32_t crc, mkcromfs_block_location& result,
                   size_t find_index) const;

    /* Adds a new index entry */
    void Add(crc32_t crc,
             cromfs_fblocknum_t fnum,
             uint_fast32_t startoffs,
             cromfs_blocknum_t bnum);

    /* Puts blocknum into existing entry */
    void Update(crc32_t crc,
                cromfs_fblocknum_t fnum,
                uint_fast32_t startoffs,
                cromfs_blocknum_t bnum);

    block_index_type(const block_index_type& b);
    block_index_type& operator=(const block_index_type& b);

private:
#if 1
    typedef BucketContainer<mkcromfs_block_location> block_index_t;
#else
# ifdef USE_HASHMAP   
    typedef __gnu_cxx::hash_multimap<crc32_t, mkcromfs_block_location> block_index_t;
# else
    typedef std::multimap<crc32_t, mkcromfs_block_location> block_index_t;
# endif
#endif
    block_index_t index;

    mutable struct last_srch
    {
        crc32_t crc;
        size_t index;
        block_index_t::const_iterator i;
        
        last_srch() : crc(0),index( (size_t)-1 ), i() { }
        void reset() { index = (size_t)-1; }
    } last_search;
};
