/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdlib.h>

#include <windows.h>

#include "stlink_mutex.h"

/* TryAcquireSRWLockExclusive is declared only for Windows 7 and newer. */
#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0601)
#error "stlink_mutex_win32.c requires _WIN32_WINNT >= 0x0601 (Windows 7 or newer)"
#endif

struct stlink_mutex {
    SRWLOCK native;
};

int32_t stlink_mutex_create(stlink_mutex_t *mutex) {
    stlink_mutex_t created = malloc(sizeof(*created));
    if(created == NULL) { return ENOMEM; }

    InitializeSRWLock(&created->native);
    *mutex = created;
    return 0;
}

int32_t stlink_mutex_destroy(stlink_mutex_t mutex) {
    /* SRW locks need no native cleanup, but the wrapper owns heap storage. */
    free(mutex);
    return 0;
}

int32_t stlink_mutex_lock(stlink_mutex_t mutex) {
    AcquireSRWLockExclusive(&mutex->native);
    return 0;
}

int32_t stlink_mutex_trylock(stlink_mutex_t mutex) {
    return TryAcquireSRWLockExclusive(&mutex->native) ? 0 : EBUSY;
}

int32_t stlink_mutex_unlock(stlink_mutex_t mutex) {
    ReleaseSRWLockExclusive(&mutex->native);
    return 0;
}
