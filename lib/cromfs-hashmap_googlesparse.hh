#ifndef bqtCromfsHashMapGoogleSparse
#define bqtCromfsHashMapGoogleSparse

#include "fsballocator.hh"

#if 1
# include <google/sparsetable>
# define OPTIMAL_GOOGLE_SPARSETABLE
#else
# include <google/sparse_hash_map>
#endif

template<typename HashType, typename T>
class GoogleSparseMap
{
public:
    GoogleSparseMap();
    ~GoogleSparseMap();

    void extract(HashType crc, T& result)       const;
    void     set(HashType crc, const T& value);
    void   unset(HashType crc);
    bool     has(HashType crc) const;

    void Close() {}
    void Clone() {}
private:
#ifdef OPTIMAL_GOOGLE_SPARSETABLE
    typedef google::sparsetable<T, 0x8000, uint_fast64_t> index_type;
#else
    typedef google::sparse_hash_map<
        HashType,
        T,
        SPARSEHASH_HASH<HashType>,
        std::equal_to<HashType>,
        FSBAllocator<T>
    > index_type;
#endif
    index_type index;
};

#endif
