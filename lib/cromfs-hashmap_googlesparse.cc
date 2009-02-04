#include "endian.hh"
#include "minilzo.h"

#include "cromfs-hashmap_googlesparse.hh"

template<typename HashType, typename T>
GoogleSparseMap<HashType,T>::GoogleSparseMap()
#ifdef OPTIMAL_GOOGLE_SPARSETABLE
    : index(UINT64_C(0x100000000))
#else
    : index()
#endif
{
}

template<typename HashType, typename T>
GoogleSparseMap<HashType,T>::~GoogleSparseMap()
{
}

template<typename HashType, typename T>
void GoogleSparseMap<HashType,T>::extract(HashType crc, T& result) const
{
#ifdef OPTIMAL_GOOGLE_SPARSETABLE
    result = index.get(crc);
#else
    typename index_type::const_iterator i = index.find(crc);
    if(i != index.end())
        result = i->second;
#endif
}

template<typename HashType, typename T>
void GoogleSparseMap<HashType,T>::set(HashType crc, const T& value)
{
#ifdef OPTIMAL_GOOGLE_SPARSETABLE
    index.set(crc, value);
#else
    index.insert(std::make_pair(crc, value));
#endif
}

template<typename HashType, typename T>
void GoogleSparseMap<HashType,T>::unset(HashType crc)
{
    index.erase(crc);
}

template<typename HashType, typename T>
bool GoogleSparseMap<HashType,T>::has(HashType crc) const
{
#ifdef OPTIMAL_GOOGLE_SPARSETABLE
    return index.test(crc);
#else
    return index.find(crc) != index.end();
#endif
}



#include "cromfs-blockindex.hh" // for BlockIndexhashType, blocknum etc.
typedef GoogleSparseMap<BlockIndexHashType,cromfs_blocknum_t> ri;
template ri::GoogleSparseMap();
template ri::~GoogleSparseMap();
template void ri::extract(BlockIndexHashType,cromfs_blocknum_t&) const;
template void ri::set(BlockIndexHashType,const cromfs_blocknum_t&);
//template void ri::unset(BlockIndexHashType);
template bool ri::has(BlockIndexHashType)const;

typedef GoogleSparseMap<BlockIndexHashType,cromfs_block_internal> ai;
template ai::GoogleSparseMap();
template ai::~GoogleSparseMap();
template void ai::extract(BlockIndexHashType,cromfs_block_internal&) const;
template void ai::set(BlockIndexHashType,const cromfs_block_internal&);
template void ai::unset(BlockIndexHashType);
template bool ai::has(BlockIndexHashType)const;
