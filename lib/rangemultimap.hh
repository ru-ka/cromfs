#ifndef bqtRangeMultiMapHH
#define bqtRangeMultiMapHH

#include <list>
#include <map>

#include "rangeset.hh"

/***************
 *
 */
template<typename Key, typename Value>
class rangemultimap
{
    typedef std::map<Value, rangeset<Key> > Cont;
    Cont data;
    
public:
    rangemultimap() : data() {}
    
    /* Erase everything between the given range */
    void erase(const Key& lo, const Key& up);
    
    /* Erase a single value */
    void erase(const Key& lo) { data.erase(lo, lo+1); }
    
    /* Modify the given range to have the given value */
    void set(const Key& lo, const Key& up, const Value& v)
    {
        data[v].set(lo, up);
    }
    
    void set(const Key& pos, const Value& v)
    {
        set(pos, pos+1, v);
    }
    
    void erase(const Key& lo, const Key& up, const Value& v)
    {
        data[v].erase(lo, up);
        if(data[v].empty()) data.erase(v);
    }
    
    void erase(const Key& pos, const Value& v)
    {
        erase(pos, pos+1, v);
    }
    
    unsigned size() const 
    {
        unsigned res = 0;
        for(typename Cont::const_iterator i = data.begin(); i != data.end(); ++i)
            res += i->size();
        return res;
    }
    bool empty() const { return data.empty(); }
    void clear() { data.clear(); }
    
    bool operator==(const rangemultimap& b) const { return data == b.data; }
    bool operator!=(const rangemultimap& b) const { return !operator==(b); }
    
    // default copy cons. and assign-op. are fine
    
    const rangeset<Key>& get_rangelist(const Value& v) const { return data.find(v)->second; }
    
    /* Get the list of values existing in this map */
    std::list<Value> get_valuelist() const
    {
        std::list<Value> result;
        for(typename Cont::const_iterator i = data.begin(); i != data.end(); ++i)
            result.push_back(i->first);
        return result;
    }
    
    /* Get a slice of this map from given range */
    rangemultimap<Key, Value> get_slice(const Key& lo, const Key& up) const;
    
    /* This is a short for get_slice(lo, up).get_valuelist() */
    std::list<Value> get_valuelist(const Key& lo, const Key& up) const;
};

#include "rangemultimap.tcc"

#endif
