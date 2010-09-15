#include "endian.hh"

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

#define DO_COMPRESSION 1

#if HAS_LZO2
# if HAS_ASM_LZO2
#  include <lzo/lzo1x.h>
#  include <lzo/lzo_asm.h>
# else
   /* Bring lzo1x_1_15_compress() and lzo1x_decompress()
    * to the scope of inline compilation.
    */
#  define LZO_EXTERN(x) static x
#  define LZO_PUBLIC(x) static x
#  include "lzo/lzo1x_1o.c"
#  include "lzo/lzo1x_d1.c"
# endif
#else
# include "minilzo.h"
#endif

#include "cromfs-hashmap_lzo.hh"

#include <cstring>

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

template<typename IndexType, typename T>
void CompressedHashLayer<IndexType,T>::extract(IndexType index, T& result) const
{
    const
    size_t bucket = index / n_per_bucket,
           bucketpos = (index % n_per_bucket) * sizeof(T);

    lock.Lock();

    (const_cast<CompressedHashLayer<IndexType,T>*> (this))->
    load(bucket);

    if(likely(bucketpos < dirtybucket.size()))
        std::memcpy(&result, &dirtybucket[bucketpos], sizeof(T));
    else
        result = T();

    lock.Unlock();
}

template<typename IndexType, typename T>
void CompressedHashLayer<IndexType,T>::set(IndexType index, const T& result)
{
    set_no_update_hashbits(index, result);
    hashbits.set(index, index+1);
}

template<typename IndexType, typename T>
void CompressedHashLayer<IndexType,T>::set_no_update_hashbits(IndexType index, const T& result)
{
    const
    size_t bucket = index / n_per_bucket,
           bucketpos = (index % n_per_bucket) * sizeof(T);

    load(bucket);

    if(dirtybucket.size() < bucketpos + sizeof(T))
        dirtybucket.resize(bucketpos + sizeof(T));

    std::memcpy(&dirtybucket[bucketpos], &result, sizeof(T));
    dirtystate = rw;
}

template<typename IndexType, typename T>
void CompressedHashLayer<IndexType,T>::unset(IndexType index)
{
    // It is not really necessary to do this for the hashmap
    // to function properly, but it is nice, because it shrinks
    // the memory usage.
    { // blot the deleted spot with zero-bytes, shrink the bucket if possible.
        const
        size_t bucket = index / n_per_bucket,
               bucketpos = (index % n_per_bucket) * sizeof(T);

        load(bucket);

        // If the deleted position is at the end of the bucket,
        // shrink the bucket until the last item is not zero

        if(unlikely(bucketpos + sizeof(T) >= dirtybucket.size()))
        {
            size_t tpos = bucketpos;
            if(tpos > dirtybucket.size()) tpos = dirtybucket.size();
            tpos /= sizeof(T);

            while(tpos > 0 && IsZeroAt<sizeof(T)>() (&dirtybucket[(tpos-1)*sizeof(T)]))
                --tpos;

            // Resize the bucket if it could be resized
            if(tpos * sizeof(T) != dirtybucket.size())
            {
                dirtybucket.resize(tpos * sizeof(T));
                dirtystate = rw;
            }
            else
                goto BlotItOut;
        }
        else BlotItOut: if(bucketpos+sizeof(T) <= dirtybucket.size())
        {
            // Zero the slot contents
            std::memset(&dirtybucket[bucketpos], 0, sizeof(T));
            dirtystate = rw;
        }
    }
    hashbits.erase(index);
}

template<typename IndexType, typename T>
CompressedHashLayer<IndexType,T>::CompressedHashLayer(uint_fast64_t max)
: n_buckets( calc_n_buckets(max) ),
  hashbits(),
  buckets(new std::vector<unsigned char> [n_buckets]),
  dirtybucket(),
  dirtybucketno(n_buckets),
  dirtystate(none),
  lock()
{
}

template<typename IndexType, typename T>
CompressedHashLayer<IndexType,T>::~CompressedHashLayer()
{
    delete[] buckets;
}

namespace
{
  template<size_t bucketsize>
  struct lzo_bufs
  {
        static unsigned char decombuf[ bucketsize+bucketsize/16+64+3 ];
  };
  template<size_t bucketsize>
  unsigned char lzo_bufs<bucketsize>::decombuf[ bucketsize+bucketsize/16+64+3 ] = {0};

  #if HAS_LZO2
        static char wrkmem[LZO1X_1_15_MEM_COMPRESS] = {0};
  #else
        static char wrkmem[LZO1X_1_MEM_COMPRESS] = {0};
  #endif
}
template<typename IndexType, typename T>
void CompressedHashLayer<IndexType,T>::flushdirty()
{
    if(dirtystate == rw)
    {
#if DO_COMPRESSION
        lzo_uint actual_bucketsize = dirtybucket.size();
  #if HAS_LZO2
        lzo_uint destlen = sizeof lzo_bufs<bucketsize>::decombuf;
        lzo1x_1_15_compress(&dirtybucket[0], actual_bucketsize,
                            lzo_bufs<bucketsize>::decombuf, &destlen,
                            wrkmem);
  #else
        /* FIXME:
             lzo1x_1_compress sometimes triggers
             two kinds of valgrind errors:

==29018== Conditional jump or move depends on uninitialised value(s)
==29018==    at 0x410D78: _ZL20_lzo1x_1_do_compressPKhmPhPmPv (minilzo.c:2858)
==29018==    by 0x4113F1: lzo1x_1_compress (minilzo.c:3071)
==29018==    by 0x4062DB: CompressedHashLayer<unsigned, int>::flushdirty() (cromfs-hashmap_lzo.cc:160)
==29018==    by 0x406E61: CompressedHashLayer<unsigned, int>::load(unsigned long) (cromfs-hashmap_lzo.cc:181)
==29018==
==29018== Use of uninitialised value of size 8
==29018==    at 0x410D7E: _ZL20_lzo1x_1_do_compressPKhmPhPmPv (minilzo.c:2858)
==29018==    by 0x4113F1: lzo1x_1_compress (minilzo.c:3071)
==29018==    by 0x4062DB: CompressedHashLayer<unsigned, int>::flushdirty() (cromfs-hashmap_lzo.cc:160)
==29018==    by 0x406E61: CompressedHashLayer<unsigned, int>::load(unsigned long) (cromfs-hashmap_lzo.cc:181)

         */

        lzo_uint destlen = sizeof lzo_bufs<bucketsize>::decombuf;
        lzo1x_1_compress(&dirtybucket[0], actual_bucketsize,
                         lzo_bufs<bucketsize>::decombuf, &destlen,
                         wrkmem);
  #endif
        {std::vector<unsigned char> replvec;
        buckets[dirtybucketno].swap(replvec);} // free the memory used by the bucket

        {std::vector<unsigned char> replvec(lzo_bufs<bucketsize>::decombuf,
                                            lzo_bufs<bucketsize>::decombuf+destlen); // assign the bucket
        buckets[dirtybucketno].swap(replvec);}
#else
        buckets[dirtybucketno].swap(dirtybucket);
#endif
        dirtystate = ro;
    }
#if !DO_COMPRESSION
    else if(dirtystate == ro)
    {
        buckets[dirtybucketno].swap(dirtybucket);
    }
#endif
}

template<typename IndexType, typename T>
void CompressedHashLayer<IndexType,T>::load(size_t bucketno)
{
    if(dirtystate == none || dirtybucketno != bucketno)
    {
        flushdirty();

        lzo_uint destlen = 0;
        if(!buckets[bucketno].empty())
        {
#if DO_COMPRESSION
  #if HAS_LZO2
    #if HAS_ASM_LZO2
            destlen = bucketsize;
            dirtybucket.resize(bucketsize + 3); // prepare to receive maximum size
            lzo1x_decompress_asm_fast(&buckets[bucketno][0], buckets[bucketno].size(),
                             &dirtybucket[0], &destlen,
                             0/*wrkmem*/);
    #else
            destlen = bucketsize;
            dirtybucket.resize(bucketsize); // prepare to receive maximum size
            lzo1x_decompress(&buckets[bucketno][0], buckets[bucketno].size(),
                             &dirtybucket[0], &destlen,
                             0/*wrkmem*/);
    #endif
  #else
            destlen = bucketsize;
            dirtybucket.resize(bucketsize); // prepare to receive maximum size
            lzo1x_decompress(&buckets[bucketno][0], buckets[bucketno].size(),
                             &dirtybucket[0], &destlen,
                             0/*wrkmem*/);
  #endif
#else
            destlen = buckets[bucketno].size();
            dirtybucket.swap(buckets[bucketno]);
#endif
        }
        dirtybucket.resize(destlen); // shrink to the actual length
        dirtybucketno = bucketno;
        dirtystate = ro;
    }
}

template<typename IndexType, typename T>
bool CompressedHashLayer<IndexType,T>::has(IndexType index) const
{
    return hashbits.find(index) != hashbits.end();
}

template<typename IndexType, typename T>
uint_fast64_t CompressedHashLayer<IndexType,T>::GetLength() const
{
    return n_buckets * uint_fast64_t(n_per_bucket);
}

template<typename IndexType, typename T>
void CompressedHashLayer<IndexType,T>::Resize(uint_fast64_t length)
{
    uint_fast64_t new_n_buckets = calc_n_buckets(length);
    if(new_n_buckets > n_buckets)
    {
        std::vector<unsigned char>* old_buckets = buckets;
        buckets   = new std::vector<unsigned char> [new_n_buckets];

        for(size_t a=0; a<n_buckets; ++a)
            buckets[a].swap(old_buckets[a]);

        n_buckets = new_n_buckets;

        delete[] old_buckets;
    }
}

template<typename IndexType, typename T>
void CompressedHashLayer<IndexType,T>::Merge
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
        hashbits.set( i->lower + target_offset,
                      i->upper + target_offset);
        for(IndexType pos = i->lower; pos != i->upper; ++pos)
        {
            b.extract(pos, tmp);
            set_no_update_hashbits(target_offset + pos, tmp);
        }
    }
}

#include "cromfs-blockindex.hh" // for BlockIndexhashType, blocknum etc.
#define ri lzo_ri
#define ai lzo_ai
#define si lzo_si

#include "newhash.h"
/*
typedef CompressedHashLayer<newhash_t,cromfs_blocknum_t> ri;
template ri::CompressedHashLayer(uint_fast64_t);
template ri::~CompressedHashLayer();
template void ri::extract(newhash_t,cromfs_blocknum_t&) const;
template void ri::set(newhash_t,const cromfs_blocknum_t&);
template void ri::unset(newhash_t);
template bool ri::has(newhash_t)const;
template uint_fast64_t ri::GetLength()const;
template void ri::Merge(const ri&, uint_fast32_t);

typedef CompressedHashLayer<newhash_t,cromfs_block_internal> ai;
template ai::CompressedHashLayer(uint_fast64_t);
template ai::~CompressedHashLayer();
template void ai::extract(newhash_t,cromfs_block_internal&) const;
template void ai::set(newhash_t,const cromfs_block_internal&);
template void ai::unset(newhash_t);
template bool ai::has(newhash_t)const;
template uint_fast64_t ai::GetLength()const;
template void ai::Merge(const ai&, uint_fast32_t);
*/
typedef CompressedHashLayer<newhash_t,uint_least32_t> si;
template si::CompressedHashLayer(uint_fast64_t);
template si::~CompressedHashLayer();
template void si::extract(newhash_t,uint_least32_t&) const;
template void si::set(newhash_t,const uint_least32_t&);
template void si::unset(newhash_t);
template void si::Resize(uint_fast64_t);
template bool si::has(newhash_t)const;
template uint_fast64_t si::GetLength()const;
template void si::Merge(const si&, uint_fast32_t);

#undef ri
#undef ai
#undef si
