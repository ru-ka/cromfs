#ifndef bqtCromfsHashMapGoogleSparse
#define bqtCromfsHashMapGoogleSparse

#include "fsballocator.hh"

#include <google/sparse_hash_map>


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
    typedef google::sparse_hash_map<
        HashType,
        T,
        SPARSEHASH_HASH<HashType>,
        std::equal_to<HashType>,
        FSBAllocator<T>
    > index_type;

    index_type index;
};

#endif
