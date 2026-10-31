/* Standalone requested-byte measurement; only util's pool object is wrapped. */
#include "pool.h"
#include "pool_util.h"
#include "test_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif

static size_t requested_bytes, live_blocks, current_size;

static void require(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "UTIL FAIL: %s\n", message); exit(1); }
}

/* Equivalent to the test allocator seam, without its artificial header/padding. */
void *q2_test_malloc(size_t n) {
    require(n <= SIZE_MAX - requested_bytes, "requested-byte counter overflow");
    void *p = malloc(n);
    require(p != NULL, "backing malloc");
    requested_bytes += n;
    ++live_blocks;
    printf("POOL_ALLOC,%zu,%zu\n", current_size, n);
    return p;
}

void q2_test_free(void *p) {
    if (p == NULL) return;
    require(live_blocks != 0, "unbalanced backing free");
    --live_blocks;
    free(p);
}

static void pool_utilization(size_t size) {
    const size_t nslots = 1000, alignment = 16;
    void *slots[1000];
    size_t stride = 0;
    require(live_blocks == 0, "previous pool leaked");
    requested_bytes = 0;
    current_size = size;
    require(align_up(size, alignment, &stride), "stride overflow");
    pool_t *p = pool_create(size, nslots, alignment);
    require(p != NULL, "pool_create");
    size_t total = requested_bytes;
    require(total >= nslots * stride, "measured total smaller than slot storage");
    for (size_t i = 0; i < nslots; ++i) {
        slots[i] = pool_alloc(p);
        require(slots[i] != NULL, "full-capacity allocation");
        require((uintptr_t)slots[i] % alignment == 0, "slot alignment");
    }
    require(pool_alloc(p) == NULL && pool_check(p), "full-capacity invariant");
    double bookkeeping = (double)(total - nslots * stride) / (double)nslots;
    printf("POOL,%zu,%zu,%zu,%zu,%zu,%zu,%.6f,%.3f\n",
           size, nslots, nslots, stride, size * nslots, total,
           100.0 * (double)(size * nslots) / (double)total, bookkeeping);
    for (size_t i = 100; i < nslots; ++i)
        require(pool_free(p, slots[i]) == POOL_OK, "free to 10% live");
    require(pool_check(p), "10%-live invariant");
    require(requested_bytes == total, "pool allocated after create");
    printf("POOL,%zu,%zu,100,%zu,%zu,%zu,%.6f,%.3f\n",
           size, nslots, stride, size * 100, total,
           100.0 * (double)(size * 100) / (double)total, bookkeeping);
    for (size_t i = 0; i < 100; ++i)
        require(pool_free(p, slots[i]) == POOL_OK, "free remaining slots");
    pool_destroy(p);
    require(live_blocks == 0, "pool_destroy leaked backing blocks");
}

int main(void) {
    const size_t sizes[] = {16, 64, 256, 4096, 40000};
    puts("POOL: total is measured backing-allocator requested bytes, not malloc's physical footprint.");
    puts("POOL: 10% live retains all preallocated storage (preallocation cost).");
    puts("POOL_ALLOC_HEADER,slot_size,requested_bytes");
    puts("POOL_HEADER,slot_size,nslots,live_slots,stride,payload_bytes,total_requested_bytes,utilization_pct,bookkeeping_bytes_per_slot");
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; ++i)
        pool_utilization(sizes[i]);
#ifdef __APPLE__
    puts("MALLOC: ROUNDING ONLY; libmalloc headers/metadata are not visible through malloc_size.");
#elif defined(__linux__)
    puts("MALLOC: ROUNDING ONLY; glibc hidden metadata is not visible through malloc_usable_size.");
#endif
#if defined(__APPLE__) || defined(__linux__)
    puts("MALLOC: lower bound on overhead, NOT total utilization; cannot compare total utilization with pool.");
    puts("MALLOC_ROUNDING_ONLY_HEADER,request_bytes,usable_bytes,rounding_waste_pct");
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; ++i) {
        void *p = malloc(sizes[i]);
        require(p != NULL, "malloc_size probe allocation");
#ifdef __APPLE__
        size_t usable = malloc_size(p);
#else
        size_t usable = malloc_usable_size(p);
#endif
        require(usable >= sizes[i], "malloc_size below requested size");
        printf("MALLOC_ROUNDING_ONLY,%zu,%zu,%.6f\n", sizes[i], usable,
               100.0 * (double)(usable - sizes[i]) / (double)usable);
        free(p);
    }
#else
    puts("MALLOC: NOT MEASURED");
#endif
    puts("UTIL: PASS");
    return 0;
}
