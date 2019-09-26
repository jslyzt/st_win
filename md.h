#pragma once

#if defined(ETIMEDOUT) && !defined(ETIME)
#define ETIME ETIMEDOUT
#endif

#if defined(MAP_ANONYMOUS) && !defined(MAP_ANON)
#define MAP_ANON MAP_ANONYMOUS
#endif

#ifndef MAP_FAILED
#define MAP_FAILED -1
#endif

// Platform specifics

#if defined (CYGWIN)

#define MD_STACK_GROWS_DOWN
#define MD_USE_BSD_ANON_MMAP
#define MD_ACCEPT_NB_NOT_INHERITED
#define MD_ALWAYS_UNSERIALIZED_ACCEPT

#define MD_SETJMP(env) setjmp(env)
#define MD_LONGJMP(env, val) longjmp(env, val)

#define MD_JB_SP 7
#define MD_GET_SP(_t) (_t)->context[MD_JB_SP]

#define MD_INIT_CONTEXT(_thread, _sp, _main) \
    ST_BEGIN_MACRO \
    if (MD_SETJMP((_thread)->context)) { \
        _main(); \
    } \
    MD_GET_SP(_thread) = (long) (_sp); \
    ST_END_MACRO

#define MD_GET_UTIME() \
    struct timeval tv; \
    (void) gettimeofday(&tv, NULL); \
    return (tv.tv_sec * 1000000LL + tv.tv_usec)

#elif defined (WIN32)
#include <setjmp.h>
#include <sys/timeb.h>

int getpagesize(void);
int _st_GetError(int err);

#if FD_SETSIZE < 200
#undef FD_SETSIZE
#define FD_SETSIZE 200
#endif

#define MD_DONT_HAVE_POLL
#define MALLOC_STACK
#define MD_STACK_GROWS_DOWN
#define MD_ACCEPT_NB_NOT_INHERITED
#define MD_ALWAYS_UNSERIALIZED_ACCEPT

#define MD_SETJMP(env)
#define MD_LONGJMP(env, val) \
    ST_BEGIN_MACRO \
    if (env != NULL) { \
        swapFiber(env, 0); \
    } \
    ST_END_MACRO

#define MD_GET_UTIME() \
  struct _timeb tb; \
  _ftime(&tb); \
  return((tb.time*1000000)+(tb.millitm*1000));

#define _ST_SWITCH_CONTEXT(thd) \
    ST_BEGIN_MACRO \
    if((thd)->context != NULL) { \
        swapFiber((thd)->context, 0); \
    } \
    ST_END_MACRO

#elif defined (DARWIN)

#define MD_STACK_GROWS_DOWN
#define MD_USE_BSD_ANON_MMAP
#define MD_ACCEPT_NB_INHERITED
#define MD_ALWAYS_UNSERIALIZED_ACCEPT
#define MD_HAVE_SOCKLEN_T

#define MD_SETJMP(env) _setjmp(env)
#define MD_LONGJMP(env, val) _longjmp(env, val)

#if defined(__ppc__)
#define MD_JB_SP  0
#elif defined(__i386__)
#define MD_JB_SP  9
#elif defined(__x86_64__)
#define MD_JB_SP  4
#else
#error Unknown CPU architecture
#endif

#define MD_INIT_CONTEXT(_thread, _sp, _main) \
    ST_BEGIN_MACRO \
    if (MD_SETJMP((_thread)->context)) { \
        _main(); \
    } \
    *((long *)&((_thread)->context[MD_JB_SP])) = (long) (_sp); \
    ST_END_MACRO

#define MD_GET_UTIME() \
    struct timeval tv; \
    (void) gettimeofday(&tv, NULL); \
    return (tv.tv_sec * 1000000LL + tv.tv_usec)

#elif defined (FREEBSD)

#define MD_STACK_GROWS_DOWN
#define MD_USE_BSD_ANON_MMAP
#define MD_ACCEPT_NB_INHERITED
#define MD_ALWAYS_UNSERIALIZED_ACCEPT

#define MD_SETJMP(env) _setjmp(env)
#define MD_LONGJMP(env, val) _longjmp(env, val)

#if defined(__i386__)
#define MD_JB_SP   2
#elif defined(__alpha__)
#define MD_JB_SP  34
#elif defined(__amd64__)
#define MD_JB_SP   2
#else
#error Unknown CPU architecture
#endif

#define MD_INIT_CONTEXT(_thread, _sp, _main) \
    ST_BEGIN_MACRO \
    if (MD_SETJMP((_thread)->context)) { \
        _main(); \
    } \
    (_thread)->context[0]._jb[MD_JB_SP] = (long) (_sp); \
    ST_END_MACRO

#define MD_GET_UTIME() \
    struct timeval tv; \
    (void) gettimeofday(&tv, NULL); \
    return (tv.tv_sec * 1000000LL + tv.tv_usec)

#elif defined (LINUX)

// These are properties of the linux kernel and are the same on every flavor and architecture.
#define MD_USE_BSD_ANON_MMAP
#define MD_ACCEPT_NB_NOT_INHERITED
#define MD_ALWAYS_UNSERIALIZED_ACCEPT

// Modern GNU/Linux is Posix.1g compliant.
#define MD_HAVE_SOCKLEN_T

// All architectures and flavors of linux have the gettimeofday function but if you know of a faster way, use it.
#define MD_GET_UTIME() \
    struct timeval tv; \
    (void) gettimeofday(&tv, NULL); \
    return (tv.tv_sec * 1000000LL + tv.tv_usec)

#if defined(__ia64__)
#define MD_STACK_GROWS_DOWN

/*
 * IA-64 architecture.  Besides traditional memory call stack, IA-64
 * uses general register stack.  Thus each thread needs a backing store
 * for register stack in addition to memory stack.  Standard
 * setjmp()/longjmp() cannot be used for thread context switching
 * because their implementation implicitly assumes that only one register stack exists.
 */
#ifdef USE_LIBC_SETJMP
#undef USE_LIBC_SETJMP
#endif
#define MD_USE_BUILTIN_SETJMP

#define MD_STACK_PAD_SIZE 128

// Last register stack frame must be preserved
#define MD_INIT_CONTEXT(_thread, _sp, _bsp, _main) \
    ST_BEGIN_MACRO \
    if (MD_SETJMP((_thread)->context)) { \
        _main(); \
    } \
    memcpy((char *)(_bsp) - MD_STACK_PAD_SIZE, \
            (char *)(_thread)->context[0].__jmpbuf[17] - MD_STACK_PAD_SIZE, \
            MD_STACK_PAD_SIZE); \
    (_thread)->context[0].__jmpbuf[0]  = (long) (_sp); \
    (_thread)->context[0].__jmpbuf[17] = (long) (_bsp); \
    ST_END_MACRO

#elif defined(__mips__)
#define MD_STACK_GROWS_DOWN

#define MD_INIT_CONTEXT(_thread, _sp, _main) \
    ST_BEGIN_MACRO \
    MD_SETJMP((_thread)->context); \
    _thread->context[0].__jmpbuf[0].__pc = (__ptr_t) _main;  \
    _thread->context[0].__jmpbuf[0].__sp = _sp; \
    ST_END_MACRO

#else

/*
 * On linux, there are a few styles of jmpbuf format.  These vary based on architecture/glibc combination.
 * Most of the glibc based toggles were lifted from: mozilla/nsprpub/pr/include/md/_linux.h
 */

#if defined(__powerpc__)
#define MD_STACK_GROWS_DOWN

#if (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 1)
#ifndef JB_GPR1
#define JB_GPR1 0
#endif
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[JB_GPR1]
#else
// not an error but certainly cause for caution
#error "Untested use of old glibc on powerpc"
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__misc[0]
#endif

#elif defined(__alpha)
#define MD_STACK_GROWS_DOWN

#if defined(__GLIBC__) && __GLIBC__ >= 2
#ifndef JB_SP
#define JB_SP 8
#endif
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[JB_SP]
#else
// not an error but certainly cause for caution
#error "Untested use of old glibc on alpha"
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__sp
#endif

#elif defined(__mc68000__)
#define MD_STACK_GROWS_DOWN

// m68k still uses old style sigjmp_buf
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__sp

#elif defined(__sparc__)
#define MD_STACK_GROWS_DOWN

#if defined(__GLIBC__) && __GLIBC__ >= 2
#ifndef JB_SP
#define JB_SP 0
#endif
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[JB_SP]
#else
// not an error but certainly cause for caution
#error "Untested use of old glic on sparc -- also using odd mozilla derived __fp"
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__fp
#endif

#elif defined(__i386__)
#define MD_STACK_GROWS_DOWN
#define MD_USE_BUILTIN_SETJMP

#if defined(__GLIBC__) && __GLIBC__ >= 2
#ifndef JB_SP
#define JB_SP 4
#endif
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[JB_SP]
#else
// not an error but certainly cause for caution
#error "Untested use of old glibc on i386"
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__sp
#endif

#elif defined(__amd64__) || defined(__x86_64__)
#define MD_STACK_GROWS_DOWN
#define MD_USE_BUILTIN_SETJMP

#ifndef JB_RSP
#define JB_RSP 6
#endif
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[JB_RSP]

#elif defined(__arm__)
#define MD_STACK_GROWS_DOWN

#if defined(__GLIBC__) && __GLIBC__ >= 2
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[20]
#else
#error "ARM/Linux pre-glibc2 not supported yet"
#endif // defined(__GLIBC__) && __GLIBC__ >= 2

#elif defined(__s390__)
#define MD_STACK_GROWS_DOWN

// There is no JB_SP in glibc at this time. (glibc 2.2.5)
#define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__gregs[9]

#elif defined(__hppa__)
#define MD_STACK_GROWS_UP

// yes, this is gross, unfortunately at the moment (2002/08/01) there is a bug in hppa's glibc header definition for JB_SP, so we can't use that...
#define MD_GET_SP(_t) (*(long *)(((char *)&(_t)->context[0].__jmpbuf[0]) + 76))

#else
#error "Unknown CPU architecture"
#endif

#define MD_INIT_CONTEXT(_thread, _sp, _main) \
    ST_BEGIN_MACRO \
    if (MD_SETJMP((_thread)->context)) { \
        _main(); \
    } \
    MD_GET_SP(_thread) = (long) (_sp); \
    ST_END_MACRO

#endif

#if defined(MD_USE_BUILTIN_SETJMP) && !defined(USE_LIBC_SETJMP)
#define MD_SETJMP(env) _st_md_cxt_save(env)
#define MD_LONGJMP(env, val) _st_md_cxt_restore(env, val)

extern int _st_md_cxt_save(jmp_buf env);
extern void _st_md_cxt_restore(jmp_buf env, int val);
#else
#define MD_SETJMP(env) setjmp(env)
#define MD_LONGJMP(env, val) longjmp(env, val)
#endif

#endif

#if !defined(MD_HAVE_POLL) && !defined(MD_DONT_HAVE_POLL)
#define MD_HAVE_POLL
#endif

#ifndef MD_STACK_PAD_SIZE
#define MD_STACK_PAD_SIZE 128
#endif

#if !defined(MD_HAVE_SOCKLEN_T) && !defined(socklen_t)
#define socklen_t int
#endif

#ifndef MD_CAP_STACK
#define MD_CAP_STACK(var_addr)
#endif

// Switch away from the current thread context by saving its state and calling the thread scheduler
#ifndef _ST_SWITCH_CONTEXT
#define _ST_SWITCH_CONTEXT(_thread) \
    ST_BEGIN_MACRO \
    if (!MD_SETJMP((_thread)->context)) { \
        _st_vp_schedule(); \
    } \
    ST_END_MACRO
#endif