# ST-Link threading, atomics and mutex experiment

`src/stlink-env/pthreads/` started as a copy of `stlink_threads.h`,
`stlink_threads_win32.c` and `stlink_threads_posix.c` from
`D:/Projects/stlink-origin/src/stlink-env/pthreads/` and has since been changed
here; `REVIEW.md` lists what changed and why. The upstream BSD license
is in `LICENSE`. The original ST-Link checkout is not modified.

The project is written in C17 (`CMAKE_C_STANDARD 17`, compiler extensions off).

CMake selects `_beginthreadex`/Win32 on Windows and pthreads on POSIX systems.
Windows builds do not use `<threads.h>` or link a pthread library.

## Threads

`stlink_threads.h` offers `stlink_thread_create` and `stlink_thread_join`. Both
return zero on success, otherwise a positive errno-style value on every
platform (`ENOMEM`, `EAGAIN`, `EINVAL`, and `EDEADLK` for a thread joining
itself; POSIX may report more). They never return a negative value and do not
use `errno` to report. Win32 errors are translated to these codes.

Every created thread must be joined exactly once; there is no detach. A failed
join leaves the thread joinable. `*thread` is written only on success, and the
new thread may run before `stlink_thread_create` returns, so a worker must not
read its own handle from `*thread`. A thread entry point returns `void`.

There is no stack-size parameter, so a worker gets the platform default: the
executable's linked stack size on Windows (1 MB by default with MSVC), usually
8 MB with glibc, about 128 KB with musl and 512 KB on macOS. Keep large buffers
off a worker's stack.

## Atomics

`src/stlink-env/atomic/stlink_atomic.h` adds a header-only C API for signed
32-bit counters and flags. GCC and Clang use C11 `<stdatomic.h>`; MSVC-compatible
targets use Interlocked intrinsics without `/experimental:c11atomics`.

```c
#include "stlink_atomic.h"

static stlink_atomic_int_t counter = STLINK_ATOMIC_INT_INIT(0);
static stlink_atomic_int_t stop = STLINK_ATOMIC_INT_INIT(0);

void worker(void *arg) {
    (void)arg;
    while(!stlink_atomic_load(&stop)) {
        stlink_atomic_fetch_add(&counter, 1);
    }
}

void request_stop(void) {
    stlink_atomic_store(&stop, 1);
}
```

The API provides init, load, store, exchange, fetch_add, fetch_sub and
compare_exchange (all with the `stlink_atomic_` prefix).
Exchange/add/subtract return the previous value. Compare-exchange is strong:
on failure it updates `*expected` to the observed value. Add/subtract wrap at
32 bits. All concurrent operations use sequential consistency, so a flag can
also publish preceding ordinary writes to a thread that observes the flag.

Initialize before sharing, use only these functions to access live atomic
objects, and do not copy or pack them. This small API does not cover atomic
pointers, 64-bit values, arbitrary types or selectable memory orders. It is
intended for C, not C++.

### Why the atomic type is a struct

`stlink_atomic_int_t` is a type-safety wrapper around an aligned `volatile long`
on MSVC or an `_Atomic(int32_t)` on the C11 backend. The struct itself does not
provide atomicity; the backend operations do.

The wrapper makes accidental scalar operations such as `counter++` and
`counter = 10` compile errors, encouraging callers to use
`stlink_atomic_fetch_add(&counter, 1)` and `stlink_atomic_store(&counter, 10)`.
This matters especially on MSVC: with a plain `volatile long` typedef,
`counter++` would compile but would not be an atomic increment. `volatile`
alone does not provide atomicity.

A scalar typedef could work if callers consistently used the API, but the
struct adds these guardrails without extra runtime operations. It does not
fully enforce the rules: callers can still access `.value` or copy the whole
struct, so live objects must be accessed only through the API and must not be
copied.

## Mutexes

`src/stlink-env/mutex/stlink_mutex.h` provides a non-recursive, process-local
mutex API. Windows uses an exclusive SRW lock (Windows 7 or newer for try-lock);
POSIX uses `pthread_mutex_t`. Windows needs no pthread library. The mutex
handle is opaque: the header declares
`typedef struct stlink_mutex *stlink_mutex_t`, and each backend defines the
struct with its actual native lock. The public mutex header includes neither
`<windows.h>` nor `<pthread.h>`.
The Windows source requires `_WIN32_WINNT` of at least `0x0601`.

```c
#include <stddef.h>
#include "stlink_mutex.h"

static stlink_mutex_t mutex;
static int counter;

/* Call before starting workers; check the result before using the mutex. */
int32_t counter_create(void) {
    return stlink_mutex_create(&mutex);
}

int32_t increment(void) {
    int32_t error = stlink_mutex_lock(mutex);
    if(error != 0) { return error; }
    ++counter;
    return stlink_mutex_unlock(mutex);
}

/* Call once, after all workers have finished. */
int32_t counter_destroy(void) {
    int32_t error = stlink_mutex_destroy(mutex);
    if(error == 0) { mutex = NULL; }
    return error;
}
```

Declare `stlink_mutex_t mutex` and check `stlink_mutex_create(&mutex)` before
sharing it, matching the thread handle API. Creation allocates an unlocked
mutex, writes the handle only on success, and leaves it unchanged on failure.
It returns `ENOMEM` if allocation fails, or an initialization error from
pthreads. The output argument must not be NULL. Other operations take the
handle by value and require a non-NULL, live mutex; these preconditions are
unchecked.

This replaces the former `stlink_mutex_init` and `STLINK_MUTEX_INIT` API;
there is no static mutex initializer. A static handle still needs an explicit
create call. All five functions (`create`, `destroy`, `lock`, `trylock`,
`unlock`, with the `stlink_mutex_` prefix) return zero on success or a positive
errno-style code directly, not via `errno`.
`stlink_mutex_trylock` returns `EBUSY` if unavailable; it never waits.
Only a successful lock or try-lock grants ownership.

Only the owner may unlock. Do not acquire recursively. Handles can be copied
and shared; they all refer to the same mutex and do not duplicate it or extend
its lifetime. Protect all concurrent accesses to shared ordinary data with the
same mutex; unlocking publishes writes to a subsequent successful acquisition.
Call `stlink_mutex_destroy` only after the mutex is unlocked and all users
have finished (usually after joining workers). A successful destroy frees the
allocation on both backends and invalidates every handle to it. A failed
POSIX destroy frees nothing and leaves the mutex live. Destroy each created
mutex exactly once; create a new one before reusing the handle variable.
Misuse is not reliably detected on either backend.
No timed, recursive, or inter-process mutex operations are provided.

Backend references: [Windows SRW locks](https://learn.microsoft.com/en-us/windows/win32/sync/slim-reader-writer--srw--locks),
[POSIX mutex operations](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_lock.html).

## Build and test

From a shell with the desired compiler on PATH (an MSYS2 shell, a Visual Studio
Developer Command Prompt, or a Unix shell):

```sh
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

Use a different build directory for each compiler. Set `-DCMAKE_C_COMPILER=gcc`,
`clang` or `cl` at the first configure to choose explicitly. The executable is
also copied into `release/`. `-DRELEASE_DIR=<directory>` selects a separate
output directory, which is created automatically.

MSVC builds use `/W4` for both targets. CMake removes existing `/w*` and
`/W*` options (including `/WX` and per-warning overrides) from `CMAKE_C_FLAGS`
and its configuration variants in the project scope, leaving cached values
unchanged. Both targets disable `COMPILE_WARNING_AS_ERROR` so it cannot add
`/WX` back. Other compilers retain their configured warning options.

The test checks operation return values, both compare-exchange outcomes,
signed wraparound, four workers producing 400,000 increments through both
fetch-add and compare-exchange, and publication of ordinary data through an
atomic ready flag. Mutex checks cover creation, recreation after destruction,
1,000 create/destroy cycles with independent mutexes, contended and successful
try-lock (including from a second thread), and 400,000 protected increments
with a two-field invariant and data publication through the lock. Thread checks
cover a refused self-join
(`EDEADLK`) and 1,000 create/join cycles, which on Windows also verify that the
process handle count does not grow and that joining a non-thread handle gives
`EINVAL`. Waiting workers yield the CPU while they spin. Allocation,
mutex-initialization/destruction, and thread-creation failure paths cannot be
forced by these regression checks. Checks remain enabled in Release builds.
CTest imposes a 30-second timeout.

Verified with warnings treated as errors in optimized builds:

| Platform | Compiler | Thread / atomic backend |
| --- | --- | --- |
| Windows x64 and x86 | MSYS2 GCC 16.2 | Win32 / C11 |
| Windows x64 | MSYS2 Clang 22.1 | Win32 / C11 |
| Windows x64 | MSVC 19.51 | Win32 / Interlocked |
| Linux x86_64 (WSL Ubuntu) | GCC 13.3 | POSIX / C11 |

Backend references: [C11 atomics in GCC](https://gcc.gnu.org/projects/c-status.html#c11),
[Microsoft Interlocked operations](https://learn.microsoft.com/en-us/windows/win32/sync/interlocked-variable-access).
