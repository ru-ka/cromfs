#ifndef bqtThreadFunHH
#define bqtThreadFunHH

/* Minimal wrappers for threads... */

#define THREAD_DEBUG 0

template<typename MutexType>
struct BasicScopedLock
{
    explicit BasicScopedLock(MutexType& m) : mut(m), locked(true) { mut.Lock(); }
    ~BasicScopedLock() { Unlock(); }
    void Unlock() { if(!locked) return; locked=false; mut.Unlock(); }
    void LockAgain() { if(locked) return; mut.Lock(); locked=true; }
private: // prevent copying the scoped lock.
    void operator=(const BasicScopedLock&);
    BasicScopedLock(const BasicScopedLock&);
private:
    MutexType& mut;
    bool locked;
};

#if THREAD_DEBUG >= 1
    #define ONLY_IF_THREAD_DEBUG(n) n
#else
    #define ONLY_IF_THREAD_DEBUG(n)
#endif


/***************************************************/

#if defined(_OPENMP)
# include "threadfun_openmp.hh"
#elif USE_PTHREADS
# include "threadfun_pthread.hh"
#else
# include "threadfun_none.hh"
#endif

/***************************************************/

typedef BasicScopedLock<MutexType> ScopedLock;

template<typename Rt,typename T>
static inline void CreateThread(ThreadType& t, Rt*(*prog)(T& ), T& param)
    { t.Init(prog, param); }
static inline void CancelThread(ThreadType& t) { t.Cancel(); }
static inline void JoinThread(ThreadType& t) { t.End(); }

#ifdef linux
#include <sched.h>
#define ForceSwitchThread() sched_yield()
#else
#define ForceSwitchThread() 
#endif

#endif
