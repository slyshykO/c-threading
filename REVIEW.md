# Cross-platform Threading Review

Scope: `src/stlink-env/pthreads/*`, `src/stlink-env/mutex/*`, `src/stlink-env/atomic/stlink_atomic.h`,
`src/main.c`, backend selection in `CMakeLists.txt`.

Method: static read of the code only. Nothing was built or run for this review.
Reviewed at commit `f2dcba9` (branch `main`).

## Verdict

The design is sound. Win32 and POSIX thread backends share the same structure, the heap-allocated
context is the right way to bridge the different entry-point signatures, `_beginthreadex` is correct
for CRT use, and `CMakeLists.txt` and the headers agree on the platform split (`WIN32` / `_WIN32`).
The problems are mostly inconsistent contracts between the two platforms, not logic bugs.

## Status legend

- `open` - not yet addressed
- `fixed` - fixed; see "Fix" line for what was changed and how
- `wontfix` - deliberately not changed; see "Fix" line for the reason

## Findings

### 1. Error codes come from different domains on Windows - `fixed`

- `stlink_thread_create` (`stlink_threads_win32.c:58`) returns `errno`; `stlink_thread_join`
  (`stlink_threads_win32.c:70`) returns `GetLastError()`. The header only says "platform error code",
  so callers cannot tell which domain a value belongs to.
- Allocation failure returns a bare `-1` on both platforms (`stlink_threads_posix.c:42`,
  `stlink_threads_win32.c:48`). That is not a real error code. Use `ENOMEM` (the POSIX file needs
  `<errno.h>`).
- Proposed: map join failures to errno-style values, or document the domains explicitly.

Fix: both backends now return 0 or a positive errno-style value, matching the mutex API, which
already returns errno values such as `EBUSY`. POSIX needed no translation, since pthreads already
returns errno values.
- Allocation failure returns `ENOMEM` in both backends (`stlink_threads_posix.c` gained `<errno.h>`).
- Win32 `stlink_thread_create` keeps the errno set by `_beginthreadex`; the `errno == 0` fallback is
  now `EAGAIN` instead of `-1`.
- Win32 `stlink_thread_join` no longer returns a raw `GetLastError()` value. A new `static`
  `stlink_thread_error()` maps `ERROR_NOT_ENOUGH_MEMORY` / `ERROR_OUTOFMEMORY` to `ENOMEM` and
  everything else (chiefly `ERROR_INVALID_HANDLE`) to `EINVAL`. `GetLastError()` is consulted only
  after `WAIT_FAILED`; any other non-success wait result returns `EINVAL`.
- `stlink_threads.h` documents the contract: positive errno-style values only, never negative, not
  through `errno`; `ENOMEM`, `EAGAIN`, `EINVAL`, plus pthreads codes such as `EDEADLK` on POSIX.
- Windows self-join (`EDEADLK` parity with pthreads): `stlink_thread_join` first compares
  `GetThreadId(handle)` with `GetCurrentThreadId()` and returns `EDEADLK` instead of waiting forever
  on its own handle. `GetThreadId` yields 0 for an invalid handle, which never equals a real thread
  id, so that case still falls through to the wait and is reported as `EINVAL`. The refused join
  returns before the handle is closed, so the thread stays joinable, as on POSIX; the header now says
  "a join that fails leaves the thread joinable" (partly covering finding 4).
- New regression check `check_thread_self_join` in `src/main.c`: a worker joins its own handle and
  must get `EDEADLK`, then the main thread joins it normally. It passes on Win32 and POSIX.
- The other failure paths (`ENOMEM`, `EAGAIN`, the join error mapping) cannot be triggered by the
  tests; they are verified by inspection only (failure-path tests belong to finding 6).

### 2. `stlink_mutex.h` includes `<windows.h>` publicly - `fixed`

- `stlink_mutex.h:9` pulls `windows.h` into every consumer (macro pollution: `min`, `max`, `ERROR`,
  winsock include ordering). This contradicts `stlink_threads.h`, which deliberately keeps it out.
- Proposed: make the native member an opaque pointer-sized field (`SRWLOCK` is `{ PVOID Ptr; }`), keep
  `windows.h` in the `.c` file, add a size/alignment static assert there. Fallback: define
  `WIN32_LEAN_AND_MEAN` and `NOMINMAX` before the include.
- Constraint: `STLINK_MUTEX_INIT` must stay a valid static initializer on Windows.

Fix: `stlink_mutex.h` no longer includes `<windows.h>`. On Windows `stlink_mutex_t` is now
`struct { void *native; }` and `STLINK_MUTEX_INIT` is `{ 0 }` (all-zero equals `SRWLOCK_INIT`).
`stlink_mutex_win32.c` now includes `<windows.h>` itself, reaches the storage through a private
`stlink_mutex_native()` helper that casts to `SRWLOCK *`, and has three `_Static_assert`s
guarding that size, alignment and offset match `SRWLOCK`. The POSIX side is unchanged.
`<errno.h>` stays in the header because `EBUSY` is part of the public contract.

### 3. No minimum Windows version is declared - `fixed`

- `TryAcquireSRWLockExclusive` needs `_WIN32_WINNT >= 0x0601` (Windows 7). MSVC and recent mingw-w64
  default high enough; older mingw-w64 toolchains default lower and fail to compile.
- Originally proposed: `target_compile_definitions(stlink_env PUBLIC _WIN32_WINNT=0x0601)` under
  `if(WIN32)`.
- Reconsidered: the target project (`stlink-origin`) supports Windows 10 and newer only, and its
  toolchains default `_WIN32_WINNT` at or above Windows 7. Forcing `0x0601` would lower the API level
  below that default and could clash with a value the consumer sets, so no CMake definition was added.

Fix: `stlink_mutex_win32.c` now has a compile-time guard right after its includes:
`#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0601)` -> `#error` naming the Windows 7 requirement.
An old toolchain default therefore fails with a clear message instead of a run of "implicit
declaration of function" errors. Confirmed by compiling with `-D_WIN32_WINNT=0x0501`: the `#error`
is the first diagnostic. No CMake change; the supported floor (Windows 10+) is set by the target
project's declared compilers, not by this library.

### 4. Thread contract under-documented in `stlink_threads.h` - `fixed`

Undefined or unstated today:
- `*thread` is valid only on success (POSIX leaves it unspecified on failure).
- Each thread must be joined exactly once; joining yourself is invalid.
- No detach, and no invalid-handle sentinel (a zero-initialised `stlink_thread_t` is not portable,
  since `pthread_t` may be a struct).
- Windows join leaks the handle if `WaitForSingleObject` fails (`stlink_threads_win32.c:69-71`), and a
  failing join does not say whether the thread is still joinable.

Fix: documentation only, in `stlink_threads.h`; no code changed for this finding.
- New lifetime block: no detach, so every created thread must be joined exactly once; no invalid
  value, so an `stlink_thread_t` must not be zero-initialised or compared and is meaningful only after
  a successful create; `arg` must outlive the thread's use of it.
- `stlink_thread_create`: `thread` and `fn` must not be NULL (unchecked); `*thread` is written only on
  success and unspecified after failure; the new thread may run before create returns, so `fn` must
  not read `*thread` (publish it separately, as `check_thread_self_join` does); `fn` must return
  normally to end the thread.
- `stlink_thread_join`: once per successful create, from any thread but the target (self-join gives
  `EDEADLK`, added under finding 1); success releases the handle and makes the thread's writes
  visible to the caller; a failed join releases nothing, so the thread stays joinable.
- The Windows "leak on failed wait" is resolved by that last rule rather than by closing the handle:
  the code already leaves the handle open on failure, so a caller may retry, and the header now says
  so. `stlink_threads_win32.c` is unchanged.
- Not changed, by choice: NULL arguments are documented preconditions, not checked with `EINVAL`.

### 5. Default stack size differs by platform - `fixed`

The API has no stack parameter. Windows uses the executable's setting (1 MB default), glibc about
8 MB, musl about 128 KB, macOS 512 KB. Code that works on one platform can overflow on another.
Proposed: document it, or add a stack-size argument.

Fix: documented only, by decision (no stack-size parameter added). `stlink_threads.h` has a "Stack
size" paragraph in the lifetime block listing the defaults (Windows: linked size, 1 MB with MSVC;
glibc: `RLIMIT_STACK`, usually 8 MB; musl: about 128 KB; macOS: 512 KB for non-main threads) and
telling callers to fit the smallest and keep large buffers off a worker's stack. The README has a
matching "Threads" section. The figures are from general knowledge of those platforms, not measured
here.

### 6. Test gaps in `src/main.c` - `fixed` (failure paths partly)

- Busy-wait gates (`while(stlink_atomic_load(&ready) == 0) {}` at lines 64 and 137) burn CPU; on a
  single-core CI runner they can approach the 30 s CTest timeout. Add a yield/sleep.
- No coverage of the `create` failure paths or `ctx` cleanup.
- No check of a successful `trylock` from a second thread while the mutex is free (only the `EBUSY`
  case is probed).

Fix, all in `src/main.c`:
- Spin-waits: the three `while(... == 0) {}` gates (`ready`, `state->start`, `probe->published`) now
  call a test-local `cpu_yield()` (`SwitchToThread()` on Windows, `sched_yield()` elsewhere). It lives
  in `main.c` and not in the library, so no new public API; `main.c` therefore includes `<windows.h>`
  under `_WIN32`.
- Free-mutex try-lock: `check_mutex_operations` now has a second thread take the unlocked mutex
  (`probe.result == 0`) and release it, and the main thread then takes it again.
- Thread lifecycle: new `check_thread_lifecycle` runs 1,000 create/join cycles and checks every worker
  ran. On Windows it also compares `GetProcessHandleCount` before and after (a leaked handle per join
  would add 1,000; the limit allows 100) and checks `stlink_thread_join(NULL) == EINVAL`, which
  exercises the join error mapping. This covers `ctx` and handle cleanup on the success path.
  Mutation-checked: removing `CloseHandle` from a scratch copy of the Win32 backend makes the
  test fail (exit 1).
- Not covered, and not coverable without fault injection: the `create` failure paths themselves
  (`ENOMEM` from a failed `malloc`, `EAGAIN` from a failed `_beginthreadex` or `pthread_create`) and
  the freeing of `ctx` on that failure. They stay verified by inspection only. The README says so.

### 7. Minor - `fixed`

- File names and `@brief` still say "pthreads wrapper for WIN32"; the directory is `pthreads/` though
  the Windows backend has nothing to do with pthreads.
- `CMAKE_C_STANDARD 17`, but AGENTS.md and the atomics code speak of C11. README/AGENTS.md should match.
- `CMAKE_C_EXTENSIONS ON` builds as gnu17; turning it off would catch non-portable code earlier.

Fix:
- Naming: the `@brief` lines now read "portable thread create/join wrapper" (`stlink_threads.h`),
  "Win32 thread backend (_beginthreadex)" and "POSIX thread backend (pthreads)". The file names and the
  `pthreads/` directory are deliberately not renamed, so the layout keeps matching upstream
  `stlink-origin`; the license header text was not touched.
- C standard: C17 is the project's standard. `AGENTS.md` now says C17 (it said C11 twice) and the
  README states it. References to "C11 atomics" / `<stdatomic.h>` are unchanged, since C17 includes
  C11's atomics and those name the feature and backend.
- `CMAKE_C_EXTENSIONS` is now `OFF`; the gcc build line is `-std=c17`, not `gnu17`. Note that
  `stlink-origin/CMakeLists.txt` still has it `ON`, so this project is stricter than its target; revert
  the one line if exact parity matters more than the stricter check.
- Docs also updated because the earlier fixes made them wrong: the README no longer calls the thread
  files an "unchanged copy", no longer says the mutex header includes the native platform header, and
  gained a "Threads" section and the new checks in its test description.

## Checked and fine

- Atomics: MSVC path uses full-barrier Interlocked intrinsics (correct on x86 and ARM64), avoids
  `/experimental:c11atomics`, clang-cl takes the same path; `fetch_sub` negation for `INT32_MIN` is
  handled; MinGW-gcc falls through to `<stdatomic.h>`.
- Mutexes: SRWLOCK vs `pthread_mutex_t` semantics match the documented contract (non-recursive,
  `trylock` returns `EBUSY`, unlock-then-lock publishes writes); static initializers nest correctly
  in `struct mutex_counter`; publication through the mutex in `check_mutex_counter` is ordered correctly.
- Threads: no race on `*thread` (the new thread never reads it); `ctx` ownership is correct on both
  paths (freed in the entry function, or on create failure).

## Verification log

Finding 2 (Windows x64, Release, Ninja, fresh build dirs, no warnings emitted):
- MSVC 14.51 (VS 2026) `/W4`: `threading-and-atomics` passed.
- MinGW gcc (msys64 ucrt64): `threading-and-atomics` passed.
- Not tested: POSIX/Linux backend (untouched by this change), clang-cl, 32-bit Windows.

Finding 1 (same setup, rebuilt after the error-code change and the Windows `EDEADLK` addition): MSVC
and MinGW gcc pass `threading-and-atomics` with no warnings. The POSIX backend was built with
`-std=c17 -Wall -Wextra -O2 -pthread` under WSL gcc (`main.c` + the two POSIX sources, compiled
directly, not through CMake) and the full regression executable ran and passed with no warnings,
including `check_thread_self_join`. Not tested: macOS, musl, clang, and the `ENOMEM` / `EAGAIN` /
join-mapping paths.

Findings 5, 6, 7 (same Windows setup, reconfigured because `CMAKE_C_EXTENSIONS` changed): MSVC and
MinGW gcc build with no warnings and pass `threading-and-atomics`. WSL gcc built with
`-std=c17 -pedantic -Wall -Wextra -O2 -pthread` (main.c + the POSIX sources, compiled directly) and
the full executable passed, including the new lifecycle and free-mutex checks. Not tested: macOS,
musl, clang, clang-cl, 32-bit Windows; the Windows-only handle-count and `EINVAL` checks ran on
Windows only, and the POSIX lifecycle run checks worker count but not resource growth.

Finding 4 (comment-only change to `stlink_threads.h`, same setup): MSVC and MinGW gcc rebuilt with no
warnings and pass `threading-and-atomics`; the WSL gcc POSIX build (`-std=c17 -Wall -Wextra -O2
-pthread`) also passes. The documented join guarantees were not tested beyond the existing checks.

Finding 3 (same setup, rebuilt after adding the guard): MSVC and MinGW gcc both still pass
`threading-and-atomics` with no warnings; the guard fires on `-D_WIN32_WINNT=0x0501` (gcc, syntax
check only). The compiler minimums declared in `stlink-origin` were not read, so the claim that their
default `_WIN32_WINNT` is >= `0x0601` rests on the stated Windows 10+ target, not on a checked file.
