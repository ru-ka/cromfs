#ifndef bqtCromfsHashMapLZO
#define bqtCromfsHashMapLZO

#include <vector>
#include "rangeset.hh"
#include "threadfun.hh"

template<typename IndexType, typename T>
class CompressedHashLayer
{
public:
    CompressedHashLayer(uint_fast64_t max = (UINT64_C(1) << (8*sizeof(IndexType))));
    ~CompressedHashLayer();

    void extract(IndexType index, T& result)       const;
    void     set(IndexType index, const T& value);
    void   unset(IndexType index);
    bool     has(IndexType index) const;

    void Merge(const CompressedHashLayer& b,
               uint_fast32_t target_offset);

    uint_fast64_t   GetLength() const;
    static unsigned GetGranularity() { return n_per_bucket; }
    void Resize(uint_fast64_t length);

private:
    CompressedHashLayer(const CompressedHashLayer&);
    void operator=(const CompressedHashLayer&);
private:
    unsigned n_buckets;
    static const unsigned n_per_bucket = 0x1000;
    // ^ Bucket size. A magic constant, chosen by arbitration.

    static const unsigned bucketsize   = n_per_bucket * sizeof(T);

    typedef rangeset<IndexType> hashbits_t;
    hashbits_t hashbits;

    std::vector<unsigned char>* buckets;
    std::vector<unsigned char> dirtybucket;
    size_t dirtybucketno;
    enum { none, ro, rw } dirtystate;
    mutable MutexType lock;

    void flushdirty();
    void load(size_t bucketno);
    void set_no_update_hashbits(IndexType index, const T& value);

private:
    static inline size_t calc_n_buckets(uint_fast64_t extent)
    {
        return (extent + n_per_bucket-1) / n_per_bucket;
    }
};

#endif
