#include <stdlib.h>
#ifndef WIN32
#include <unistd.h>
#else
#include <winsock2.h>
#endif
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "common.h"

#include <cstl/cmap.h>
#include <cstl/cutility.h>

struct _st_seldata {
    int index;
    int maxfd;
    int size;
    fd_set fd_read_set, fd_write_set, fd_exception_set;
};

static struct _st_seldata** _st_select_data = NULL;
static int _st_select_num = 0;
static struct map_t* _st_select_fd_rmp = NULL;
static struct map_t* _st_select_fd_wmp = NULL;
static struct map_t* _st_select_fd_emp = NULL;

volatile _st_eventsys_t* _st_eventsys = NULL;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define _ST_SELECT_MAX_OSFD(g)  (g->maxfd)
#define _ST_SELECT_READ_SET(g)  (g->fd_read_set)
#define _ST_SELECT_WRITE_SET(g) (g->fd_write_set)
#define _ST_SELECT_EXCEP_SET(g) (g->fd_exception_set)

#define _ST_SELECT_ADD_CNT(map, fd, val) \
    void* pos = map_at(map, fd); \
    if (pos == NULL) { \
        pair_t* pair = create_pair(int, int); \
        pair_make(pair, fd, val); \
        map_insert(map, pair); \
    } else { \
        *(int*)pos += val; \
    }

#define _ST_SELECT_ADD_READ_CNT(fd)  _ST_SELECT_ADD_CNT(_st_select_fd_rmp, fd, 1)
#define _ST_SELECT_ADD_WRITE_CNT(fd) _ST_SELECT_ADD_CNT(_st_select_fd_wmp, fd, 1)
#define _ST_SELECT_ADD_EXCEP_CNT(fd) _ST_SELECT_ADD_CNT(_st_select_fd_emp, fd, 1)

ST_HIDDEN int _st_select_del_cnt(struct map_t* map, int fd, int val) {
    void* pos = map_at(map, fd);
    if (pos != NULL) {
        return (*(int*)pos -= val);
    }
    return 0;
}

#define _ST_SELECT_CUT_READ_CNT(fd)  _st_select_del_cnt(_st_select_fd_rmp, fd, 1)
#define _ST_SELECT_CUT_WRITE_CNT(fd) _st_select_del_cnt(_st_select_fd_wmp, fd, 1)
#define _ST_SELECT_CUT_EXCEP_CNT(fd) _st_select_del_cnt(_st_select_fd_emp, fd, 1)

ST_HIDDEN int _st_select_get_cnt(struct map_t* map, int fd) {
    void* pos = map_at(map, fd); 
    if (pos != NULL) {
        return *(int*)pos;
    }
    return 0;
}

#define _ST_SELECT_GET_READ_CNT(fd)  _st_select_get_cnt(_st_select_fd_rmp, fd)
#define _ST_SELECT_GET_WRITE_CNT(fd) _st_select_get_cnt(_st_select_fd_wmp, fd)
#define _ST_SELECT_GET_EXCEP_CNT(fd) _st_select_get_cnt(_st_select_fd_emp, fd)

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ST_HIDDEN struct _st_seldata* _st_seldata_new() {
    struct _st_seldata* dt = (struct _st_seldata*) malloc(sizeof(struct _st_seldata));
    if (dt != NULL) {
        memset(dt, 0, sizeof(struct _st_seldata));
    }
    return dt;
}

ST_HIDDEN int _st_select_get_gfd(int fd) {
    return (fd - 1) / FD_SETSIZE;
}

ST_HIDDEN void _st_select_check_seldata(int gfd) {
    if (gfd < _st_select_num) {
        return;
    }
    int size = sizeof(struct _st_seldata*) * (gfd + 1);
    struct _st_seldata** newdt = (struct _st_seldata**) malloc(size);
    memset(newdt, 0, size);
    if (_st_select_data != NULL) {
        memcpy(newdt, _st_select_data, sizeof(struct _st_seldata*) * _st_select_num);
        free(_st_select_data);
        _st_select_data = NULL;
    }
    for (int i = _st_select_num; i <= gfd; i++) {
        newdt[i] = _st_seldata_new();
        newdt[i]->index = i;
    }
    _st_select_data = newdt;
    _st_select_num = gfd + 1;
}

// select event system
ST_HIDDEN int _st_select_init(void) {
    _st_select_fd_rmp = create_map(int, int);
    _st_select_fd_wmp = create_map(int, int);
    _st_select_fd_emp = create_map(int, int);
    return 0;
}

ST_HIDDEN int _st_select_pollset_add(struct pollfd* pds, int npds) {
    struct pollfd* pd;
    struct pollfd* epd = pds + npds;
    struct _st_seldata* seldt = NULL;
    int ofd = 0, gfd = 0;
    

    for (pd = pds; pd < epd; pd++) {
        ofd = pd->fd;
        if (ofd <= 0 || pd->events == 0) {
            continue;
        }
        gfd = _st_select_get_gfd(ofd);
        _st_select_check_seldata(gfd);
        seldt = _st_select_data[gfd];
        if (pd->events & POLLIN) {
            FD_SET(ofd, &_ST_SELECT_READ_SET(seldt));
            _ST_SELECT_ADD_READ_CNT(ofd);
        }
        if (pd->events & POLLOUT) {
            FD_SET(ofd, &_ST_SELECT_WRITE_SET(seldt));
            _ST_SELECT_ADD_WRITE_CNT(ofd);
        }
        if (pd->events & POLLPRI) {
            FD_SET(ofd, &_ST_SELECT_EXCEP_SET(seldt));
            _ST_SELECT_ADD_EXCEP_CNT(ofd);
        }
        if (_ST_SELECT_MAX_OSFD(seldt) < ofd) {
            _ST_SELECT_MAX_OSFD(seldt) = ofd;
        }
        seldt->size++;
    }
    return 0;
}

ST_HIDDEN void _st_select_pollset_del(struct pollfd* pds, int npds) {
    struct pollfd* pd;
    struct pollfd* epd = pds + npds;
    struct _st_seldata* seldt = NULL;
    int ofd = 0, gfd = 0;

    for (pd = pds; pd < epd; pd++) {
        ofd = pd->fd;
        gfd = _st_select_get_gfd(ofd);
        if (ofd <= 0 || pd->events == 0 || gfd >= _st_select_num) {
            continue;
        }
        seldt = _st_select_data[gfd];
        if (pd->events & POLLIN) {
            if (_ST_SELECT_CUT_READ_CNT(ofd) == 0) {
                FD_CLR(ofd, &_ST_SELECT_READ_SET(seldt));
            }
        }
        if (pd->events & POLLOUT) {
            if (_ST_SELECT_CUT_WRITE_CNT(ofd) == 0) {
                FD_CLR(ofd, &_ST_SELECT_WRITE_SET(seldt));
            }
        }
        if (pd->events & POLLPRI) {
            if (_ST_SELECT_CUT_EXCEP_CNT(ofd) == 0) {
                FD_CLR(ofd, &_ST_SELECT_EXCEP_SET(seldt));
            }
        }
        seldt->size--;
    }
}

ST_HIDDEN void _st_select_find_bad_fd(void) {
    _st_clist_t* q;
    _st_pollq_t* pq;
    int notify;
    struct pollfd* pds, *epds;
    short events;
    struct _st_seldata* seldt = NULL;
    int osfd = 0, gfd = 0;

    for (int i = 0; i < _st_select_num; i++) {
        _st_select_data[i]->maxfd = 0;
    }

    for (q = _ST_IOQ.next; q != &_ST_IOQ; q = q->next) {
        pq = _ST_POLLQUEUE_PTR(q);
        notify = 0;
        epds = pq->pds + pq->npds;

        for (pds = pq->pds; pds < epds; pds++) {
            osfd = (int)pds->fd;
            pds->revents = 0;
            if (pds->events == 0) {
                continue;
            }
#ifndef WIN32
            if (fcntl(osfd, F_GETFL, 0) < 0) {
                pds->revents = POLLNVAL;
                notify = 1;
            }
#endif
        }

        if (notify) {
            ST_REMOVE_LINK(&pq->links);
            pq->on_ioq = 0;
            // Decrement the count of descriptors for each descriptor/event because this I/O request is being removed from the ioq
            for (pds = pq->pds; pds < epds; pds++) {
                osfd = (int)pds->fd;
                gfd = _st_select_get_gfd(osfd);
                _st_select_check_seldata(gfd);
                seldt = _st_select_data[gfd];

                if (osfd > seldt->maxfd) {
                    seldt->maxfd = osfd;
                }

                events = pds->events;
                if (events & POLLIN) {
                    if (_ST_SELECT_CUT_READ_CNT(osfd) == 0) {
                        FD_CLR(osfd, &_ST_SELECT_READ_SET(seldt));
                    }
                }
                if (events & POLLOUT) {
                    if (_ST_SELECT_CUT_WRITE_CNT(osfd) == 0) {
                        FD_CLR(osfd, &_ST_SELECT_WRITE_SET(seldt));
                    }
                }
                if (events & POLLPRI) {
                    if (_ST_SELECT_CUT_EXCEP_CNT(osfd) == 0) {
                        FD_CLR(osfd, &_ST_SELECT_EXCEP_SET(seldt));
                    }
                }
            }

            if (pq->thread->flags & _ST_FL_ON_SLEEPQ) {
                _ST_DEL_SLEEPQ(pq->thread);
            }
            pq->thread->state = _ST_ST_RUNNABLE;
            _ST_ADD_RUNQ(pq->thread);
        }
    }
}

ST_HIDDEN void _st_select_dispatch_once(struct _st_seldata* sdt) {
    if (sdt == NULL || sdt->size <= 0 || sdt->maxfd <= 0) {
        return;
    }

    struct timeval timeout, *tvp;
    fd_set* rp, *wp, *ep;
    int nfd, osfd, gfd;
    _st_clist_t* q;
    st_utime_t min_timeout;
    _st_pollq_t* pq;
    int notify, setr, setw, sete;
    struct pollfd* pds, *epds;
    short events, revents;
    struct _st_seldata* seldt = NULL;

    rp = &_ST_SELECT_READ_SET(sdt);
    wp = &_ST_SELECT_WRITE_SET(sdt);
    ep = &_ST_SELECT_EXCEP_SET(sdt);

    if (_ST_SLEEPQ == NULL) {
        tvp = NULL;
    } else {
        min_timeout = (_ST_SLEEPQ->due <= _ST_LAST_CLOCK) ? 0 : (_ST_SLEEPQ->due - _ST_LAST_CLOCK);
        timeout.tv_sec  = (int)(min_timeout / 1000000);
        timeout.tv_usec = (int)(min_timeout % 1000000);
        tvp = &timeout;
    }

    // Check for I/O operations
    nfd = select(_ST_SELECT_MAX_OSFD(sdt) + 1, rp, wp, ep, tvp);

    // Notify threads that are associated with the selected descriptors
    if (nfd > 0) {
        for (q = _ST_IOQ.next; q != &_ST_IOQ; q = q->next) {
            pq = _ST_POLLQUEUE_PTR(q);
            notify = 0;
            epds = pq->pds + pq->npds;

            for (pds = pq->pds; pds < epds; pds++) {
                osfd = (int)pds->fd;
                setr = FD_ISSET(osfd, rp);
                setw = FD_ISSET(osfd, wp);
                sete = FD_ISSET(osfd, ep);
                if (setr == 0 && setw == 0 && sete == 0) {
                    continue;
                }
                events = pds->events;
                revents = 0;
                if ((events & POLLIN) && setr > 0) {
                    revents |= POLLIN;
                }
                if ((events & POLLOUT) && setw > 0) {
                    revents |= POLLOUT;
                }
                if ((events & POLLPRI) && sete > 0) {
                    revents |= POLLPRI;
                }
                pds->revents = revents;
                if (revents) {
                    notify = 1;
                }
            }
            if (notify) {
                ST_REMOVE_LINK(&pq->links);
                pq->on_ioq = 0;
                // Decrement the count of descriptors for each descriptor/event because this I/O request is being removed from the ioq
                for (pds = pq->pds; pds < epds; pds++) {
                    osfd = (int)pds->fd;
                    gfd = _st_select_get_gfd(osfd);
                    if (gfd >= _st_select_num) {
                        continue;
                    }
                    seldt = _st_select_data[gfd];
                    events = pds->events;
                    if (events & POLLIN) {
                        if (_ST_SELECT_CUT_READ_CNT(osfd) == 0) {
                            FD_CLR(osfd, &_ST_SELECT_READ_SET(seldt));
                        }
                    }
                    if (events & POLLOUT) {
                        if (_ST_SELECT_CUT_WRITE_CNT(osfd) == 0) {
                            FD_CLR(osfd, &_ST_SELECT_WRITE_SET(seldt));
                        }
                    }
                    if (events & POLLPRI) {
                        if (_ST_SELECT_CUT_EXCEP_CNT(osfd) == 0) {
                            FD_CLR(osfd, &_ST_SELECT_EXCEP_SET(seldt));
                        }
                    }
                }
                if (pq->thread->flags & _ST_FL_ON_SLEEPQ) {
                    _ST_DEL_SLEEPQ(pq->thread);
                }
                pq->thread->state = _ST_ST_RUNNABLE;
                _ST_ADD_RUNQ(pq->thread);
            }
        }
    } else if (nfd < 0) {
        // It can happen when a thread closes file descriptor that is being used by some other thread -- BAD!
        if (errno == EBADF) {
            _st_select_find_bad_fd();
        }
    }
}

ST_HIDDEN void _st_select_dispatch(void) {
    if (_st_select_data != NULL) {
        for (int i = 0; i < _st_select_num; i++) {
            _st_select_dispatch_once(_st_select_data[i]);
        }
    }
}

ST_HIDDEN int _st_select_fd_new(int osfd) {
    return 0;
}

ST_HIDDEN int _st_select_fd_close(int osfd) {
    int gfd = _st_select_get_gfd(osfd);
    if (gfd >= _st_select_num) {
        return 0;
    }
    if (_ST_SELECT_GET_READ_CNT(osfd) > 0 || _ST_SELECT_GET_WRITE_CNT(osfd) > 0 || _ST_SELECT_GET_EXCEP_CNT(osfd) > 0) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

ST_HIDDEN int _st_select_fd_getlimit(void) {
    return FD_SETSIZE;
}

static _st_eventsys_t _st_select_eventsys = {
    "select",                   // name
    ST_EVENTSYS_SELECT,         // val
    _st_select_init,            // init
    _st_select_dispatch,        // dispatch
    _st_select_pollset_add,     // pollset_add
    _st_select_pollset_del,     // pollset_del
    _st_select_fd_new,          // fd_new
    _st_select_fd_close,        // fd_close
    _st_select_fd_getlimit      // fd_getlimit
};


// Public functions
int st_set_eventsys(int eventsys) {
    if (_st_eventsys) {
        errno = EBUSY;
        return -1;
    }

    switch (eventsys) {
        case ST_EVENTSYS_DEFAULT:
        case ST_EVENTSYS_SELECT:
        case ST_EVENTSYS_POLL:
        case ST_EVENTSYS_ALT:
            _st_eventsys = &_st_select_eventsys;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    return 0;
}

int st_get_eventsys(void) {
    return _st_eventsys ? _st_eventsys->val : -1;
}

const char* st_get_eventsys_name(void) {
    return _st_eventsys ? _st_eventsys->name : "";
}
