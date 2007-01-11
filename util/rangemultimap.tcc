template<typename Key, typename Value>
void rangemultimap<Key,Value>::erase(const Key& lo, const Key& up)
{
    for(typename Cont::iterator j, i = data.begin(); i != data.end(); i = j)
    {
        j = i; ++j;
        i->erase(lo, up);
        if(i->empty()) data.erase(i);
    }
}

template<typename Key, typename Value>
rangemultimap<Key, Value>
rangemultimap<Key,Value>::get_slice(const Key& lo, const Key& up) const
{
    rangeset<Key> tmp; tmp.set(lo, up);
    rangemultimap<Key, Value> result;
    for(typename Cont::const_iterator i = data.begin(); i != data.end(); ++i)
    {
        rangeset<Key> intersection = i->second.intersect(tmp);
        if(!intersection.empty())
            result.data[i->first] = intersection;
    }
    return result;
}

template<typename Key, typename Value>
std::list<Value>
rangemultimap<Key,Value>::get_valuelist(const Key& lo, const Key& up) const
{
    rangeset<Key> tmp; tmp.set(lo, up);
    std::list<Value> result;
    for(typename Cont::const_iterator i = data.begin(); i != data.end(); ++i)
        if(!i->second.intersect(tmp).empty())
            result.push_back(i->first);
    return result;
}
