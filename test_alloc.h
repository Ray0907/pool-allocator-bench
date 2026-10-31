#ifndef Q2_TEST_ALLOC_H
#define Q2_TEST_ALLOC_H
#include <stddef.h>
void *q2_test_malloc(size_t n);
void q2_test_free(void *p);
#endif
