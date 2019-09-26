#if 1

#include <iostream>
#include <functional>
#include <context.h>
#include <fiber.h>

void runstr(const char* str) {
    std::cout << str << std::endl;

    auto& ctx = FiberSG::CurrentContext();
    if (ctx != nullptr) {
        ctx->SwapOut();
    }
}

void run(intptr_t) {
    runstr("call by SwitchToFiber");
}

void runptr(intptr_t) {
    runstr("call by swapFiber");
}

int main(int argc, char** argv) {
    //FiberSG sg;
    auto sg = createFiberSG();

    Context ctx(&run, 0, 1 * 1024 * 1024);
    ctx.SwapIn();

    auto ptr = createFiber(&runptr, 0, 1 * 1024 * 1024);
    swapFiber(ptr, 0);

    delFiberSG(sg);

    return 0;
}

#endif