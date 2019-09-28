#include <stdlib.h>
#ifndef WIN32
#include <unistd.h>
#else
#include <WinSock2.h>
#endif
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "common.h"
#include "fiber.h"


// Global data
volatile _st_vp_t _st_this_vp;           // This VP
volatile _st_thread_t* _st_this_thread;  // Current thread
volatile int _st_active_count = 0; // Active thread count

volatile time_t _st_curr_time = 0;       // Current time as returned by time(2)
volatile st_utime_t _st_last_tset;       // Last time it was fetched


int st_poll(struct pollfd* pds, int npds, st_utime_t timeout) {
    volatile _st_thread_t* me = _ST_CURRENT_THREAD();
    if (me == NULL) {
        return 0;
    }

    struct pollfd* pd;
    struct pollfd* epd = pds + npds;
    _st_pollq_t pq;
    int n;

    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }

    if ((*_st_eventsys->pollset_add)(pds, npds) < 0) {
        return -1;
    }

    pq.pds = pds;
    pq.npds = npds;
    pq.thread = me;
    pq.on_ioq = 1;
    _ST_ADD_IOQ(pq);
    if (timeout != ST_UTIME_NO_TIMEOUT) {
        _ST_ADD_SLEEPQ(me, timeout);
    }
    me->state = _ST_ST_IO_WAIT;

    _ST_SWITCH_CONTEXT(me);

    n = 0;
    if (pq.on_ioq) {
        // If we timed out, the pollq might still be on the ioq. Remove it
        _ST_DEL_IOQ(pq);
        (*_st_eventsys->pollset_del)(pds, npds);
    } else {
        // Count the number of ready descriptors
        for (pd = pds; pd < epd; pd++) {
            if (pd->revents) {
                n++;
            }
        }
    }

    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }
    return n;
}

void _st_vp_schedule(void) {
    _st_thread_t* thread = NULL;
    if (_ST_RUNQ.next != &_ST_RUNQ) {
        thread = _ST_THREAD_PTR(_ST_RUNQ.next);
        _ST_DEL_RUNQ(thread);
    }
    if (thread == NULL || thread->state != _ST_ST_RUNNABLE) {
        return;
    }
    // Resume the thread
    thread->state = _ST_ST_RUNNING;
    _ST_SET_CURRENT_THREAD(thread);
    if (thread->context != NULL) {
        swapFiber(thread->context);
    }
}

// Initialize this Virtual Processor
int st_init(void) {
    _st_thread_t* thread;
    if (_st_active_count > 0) {
        return 0;
    }

    // We can ignore return value here
    st_set_eventsys(ST_EVENTSYS_DEFAULT);
    if (_st_io_init() < 0) {
        return -1;
    }
    memset(&_st_this_vp, 0, sizeof(_st_vp_t));

    ST_INIT_CLIST(&_ST_RUNQ);
    ST_INIT_CLIST(&_ST_IOQ);
    ST_INIT_CLIST(&_ST_ZOMBIEQ);

    if ((*_st_eventsys->init)() < 0) {
        return -1;
    }
    _st_this_vp.pagesize = getpagesize();
    _st_this_vp.last_clock = st_utime();

    return 0;
}

// Start function for the idle thread
void _st_idle_thread_run() {

    auto sg = createFiberSG();

    while (_st_active_count > 0) {
        // Idle vp till I/O is ready or the smallest timeout expired
        _ST_VP_IDLE();

        // Check sleep queue for expired threads
        _st_vp_check_clock();

        _st_vp_schedule();
    }

    delFiberSG(sg);
}

void st_thread_exit(void* retval) {
    volatile _st_thread_t* thread = _ST_CURRENT_THREAD();
    if (thread == NULL) {
        return;
    }

    thread->retval = retval;
    _st_thread_cleanup(thread);
    _st_active_count--;
    if (thread->term) {
        // Put thread on the zombie queue
        thread->state = _ST_ST_ZOMBIE;
        _ST_ADD_ZOMBIEQ(thread);

        // Notify on our termination condition variable
        st_cond_signal(thread->term);

        // Switch context and come back later
        _ST_SWITCH_CONTEXT(thread);

        // Continue the cleanup
        st_cond_destroy(thread->term);
        thread->term = NULL;
    }

    if (!(thread->flags & _ST_FL_PRIMORDIAL)) {
        _st_stack_free(thread->stack);
    }

    // Find another thread to run
    _ST_SWITCH_CONTEXT(thread);
    // Not going to land here
}

int st_thread_join(_st_thread_t* thread, void** retvalp) {
    _st_cond_t* term = thread->term;

    // Can't join a non-joinable thread
    if (term == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (_ST_CURRENT_THREAD() == thread) {
        errno = EDEADLK;
        return -1;
    }

    // Multiple threads can't wait on the same joinable thread
    if (term->wait_q.next != &term->wait_q) {
        errno = EINVAL;
        return -1;
    }

    while (thread->state != _ST_ST_ZOMBIE) {
        if (st_cond_timedwait(term, ST_UTIME_NO_TIMEOUT) != 0) {
            return -1;
        }
    }

    if (retvalp) {
        *retvalp = thread->retval;
    }

    // Remove target thread from the zombie queue and make it runnable. When it gets scheduled later, it will do the clean up.
    thread->state = _ST_ST_RUNNABLE;
    _ST_DEL_ZOMBIEQ(thread);
    _ST_ADD_RUNQ(thread);

    return 0;
}

// Insert "thread" into the timeout heap, in the position specified by thread->heap_index.
// See docs/timeout_heap.txt for details about the timeout heap.
static _st_thread_t** heap_insert(volatile _st_thread_t* thread) {
    int target = thread->heap_index;
    int s = target;
    _st_thread_t** p = &_ST_SLEEPQ;
    int bits = 0;
    int bit;
    int index = 1;

    while (s) {
        s >>= 1;
        bits++;
    }
    for (bit = bits - 2; bit >= 0; bit--) {
        if (thread->due < (*p)->due) {
            _st_thread_t* t = *p;
            thread->left = t->left;
            thread->right = t->right;
            *p = thread;
            thread->heap_index = index;
            thread = t;
        }
        index <<= 1;
        if (target & (1 << bit)) {
            p = &((*p)->right);
            index |= 1;
        } else {
            p = &((*p)->left);
        }
    }
    thread->heap_index = index;
    *p = thread;
    thread->left = thread->right = NULL;
    return p;
}

// Delete "thread" from the timeout heap.
static void heap_delete(volatile _st_thread_t* thread) {
    _st_thread_t* t, **p;
    int bits = 0;
    int s, bit;

    // First find and unlink the last heap element
    p = &_ST_SLEEPQ;
    s = _ST_SLEEPQ_SIZE;
    while (s) {
        s >>= 1;
        bits++;
    }
    for (bit = bits - 2; bit >= 0; bit--) {
        if (_ST_SLEEPQ_SIZE & (1 << bit)) {
            p = &((*p)->right);
        } else {
            p = &((*p)->left);
        }
    }
    t = *p;
    *p = NULL;
    --_ST_SLEEPQ_SIZE;
    if (t != thread) {
        // Insert the unlinked last element in place of the element we are deleting
        t->heap_index = thread->heap_index;
        p = heap_insert(t);
        t = *p;
        t->left = thread->left;
        t->right = thread->right;

        // Reestablish the heap invariant.
        for (;;) {
            _st_thread_t* y; // The younger child
            int index_tmp;
            if (t->left == NULL) {
                break;
            } else if (t->right == NULL) {
                y = t->left;
            } else if (t->left->due < t->right->due) {
                y = t->left;
            } else {
                y = t->right;
            }
            if (t->due > y->due) {
                _st_thread_t* tl = y->left;
                _st_thread_t* tr = y->right;
                *p = y;
                if (y == t->left) {
                    y->left = t;
                    y->right = t->right;
                    p = &y->left;
                } else {
                    y->left = t->left;
                    y->right = t;
                    p = &y->right;
                }
                t->left = tl;
                t->right = tr;
                index_tmp = t->heap_index;
                t->heap_index = y->heap_index;
                y->heap_index = index_tmp;
            } else {
                break;
            }
        }
    }
    thread->left = thread->right = NULL;
}

void _st_add_sleep_q(volatile _st_thread_t* thread, st_utime_t timeout) {
    thread->due = _ST_LAST_CLOCK + timeout;
    thread->flags |= _ST_FL_ON_SLEEPQ;
    thread->heap_index = ++_ST_SLEEPQ_SIZE;
    heap_insert(thread);
}

void _st_del_sleep_q(volatile _st_thread_t* thread) {
    heap_delete(thread);
    thread->flags &= ~_ST_FL_ON_SLEEPQ;
}

void _st_vp_check_clock(void) {
    _st_thread_t* thread;
    st_utime_t elapsed, now;

    now = st_utime();
    elapsed = now - _ST_LAST_CLOCK;
    _ST_LAST_CLOCK = now;

    if (_st_curr_time && now - _st_last_tset > 999000) {
        _st_curr_time = time(NULL);
        _st_last_tset = now;
    }

    while (_ST_SLEEPQ != NULL) {
        thread = _ST_SLEEPQ;
        ST_ASSERT(thread->flags & _ST_FL_ON_SLEEPQ);
        if (thread->due > now)
            break;
        _ST_DEL_SLEEPQ(thread);

        // If thread is waiting on condition variable, set the time out flag
        if (thread->state == _ST_ST_COND_WAIT) {
            thread->flags |= _ST_FL_TIMEDOUT;
        }
        // Make thread runnable
        ST_ASSERT(!(thread->flags & _ST_FL_IDLE_THREAD));
        thread->state = _ST_ST_RUNNABLE;
        _ST_ADD_RUNQ(thread);
    }
}

void st_thread_interrupt(_st_thread_t* thread) {
    // If thread is already dead
    if (thread->state == _ST_ST_ZOMBIE) {
        return;
    }
    thread->flags |= _ST_FL_INTERRUPT;
    if (thread->state == _ST_ST_RUNNING || thread->state == _ST_ST_RUNNABLE) {
        return;
    }
    if (thread->flags & _ST_FL_ON_SLEEPQ) {
        _ST_DEL_SLEEPQ(thread);
    }
    // Make thread runnable
    thread->state = _ST_ST_RUNNABLE;
    _ST_ADD_RUNQ(thread);
}

void _st_thread_main(intptr_t ptr) {
    _st_thread_t* thd = (_st_thread_t*)ptr;
    if (thd == NULL) {
        return;
    }
    if (thd->start != NULL) {
        thd->start(thd->arg);
    }
    st_thread_exit(thd->retval);
    swapOutFiber();
}

_st_thread_t* st_thread_create(void* (*start)(void* arg), void* arg, int joinable, int stk_size) {
    _st_thread_t* thread;
    _st_stack_t* stack;
    void** ptds;
    char* sp;
#ifdef __ia64__
    char* bsp;
#endif

    // Adjust stack size
    if (stk_size == 0) {
        stk_size = ST_DEFAULT_STACK_SIZE;
    }
    stk_size = ((stk_size + _ST_PAGE_SIZE - 1) / _ST_PAGE_SIZE) * _ST_PAGE_SIZE;
    stack = _st_stack_new(stk_size);
    if (!stack) {
        return NULL;
    }
    // Allocate thread object and per-thread data off the stack
#if defined (MD_STACK_GROWS_DOWN)
    sp = stack->stk_top;
#ifdef __ia64__
    /*
     * The stack segment is split in the middle. The upper half is used
     * as backing store for the register stack which grows upward.
     * The lower half is used for the traditional memory stack which
     * grows downward. Both stacks start in the middle and grow outward
     * from each other.
     */
    sp -= (stk_size >> 1);
    bsp = sp;
    // Make register stack 64-byte aligned
    if ((unsigned long)bsp & 0x3f) {
        bsp = bsp + (0x40 - ((unsigned long)bsp & 0x3f));
    }
    stack->bsp = bsp + _ST_STACK_PAD_SIZE;
#endif
    sp = sp - (ST_KEYS_MAX * sizeof(void*));
    ptds = (void**) sp;
    sp = sp - sizeof(_st_thread_t);
    thread = (_st_thread_t*) sp;

    // Make stack 64-byte aligned
    if ((unsigned long)sp & 0x3f) {
        sp = sp - ((unsigned long)sp & 0x3f);
    }
    stack->sp = sp - _ST_STACK_PAD_SIZE;
#elif defined (MD_STACK_GROWS_UP)
    sp = stack->stk_bottom;
    thread = (_st_thread_t*) sp;
    sp = sp + sizeof(_st_thread_t);
    ptds = (void**) sp;
    sp = sp + (ST_KEYS_MAX * sizeof(void*));

    // Make stack 64-byte aligned
    if ((unsigned long)sp & 0x3f) {
        sp = sp + (0x40 - ((unsigned long)sp & 0x3f));
    }
    stack->sp = sp + _ST_STACK_PAD_SIZE;
#else
#error Unknown OS
#endif

    memset(thread, 0, sizeof(_st_thread_t));
    memset(ptds, 0, ST_KEYS_MAX * sizeof(void*));

    // Initialize thread
    thread->private_data = ptds;
    thread->stack = stack;
    thread->start = start;
    thread->arg = arg;

    thread->context = createFiber(_st_thread_main, (intptr_t)thread, 1 * 1024 * 1024);

    // If thread is joinable, allocate a termination condition variable
    if (joinable) {
        thread->term = st_cond_new();
        if (thread->term == NULL) {
            _st_stack_free(thread->stack);
            return NULL;
        }
    }

    // Make thread runnable
    thread->state = _ST_ST_RUNNABLE;
    _st_active_count++;
    _ST_ADD_RUNQ(thread);

    return thread;
}

st_thread_t st_thread_self(void) {
    return _ST_CURRENT_THREAD();
}

void st_idle_thread_run() {
    _st_idle_thread_run();
}