#ifndef bqtCromfsHashMapLZO
#define bqtCromfsHashMapLZO

#include <vector>
#include "staticallocator.hh"
#include "fsballocator.hh"
#include "rangeset.hh"
#include "threadfun.hh"

template<typename HashType, typename T>
class CompressedHashLayer
{
public:
    CompressedHashLayer();

    void extract(HashType crc, T& result)       const;
    void     set(HashType crc, const T& value);
    void   unset(HashType crc);
    bool     has(HashType crc) const;

    void Close() {}
    void Clone() {}
private:
    static const unsigned n_per_bucket = 0x10000;
    static const unsigned n_buckets    = (UINT64_C(1) << 32) / n_per_bucket;
    static const unsigned bucketsize   = n_per_bucket * sizeof(T);

    rangeset<HashType, StaticAllocator<HashType> > hashbits;
    //rangeset<HashType, StaticAllocator<HashType> > delbits;

    std::vector<unsigned char> buckets[ n_buckets ];
    std::vector<unsigned char> dirtybucket;
    size_t dirtybucketno;
    enum { none, ro, rw } dirtystate;
    mutable MutexType lock;

    void flushdirty();
    void load(size_t bucketno);
};

#endif
