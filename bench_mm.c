#define _POSIX_C_SOURCE 200809L
#include "check_common.h"
#include "mymalloc.h"
#include "pool.h"
#include "trace_mm.h"
#include <pthread.h>

enum { SYSTEM, EXPLICIT, WRAPPER, RAW };
static const char *names[] = {"malloc","mm","wrapper","pool"};
struct arena { int kind; mm_t *mm; pool_t *pool; };
static void *allocate(struct arena *a, size_t n) {
    switch (a->kind) {
        case SYSTEM: return malloc(n);
        case EXPLICIT: return mm_alloc(a->mm, n);
        case WRAPPER: return myMalloc(n);
        default: return pool_alloc(a->pool);
    }
}
static void release(struct arena *a, void *p) {
    switch (a->kind) {
        case SYSTEM: free(p); break;
        case EXPLICIT: check_require(mm_free(a->mm, p) == MM_OK, "mm_free"); break;
        case WRAPPER: myFree(p); break;
        default: check_require(pool_free(a->pool, p) == POOL_OK, "pool_free"); break;
    }
}
static void payload(void *p, size_t n) {
    volatile unsigned char *b = p;
    for (size_t i = 0; i < n; i += 64) b[i] = (unsigned char)i;
    b[n - 1] = 1;
}
/* External linkage: keep timed loops visible to assembly inspection. */
double bench_mm_pairs(struct arena *a, size_t n, size_t pairs, int touch) {
    uintptr_t volatile sink = 0;
    uint64_t start = check_ticks();
    for (size_t i = 0; i < pairs; ++i) {
        void *p = allocate(a, n);
        check_require(p != NULL, "pair allocation");
        sink = (uintptr_t)p;
        if (touch) payload(p, n);
        release(a, p);
    }
    check_require(sink != 0, "volatile sink");
    return check_ns(check_ticks() - start) / (double)pairs;
}
double bench_mm_trace(struct arena *a, const mm_trace *t, void **slots, size_t rounds, int touch) {
    uintptr_t volatile sink = 0;
    uint64_t start = check_ticks();
    for (size_t r = 0; r < rounds; ++r) {
        for (size_t i = 0; i < t->count; ++i) {
            slots[i] = allocate(a, t->sizes[i]);
            check_require(slots[i] != NULL, "trace allocation");
            sink = (uintptr_t)slots[i];
            if (touch) payload(slots[i], t->sizes[i]);
        }
        for (size_t i = 0; i < t->count; ++i) release(a, slots[t->order[i]]);
    }
    check_require(sink != 0, "trace sink");
    return check_ns(check_ticks() - start) / (double)(t->count * rounds);
}
static struct arena create_arena(int kind, size_t size, const mm_trace *t) {
    struct arena a = {kind, NULL, NULL};
    if (kind == EXPLICIT) {
        mm_config cfg = mm_default_config;
        if (t != NULL) for (size_t i = 0; i < 9; ++i)
            cfg.capacities[i] = t->counts[i] ? t->counts[i] : 1;
        a.mm = mm_create(&cfg);
        check_require(a.mm != NULL, "mm_create");
    } else if (kind == RAW) {
        a.pool = pool_create(size, 8, 16);
        check_require(a.pool != NULL, "pool_create");
    } else if (kind == WRAPPER) {
        void *p = myMalloc(size); check_require(p != NULL, "default warmup"); myFree(p);
    }
    return a;
}
static void destroy_arena(struct arena *a) {
    if (a->kind == WRAPPER) myMalloc_shutdown();
    mm_destroy(a->mm); pool_destroy(a->pool);
}
struct gate { pthread_mutex_t mutex; pthread_cond_t cond; int ready, go; };
struct worker {
    struct gate *gate;
    const mm_trace *trace;
    int kind, touch;
    size_t size, pairs, rounds;
    double ns;
    uint64_t start;
};
static void *worker_run(void *arg) {
    struct worker *w = arg;
    struct arena a = create_arena(w->kind, w->size, w->trace);
    void **slots = w->trace ? malloc(w->trace->count * sizeof *slots) : NULL;
    check_require(!w->trace || slots != NULL, "trace slots");
    /* Warm each thread's actual storage, not just a different instance. */
    if (w->trace) (void)bench_mm_trace(&a, w->trace, slots, 1, w->touch);
    else (void)bench_mm_pairs(&a, w->size, 1000, w->touch);
    check_require(pthread_mutex_lock(&w->gate->mutex) == 0, "gate lock");
    ++w->gate->ready;
    check_require(pthread_cond_broadcast(&w->gate->cond) == 0, "ready broadcast");
    while (!w->gate->go)
        check_require(pthread_cond_wait(&w->gate->cond, &w->gate->mutex) == 0, "gate wait");
    check_require(pthread_mutex_unlock(&w->gate->mutex) == 0, "gate unlock");
    w->start = check_ticks();
    w->ns = w->trace ? bench_mm_trace(&a, w->trace, slots, w->rounds, w->touch)
                     : bench_mm_pairs(&a, w->size, w->pairs, w->touch);
    free(slots); destroy_arena(&a);
    return NULL;
}
static void mt(int kind, size_t size, const mm_trace *t, int threads, int touch,
               size_t pairs, size_t rounds, int rep) {
    struct gate g = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0};
    pthread_t ids[8]; struct worker w[8];
    for (int i = 0; i < threads; ++i) {
        w[i] = (struct worker){&g,t,kind,touch,size,pairs,rounds,0,0};
        check_require(pthread_create(&ids[i], NULL, worker_run, &w[i]) == 0, "pthread_create");
    }
    check_require(pthread_mutex_lock(&g.mutex) == 0, "main gate lock");
    while (g.ready != threads)
        check_require(pthread_cond_wait(&g.cond, &g.mutex) == 0, "main gate wait");
    g.go = 1; check_require(pthread_cond_broadcast(&g.cond) == 0, "start broadcast");
    check_require(pthread_mutex_unlock(&g.mutex) == 0, "main gate unlock");
    double sum = 0, worst = 0; uint64_t lo = UINT64_MAX, hi = 0;
    for (int i = 0; i < threads; ++i) {
        check_require(pthread_join(ids[i], NULL) == 0, "pthread_join");
        sum += w[i].ns; if (w[i].ns > worst) worst = w[i].ns;
        if (w[i].start < lo) lo = w[i].start;
        if (w[i].start > hi) hi = w[i].start;
    }
    printf("CELL workload=%s size=%zu threads=%d touch=%d allocator=%s rep=%d phase=%s ns_pair=%.6f slowest_ns=%.6f ready=%d start_spread_ns=%.6f\n",
           t ? "mixed-mt" : "pair-mt", size, threads, touch, names[kind], rep,
           rep == 0 ? "warmup" : "measured", sum / threads, worst, g.ready, check_ns(hi - lo));
    check_require(pthread_cond_destroy(&g.cond) == 0 && pthread_mutex_destroy(&g.mutex) == 0, "gate destroy");
}
int main(int argc, char **argv) {
    int smoke = check_smoke(argc, argv), reps = smoke ? 1 : 5;
    size_t pairs = smoke ? 1000 : 200000, rounds = smoke ? 1 : 2;
    mm_trace trace;
    check_require(mm_trace_create(&trace, smoke ? 128 : 16384, UINT32_C(0x70ace123)), "trace create");
    check_machine(); check_clock();
    printf("CONFIG synthetic trace, not a real workload; reps=%d warmup=1 pairs=%zu rounds=%zu live=%zu live_bytes=%zu seed=0x70ace123 threads=unpinned\n",
           reps, pairs, rounds, trace.count, trace.bytes);
    puts("TOUCH volatile byte store per 64 bytes and final byte; setup outside timing; MT ns_pair=mean worker ns/pair, not throughput.");
    puts("RANGE 16,64,256,4096,40000 only; deliberate maximum, no fallback benchmark.");
    for (size_t i = 0; i < 9; ++i)
        printf("TRACE class=%zu peak=%zu percentile=100\n", mm_default_config.sizes[i], trace.counts[i]);
    const size_t sizes[] = {16,64,256,4096,40000};
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; ++s) for (int touch = 0; touch < 2; ++touch) {
        for (int rep = 0; rep <= reps; ++rep) {
            /* Rotate allocator order each rep to reduce fixed-order drift. */
            for (int k = 0; k < 4; ++k) {
                int kind = (k + rep) % 4;
                struct arena a = create_arena(kind, sizes[s], NULL);
                (void)bench_mm_pairs(&a, sizes[s], 1000, touch);
                double ns = bench_mm_pairs(&a, sizes[s], pairs, touch);
                printf("CELL workload=pair size=%zu threads=1 touch=%d allocator=%s rep=%d phase=%s ns_pair=%.6f\n",
                       sizes[s], touch, names[kind], rep, rep ? "measured" : "warmup", ns);
                destroy_arena(&a);
            }
        }
    }
    void **slots = malloc(trace.count * sizeof *slots);
    check_require(slots != NULL, "main slots");
    for (int touch = 0; touch < 2; ++touch) for (int rep = 0; rep <= reps; ++rep) {
        for (int k = 0; k < 2; ++k) {
            int kind = (k + rep) % 2;
            struct arena a = create_arena(kind, 0, &trace);
            (void)bench_mm_trace(&a, &trace, slots, 1, touch);
            double ns = bench_mm_trace(&a, &trace, slots, rounds, touch);
            printf("CELL workload=mixed size=0 threads=1 touch=%d allocator=%s rep=%d phase=%s ns_pair=%.6f\n",
                   touch, names[kind], rep, rep ? "measured" : "warmup", ns);
            destroy_arena(&a);
        }
    }
    free(slots);
    const size_t mt_sizes[] = {64,40000,0};
    for (int threads = 1; threads <= 8; threads *= 2)
        for (size_t s = 0; s < 3; ++s) for (int touch = 0; touch < 2; ++touch)
            for (int rep = 0; rep <= reps; ++rep) for (int k = 0; k < 2; ++k) {
                int kind = (k + rep) % 2;
                mt(kind, mt_sizes[s], s == 2 ? &trace : NULL, threads, touch, pairs, rounds, rep);
            }
    mm_trace_destroy(&trace);
    return 0;
}
