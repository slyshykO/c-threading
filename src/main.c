#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sched.h>
#endif

#include "stlink_atomic.h"
#include "stlink_threads.h"
#include "stlink_mutex.h"

enum { THREAD_COUNT = 4, INCREMENTS_PER_THREAD = 100000, LIFECYCLE_ITERATIONS = 1000 };

static stlink_atomic_int_t counter = STLINK_ATOMIC_INT_INIT(0);
static stlink_atomic_int_t cas_counter = STLINK_ATOMIC_INT_INIT(0);
static stlink_atomic_int_t ready = STLINK_ATOMIC_INT_INIT(0);
static stlink_atomic_int_t publication_errors = STLINK_ATOMIC_INT_INIT(0);
static int increments; /* Published to workers by the atomic ready flag. */

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while(0)

/* Give up the CPU inside a spin-wait, so that a starved single-core machine
 * lets the thread being waited for run instead of burning its whole quantum. */
static void cpu_yield(void) {
#if defined(_WIN32)
    (void)SwitchToThread();
#else
    (void)sched_yield();
#endif
}

static int check_atomic_operations(void) {
    stlink_atomic_int_t value;
    stlink_atomic_int_t initialized = STLINK_ATOMIC_INT_INIT(-7);
    CHECK(stlink_atomic_load(&initialized) == -7);

    stlink_atomic_init(&value, 10);
    CHECK(stlink_atomic_load(&value) == 10);
    CHECK(stlink_atomic_fetch_add(&value, 5) == 10);
    CHECK(stlink_atomic_load(&value) == 15);
    CHECK(stlink_atomic_fetch_sub(&value, 3) == 15);
    CHECK(stlink_atomic_load(&value) == 12);
    CHECK(stlink_atomic_exchange(&value, -2) == 12);
    CHECK(stlink_atomic_load(&value) == -2);

    int32_t expected = -2;
    CHECK(stlink_atomic_compare_exchange(&value, &expected, 42));
    CHECK(expected == -2);
    CHECK(stlink_atomic_load(&value) == 42);
    expected = 0;
    CHECK(!stlink_atomic_compare_exchange(&value, &expected, 99));
    CHECK(expected == 42);
    CHECK(stlink_atomic_load(&value) == 42);

    /* Signed atomic arithmetic wraps; ordinary signed arithmetic does not. */
    stlink_atomic_store(&value, INT32_MAX);
    CHECK(stlink_atomic_fetch_add(&value, 1) == INT32_MAX);
    CHECK(stlink_atomic_load(&value) == INT32_MIN);
    CHECK(stlink_atomic_fetch_sub(&value, 1) == INT32_MIN);
    CHECK(stlink_atomic_load(&value) == INT32_MAX);
    stlink_atomic_store(&value, 0);
    CHECK(stlink_atomic_fetch_sub(&value, INT32_MIN) == 0);
    CHECK(stlink_atomic_load(&value) == INT32_MIN);
    puts("Atomic operation checks passed.");
    return EXIT_SUCCESS;
}

static void increment_counter(void *arg) {
    const int thread_id = *(const int *)arg;

    /* A short start gate exercises flag synchronization and data publication. */
    while(stlink_atomic_load(&ready) == 0) { cpu_yield(); }
    const int count = increments;
    if(count != INCREMENTS_PER_THREAD) {
        stlink_atomic_fetch_add(&publication_errors, 1);
        return;
    }

    for(int i = 0; i < count; ++i) {
        stlink_atomic_fetch_add(&counter, 1);

        int32_t expected = stlink_atomic_load(&cas_counter);
        while(!stlink_atomic_compare_exchange(&cas_counter, &expected, expected + 1)) {
            /* On failure expected is refreshed for the next attempt. */
        }
    }

    printf("Thread %d finished %d increments.\n", thread_id, count);
}

/* Test failures terminate the process so no worker outlives its context. */
static void require_success(int32_t error, const char *operation) {
    if(error != 0) {
        fprintf(stderr, "%s failed (error %" PRId32 ").\n", operation, error);
        exit(EXIT_FAILURE);
    }
}

struct mutex_probe {
    stlink_mutex_t mutex;
    int32_t result;
};

static void try_mutex(void *arg) {
    struct mutex_probe *probe = arg;
    probe->result = stlink_mutex_trylock(probe->mutex);
    if(probe->result == 0) {
        require_success(stlink_mutex_unlock(probe->mutex), "probe unlock");
    }
}

static int check_mutex_operations(void) {
    stlink_mutex_t mutex = NULL;
    require_success(stlink_mutex_create(&mutex), "mutex create");
    CHECK(mutex != NULL);
    require_success(stlink_mutex_lock(mutex), "mutex lock");

    /* The owner keeps the lock until a different thread has tried it. */
    struct mutex_probe probe = { mutex, 0 };
    stlink_thread_t thread;
    require_success(stlink_thread_create(&thread, try_mutex, &probe), "probe create");
    require_success(stlink_thread_join(thread), "probe join");
    require_success(stlink_mutex_unlock(mutex), "mutex unlock");
    require_success(stlink_mutex_destroy(mutex), "mutex destroy");
    CHECK(probe.result == EBUSY);

    /* Reuse the handle variable for a new mutex, initially unlocked. */
    mutex = NULL;
    require_success(stlink_mutex_create(&mutex), "mutex recreate");
    CHECK(mutex != NULL);
    require_success(stlink_mutex_trylock(mutex), "mutex trylock");
    require_success(stlink_mutex_unlock(mutex), "mutex unlock");

    /* A free mutex can be taken, and released again, by a different thread. */
    probe.mutex = mutex;
    probe.result = EBUSY;
    require_success(stlink_thread_create(&thread, try_mutex, &probe), "free probe create");
    require_success(stlink_thread_join(thread), "free probe join");
    CHECK(probe.result == 0);
    require_success(stlink_mutex_trylock(mutex), "mutex trylock after probe");
    require_success(stlink_mutex_unlock(mutex), "mutex unlock after probe");
    require_success(stlink_mutex_destroy(mutex), "mutex destroy");
    puts("Mutex operation checks passed.");
    return EXIT_SUCCESS;
}

static int check_mutex_lifecycle(void) {
    stlink_mutex_t mutex = NULL;
    for(int i = 0; i < LIFECYCLE_ITERATIONS; ++i) {
        stlink_mutex_t other = NULL;
        require_success(stlink_mutex_create(&mutex), "lifecycle mutex create");
        require_success(stlink_mutex_create(&other), "independent mutex create");
        CHECK(mutex != NULL && other != NULL && mutex != other);

        /* A held mutex must not prevent acquiring a separately created one. */
        require_success(stlink_mutex_lock(mutex), "lifecycle mutex lock");
        require_success(stlink_mutex_trylock(other), "independent mutex trylock");
        require_success(stlink_mutex_unlock(other), "independent mutex unlock");
        require_success(stlink_mutex_destroy(other), "independent mutex destroy");
        require_success(stlink_mutex_unlock(mutex), "lifecycle mutex unlock");
        require_success(stlink_mutex_destroy(mutex), "lifecycle mutex destroy");
        mutex = NULL;
    }
    puts("Mutex lifecycle checks passed.");
    return EXIT_SUCCESS;
}

struct self_join_probe {
    stlink_thread_t thread;
    stlink_atomic_int_t published;
    int32_t result;
};

static void join_self(void *arg) {
    struct self_join_probe *probe = arg;
    /* The handle is written by stlink_thread_create; wait until it is published. */
    while(stlink_atomic_load(&probe->published) == 0) { cpu_yield(); }
    probe->result = stlink_thread_join(probe->thread);
}

static int check_thread_self_join(void) {
    struct self_join_probe probe = { .published = STLINK_ATOMIC_INT_INIT(0) };
    require_success(stlink_thread_create(&probe.thread, join_self, &probe), "self-join create");
    stlink_atomic_store(&probe.published, 1);

    /* A refused self-join must leave the thread joinable for this call. */
    require_success(stlink_thread_join(probe.thread), "self-join join");
    CHECK(probe.result == EDEADLK);
    puts("Thread self-join check passed.");
    return EXIT_SUCCESS;
}

static stlink_atomic_int_t lifecycle_runs = STLINK_ATOMIC_INT_INIT(0);

static void lifecycle_worker(void *arg) {
    (void)arg;
    stlink_atomic_fetch_add(&lifecycle_runs, 1);
}

/* Repeated create and join must release everything: the per-thread context on
 * both backends and, on Windows, the thread handle. */
static int check_thread_lifecycle(void) {
#if defined(_WIN32)
    DWORD handles_before = 0;
    DWORD handles_after = 0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
#endif
    for(int i = 0; i < LIFECYCLE_ITERATIONS; ++i) {
        stlink_thread_t thread;
        require_success(stlink_thread_create(&thread, lifecycle_worker, NULL), "lifecycle create");
        require_success(stlink_thread_join(thread), "lifecycle join");
    }
    CHECK(stlink_atomic_load(&lifecycle_runs) == LIFECYCLE_ITERATIONS);
#if defined(_WIN32)
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
    /* A leaked handle per join would add LIFECYCLE_ITERATIONS; allow some noise. */
    CHECK(handles_after < handles_before + LIFECYCLE_ITERATIONS / 10);

    /* A handle that is not a thread cannot be waited on; it maps to EINVAL. */
    CHECK(stlink_thread_join(NULL) == EINVAL);
#endif
    puts("Thread lifecycle checks passed.");
    return EXIT_SUCCESS;
}

struct mutex_counter {
    stlink_mutex_t mutex;
    stlink_atomic_int_t start;
    int count;
    int mirror;
    int errors;
};

static void increment_mutex_counter(void *arg) {
    struct mutex_counter *state = arg;
    while(stlink_atomic_load(&state->start) == 0) { cpu_yield(); }
    for(int i = 0; i < INCREMENTS_PER_THREAD; ++i) {
        require_success(stlink_mutex_lock(state->mutex), "counter lock");
        if(state->mirror != state->count) { ++state->errors; }
        ++state->count;
        state->mirror = state->count;
        require_success(stlink_mutex_unlock(state->mutex), "counter unlock");
    }
}

static int check_mutex_counter(void) {
    struct mutex_counter state = {
        NULL, STLINK_ATOMIC_INT_INIT(0), 0, 0, 0
    };
    stlink_thread_t threads[THREAD_COUNT];
    require_success(stlink_mutex_create(&state.mutex), "counter mutex create");

    /* Workers must observe this ordinary write through the mutex. */
    require_success(stlink_mutex_lock(state.mutex), "publication lock");
    for(int i = 0; i < THREAD_COUNT; ++i) {
        require_success(stlink_thread_create(&threads[i], increment_mutex_counter, &state),
                        "counter thread create");
    }
    stlink_atomic_store(&state.start, 1);
    state.count = 7;
    state.mirror = 7;
    require_success(stlink_mutex_unlock(state.mutex), "publication unlock");

    for(int i = 0; i < THREAD_COUNT; ++i) {
        require_success(stlink_thread_join(threads[i]), "counter thread join");
    }
    require_success(stlink_mutex_destroy(state.mutex), "counter mutex destroy");
    const int expected = 7 + THREAD_COUNT * INCREMENTS_PER_THREAD;
    printf("Mutex counter: %d (expected %d)\n", state.count, expected);
    CHECK(state.count == expected);
    CHECK(state.mirror == expected);
    CHECK(state.errors == 0);
    return EXIT_SUCCESS;
}

int main(void) {
#if defined(_WIN32)
    puts("Threads: ST-Link Win32 wrapper");
#else
    puts("Threads: ST-Link POSIX wrapper");
#endif
    printf("Atomics: %s\n", STLINK_ATOMIC_BACKEND);
    if(check_atomic_operations() != EXIT_SUCCESS) { return EXIT_FAILURE; }
    if(check_mutex_operations() != EXIT_SUCCESS) { return EXIT_FAILURE; }
    if(check_mutex_lifecycle() != EXIT_SUCCESS) { return EXIT_FAILURE; }
    if(check_mutex_counter() != EXIT_SUCCESS) { return EXIT_FAILURE; }
    if(check_thread_self_join() != EXIT_SUCCESS) { return EXIT_FAILURE; }
    if(check_thread_lifecycle() != EXIT_SUCCESS) { return EXIT_FAILURE; }

    stlink_thread_t threads[THREAD_COUNT];
    int thread_ids[THREAD_COUNT];
    int threads_created = 0;
    int exit_code = EXIT_SUCCESS;

    printf("Starting %d threads with %d increments each.\n",
           THREAD_COUNT, INCREMENTS_PER_THREAD);
    for(int i = 0; i < THREAD_COUNT; ++i) {
        thread_ids[i] = i + 1;
        int32_t error = stlink_thread_create(&threads[i], increment_counter, &thread_ids[i]);
        if(error != 0) {
            fprintf(stderr, "Failed to create thread %d (error %" PRId32 ").\n", thread_ids[i], error);
            exit_code = EXIT_FAILURE;
            break;
        }
        ++threads_created;
    }

    /* Release any started workers even if a later thread could not start. */
    increments = INCREMENTS_PER_THREAD;
    stlink_atomic_store(&ready, 1);

    for(int i = 0; i < threads_created; ++i) {
        int32_t error = stlink_thread_join(threads[i]);
        if(error != 0) {
            fprintf(stderr, "Failed to join thread %d (error %" PRId32 ").\n", thread_ids[i], error);
            exit_code = EXIT_FAILURE;
        }
    }
    if(exit_code != EXIT_SUCCESS) { return exit_code; }

    const int32_t expected = THREAD_COUNT * INCREMENTS_PER_THREAD;
    const int32_t actual = stlink_atomic_load(&counter);
    const int32_t cas_actual = stlink_atomic_load(&cas_counter);
    printf("Atomic counter: %" PRId32 " (expected %" PRId32 ")\n", actual, expected);
    printf("CAS counter: %" PRId32 " (expected %" PRId32 ")\n", cas_actual, expected);
    CHECK(actual == expected);
    CHECK(cas_actual == expected);
    CHECK(stlink_atomic_load(&publication_errors) == 0);
    puts("Threading, atomics, mutexes and flag publication passed.");
    return EXIT_SUCCESS;
}
