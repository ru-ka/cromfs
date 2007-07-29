#include <ctime>
#include <map>
#include <vector>
#include <algorithm>

template<
    typename KeyType,
    typename ValueType>
class DataCache
{
public:
    DataCache() : max_size(5), max_age(0) { }
    DataCache(size_t ms, unsigned ma): max_size(ms), max_age(ma) { }
    ~DataCache() { }
    
private:
    typedef typename std::map<KeyType, std::pair<time_t, ValueType> >::iterator it;
    typedef typename std::map<KeyType, std::pair<time_t, ValueType> >::const_iterator cit;
public:
    void clear() { data.clear(); }
    
    void CheckAges()
    {
        std::vector<it> age_order;
        for(it j,i = data.begin(); i != data.end(); i=j)
        {
            j = i; ++j;
            if(max_age &&
                (time_t)(i->first + max_age) < time(0))
                data.erase(i);
            else if(max_size)
                age_order.push_back(i);
        }
        if(max_size && data.size() > max_size)
        {
            std::sort(age_order.begin(), age_order.end(), CompareAges);
            long num_kill = max_size - age_order.size();
            for(long a=0; a<num_kill; ++a)
                data.erase(age_order[a]);
        }
    }
    
    ValueType* Find(const KeyType key)
    {
        it i = data.find(key);
        if(i != data.end())
        {
            i->second.first = std::time(0);
            return &i->second.second;
        }
        return 0;
    }
    
    bool Has(const KeyType key) const
    {
        cit i = data.find(key);
        return i != data.end();
    }
    
    ValueType& Put(const KeyType key, const ValueType& value)
    {
        std::pair<time_t, ValueType>& val = data[key];
        val.first  = std::time(0);
        return val.second = value;
    }

private:
    static bool CompareAges(const it& a, const it& b)
    {
        return a->second.first < b->second.first;
    }
    
private:
    std::map<KeyType, std::pair<time_t, ValueType> > data;
    size_t max_size;
    int max_age;
};
