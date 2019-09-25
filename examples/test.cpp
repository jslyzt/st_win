#if 0

#include <stdlib.h>
#include <setjmp.h>
#include <iostream>

jmp_buf context_level_0;

void func_level_0() {
    const char* level_0_0 = "stack variables for func_level_0";
    int ret = setjmp(context_level_0);
    std::cout << "func_level_0 ret=" << ret << std::endl;
    if (ret != 0) {
        std::cout << "call by longjmp." << std::endl;
        exit(0);
    }
}

int main(int argc, char** argv) {
    func_level_0();
    longjmp(context_level_0, 1);
    return 0;
}

#endif