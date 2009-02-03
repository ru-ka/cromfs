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
    const
    size_t bucket = crc / n_per_bucket,
           bucketpos = (crc % n_per_bucket) * sizeof(T);

    load(bucket);

    if(dirtybucket.size() < bucketpos + sizeof(T))
        dirtybucket.resize(bucketpos + sizeof(T));

    std::memcpy(&dirtybucket[bucketpos], &result, sizeof(T));
    hashbits.set(crc, crc+1);
    //delbits.erase(crc);
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
    //delbits.set(crc, crc+1);
}

template<typename HashType, typename T>
CompressedHashLayer<HashType,T>::CompressedHashLayer()
: hashbits(), //delbits(),
  dirtybucket(),
  dirtybucketno(n_buckets),
  dirtystate(none),
  lock()
{
}

template<typename HashType, typename T>
void CompressedHashLayer<HashType,T>::flushdirty()
{
    if(dirtystate == rw)
    {
        size_t actual_bucketsize = dirtybucket.size();
#if 1
        /*
        typedef rangeset<HashType, StaticAllocator<HashType> > rtype;
        rtype intersection(delbits);
        intersection.erase_before( (dirtybucketno  ) * n_per_bucket );
        intersection.erase_after(  (dirtybucketno+1) * n_per_bucket );

        for(typename rtype::const_iterator i = intersection.begin(); i != intersection.end(); ++i)
        {
            const size_t bucketpos1 = (i->lower % n_per_bucket) * sizeof(T);
            const size_t bucketpos2 = (i->upper % n_per_bucket) * sizeof(T);
            if(bucketpos1 < dirtybucket.size())
            {
                size_t nbytes = bucketpos2-bucketpos1;
                if(bucketpos1 + nbytes > dirtybucket.size())
                    nbytes = dirtybucket.size() - bucketpos1;

                std::memset(&dirtybucket[bucketpos1], 0, nbytes);
           }
        }
        delbits.erase((dirtybucketno  ) * n_per_bucket,
                      (dirtybucketno+1) * n_per_bucket);
        */

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

#include "cromfs-blockindex.hh" // for BlockIndexhashType, blocknum etc.
typedef CompressedHashLayer<BlockIndexHashType,cromfs_blocknum_t> ri;
template ri::CompressedHashLayer();
template void ri::extract(BlockIndexHashType,cromfs_blocknum_t&) const;
template void ri::set(BlockIndexHashType,const cromfs_blocknum_t&);
//template void ri::unset(BlockIndexHashType);
template bool ri::has(BlockIndexHashType)const;
//template void ri::Close();
//template void ri::Clone();

typedef CompressedHashLayer<BlockIndexHashType,cromfs_block_internal> ai;
template ai::CompressedHashLayer();
template void ai::extract(BlockIndexHashType,cromfs_block_internal&) const;
template void ai::set(BlockIndexHashType,const cromfs_block_internal&);
template void ai::unset(BlockIndexHashType);
template bool ai::has(BlockIndexHashType)const;
//template void ai::Close();
//template void ai::Clone();
