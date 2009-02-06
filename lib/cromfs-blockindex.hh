#ifndef bqtCromfsBlockIndexHH
#define bqtCromfsBlockIndexHH

#include "../cromfs-defs.hh"

#include <vector>
#include <string>

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

template<typename Key, typename Value>
class block_index_stack
{
    class layer;
    std::vector<layer*> index;
public:
    size_t size, deleted;
public:
    block_index_stack();
    block_index_stack(const block_index_stack&);
    block_index_stack& operator= (const block_index_stack& b);
    ~block_index_stack() { Close(); }
    void clear();

    bool Find(Key crc, Value& result, size_t find_index) const;
    void Add(Key crc, const Value& value);
    void Del(Key crc, const Value& value);

private:
    void Close();
    void Clone();
};

#endif
