#include <ctime>
#include <map>
#include <vector>
#include <algorithm>

#include "threadfun.hh"

template<
    typename KeyType,
    typename ValueType>
class DataCache
{
public:
    DataCache() : writelock(),data(), max_size(5), max_age(0) { }
    DataCache(size_t ms, unsigned ma)
        : writelock(),data(), max_size(ms), max_age(ma) { }
    ~DataCache() { }
    
private:
    typedef typename std::map<KeyType, std::pair<time_t, ValueType> >::iterator it;
    typedef typename std::map<KeyType, std::pair<time_t, ValueType> >::const_iterator cit;
public:
    void clear() { ScopedLock lck(writelock); data.clear(); }
    size_t num_entries() const { return data.size(); }
    
    void CheckAges(long count_offset)
    {
        ScopedLock lck(writelock);
        
        std::vector<it> age_order;
        time_t nowtime = time(0);
        for(it j,i = data.begin(); i != data.end(); i=j)
        {
            j = i; ++j;
            if(max_age && std::difftime(nowtime, i->first) >= max_age)
                data.erase(i);
            else if(max_size)
                age_order.push_back(i);
        }
        
        const long effective_max_size = (long)max_size + count_offset;
        
        if(effective_max_size && (long)data.size() > effective_max_size)
        {
            std::sort(age_order.begin(), age_order.end(), CompareAges);
            const long num_kill = age_order.size() - effective_max_size;
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
        ScopedLock lck(writelock);
        
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
    MutexType writelock;
    std::map<KeyType, std::pair<time_t, ValueType> > data;
    size_t max_size;
    int max_age;
};
