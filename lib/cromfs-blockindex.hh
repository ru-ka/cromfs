#ifndef bqtCromfsBlockIndexHH
#define bqtCromfsBlockIndexHH

#include "../cromfs-defs.hh"

#include <vector>
#include <string>

#define NO_BLOCK   ((cromfs_blocknum_t)~UINT64_C(0))

/* Block index may contain two different types of things:
 *   index32 -> cromfs_blocknum_t
 *            (real index)
 *   index32 -> cromfs_block_internal
 *            (autoindex)
 *
 * The same data structure may serve both of these purposes.
 * Advantage in doing so would be that when an autoindex entry
 * is turned into a real index entry, no deletion&reinsertion
 * is required.
 *
 * However, we now use two separate indexes to conserve resources.
 */

template<typename Key, typename Value, typename layer>
class block_index_stack
{
    // Vector items are made pointers so that
    // index resizing will not cause copying.
    // It is not a std::list because we still
    // want random access.
    std::vector<layer*> layers;
public:
    size_t size, deleted;
public:
    block_index_stack();
    ~block_index_stack() { Close(); }
    void clear();

    typedef size_t find_index_t;
    bool Find(Key index, Value& result, find_index_t find_index) const;
    void Add(Key index, const Value& value);
    void Del(Key index, const Value& value);

    std::string GetStatistics() const;

private:
    block_index_stack(const block_index_stack&);
    block_index_stack& operator= (const block_index_stack& b);

private:
    void Close();
    //void Clone();
};

#include "cromfs-blockindex.tcc"

#if 0 && (defined(_LP64) || defined(__LP64__))
# include <utility>
# include "rbtree.hh"
# include "allocatornk.hh"

template<typename Pair>
struct Select1st
{
    typename Pair::first_type&       operator() (Pair& x) const { return x.first; }
    const typename Pair::first_type& operator() (const Pair& x) const { return x.first; }
};
template<typename K>
struct Less
{
    bool operator() (const K& a, const K& b) const { return a < b; }
};
#else
# include <map>
#endif
# include "fsballocator.hh"

template<typename K,typename V>
class block_index_stack_simple : public
#if 0 && (defined(_LP64) || defined(__LP64__))
        RbTree<K,
               std::pair<K, V>,
               Select1st<std::pair<K,V> >,
               Less<K>,
               allocatorNk<int, FSBAllocator<int>, uint_least32_t>
               //FSBAllocator<int>
              >
#else
        std::multimap<K,V, std::less<K>,
                      FSBAllocator<int>
                     >
#endif
{
#if 0 && (defined(_LP64) || defined(__LP64__))
    typedef
        RbTree<K,
               std::pair<K, V>,
               Select1st<std::pair<K,V> >,
               Less<K>,
               allocatorNk<int, FSBAllocator<int>, uint_least32_t>
               //FSBAllocator<int>
              > autoindex_base;
#else
    typedef std::multimap<K,V, std::less<K>,
                          FSBAllocator<int>
                         > autoindex_base;
#endif
public:
    struct find_index_t
    {
        typename autoindex_base::const_iterator i;
        bool first, last;

        find_index_t() : i(), first(true), last(false) { }

        find_index_t& operator++() { if(!last) ++i; first=false; return *this; }
        find_index_t operator++(int) const { find_index_t res(*this); ++res; return res; }
    };
public:
    block_index_stack_simple() : autoindex_base(), added(0), deleted(0) { }

    void Del(K index, const V& b)
    {
        for(typename autoindex_base::iterator i = this->lower_bound(index);
            i != autoindex_base::end() && i->first == index;
            ++i)
        {
            if(i->second == b) { this->erase(i); ++deleted; break; }
        }
    }
    void Add(K index, const V& b)
    {
        this->insert(std::make_pair(index, b));
        ++added;
    }
    bool Find(K index, V& res, find_index_t& nmatch) const
    {
        if(nmatch.first)
        {
            nmatch.i     = this->lower_bound(index);
            nmatch.first = false;
        }
        if(nmatch.i == autoindex_base::end()) { nmatch.last = true; return false; }
        if(nmatch.i->first != index) { nmatch.last = true; return false; }
        res = nmatch.i->second;
        return true;
    }
    const std::string GetStatistics() const;
private:
    size_t added, deleted;
};

#endif
