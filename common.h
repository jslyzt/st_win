#pragma once

#include <stddef.h>
#include <sys/types.h>
#ifndef WIN32
#include <unistd.h>
#include <sys/time.h>
#endif
#include <setjmp.h>

// Enable assertions only if DEBUG is defined
#ifndef DEBUG
#define NDEBUG
#endif
#include <assert.h>
#define ST_ASSERT(expr) assert(expr)

#define ST_BEGIN_MACRO  {
#define ST_END_MACRO    }
#define	ST_HIDDEN   static

#include "st.h"
#include "md.h"

#ifdef __cplusplus
extern "C" {
#endif

// Circular linked list definitions
typedef struct _st_clist {
    struct _st_clist* next;
    struct _st_clist* prev;
} _st_clist_t;

// Insert element "_e" into the list, before "_l"
#define ST_INSERT_BEFORE(_e, _l) \
    ST_BEGIN_MACRO \
    (_e)->next = (_l); \
    (_e)->prev = (_l)->prev; \
    if((_l)->prev != NULL) { \
        (_l)->prev->next = (_e); \
    } \
    (_l)->prev = (_e); \
    ST_END_MACRO

// Insert element "_e" into the list, after "_l"
#define ST_INSERT_AFTER(_e, _l) \
    ST_BEGIN_MACRO \
    (_e)->next = (_l)->next; \
    (_e)->prev = (_l); \
    if((_l)->next != NULL) { \
        (_l)->next->prev = (_e); \
    } \
    (_l)->next = (_e); \
    ST_END_MACRO

// Return the element following element "_e"
#define ST_NEXT_LINK(_e) ((_e)->next)

// Append an element "_e" to the end of the list "_l"
#define ST_APPEND_LINK(_e,_l) ST_INSERT_BEFORE(_e,_l)

// Insert an element "_e" at the head of the list "_l"
#define ST_INSERT_LINK(_e,_l) ST_INSERT_AFTER(_e,_l)

// Return the head/tail of the list
#define ST_LIST_HEAD(_l) (_l)->next
#define ST_LIST_TAIL(_l) (_l)->prev

// Remove the element "_e" from it's circular list
#define ST_REMOVE_LINK(_e) \
    ST_BEGIN_MACRO \
    if((_e)->prev != NULL) { \
        (_e)->prev->next = (_e)->next; \
    } \
    if ((_e)->next != NULL) { \
        (_e)->next->prev = (_e)->prev; \
    } \
    ST_END_MACRO

// Return non-zero if the given circular list "_l" is empty, zero if the circular list is not empty
#define ST_CLIST_IS_EMPTY(_l) \
    ((_l)->next == (_l))

// Initialize a circular list
#define ST_INIT_CLIST(_l) \
    ST_BEGIN_MACRO \
    (_l)->next = (_l); \
    (_l)->prev = (_l); \
    ST_END_MACRO

#define ST_INIT_STATIC_CLIST(_l) \
    {(_l), (_l)}


// Basic types definitions
typedef void (*_st_destructor_t)(void*);


typedef struct _st_stack {
    _st_clist_t links;
    char* vaddr;                // Base of stack's allocated memory
    int vaddr_size;             // Size of stack's allocated memory
    int stk_size;               // Size of usable portion of the stack
    char* stk_bottom;           // Lowest address of stack's usable portion
    char* stk_top;              // Highest address of stack's usable portion
    void* sp;                   // Stack pointer from C's point of view
#ifdef __ia64__
    void* bsp;                  // Register stack backing store pointer
#endif
} _st_stack_t;


typedef struct _st_cond {
    _st_clist_t wait_q;         // Condition variable wait queue
} _st_cond_t;


typedef struct _st_thread _st_thread_t;

struct _st_thread {
    volatile int state;         // Thread's state
    volatile int flags;         // Thread's flags

    void* (*start)(void* arg);  // The start function of the thread
    void* arg;                  // Argument of the start function
    void* retval;               // Return value of the start function

    _st_stack_t* stack;         // Info about thread's stack

    _st_clist_t links;          // For putting on run/sleep/zombie queue
    _st_clist_t wait_links;     // For putting on mutex/condvar wait queue

    st_utime_t due;             // Wakeup time when thread is sleeping
    _st_thread_t* left;         // For putting in timeout heap
    _st_thread_t* right;        // -- see docs/timeout_heap.txt for details
    int heap_index;

    void** private_data;        // Per thread private data

    _st_cond_t* term;           // Termination condition variable for join
#ifdef MD_INIT_CONTEXT
    jmp_buf context;            // Thread's context
#else
    void* context;
#endif
};


typedef struct _st_mutex {
    _st_thread_t* owner;        // Current mutex owner
    _st_clist_t wait_q;         // Mutex wait queue
} _st_mutex_t;


typedef struct _st_pollq {
    _st_clist_t links;          // For putting on io queue
    _st_thread_t* thread;       // Polling thread
    struct pollfd* pds;         // Array of poll descriptors
    int npds;                   // Length of the array
    int on_ioq;                 // Is it on ioq?
} _st_pollq_t;


typedef struct _st_eventsys_ops {
    const char* name;                          // Name of this event system
    int val;                                   // Type of this event system
    int (*init)(void);                         // Initialization
    void (*dispatch)(void);                    // Dispatch function
    int (*pollset_add)(struct pollfd*, int);   // Add descriptor set
    void (*pollset_del)(struct pollfd*, int);  // Delete descriptor set
    int (*fd_new)(int);                        // New descriptor allocated
    int (*fd_close)(int);                      // Descriptor closed
    int (*fd_getlimit)(void);                  // Descriptor hard limit
} _st_eventsys_t;


typedef struct _st_vp {
    _st_thread_t* idle_thread;  // Idle thread for this vp
    st_utime_t last_clock;      // The last time we went into vp_check_clock()

    _st_clist_t run_q;          // run queue for this vp
    _st_clist_t io_q;           // io queue for this vp
    _st_clist_t zombie_q;       // zombie queue for this vp

    int pagesize;

    _st_thread_t* sleep_q;      // sleep queue for this vp
    int sleepq_size;            // number of threads on sleep queue
} _st_vp_t;


typedef struct _st_netfd {
    int osfd;                   // Underlying OS file descriptor
    int inuse;                  // In-use flag
    void* private_data;         // Per descriptor private data
    _st_destructor_t destructor; // Private data destructor function
    void* aux_data;             // Auxiliary data for internal use
    struct _st_netfd* next;     // For putting on the free list
} _st_netfd_t;


// Current vp, thread, and event system
extern volatile _st_vp_t _st_this_vp;
extern volatile _st_thread_t* _st_this_thread;
extern volatile _st_eventsys_t* _st_eventsys;

#define _ST_CURRENT_THREAD() (_st_this_thread)
#define _ST_SET_CURRENT_THREAD(_thread) (_st_this_thread = (_thread))

#define _ST_LAST_CLOCK  (_st_this_vp.last_clock)

#define _ST_RUNQ        (_st_this_vp.run_q)
#define _ST_IOQ         (_st_this_vp.io_q)
#define _ST_ZOMBIEQ     (_st_this_vp.zombie_q)

#define _ST_PAGE_SIZE   (_st_this_vp.pagesize)

#define _ST_SLEEPQ      (_st_this_vp.sleep_q)
#define _ST_SLEEPQ_SIZE (_st_this_vp.sleepq_size)

#define _ST_VP_IDLE()   (*_st_eventsys->dispatch)()

// vp queues operations
#define _ST_ADD_IOQ(_pq)    ST_APPEND_LINK(&_pq.links, &_ST_IOQ)
#define _ST_DEL_IOQ(_pq)    ST_REMOVE_LINK(&_pq.links)

#define _ST_ADD_RUNQ(_thr)  ST_APPEND_LINK(&(_thr)->links, &_ST_RUNQ)
#define _ST_DEL_RUNQ(_thr)  ST_REMOVE_LINK(&(_thr)->links)

#define _ST_ADD_SLEEPQ(_thr, _timeout)  _st_add_sleep_q(_thr, _timeout)
#define _ST_DEL_SLEEPQ(_thr)   _st_del_sleep_q(_thr)

#define _ST_ADD_ZOMBIEQ(_thr)  ST_APPEND_LINK(&(_thr)->links, &_ST_ZOMBIEQ)
#define _ST_DEL_ZOMBIEQ(_thr)  ST_REMOVE_LINK(&(_thr)->links)

// Thread states and flags
#define _ST_ST_RUNNING      0   // 运行中
#define _ST_ST_RUNNABLE     1   // 可运行
#define _ST_ST_IO_WAIT      2   // io等待
#define _ST_ST_LOCK_WAIT    3   // io锁
#define _ST_ST_COND_WAIT    4   // io条件
#define _ST_ST_SLEEPING     5   // sleep
#define _ST_ST_ZOMBIE       6   // 结束
#define _ST_ST_SUSPENDED    7   // 暂停

#define _ST_FL_PRIMORDIAL   0x01
#define _ST_FL_IDLE_THREAD  0x02
#define _ST_FL_ON_SLEEPQ    0x04
#define _ST_FL_INTERRUPT    0x08
#define _ST_FL_TIMEDOUT     0x10


// Pointer conversion
#ifndef offsetof
#define offsetof(type, identifier) ((size_t)&(((type *)0)->identifier))
#endif

#define _ST_THREAD_PTR(_qp) \
    ((_st_thread_t *)((char *)(_qp) - offsetof(_st_thread_t, links)))

#define _ST_THREAD_WAITQ_PTR(_qp) \
    ((_st_thread_t *)((char *)(_qp) - offsetof(_st_thread_t, wait_links)))

#define _ST_THREAD_STACK_PTR(_qp) \
    ((_st_stack_t *)((char*)(_qp) - offsetof(_st_stack_t, links)))

#define _ST_POLLQUEUE_PTR(_qp) \
    ((_st_pollq_t *)((char *)(_qp) - offsetof(_st_pollq_t, links)))

// Constants
#ifndef ST_UTIME_NO_TIMEOUT
#define ST_UTIME_NO_TIMEOUT ((st_utime_t) -1LL)
#endif

#ifndef __ia64__
#define ST_DEFAULT_STACK_SIZE (64*1024)
#else
#define ST_DEFAULT_STACK_SIZE (128*1024)  // Includes register stack size
#endif

#ifndef ST_KEYS_MAX
#define ST_KEYS_MAX 16
#endif

#ifndef ST_MIN_POLLFDS_SIZE
#define ST_MIN_POLLFDS_SIZE 64
#endif

// Number of bytes reserved under the stack "bottom"
#define _ST_STACK_PAD_SIZE MD_STACK_PAD_SIZE

// Forward declarations
void _st_vp_schedule(void);
void _st_vp_check_clock(void);
void _st_idle_thread_run();
void* _st_idle_thread_start(void*);
void _st_thread_cleanup(volatile _st_thread_t* thread);
void _st_add_sleep_q(volatile _st_thread_t* thread, st_utime_t timeout);
void _st_del_sleep_q(volatile _st_thread_t* thread);
_st_stack_t* _st_stack_new(int stack_size);
void _st_stack_free(_st_stack_t* ts);
int _st_io_init(void);

st_utime_t st_utime(void);
_st_cond_t* st_cond_new(void);
int st_cond_destroy(_st_cond_t* cvar);
int st_cond_timedwait(_st_cond_t* cvar, st_utime_t timeout);
int st_cond_signal(_st_cond_t* cvar);
ssize_t st_read(_st_netfd_t* fd, void* buf, size_t nbyte, st_utime_t timeout);
ssize_t st_write(_st_netfd_t* fd, const void* buf, size_t nbyte, st_utime_t timeout);
int st_poll(struct pollfd* pds, int npds, st_utime_t timeout);
_st_thread_t* st_thread_create(void* (*start)(void* arg), void* arg, int joinable, int stk_size);

#ifdef __cplusplus
}
#endif
