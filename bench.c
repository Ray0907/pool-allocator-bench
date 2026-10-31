#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "pool.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

enum { SYSTEM, POOL, REUSE };
enum { HOT, BATCH, MIXED };
static const char *methods[] = {"malloc", "pool", "reuse_not_allocator"};
static const char *patterns[] = {"hot_best_case", "batch1000_random_free", "mixed_large_live"};
static volatile uint64_t sink;
static size_t cache_line;
static const size_t BENCH_ALIGN = 16;

static void require(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "BENCH FAIL: %s\n", message); exit(1); }
}

static size_t measured_cache_line(void) {
    long line = 0;
#if defined(__APPLE__)
    size_t bytes = sizeof line;
    require(sysctlbyname("hw.cachelinesize", &line, &bytes, NULL, 0) == 0,
            "cache-line measurement");
#elif defined(__linux__)
    line = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
#endif
    require(line > 0 && ((size_t)line & ((size_t)line - 1)) == 0,
            "measured cache line must be a positive power of two");
    return (size_t)line;
}

static void check_malloc_alignment(void) {
    const size_t sizes[] = {1, 16, 64, 256, 4096, 40000};
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; ++i) {
        void *p = malloc(sizes[i]);
        require(p != NULL, "malloc alignment probe allocation");
        if ((uintptr_t)p % BENCH_ALIGN != 0) {
            fprintf(stderr, "BENCH FAIL: malloc(%zu) returned %p, not 16-byte aligned\n",
                    sizes[i], p);
            free(p);
            exit(1);
        }
        free(p);
    }
    puts("MALLOC_ALIGN16 ok");
}

static uint64_t now_ns(void) {
    struct timespec ts;
    require(clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "clock_gettime");
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static uint32_t random32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}

static void touch(void *ptr, size_t size, unsigned char tag) {
    volatile unsigned char *p = ptr;
    p[0] = tag;
    /* Cover physical cache lines even if malloc's pointer is not line aligned. */
    size_t offset = cache_line - (size_t)((uintptr_t)ptr % cache_line);
    while (offset < size) { p[offset] = tag; offset += cache_line; }
    sink += p[0];
}

typedef struct {
    int method;
    size_t size;
    pool_t *pool;
    void *reuse;
} allocator;

static void *allocate(allocator *a) {
    void *p;
    if (a->method == SYSTEM) p = malloc(a->size);
    else if (a->method == POOL) p = pool_alloc(a->pool);
    else p = a->reuse;
    require(p != NULL, "allocation failed/exhausted");
    return p;
}

static void release(allocator *a, void *ptr) {
    if (a->method == SYSTEM) free(ptr);
    else if (a->method == POOL)
        require(pool_free(a->pool, ptr) == POOL_OK, "pool_free");
}

static int compare_u64(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

typedef struct { uint64_t p50, p99, p999, max; } tails;

static tails quantiles(uint64_t *samples, size_t n) {
    require(n != 0, "empty latency samples");
    qsort(samples, n, sizeof *samples, compare_u64);
    tails t;
    /* Nearest-rank quantiles, including a well-defined maximum. */
    t.p50 = samples[(n * 50 + 99) / 100 - 1];
    t.p99 = samples[(n * 99 + 99) / 100 - 1];
    t.p999 = samples[(n * 999 + 999) / 1000 - 1];
    t.max = samples[n - 1];
    return t;
}

static void timer_overhead(size_t reps, size_t n) {
    uint64_t *samples = malloc(n * sizeof *samples);
    require(samples != NULL, "timer samples malloc");
    puts("TIMER_HEADER,rep,samples,p50_ns,p99_ns,p999_ns,max_ns");
    for (size_t rep = 0; rep < reps; ++rep) {
        for (size_t i = 0; i < n; ++i) {
            uint64_t begin = now_ns();
            uint64_t end = now_ns();
            samples[i] = end - begin;
        }
        tails t = quantiles(samples, n);
        printf("TIMER,%zu,%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
               rep + 1, n, t.p50, t.p99, t.p999, t.max);
    }
    free(samples);
}

static void cold_start(size_t size, size_t target) {
    size_t n = (target + size - 1) / size;
    uint64_t start = now_ns();
    pool_t *p = pool_create(size, n, BENCH_ALIGN);
    uint64_t create = now_ns() - start;
    require(p != NULL, "cold pool_create");
    start = now_ns();
    for (size_t i = 0; i < n; ++i) touch(allocate(&(allocator){POOL, size, p, NULL}), size, 0x5a);
    uint64_t elapsed = now_ns() - start;
    require(pool_check(p), "cold pool_check");
    puts("COLD_HEADER,size,slots,payload_bytes,create_ns,first_alloc_touch_ns");
    printf("COLD,%zu,%zu,%zu,%" PRIu64 ",%" PRIu64 "\n",
           size, n, n * size, create, elapsed);
    pool_destroy(p);
}

/* Timed batch and per-call phases use the SAME operation sequence and seed.
 * No allocation of measurement arrays, sorting, pool creation/destruction or
 * initial live-set fill is in a timed interval. Payload touch is inside batch
 * timing but OUTSIDE individual allocate/free call timing.
 */
typedef struct {
    allocator a;
    int pattern;
    size_t count, ops;
    void **live;
    size_t *order, *choices;
} workload;

static void *timed_allocate(allocator *a, uint64_t *sample) {
    uint64_t start = now_ns();
    void *ptr = allocate(a);
    *sample = now_ns() - start;
    return ptr;
}

static void timed_release(allocator *a, void *ptr, uint64_t *sample) {
    uint64_t start = now_ns();
    release(a, ptr);
    *sample = now_ns() - start;
}

static void drive(workload *w, size_t ops, int latency, uint64_t *at, uint64_t *ft) {
    require(w->pattern != BATCH || ops % w->count == 0, "batch size");
    if (w->pattern == BATCH) {
        for (size_t offset = 0; offset < ops; offset += w->count) {
            for (size_t j = 0; j < w->count; ++j) {
                w->live[j] = latency ? timed_allocate(&w->a, &at[offset+j]) : allocate(&w->a);
                touch(w->live[j], w->a.size, (unsigned char)(offset+j));
            }
            for (size_t j = 0; j < w->count; ++j) {
                void *ptr = w->live[w->order[j]];
                if (latency) timed_release(&w->a, ptr, &ft[offset+j]);
                else release(&w->a, ptr);
            }
        }
    } else {
        for (size_t i = 0; i < ops; ++i) {
            void *ptr = latency ? timed_allocate(&w->a, &at[i]) : allocate(&w->a);
            touch(ptr, w->a.size, (unsigned char)i);
            if (w->pattern == MIXED) {
                size_t idx = w->choices[i % w->ops];
                void *old = w->live[idx];
                w->live[idx] = ptr;
                ptr = old;
            }
            if (latency) timed_release(&w->a, ptr, &ft[i]);
            else release(&w->a, ptr);
        }
    }
}

static void prepare(workload *w, int method, int pattern, size_t size,
                    size_t count, size_t ops, uint32_t seed) {
    memset(w, 0, sizeof *w);
    w->a.method = method;
    w->a.size = size;
    w->pattern = pattern;
    w->count = count;
    w->ops = ops;
    w->live = malloc(count * sizeof *w->live);
    w->order = malloc(count * sizeof *w->order);
    w->choices = malloc(ops * sizeof *w->choices);
    require(w->live && w->order && w->choices, "workload arrays");
    if (method == POOL) {
        w->a.pool = pool_create(size, count + (pattern == MIXED), BENCH_ALIGN);
        require(w->a.pool != NULL, "pool_create");
    } else if (method == REUSE) {
        w->a.reuse = malloc(size);
        require(w->a.reuse != NULL, "reuse malloc");
    }
    for (size_t i = 0; i < count; ++i) w->order[i] = i;
    uint32_t rng = seed;
    for (size_t i = count; i > 1; --i) {
        size_t j = random32(&rng) % i, temp = w->order[i - 1];
        w->order[i - 1] = w->order[j];
        w->order[j] = temp;
    }
    for (size_t i = 0; i < ops; ++i) w->choices[i] = random32(&rng) % count;
    if (pattern == MIXED) {
        for (size_t i = 0; i < count; ++i) {
            w->live[i] = allocate(&w->a);
            touch(w->live[i], size, (unsigned char)i);
        }
    }
}

static void finish(workload *w) {
    if (w->pattern == MIXED)
        for (size_t i = 0; i < w->count; ++i) release(&w->a, w->live[i]);
    if (w->a.pool) require(pool_check(w->a.pool), "post-benchmark pool_check");
    pool_destroy(w->a.pool);
    free(w->a.reuse);
    free(w->choices);
    free(w->order);
    free(w->live);
}

static void cell(int method, int pattern, size_t size, size_t count,
                 size_t ops, size_t rep, uint32_t seed) {
    uint64_t *at = malloc(ops * sizeof *at), *ft = malloc(ops * sizeof *ft);
    require(at && ft, "latency arrays");
    workload w;
    size_t warmup = 1000;
    prepare(&w, method, pattern, size, count, ops, seed);
    drive(&w, warmup, 0, NULL, NULL);
    uint64_t begin = now_ns();
    drive(&w, ops, 0, NULL, NULL);
    uint64_t batch_ns = now_ns() - begin;
    finish(&w);

    /* Recreate a fresh workload so batch timing does not change latency-phase
     * object lifetime history. Identical seed/order for malloc and pool. */
    prepare(&w, method, pattern, size, count, ops, seed);
    drive(&w, warmup, 0, NULL, NULL);
    drive(&w, ops, 1, at, ft);
    finish(&w);
    tails a = quantiles(at, ops), f = quantiles(ft, ops);
    printf("RAW,%zu,%s,%s,%zu,0x%08" PRIx32 ",%zu,%zu,%" PRIu64 ",%.6f,%.6f,"
           "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
           "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
           size, patterns[pattern], methods[method], rep, seed, ops, count * size,
           batch_ns, (double)batch_ns / (2.0 * ops), 2000.0 * ops / batch_ns,
           a.p50, a.p99, a.p999, a.max, f.p50, f.p99, f.p999, f.max);
    fflush(stdout);
    free(ft);
    free(at);
}

int main(int argc, char **argv) {
    int smoke = 0;
    if (argc == 2 && strcmp(argv[1], "--smoke") == 0) smoke = 1;
    else if (argc != 1) { fprintf(stderr, "usage: %s [--smoke]\n", argv[0]); return 2; }
    cache_line = measured_cache_line();
    check_malloc_alignment();
    size_t reps = smoke ? 1 : 5, ops = smoke ? 1000 : 20000;
    size_t target = smoke ? 1024 * 1024 : 64 * 1024 * 1024;
    const size_t sizes[] = {40000, 16, 64, 256, 4096};
    const uint32_t seed = UINT32_C(0xbadc0ffe);
    printf("CONFIG smoke=%d reps=%zu pairs_per_cell=%zu cache_line=%zu align=%zu "
           "mixed_target_bytes=%zu base_seed=0x%08" PRIx32 "\n",
           smoke, reps, ops, cache_line, BENCH_ALIGN, target, seed);
    puts("NOTE hot_best_case is hot LIFO; reuse_not_allocator does NOT preserve independent live objects.");
    puts("NOTE batch ns/op includes touches/bookkeeping; individual tails are uncorrected for timer/dispatch overhead.");
    puts("NOTE p99 tails include timer/scheduling noise; no CPU affinity is set by this helper.");
    struct timespec resolution;
    require(clock_getres(CLOCK_MONOTONIC, &resolution) == 0, "clock_getres");
    uint64_t minimum = UINT64_MAX, previous = now_ns();
    size_t zero_deltas = 0;
    for (size_t i = 0; i < 100000; ++i) {
        uint64_t current = now_ns(), delta = current - previous;
        if (delta == 0) ++zero_deltas;
        else if (delta < minimum) minimum = delta;
        previous = current;
    }
    printf("CLOCK resolution_ns=%" PRIu64 " observed_min_positive_ns=%" PRIu64
           " zero_deltas=%zu probes=100000\n",
           (uint64_t)resolution.tv_sec * UINT64_C(1000000000) + (uint64_t)resolution.tv_nsec,
           minimum, zero_deltas);
    timer_overhead(reps, ops);
    cold_start(40000, target);
    puts("RAW_HEADER,size,pattern,method,rep,seed,pairs,live_payload_bytes,batch_ns,ns_per_op,mops_s,"
         "alloc_p50_ns,alloc_p99_ns,alloc_p999_ns,alloc_max_ns,free_p50_ns,free_p99_ns,free_p999_ns,free_max_ns");
    for (size_t s = 0; s < sizeof sizes / sizeof *sizes; ++s) {
        for (int pattern = HOT; pattern <= MIXED; ++pattern) {
            size_t count = pattern == HOT ? 1 : pattern == BATCH ? 1000 : (target + sizes[s] - 1) / sizes[s];
            for (size_t rep = 0; rep < reps; ++rep) {
                /* Rotate method order to limit systematic thermal/order bias. */
                for (int order = 0; order < 3; ++order) {
                    int method = (order + (int)rep) % 3;
                    cell(method, pattern, sizes[s], count, ops, rep + 1, seed + (uint32_t)rep);
                }
            }
        }
    }
    printf("SINK=%" PRIu64 "\n", sink);
    puts("BENCH: PASS");
    return 0;
}
