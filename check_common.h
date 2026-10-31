#ifndef CHECK_COMMON_H
#define CHECK_COMMON_H

#ifdef __APPLE__
/* POSIX feature selection otherwise hides Darwin sysctl and rusage fields. */
#define _DARWIN_C_SOURCE
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#endif

static double check_tick_ns = 1.0;
static double check_granularity_ns = 1.0;

static inline void check_require(int ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "experiment failed: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static inline uint64_t check_ticks(void)
{
#ifdef __APPLE__
    return mach_absolute_time();
#else
    struct timespec ts;
    check_require(clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "clock_gettime");
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
#endif
}

static inline double check_ns(uint64_t ticks)
{
    return (double)ticks * check_tick_ns;
}

static inline void check_clock(void)
{
#ifdef __APPLE__
    mach_timebase_info_data_t tb;
    check_require(mach_timebase_info(&tb) == KERN_SUCCESS && tb.denom != 0,
                  "mach_timebase_info");
    check_tick_ns = (double)tb.numer / tb.denom;
    check_granularity_ns = check_tick_ns;
    printf("CLOCK name=mach_absolute_time tick_ns=%.9f granularity_ns=%.9f\n",
           check_tick_ns, check_granularity_ns);
#else
    struct timespec resolution;
    check_require(clock_getres(CLOCK_MONOTONIC, &resolution) == 0, "clock_getres");
    check_granularity_ns = (double)resolution.tv_sec * 1e9 + resolution.tv_nsec;
    printf("CLOCK name=clock_gettime(CLOCK_MONOTONIC) tick_ns=%.9f granularity_ns=%.9f\n",
           check_tick_ns, check_granularity_ns);
#endif
}

static inline long check_faults(void)
{
    struct rusage usage;
    check_require(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage");
    return usage.ru_minflt;
}

static inline void check_machine(void)
{
    struct utsname info;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    check_require(cpus > 0 && uname(&info) == 0, "machine facts");
    printf("MACHINE os=%s arch=%s logical_cpus=%ld", info.sysname, info.machine, cpus);
#ifdef __APPLE__
    const char *keys[] = {"hw.perflevel0.logicalcpu", "hw.perflevel1.logicalcpu"};
    const char *labels[] = {"performance_cpus", "efficiency_cpus"};
    for (size_t i = 0; i < 2; ++i) {
        int count;
        size_t length = sizeof count;
        if (sysctlbyname(keys[i], &count, &length, NULL, 0) == 0)
            printf(" %s=%d", labels[i], count);
        else
            printf(" %s=unavailable", labels[i]);
    }
#else
    printf(" performance_cpus=unavailable efficiency_cpus=unavailable");
#endif
    putchar('\n');
}

static inline int check_smoke(int argc, char **argv)
{
    check_require(argc == 1 || (argc == 2 && strcmp(argv[1], "--smoke") == 0),
                  "usage: program [--smoke]");
    return argc == 2;
}

static inline int check_cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static inline double check_median(const double *values, int count)
{
    double sorted[5];
    check_require(count == 1 || count == 5, "median repetition count");
    memcpy(sorted, values, (size_t)count * sizeof *values);
    qsort(sorted, (size_t)count, sizeof *sorted, check_cmp_double);
    return sorted[count / 2];
}

static inline double check_max(const double *values, int count)
{
    double maximum = values[0];
    for (int i = 1; i < count; ++i)
        if (values[i] > maximum) maximum = values[i];
    return maximum;
}

static inline void check_latency(const char *name, double ns)
{
    printf(" %s=", name);
    if (ns < check_granularity_ns) printf("<tick");
    else printf("%.6f", ns);
}

#endif
