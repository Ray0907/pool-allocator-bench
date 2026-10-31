/* Public-API-only tests. No access to pool_t internals or test setters. */
#include "pool.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t assertions;
static unsigned tests;
static uint32_t seed = UINT32_C(0x51a7c0de);
#define CHECK(x) do { ++assertions; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    exit(EXIT_FAILURE); } } while (0)
#define RUN(fn) do { fn(); ++tests; printf("PASS %s\n", #fn); } while (0)

static uint32_t random32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}

static void invalid_create(void) {
    CHECK(pool_create(0, 1, 8) == NULL);
    CHECK(pool_create(1, 0, 8) == NULL);
    CHECK(pool_create(1, 1, 0) == NULL);
    CHECK(pool_create(1, 1, 3) == NULL);
    CHECK(pool_create(16, 1, 24) == NULL);
    CHECK(pool_create(16, 1, 12) == NULL);
    CHECK(pool_create(16, 1, 48) == NULL);
    CHECK(pool_create(1, 1, sizeof(void *) / 2) == NULL);
    CHECK(pool_check(NULL) == 0);
    CHECK(pool_alloc(NULL) == NULL);
    CHECK(pool_free(NULL, NULL) == POOL_OK);
    int outside = 0;
    CHECK(pool_free(NULL, &outside) == POOL_ERR_OUTSIDE);
    pool_destroy(NULL);
}

static void overflow_create(void) {
    CHECK(pool_create(SIZE_MAX, 1, 8) == NULL); /* align_up overflow */
    CHECK(pool_create(SIZE_MAX / 2, 4, 8) == NULL); /* stride * count */
    CHECK(pool_create(SIZE_MAX - 7, 1, 8) == NULL); /* backing + align */
#if SIZE_MAX > UINT32_MAX
    CHECK(pool_create(8, (size_t)UINT32_MAX + 1, 8) == NULL);
#endif
}

static void backing_failure(void) {
    /* Valid arithmetic, impossible address-space demand. The additional
     * allocator-shim build deterministically fails this backing request. */
    CHECK(pool_create(SIZE_MAX / 2, 1, 8) == NULL);
}

static void alignment_and_overlap(void) {
    const size_t aligns[] = {8, 16, 64, 128, 4096};
    enum { N = 37, SIZE = 37 };
    for (size_t a = 0; a < sizeof aligns / sizeof *aligns; ++a) {
        pool_t *p = pool_create(SIZE, N, aligns[a]);
        void *ptrs[N];
        CHECK(p != NULL);
        CHECK(pool_check(p));
        for (size_t i = 0; i < N; ++i) {
            ptrs[i] = pool_alloc(p);
            CHECK(ptrs[i] != NULL);
            CHECK((uintptr_t)ptrs[i] % aligns[a] == 0);
            memset(ptrs[i], (int)(i + 1), SIZE);
            for (size_t j = 0; j < i; ++j) {
                uintptr_t x = (uintptr_t)ptrs[i], y = (uintptr_t)ptrs[j];
                CHECK(x > y ? x - y >= SIZE : y - x >= SIZE);
            }
            CHECK(pool_check(p));
        }
        CHECK(pool_alloc(p) == NULL);
        for (size_t i = 0; i < N; ++i) {
            for (size_t b = 0; b < SIZE; ++b)
                CHECK(((unsigned char *)ptrs[i])[b] == (unsigned char)(i + 1));
            CHECK(pool_free(p, ptrs[i]) == POOL_OK);
            CHECK(pool_check(p));
        }
        pool_destroy(p);
    }
}

static void exhaustion_and_lifo(void) {
    pool_t *p = pool_create(1, 3, 8);
    CHECK(p != NULL);
    void *a = pool_alloc(p), *b = pool_alloc(p), *c = pool_alloc(p);
    CHECK(a && b && c && a != b && b != c && a != c);
    CHECK(pool_alloc(p) == NULL);
    CHECK(pool_check(p));
    CHECK(pool_free(p, b) == POOL_OK);
    CHECK(pool_alloc(p) == b);
    CHECK(pool_alloc(p) == NULL);
    CHECK(pool_free(p, a) == POOL_OK);
    CHECK(pool_free(p, c) == POOL_OK);
    CHECK(pool_alloc(p) == c);
    CHECK(pool_alloc(p) == a);
    CHECK(pool_alloc(p) == NULL);
    pool_destroy(p); /* Defined: destroy frees storage even with live slots. */
}

static void null_and_double_free(void) {
    pool_t *p = pool_create(16, 2, 16);
    CHECK(p != NULL);
    void *a = pool_alloc(p);
    CHECK(a != NULL);
    memset(a, 0x6b, 16);
    CHECK(pool_free(p, NULL) == POOL_OK);
    CHECK(pool_check(p));
    CHECK(pool_free(p, a) == POOL_OK);
#ifndef POOL_DEBUG
    for (size_t i = 0; i < 16; ++i) CHECK(((unsigned char *)a)[i] == 0x6b);
#endif
    CHECK(pool_free(p, a) == POOL_ERR_DOUBLE_FREE);
    CHECK(pool_check(p));
    CHECK(pool_alloc(p) == a);
    void *b = pool_alloc(p);
    CHECK(b != NULL && b != a);
    CHECK(pool_alloc(p) == NULL);
    pool_destroy(p);
}

static void invalid_pointers(void) {
    pool_t *p = pool_create(32, 3, 16);
    pool_t *q = pool_create(32, 1, 16);
    CHECK(p && q);
    void *ptrs[3];
    uintptr_t base = UINTPTR_MAX;
    for (size_t i = 0; i < 3; ++i) {
        ptrs[i] = pool_alloc(p);
        CHECK(ptrs[i] != NULL);
        if ((uintptr_t)ptrs[i] < base) base = (uintptr_t)ptrs[i];
    }
    /* Exactly at the upper logical boundary, still inside overallocated raw
     * storage (or one-past): isolates the index range check. Never dereference. */
    CHECK(pool_free(p, (void *)(base + 3 * 32)) == POOL_ERR_OUTSIDE);
    CHECK(pool_free(p, (unsigned char *)ptrs[0] + 1) == POOL_ERR_NOT_START);
    CHECK(pool_free(p, (unsigned char *)ptrs[0] + 16) == POOL_ERR_NOT_START);
    int stack_object = 0;
    CHECK(pool_free(p, &stack_object) != POOL_OK);
    void *heap_object = malloc(64);
    CHECK(heap_object != NULL);
    CHECK(pool_free(p, heap_object) != POOL_OK);
    free(heap_object);
    void *foreign = pool_alloc(q);
    CHECK(foreign != NULL);
    CHECK(pool_free(p, foreign) != POOL_OK);
    CHECK(pool_check(p) && pool_check(q));
    CHECK(pool_alloc(p) == NULL);
    for (size_t i = 0; i < 3; ++i) CHECK(pool_free(p, ptrs[i]) == POOL_OK);
    CHECK(pool_free(q, foreign) == POOL_OK);
    CHECK(pool_check(p) && pool_check(q));
    pool_destroy(q);
    pool_destroy(p);
}

static void single_slot_and_destroy(void) {
    for (size_t i = 0; i < 1000; ++i) {
        pool_t *p = pool_create(7, 1, 128);
        CHECK(p != NULL);
        void *x = pool_alloc(p);
        CHECK(x != NULL);
        CHECK(pool_alloc(p) == NULL);
        CHECK(pool_free(p, x) == POOL_OK);
        CHECK(pool_check(p));
        CHECK(pool_alloc(p) == x);
        pool_destroy(p); /* No requirement to return slots first. */
    }
}

static unsigned char canary(size_t idx, uint32_t generation, size_t byte) {
    return (unsigned char)(idx * 19 + generation * 37 + byte * 13);
}

static void check_canary(const void *ptr, size_t idx, uint32_t gen) {
    for (size_t b = 0; b < 37; ++b)
        CHECK(((const unsigned char *)ptr)[b] == canary(idx, gen, b));
}

static void random_differential(void) {
    enum { N = 31, STEPS = 1000000 };
    void *slots[N];
    unsigned char live[N] = {0};
    uint32_t generations[N] = {0};
    uint32_t rng = seed;
    size_t live_count = 0, allocs = 0, frees = 0, exhausted = 0;
    pool_t *p = pool_create(37, N, 16);
    CHECK(p != NULL);
    for (size_t i = 0; i < N; ++i) {
        slots[i] = pool_alloc(p);
        CHECK(slots[i] != NULL);
        for (size_t j = 0; j < i; ++j) CHECK(slots[i] != slots[j]);
    }
    CHECK(pool_alloc(p) == NULL);
    for (size_t i = 0; i < N; ++i) CHECK(pool_free(p, slots[i]) == POOL_OK);
    printf("random seed=0x%08" PRIx32 " steps=%d slots=%d\n", seed, STEPS, N);
    fflush(stdout);
    for (size_t step = 0; step < STEPS; ++step) {
        if ((random32(&rng) & 1) == 0) {
            void *x = pool_alloc(p);
            if (live_count == N) {
                CHECK(x == NULL);
                ++exhausted;
            } else {
                CHECK(x != NULL);
                size_t idx = 0;
                while (idx < N && slots[idx] != x) ++idx;
                CHECK(idx < N);
                CHECK(!live[idx]);
                live[idx] = 1;
                ++generations[idx];
                ++live_count;
                ++allocs;
                for (size_t b = 0; b < 37; ++b)
                    ((unsigned char *)x)[b] = canary(idx, generations[idx], b);
            }
        } else {
            size_t idx = random32(&rng) % N;
            if (live[idx]) {
                check_canary(slots[idx], idx, generations[idx]);
                CHECK(pool_free(p, slots[idx]) == POOL_OK);
                live[idx] = 0;
                --live_count;
                ++frees;
            } else {
                CHECK(pool_free(p, NULL) == POOL_OK);
            }
        }
        CHECK(pool_check(p)); /* Every step, no sampling. */
        if (step % 1024 == 0)
            for (size_t i = 0; i < N; ++i)
                if (live[i]) check_canary(slots[i], i, generations[i]);
    }
    for (size_t i = 0; i < N; ++i) {
        if (live[i]) {
            check_canary(slots[i], i, generations[i]);
            CHECK(pool_free(p, slots[i]) == POOL_OK);
        }
    }
    CHECK(pool_check(p));
    CHECK(allocs > 100000 && frees > 100000 && exhausted > 0);
    printf("oracle allocs=%zu frees=%zu exhausted=%zu remaining=%zu\n",
           allocs, frees, exhausted, live_count);
    pool_destroy(p);
}

int main(int argc, char **argv) {
    if (argc > 2) { fprintf(stderr, "usage: %s [nonzero-seed]\n", argv[0]); return 2; }
    if (argc == 2) {
        char *end;
        unsigned long value = strtoul(argv[1], &end, 0);
        if (!*argv[1] || *end || value == 0 || value > UINT32_MAX) return 2;
        seed = (uint32_t)value;
    }
    RUN(invalid_create);
    RUN(overflow_create);
    RUN(backing_failure);
    RUN(alignment_and_overlap);
    RUN(exhaustion_and_lifo);
    RUN(null_and_double_free);
    RUN(invalid_pointers);
    RUN(single_slot_and_destroy);
    RUN(random_differential);
    printf("TESTS: %u passed, 0 failed; assertions=%" PRIu64 "\n", tests, assertions);
    return 0;
}
