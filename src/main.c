#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "stlink_atomic.h"
#include "stlink_threads.h"

enum { THREAD_COUNT = 4, INCREMENTS_PER_THREAD = 100000 };

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
    while(stlink_atomic_load(&ready) == 0) {}
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

int main(void) {
#if defined(_WIN32)
    puts("Threads: ST-Link Win32 wrapper");
#else
    puts("Threads: ST-Link POSIX wrapper");
#endif
    printf("Atomics: %s\n", STLINK_ATOMIC_BACKEND);
    if(check_atomic_operations() != EXIT_SUCCESS) { return EXIT_FAILURE; }

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
    puts("Threading, atomic counters and flag publication passed.");
    return EXIT_SUCCESS;
}
