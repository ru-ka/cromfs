#include <vector>
#include "threadfun.hh"

template<typename T, typename Allocator = std::allocator<T> >
class NoCopyArray
{
public:
    NoCopyArray() : alloc(), pools(), last_pool_size(n_per_pool), lock()
    {
    }
    ~NoCopyArray()
    {
        clear();
    }
    void clear()
    {
        for(size_t a=pools.size(); a-- > 0; )
        {
            size_t cap = n_per_pool;
            if(a == pools.size()-1) cap = last_pool_size;
            for(size_t b=cap; b-- > 0; )
                alloc.destroy(&pools[a][b]);

            alloc.deallocate(pools[a], n_per_pool);
        }
        pools.clear();
        last_pool_size = n_per_pool;
    }

    template<typename T1,typename T2>
    T* push_construct(const T1& a, const T2& b)
    {
        ScopedLock lck(lock);
        T* res = AllocOneUnlocked();
        new(res) T(a,b);
        return res;
    }

    template<typename T1>
    T* push_construct(const T1& a)
    {
        ScopedLock lck(lock);
        T* res = AllocOneUnlocked();
        new(res) T(a);
        return res;
    }

private:
    NoCopyArray(const NoCopyArray<T> &);
    void operator=(const NoCopyArray<T> &);

private:
    T* AllocOneUnlocked()
    {
        if(last_pool_size == n_per_pool)
        {
            pools.push_back( alloc.allocate(n_per_pool) );
            last_pool_size = 0;
        }
        return &pools.back() [ last_pool_size++ ];
    }

private:
    enum { n_per_pool = 0x10000 / sizeof(T) };

    Allocator alloc;

    std::vector<T*> pools;
    size_t last_pool_size;
    MutexType lock;
};
