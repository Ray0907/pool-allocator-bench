/* Public API tests: no allocator internals. */
#include "mymalloc.h"
#include "pool.h"
#ifdef MM_SHIM
#include "mm_test_alloc.h"
#endif
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
static const size_t sizes[9] = {16,32,64,128,256,512,1024,4096,40000};
static uint32_t random32(uint32_t *s) {
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static mm_config tiny(void) {
    mm_config c = mm_default_config;
    for (size_t i = 0; i < c.nclasses; ++i) c.capacities[i] = 3;
    return c;
}
static size_t which(size_t n) {
    for (size_t i = 0; i < 9; ++i) if (n <= sizes[i]) return i;
    return 9;
}
static void canary(const void *p, size_t n, unsigned char value) {
    for (size_t i = 0; i < n; ++i) CHECK(((const unsigned char *)p)[i] == value);
}
static void contains_edges(void) {
    pool_t *p = pool_create(32, 3, 16);
    CHECK(p != NULL);
    void *a = pool_alloc(p), *b = pool_alloc(p), *c = pool_alloc(p);
    uintptr_t base = (uintptr_t)a;
    CHECK(pool_contains(NULL, a) == 0 && pool_contains(p, NULL) == 0);
    CHECK(pool_contains(p, a) == 1 && pool_contains(p, b) == 1);
    CHECK(pool_contains(p, c) == 1);
    CHECK(pool_contains(p, (void *)(base + 95)) == 1);
    CHECK(pool_contains(p, (void *)(base + 96)) == 0);
    CHECK(pool_contains(p, (void *)(base - 1)) == 0);
    CHECK(pool_contains(p, (void *)UINTPTR_MAX) == 0);
    CHECK(pool_free(p, a) == 0 && pool_contains(p, a) == 1);
    CHECK(pool_check(p));
    pool_destroy(p);
}
static void boundaries(void) {
    const size_t requests[] = {0,1,15,16,17,31,32,33,63,64,65,127,128,129,
        255,256,257,511,512,513,1023,1024,1025,4095,4096,4097,39999,40000,40001,SIZE_MAX};
    for (size_t k = 0; k < sizeof requests / sizeof *requests; ++k) {
        mm_config c = tiny();
        for (size_t i = 0; i < 9; ++i) c.capacities[i] = 1;
        mm_t *m = mm_create(&c);
        CHECK(m != NULL);
        size_t n = requests[k], cl = which(n);
        void *p = mm_alloc(m, n);
        if (n == 0 || cl == 9) CHECK(p == NULL);
        else {
            CHECK(p != NULL && (uintptr_t)p % 16 == 0);
            memset(p, 0xa7, n);
            /* An independent class-size request must hit this exhausted class. */
            CHECK(mm_alloc(m, sizes[cl]) == NULL);
            canary(p, n, 0xa7);
            CHECK(mm_free(m, p) == MM_OK);
        }
        CHECK(mm_check(m));
        mm_destroy(m);
    }
}
static void exhaustion_and_free(void) {
    mm_config c = tiny();
    mm_t *m = mm_create(&c);
    void *p[9][3];
    CHECK(m != NULL);
    CHECK(mm_free(NULL, NULL) == MM_OK && mm_free(m, NULL) == MM_OK);
    CHECK(mm_alloc(NULL, 16) == NULL && !mm_check(NULL));
    int foreign = 0;
    CHECK(mm_free(NULL, &foreign) == MM_ERR_OUTSIDE);
    CHECK(mm_free(m, &foreign) == MM_ERR_OUTSIDE);
    void *heap = malloc(123);
    CHECK(heap != NULL && mm_free(m, heap) == MM_ERR_OUTSIDE);
    free(heap);
    for (size_t i = 0; i < 9; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            p[i][j] = mm_alloc(m, sizes[i]);
            CHECK(p[i][j] != NULL && (uintptr_t)p[i][j] % 16 == 0);
            memset(p[i][j], (int)(i * 3 + j + 1), sizes[i]);
            for (size_t a = 0; a <= i; ++a) for (size_t b = 0; b < 3; ++b) {
                if (a == i && b >= j) break;
                uintptr_t x = (uintptr_t)p[i][j], y = (uintptr_t)p[a][b];
                CHECK(x >= y ? x - y >= sizes[a] : y - x >= sizes[i]);
            }
        }
        CHECK(mm_alloc(m, sizes[i]) == NULL); /* no fall-through */
        CHECK(mm_check(m));
        CHECK(mm_free(m, (void *)((uintptr_t)p[i][0] - 1)) == MM_ERR_OUTSIDE);
        CHECK(mm_free(m, (void *)((uintptr_t)p[i][0] + 1)) == MM_ERR_NOT_START);
        CHECK(mm_free(m, (void *)((uintptr_t)p[i][2] + sizes[i])) == MM_ERR_OUTSIDE);
        CHECK(mm_free(m, p[i][2]) == MM_OK); /* last slot */
        CHECK(mm_free(m, p[i][2]) == MM_ERR_DOUBLE_FREE);
        CHECK(mm_alloc(m, sizes[i]) == p[i][2]);
    }
    for (size_t i = 0; i < 9; ++i) for (size_t j = 0; j < 3; ++j) {
        canary(p[i][j], sizes[i], (unsigned char)(i * 3 + j + 1));
        CHECK(mm_free(m, p[i][j]) == MM_OK);
    }
    CHECK(mm_check(m));
    mm_destroy(m);
    mm_destroy(NULL);
}
static void configuration(void) {
    mm_config c = tiny(), bad;
    CHECK(c.nclasses == 9 && c.fallback_to_system == 0);
    for (size_t i = 0; i < 9; ++i) {
        CHECK(mm_default_config.sizes[i] == sizes[i]);
        CHECK(mm_default_config.capacities[i] == ((1048576 / sizes[i] < 64) ? 64 : 1048576 / sizes[i]));
    }
    bad = c; bad.nclasses = 0; CHECK(mm_create(&bad) == NULL);
    bad = c; bad.nclasses = 10; CHECK(mm_create(&bad) == NULL);
    bad = c; bad.sizes[1] = 16; CHECK(mm_create(&bad) == NULL);
    bad = c; bad.sizes[1] = 48; bad.sizes[2] = 32; CHECK(mm_create(&bad) == NULL);
    bad = c; bad.sizes[0] = 0; CHECK(mm_create(&bad) == NULL);
    bad = c; bad.sizes[0] = 15; CHECK(mm_create(&bad) == NULL);
    bad = c; bad.capacities[5] = 0; CHECK(mm_create(&bad) == NULL);
    bad = c; bad.capacities[8] = UINT32_MAX; bad.sizes[8] = SIZE_MAX - 15; CHECK(mm_create(&bad) == NULL);
#if SIZE_MAX > UINT32_MAX
    bad = c; bad.capacities[0] = (size_t)UINT32_MAX + 1; CHECK(mm_create(&bad) == NULL);
#endif
    bad = c; bad.fallback_to_system = 2; CHECK(mm_create(&bad) == NULL);
    c.nclasses = 2;
    mm_t *m = mm_create(&c);
    CHECK(m != NULL && mm_alloc(m, 33) == NULL && mm_check(m));
    mm_destroy(m);
    m = mm_create(NULL); CHECK(m != NULL && mm_check(m)); mm_destroy(m);
}
static void fallback(void) {
    mm_config c = tiny();
    mm_t *m = mm_create(&c);
    CHECK(m != NULL && mm_alloc(m, 40001) == NULL && mm_alloc(m, SIZE_MAX) == NULL);
    mm_destroy(m);
    c.fallback_to_system = 1;
    m = mm_create(&c); CHECK(m != NULL);
    void *p = mm_alloc(m, 40001);
    CHECK(p != NULL && (uintptr_t)p % 16 == 0);
    memset(p, 0x83, 40001); canary(p, 40001, 0x83);
    CHECK(mm_free(m, p) == MM_OK);
    p = malloc(40001); CHECK(p != NULL); /* documented system-free rule */
    CHECK(mm_free(m, p) == MM_OK);
    CHECK(mm_check(m)); mm_destroy(m);
    m = mm_create(&c); CHECK(m != NULL);
    CHECK(mm_alloc(m, 16) != NULL); /* destroy invalidates live pool slots */
    CHECK(mm_alloc(m, 40001) != NULL); /* and tracked fallback allocations */
    CHECK(mm_check(m)); mm_destroy(m);
    myMalloc_shutdown(); CHECK(myMalloc(0) == NULL);
    p = myMalloc(17); CHECK(p != NULL); myFree(p); myFree(NULL);
    CHECK(myMalloc(40001) == NULL); myMalloc_shutdown(); myMalloc_shutdown();
    p = myMalloc(64); CHECK(p != NULL); myFree(p); myMalloc_shutdown();
}
struct entry { unsigned char *p; size_t n, cl; unsigned char tag; };
static void differential(uint32_t seed, size_t steps) {
    enum { LIVE = 48 };
    struct entry live[LIVE] = {{0}};
    size_t used[9] = {0};
    mm_config c = tiny();
    mm_t *m = mm_create(&c); CHECK(m != NULL);
    for (size_t step = 0; step < steps; ++step) {
        size_t k = random32(&seed) % LIVE;
        struct entry *e = &live[k];
        if (e->p != NULL) {
            canary(e->p, e->n, e->tag);
            CHECK(mm_free(m, e->p) == MM_OK);
            --used[e->cl]; e->p = NULL;
        } else {
            size_t n = 1 + random32(&seed) % 42000;
            /* Mix uniform byte sizes with boundaries to exercise all classes. */
            if (random32(&seed) % 2 == 0) n = sizes[random32(&seed) % 9];
            size_t cl = which(n);
            e->p = mm_alloc(m, n);
            int expect = cl < 9 && used[cl] < c.capacities[cl];
            CHECK((e->p != NULL) == expect);
            if (e->p != NULL) {
                CHECK((uintptr_t)e->p % 16 == 0);
                e->n = n; e->cl = cl; e->tag = (unsigned char)(k + 1); ++used[cl];
                for (size_t j = 0; j < LIVE; ++j) if (j != k && live[j].p) {
                    uintptr_t x = (uintptr_t)e->p, y = (uintptr_t)live[j].p;
                    CHECK(x >= y ? x - y >= live[j].n : y - x >= n);
                }
                memset(e->p, e->tag, n);
            }
        }
        CHECK(mm_check(m)); /* tiny capacities: after EVERY step */
    }
    for (size_t k = 0; k < LIVE; ++k) if (live[k].p) {
        canary(live[k].p, live[k].n, live[k].tag);
        CHECK(mm_free(m, live[k].p) == MM_OK);
    }
    CHECK(mm_check(m)); mm_destroy(m);
}
#ifndef MM_SHIM
struct gate { pthread_mutex_t mutex; pthread_cond_t cond; int ready, go; };
struct worker { struct gate *g; uint32_t seed; };
static void *thread_test(void *arg) {
    struct worker *w = arg;
    CHECK(pthread_mutex_lock(&w->g->mutex) == 0);
    ++w->g->ready; CHECK(pthread_cond_broadcast(&w->g->cond) == 0);
    while (!w->g->go) CHECK(pthread_cond_wait(&w->g->cond, &w->g->mutex) == 0);
    CHECK(pthread_mutex_unlock(&w->g->mutex) == 0);
    differential(w->seed, 1000000);
    return NULL;
}
static void threads(void) {
    struct gate g = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0};
    pthread_t t[4]; struct worker w[4];
    for (size_t i = 0; i < 4; ++i) {
        w[i].g = &g; w[i].seed = UINT32_C(0x12345678) + (uint32_t)i;
        CHECK(pthread_create(&t[i], NULL, thread_test, &w[i]) == 0);
    }
    CHECK(pthread_mutex_lock(&g.mutex) == 0);
    while (g.ready != 4) CHECK(pthread_cond_wait(&g.cond, &g.mutex) == 0);
    g.go = 1; CHECK(pthread_cond_broadcast(&g.cond) == 0);
    CHECK(pthread_mutex_unlock(&g.mutex) == 0);
    for (size_t i = 0; i < 4; ++i) CHECK(pthread_join(t[i], NULL) == 0);
    CHECK(pthread_cond_destroy(&g.cond) == 0 && pthread_mutex_destroy(&g.mutex) == 0);
}
#endif
/* Review 1 addition: declaration only; existing tests above are unchanged. */
static void fallback_exhaustion(void);
int main(int argc, char **argv) {
    contains_edges(); configuration(); boundaries(); exhaustion_and_free(); fallback();
#ifdef MM_SHIM
    mm_config c = tiny();
    for (size_t fail = 1; fail <= 37; ++fail) {
        mm_shim_fail_at(fail);
        mm_t *m = mm_create(&c); CHECK(m == NULL); CHECK(mm_shim_live() == 0);
    }
    mm_shim_fail_at(0);
    c.fallback_to_system = 1;
    mm_t *m = mm_create(&c); CHECK(m != NULL);
    size_t baseline = mm_shim_live();
    for (size_t fail = 1; fail <= 2; ++fail) {
        mm_shim_fail_at(fail); CHECK(mm_alloc(m, 40001) == NULL);
        CHECK(mm_shim_live() == baseline);
    }
    mm_shim_fail_at(0); mm_destroy(m); CHECK(mm_shim_live() == 0);
#endif
    if (argc == 1 || strcmp(argv[1], "--quick") != 0) {
        differential(UINT32_C(0x51a7c0de), 1000000);
#ifndef MM_SHIM
        threads();
#endif
        puts("PASS differential: 1000000 steps; mm_check every step; seed=0x51a7c0de");
#ifndef MM_SHIM
        puts("PASS 4 threads: one instance per thread; 1000000 steps each; seeds=0x12345678..0x1234567b");
#endif
    }
    fallback_exhaustion();
    puts("PASS mm public API: configuration, boundaries, canaries, exhaustion, free, fallback, wrappers, contains");
    return 0;
}

/* Review 1 addition: fallback is only for requests above the largest class. */
static void fallback_exhaustion(void) {
    mm_config c = tiny();
    c.capacities[0] = 1;
    c.fallback_to_system = 1;
    mm_t *m = mm_create(&c); CHECK(m != NULL);
    void *first = mm_alloc(m, 16); CHECK(first != NULL);
    CHECK(mm_alloc(m, 16) == NULL);
    CHECK(mm_free(m, first) == MM_OK);
    first = mm_alloc(m, 16); CHECK(first != NULL);
    void *large = mm_alloc(m, 40001); CHECK(large != NULL);
    CHECK(mm_free(m, large) == MM_OK);
    CHECK(mm_free(m, first) == MM_OK);
    CHECK(mm_check(m)); mm_destroy(m);
    puts("PASS fallback enabled: capacity-one exhaustion stays NULL, reuse succeeds, above-largest uses system");
}
