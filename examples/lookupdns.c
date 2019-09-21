#if 0

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


/* Resolution timeout (in microseconds) */
#define TIMEOUT (2*1000000LL)

/* External function defined in the res.c file */
int dns_getaddr(const char* host, struct in_addr* addr, st_utime_t timeout) {
    return 0;
}


void* do_resolve(void* host) {
    struct in_addr addr;

    /* Use dns_getaddr() instead of gethostbyname(3) to get IP address */
    if (dns_getaddr(host, &addr, TIMEOUT) < 0) {
        fprintf(stderr, "dns_getaddr: can't resolve %s: ", (char*)host);
        perror("");
    } else
        printf("%-40s %s\n", (char*)host, inet_ntoa(addr));

    return NULL;
}


/*
 * Asynchronous DNS host name resolution. This program creates one
 * ST thread for each host name (specified as command line arguments).
 * All threads do host name resolution concurrently.
 */
int main(int argc, char* argv[]) {
    int i;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <hostname1> [<hostname2>] ...\n", argv[0]);
        exit(1);
    }

    if (st_init() < 0) {
        perror("st_init");
        exit(1);
    }

    for (i = 1; i < argc; i++) {
        /* Create a separate thread for each host name */
        if (st_thread_create(do_resolve, argv[i], 0, 0) == NULL) {
            perror("st_thread_create");
            exit(1);
        }
    }

    st_thread_exit(NULL);

    /* NOTREACHED */
    return 1;
}

#endif