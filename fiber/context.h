#pragma once

#include "fiber.h"
#include <WinSock2.h>
#include <Windows.h>
#include <algorithm>

#if defined(_MSC_VER) && _MSC_VER < 1900
# define thread_local __declspec(thread)
#endif

struct CtxTls {
    void* native;
    volatile int quote;
};

class Context;
struct FiberScopedGuard {
    FiberScopedGuard::FiberScopedGuard() {
        auto& ctx = GetTlsContext();
        if (ctx == nullptr) {
            ctx = new CtxTls{ ConvertThreadToFiber(nullptr), 1};
        } else {
            ++ctx->quote;
        }
    }
    FiberScopedGuard::~FiberScopedGuard() {
        auto& ctx = GetTlsContext();
        if (ctx != nullptr) {
            ctx->quote--;
            if (ctx->quote <= 0) {
                ConvertFiberToThread();
                delete ctx;
                ctx = nullptr;
            }
        }
    }
    static CtxTls*& GetTlsContext() {
        static thread_local CtxTls* native = nullptr;
        return native;
    }
    static Context*& CurrentContext() {
        static thread_local Context* current = nullptr;
        return current;
    }
};

typedef FiberScopedGuard FiberSG;

class Context {
public:
    Context(fn_t fn, intptr_t vp, std::size_t stackSize) {
        SIZE_T commit_size = 4 * 1024;
        ctx_ = CreateFiberEx(commit_size, std::max<std::size_t>(stackSize, commit_size),
            FIBER_FLAG_FLOAT_SWITCH, (LPFIBER_START_ROUTINE)fn, (LPVOID)vp);
    }

    ~Context() {
        DeleteFiber(ctx_);
    }

    void SwapIn() {
        auto& sg = FiberSG::CurrentContext();
        if (sg != this) {
            sg = this;
        }
        auto& ctx = FiberSG::GetTlsContext();
        if (ctx != nullptr && ctx->native != nullptr && ctx_ != nullptr) {
            SwitchToFiber(ctx_);
        }
    }

    static void SwapOut() {
        auto& sg = FiberSG::CurrentContext();
        if (sg != nullptr) {
            sg = nullptr;
            auto& ctx = FiberSG::GetTlsContext();
            if (ctx != nullptr && ctx->native != nullptr) {
                SwitchToFiber(ctx->native);
            }
        }
    }

    inline void* getCtx() {
        return ctx_;
    }

private:
    void* ctx_;
};

