#ifndef Q2_MYMALLOC_H
#define Q2_MYMALLOC_H
#include <stddef.h>

/* One instance per thread; only its owner may allocate/free/check/destroy.
 * Cross-thread free is unsupported, even when operations do not overlap.
 * The lazy default instance is NOT thread-safe. No locks or TLS.
 * cfg==NULL selects the defaults below; 1..9 strictly ascending sizes,
 * each a nonzero multiple of 16; nonzero capacities <= UINT32_MAX.
 * size==0 returns NULL. Exhaustion never uses another class or fallback.
 * All returned blocks have 16-byte alignment; contents are uninitialized.
 * Destroy releases all pools and outstanding tracked fallback blocks.
 * Stale pointers after reuse cannot be distinguished from current ones.
 * Handles must be live, genuine handles. No access after free/destroy.
 */
typedef struct mm mm_t;
typedef struct {
    size_t nclasses;
    size_t sizes[9];
    size_t capacities[9];
    int fallback_to_system;
} mm_config;
extern const mm_config mm_default_config;
enum { MM_OK = 0, MM_ERR_OUTSIDE = 1, MM_ERR_NOT_START = 2, MM_ERR_DOUBLE_FREE = 3 };
mm_t *mm_create(const mm_config *cfg);
void *mm_alloc(mm_t *m, size_t size);
/* NULL is always success. Without fallback, a foreign pointer is rejected.
 * With fallback, ANY out-of-pool pointer is handed to system free: passing
 * an invalid/stale pointer is dangerous and can corrupt memory or abort.
 * Fallback is only for sizes above the largest class; latency is unbounded.
 */
int mm_free(mm_t *m, void *ptr);
void mm_destroy(mm_t *m);
int mm_check(const mm_t *m);
void *myMalloc(size_t size);
void myFree(void *ptr);
void myMalloc_shutdown(void);
#endif
