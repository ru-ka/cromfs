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
    CompressedHashLayer(uint_fast64_t max = (UINT64_C(1) << 32));
    ~CompressedHashLayer();

    void extract(HashType crc, T& result)       const;
    void     set(HashType crc, const T& value);
    void   unset(HashType crc);
    bool     has(HashType crc) const;

    void Merge(const CompressedHashLayer& b,
               uint_fast32_t target_offset);

    uint_fast64_t   GetLength() const;
    static unsigned GetGranularity() { return n_per_bucket; }
    void Resize(uint_fast64_t length);

public:
    struct NotCopiableError { };
    CompressedHashLayer(const CompressedHashLayer&)
        { throw NotCopiableError(); }
    void operator=(const CompressedHashLayer&)
        { throw NotCopiableError(); }
private:
    unsigned n_buckets;
    static const unsigned n_per_bucket = 0x10000;
    static const unsigned bucketsize   = n_per_bucket * sizeof(T);

    typedef rangeset<HashType, StaticAllocator<HashType> > hashbits_t;
    hashbits_t hashbits;

    std::vector<unsigned char>* buckets;
    std::vector<unsigned char> dirtybucket;
    size_t dirtybucketno;
    enum { none, ro, rw } dirtystate;
    mutable MutexType lock;

    void flushdirty();
    void load(size_t bucketno);
    void set_no_update_hashbits(HashType crc, const T& value);

private:
    static inline size_t calc_n_buckets(uint_fast64_t extent)
    {
        return (extent + n_per_bucket-1) / n_per_bucket;
    }
};

#endif
