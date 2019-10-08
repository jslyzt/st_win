#if 1

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#ifndef WIN32
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#else
#include <WinSock2.h>
#endif
#include "st.h"
#include "common.h"
#include "fiber/fiber.h"
#include <iostream>

void* do_resolve(void* ptr) {
    auto str = static_cast<const char*>(ptr);
    if (str != nullptr) {
        std::cout << "resolve: " << str << std::endl;
    }
    return nullptr;
}

/*
 * Asynchronous DNS host name resolution. This program creates one
 * ST thread for each host name (specified as command line arguments).
 * All threads do host name resolution concurrently.
 */
int main(int argc, char* argv[]) {
    int i;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s arg1 arg2 ...\n", argv[0]);
        exit(1);
    }

    if (st_init() < 0) {
        perror("st_init");
        exit(1);
    }

    auto sg = createFiberSG();

    for (i = 1; i < argc; i++) {
        // Create a separate thread for each host name
        if (st_thread_create(do_resolve, argv[i], 0, 0) == nullptr) {
            perror("st_thread_create");
            exit(1);
        }
    }

    while (st_active_count() > 0) {
        st_usleep(1);
    }

    delFiberSG(sg);

    return 1;
}

#endif