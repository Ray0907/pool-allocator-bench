#ifndef Q2_TRACE_MM_H
#define Q2_TRACE_MM_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    size_t count, bytes, counts[9];
    size_t *sizes, *order;
} mm_trace;
int mm_trace_create(mm_trace *t, size_t count, uint32_t seed);
void mm_trace_destroy(mm_trace *t);
#endif
