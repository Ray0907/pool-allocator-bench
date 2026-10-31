/* Link-time allocator shim, ONLY for an additional test/mutation build.
 * No pool setters; tests still call only the public pool API.
 * Returns 16-byte-aligned allocations deliberately NOT aligned to 128 bytes.
 * This models a legal macOS malloc result and makes missing base alignment
 * observable even when the system allocator happens to over-align its blocks.
 * Its bookkeeping header is NOT used by the production pool or its slots.
 */
#include "test_alloc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t live_blocks, failures;
static int registered;

static void report(void) {
    printf("allocator shim: live blocks=%zu forced failures=%zu\n", live_blocks, failures);
    if (live_blocks != 0) {
        fprintf(stderr, "FAIL: allocator shim detected a leak\n");
        abort();
    }
}

void *q2_test_malloc(size_t n) {
    if (!registered) {
        if (atexit(report) != 0) abort();
        registered = 1;
    }
    if (n > SIZE_MAX / 4) { ++failures; return NULL; }
    unsigned char *raw = malloc(n + 48);
    if (raw == NULL) return NULL;
    unsigned char *result = raw + 16;
    if ((uintptr_t)result % 128 == 0) result += 16;
    memcpy(result - sizeof raw, &raw, sizeof raw);
    ++live_blocks;
    return result;
}

void q2_test_free(void *p) {
    if (p == NULL) return;
    unsigned char *raw;
    memcpy(&raw, (unsigned char *)p - sizeof raw, sizeof raw);
    --live_blocks;
    free(raw);
}
