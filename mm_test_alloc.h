#ifndef Q2_MM_TEST_ALLOC_H
#define Q2_MM_TEST_ALLOC_H
#include <stddef.h>
void *mm_test_malloc(size_t n);
void mm_test_free(void *p);
void mm_shim_fail_at(size_t call);
size_t mm_shim_live(void);
size_t mm_shim_bytes(void);
size_t mm_shim_calls(void);
#endif
