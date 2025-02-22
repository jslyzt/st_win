#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include "common.h"

extern volatile time_t _st_curr_time;
extern volatile st_utime_t _st_last_tset;
extern volatile int _st_active_count;

static st_utime_t (*_st_utime)(void) = NULL;

// Time functions
st_utime_t st_utime(void) {
    if (_st_utime == NULL) {
#ifdef MD_GET_UTIME
        MD_GET_UTIME();
#else
#error Unknown OS
#endif
    }

    return (*_st_utime)();
}

int st_set_utime_function(st_utime_t (*func)(void)) {
    if (_st_active_count) {
        errno = EINVAL;
        return -1;
    }
    _st_utime = func;
    return 0;
}

st_utime_t st_utime_last_clock(void) {
    return _ST_LAST_CLOCK;
}

int st_timecache_set(int on) {
    int wason = (_st_curr_time) ? 1 : 0;
    if (on) {
        _st_curr_time = time(NULL);
        _st_last_tset = st_utime();
    } else {
        _st_curr_time = 0;
    }
    return wason;
}

time_t st_time(void) {
    if (_st_curr_time) {
        return _st_curr_time;
    }
    return time(NULL);
}

int st_usleep(st_utime_t usecs) {
    volatile _st_thread_t* me = _ST_CURRENT_THREAD();
    if (me != NULL && me->state != _ST_ST_ZOMBIE) {
        if (me->flags & _ST_FL_INTERRUPT) {
            me->flags &= ~_ST_FL_INTERRUPT;
            errno = EINTR;
            return -1;
        }
        if (usecs != ST_UTIME_NO_TIMEOUT) {
            if (me->state != _ST_ST_SLEEPING) {
                me->state = _ST_ST_SLEEPING;
                _ST_ADD_SLEEPQ(me, usecs);
            }
        } else {
            if (me->state != _ST_ST_SUSPENDED) {
                me->state = _ST_ST_SUSPENDED;
            }
        }
    }
    _st_vp_schedule();
    return 0;
}

int st_sleep(int secs) {
    return st_usleep((secs >= 0) ? secs * (st_utime_t) 1000000LL : ST_UTIME_NO_TIMEOUT);
}


// Condition variable functions
_st_cond_t* st_cond_new(void) {
    _st_cond_t* cvar;
    cvar = (_st_cond_t*) calloc(1, sizeof(_st_cond_t));
    if (cvar) {
        ST_INIT_CLIST(&cvar->wait_q);
    }
    return cvar;
}

int st_cond_destroy(_st_cond_t* cvar) {
    if (cvar->wait_q.next != &cvar->wait_q) {
        errno = EBUSY;
        return -1;
    }
    free(cvar);
    return 0;
}

int st_cond_timedwait(_st_cond_t* cvar, st_utime_t timeout) {
    volatile _st_thread_t* me = _ST_CURRENT_THREAD();
    if (me == NULL) {
        return 0;
    }
    int rv;
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }

    // Put caller thread on the condition variable's wait queue
    me->state = _ST_ST_COND_WAIT;
    ST_APPEND_LINK(&me->wait_links, &cvar->wait_q);

    if (timeout != ST_UTIME_NO_TIMEOUT) {
        _ST_ADD_SLEEPQ(me, timeout);
    }

    _st_vp_schedule();
    //_ST_SWITCH_CONTEXT(me);

    ST_REMOVE_LINK(&me->wait_links);
    rv = 0;

    if (me->flags & _ST_FL_TIMEDOUT) {
        me->flags &= ~_ST_FL_TIMEDOUT;
        errno = ETIME;
        rv = -1;
    }
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        rv = -1;
    }
    return rv;
}

int st_cond_wait(_st_cond_t* cvar) {
    return st_cond_timedwait(cvar, ST_UTIME_NO_TIMEOUT);
}

static int _st_cond_signal(_st_cond_t* cvar, int broadcast) {
    _st_thread_t* thread;
    _st_clist_t* q;

    for (q = cvar->wait_q.next; q != &cvar->wait_q; q = q->next) {
        thread = _ST_THREAD_WAITQ_PTR(q);
        if (thread->state == _ST_ST_COND_WAIT) {
            if (thread->flags & _ST_FL_ON_SLEEPQ) {
                _ST_DEL_SLEEPQ(thread);
            }
            // Make thread runnable
            thread->state = _ST_ST_RUNNABLE;
            _ST_ADD_RUNQ(thread);
            if (!broadcast) {
                break;
            }
        }
    }
    return 0;
}

int st_cond_signal(_st_cond_t* cvar) {
    return _st_cond_signal(cvar, 0);
}

int st_cond_broadcast(_st_cond_t* cvar) {
    return _st_cond_signal(cvar, 1);
}

// Mutex functions
_st_mutex_t* st_mutex_new(void) {
    _st_mutex_t* lock;
    lock = (_st_mutex_t*) calloc(1, sizeof(_st_mutex_t));
    if (lock) {
        ST_INIT_CLIST(&lock->wait_q);
        lock->owner = NULL;
    }
    return lock;
}

int st_mutex_destroy(_st_mutex_t* lock) {
    if (lock->owner != NULL || lock->wait_q.next != &lock->wait_q) {
        errno = EBUSY;
        return -1;
    }
    free(lock);
    return 0;
}

int st_mutex_lock(_st_mutex_t* lock) {
    volatile _st_thread_t* me = _ST_CURRENT_THREAD();
    if (me != NULL) {
        if (me->flags & _ST_FL_INTERRUPT) {
            me->flags &= ~_ST_FL_INTERRUPT;
            errno = EINTR;
            return -1;
        }
        if (lock->owner == NULL) {
            // Got the mutex
            lock->owner = me;
            return 0;
        }
        if (lock->owner == me) {
            errno = EDEADLK;
            return -1;
        }

        // Put caller thread on the mutex's wait queue
        me->state = _ST_ST_LOCK_WAIT;
        ST_APPEND_LINK(&me->wait_links, &lock->wait_q);

        _st_vp_schedule();
        //_ST_SWITCH_CONTEXT(me);

        ST_REMOVE_LINK(&me->wait_links);

        if ((me->flags & _ST_FL_INTERRUPT) && lock->owner != me) {
            me->flags &= ~_ST_FL_INTERRUPT;
            errno = EINTR;
            return -1;
        }
    }
    return 0;
}

int st_mutex_unlock(_st_mutex_t* lock) {
    _st_thread_t* thread;
    _st_clist_t* q;
    if (lock->owner != _ST_CURRENT_THREAD()) {
        errno = EPERM;
        return -1;
    }
    for (q = lock->wait_q.next; q != &lock->wait_q; q = q->next) {
        thread = _ST_THREAD_WAITQ_PTR(q);
        if (thread->state == _ST_ST_LOCK_WAIT) {
            lock->owner = thread;
            // Make thread runnable
            thread->state = _ST_ST_RUNNABLE;
            _ST_ADD_RUNQ(thread);
            return 0;
        }
    }

    // No threads waiting on this mutex
    lock->owner = NULL;
    return 0;
}

int st_mutex_trylock(_st_mutex_t* lock) {
    if (lock->owner != NULL) {
        errno = EBUSY;
        return -1;
    }
    // Got the mutex
    lock->owner = _ST_CURRENT_THREAD();
    return 0;
}
