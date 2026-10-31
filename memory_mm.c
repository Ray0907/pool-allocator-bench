#define _POSIX_C_SOURCE 200809L
#include "check_common.h"
#include "mymalloc.h"
#include "mm_test_alloc.h"
#ifdef __APPLE__
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif
int main(void) {
    check_machine(); check_clock();
    mm_t *m = mm_create(NULL);
    check_require(m != NULL, "create");
    printf("CREATE requested_bytes=%zu calls=%zu live_blocks=%zu handle_and_metadata_included=1 rounding_and_RSS_excluded=1\n",
           mm_shim_bytes(), mm_shim_calls(), mm_shim_live());
    const size_t requests[] = {17,33,65,129,4097,39999};
    for (size_t j = 0; j < 6; ++j) {
        size_t n = requests[j], cl = 0;
        while (mm_default_config.sizes[cl] < n) ++cl;
        void *p = mm_alloc(m, n); check_require(p != NULL, "allocation");
        void *q = malloc(n); check_require(q != NULL, "system allocation");
        size_t rounded = 0;
#ifdef __APPLE__
        rounded = malloc_size(q);
#elif defined(__linux__)
        rounded = malloc_usable_size(q);
#endif
        printf("FRAGMENT request=%zu class=%zu waste_bytes=%zu system_usable=%zu lower_bound_only=1\n",
               n, mm_default_config.sizes[cl], mm_default_config.sizes[cl] - n, rounded);
        check_require(mm_free(m, p) == MM_OK, "free"); free(q);
    }
    for (size_t i = 0; i < 9; ++i) printf("DEFAULT class=%zu capacity=%zu slot_bytes=%zu metadata_index_state_bytes=%zu\n",
        mm_default_config.sizes[i], mm_default_config.capacities[i],
        mm_default_config.sizes[i] * mm_default_config.capacities[i],
        mm_default_config.capacities[i] * (sizeof(uint32_t) + sizeof(uint8_t)));
    /* Page-fault diagnostic, not an allocator timing benchmark. */
    long start = check_faults();
    size_t touched = 0;
    for (size_t i = 0; i < 9; ++i) for (size_t j = 0; j < mm_default_config.capacities[i]; ++j) {
        void *p = mm_alloc(m, mm_default_config.sizes[i]);
        check_require(p != NULL, "cold allocation");
        volatile unsigned char *b = p;
        for (size_t k = 0; k < mm_default_config.sizes[i]; k += 64) b[k] = 1;
        b[mm_default_config.sizes[i] - 1] = 1;
        ++touched;
    }
    printf("COLD first_payload_touch_minor_faults=%ld live_slots=%zu prefault=0 timing=not_measured\n",
           check_faults() - start, touched);
    check_require(mm_check(m), "check");
    mm_destroy(m);
    printf("DESTROY live_blocks=%zu requested_bytes=%zu\n", mm_shim_live(), mm_shim_bytes());
    check_require(mm_shim_live() == 0 && mm_shim_bytes() == 0, "leaks");
    return 0;
}
