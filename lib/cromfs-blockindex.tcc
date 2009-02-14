#include "cromfs-blockindex.hh"
#include "util/mkcromfs_sets.hh"
#include "util.hh" // For ReportSize

#include <cstring>
#include <sstream>

template<typename Key, typename Value,typename layer>
bool block_index_stack<Key,Value,layer>::Find(Key index, Value& result,     size_t find_index) const
{
    if(find_index >= layers.size()) return false;
    if(!layers[find_index]->has(index)) return false;
    layers[find_index]->extract(index, result);
    return true;
}

template<typename Key, typename Value,typename layer>
void block_index_stack<Key,Value,layer>::Add(Key index, const Value& value)
{
    for(size_t ind = 0; ind < layers.size(); ++ind)
    {
        layer& lay = *layers[ind];
        if(!lay.has(index))
        {
            lay.set(index, value);
            ++size;
            return;
        }

        // Removed: Assume the caller knows not to add
        //          something that already exists.
        /*
        Value tmp;
        lay.extract(index, tmp);
        if(tmp == value) return;*/
    }
    size_t ind = layers.size();
    layers.push_back(new layer);
    layers[ind]->set(index, value);
    ++size;
}

template<typename Key, typename Value,typename layer>
void block_index_stack<Key,Value,layer>::Del(Key index, const Value& value)
{
    for(size_t ind = 0; ind < layers.size(); ++ind)
    {
        layer& lay = *layers[ind];
        if(!lay.has(index))
        {
            break;
        }
        Value tmp;
        lay.extract(index, tmp);
        if(tmp == value)
        {
            lay.unset(index);
            --size;
            ++deleted;
            return;
        }
    }
}

/*
bool block_index_type::EmergencyFreeSpace(bool Auto, bool Real)
{
    return false;
}
*/

/*template<typename Key, typename Value,typename layer>
void block_index_stack<Key,Value,layer>::Clone()
{
    for(size_t a=0; a<layers.size(); ++a)
        layers[a] = new layer(*layers[a]);
}*/

template<typename Key, typename Value,typename layer>
void block_index_stack<Key,Value,layer>::Close()
{
    for(size_t a=0; a<layers.size(); ++a) delete layers[a];
    layers.clear();
}

template<typename Key, typename Value,typename layer>
block_index_stack<Key,Value,layer>::block_index_stack()
    : layers(), size(0), deleted(0)
{
}

/*
template<typename Key, typename Value,typename layer>
block_index_stack<Key,Value,layer>::block_index_stack(const block_index_stack<Key,Value,layer>& b)
    : index(b.index),
      size(b.size),
      deleted(b.deleted)
{
    Clone();
}

template<typename Key, typename Value,typename layer>
block_index_stack<Key,Value,layer>&
block_index_stack<Key,Value,layer>::operator= (const block_index_stack<Key,Value,layer>& b)
{
    if(&b != this)
    {
        Close();
        index = b.index;
        size  = b.size;
        deleted = b.deleted;
        Clone();
    }
    return *this;
}
*/

template<typename Key, typename Value,typename layer>
void block_index_stack<Key,Value,layer>::clear()
{
    Close();
    layers.clear();
    size = deleted = 0;
}

template<typename Key, typename Value,typename layer>
std::string block_index_stack<Key,Value,layer>::GetStatistics() const
{
    std::stringstream out;
    out << "size=" << size << ",del=" << deleted << ",layers=" << layers.size();
    return out.str();
}

/*
std::string block_index_type::get_usage() const
{
    std::stringstream tmp;

    tmp << " index_use:r=" << real.size
        << ";a=" << autom.size
        << ",-" << autom.deleted;
    return tmp.str();
}
*/
