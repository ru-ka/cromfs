#ifndef bqtThreadFunHH
 #error Do not include this file directly! Include threadfun.hh instead.
#endif

/***************************************************/
/* Dummy non-threading interface for threads. */

struct MutexType
{
    inline void Lock() { }
    inline void Unlock() { }
    inline bool TryLock() { return true; }
    bool IsLocked() const { return false; }
};
struct ThreadType
{
public:
    template<typename Rt,typename T>
    static void Init(Rt*(*prog)(T& ), T& param) { prog(param); }
    static void Cancel() { }
    static void End()    { }
};

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

static inline void TestThreadCancel() { }
static inline void SetCancellableThread(bool=false)  { }
