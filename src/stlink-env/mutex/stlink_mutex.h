/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef STLINK_MUTEX_H
#define STLINK_MUTEX_H

#include <errno.h>
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
typedef struct {
    SRWLOCK native;
} stlink_mutex_t;
#define STLINK_MUTEX_INIT { SRWLOCK_INIT }
#else
#include <pthread.h>
typedef struct {
    pthread_mutex_t native;
} stlink_mutex_t;
#define STLINK_MUTEX_INIT { PTHREAD_MUTEX_INITIALIZER }
#endif

/*
 * Non-recursive, process-local mutex. Initialize with STLINK_MUTEX_INIT or
 * stlink_mutex_init before sharing, never both on an already-live object.
 * Do not copy, move, or pack a live mutex. Do not lock it recursively.
 * Only the owning thread may unlock it. Destroy only when unlocked and no
 * thread can still access it; initialize again before reusing destroyed storage.
 *
 * Operations return 0 on success, otherwise an error code (not via errno).
 * trylock returns EBUSY when unavailable, without waiting or taking ownership.
 * Misuse is not reliably diagnosed; Windows lock/unlock/destroy return 0.
 * Unlock followed by successful lock/trylock publishes protected writes.
 */
int32_t stlink_mutex_init(stlink_mutex_t *mutex);
int32_t stlink_mutex_destroy(stlink_mutex_t *mutex);
int32_t stlink_mutex_lock(stlink_mutex_t *mutex);
int32_t stlink_mutex_trylock(stlink_mutex_t *mutex);
int32_t stlink_mutex_unlock(stlink_mutex_t *mutex);

#endif /* STLINK_MUTEX_H */
