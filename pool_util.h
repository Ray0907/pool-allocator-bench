#ifndef Q2_POOL_UTIL_H
#define Q2_POOL_UTIL_H
#include <stddef.h>
#include <stdint.h>

/* Pure arithmetic helpers; out changes only on success. */
static inline int checked_mul(size_t a, size_t b, size_t *out) {
    if (b != 0 && a > SIZE_MAX / b) return 0;
    *out = a * b;
    return 1;
}

/* Precondition: align is a nonzero power of two. */
static inline int align_up(size_t n, size_t align, size_t *out) {
    if (n > SIZE_MAX - (align - 1)) return 0;
    *out = (n + align - 1) & ~(align - 1);
    return 1;
}
#endif
