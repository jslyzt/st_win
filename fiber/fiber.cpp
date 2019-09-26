#include "fiber.h"
#include "context.h"

void* createFiber(fn_t fn, intptr_t vp, uint64_t stackSize) {
    auto ctx = new Context(fn, vp, stackSize);
    if (ctx != nullptr && ctx->getCtx() == nullptr) {
        delete ctx;
        ctx = nullptr;
    }
    return ctx;
}

void swapFiber(void* ctx, int stop) {
    if (ctx == nullptr) {
        return;
    }
    Context* cnt = static_cast<Context*>(ctx);
    if (cnt == nullptr) {
        return;
    }
    if (stop > 0) {
        cnt->SwapOut();
    } else {
        cnt->SwapIn();
    }
}

void delFiber(void* ctx) {
    if (ctx == nullptr) {
        return;
    }
    Context* cnt = static_cast<Context*>(ctx);
    if (cnt == nullptr) {
        return;
    }
    delete cnt;
}
