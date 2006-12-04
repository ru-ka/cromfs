#include "rangeset.hh"

template<typename Key>
const typename rangeset<Key>::const_iterator
    rangeset<Key>::ConstructIterator(typename Cont::const_iterator i) const
{
    const_iterator tmp(data);
    while(i != data.end() && i->second.is_nil()) ++i;
    tmp.i = i;
    tmp.Reconstruct();
    return tmp;
}
template<typename Key>
void rangeset<Key>::const_iterator::Reconstruct()
{
    if(i != data.end())
    {
        rangetype<Key>::lower = i->first;
        typename Cont::const_iterator j = i;
        if(++j != data.end())
            rangetype<Key>::upper = j->first;
        else
            rangetype<Key>::upper = rangetype<Key>::lower;
        
        if(i->second.is_nil())
        {
            fprintf(stderr, "rangeset: internal error\n");
        }
    }
}
template<typename Key>
void rangeset<Key>::const_iterator::operator++ ()
{
    /* The last node before end() is always nil. */
    while(i != data.end())
    {
        ++i;
        if(!i->second.is_nil())break;
    }
    Reconstruct();
}
template<typename Key>
void rangeset<Key>::const_iterator::operator-- ()
{
    /* The first node can not be nil. */
    while(i != data.begin())
    {
        --i;
        if(!i->second.is_nil())break;
    }
    Reconstruct();
}
    
template<typename Key>
rangeset<Key> rangeset<Key>::intersect(const rangeset<Key>& b) const
{
    rangeset<Key> result;
    const_iterator ai = begin();
    const_iterator bi = b.begin();
    
    for(;;)
    {
        if(ai == end()) break;
        if(bi == b.end()) break;
        
        if(ai->upper <= bi->lower) { ++ai; continue; }
        if(bi->upper <= ai->lower) { ++bi; continue; }
        
        rangetype<Key> intersection = ai->intersect(bi);
        if(!intersection.empty())
            result.set(intersection.lower, intersection.upper);
        
        if(ai->upper < bi->upper) { ++ai; continue; }
        if(bi->upper < ai->upper) { ++bi; continue; }
        ++ai; ++bi; continue; // identical ranges
    }
    return result;
}

