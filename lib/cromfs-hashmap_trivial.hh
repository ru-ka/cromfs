#ifndef bqtCromfsHashMapTrivial
#define bqtCromfsHashMapTrivial

#include <map>

template<typename IndexType, typename T,
         typename allocator = std::allocator<std::pair<const IndexType, T> >
        >
class TrivialHashLayer
{
public:
    TrivialHashLayer() : data() { }
    ~TrivialHashLayer() { }

    void extract(IndexType index, T& result)       const
    {
        typename std::map<IndexType, T>::const_iterator
            i = data.find(index);
        if(i != data.end())
            result = i->second;
    }
    void     set(IndexType index, const T& value)
    {
        data.insert(std::pair<IndexType,T> (index,value));
    }
    void   unset(IndexType index)
    {
        data.erase(index);
    }
    bool     has(IndexType index) const
    {
        return data.find(index) != data.end();
    }

private:
    TrivialHashLayer(const TrivialHashLayer&);
    void operator=(const TrivialHashLayer&);
private:
    std::map<IndexType, T, std::less<IndexType>, allocator> data;
};

#endif
