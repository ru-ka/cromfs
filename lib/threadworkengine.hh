#ifndef bqtThreadWorkEngineHH
#define bqtThreadWorkEngineHH

#include "threadfun.hh"

#include <vector>

#if THREAD_DEBUG >= 1
#include <cstdio>
    static volatile unsigned ThreadDebugCounter=0;
    #define ThreadDebug(p...) do { \
        char Buf[8192]; size_t n=0;\
        n+=std::sprintf(Buf+n,"%5u|", \
            ++ThreadDebugCounter); \
        n+=std::snprintf(Buf+n, sizeof(Buf)-n, ##p); \
        std::fwrite(Buf,1,n,stderr); fflush(stderr); \
    } while(0)
#endif

template<typename WorkType>
class ThreadWorkEngine
{
public:
#ifndef _OPENMP
    ThreadWorkEngine() : threads(), params()
    {
    }
#endif

    void RunTasks(
        size_t num_threads,
        ssize_t num_workunits,
        WorkType& workparams,
        bool (*DoWork)(size_t index, WorkType& )
        /* DoWork returns bool if it wants to cancel its siblings */
    );

private:
#ifndef _OPENMP
    std::vector<ThreadType> threads;

    struct workerparam
    {
        bool init_ok; // flag: should threads begin work?
        bool done_ok; // flag: can threads go and begin waiting for another work?
        size_t num_inits; // number of threads that have noticed positive "init_ok"
        size_t num_done;  // number of threads that have ended their work
        size_t num_doneconfirm; // number of threads that have noticed positive "done_ok"

        MutexType       mutex; // mutex for locking access to this data
        ThreadCondition main_cond; // signal from main to threads
        ThreadCondition sub_cond;  // signal from threads to main

        // And the actual work-related information goes here.
        size_t work_index;
        size_t num_totalworks;
        bool cancelled;

        bool (*DoWork)(size_t index, WorkType& );
        WorkType* work;

    public:
        workerparam() :
            init_ok(false), done_ok(false),
            num_inits(0), num_done(0), num_doneconfirm(0),
            mutex(), main_cond(), sub_cond(),
            work_index(0), num_totalworks(0), cancelled(0),
            DoWork(0), work(0) { }
    } params;

    static void* WorkRunner(workerparam& params);
#endif
};

#include "threadworkengine.tcc"

#endif
