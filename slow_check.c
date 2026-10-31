#define _POSIX_C_SOURCE 200809L
#include "check_common.h"
#include "pool.h"

static void *volatile sink;

/* External linkage leaves an isolated timed-loop body for the assembly check. */
double slow_pairs(size_t size, pool_t *pool, size_t pairs)
{
    uint64_t start = check_ticks();
    for (size_t i = 0; i < pairs; ++i) {
        void *p = pool ? pool_alloc(pool) : malloc(size);
        check_require(p != NULL, "allocation");
        sink = p;
        if (pool) check_require(pool_free(pool, p) == 0, "pool_free");
        else free(p);
    }
    return check_ns(check_ticks() - start) / (double)pairs;
}

static int compare_ticks(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void batch(size_t size, int use_pool, size_t pairs, int reps)
{
    pool_t *pool = use_pool ? pool_create(size, 8, 16) : NULL;
    double measured[5];
    const char *allocator = use_pool ? "pool" : "malloc";
    check_require(!use_pool || pool != NULL, "pool_create");
    for (int rep = 0; rep <= reps; ++rep) {
        long faults = check_faults();
        double elapsed = slow_pairs(size, pool, pairs);
        faults = check_faults() - faults;
        printf("BATCH size=%zu allocator=%s rep=%d phase=%s ns_pair=%.6f minor_faults=%ld\n",
               size, allocator, rep, rep == 0 ? "warmup" : "measured", elapsed, faults);
        if (rep != 0) measured[rep - 1] = elapsed;
    }
    printf("BATCH_SUMMARY size=%zu allocator=%s median_ns=%.6f worst_ns=%.6f\n",
           size, allocator, check_median(measured, reps), check_max(measured, reps));
    pool_destroy(pool);
}

static void overhead(uint64_t *samples, size_t count, int reps)
{
    for (int rep = 1; rep <= reps; ++rep) {
        double sum = 0;
        for (size_t i = 0; i < count; ++i) {
            uint64_t start = check_ticks();
            uint64_t end = check_ticks();
            samples[i] = end - start;
            sum += check_ns(samples[i]);
        }
        qsort(samples, count, sizeof *samples, compare_ticks);
        printf("OVERHEAD rep=%d samples=%zu mean_ns=%.6f min_ticks=%llu p50_ticks=%llu",
               rep, count, sum / (double)count, (unsigned long long)samples[0],
               (unsigned long long)samples[count / 2]);
        check_latency("p50_ns", check_ns(samples[count / 2]));
        putchar('\n');
    }
}

static void tails(size_t size, int use_pool, uint64_t *samples, size_t count, int reps)
{
    pool_t *pool = use_pool ? pool_create(size, 8, 16) : NULL;
    const char *allocator = use_pool ? "pool" : "malloc";
    const char *names[] = {"p50", "p99", "p99_9", "p99_99", "max"};
    const size_t indices[] = {(count * 50 - 1) / 100, (count * 99 - 1) / 100,
                              (count * 999 - 1) / 1000, (count * 9999 - 1) / 10000,
                              count - 1};
    double values[5][5];
    check_require(!use_pool || pool != NULL, "pool_create");
    (void)slow_pairs(size, pool, 1000);
    for (int rep = 0; rep < reps; ++rep) {
        long faults = check_faults();
        for (size_t i = 0; i < count; ++i) {
            uint64_t start = check_ticks();
            void *p = pool ? pool_alloc(pool) : malloc(size);
            check_require(p != NULL, "tail allocation");
            sink = p;
            if (pool) check_require(pool_free(pool, p) == 0, "tail pool_free");
            else free(p);
            samples[i] = check_ticks() - start;
        }
        faults = check_faults() - faults;
        qsort(samples, count, sizeof *samples, compare_ticks);
        printf("TAIL size=%zu allocator=%s rep=%d minor_faults=%ld", size, allocator, rep + 1, faults);
        for (size_t k = 0; k < 5; ++k) {
            uint64_t value = samples[indices[k]];
            values[k][rep] = check_ns(value);
            printf(" %s_ticks=%llu", names[k], (unsigned long long)value);
        }
        putchar('\n');
    }
    printf("TAIL_SUMMARY size=%zu allocator=%s", size, allocator);
    const char *fields[] = {"p50_ns", "p99_ns", "p99_9_ns", "p99_99_ns", "worst_max_ns"};
    for (size_t k = 0; k < 5; ++k)
        check_latency(fields[k], k == 4 ? check_max(values[k], reps) : check_median(values[k], reps));
    putchar('\n');
    pool_destroy(pool);
}

int main(int argc, char **argv)
{
    int smoke = check_smoke(argc, argv);
    int reps = smoke ? 1 : 5;
    size_t pairs = smoke ? 1000 : 2000000;
    size_t count = smoke ? 2000 : 1000000;
    const size_t sizes[] = {16, 256, 4096, 40000, 262144, 1048576};
    uint64_t *samples = malloc(count * sizeof *samples);
    check_require(samples != NULL, "sample buffer");
    /* Pre-touch only the measurement buffer, never the allocated payload. */
    memset(samples, 0, count * sizeof *samples);
    printf("CONFIG mode=%s reps=%d warmup=1 pairs=%zu samples=%zu\n",
           smoke ? "smoke" : "full", reps, pairs, count);
    check_clock();
    check_machine();
    puts("No payload touch; pointer stored to a volatile sink; threads not pinned.");
    puts("Batch warmup rep=0 excluded; minor_faults=getrusage(RUSAGE_SELF) loop delta, excluding pool setup.");
    puts("Timer overhead is back-to-back reads in an empty loop; latency includes overhead, not subtracted.");
    puts("p50 below one tick is quantized, NOT a real zero-ns latency; <tick means below clock granularity.");
    puts("Raw TAIL fields are integer clock ticks; nearest-rank percentiles; summary=median of each percentile, worst max.");
    puts("Latency tails can be OS-noise dominated; max is not an allocator-only or real-time bound.");
    overhead(samples, smoke ? 1000 : 100000, reps);
    for (size_t k = 0; k < sizeof sizes / sizeof *sizes; ++k) {
        batch(sizes[k], 0, pairs, reps);
        batch(sizes[k], 1, pairs, reps);
    }
    for (size_t k = 0; k < 4; ++k) {
        tails(sizes[k], 0, samples, count, reps);
        tails(sizes[k], 1, samples, count, reps);
    }
    free(samples);
    return 0;
}
