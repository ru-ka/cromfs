#ifndef bqtAllocatorNkHH
#define bqtAllocatorNkHH

/* A pool-based allocator that implements a custom pointer type.
 *
 * allocatorNk<T, std::allocator<T>, uint_least16_t>
 *   = allocator for T that uses std::allocator<T>
 *     as its backend and workhorse, but externally,
 *     yields pointers that are no larger than uint_least16_t.
 *
 *
 * Assuming pointer size 8 bytes, using allocatorNk<.., uint_least32_t>
 * makes a memory usage 4*3 + 8 + 1/64 instead of 8*3, per element.
 * this saves 4 bytes per element. (Actually, 3.984375).
 * For a map having 198791914 elements, that's saving of 755 MiB.
 */

#include "endian.hh"
#include <vector>

#define allocatorNk_printf(x...) /**/
//#define allocatorNk_printf printf

template<typename IntT>
struct allocatorNk_pool
{
    typedef unsigned long BitT;
    enum { Extent = UINT64_C(1) << (uint_fast64_t)(8*sizeof(IntT)) };
    enum { Bits = 8*sizeof(BitT) };
    enum { Per_Pool = Bits * 1024, n_pools = (Extent + Per_Pool-1) / Per_Pool };

    struct ptr_pool
    {
        void*  ptrlist[Per_Pool];
        BitT   freelist[Per_Pool / Bits];
        size_t n;

        ptr_pool() : ptrlist(), freelist(), n(0) { }
    };

    static ptr_pool* pools[n_pools];

    static void* translate(IntT index)
    {
        if(!index) return 0;
        --index;
        return pools[index / Per_Pool]->ptrlist[index % Per_Pool];
    }
    static IntT mark(void* ptr)
    {
      {
        IntT res = 0;
        for(size_t poolno = 0; ; ++poolno)
        {
            if(poolno >= n_pools) goto gives_null;
            if(!pools[poolno])
            {
                allocatorNk_printf("creating pool %lu\n", (unsigned long) poolno);
                AddPool(poolno);
                break;
            }
            if(pools[poolno]->n >= Per_Pool)
            {
                res += Per_Pool;
                continue; // it's full
            }
            for(size_t r=0; r<Per_Pool / Bits; ++r, res += Bits)
            {
                if(pools[poolno]->freelist[r] != ~(BitT(0)))
                    goto got_slot;
            }
        }
    got_slot:;
        size_t poolno  = res / Per_Pool;
        size_t poolpos = res % Per_Pool;
        BitT& word = pools[poolno]->freelist[poolpos / Bits];
        allocatorNk_printf("got slot %lu in pool %lu (n=%lu) -- value %08lX (res %lu), sizeof(word)=%u\n",
            (unsigned long) poolpos, (unsigned long) poolno,
            (unsigned long) pools[poolno]->n,
            (unsigned long) word,
            (unsigned long) res,
            (unsigned) (sizeof word));

        BitT mask = 1;
        for(unsigned b=0; b<Bits; ++b)
        {
            //allocatorNk_printf("trying mask %08lX\n", (unsigned long) mask);
            if(!(word & mask))
            {
                allocatorNk_printf("mask %08lX is free, giving res %lu (slot %lu)\n",
                    (unsigned long) mask, (unsigned long) (res+1),
                    (unsigned long long) poolpos);
                word |= mask;
                ++pools[poolno]->n;
                pools[poolno]->ptrlist[poolpos] = ptr;
                return res+1;
            }
            mask <<= 1;
            if(unlikely(res == (uint_fast64_t)Extent))
            {
                allocatorNk_printf("res=%"LL_FMT"u, Extent=%"LL_FMT"u\n",
                    (unsigned long long) res,
                    (unsigned long long) Extent);
                goto gives_null;
            }
            ++res; ++poolpos;
        }
      }
    gives_null:
        allocatorNk_printf("returning NULL\n");
        return 0;
    }
    static void unmark(IntT index)
    {
        if(index)
        {
            --index;

            size_t poolno = index / Per_Pool;
            size_t poolpos= index % Per_Pool;

            pools[poolno]->freelist[poolpos / Bits]
                &= ~(BitT(1) << (poolpos % Bits));

            if(--pools[poolno]->n <= 0)
            {
                DelPool(poolno);
            }
        }
    }
/*
    static IntT find(void* ptr)
    {
        for(size_t a=0; a<Extent; ++a)
            if(ptrlist[a] == ptr)
                return a+1;
        return 0;
    }
*/
private:
    static void DelPool(size_t n)
    {
        FSBAllocator<ptr_pool> alloc;
        alloc.destroy(pools[n]);
        alloc.deallocate(pools[n], 1);
    }
    static void AddPool(size_t n)
    {
        FSBAllocator<ptr_pool> alloc;
        pools[n] = alloc.allocate(1);
        alloc.construct(pools[n], ptr_pool());
    }
};

template<typename IntT>
typename allocatorNk_pool<IntT>::ptr_pool*
allocatorNk_pool<IntT>::pools[];


template<typename Ctype, typename IntT>
struct allocatorNk_pointer_base
{
    IntT index;

    operator Ctype* () const
    {
        return (Ctype*) allocatorNk_pool<IntT>::translate(index);
    }
    Ctype& operator*  () const
    {
        return *(Ctype*) allocatorNk_pool<IntT>::translate(index);
    }
    Ctype* operator-> () const
    {
        return (Ctype*) allocatorNk_pool<IntT>::translate(index);
    }

    allocatorNk_pointer_base() : index(0) { }

    explicit allocatorNk_pointer_base(IntT i) : index(i) { }

    template<typename Type2>
    allocatorNk_pointer_base<Ctype,IntT>&
        operator= (const allocatorNk_pointer_base<Type2,IntT>& b)
    {
        index = b.index;
        return *this;
    }

    template<typename Type2>
    allocatorNk_pointer_base(const allocatorNk_pointer_base<Type2,IntT>& b)
        : index(b.index)
    {
    }

    bool operator! () const { return !index; }
    //operator bool  () const { return index; }

    template<typename Type2>
    bool operator== (const allocatorNk_pointer_base<Type2,IntT>& b) const
    {
        return index == b.index;
    }

    template<typename Type2>
    bool operator!= (const allocatorNk_pointer_base<Type2,IntT>& b) const
    {
        return index != b.index;
    }
/*
    bool operator== (const allocatorNk_pointer_base<Ctype,IntT>& b)
    {
        return index == b.index;
    }

    bool operator!= (const allocatorNk_pointer_base<Ctype,IntT>& b)
    {
        return index != b.index;
    }
*/

   // template<typename Type2>
   // operator allocatorNk_pointer_base<Type2,IntT> () const
   //     { return allocatorNk_pointer_base<Type2,IntT> (index); }
};

template<typename DataT,
         typename BaseT = std::allocator<DataT>,
         typename IntT = uint_least16_t>
class allocatorNk
{
private:
    typename BaseT::template rebind<DataT>::other RealBase;
public:
    typedef IntT size_type;
    typedef IntT difference_type;

    typedef allocatorNk_pointer_base<DataT,IntT>       pointer;
    typedef allocatorNk_pointer_base<const DataT,IntT> const_pointer;

    /*
    template<typename Ctype>
    struct reference_base
    {
        IntT index;

        operator Ctype& () const
        {
        }
    };
    typedef reference_base<DataT>       reference;
    typedef reference_base<const DataT> const_reference;
    */
    typedef DataT&       reference;
    typedef const DataT& const_reference;

    typedef DataT value_type;

    template<class Other>
    struct rebind
    {
        typedef allocatorNk<Other, BaseT, IntT> other;
    };

    allocatorNk() throw() {}

    template<typename D,typename B,typename I>
    allocatorNk(const allocatorNk<D,B,I>&) throw() {}

    template<typename D,typename B,typename I>
    allocatorNk& operator=(const allocatorNk<D,B,I>&) { return *this; }

    pointer allocate(size_type count, const void*p = 0)
    {
        DataT* resultptr = RealBase.allocate(count, p);
        pointer result ( allocatorNk_pool<IntT>::mark(resultptr) );
        return result;
    }

    void deallocate(pointer ptr, size_type n)
    {
        RealBase.deallocate( (DataT*) ptr, n);
        allocatorNk_pool<IntT>::unmark(ptr.index);
    }
    /*void deallocate(DataT* ptr, size_type n)
    {
        RealBase.deallocate(ptr, n);
        allocatorNk_pool<DataT,IntT>::unmark(
           allocatorNk_pool<DataT,IntT>::find(ptr) );
    }*/

    void construct(pointer ptr, const DataT& val)
    {
        RealBase.construct( (DataT*) ptr, val);
    }
    /*void construct(DataT* ptr, const DataT& val)
    {
        RealBase.construct(ptr, val);
    }*/

    void destroy(pointer ptr)
    {
        RealBase.destroy( (DataT*) ptr);
    }
    /*void destroy(DataT* ptr)
    {
        RealBase.destroy(ptr);
    }*/

    size_type max_size() const throw() { return RealBase.max_size(); }
};

#endif
