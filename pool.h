#ifndef Q2_POOL_H
#define Q2_POOL_H
#include <stddef.h>

/* Single-threaded: caller must serialize ALL access, including pool_check.
 * Fixed-size slots; size/count/alignment are runtime arguments. Alignment must
 * be a power of two >= sizeof(void *). Zero sizes/counts and overflow fail.
 * No malloc fallback on exhaustion; no zeroing on alloc/free (except DEBUG).
 * Do not access a slot after free. Destroy invalidates ALL slots, including
 * live ones, and frees the pool; pool_destroy(NULL) is safe.
 * Never pass a destroyed/forged handle. Stale pointers after address reuse
 * cannot be distinguished from current allocations by a pointer-only API.
 */
typedef struct pool pool_t;
enum {
    POOL_OK = 0,
    POOL_ERR_OUTSIDE = 1,
    POOL_ERR_NOT_START = 2,
    POOL_ERR_DOUBLE_FREE = 3
};

pool_t *pool_create(size_t slot_size, size_t nslots, size_t align);
void *pool_alloc(pool_t *p);
/* NULL ptr is always a no-op success, even with NULL p. Invalid frees leave
 * the pool unchanged. Validation order: below base, off-grid, index, state.
 * Thus an off-grid address above the pool also returns ERR_NOT_START.
 */
int pool_free(pool_t *p, void *ptr);
void pool_destroy(pool_t *p);
/* Public, read-only O(nslots) diagnostic; 1 = valid, 0 = invalid/NULL.
 * No allocation, no test-only mutation hooks. Not part of the hot path.
 */
int pool_check(const pool_t *p);
/* Read-only slot-area membership, including freed slots and off-grid bytes.
 * NULL pool/pointer returns 0; never reads slot memory or changes state. */
int pool_contains(const pool_t *p, const void *ptr);
#endif
