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
 * However, we now use two separate indexes to conserve RAM.
 * Also, the performance of std::multimap<> matches the intended
 * use of real_index better than that of BucketContainer does.
 */

/* Index for reusable material */
/* This structure should be optimized for minimal RAM usage,
 * though its access times should not be very slow either.
 * One important aspect in the design of this structure
 * is knowing you never need to delete anything from it.
 */
class block_index_type
{
private:
    template<typename IndexT, typename LastT, typename ValueT>
    static bool FindBucketIndex(crc32_t crc, ValueT& result, size_t find_index,
                          const IndexT& index, LastT& lasts)
    {
        if(crc != lasts.crc
        || find_index < lasts.index)
        {
            lasts.crc   = crc;
            lasts.i     = index.find(crc);
            lasts.index = 0;
        }
        if(lasts.i == index.end()) return false;

        while(lasts.index < find_index)
        {
            ++lasts.i;
            ++lasts.index;

            if(lasts.i == index.end()) return false;
            
            if(lasts.i.get_key() != crc)
            {
                lasts.i = index.end();
                return false;
            }
        }
        if(lasts.i == index.end())
            return false;
        
        result = lasts.i.get_value();
        return true;
    }

    template<typename IndexT, typename LastT, typename ValueT>
    static bool FindMapIndex(crc32_t crc, ValueT& result, size_t find_index,
                          const IndexT& index, LastT& lasts)
    {
        if(crc != lasts.crc
        || find_index < lasts.index)
        {
            lasts.crc   = crc;
            lasts.i     = index.find(crc);
            lasts.index = 0;
        }
        if(lasts.i == index.end()) return false;

        while(lasts.index < find_index)
        {
            ++lasts.i;
            ++lasts.index;

            if(lasts.i == index.end()) return false;
            
            if(lasts.i->first != crc)
            {
                lasts.i = index.end();
                return false;
            }
        }
        if(lasts.i == index.end())
            return false;
        
        result = lasts.i->second;
        return true;
    }

public:
    block_index_type(): autoindex(),realindex(),
                        last_search_a(),
                        last_search_r()
                       { }
    
    void clear() { last_search_a.reset(); autoindex.clear();
                   last_search_r.reset(); realindex.clear();
                 }
    
    bool FindRealIndex(crc32_t crc, cromfs_blocknum_t& result,     size_t find_index) const
        { return FindMapIndex(crc, result, find_index, realindex, last_search_r); }

    bool FindAutoIndex(crc32_t crc, cromfs_block_internal& result, size_t find_index) const
        { return FindBucketIndex(crc, result, find_index, autoindex, last_search_a); }
    
    void AddRealIndex(crc32_t crc, cromfs_blocknum_t value)
        { last_search_r.reset();
          realindex.insert(std::make_pair(crc, value)); }
    
    void AddAutoIndex(crc32_t crc, const cromfs_block_internal& value)
        { last_search_a.reset();
          autoindex.insert(std::make_pair(crc, value)); }

    void DelAutoIndex(crc32_t crc, const cromfs_block_internal& value)
        { last_search_a.reset();
          autoindex.erase(std::make_pair(crc, value)); }

    block_index_type(const block_index_type& b);
    block_index_type& operator=(const block_index_type& b);

private:
    typedef BucketContainer<cromfs_block_internal> autoindex_t;
    autoindex_t autoindex;
    
    typedef std::multimap<crc32_t, cromfs_blocknum_t> realindex_t;
    realindex_t realindex;

    mutable struct last_srch_a
    {
        crc32_t crc;
        size_t index;
        autoindex_t::const_iterator i;
        
        last_srch_a() : crc(0),index( (size_t)-1 ), i() { }
        void reset() { index = (size_t)-1; }
    } last_search_a;

    mutable struct last_srch_r
    {
        crc32_t crc;
        size_t index;
        realindex_t::const_iterator i;
        
        last_srch_r() : crc(0),index( (size_t)-1 ), i() { }
        void reset() { index = (size_t)-1; }
    } last_search_r;
};
