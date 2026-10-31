#include "pool.h"
#include "pool_util.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct pool {
    size_t slot_size, nslots, stride, align, free_count;
    unsigned char *raw, *base;
    uint32_t *next;
    uint8_t *state;
    uint32_t head;
};

pool_t *pool_create(size_t slot_size, size_t nslots, size_t align) {
    size_t stride, bytes, links;
    if (slot_size == 0 || nslots == 0 || nslots > UINT32_MAX ||
        align < sizeof(void *) || (align & (align - 1)) != 0)
        return NULL;
    if (!align_up(slot_size, align, &stride) ||
        !checked_mul(nslots, stride, &bytes) || bytes > SIZE_MAX - align ||
        !checked_mul(nslots, sizeof(uint32_t), &links))
        return NULL;

    pool_t *p = malloc(sizeof *p);
    if (p == NULL) return NULL;
    memset(p, 0, sizeof *p);
    p->next = malloc(links);
    p->state = malloc(nslots * sizeof *p->state);
    p->raw = malloc(bytes + align);
    if (p->next == NULL || p->state == NULL || p->raw == NULL) {
        pool_destroy(p);
        return NULL;
    }
    uintptr_t address = (uintptr_t)p->raw;
    if (address > UINTPTR_MAX - (align - 1)) {
        pool_destroy(p);
        return NULL;
    }
    uintptr_t aligned = (address + align - 1) & ~(uintptr_t)(align - 1);
    p->base = (unsigned char *)aligned;
    p->slot_size = slot_size;
    p->nslots = nslots;
    p->stride = stride;
    p->align = align;
    p->free_count = nslots;
    p->head = 0;
    for (size_t i = 0; i < nslots; ++i)
        p->next[i] = i + 1 < nslots ? (uint32_t)(i + 1) : UINT32_MAX;
    memset(p->state, 0, nslots * sizeof *p->state);
    return p;
}

void *pool_alloc(pool_t *p) {
    if (p == NULL || p->head == UINT32_MAX) return NULL;
    uint32_t idx = p->head;
    p->head = p->next[idx];
    p->state[idx] = 1;
    --p->free_count;
    return p->base + (size_t)idx * p->stride;
}

int pool_free(pool_t *p, void *ptr) {
    if (ptr == NULL) return POOL_OK;
    if (p == NULL) return POOL_ERR_OUTSIDE;
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)p->base;
    if (address < base) return POOL_ERR_OUTSIDE;
    uintptr_t delta = address - base;
    if (delta % p->stride != 0) return POOL_ERR_NOT_START;
    uintptr_t idx = delta / p->stride;
    if (idx >= p->nslots) return POOL_ERR_OUTSIDE;
    if (p->state[idx] != 1) return POOL_ERR_DOUBLE_FREE;
#ifdef POOL_DEBUG
    memset(ptr, 0xdd, p->slot_size);
#endif
    p->state[idx] = 0;
    p->next[idx] = p->head;
    p->head = (uint32_t)idx;
    ++p->free_count;
    return POOL_OK;
}

void pool_destroy(pool_t *p) {
    if (p == NULL) return;
    free(p->raw);
    free(p->state);
    free(p->next);
    free(p);
}

int pool_contains(const pool_t *p, const void *ptr) {
    if (p == NULL || ptr == NULL) return 0;
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)p->base;
    return address >= base && address - base < p->nslots * p->stride;
}

int pool_check(const pool_t *p) {
    if (p == NULL || p->base == NULL || p->next == NULL || p->state == NULL ||
        p->nslots == 0 || p->free_count > p->nslots ||
        p->stride < p->slot_size || p->stride % p->align != 0 ||
        (uintptr_t)p->base % p->align != 0)
        return 0;
    size_t free_states = 0;
    for (size_t i = 0; i < p->nslots; ++i) {
        if (p->state[i] > 1) return 0;
        if (p->state[i] == 0) ++free_states;
    }
    if (free_states != p->free_count) return 0;
    size_t visited = 0;
    uint32_t idx = p->head;
    while (idx != UINT32_MAX) {
        if (idx >= p->nslots || p->state[idx] != 0 || visited >= p->nslots)
            return 0;
        ++visited;
        idx = p->next[idx];
    }
    /* A repeated index implies a cycle. Bounded traversal plus equal counts
     * proves every free index occurs exactly once, without scratch allocation. */
    return visited == free_states;
}
