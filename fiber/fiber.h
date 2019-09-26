#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef void* fcontext_t;
    typedef void(__stdcall* fn_t)(intptr_t);

    void* createFiberSG();
    void delFiberSG(void* sg);

    void* createFiber(fn_t fn, intptr_t vp, uint64_t stackSize);
    void swapFiber(void* ctx, int stop);
    void delFiber(void* ctx);

#ifdef __cplusplus
}
#endif
