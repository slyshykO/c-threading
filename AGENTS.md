# Repository Guidelines

## Project Structure & Module Organization

This repository experiments with ST-Link threading and signed 32-bit atomics in C17.
- `src/main.c` contains the executable and regression checks.
- `src/stlink-env/pthreads/` contains the shared thread API and Win32/POSIX implementations, copied from upstream ST-Link.
- `src/stlink-env/atomic/stlink_atomic.h` provides header-only atomics using C11 operations or MSVC Interlocked intrinsics.
- `CMakeLists.txt` builds the `stlink_env` static library and `c-th` executable. `README.md` documents API semantics; `LICENSE` preserves upstream BSD terms.

There is no separate test or asset directory. Keep generated output in `build/` and `release/`.

## Build, Test, and Development Commands

Use CMake 3.28 or newer, Ninja, and a C17-capable compiler available on PATH. For MSVC, start in a Visual Studio developer shell.

```sh
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

These commands configure, build, and run the regression executable. Run `./release/c-th` on POSIX or `.\release\c-th.exe` in PowerShell for direct output. Use separate build directories per compiler; choose one at initial configuration with `-DCMAKE_C_COMPILER=gcc`, `clang`, or `cl`. Override copied output with `-DRELEASE_DIR=<directory>`.

## Coding Style & Naming Conventions

Follow existing C style: four-space indentation, opening braces on the same line, and control statements such as `if(condition)`. Use snake_case identifiers, `stlink_thread_` or `stlink_atomic_` API prefixes, `_t` type suffixes, and uppercase constants/macros. Keep internal helpers `static`. No formatter or linter configuration is checked in; avoid unrelated reformatting and preserve upstream license headers.

## Testing Guidelines

CTest runs `threading-and-atomics` with a 30-second timeout. Extend `src/main.c` using descriptive `check_*` helpers and the existing `CHECK` macro, which remains active in Release builds. Cover return values, compare-exchange outcomes, signed wraparound, concurrent increments, and flag publication. No coverage threshold is configured. For backend changes, validate Windows and POSIX where available and report untested configurations.

## Commit & Pull Request Guidelines

History is limited to `init` and a descriptive imperative README update; no formal commit format is established. Use concise imperative subjects. PRs should explain the behavior changed, link relevant issues, and record compilers, platforms, and test results. Update README documentation when API semantics or build instructions change.

## Concurrency Constraints

Initialize atomics before sharing. Access live atomic objects only through the API; never copy or pack them. Preserve sequential consistency and keep Windows builds independent of pthread libraries.
