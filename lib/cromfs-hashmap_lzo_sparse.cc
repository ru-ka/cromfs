#include "endian.hh"
#include "cromfs-hashmap_lzo_sparse.hh"

template<typename HashType, typename T>
CompressedHashLayer_Sparse<HashType,T>::CompressedHashLayer_Sparse()
    : data()
{
}

template<typename HashType, typename T>
CompressedHashLayer_Sparse<HashType,T>::~CompressedHashLayer_Sparse()
{
    for(typename map_t::iterator i = data.begin(); i != data.end(); ++i)
        delete i->second;
}

template<typename HashType, typename T>
void CompressedHashLayer_Sparse<HashType,T>::extract(HashType crc, T& result) const
{
    typename map_t::const_iterator i = data.lower_bound(crc);
    if(unlikely(i != data.end() && i->first == crc))
        { i->second->extract(crc - i->first, result); return; }

    if(i != data.begin())
    {
        --i;
        if(likely(crc >= i->first)
        && i->first + i->second->GetLength() > crc)
            { i->second->extract(crc - i->first, result); return; }
    }
}

template<typename HashType, typename T>
void CompressedHashLayer_Sparse<HashType,T>::set(HashType crc, const T& value)
{
redo:;

    typename map_t::iterator i = data.lower_bound(crc);
    typename map_t::iterator next = i, prev = data.end();
    if(unlikely(next != data.end() && next->first == crc))
    {
        next->second->set(crc - next->first, value);
        return;
    }

    if(i != data.begin())
    {
        prev = --i;
        if(likely(crc >= prev->first)
        && prev->first + prev->second->GetLength() > crc)
            { prev->second->set(crc - prev->first, value); return; }
    }

    unsigned granu = array_t::GetGranularity();
    HashType new_begin = (crc / granu) * granu;
    HashType new_end   = new_begin + granu;

    HashType next_begin=0, next_end=0;
    HashType prev_begin=0, prev_end=0;

    if(prev != data.end())
    {
        prev_begin = prev->first;
        prev_end   = prev_begin + prev->second->GetLength();
    }
    if(next != data.end())
    {
        next_begin = next->first;
        next_end   = next_begin + next->second->GetLength();
    }
    HashType prev_distance = new_begin - prev_end;
    HashType next_distance = next_begin - new_end;

    bool merge_prev = prev_distance <= 0x8000;
    bool merge_next = next_distance <= 0x8000;

    if(prev == data.end()) merge_prev = false;
    if(next == data.end()) merge_next = false;

    if(!(merge_prev || merge_next))
    {
        if(data.size() >= 256)
        {
            // Find the shortest gap in the map and merge them, then try again
            bool first=true; HashType shortest_gap=0;
            for(typename map_t::iterator i = data.begin(); ; i=next)
            {
                next = i; ++next;
                if(next == data.end()) break;
                HashType gaplength = next->first + next->second->GetLength() - i->first;
                if(first || gaplength < shortest_gap)
                {
                    shortest_gap = gaplength;
                    first = false;
                    prev = i;
                }
            }
            next = prev; ++next;
            prev->second->Merge(*next->second, next->first - prev->first);
            delete next->second;
            data.erase(next);
            goto redo;
        }
    }

    if(merge_prev && merge_next)
    {
        prev->second->Merge(*next->second, next_begin - prev_begin);
        prev->second->set(crc - prev->first, value);
        delete next->second;
        data.erase(next);
        return;
    }
    if(merge_prev)
    {
        prev->second->Resize(new_end - prev_begin);
        prev->second->set(crc - prev->first, value);
        return;
    }
    array_t* newarray = new array_t(new_end - new_begin);
    newarray->set(crc - new_begin, value);
    if(merge_next)
    {
        newarray->Merge(*next->second, next_begin - new_begin);
        delete next->second;
        next->second = newarray;
    }
    else
    {
        data[new_begin] = newarray;
    }
}

template<typename HashType, typename T>
void CompressedHashLayer_Sparse<HashType,T>::unset(HashType crc)
{
    typename map_t::iterator i = data.lower_bound(crc);
    if(unlikely(i != data.end() && i->first == crc))
        { i->second->unset(crc - i->first); return; }

    if(i != data.begin())
    {
        --i;
        if(likely(crc >= i->first)
        && i->first + i->second->GetLength() > crc)
            { i->second->unset(crc - i->first); return; }
    }
}

template<typename HashType, typename T>
bool CompressedHashLayer_Sparse<HashType,T>::has(HashType crc) const
{
    typename map_t::const_iterator i = data.lower_bound(crc);
    if(unlikely(i != data.end() && i->first == crc))
        return i->second->has(crc - i->first);

    if(i != data.begin())
    {
        --i;
        if(likely(crc >= i->first)
        && i->first + i->second->GetLength() > crc)
            return i->second->has(crc - i->first);
    }
    return false;
}


#include "cromfs-blockindex.hh" // for BlockIndexhashType, blocknum etc.
typedef CompressedHashLayer_Sparse<BlockIndexHashType,cromfs_blocknum_t> ri;
template ri::CompressedHashLayer_Sparse();
template ri::~CompressedHashLayer_Sparse();
template void ri::extract(BlockIndexHashType,cromfs_blocknum_t&) const;
template void ri::set(BlockIndexHashType,const cromfs_blocknum_t&);
//template void ri::unset(BlockIndexHashType);
template bool ri::has(BlockIndexHashType)const;

typedef CompressedHashLayer_Sparse<BlockIndexHashType,cromfs_block_internal> ai;
template ai::CompressedHashLayer_Sparse();
template ai::~CompressedHashLayer_Sparse();
template void ai::extract(BlockIndexHashType,cromfs_block_internal&) const;
template void ai::set(BlockIndexHashType,const cromfs_block_internal&);
template void ai::unset(BlockIndexHashType);
template bool ai::has(BlockIndexHashType)const;
