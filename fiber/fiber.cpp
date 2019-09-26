#include "fiber.h"
#include "context.h"

void* createFiberSG() {
    return new FiberSG();
}

void delFiberSG(void* sg) {
    if (sg == nullptr) {
        return;
    }
    FiberSG* fsg = static_cast<FiberSG*>(sg);
    if (fsg == nullptr) {
        return;
    }
    delete fsg;
}

void* createFiber(fn_t fn, intptr_t vp, uint64_t stackSize) {
    auto ctx = new Context(fn, vp, stackSize);
    if (ctx != nullptr && ctx->getCtx() == nullptr) {
        delete ctx;
        ctx = nullptr;
    }
    return ctx;
}

void swapFiber(void* ctx) {
    if (ctx == nullptr) {
        return;
    }
    Context* cnt = static_cast<Context*>(ctx);
    if (cnt == nullptr) {
        return;
    }
    cnt->SwapIn();
}

void swapOutFiber() {
    Context::SwapOut();
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
