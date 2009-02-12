#ifndef bqtStaticAllocHH
#define bqtStaticAllocHH

#include <algorithm>
#include <iostream>
#include <sstream>
#include <map>
#include <vector>

#include "simplevec.hh"

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif


/*
  Memory blocks are sorted biggest-first, because
  small blocks tend to be the most common, and we want
  to avoid excess lengths of copy()/copy_backward().
*/
class StaticAllocatorException: public std::bad_alloc
{
public:
    StaticAllocatorException(const std::string& s): msg(s) { }
    virtual const char* what() const throw() { return msg.c_str(); }
    virtual ~StaticAllocatorException() throw() { }
private:
    std::string msg;
};

namespace StaticAllocatorImplementation
{
    static const unsigned MaxAllocatorBytes = 1024UL*1024*8;
    /* Defragmentation seems to actually cause a performance
     * hit AFTER the defragmentation! A fragmented FreeInfo
     * seems to be a good thing.
     */
    static const unsigned StaticAllocatorMaxDefragPeriod = 15000100;
    /* 10000: 2m19s */
    /* 100000: 1m12s */
    /* 500000: 1m7s */

    struct StaticAllocatorPool
    {
        char Buffer[MaxAllocatorBytes];
    };

    typedef std::multimap<size_t/*length*/,size_t/*ptr*/> FreeInfoData;
    static std::vector<StaticAllocatorPool*> Pools;
    static FreeInfoData FreeInfo;

    static size_t FreeCounter=0;

    static struct StaticAllocatorCleanerUpper
    {
        /* This makes valgrind happier. No memory leaks! */
        ~StaticAllocatorCleanerUpper()
        {
            for(size_t a=0; a<Pools.size(); ++a)
                delete Pools[a];
            Pools.clear();
            FreeInfo.clear();
        }
    } StaticAllocatorCleanerUpper;

//private:
#if 1
    typedef std::pair<size_t,size_t> FreeInfoEnt;
    static inline bool SortByPosition(const FreeInfoEnt& a, const FreeInfoEnt& b)
    {
        return a.second < b.second;
    }
#endif
    static void Defragment()
    {
#if 1
        //std::cerr << "Warning: Defragmentating!\n";

        std::vector<FreeInfoEnt> tmpvec;
        tmpvec.insert(tmpvec.end(), FreeInfo.begin(), FreeInfo.end());
        FreeInfo.clear();

        std::sort(tmpvec.begin(), tmpvec.end(), SortByPosition);

        FreeInfoEnt prev;
        bool pending=false;

        size_t nsaved=0, nfree=0;
        for(size_t b=tmpvec.size(),a=0; a<b; ++a)
        {
            const FreeInfoEnt& item = tmpvec[a];
            nfree += item.first;

            if(likely(pending) && item.second == prev.second + prev.first)
            {
                prev.first += item.first;
                ++nsaved;
            }
            else
            {
                if(pending) FreeInfo.insert(prev);
                prev = item; pending=true;
            }
        }
        if(pending) FreeInfo.insert(prev);

        /*
        size_t average = FreeInfo.empty() ? 0 : nfree/FreeInfo.size();

        std::cerr << "Warning: Done defragmentating! "
                     "Saved " << nsaved
                  << ", size now " << FreeInfo.size()
                  << ", average block size " << average
                  << ", " << nfree << " bytes free, "
                  << (MaxAllocatorBytes*Pools.size()-nfree)
                  << " bytes used\n"; // DumpFree();
        */
#endif
    }
    static void* GetPointer(size_t pos)
    {
        return &Pools[pos/MaxAllocatorBytes]->Buffer[pos%MaxAllocatorBytes];
    }
    static size_t GetPos(void* voidptr)
    {
        char* ptr = (char*)voidptr;
        const size_t MaxAllocatorPools = Pools.size();
        for(size_t a=0; a<MaxAllocatorPools; ++a)
        {
            char* begin = Pools[a]->Buffer;
            char* end   = Pools[a]->Buffer+MaxAllocatorBytes;
            if(ptr >= begin && ptr < end) { return a*MaxAllocatorBytes + (ptr-begin); }
        }
        return 0;
    }
    /*
    static void DumpPools()
    {
        const size_t MaxAllocatorPools = Pools.size();
        for(size_t a=0; a<MaxAllocatorPools; ++a)
        {
            void* begin = (void*)(Pools[a]->Buffer);
            void* end   = (void*)(Pools[a]->Buffer+MaxAllocatorBytes);
            std::cerr << "Pool " << a << " at " << begin << "-" << end << "\n";
        }
        std::cerr << "Total: " << (MaxAllocatorBytes*MaxAllocatorPools) << " bytes\n";
    }
    */
    static bool AddPool() throw()
    {
        size_t MaxAllocatorPools = Pools.size();
        try
        {
            Pools.push_back(new StaticAllocatorPool);
        }
        catch(std::bad_alloc)
        {
            return false;
        }
        /*
        std::cerr << "Created a StaticAllocatorPool of "
                  << MaxAllocatorBytes << " bytes\n";
        */
        FreeInfo.insert(std::make_pair(MaxAllocatorBytes,
                                       MaxAllocatorBytes*MaxAllocatorPools));
        return true;
    }
    static void DumpFree(std::ostream& out = std::cerr)
    {
        out << "  (" << FreeInfo.size() << ")";

        for(FreeInfoData::const_iterator
            i = FreeInfo.begin();
            i != FreeInfo.end();
            ++i)
        {
            out << "["/* << i->second << ":"*/ << i->first << "]";
        }
        out << "\n";
    }
    static inline FreeInfoData::iterator FindUsableSlot(size_t length)
    {
    #if 1
        return FreeInfo.lower_bound(length);
    #endif

    #if 0
        /* Method: Use a customized version of upper_bound() */
        /* Small items are most commonly sought after, so start
         * the comparison from near the end
         */
        size_t begin=0, end=FreeInfo.size(), size=end-begin;
        size_t middle=begin + size * 95/100;
        if(middle==end && end>begin) --middle;
        while(begin != end)
            if(FreeInfo[middle] > length)
            {
                end = middle;
                middle = begin+(end-begin)/2;
            }
            else
            {
                begin = middle+1;
                middle = begin+(end-begin)/2;
            }
        if(begin==0) return FreeInfo.end();
        return FreeInfo.begin()+(begin-1);
    #endif

    #if 0
        /* Method: Search linearly starting from the end */
        FreeInfoData::iterator i;
        for(i = FreeInfo.end(); i != FreeInfo.begin(); )
            if((--i)->length >= length) return i;
        return FreeInfo.end();
    #endif
    }
    static inline size_t FindAndTake(size_t length) throw(StaticAllocatorException)
    {
        if(length > MaxAllocatorBytes)
        {
        Failure:
            std::stringstream msg;
            msg << "FindAndTake(" << length
                << "): Failed. Max size = " << MaxAllocatorBytes
                << ", number of pools = " << Pools.size()
                << "\n";
            DumpFree(msg);
            msg << "\n";
            throw StaticAllocatorException(msg.str());
        }

        FreeInfoData::iterator i;
        for(int trynum=0; trynum<3; ++trynum)
        {
            /* Find the last block that is of the given length */
            i = FindUsableSlot(length);
            if(likely(i != FreeInfo.end()))
            {
                break;
            }
            switch(trynum)
            {
                case 0: Defragment(); break;
                case 1: if(!AddPool()) goto Failure; break;
                case 2: goto Failure;
            }
        }

        // Values returned by upper_bound():
        /* 10 5 2 1 1 */
        //           ^  for 1
        //      ^       for 3
        //    ^         for 10
        // ^            for 20

        size_t pos    = i->second;
        size_t remain = i->first - length;

        FreeInfo.erase(i);
        if(remain > 0)
        {
            if(length >= 131072)
            {
                /* take from end */
                FreeInfo.insert(std::make_pair(remain, pos));
                pos += remain;
            }
            else
            {
                /* take from beginning */
                FreeInfo.insert(std::make_pair(remain, pos+length));
            }
        }
        return pos;
    }

//public:
    static void* Alloc(size_t nbytes) throw(StaticAllocatorException)
    {
        //std::cerr << "Allocate(" << nbytes << ")\n"; DumpFree();
        //return ::operator new(nbytes);
        size_t pos = FindAndTake(nbytes);

        //std::cerr << "Got(" << pos << ")\n"; DumpFree(); std::cerr << "---\n";

        return GetPointer(pos);
    }
    static void Free(void* ptr, size_t nbytes) throw()
    {
        //::operator delete(ptr);
        //return;

        if(++FreeCounter >= StaticAllocatorMaxDefragPeriod)
        {
            Defragment();
            FreeCounter=0;
        }

        const size_t beginpos = GetPos(ptr);
        const size_t length = nbytes;
        //const size_t endpos   = beginpos + length;

        //std::cerr << "Free(" << beginpos << "," << length << " (end " << endpos << "))\n"; DumpFree();

        /* This is a new fragment */
        FreeInfo.insert(std::make_pair(length, beginpos));

        //std::cerr << "After freeing:\n"; DumpFree(); std::cerr << "---\n";
    }

//private:
}

/* This turns the StaticAllocatorImplementation into an "allocator"
 * that can be used in creation of STL objects.
 */
template<typename T>
struct StaticAllocator
{
    typedef T*        pointer;
    typedef const T*  const_pointer;
    typedef T&        reference;
    typedef const T&  const_reference;
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    typedef T         value_type;

    StaticAllocator()                                throw() { }
    StaticAllocator(const StaticAllocator& )         throw() { }

    template<typename T2>
    StaticAllocator(const StaticAllocator<T2>& ) throw() { }

    template<typename T2>
    struct rebind
    { typedef StaticAllocator<T2> other; };

    inline bool operator==(const StaticAllocator& b) const throw() { return true; }

    static pointer allocate(const size_type n, const void* = 0) throw(StaticAllocatorException)
    {
        if(!n) return 0;

        /* This statement allocates "n * sizeof(T)" bytes of memory
         * and returns the pointer to the allocated memory.
         */
        return (T*)StaticAllocatorImplementation::Alloc(n*sizeof(T));
    }

    static void deallocate(const pointer p, const size_type n) throw()
    {
        /* This statement deallocates "n * sizeof(T)" bytes of memory
         * from the location pointed by "p". The number "n" is expected
         * to be the same as it was when the memory was allocated.
         */
        if(p&&n) StaticAllocatorImplementation::Free(p, n*sizeof(T));
    }

    static inline size_type max_size() throw()
    {
        return StaticAllocatorImplementation::MaxAllocatorBytes/sizeof(T);
    }
    static void construct(const pointer p, const_reference val)
    {
        /* This statement constructs the object pointed by "p"
         * using the copy constructor with default value from "val".
         * "p" is expected to point into an uninitialized memory
         * region that is big enough to hold an instance of "T".
         * The value of "p" isn't changed.
         */
        new(p) T(val);
    }
    static void destroy(const pointer p)
    {
        /* This statement destructs the object pointed by "p".
         * After this call, "p" will point into an uninitialized
         * memory region that can be freed.
         * The value of "p" isn't changed.
         */
        p->~T();
    }
};

#endif
