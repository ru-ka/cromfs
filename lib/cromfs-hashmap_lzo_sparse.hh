#ifndef bqtCromfsHashMapLZOsparse
#define bqtCromfsHashMapLZOsparse

#include <map>

#include "fsballocator.hh"
#include "cromfs-hashmap_lzo.hh"

template<typename HashType, typename T>
class CompressedHashLayer_Sparse
{
public:
    CompressedHashLayer_Sparse();
    ~CompressedHashLayer_Sparse();

    void extract(HashType index, T& result)       const;
    void     set(HashType index, const T& value);
    void   unset(HashType index);
    bool     has(HashType index) const;

private:
    typedef CompressedHashLayer<HashType,T> array_t;

    /* Store array_t* instead of array_t in order to
     * remove the need to use copy constructors when
     * putting data to the map.
     */
    typedef std::map<HashType/*begin*/, array_t*,
                     std::less<HashType>,
                     FSBAllocator<std::pair<const HashType, array_t*> >
                    > map_t;

    map_t data;
};

#endif
