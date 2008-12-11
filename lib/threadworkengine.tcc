#include "threadworkengine.hh"

#ifdef _OPENMP
#include <omp.h>
#endif

template<typename WorkType>
void ThreadWorkEngine<WorkType>::RunTasks(
    size_t
    #ifndef _OPENMP
           num_threads /* Note: <- unused when OPENMP */
    #endif
                      ,
    ssize_t num_workunits,
    WorkType& workparams,
    bool (*DoWork)(size_t index, WorkType& )
    /* DoWork returns bool if it wants to cancel its siblings */
)
{
#ifdef _OPENMP
    /* This version does not cancel sibling threads,
     * but may be a little more robust that way.
     * Just hope that DoWork() does not consume a lot of time.
     */
    bool cancel = false;
    
  #pragma omp parallel for schedule(guided,1) shared(cancel)
    for(ssize_t a=0; a<num_workunits; ++a)
    {
      #pragma omp flush(cancel)
        if(!cancel && DoWork(a, workparams))
        {
            cancel = true;
            //#pragma omp flush(cancel) -- is this needed here?
        }
    }
#else
    if(num_threads <= 1 || num_workunits <= 1)
    {
        for(size_t a = 0; a < num_workunits; ++a)
            if( DoWork(a, workparams) ) break;
        return;
    }
    
    if(threads.size() < num_threads)
    {
        // Create the required threads
        size_t oldsize = threads.size();
        threads.resize(num_threads);
        /* FIXME: copying of ThreadType might be unsafe */
        for(size_t a = oldsize; a < num_threads; ++a)
            CreateThread(threads[a], WorkRunner, params);
    }
    if(threads.size() > num_threads)
    {
        for(size_t a = threads.size(); a-- > num_threads; )
            CancelThread(threads[a]);
        threads.resize(num_threads);
        /* FIXME: copying of ThreadType might be unsafe */
    }
    
    ScopedLock lck(params.mutex);
    params.init_ok = true;
    params.done_ok = false;
    params.num_inits = 0;
    params.num_done  = 0;
    params.num_doneconfirm = 0;
    params.work_index = 0;
    params.num_totalworks = num_workunits;
    params.cancelled = false;
    params.DoWork = DoWork;
    params.work = &workparams;
    
#if THREAD_DEBUG >= 1
    ThreadDebug("Threads Go-ahead\n");
#endif
    // Inform threads that they can now begin
    params.main_cond.Broadcast();
    
#if THREAD_DEBUG >= 1
    ThreadDebug("Waiting for all inits\n");
#endif
    // Wait until all threads have began the work
    while(params.num_inits < threads.size())
    {
#if THREAD_DEBUG >= 1
        ThreadDebug("Waiting num_inits, got %u\n", params.num_inits);
#endif
        params.sub_cond.Wait(params.mutex);
    }
#if THREAD_DEBUG >= 1
    ThreadDebug("All threads began\n");
#endif
    
    // Prevent threads from beginning another work yet
    params.init_ok = false;
    
    // Wait until all threads have completed their work
    while(params.num_done < threads.size())
    {
        if(params.cancelled) break;
#if THREAD_DEBUG >= 1
        ThreadDebug("Waiting num_done, got %u\n", params.num_done);
#endif
        params.sub_cond.Wait(params.mutex);
        if(params.cancelled) break;
    }
    
#if THREAD_DEBUG >= 1
    ThreadDebug("Threads Done-ok\n");
#endif
    // Inform all threads that we know they've finished
    params.done_ok = true; params.main_cond.Broadcast();
    
    if(params.cancelled)
    {
        lck.Unlock();
#if THREAD_DEBUG >= 1
        ThreadDebug("threads Terminating\n");
#endif
        threads.clear();
        return;
    }
    
#if THREAD_DEBUG >= 1
    ThreadDebug("Waiting for doneconfirm\n");
#endif
    // Wait until all threads have received the message
    while(params.num_doneconfirm < threads.size())
    {
#if THREAD_DEBUG >= 1
        ThreadDebug("Waiting num_doneconfirm, got %u\n", params.num_doneconfirm);
#endif
        params.sub_cond.Wait(params.mutex);
    }
#if THREAD_DEBUG >= 1
    ThreadDebug("All threads confirmed done\n");
#endif

    // End, the threads are now waiting for another work
#endif
}

#ifndef _OPENMP
template<typename WorkType>
void* ThreadWorkEngine<WorkType>::WorkRunner
    (ThreadWorkEngine<WorkType>::workerparam& params)
{
    SetCancellableThread();
    
    ScopedLock lck(params.mutex);
    
#if THREAD_DEBUG >= 1
    void* thread_id = &thread_id;
    // ^Just a way to distinguish different threads for debugging output.
    //  The actual value is not important.
#endif
    
    for(;;)
    {
#if THREAD_DEBUG >= 1
        ThreadDebug("Thread %p waiting go\n", thread_id);
#endif
    
        // Wait for a permission to begin work
        while(!params.init_ok)
            params.main_cond.Wait(params.mutex);
        
        // Inform main that we have began
        ++params.num_inits; params.sub_cond.Signal();
#if THREAD_DEBUG >= 1
        ThreadDebug("Thread %p acknowledge (now %u)\n", thread_id,
            params.num_inits);
#endif
        
        // do work here
        
        while(params.work_index < params.num_totalworks
        &&    !params.cancelled)
        {
            const size_t cur_work = params.work_index;
            ++params.work_index;
            
            lck.Unlock();
            
#if THREAD_DEBUG >= 1
            ThreadDebug("Thread %p checks work unit %u/%u\n", thread_id, cur_work+1,
                 params.num_totalworks);
#endif
        
            if(cur_work+1 < params.num_totalworks)
            {
                // Just in case, try to awake some sibling so that this thread
                // won't end up doing all the work by itself
                params.main_cond.Broadcast();
                ForceSwitchThread();
            }
            
#if THREAD_DEBUG >= 1
            ThreadDebug("Thread %p begin work with %u\n", thread_id, cur_work+1);
#endif
            bool cancelflag = params.DoWork(cur_work, *params.work);
#if THREAD_DEBUG >= 1
            ThreadDebug("Thread %p end work\n", thread_id);
#endif
            lck.LockAgain();
            
            if(cancelflag)
            {
                params.cancelled = true;
                break;
            }
        }
        
        // Inform RunTasks that we are done
        ++params.num_done; params.sub_cond.Signal();
#if THREAD_DEBUG >= 1
        ThreadDebug("Thread %p done (now %u)\n", thread_id, params.num_done);
#endif
        
        // Wait for permission to start waiting another job.
        // Without this line, it is possible that the thread
        // finishes so quickly that it will re-enter the init_ok
        // testing loop before main() has had chance to set the
        // flag to false.
        while(!params.done_ok)
        {
#if THREAD_DEBUG >= 1
            ThreadDebug("Thread %p waits for done-ok\n", thread_id);
#endif
            params.main_cond.Wait(params.mutex);
        }
        
        // Inform RunTasks that we are now waiting for another job
        ++params.num_doneconfirm; params.sub_cond.Signal();
#if THREAD_DEBUG >= 1
        ThreadDebug("Thread %p got done-ok, sending doneconfirm (now %u)\n", thread_id,
            params.num_doneconfirm); 
#endif
    }
    return 0;
}
#endif
