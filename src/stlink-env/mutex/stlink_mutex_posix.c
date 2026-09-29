/* SPDX-License-Identifier: BSD-3-Clause */
#include "stlink_mutex.h"

int32_t stlink_mutex_init(stlink_mutex_t *mutex) {
    return (int32_t)pthread_mutex_init(&mutex->native, NULL);
}

int32_t stlink_mutex_destroy(stlink_mutex_t *mutex) {
    return (int32_t)pthread_mutex_destroy(&mutex->native);
}

int32_t stlink_mutex_lock(stlink_mutex_t *mutex) {
    return (int32_t)pthread_mutex_lock(&mutex->native);
}

int32_t stlink_mutex_trylock(stlink_mutex_t *mutex) {
    return (int32_t)pthread_mutex_trylock(&mutex->native);
}

int32_t stlink_mutex_unlock(stlink_mutex_t *mutex) {
    return (int32_t)pthread_mutex_unlock(&mutex->native);
}
