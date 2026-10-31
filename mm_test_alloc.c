/* Test/memory-only shim. No parallel access; records requested bytes, not RSS. */
#include "mm_test_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static struct { void *p; size_t n; } blocks[4096];
static size_t calls, fail_at, live, bytes;
void mm_shim_fail_at(size_t call) { calls = 0; fail_at = call; }
size_t mm_shim_live(void) { return live; }
size_t mm_shim_bytes(void) { return bytes; }
size_t mm_shim_calls(void) { return calls; }
void *mm_test_malloc(size_t n) {
    ++calls;
    if (calls == fail_at || n > SIZE_MAX / 4) return NULL;
    void *p = malloc(n);
    if (p == NULL) return NULL;
    for (size_t i = 0; i < 4096; ++i) if (blocks[i].p == NULL) {
        blocks[i].p = p; blocks[i].n = n; ++live; bytes += n; return p;
    }
    abort();
}
void mm_test_free(void *p) {
    if (p == NULL) return;
    for (size_t i = 0; i < 4096; ++i) if (blocks[i].p == p) {
        blocks[i].p = NULL; --live; bytes -= blocks[i].n; free(p); return;
    }
    /* The public fallback contract permits a caller's system allocation. */
    free(p);
}
