#include "cromfs-defs.hh" // for USE_HASHMAP
#include "fadvise.hh"

#include <map>
#include <vector>
#include <algorithm>

//#define BUCKET_USE_HASHMAP

#ifdef BUCKET_USE_HASHMAP
# include <tr1/unordered_map>
# include "hash.hh"
#endif

template<typename DataType, typename IntType = uint_fast32_t>
class BucketContainer
{
private:
    static const IntType SplitOff = 0x10000;
private:
    struct BucketData
    {
        DataType data          /*__attribute__((packed))*/;
        uint_least16_t  lo_index __attribute__((packed));
        
        bool CompareValue(const uint_least16_t l) const
            { return lo_index < l; }

        BucketData() { }
        
        BucketData(const DataType& d, uint_fast16_t l)
            : data(d), lo_index(l) { }

    }   __attribute__((packed));
    
private:
    typedef std::vector<BucketData> bucketlist_t;
#ifdef BUCKET_USE_HASHMAP
    typedef std::tr1::unordered_map<uint_least16_t, bucketlist_t> buckets_t;
#else
    typedef std::map<uint_least16_t, bucketlist_t> buckets_t;
#endif
    buckets_t     buckets;
    uint_fast64_t count;

public:
    class const_iterator
    {
    public:
        typedef typename buckets_t::const_iterator hi_t;
        typedef typename bucketlist_t::const_iterator lo_t;
    private:
        const buckets_t* ref;
        hi_t hi;
        lo_t lo;
    public:
        const_iterator() : ref(0),hi(),lo() { }

        const_iterator(const buckets_t* r, const hi_t& h, const lo_t& l)
            : ref(r), hi(h), lo(l) { }
    
        const_iterator(const buckets_t* r, const hi_t& h)
            : ref(r), hi(h), lo()
        {
            if(hi != r->end()) lo = hi->second.begin();
        }
    
        bool operator==(const const_iterator& b) const
            { return hi==b.hi && lo==b.lo; }
    
        const_iterator& operator++()
        {
            if(++lo == (*hi).second.end())
                *this = const_iterator(ref, ++hi);
            return *this;
        }
        
        const DataType& get_value() const
            { return (*lo).data; }

        DataType& get_value_mutable()
            { return const_cast<DataType&>
              (
                 (const_cast<const_iterator const&> (*this)).get_value()
              );
            }

        const IntType get_key() const
            { return (*lo).lo_index + (*hi).first * SplitOff; }
    };
    
    inline uint_fast64_t size() const { return count; }
    
    void clear() { buckets.clear(); count = 0; }

    const_iterator end() const
        { return const_iterator(&buckets, buckets.end(), typename const_iterator::lo_t()); }

    const_iterator begin() const
        { return const_iterator(&buckets, buckets.begin()); }

    const_iterator find(IntType intval) const
    {
        const uint_fast16_t data_hi = intval / SplitOff;
        const uint_fast16_t data_lo = intval % SplitOff;
        
        typename buckets_t::const_iterator i = buckets.find(data_hi);
        if(i == buckets.end()) return end();
        
        const bucketlist_t& bucket = i->second;
        
        //MadviseRandom(&bucket[0], bucket.size() * sizeof(bucket[0]));
        
        typename bucketlist_t::const_iterator j =
            std::lower_bound(bucket.begin(), bucket.end(),
                data_lo,
                std::mem_fun_ref(&BucketData::CompareValue) );
        if(j == bucket.end() || (*j).lo_index != data_lo)
            return end();
        
        return const_iterator(&buckets, i, j);
    }
    
    template<typename TempIntType>
    void insert(const std::pair<TempIntType, DataType>& data)
    {
        const uint_fast16_t data_hi = data.first / SplitOff;
        const uint_fast16_t data_lo = data.first % SplitOff;
        
        bucketlist_t& bucket = buckets[data_hi];

        typename bucketlist_t::iterator j =
            std::lower_bound(bucket.begin(), bucket.end(),
                data_lo,
                std::mem_fun_ref(&BucketData::CompareValue) );

        /* Aiee, this is slow. */
        bucket.insert(j, BucketData(data.second, data_lo) );
        ++count;
    }
    
    template<typename TempIntType>
    void erase(const std::pair<TempIntType, DataType>& data)
    {
        const uint_fast16_t data_hi = data.first / SplitOff;
        const uint_fast16_t data_lo = data.first % SplitOff;
        
        bucketlist_t& bucket = buckets[data_hi];

        typename bucketlist_t::iterator j =
            std::lower_bound(bucket.begin(), bucket.end(),
                data_lo,
                std::mem_fun_ref(&BucketData::CompareValue) );

        while(!(j == bucket.end()))
        {
            if( (*j).lo_index != data_lo ) return;
            if( (*j).data == data.second ) break;
            ++j;
        }
        if(j != bucket.end())
        {
            bucket.erase(j);
            --count;
        }
    }
};
