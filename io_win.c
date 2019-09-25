#include <stdlib.h>
#ifdef WIN32
#include <io.h>
#include <WinSock2.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "common.h"

#if EAGAIN != EWOULDBLOCK
#define _IO_NOT_READY_ERROR  ((errno == EAGAIN) || (errno == EWOULDBLOCK))
#else
#define _IO_NOT_READY_ERROR  (errno == EAGAIN)
#endif
#define _LOCAL_MAXIOV  16

// Winsock Data
static WSADATA wsadata;

// File descriptor object free list
static _st_netfd_t* _st_netfd_freelist = NULL;

// Maximum number of file descriptors that the process can open
static int _st_osfd_limit = -1;

static void _st_netfd_free_aux_data(_st_netfd_t* fd);

int st_errno(void) {
    return (errno);
}

// _st_GetError xlate winsock errors to unix
int _st_GetError(int err) {
    int syserr;

    if (err == 0) syserr = GetLastError();
    SetLastError(0);
    if (syserr < WSABASEERR) return (syserr);
    switch (syserr) {
        case WSAEINTR:
            syserr = EINTR;
            break;
        case WSAEBADF:
            syserr = EBADF;
            break;
        case WSAEACCES:
            syserr = EACCES;
            break;
        case WSAEFAULT:
            syserr = EFAULT;
            break;
        case WSAEINVAL:
            syserr = EINVAL;
            break;
        case WSAEMFILE:
            syserr = EMFILE;
            break;
        case WSAEWOULDBLOCK:
            syserr = EAGAIN;
            break;
        case WSAEINPROGRESS:
            syserr = EINTR;
            break;
        case WSAEALREADY:
            syserr = EINTR;
            break;
        case WSAENOTSOCK:
            syserr = ENOTSOCK;
            break;
        case WSAEDESTADDRREQ:
            syserr = EDESTADDRREQ;
            break;
        case WSAEMSGSIZE:
            syserr = EMSGSIZE;
            break;
        case WSAEPROTOTYPE:
            syserr = EPROTOTYPE;
            break;
        case WSAENOPROTOOPT:
            syserr = ENOPROTOOPT;
            break;
        case WSAEOPNOTSUPP:
            syserr = EOPNOTSUPP;
            break;
        case WSAEADDRINUSE:
            syserr = EADDRINUSE;
            break;
        case WSAEADDRNOTAVAIL:
            syserr = EADDRNOTAVAIL;
            break;
        case WSAECONNABORTED:
            syserr = ECONNABORTED;
            break;
        case WSAECONNRESET:
            syserr = ECONNRESET;
            break;
        case WSAEISCONN:
            syserr = EISCONN;
            break;
        case WSAENOTCONN:
            syserr = ENOTCONN;
            break;
        case WSAETIMEDOUT:
            syserr = ETIMEDOUT;
            break;
        case WSAECONNREFUSED:
            syserr = ECONNREFUSED;
            break;
        case WSAEHOSTUNREACH:
            syserr = EHOSTUNREACH;
            break;
    }
    return (syserr);
}

int getpagesize(void) {
    SYSTEM_INFO sysinf;
    GetSystemInfo(&sysinf);
    return (int)sysinf.dwPageSize;
}

int _st_io_init(void) {
    _st_osfd_limit = FD_SETSIZE;
    WSAStartup(2, &wsadata);
    return 0;
}

int st_getfdlimit(void) {
    return _st_osfd_limit;
}

void st_netfd_free(_st_netfd_t* fd) {
    if (!fd->inuse) {
        return;
    }
    fd->inuse = 0;
    if (fd->aux_data) {
        _st_netfd_free_aux_data(fd);
    }
    if (fd->private_data && fd->destructor) {
        (*(fd->destructor))(fd->private_data);
    }
    fd->private_data = NULL;
    fd->destructor = NULL;
    fd->next = _st_netfd_freelist;
    _st_netfd_freelist = fd;
}

static _st_netfd_t* _st_netfd_new(int osfd, int nonblock, int is_socket) {
    _st_netfd_t* fd;
    if ((*_st_eventsys->fd_new)(osfd) < 0) {
        return NULL;
    }
    if (_st_netfd_freelist) {
        fd = _st_netfd_freelist;
        _st_netfd_freelist = _st_netfd_freelist->next;
    } else {
        fd = calloc(1, sizeof(_st_netfd_t));
        if (!fd) {
            return NULL;
        }
    }

    fd->osfd = osfd;
    fd->inuse = 1;
    fd->next = NULL;

    if (nonblock) {
        int flags = 1;
        ioctlsocket(osfd, FIONBIO, &flags);
    }
    return fd;
}

_st_netfd_t* st_netfd_open(int osfd) {
    return _st_netfd_new(osfd, 1, 0);
}

_st_netfd_t* st_netfd_open_socket(int osfd) {
    return _st_netfd_new(osfd, 1, 1);
}

int st_netfd_close(_st_netfd_t* fd) {
    if ((*_st_eventsys->fd_close)(fd->osfd) < 0) {
        return -1;
    }
    st_netfd_free(fd);
    _close(fd->osfd);
    errno = _st_GetError(0);
    return errno;
}

int st_netfd_fileno(_st_netfd_t* fd) {
    return (fd->osfd);
}

void st_netfd_setspecific(_st_netfd_t* fd, void* value, _st_destructor_t destructor) {
    if (value != fd->private_data) {
        // Free up previously set non-NULL data value
        if (fd->private_data && fd->destructor) {
            (*(fd->destructor))(fd->private_data);
        }
    }
    fd->private_data = value;
    fd->destructor = destructor;
}

void* st_netfd_getspecific(_st_netfd_t* fd) {
    return (fd->private_data);
}


// Wait for I/O on a single descriptor.
int st_netfd_poll(_st_netfd_t* fd, int how, st_utime_t timeout) {
    struct pollfd pd;
    int n;

    pd.fd = fd->osfd;
    pd.events = (short) how;
    pd.revents = 0;

    if ((n = st_poll(&pd, 1, timeout)) < 0) {
        return -1;
    }
    if (n == 0) {
        // Timed out
        errno = ETIME;
        return -1;
    }
    if (pd.revents & POLLNVAL) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int st_netfd_serialize_accept(_st_netfd_t* fd) {
    fd->aux_data = NULL;
    return 0;
}

static void _st_netfd_free_aux_data(_st_netfd_t* fd) {
    fd->aux_data = NULL;
}

_st_netfd_t* st_accept(_st_netfd_t* fd, struct sockaddr* addr, int* addrlen, st_utime_t timeout) {
    SOCKET osfd;
    _st_netfd_t* newfd;

    while ((osfd = accept(fd->osfd, addr, addrlen)) < 0) {
        errno = _st_GetError(0);
        if (errno == EINTR) {
            continue;
        }
        if (!_IO_NOT_READY_ERROR) {
            return NULL;
        }
        // Wait until the socket becomes readable
        if (st_netfd_poll(fd, POLLIN, timeout) < 0) {
            return NULL;
        }
    }

    // On some platforms the new socket created by accept() inherits the nonblocking attribute of the listening socket
#if defined (MD_ACCEPT_NB_INHERITED)
    newfd = _st_netfd_new((int)osfd, 0, 1);
#elif defined (MD_ACCEPT_NB_NOT_INHERITED)
    newfd = _st_netfd_new((int)osfd, 1, 1);
#else
#error Unknown OS
#endif

    if (!newfd) {
        _close((int)osfd);
    }
    return newfd;
}

int st_connect(_st_netfd_t* fd, const struct sockaddr* addr, int addrlen, st_utime_t timeout) {
    int n, err = 0;

    while (connect(fd->osfd, addr, addrlen) < 0) {
        errno = _st_GetError(0);
        if (errno != EINTR) {
            if (errno != EAGAIN && errno != EINTR) {
                return -1;
            }
            // Wait until the socket becomes writable
            if (st_netfd_poll(fd, POLLOUT, timeout) < 0) {
                return -1;
            }
            // Try to find out whether the connection setup succeeded or failed
            n = sizeof(int);
            if (getsockopt(fd->osfd, SOL_SOCKET, SO_ERROR, (char*)&err, (socklen_t*)&n) < 0) {
                return -1;
            }
            if (err) {
                errno = _st_GetError(err);
                return -1;
            }
            break;
        }
        err = 1;
    }
    return 0;
}

ssize_t st_read(_st_netfd_t* fd, void* buf, size_t nbyte, st_utime_t timeout) {
    ssize_t n;
    while ((n = recv(fd->osfd, buf, (int)nbyte, 0)) < 0) {
        errno = _st_GetError(0);
        if (errno == EINTR) {
            continue;
        }
        if (!_IO_NOT_READY_ERROR) {
            return (-1);
        }
        // Wait until the socket becomes readable
        if (st_netfd_poll(fd, POLLIN, timeout) < 0) {
            return -1;
        }
    }
    return n;
}

ssize_t st_read_fully(_st_netfd_t* fd, void* buf, size_t nbyte, st_utime_t timeout) {
    ssize_t n;
    size_t nleft = nbyte;

    while (nleft > 0) {
        if ((n = recv(fd->osfd, buf, (int)nleft, 0)) < 0) {
            errno = _st_GetError(0);
            if (errno == EINTR) {
                continue;
            }
            if (!_IO_NOT_READY_ERROR) {
                return -1;
            }
        } else {
            nleft -= n;
            if (nleft == 0 || n == 0) {
                break;
            }
            buf = (void*)((char*)buf + n);
        }
        // Wait until the socket becomes readable
        if (st_netfd_poll(fd, POLLIN, timeout) < 0) {
            return -1;
        }
    }
    return (ssize_t)(nbyte - nleft);
}

ssize_t st_write(_st_netfd_t* fd, const void* buf, size_t nbyte, st_utime_t timeout) {
    ssize_t n;
    size_t nleft = nbyte;

    while (nleft > 0) {
        if ((n = send(fd->osfd, buf, (int)nleft, 0)) < 0) {
            errno = _st_GetError(0);
            if (errno == EINTR) {
                continue;
            }
            if (!_IO_NOT_READY_ERROR) {
                return -1;
            }
        } else {
            if (n == nleft) {
                break;
            }
            nleft -= n;
            buf = (const void*)((const char*)buf + n);
        }
        // Wait until the socket becomes writable
        if (st_netfd_poll(fd, POLLOUT, timeout) < 0) {
            return -1;
        }
    }
    return (ssize_t)nbyte;
}

// Simple I/O functions for UDP.
int st_recvfrom(_st_netfd_t* fd, void* buf, int len, struct sockaddr* from, int* fromlen, st_utime_t timeout) {
    int n;
    while ((n = recvfrom(fd->osfd, buf, len, 0, from, (socklen_t*)fromlen)) < 0) {
        errno = _st_GetError(0);
        if (errno == EINTR) {
            continue;
        }
        if (!_IO_NOT_READY_ERROR) {
            return -1;
        }
        // Wait until the socket becomes readable
        if (st_netfd_poll(fd, POLLIN, timeout) < 0) {
            return -1;
        }
    }
    return n;
}

int st_sendto(_st_netfd_t* fd, const void* msg, int len, const struct sockaddr* to, int tolen, st_utime_t timeout) {
    int n;
    while ((n = sendto(fd->osfd, msg, len, 0, to, tolen)) < 0) {
        errno = _st_GetError(0);
        if (errno == EINTR) {
            continue;
        }
        if (!_IO_NOT_READY_ERROR) {
            return -1;
        }
        // Wait until the socket becomes writable
        if (st_netfd_poll(fd, POLLOUT, timeout) < 0) {
            return -1;
        }
    }
    return n;
}

// To open FIFOs or other special files.
_st_netfd_t* st_open(const char* path, int oflags, mode_t mode) {
    int osfd, err;
    _st_netfd_t* newfd;
    while ((osfd = _open(path, oflags, mode)) < 0) {
        errno = _st_GetError(0);
        if (errno != EINTR) {
            return NULL;
        }
    }
    newfd = _st_netfd_new(osfd, 0, 0);
    if (!newfd) {
        errno = _st_GetError(0);
        err = errno;
        _close(osfd);
        errno = err;
    }
    return newfd;
}
