/* SPDX-License-Identifier: BSD-3-Clause */
#include "stlink_mutex.h"

int32_t stlink_mutex_init(stlink_mutex_t *mutex) {
    InitializeSRWLock(&mutex->native);
    return 0;
}

int32_t stlink_mutex_destroy(stlink_mutex_t *mutex) {
    /* SRW locks own no resources requiring release. Lifetime rules still apply. */
    (void)mutex;
    return 0;
}

int32_t stlink_mutex_lock(stlink_mutex_t *mutex) {
    AcquireSRWLockExclusive(&mutex->native);
    return 0;
}

int32_t stlink_mutex_trylock(stlink_mutex_t *mutex) {
    return TryAcquireSRWLockExclusive(&mutex->native) ? 0 : EBUSY;
}

int32_t stlink_mutex_unlock(stlink_mutex_t *mutex) {
    ReleaseSRWLockExclusive(&mutex->native);
    return 0;
}
