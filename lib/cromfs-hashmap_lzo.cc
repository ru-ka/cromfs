#include "endian.hh"
#include "minilzo.h"

#include "cromfs-hashmap_lzo.hh"

template<size_t size>
struct IsZeroAt
{
    bool operator() (const unsigned char* p) const
    {
        for(size_t a=0; a<size; ++a)
            if(p[a]) return false;
        return true;
    }
};
template<>
struct IsZeroAt<sizeof(uint_least16_t)>
{
    bool operator() (const unsigned char* p) const { return !*(const uint_least16_t*)p; }
};
template<>
struct IsZeroAt<sizeof(uint_least32_t)>
{
    bool operator() (const unsigned char* p) const { return !*(const uint_least32_t*)p; }
};
template<>
struct IsZeroAt<sizeof(uint_least64_t)>
{
    bool operator() (const unsigned char* p) const { return !*(const uint_least64_t*)p; }
};

template<typename HashType, typename T>
void CompressedHashLayer<HashType,T>::extract(HashType crc, T& result) const
{
    const
    size_t bucket = crc / n_per_bucket,
           bucketpos = (crc % n_per_bucket) * sizeof(T);

    lock.Lock();

    (const_cast<CompressedHashLayer<HashType,T>*> (this))->
    load(bucket);

    if(likely(bucketpos < dirtybucket.size()))
        std::memcpy(&result, &dirtybucket[bucketpos], sizeof(T));
    else
        result = T();

    lock.Unlock();
}

template<typename HashType, typename T>
void CompressedHashLayer<HashType,T>::set(HashType crc, const T& result)
{
    set_no_update_hashbits(crc, result);
    hashbits.set(crc, crc+1);
}

template<typename HashType, typename T>
void CompressedHashLayer<HashType,T>::set_no_update_hashbits(HashType crc, const T& result)
{
    const
    size_t bucket = crc / n_per_bucket,
           bucketpos = (crc % n_per_bucket) * sizeof(T);

    load(bucket);

    if(dirtybucket.size() < bucketpos + sizeof(T))
        dirtybucket.resize(bucketpos + sizeof(T));

    std::memcpy(&dirtybucket[bucketpos], &result, sizeof(T));
    dirtystate = rw;
}

template<typename HashType, typename T>
void CompressedHashLayer<HashType,T>::unset(HashType crc)
{
    // It is not really necessary to do this for the hashmap
    // to function properly, but it is nice, because it shrinks
    // the memory usage.
    { // blot the deleted spot with zero-bytes, shrink the bucket if possible.
        const
        size_t bucket = crc / n_per_bucket,
               bucketpos = (crc % n_per_bucket) * sizeof(T);

        load(bucket);

        if(unlikely(bucketpos + sizeof(T) >= dirtybucket.size()))
        {
            size_t tpos = bucketpos;
            if(tpos > dirtybucket.size()) tpos = dirtybucket.size();
            tpos /= sizeof(T);

            while(tpos > 0 && IsZeroAt<sizeof(T)>() (&dirtybucket[(tpos-1)*sizeof(T)]))
                --tpos;

            if(tpos * sizeof(T) != dirtybucket.size())
            {
                dirtybucket.resize(tpos * sizeof(T));
                dirtystate = rw;
            }
        }
        else
        {
            std::memset(&dirtybucket[bucketpos], 0, sizeof(T));
            dirtystate = rw;
        }
    }
    hashbits.erase(crc);
}

template<typename HashType, typename T>
CompressedHashLayer<HashType,T>::CompressedHashLayer(uint_fast64_t max)
: n_buckets( calc_n_buckets(max) ),
  hashbits(),
  buckets(new std::vector<unsigned char> [n_buckets]),
  dirtybucket(),
  dirtybucketno(n_buckets),
  dirtystate(none),
  lock()
{
}

template<typename HashType, typename T>
CompressedHashLayer<HashType,T>::~CompressedHashLayer()
{
    delete[] buckets;
}

template<typename HashType, typename T>
void CompressedHashLayer<HashType,T>::flushdirty()
{
    if(dirtystate == rw)
    {
        size_t actual_bucketsize = dirtybucket.size();
#if 1
        const size_t decom_max = bucketsize+bucketsize/16+64+3;
        static unsigned char decombuf[decom_max];
        lzo_uint destlen = decom_max;
        char wrkmem[LZO1X_1_MEM_COMPRESS];
        lzo1x_1_compress(&dirtybucket[0], actual_bucketsize,
                         decombuf, &destlen,
                         wrkmem);

        {std::vector<unsigned char> replvec;
        buckets[dirtybucketno].swap(replvec);}

        {std::vector<unsigned char> replvec(decombuf, decombuf+destlen);
        buckets[dirtybucketno].swap(replvec);}
#else
        buckets[dirtybucketno] = dirtybucket;
#endif
        dirtystate = ro;
    }
}

template<typename HashType, typename T>
void CompressedHashLayer<HashType,T>::load(size_t bucketno)
{
    if(dirtystate == none || dirtybucketno != bucketno)
    {
        flushdirty();

        lzo_uint destlen = 0;
        if(!buckets[bucketno].empty())
        {
#if 1
            destlen = bucketsize;
            dirtybucket.resize(bucketsize); // prepare to receive maximum size
            lzo1x_decompress(&buckets[bucketno][0], buckets[bucketno].size(),
                             &dirtybucket[0], &destlen,
                             0);
#else
            destlen = buckets[bucketno].size();
            dirtybucket = buckets[bucketno];
#endif
        }

        dirtybucket.resize(destlen); // shrink to the actual length
        dirtybucketno = bucketno;
        dirtystate = ro;
    }
}

template<typename HashType, typename T>
bool CompressedHashLayer<HashType,T>::has(HashType crc) const
{
    return hashbits.find(crc) != hashbits.end();
}

template<typename HashType, typename T>
uint_fast64_t CompressedHashLayer<HashType,T>::GetLength() const
{
    return n_buckets * uint_fast64_t(n_per_bucket);
}

template<typename HashType, typename T>
void CompressedHashLayer<HashType,T>::Resize(uint_fast64_t length)
{
    uint_fast64_t new_n_buckets = calc_n_buckets(length);
    if(new_n_buckets > n_buckets)
    {
        std::vector<unsigned char>* new_buckets =
            new std::vector<unsigned char> [new_n_buckets];
        for(size_t a=0; a<n_buckets; ++a)
            new_buckets[a].swap(buckets[a]);
        delete[] buckets;
        buckets   = new_buckets;
        n_buckets = new_n_buckets;
    }
}

template<typename HashType, typename T>
void CompressedHashLayer<HashType,T>::Merge
    (const CompressedHashLayer& b,
     uint_fast32_t target_offset)
{
    Resize(target_offset + b.GetLength());
    T tmp;
    for(typename hashbits_t::const_iterator
        i = b.hashbits.begin();
        i != b.hashbits.end();
        ++i)
    {
        hashbits.set( i->lower + target_offset, i->upper + target_offset);
        for(HashType pos = i->lower; pos != i->upper; ++pos)
        {
            b.extract(pos, tmp);
            set_no_update_hashbits(target_offset + pos, tmp);
        }
    }
}

#include "cromfs-blockindex.hh" // for BlockIndexhashType, blocknum etc.
typedef CompressedHashLayer<BlockIndexHashType,cromfs_blocknum_t> ri;
template ri::CompressedHashLayer(uint_fast64_t);
template ri::~CompressedHashLayer();
template void ri::extract(BlockIndexHashType,cromfs_blocknum_t&) const;
template void ri::set(BlockIndexHashType,const cromfs_blocknum_t&);
//template void ri::unset(BlockIndexHashType);
template bool ri::has(BlockIndexHashType)const;
template uint_fast64_t ri::GetLength()const;
template void ri::Merge(const ri&, uint_fast32_t);

typedef CompressedHashLayer<BlockIndexHashType,cromfs_block_internal> ai;
template ai::CompressedHashLayer(uint_fast64_t);
template ai::~CompressedHashLayer();
template void ai::extract(BlockIndexHashType,cromfs_block_internal&) const;
template void ai::set(BlockIndexHashType,const cromfs_block_internal&);
template void ai::unset(BlockIndexHashType);
template bool ai::has(BlockIndexHashType)const;
template uint_fast64_t ai::GetLength()const;
template void ai::Merge(const ai&, uint_fast32_t);
