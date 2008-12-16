#ifndef bqtThreadFunHH
 #error Do not include this file directly! Include threadfun.hh instead.
#endif

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
        ONLY_IF_THREAD_DEBUG(int res =) pthread_mutex_lock(&mut);
#if THREAD_DEBUG >= 1
        //{char tmp;fprintf(stderr, "- mutex locking by %p\n", &tmp); fflush(stderr);}
        if(res != 0)
            { fprintf(stderr, "- mutex locking failed: %d\n", res); fflush(stderr); throw this; }
#endif
    }
    void Unlock() {
        ONLY_IF_THREAD_DEBUG(int res =) pthread_mutex_unlock(&mut);
#if THREAD_DEBUG >= 1
        //{char tmp;fprintf(stderr, "- mutex unlocking by %p\n", &tmp); fflush(stderr);}
        if(res != 0)
            { fprintf(stderr, "- mutex unlocking failed: %d\n", res); fflush(stderr); throw this; }
#endif
    }
    bool TryLock() {
        int res = pthread_mutex_trylock(&mut);
#if THREAD_DEBUG >= 1
        //{char tmp;fprintf(stderr, "- mutex locking by %p\n", &tmp); fflush(stderr);}
        if(res != 0)
            { fprintf(stderr, "- mutex locking failed: %d\n", res); fflush(stderr); throw this; }
#endif
        return res == 0;
    }

    pthread_mutex_t& Get() { return mut; }
private:
    pthread_mutex_t mut;
};

struct ThreadCondition
{
public:
    ThreadCondition() {
      ONLY_IF_THREAD_DEBUG(int res =) pthread_cond_init(&cond, NULL);
#if THREAD_DEBUG >= 1
      if(res != 0) fprintf(stderr, "- cond init(%p):%d\n", &cond, res); fflush(stderr);
#endif
    }
    ~ThreadCondition() { pthread_cond_destroy(&cond); }

    void Wait() { MutexType mut; BasicScopedLock<MutexType> lck(mut); Wait(mut); }
    void Wait(MutexType& mut)
        {
#if THREAD_DEBUG >= 2
          fprintf(stderr, "- waiting(%p)\n", &cond); fflush(stderr);
#endif
          ONLY_IF_THREAD_DEBUG(int res =) pthread_cond_wait(&cond, &mut.Get());
#if THREAD_DEBUG >= 1
          if(res != 0) fprintf(stderr, "- wait(%p):%d\n", &cond, res); fflush(stderr);
#endif
        }
    void Signal() { ONLY_IF_THREAD_DEBUG(int res =) pthread_cond_signal(&cond);
#if THREAD_DEBUG >= 1
                    if(res != 0) fprintf(stderr, "- signal(%p):%d\n", &cond, res); fflush(stderr);
#endif
                  }
    void Broadcast() { ONLY_IF_THREAD_DEBUG(int res =) pthread_cond_broadcast(&cond);
#if THREAD_DEBUG >= 1
                       if(res != 0) fprintf(stderr, "- bcast(%p):%d\n", &cond, res); fflush(stderr);
#endif
                  }
private:
    pthread_cond_t cond;
};

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

static inline void TestThreadCancel() { pthread_testcancel(); }

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
    InterruptibleContext() : now_disabled(false)
        { int oldtype;
          pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
          was_disabled = oldtype == PTHREAD_CANCEL_DEFERRED;
        }
    ~InterruptibleContext() { if(was_disabled) Disable(); else Enable(); }
    void Disable() { if(now_disabled)return;
      pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0); now_disabled=true; }
    void Enable() { if(!now_disabled)return;
      pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0); now_disabled=false; }
private:
    bool now_disabled,was_disabled;
};
