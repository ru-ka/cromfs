#ifndef bqtThreadFunHH
#define bqtThreadFunHH

/* Minimal wrappers for threads... */

#define USE_PTHREADS

#if defined(USE_PTHREADS) /* posix threads */

/***************************************************/
/* Threading with Posix Threads */

#include <pthread.h>

typedef pthread_mutex_t MutexType;
#define MutexInitializer PTHREAD_MUTEX_INITIALIZER

struct ScopedLock
{
    explicit ScopedLock(MutexType& m)
        : mut(m), locked(true)
        { pthread_mutex_lock(&mut); }
    ~ScopedLock() { Unlock(); }
    void Unlock() { if(locked) { pthread_mutex_unlock(&mut); locked=false; } }
    void LockAgain() { if(!locked) { pthread_mutex_lock(&mut); locked=true; } }
private:
    MutexType& mut;
    bool locked;
};

typedef pthread_t ThreadType;

template<typename T>
static inline void CreateThread(ThreadType& t, void*(*prog)(T& ), T& param)
    { pthread_create(&t, NULL, (void*(*)(void*)) prog, (void*)&param); }
static inline void CancelThread(ThreadType& t)
    { pthread_cancel(t); }
static inline void JoinThread(ThreadType& t)
    { pthread_join(t, NULL); }
static inline void TestThreadCancel() { pthread_testcancel(); }

/* Note: Because using asynchronous cancelling is very hazardous
 * in C++ -- it may cause the program to terminate with a
 * "terminate called without an active exception" message, --
 * you should use deferred cancelling instead, and explicitly use
 * InterruptableContext where there's no way any destructors are
 * going to be called.
 */
static inline void SetCancellableThread(bool async = false)
    { pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
      pthread_setcanceltype(async ? PTHREAD_CANCEL_ASYNCHRONOUS
                                  : PTHREAD_CANCEL_DEFERRED, 0);
    }

/* Instantiate this object in a context where an asynchronous
 * pthread_cancel() won't hurt (i.e. there are no destructors
 * that may be called in it).
 */
struct InterruptableContext
{
    InterruptableContext() : disabled(false)
        { pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0); }
    ~InterruptableContext() { Disable(); }
    void Disable() { if(disabled)return;
      pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0); disabled=true; }
    void Enable() { if(!disabled)return;
      pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0); disabled=false; }
private:
    bool disabled;
};

/***************************************************/

#else /* No threads? */

/***************************************************/
/* Dummy non-threading interface for threads. */

struct MutexType { };
#define MutexInitializer {}
struct ScopedLock
{
    explicit ScopedLock(MutexType& ) { }
    inline void Unlock() { }
    inline void LockAgain() { }
};
struct ThreadType { };

template<typename T>
static inline void CreateThread(ThreadType& , void*(*prog)(T& ), T& param) { prog(param); }
static inline void CancelThread(ThreadType& ) { }
static inline void JoinThread(ThreadType& )  { }
static inline void TestThreadCancel() { }
static inline void SetCancellableThread(bool)  { }

/* Instantiate this object in a context where an asynchronous
 * pthread_cancel() won't hurt (i.e. there are no destructors
 * that may be called in it).
 */
struct InterruptableContext
{
    inline void Disable() { }
    inline void Enable() { }
};

/***************************************************/

#endif

#endif
