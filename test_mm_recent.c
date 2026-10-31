/* Additional public-API checks; also runnable against the unmodified v1. */
#include "mymalloc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)

int main(void) {
    mm_config cfg = mm_default_config;
    for (size_t i = 0; i < cfg.nclasses; ++i) cfg.capacities[i] = 2;
    mm_t *m = mm_create(&cfg);
    CHECK(m != NULL);
    void *base[9];
    for (size_t i = 0; i < cfg.nclasses; ++i) {
        base[i] = mm_alloc(m, cfg.sizes[i]);
        CHECK(base[i] != NULL);
        CHECK(mm_free(m, base[i]) == MM_OK);
    }
    /* Smallest-class rule, exhaustion, and all 40,001 request boundaries. */
    for (size_t n = 1; n <= 40001; ++n) {
        size_t owner = 0;
        while (owner < cfg.nclasses && cfg.sizes[owner] < n) ++owner;
        void *p = mm_alloc(m, n);
        CHECK(p == (owner < cfg.nclasses ? base[owner] : NULL));
        CHECK(mm_free(m, p) == MM_OK);
    }
    /* Every owner before, at, and after every possible recent class. */
    for (size_t hint = 0; hint < cfg.nclasses; ++hint)
        for (size_t owner = 0; owner < cfg.nclasses; ++owner) {
            void *p = mm_alloc(m, cfg.sizes[owner]);
            void *h = mm_alloc(m, cfg.sizes[hint]);
            CHECK(p != NULL && h != NULL && p != h);
            CHECK(mm_free(m, (unsigned char *)p + 1) == MM_ERR_NOT_START);
            CHECK(mm_free(m, p) == MM_OK);
            CHECK(mm_free(m, p) == MM_ERR_DOUBLE_FREE);
            CHECK(mm_free(m, h) == MM_OK);
            CHECK(mm_check(m));
        }
    /* One byte before, at, last byte, and one past every class slot area. */
    for (size_t hint = 0; hint < cfg.nclasses; ++hint)
        for (size_t c = 0; c < cfg.nclasses; ++c) {
            uintptr_t start = (uintptr_t)base[c], end = start + 2 * cfg.sizes[c];
            uintptr_t addresses[] = {start - 1, start, end - 1, end};
            for (size_t k = 0; k < 4; ++k) {
                void *h = mm_alloc(m, cfg.sizes[hint]);
                CHECK(h != NULL && mm_free(m, h) == MM_OK);
                int expected = MM_ERR_OUTSIDE;
                for (size_t i = 0; i < cfg.nclasses; ++i) {
                    uintptr_t b = (uintptr_t)base[i];
                    if (addresses[k] >= b && addresses[k] - b < 2 * cfg.sizes[i])
                        expected = (addresses[k] - b) % cfg.sizes[i] ? MM_ERR_NOT_START : MM_ERR_DOUBLE_FREE;
                }
                CHECK(mm_free(m, (void *)addresses[k]) == expected);
                CHECK(mm_check(m));
            }
        }
    mm_destroy(m);
    puts("PASS extra public API: 1..40001 class rule; 81 hint/owner orders; 324 range boundary probes");
    return 0;
}
