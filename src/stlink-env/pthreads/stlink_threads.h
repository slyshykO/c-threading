/**
  ******************************************************************************
  * @file           : stlink_threads.h
  * @brief          : portable thread create/join wrapper
  * @copyright      : Copyright (c) 2026 stlink-org. All rights reserved.
  * @author         : Andreas Michelis (a-michelis)
  * @date           : 2026-09-15
  * SPDX-License-Identifier: BSD-3-Clause
  *
  * This file is licensed under the BSD 3-Clause License.
  * See the LICENSE file in the project root for full license information.
  ******************************************************************************
  */

#ifndef STLINK_THREADS_H
#define STLINK_THREADS_H

#include <stdint.h>

/*
 * Minimal thread wrapper.
 *
 * The library only ever starts a handful of workers and waits for them, which
 * Win32 and pthreads both provide directly. Wrapping those two calls removes
 * the last reason for a Windows build to carry a pthreads implementation:
 * PThreads4W on MSVC, or winpthread on MinGW, the latter being a DLL that the
 * produced binaries would otherwise have to ship beside them.
 */

#if defined(_WIN32)
/* A HANDLE, held as void* so that windows.h stays out of this header. */
typedef void *stlink_thread_t;
#else
#include <pthread.h>
typedef pthread_t stlink_thread_t;
#endif

/*
 * A thread entry point. It deliberately returns nothing: Win32 and pthreads
 * disagree on both the return type and the calling convention, and nothing
 * here reads a thread's result.
 */
typedef void (*stlink_thread_fn)(void *arg);

/*
 * Both functions return 0 on success, otherwise a positive errno-style value
 * (never negative, and not through errno), the same on every platform:
 * ENOMEM, EAGAIN, EINVAL or, for a thread joining itself, EDEADLK, plus
 * whatever else pthreads reports on POSIX. Win32 errors are translated to
 * these. A join that fails leaves the thread joinable.
 */

/*
 * Lifetime rules. There is no detach: every thread that was created must be
 * joined exactly once, or its handle and resources are leaked. There is also no
 * invalid-thread value, because pthread_t may be a struct, so an
 * stlink_thread_t must not be zero-initialised or compared, and only holds
 * something meaningful after a successful create. The arg passed to create
 * must stay valid until the thread has finished with it.
 *
 * Stack size. There is no stack-size parameter, so a thread gets the platform
 * default, and that differs a lot: on Windows it is the executable's reserved
 * stack size, fixed at link time (1 MB by default with MSVC); glibc uses
 * RLIMIT_STACK, usually 8 MB; musl about 128 KB; macOS 512 KB for threads other
 * than the main one. Code that runs in a worker must fit the smallest of them,
 * so keep large buffers off the worker's stack and allocate them instead.
 */

/**
 * @brief Start a thread running fn(arg).
 *
 * thread and fn must not be NULL; neither is checked. *thread is written only
 * on success and is unspecified after a failure. The new thread may start
 * running before this function returns, so fn must not read *thread and expect
 * it to be set; publish the handle separately if the thread needs it.
 * fn must return normally to end the thread.
 * @return 0 on success, otherwise an errno-style code
 */
int32_t stlink_thread_create(stlink_thread_t *thread, stlink_thread_fn fn, void *arg);

/**
 * @brief Wait for a thread to finish, then release it.
 *
 * Call this once per successfully created thread, from any thread but the one
 * being joined (that fails with EDEADLK). A successful join releases the
 * handle, which must not be used again, and makes everything the thread wrote
 * visible to the caller. A failed join releases nothing, so the thread is still
 * joinable and the handle still valid.
 * @return 0 on success, otherwise an errno-style code
 */
int32_t stlink_thread_join(stlink_thread_t thread);

#endif // STLINK_THREADS_H