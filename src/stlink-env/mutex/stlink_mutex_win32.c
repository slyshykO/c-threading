/* SPDX-License-Identifier: BSD-3-Clause */
#include <stddef.h>

#include <windows.h>

#include "stlink_mutex.h"

/* TryAcquireSRWLockExclusive is declared only for Windows 7 and newer. */
#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0601)
#error "stlink_mutex_win32.c requires _WIN32_WINNT >= 0x0601 (Windows 7 or newer)"
#endif

/* stlink_mutex_t stores an SRWLOCK as a void*; the layouts must be identical. */
_Static_assert(sizeof(stlink_mutex_t) == sizeof(SRWLOCK),
               "stlink_mutex_t must have the size of SRWLOCK");
_Static_assert(_Alignof(stlink_mutex_t) == _Alignof(SRWLOCK),
               "stlink_mutex_t must have the alignment of SRWLOCK");
_Static_assert(offsetof(stlink_mutex_t, native) == 0,
               "stlink_mutex_t storage must start at offset 0");

static SRWLOCK *stlink_mutex_native(stlink_mutex_t *mutex) {
    return (SRWLOCK *)mutex;
}

int32_t stlink_mutex_init(stlink_mutex_t *mutex) {
    InitializeSRWLock(stlink_mutex_native(mutex));
    return 0;
}

int32_t stlink_mutex_destroy(stlink_mutex_t *mutex) {
    /* SRW locks own no resources requiring release. Lifetime rules still apply. */
    (void)mutex;
    return 0;
}

int32_t stlink_mutex_lock(stlink_mutex_t *mutex) {
    AcquireSRWLockExclusive(stlink_mutex_native(mutex));
    return 0;
}

int32_t stlink_mutex_trylock(stlink_mutex_t *mutex) {
    return TryAcquireSRWLockExclusive(stlink_mutex_native(mutex)) ? 0 : EBUSY;
}

int32_t stlink_mutex_unlock(stlink_mutex_t *mutex) {
    ReleaseSRWLockExclusive(stlink_mutex_native(mutex));
    return 0;
}
