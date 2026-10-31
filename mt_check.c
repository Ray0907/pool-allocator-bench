#define _POSIX_C_SOURCE 200809L
#include "check_common.h"
#include "pool.h"
#include <pthread.h>

struct gate {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int ready;
    int go;
};

struct worker {
    struct gate *gate;
    size_t size;
    size_t pairs;
    int use_pool;
    uint64_t start;
    double ns_pair;
};

static void pthread_ok(int error, const char *operation)
{
    if (error != 0) {
        fprintf(stderr, "%s: %s\n", operation, strerror(error));
        exit(EXIT_FAILURE);
    }
}

void *mt_worker(void *argument)
{
    struct worker *w = argument;
    pool_t *pool = w->use_pool ? pool_create(w->size, 8, 16) : NULL;
    /* A stack-local sink avoids a shared cache line between worker sinks. */
    void *volatile sink = NULL;
    check_require(!w->use_pool || pool != NULL, "worker pool_create");
    pthread_ok(pthread_mutex_lock(&w->gate->mutex), "worker lock");
    ++w->gate->ready;
    pthread_ok(pthread_cond_broadcast(&w->gate->condition), "ready broadcast");
    while (!w->gate->go)
        pthread_ok(pthread_cond_wait(&w->gate->condition, &w->gate->mutex), "worker wait");
    pthread_ok(pthread_mutex_unlock(&w->gate->mutex), "worker unlock");
    uint64_t start = check_ticks();
    for (size_t i = 0; i < w->pairs; ++i) {
        void *p = pool ? pool_alloc(pool) : malloc(w->size);
        check_require(p != NULL, "worker allocation");
        sink = p;
        if (pool) check_require(pool_free(pool, p) == 0, "worker pool_free");
        else free(p);
    }
    uint64_t end = check_ticks();
    w->start = start;
    w->ns_pair = check_ns(end - start) / (double)w->pairs;
    (void)sink;
    pool_destroy(pool);
    return NULL;
}

static void cell(size_t size, int use_pool, int threads, size_t pairs, int reps)
{
    double means[5], slowest[5];
    const char *allocator = use_pool ? "pool" : "malloc";
    for (int rep = 0; rep <= reps; ++rep) {
        struct gate gate;
        struct worker workers[8];
        pthread_t handles[8];
        double sum = 0, maximum = 0;
        uint64_t first, last;
        long faults = check_faults();
        gate.ready = 0;
        gate.go = 0;
        pthread_ok(pthread_mutex_init(&gate.mutex, NULL), "mutex init");
        pthread_ok(pthread_cond_init(&gate.condition, NULL), "condition init");
        for (int i = 0; i < threads; ++i) {
            workers[i].gate = &gate;
            workers[i].size = size;
            workers[i].pairs = pairs;
            workers[i].use_pool = use_pool;
            pthread_ok(pthread_create(&handles[i], NULL, mt_worker, &workers[i]), "pthread_create");
        }
        pthread_ok(pthread_mutex_lock(&gate.mutex), "main lock");
        while (gate.ready != threads)
            pthread_ok(pthread_cond_wait(&gate.condition, &gate.mutex), "main wait");
        gate.go = 1;
        pthread_ok(pthread_cond_broadcast(&gate.condition), "start broadcast");
        pthread_ok(pthread_mutex_unlock(&gate.mutex), "main unlock");
        for (int i = 0; i < threads; ++i)
            pthread_ok(pthread_join(handles[i], NULL), "pthread_join");
        faults = check_faults() - faults;
        first = last = workers[0].start;
        for (int i = 0; i < threads; ++i) {
            double elapsed = workers[i].ns_pair;
            sum += elapsed;
            if (elapsed > maximum) maximum = elapsed;
            if (workers[i].start < first) first = workers[i].start;
            if (workers[i].start > last) last = workers[i].start;
        }
        double mean = sum / threads;
        printf("MT size=%zu allocator=%s threads=%d rep=%d phase=%s mean_ns=%.6f slowest_ns=%.6f ready=%d start_spread_ns=%.6f minor_faults=%ld thread_ns=",
               size, allocator, threads, rep, rep == 0 ? "warmup" : "measured",
               mean, maximum, gate.ready, check_ns(last - first), faults);
        for (int i = 0; i < threads; ++i)
            printf("%s%.6f", i ? "," : "", workers[i].ns_pair);
        putchar('\n');
        if (rep != 0) {
            means[rep - 1] = mean;
            slowest[rep - 1] = maximum;
        }
        pthread_ok(pthread_cond_destroy(&gate.condition), "condition destroy");
        pthread_ok(pthread_mutex_destroy(&gate.mutex), "mutex destroy");
    }
    printf("MT_SUMMARY size=%zu allocator=%s threads=%d median_mean_ns=%.6f median_slowest_ns=%.6f worst_mean_ns=%.6f worst_slowest_ns=%.6f\n",
           size, allocator, threads, check_median(means, reps), check_median(slowest, reps),
           check_max(means, reps), check_max(slowest, reps));
}

int main(int argc, char **argv)
{
    int smoke = check_smoke(argc, argv);
    int reps = smoke ? 1 : 5;
    size_t pairs = smoke ? 1000 : 1000000;
    const size_t sizes[] = {64, 40000};
    printf("CONFIG mode=%s reps=%d warmup=1 pairs=%zu\n", smoke ? "smoke" : "full", reps, pairs);
    check_clock();
    check_machine();
    puts("Unpinned microbenchmark, one machine; threads not pinned: performance/efficiency core placement changes results.");
    puts("No payload touch; one private pool and stack-local volatile sink per thread; no sharing or cross-thread free.");
    puts("Start gate: all ready under mutex before go+broadcast; start_spread_ns measures scheduler skew, not guaranteed simultaneity.");
    puts("rep=0 warmup excluded; mean/slowest are per-thread ns/pair, not aggregate throughput.");
    puts("minor_faults=getrusage(RUSAGE_SELF) whole-rep delta including thread/pool setup and teardown.");
    puts("Only slowdown pattern measured; a lock inside libmalloc is a possible cause NOT proven here.");
    puts("Batch averages are not per-pair quantiles; individual p50 below one tick would be quantized.");
    for (size_t k = 0; k < sizeof sizes / sizeof *sizes; ++k)
        for (int use_pool = 0; use_pool <= 1; ++use_pool)
            for (int threads = 1; threads <= 8; threads *= 2)
                cell(sizes[k], use_pool, threads, pairs, reps);
    return 0;
}
