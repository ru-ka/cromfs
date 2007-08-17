#ifndef bqtThreadFunHH
#define bqtThreadFunHH

/* Minimal wrappers for threads... */

#define USE_PTHREADS

#define THREAD_DEBUG 0

#if defined(USE_PTHREADS) /* posix threads */

/***************************************************/
/* Threading with Posix Threads */

#include <pthread.h>

struct MutexType
{
public:
    MutexType()
    {
#if THREAD_DEBUG >= 1
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK_NP);
        pthread_mutex_init(&mut, &attr);
#else
        pthread_mutex_init(&mut, NULL);
#endif
    }
    ~MutexType() {
        //Unlock(); // FIXME: should Unlock() be called in destructor or not?
        pthread_mutex_destroy(&mut);
    }
    void Lock() {
#if THREAD_DEBUG >= 1
        int res = pthread_mutex_lock(&mut);
        //{char tmp;fprintf(stderr, "- mutex locking by %p\n", &tmp); fflush(stderr);}
        if(res != 0)
            { fprintf(stderr, "- mutex locking failed: %d\n", res); fflush(stderr); throw this; }
#else
        pthread_mutex_lock(&mut);
#endif
    }
    void Unlock() {
#if THREAD_DEBUG >= 1
        int res = pthread_mutex_unlock(&mut); 
        //{char tmp;fprintf(stderr, "- mutex unlocking by %p\n", &tmp); fflush(stderr);}
        if(res != 0)
            { fprintf(stderr, "- mutex unlocking failed: %d\n", res); fflush(stderr); throw this; }
#else
        pthread_mutex_unlock(&mut); 
#endif
    }
    pthread_mutex_t& Get() { return mut; }
private:
    pthread_mutex_t mut;
};

struct ScopedLock
{
    explicit ScopedLock(MutexType& m) : mut(m), locked(true) { mut.Lock(); }
    ~ScopedLock() { Unlock(); }
    void Unlock() { if(!locked) return; locked=false; mut.Unlock(); }
    void LockAgain() { if(locked) return; mut.Lock(); locked=true; }
private:
    MutexType& mut;
    bool locked;
};

struct ThreadCondition
{
public:
    ThreadCondition() {
      int res =  pthread_cond_init(&cond, NULL);
#if THREAD_DEBUG >= 1
      if(res != 0) fprintf(stderr, "- cond init(%p):%d\n", &cond, res); fflush(stderr);
#endif
    }
    ~ThreadCondition() { pthread_cond_destroy(&cond); }
    
    void Wait() { MutexType mut; ScopedLock lck(mut); Wait(mut); }
    void Wait(MutexType& mut)
        { 
#if THREAD_DEBUG >= 2
          fprintf(stderr, "- waiting(%p)\n", &cond); fflush(stderr);
#endif
          int res =  pthread_cond_wait(&cond, &mut.Get());
#if THREAD_DEBUG >= 1
          if(res != 0) fprintf(stderr, "- wait(%p):%d\n", &cond, res); fflush(stderr);
#endif
        }
    void Signal() { int res =  pthread_cond_signal(&cond);
#if THREAD_DEBUG >= 1
                    if(res != 0) fprintf(stderr, "- signal(%p):%d\n", &cond, res); fflush(stderr);
#endif
                  }
    void Broadcast() { int res =  pthread_cond_broadcast(&cond);
#if THREAD_DEBUG >= 1
                       if(res != 0) fprintf(stderr, "- bcast(%p):%d\n", &cond, res); fflush(stderr);
#endif
                  }
private:
    pthread_cond_t cond;
};

#ifdef linux
#include <sched.h>
#endif
inline void* ThreadStarter(void*);
struct ThreadType
{
private:
    pthread_t t; bool inited;
    void*(*prog)(void*); void*param;
public:
    ThreadType(): t(), inited(false) { }
    template<typename Rt,typename T>
    void Init(Rt*(*pg)(T& ), T& pr)
        { if(inited) End();
          prog  = (void*(*)(void*)) pg;
          param = (void*)&pr;
          pthread_create(&t, NULL, ThreadStarter, (void*)this);
          inited=true; }
    void Cancel() { if(inited) { pthread_cancel(t); } }
    void End()    { if(inited) { pthread_join(t,NULL); inited=false; } }
    ~ThreadType() { Cancel(); End(); }
    void* PvtRun() {
#ifdef linux
    //    unshare(CLONE_FILES);
#endif
        return prog(param);
    }
};
inline void* ThreadStarter(void*param)
{
    ThreadType& t = *(ThreadType*)param;
    return t.PvtRun();
}

template<typename Rt,typename T>
static inline void CreateThread(ThreadType& t, Rt*(*prog)(T& ), T& param)
    { t.Init(prog, param); }
static inline void CancelThread(ThreadType& t) { t.Cancel(); }
static inline void JoinThread(ThreadType& t) { t.End(); }
static inline void TestThreadCancel() { pthread_testcancel(); }
static inline void ForceSwitchThread() { sched_yield(); }

/* Note: Because using asynchronous cancelling is very hazardous
 * in C++ -- it may cause the program to terminate with a
 * "terminate called without an active exception" message, --
 * you should use deferred cancelling instead, and explicitly use
 * InterruptibleContext where there's no way any destructors are
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
struct InterruptibleContext
{
    InterruptibleContext() : disabled(false)
        { pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0); }
    ~InterruptibleContext() { Disable(); }
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

struct MutexType
{
    inline void Lock() { }
    inline void Unlock() { }
    bool IsLocked() const { return false; }
};
struct ScopedLock
{
    explicit ScopedLock(MutexType& ) { }
    inline void Unlock() { }
    inline void LockAgain() { }
};
struct ThreadType
{
public:
    template<typename Rt,typename T>
    static void Init(Rt*(*prog)(T& ), T& param) { prog(param); }
    static void Cancel() { }
    static void End()    { }
};
template<typename T>
static inline void CreateThread(ThreadType& , void*(*prog)(T& ), T& param) { prog(param); }
static inline void CancelThread(ThreadType& ) { }
static inline void JoinThread(ThreadType& )  { }
static inline void TestThreadCancel() { }
static inline void SetCancellableThread(bool)  { }
static inline void ForceSwitchThread() { sched_yield(); }

/* Instantiate this object in a context where an asynchronous
 * pthread_cancel() won't hurt (i.e. there are no destructors
 * that may be called in it).
 */
struct InterruptibleContext
{
    inline void Disable() { }
    inline void Enable() { }
};

struct ThreadCondition
{
    inline void Wait() { }
    inline void Wait(MutexType&) { }
    inline void Signal() { }
    inline void Broadcast() { }
};


/***************************************************/

#endif

#endif
