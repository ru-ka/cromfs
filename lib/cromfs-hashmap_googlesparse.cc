#include "endian.hh"

#include "cromfs-hashmap_googlesparse.hh"

template<typename HashType, typename T>
GoogleSparseMap<HashType,T>::GoogleSparseMap()
#ifdef OPTIMAL_GOOGLE_SPARSETABLE
    : data(UINT64_C(0x100000000))
#else
    : data()
#endif
{
}

template<typename HashType, typename T>
GoogleSparseMap<HashType,T>::~GoogleSparseMap()
{
}

template<typename HashType, typename T>
void GoogleSparseMap<HashType,T>::extract(HashType index, T& result) const
{
#ifdef OPTIMAL_GOOGLE_SPARSETABLE
    result = data.get(index);
#else
    typename index_type::const_iterator i = data.find(index);
    if(i != data.end())
        result = i->second;
#endif
}

template<typename HashType, typename T>
void GoogleSparseMap<HashType,T>::set(HashType index, const T& value)
{
#ifdef OPTIMAL_GOOGLE_SPARSETABLE
    data.set(index, value);
#else
    data.insert(std::make_pair(index, value));
#endif
}

template<typename HashType, typename T>
void GoogleSparseMap<HashType,T>::unset(HashType index)
{
    data.erase(index);
}

template<typename HashType, typename T>
bool GoogleSparseMap<HashType,T>::has(HashType index) const
{
#ifdef OPTIMAL_GOOGLE_SPARSETABLE
    return data.test(index);
#else
    return data.find(index) != data.end();
#endif
}



#include "cromfs-blockindex.hh" // for BlockIndexhashType, blocknum etc.
#include "newhash.h"
#define ri goog_ri
#define ai goog_ai
#define si goog_si
/*
typedef GoogleSparseMap<newhash_t,cromfs_blocknum_t> ri;
template ri::GoogleSparseMap();
template ri::~GoogleSparseMap();
template void ri::extract(newhash_t,cromfs_blocknum_t&) const;
template void ri::set(newhash_t,const cromfs_blocknum_t&);
template void ri::unset(newhash_t);
template bool ri::has(newhash_t)const;

typedef GoogleSparseMap<newhash_t,cromfs_block_internal> ai;
template ai::GoogleSparseMap();
template ai::~GoogleSparseMap();
template void ai::extract(newhash_t,cromfs_block_internal&) const;
template void ai::set(newhash_t,const cromfs_block_internal&);
template void ai::unset(newhash_t);
template bool ai::has(newhash_t)const;
*/
typedef GoogleSparseMap<newhash_t,uint_least32_t> si;
template si::GoogleSparseMap();
template si::~GoogleSparseMap();
template void si::extract(newhash_t,uint_least32_t&) const;
template void si::set(newhash_t,const uint_least32_t&);
template void si::unset(newhash_t);
template bool si::has(newhash_t)const;
#undef ri
#undef ai
#undef si
