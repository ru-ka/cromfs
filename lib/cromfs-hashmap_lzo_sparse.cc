#include "endian.hh"
#include "cromfs-hashmap_lzo_sparse.hh"
#include "assert++.hh"

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

enum { MAX_LZO_STRIPES = 2048 };
// ^ Maximum stripe count. A magic constant, chosen by arbitration.

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
void CompressedHashLayer_Sparse<HashType,T>::extract(HashType index, T& result) const
{
    typename map_t::const_iterator i = data.lower_bound(index);
    if(unlikely(i != data.end() && i->first == index))
        { i->second->extract(index - i->first, result); return; }

    if(i != data.begin())
    {
        --i;
        if(likely(index >= i->first)
        && i->first + i->second->GetLength() > index)
            { i->second->extract(index - i->first, result); return; }
    }
}

template<typename HashType, typename T>
void CompressedHashLayer_Sparse<HashType,T>::set(HashType index, const T& value)
{
redo:;

    typename map_t::iterator i = data.lower_bound(index);
    typename map_t::iterator next = i, prev = data.end();
    if(unlikely(next != data.end() && next->first == index))
    {
        next->second->set(index - next->first, value);
        return;
    }

    if(i != data.begin())
    {
        prev = --i;
        if(likely(index >= prev->first)
        && prev->first + prev->second->GetLength() > index)
            { prev->second->set(index - prev->first, value); return; }
    }

    unsigned granu = array_t::GetGranularity();
    HashType      new_begin = (index / granu) * granu;
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
        if(data.size() >= MAX_LZO_STRIPES)
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
        "CRC=%08X, new=%08X..%08"LL_FMT"X; prev=%08X..%08"LL_FMT"X; next=%08X..%08"LL_FMT"X - will %s\n",
        index,
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
        assert4var(index, next->first, prev_end, prev->first);
        assert(next->first > prev_end);
        assertflush();

        prev->second->Merge(*next->second, next->first - prev->first);

        prev_end   = (uint_fast64_t)prev->first + prev->second->GetLength();

        assertbegin();
        assert4var(index, next->first, prev_end, prev->first);
        assert(index      < prev_end);
        assert(index      > prev->first);
        assertflush();

        prev->second->set(index - prev->first, value);
        delete next->second;
        data.erase(next);
        return;
    }
    if(merge_prev)
    {
        assertbegin();
        assert5var(index, new_begin, new_end, prev->first, prev_end);
        assert(new_begin > prev->first); assert(new_begin >= prev_end);
        assert(new_end   > prev->first); assert(new_end   > prev_end);
        assert(index       > prev->first); assert(index       >= prev_end);
        assertflush();

        prev->second->Resize(new_end - prev->first);

        prev_end   = (uint_fast64_t)prev->first + prev->second->GetLength();
        assertbegin();
        assert2var(index, prev_end);
        assert(index < prev_end);
        assertflush();

        prev->second->set(index - prev->first, value);
        return;
    }
    array_t* newarray = new array_t(new_end - new_begin);
    newarray->set(index - new_begin, value);
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
void CompressedHashLayer_Sparse<HashType,T>::unset(HashType index)
{
    typename map_t::iterator i = data.lower_bound(index);
    if(unlikely(i != data.end() && i->first == index))
        { i->second->unset(index - i->first); return; }

    if(i != data.begin())
    {
        --i;
        if(likely(index >= i->first)
        && i->first + i->second->GetLength() > index)
            { i->second->unset(index - i->first); return; }
    }
}

template<typename HashType, typename T>
bool CompressedHashLayer_Sparse<HashType,T>::has(HashType index) const
{
    typename map_t::const_iterator i = data.lower_bound(index);
    if(unlikely(i != data.end() && i->first == index))
        return i->second->has(index - i->first);

    if(i != data.begin())
    {
        --i;
        if(likely(index >= i->first)
        && i->first + i->second->GetLength() > index)
            return i->second->has(index - i->first);
    }
    return false;
}

#include "cromfs-blockindex.hh" // for BlockIndexhashType, blocknum etc.
#include "newhash.h"
#define ri lzsp_ri
#define ai lzsp_ai
#define si lzsp_si
/*
typedef CompressedHashLayer_Sparse<newhash_t,cromfs_blocknum_t> ri;
template ri::CompressedHashLayer_Sparse();
template ri::~CompressedHashLayer_Sparse();
template void ri::extract(newhash_t,cromfs_blocknum_t&) const;
template void ri::set(newhash_t,const cromfs_blocknum_t&);
template void ri::unset(newhash_t);
template bool ri::has(newhash_t)const;

typedef CompressedHashLayer_Sparse<newhash_t,cromfs_block_internal> ai;
template ai::CompressedHashLayer_Sparse();
template ai::~CompressedHashLayer_Sparse();
template void ai::extract(newhash_t,cromfs_block_internal&) const;
template void ai::set(newhash_t,const cromfs_block_internal&);
template void ai::unset(newhash_t);
template bool ai::has(newhash_t)const;
*/
typedef CompressedHashLayer_Sparse<newhash_t,uint_least32_t> si;
template si::CompressedHashLayer_Sparse();
template si::~CompressedHashLayer_Sparse();
template void si::extract(newhash_t,uint_least32_t&) const;
template void si::set(newhash_t,const uint_least32_t&);
template void si::unset(newhash_t);
template bool si::has(newhash_t)const;

#undef ri
#undef ai
#undef si
