#include "mymalloc.h"
#include "pool.h"
#include "pool_util.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Fallback ownership is external metadata, never a payload header. */
struct fallback { void *ptr; struct fallback *next; };
struct mm {
    mm_config cfg;
    pool_t *pools[9];
    struct fallback *fallbacks;
};
const mm_config mm_default_config = {
    9, {16,32,64,128,256,512,1024,4096,40000},
    {65536,32768,16384,8192,4096,2048,1024,256,64}, 0
};
static mm_t *default_instance;

mm_t *mm_create(const mm_config *cfg) {
    if (cfg == NULL) cfg = &mm_default_config;
    if (cfg->nclasses == 0 || cfg->nclasses > 9 ||
        (cfg->fallback_to_system != 0 && cfg->fallback_to_system != 1))
        return NULL;
    for (size_t i = 0; i < cfg->nclasses; ++i) {
        size_t bytes, links;
        if (cfg->sizes[i] == 0 || cfg->sizes[i] % 16 != 0 ||
            (i != 0 && cfg->sizes[i] <= cfg->sizes[i - 1]) ||
            cfg->capacities[i] == 0 || cfg->capacities[i] > UINT32_MAX ||
            !checked_mul(cfg->sizes[i], cfg->capacities[i], &bytes) ||
            bytes > SIZE_MAX - 16 ||
            !checked_mul(cfg->capacities[i], sizeof(uint32_t), &links))
            return NULL;
    }
    mm_t *m = malloc(sizeof *m);
    if (m == NULL) return NULL;
    memset(m, 0, sizeof *m);
    m->cfg = *cfg;
    for (size_t i = 0; i < cfg->nclasses; ++i) {
        m->pools[i] = pool_create(cfg->sizes[i], cfg->capacities[i], 16);
        if (m->pools[i] == NULL) { mm_destroy(m); return NULL; }
    }
    return m;
}

void *mm_alloc(mm_t *m, size_t size) {
    if (m == NULL || size == 0) return NULL;
    for (size_t i = 0; i < m->cfg.nclasses; ++i)
        if (size <= m->cfg.sizes[i])
            return pool_alloc(m->pools[i]);
    if (!m->cfg.fallback_to_system) return NULL;
    void *ptr = malloc(size);
    if (ptr == NULL) return NULL;
    struct fallback *f = malloc(sizeof *f);
    if (f == NULL) { free(ptr); return NULL; }
    f->ptr = ptr; f->next = m->fallbacks; m->fallbacks = f;
    return ptr;
}

int mm_free(mm_t *m, void *ptr) {
    if (ptr == NULL) return MM_OK;
    if (m == NULL) return MM_ERR_OUTSIDE;
    for (size_t i = 0; i < m->cfg.nclasses; ++i) {
        if (pool_contains(m->pools[i], ptr)) {
            int result = pool_free(m->pools[i], ptr);
            if (result == POOL_OK) return MM_OK;
            if (result == POOL_ERR_DOUBLE_FREE) return MM_ERR_DOUBLE_FREE;
            return MM_ERR_NOT_START;
        }
    }
    if (!m->cfg.fallback_to_system) return MM_ERR_OUTSIDE;
    struct fallback **link = &m->fallbacks;
    while (*link != NULL && (*link)->ptr != ptr) link = &(*link)->next;
    if (*link != NULL) {
        struct fallback *f = *link; *link = f->next; free(f);
    }
    free(ptr);
    return MM_OK;
}

void mm_destroy(mm_t *m) {
    if (m == NULL) return;
    for (size_t i = 0; i < m->cfg.nclasses; ++i) pool_destroy(m->pools[i]);
    while (m->fallbacks != NULL) {
        struct fallback *f = m->fallbacks;
        m->fallbacks = f->next;
        free(f->ptr); free(f);
    }
    free(m);
}

int mm_check(const mm_t *m) {
    if (m == NULL) return 0;
    for (size_t i = 0; i < m->cfg.nclasses; ++i)
        if (!pool_check(m->pools[i])) return 0;
    for (const struct fallback *f = m->fallbacks; f != NULL; f = f->next)
        if (f->ptr == NULL) return 0;
    return 1;
}

void *myMalloc(size_t size) {
    if (size == 0) return NULL;
    if (default_instance == NULL) default_instance = mm_create(NULL);
    return mm_alloc(default_instance, size);
}
void myFree(void *ptr) { (void)mm_free(default_instance, ptr); }
void myMalloc_shutdown(void) {
    mm_destroy(default_instance); default_instance = NULL;
}
