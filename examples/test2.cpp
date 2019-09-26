#if 1

#include <iostream>
#include <functional>
#include <context.h>
#include <fiber.h>

typedef std::function<void()> TaskF;

void run() {
    std::cout << "call by longjmp." << std::endl;

    auto& ctx = FiberSG::CurrentContext();
    if (ctx != nullptr) {
        ctx->SwapOut();
    }
}

void runptr(intptr_t) {
    run();
}

int main(int argc, char** argv) {
    FiberSG sg;

    Context ctx(&runptr, 0, 1 * 1024 * 1024);
    ctx.SwapIn();

    auto ptr = createFiber(&runptr, 0, 1 * 1024 * 1024);
    swapFiber(ptr, 0);

    return 0;
}

#endif