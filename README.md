# ST-Link threading and atomics experiment

`src/stlink-env/pthreads/` contains an unchanged copy of `stlink_threads.h`,
`stlink_threads_win32.c` and `stlink_threads_posix.c` from
`D:/Projects/stlink-origin/src/stlink-env/pthreads/`. The upstream BSD license
is in `LICENSE`. The original ST-Link checkout is not modified.

CMake selects `_beginthreadex`/Win32 on Windows and pthreads on POSIX systems.
Windows builds do not use `<threads.h>` or link a pthread library.

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

The test checks operation return values, both compare-exchange outcomes,
signed wraparound, four workers producing 400,000 increments through both
fetch-add and compare-exchange, and publication of ordinary data through an
atomic ready flag. Checks remain enabled in Release builds. CTest imposes a
30-second timeout.

Verified with warnings treated as errors in optimized builds:

| Platform | Compiler | Thread / atomic backend |
| --- | --- | --- |
| Windows x64 and x86 | MSYS2 GCC 16.2 | Win32 / C11 |
| Windows x64 | MSYS2 Clang 22.1 | Win32 / C11 |
| Windows x64 | MSVC 19.51 | Win32 / Interlocked |
| Linux x86_64 (WSL Ubuntu) | GCC 13.3 | POSIX / C11 |

Backend references: [C11 atomics in GCC](https://gcc.gnu.org/projects/c-status.html#c11),
[Microsoft Interlocked operations](https://learn.microsoft.com/en-us/windows/win32/sync/interlocked-variable-access).
