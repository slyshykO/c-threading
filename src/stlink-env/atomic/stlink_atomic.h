/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef STLINK_ATOMIC_H
#define STLINK_ATOMIC_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Signed 32-bit counters and flags (0/1). All operations except init are
 * atomic and sequentially consistent. Initialize before sharing with threads.
 * Use only this API to access storage; do not copy live objects or pack them.
 * volatile alone does not provide atomicity. This is a small C API, not a
 * replacement for all of <stdatomic.h>.
 */
#define STLINK_ATOMIC_INT_INIT(value) { (value) }

#if defined(_MSC_VER)

/* No experimental MSVC atomics flag or windows.h needed. */
#include <intrin.h>
#define STLINK_ATOMIC_BACKEND "MSVC Interlocked intrinsics"

typedef struct {
    __declspec(align(4)) volatile long value;
} stlink_atomic_int_t;

static inline void stlink_atomic_init(stlink_atomic_int_t *object, int32_t value) {
    object->value = (long)value;
}

/* An Interlocked read may write the same value, so object is not const. */
static inline int32_t stlink_atomic_load(stlink_atomic_int_t *object) {
    return (int32_t)_InterlockedCompareExchange(&object->value, 0, 0);
}

static inline void stlink_atomic_store(stlink_atomic_int_t *object, int32_t value) {
    (void)_InterlockedExchange(&object->value, (long)value);
}

/* Exchange and fetch operations return the value BEFORE the operation. */
static inline int32_t stlink_atomic_exchange(stlink_atomic_int_t *object, int32_t value) {
    return (int32_t)_InterlockedExchange(&object->value, (long)value);
}

static inline int32_t stlink_atomic_fetch_add(stlink_atomic_int_t *object, int32_t value) {
    return (int32_t)_InterlockedExchangeAdd(&object->value, (long)value);
}

static inline int32_t stlink_atomic_fetch_sub(stlink_atomic_int_t *object, int32_t value) {
    /* Unsigned negation handles INT32_MIN without signed overflow. */
    return (int32_t)_InterlockedExchangeAdd(&object->value, (long)(0UL - (unsigned long)value));
}

/* Strong CAS: failure replaces *expected with the observed value. */
static inline bool stlink_atomic_compare_exchange(stlink_atomic_int_t *object,
                                                 int32_t *expected, int32_t desired) {
    int32_t previous = (int32_t)_InterlockedCompareExchange(
        &object->value, (long)desired, (long)*expected);
    if(previous == *expected) { return true; }
    *expected = previous;
    return false;
}

#else

#if defined(__STDC_NO_ATOMICS__) && __STDC_NO_ATOMICS__
#error "stlink_atomic.h requires C11 atomics or MSVC Interlocked intrinsics"
#endif
#include <stdatomic.h>
#define STLINK_ATOMIC_BACKEND "C11 <stdatomic.h>"

typedef struct {
    _Atomic(int32_t) value;
} stlink_atomic_int_t;

static inline void stlink_atomic_init(stlink_atomic_int_t *object, int32_t value) {
    atomic_init(&object->value, value);
}

static inline int32_t stlink_atomic_load(stlink_atomic_int_t *object) {
    return atomic_load_explicit(&object->value, memory_order_seq_cst);
}

static inline void stlink_atomic_store(stlink_atomic_int_t *object, int32_t value) {
    atomic_store_explicit(&object->value, value, memory_order_seq_cst);
}

static inline int32_t stlink_atomic_exchange(stlink_atomic_int_t *object, int32_t value) {
    return atomic_exchange_explicit(&object->value, value, memory_order_seq_cst);
}

static inline int32_t stlink_atomic_fetch_add(stlink_atomic_int_t *object, int32_t value) {
    return atomic_fetch_add_explicit(&object->value, value, memory_order_seq_cst);
}

static inline int32_t stlink_atomic_fetch_sub(stlink_atomic_int_t *object, int32_t value) {
    return atomic_fetch_sub_explicit(&object->value, value, memory_order_seq_cst);
}

static inline bool stlink_atomic_compare_exchange(stlink_atomic_int_t *object,
                                                 int32_t *expected, int32_t desired) {
    return atomic_compare_exchange_strong_explicit(&object->value, expected, desired,
                                                   memory_order_seq_cst, memory_order_seq_cst);
}

#endif
#endif /* STLINK_ATOMIC_H */
