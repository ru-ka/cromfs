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

#endif
