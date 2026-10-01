/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdlib.h>

#include <pthread.h>

#include "stlink_mutex.h"

struct stlink_mutex {
    pthread_mutex_t native;
};

int32_t stlink_mutex_create(stlink_mutex_t *mutex) {
    stlink_mutex_t created = malloc(sizeof(*created));
    if(created == NULL) { return ENOMEM; }

    int32_t error = (int32_t)pthread_mutex_init(&created->native, NULL);
    if(error != 0) {
        free(created);
        return error;
    }

    *mutex = created;
    return 0;
}

int32_t stlink_mutex_destroy(stlink_mutex_t mutex) {
    int32_t error = (int32_t)pthread_mutex_destroy(&mutex->native);
    if(error == 0) { free(mutex); }
    return error;
}

int32_t stlink_mutex_lock(stlink_mutex_t mutex) {
    return (int32_t)pthread_mutex_lock(&mutex->native);
}

int32_t stlink_mutex_trylock(stlink_mutex_t mutex) {
    return (int32_t)pthread_mutex_trylock(&mutex->native);
}

int32_t stlink_mutex_unlock(stlink_mutex_t mutex) {
    return (int32_t)pthread_mutex_unlock(&mutex->native);
}
