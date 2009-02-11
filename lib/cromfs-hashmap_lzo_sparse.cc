#include "endian.hh"
#include "cromfs-hashmap_lzo_sparse.hh"
#include "assert++.hh"

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
    HashType      new_begin = (crc / granu) * granu;
    uint_fast64_t new_end   = (uint_fast64_t)new_begin + granu;

    uint_fast64_t next_end=0; bool merge_next = false;
    uint_fast64_t prev_end=0; bool merge_prev = false;

    if(prev != data.end())
    {
        prev_end   = (uint_fast64_t)prev->first + prev->second->GetLength();
        merge_prev = (new_begin - prev_end) <= granu*3;
    }
    if(next != data.end())
    {
        next_end   = (uint_fast64_t)next->first + next->second->GetLength();
        merge_next = (next->first - new_end) <= granu*3;
    }

    if(!(merge_prev || merge_next))
    {
        if(data.size() >= 1024)
        {
            // Find the shortest gap in the map and merge them, then try again.
            // The variables "prev" and "next" can be reused&overwritten here,
            // because the execution path will restart at the top of this procedure
            // after the merging is done.

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
            // Here, will always merge these "prev" and its next.
            next = prev; ++next;
            assertbegin();
            assert4var(next->first, prev->first, prev->second->GetLength(), next->second->GetLength());
            assert(next->first > prev->first + prev->second->GetLength());
            assertflush();

            prev->second->Merge(*next->second, next->first - prev->first);

            assertbegin();
            assert4var(next->first, prev->first, prev->second->GetLength(), next->second->GetLength());
            assert(prev->first + prev->second->GetLength()
                   >=
                   next->first + next->second->GetLength()
                  );
            assertflush();

            delete next->second;
            data.erase(next);
            goto redo;
        }
    }
/*
    fprintf(stderr,
        "CRC=%08X, new=%08X..%08llX; prev=%08X..%08llX; next=%08X..%08llX - will %s\n",
        crc,
        new_begin, new_end,
        prev==data.end() ? 0 : prev->first,
        prev==data.end() ? 0 : prev_end,
        next==data.end() ? 0 : next->first,
        next==data.end() ? 0 : next_end,

        (merge_prev && merge_next) ? "merge both"
      : merge_prev ? "merge prev"
      : merge_next ? "merge next"
      : "create new island"
    );
*/
    if(merge_prev && merge_next)
    {
        assertbegin();
        assert4var(crc, next->first, prev_end, prev->first);
        assert(next->first > prev_end);
        assertflush();

        prev->second->Merge(*next->second, next->first - prev->first);

        prev_end   = (uint_fast64_t)prev->first + prev->second->GetLength();

        assertbegin();
        assert4var(crc, next->first, prev_end, prev->first);
        assert(crc      < prev_end);
        assert(crc      > prev->first);
        assertflush();

        prev->second->set(crc - prev->first, value);
        delete next->second;
        data.erase(next);
        return;
    }
    if(merge_prev)
    {
        assertbegin();
        assert5var(crc, new_begin, new_end, prev->first, prev_end);
        assert(new_begin > prev->first); assert(new_begin >= prev_end);
        assert(new_end   > prev->first); assert(new_end   > prev_end);
        assert(crc       > prev->first); assert(crc       >= prev_end);
        assertflush();

        prev->second->Resize(new_end - prev->first);

        prev_end   = (uint_fast64_t)prev->first + prev->second->GetLength();
        assertbegin();
        assert2var(crc, prev_end);
        assert(crc < prev_end);
        assertflush();

        prev->second->set(crc - prev->first, value);
        return;
    }
    array_t* newarray = new array_t(new_end - new_begin);
    newarray->set(crc - new_begin, value);
    if(merge_next)
    {
        newarray->Merge(*next->second, next->first - new_begin);

        new_end = new_begin + newarray->GetLength();

        assertbegin();
        assert2var(new_end, next_end);
        assert(new_end >= next_end);
        assertflush();

        delete next->second;
        data.erase(next);
    }
    data[new_begin] = newarray;
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
#define ri lzsp_ri
#define ai lzsp_ai
#define si lzsp_si
/*
typedef CompressedHashLayer_Sparse<BlockIndexHashType,cromfs_blocknum_t> ri;
template ri::CompressedHashLayer_Sparse();
template ri::~CompressedHashLayer_Sparse();
template void ri::extract(BlockIndexHashType,cromfs_blocknum_t&) const;
template void ri::set(BlockIndexHashType,const cromfs_blocknum_t&);
template void ri::unset(BlockIndexHashType);
template bool ri::has(BlockIndexHashType)const;
*/
typedef CompressedHashLayer_Sparse<BlockIndexHashType,cromfs_block_internal> ai;
template ai::CompressedHashLayer_Sparse();
template ai::~CompressedHashLayer_Sparse();
template void ai::extract(BlockIndexHashType,cromfs_block_internal&) const;
template void ai::set(BlockIndexHashType,const cromfs_block_internal&);
template void ai::unset(BlockIndexHashType);
template bool ai::has(BlockIndexHashType)const;

typedef CompressedHashLayer_Sparse<unsigned,uint_least32_t> si;
template si::CompressedHashLayer_Sparse();
template si::~CompressedHashLayer_Sparse();
template void si::extract(unsigned,uint_least32_t&) const;
template void si::set(unsigned,const uint_least32_t&);
template void si::unset(unsigned);
template bool si::has(unsigned)const;
#undef ri
#undef ai
#undef si
