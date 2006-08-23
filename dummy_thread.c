/* This module provides some names to satisfy the linker
 * with functions that are never used.
*/
int dummy(void) __attribute__((regparm(2)));

int dummy()
{
#define DefSym(s) \
    __asm__ volatile(\
        "nop\n" \
        ".type " #s ", @function\n" \
        ".globl " #s "\n" \
        #s \
        ":\n" );

DefSym(pthread_rwlock_rdlock)
DefSym(pthread_rwlock_unlock)
DefSym(pthread_rwlock_wrlock)
DefSym(pthread_getspecific)
DefSym(pthread_setspecific)
DefSym(pthread_key_create)
DefSym(pthread_key_delete)

DefSym(pthread_mutex_init)
DefSym(pthread_mutex_lock)
DefSym(pthread_mutex_unlock)
DefSym(pthread_mutex_destroy)

DefSym(clock_gettime)

DefSym(_pthread_cleanup_push)
DefSym(_pthread_cleanup_pop)

DefSym(pthread_setcancelstate)
DefSym(pthread_setcanceltype)

DefSym(pthread_self)
DefSym(pthread_kill)
DefSym(pthread_sigmask)

DefSym(pthread_join)
DefSym(pthread_cancel)
DefSym(pthread_create)


    return 0;
}
