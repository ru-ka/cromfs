#ifndef bqtCromfsHashMapLZOsparse
#define bqtCromfsHashMapLZOsparse

#include <map>

#include "cromfs-hashmap_lzo.hh"

template<typename HashType, typename T>
class CompressedHashLayer_Sparse
{
public:
    CompressedHashLayer_Sparse();
    ~CompressedHashLayer_Sparse();

    void extract(HashType crc, T& result)       const;
    void     set(HashType crc, const T& value);
    void   unset(HashType crc);
    bool     has(HashType crc) const;

private:
    typedef CompressedHashLayer<HashType,T> array_t;
    typedef std::map<HashType/*begin*/, array_t*> map_t;

    map_t data;
};

#endif
