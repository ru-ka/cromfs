#ifndef bqtThreadFunHH
 #error Do not include this file directly! Include threadfun.hh instead.
#endif

/***************************************************/
/* Threading with OpenMP */

#include <omp.h>

struct MutexType
{
    MutexType() : lock() { omp_init_lock(&lock); }
    ~MutexType() { omp_destroy_lock(&lock); }
    void Lock() { omp_set_lock(&lock); }
    void Unlock() { omp_unset_lock(&lock); }
    bool TryLock() { return omp_test_lock(&lock); }

    MutexType(const MutexType& ) : lock() { omp_init_lock(&lock); }
    MutexType& operator= (const MutexType& ) { return *this; }
public:
    omp_lock_t lock;
};

/* In the OpenMP implementation, these are dummy functions,
 * because OpenMP has a slightly different ideology.
 */
struct ThreadCondition
{
    inline void Wait() { }
    inline void Wait(MutexType&) { }
    inline void Signal() { }
    inline void Broadcast() { }
};
struct ThreadType
{
public:
    template<typename Rt,typename T>
    static void Init(Rt*(*prog)(T& ), T& param) { prog(param); }
    static void Cancel() { }
    static void End()    { }
};

static inline void TestThreadCancel() { }
static inline void SetCancellableThread(bool) { }

struct InterruptibleContext
{
    /* Declaring inline dummy constructor and destructor
     * suppresses warnings about unused variable when
     * InterruptibleContext is instantiated
     */
    inline InterruptibleContext() { }
    inline ~InterruptibleContext() { }
    inline void Disable() { }
    inline void Enable() { }
};
