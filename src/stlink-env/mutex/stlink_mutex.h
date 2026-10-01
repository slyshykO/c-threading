/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef STLINK_MUTEX_H
#define STLINK_MUTEX_H

#include <errno.h>
#include <stdint.h>

/* The backend owns the definition and storage; no native headers are needed. */
typedef struct stlink_mutex *stlink_mutex_t;

/*
 * Non-recursive, process-local mutex. Create before sharing. Handles may be
 * shared, but all refer to the same mutex, which must be destroyed exactly once.
 * Do not lock it recursively. Only the owning thread may unlock it.
 * All operations except create require a non-NULL, live mutex; this is unchecked.
 *
 * Operations return 0 on success, otherwise a positive errno-style code
 * (not via errno).
 * trylock returns EBUSY when unavailable, without waiting or taking ownership.
 * Misuse is not reliably diagnosed; Windows lock/unlock/destroy return 0.
 * Unlock followed by successful lock/trylock publishes protected writes.
 */

/*
 * Allocate and initialize an unlocked mutex. mutex must not be NULL (unchecked).
 * *mutex is written only on success and left unchanged on failure. Do not
 * overwrite the last handle to a live mutex, or it will be leaked.
 * Returns ENOMEM on allocation failure, or an initialization error on POSIX.
 */
int32_t stlink_mutex_create(stlink_mutex_t *mutex);

/*
 * Destroy and free only when unlocked and no thread can still access it.
 * Success invalidates every handle to the mutex; a failure frees nothing
 * and leaves the mutex live. Create a new mutex before using it again.
 */
int32_t stlink_mutex_destroy(stlink_mutex_t mutex);
int32_t stlink_mutex_lock(stlink_mutex_t mutex);
int32_t stlink_mutex_trylock(stlink_mutex_t mutex);
int32_t stlink_mutex_unlock(stlink_mutex_t mutex);

#endif /* STLINK_MUTEX_H */
