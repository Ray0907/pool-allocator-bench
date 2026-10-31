/* Synthetic trace, not a real workload.
 * xorshift32 seed 0x70ace123. Each of the nine class sizes has probability
 * 1/9 (modulo bias < 1/2^32). Allocate count slots in index order, then free
 * a Fisher-Yates permutation. Repeat that exact batch; no interleaved growth.
 * Lifetimes, in operation counts, are count + free_rank - allocation_index:
 * permutation ranks are uniform, so the marginal lifetime is triangular.
 * Capacities in bench_mm are the measured per-class peak counts, not a
 * proposed production configuration. Default classes/capacities are guesses.
 */
#include "trace_mm.h"
#include <stdlib.h>
#include <string.h>
static uint32_t next32(uint32_t *s) {
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x;
}
int mm_trace_create(mm_trace *t, size_t count, uint32_t seed) {
    const size_t classes[9] = {16,32,64,128,256,512,1024,4096,40000};
    memset(t, 0, sizeof *t);
    if (count == 0 || seed == 0 || count > SIZE_MAX / sizeof(size_t)) return 0;
    t->sizes = malloc(count * sizeof *t->sizes);
    t->order = malloc(count * sizeof *t->order);
    if (t->sizes == NULL || t->order == NULL) { mm_trace_destroy(t); return 0; }
    t->count = count;
    for (size_t i = 0; i < count; ++i) {
        size_t cl = next32(&seed) % 9;
        t->sizes[i] = classes[cl]; t->order[i] = i;
        if (t->bytes > SIZE_MAX - classes[cl]) { mm_trace_destroy(t); return 0; }
        t->bytes += classes[cl]; ++t->counts[cl];
    }
    for (size_t i = count - 1; i > 0; --i) {
        size_t j = next32(&seed) % (i + 1), tmp = t->order[i];
        t->order[i] = t->order[j]; t->order[j] = tmp;
    }
    return 1;
}
void mm_trace_destroy(mm_trace *t) {
    free(t->sizes); free(t->order); memset(t, 0, sizeof *t);
}
