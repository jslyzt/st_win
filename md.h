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

// only support win32
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
        swapFiber(env); \
    } \
    ST_END_MACRO

#define MD_GET_UTIME() \
  struct _timeb tb; \
  _ftime(&tb); \
  return((tb.time*1000000)+(tb.millitm*1000));

#define _ST_SWITCH_CONTEXT(thd) \
    ST_BEGIN_MACRO \
    if((thd)->context != NULL) { \
        swapFiber((thd)->context); \
    } \
    ST_END_MACRO


#ifndef MD_STACK_PAD_SIZE
#define MD_STACK_PAD_SIZE 128
#endif

#if !defined(MD_HAVE_SOCKLEN_T) && !defined(socklen_t)
#define socklen_t int
#endif
