#include <stdlib.h>
#include <errno.h>
#include "common.h"

// Destructor table for per-thread private data
static volatile _st_destructor_t _st_destructors[ST_KEYS_MAX];
static volatile int key_max = 0;

// Return a key to be used for thread specific data
int st_key_create(int* keyp, _st_destructor_t destructor) {
    if (key_max >= ST_KEYS_MAX) {
        errno = EAGAIN;
        return -1;
    }

    *keyp = key_max++;
    _st_destructors[*keyp] = destructor;
    return 0;
}

int st_key_getlimit(void) {
    return ST_KEYS_MAX;
}

int st_thread_setspecific(st_thread_t thread, int key, void* value) {
    if (thread == NULL || key < 0 || key >= key_max) {
        errno = EINVAL;
        return -1;
    }
    if (value != thread->private_data[key]) {
        // free up previously set non-NULL data value
        if (thread->private_data[key] && _st_destructors[key]) {
            (*_st_destructors[key])(thread->private_data[key]);
        }
        thread->private_data[key] = value;
    }
    return 0;
}

void* st_thread_getspecific(st_thread_t thread, int key) {
    if (thread == NULL || key < 0 || key >= key_max) {
        return NULL;
    }
    return thread->private_data[key];
}

// Free up all per-thread private data
void _st_thread_cleanup(volatile _st_thread_t* thread) {
    if (thread != NULL) {
        for (int key = 0; key < key_max; key++) {
            if (thread->private_data[key] && _st_destructors[key]) {
                (*_st_destructors[key])(thread->private_data[key]);
                thread->private_data[key] = NULL;
            }
        }
    }
}
