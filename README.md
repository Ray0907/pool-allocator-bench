# Fixed-Size Memory Pool and a Small General-Purpose Allocator Interface

A C library that implements a fixed-size memory pool (`pool.c`) and a small general-purpose allocator (`myMalloc`) built from several pools. The project measures when preallocated pools are faster than the system `malloc`/`free`, and when they are not. It includes correctness tests, mutation tests, sanitizer runs, and benchmarks. All numbers come from one macOS arm64 machine.

## Problem Statement

`malloc` and `free` must work for any size and any object lifetime. For this reason, they can be costly when a program allocates and frees many objects again and again.

This project answers three questions:

1. Can a fixed-size memory pool make repeated allocation and release cheaper than `malloc` and `free`?
2. Can the pool be used behind a general `myMalloc(size_t)` interface, and what does this interface cost?
3. What does the program give up in return (flexibility, reserved memory, thread safety), and how do we show this with tests and measurements?

The work has two parts: a fixed-size pool (`pool.c`), and a size-class layer built on top of it (`mymalloc.c`). Correctness tests, mutation tests, sanitizer runs, and benchmarks are included. All measurements come from one macOS arm64 machine.

## 1. Question and Conclusion

The question: can a large number of repeated allocations and releases be cheaper than direct `malloc/free`? This implementation first handles fixed-size objects. Then it adds an interface of the form `myMalloc(size_t)`. Both parts trade flexibility and reserved memory for speed. They do not remove the work of a general allocator for free.

**The conclusion has conditions.** If the object size and the maximum number of live objects are known in advance, a fixed pool can shorten the hot path. But the general interface adds class selection, a range lookup, and an extra call. These costs remove the gain. In the retained macOS measurements, several `mm` cases at 256 and 4096 bytes are slower than `malloc`. Mixed allocation with payload touch also does not win in all cases. This is not a replacement for the system `malloc`. It gives no hard real-time latency guarantee.

### Why malloc/free is sometimes costly and sometimes already fast

A general allocator must support different sizes and lifetimes. It must reuse free space, handle alignment and metadata, and get more memory when needed. Multiple threads, cache misses, page faults, and scheduling can also change the observed latency. These costs do not mean that every call makes a system call or takes a global lock.

For small objects, a cache often serves the hot allocation. The tcache and arenas of glibc are one example. **Do not apply the glibc mechanisms directly to the libmalloc of macOS.** The speeds and outliers seen here support only the difference for the named workloads. They do not show the internal cause of each slow sample. A preallocated pool moves the cost of getting backing storage to the creation step. It trades a capacity limit for a shorter operation path.

## 2. Fixed-size pool

`pool_t` is an opaque handle. The interface is in [pool.h](pool.h). The implementation is in [pool.c](pool.c). The shared arithmetic helpers are in [pool_util.h](pool_util.h). The data area has no header before each object. The pool does not use the user payload to store free-list links.

### Layout and alignment

- One contiguous slot backing allocation. Its base has the requested alignment. `stride` is the slot size rounded up.
- Side metadata is a `uint32_t next[]` per slot and one `in_use[]` byte per slot. There is also the handle, the free-list head, counters, and the original backing pointer.
- `pool_create(slot_size, slot_count, alignment)` rejects zero arguments, invalid alignment, a capacity that exceeds the index range, and overflow in multiplication, addition, or round-up. If a step fails during creation, all allocated resources are released.
- The alignment must meet the power-of-two and basic-type requirements of the public interface. The tests include a base alignment larger than the normal system `malloc` alignment. Do not assume that all platforms use 16-byte alignment. The general `mm` layer states 16 bytes.

### alloc/free, errors, and invariants

`pool_alloc` takes one index from the free-list head, marks it as in use, and returns the start of the slot. An empty pool returns `NULL`. The pool does not grow. `pool_free` first checks the address range and the stride to verify the slot start. Then it checks the use state. At last it puts the slot back at the head. Because of this, the most recently freed slot is reused first (LIFO).

The public checks cover NULL free, foreign pointers, interior slot pointers, and double free. The pool does not need to dereference the payload pointer. `pool_check` verifies that the free list, the states, and the counters agree. In the normal non-debug build, the alloc/free path is constant work. The full `pool_check` and the extra `POOL_DEBUG` checks are not O(1). The performance build does not enable them.

Read the limits one by one:

- Slot exhaustion is a normal observable failure. The caller must handle `NULL`.
- The memory content is not initialized. `destroy` makes all slot pointers that were obtained invalid.
- After a slot is freed and allocated again, the old pointer and the new pointer have the same address. The pool cannot detect a stale-pointer ABA. This is not a memory-safety sandbox.
- The handle must be a real and live handle. An arbitrary address is not valid as a handle.
- The pool has no lock. Shared or cross-thread use is outside the contract of this delivery.

## 3. General interface: myMalloc / mm_t

The implementation is in [mymalloc.c](mymalloc.c). The full contract and the error codes are in [mymalloc.h](mymalloc.h). This layer does not change the pool below it. It only combines several pools.

### Size classes and default capacity

The default classes are `16, 32, 64, 128, 256, 512, 1024, 4096, 40000` bytes. Each class reserves `max(64, floor(1048576 / class_size))` slots:

| class bytes | capacity |
|---:|---:|
| 16 | 65536 |
| 32 | 32768 |
| 64 | 16384 |
| 128 | 8192 |
| 256 | 4096 |
| 512 | 2048 |
| 1024 | 1024 |
| 4096 | 256 |
| 40000 | 64 |

`mm_create(NULL)` uses the defaults above. You can also pass an `mm_config`. It sets 1 to 9 strictly increasing sizes. Each size is non-zero and a multiple of 16. It also sets non-zero capacities that an index can represent. If a failure occurs during creation, all pools that were created are cleaned up. System fallback is off by default.

### Class selection, free routing, and fallback

- `mm_alloc(m, 0)` returns `NULL`. Any other request selects the first class with `class_size >= size`. The implementation is a short linear scan (nine entries at most) that favors correctness.
- If the selected class is exhausted, the call **returns NULL at once. It does not use a larger class. It does not go to the system malloc.** This is also true when fallback is on.
- Only a request **larger than the largest class** with `fallback_to_system` on calls the system malloc. The code also allocates linked-list bookkeeping. `destroy` uses it to release the tracked fallback blocks that are still alive.
- `mm_free` uses the hint of the most recently hit class. Then it looks up the address range of each pool. `pool_free` verifies errors inside a pool and returns the matching error. There is no class header before the slot payload.
- When fallback is off, a pointer that is not from a pool returns `MM_ERR_OUTSIDE`. **When fallback is on, any pointer that is not from a pool goes to the system free.** If you pass a stack, interior, stale, or doubly freed fallback pointer, the result can be undefined behavior, a crash, or memory corruption. Do not depend on the pool to reject these pointers.
- The hint is a fast path, not authoritative data. Correctness must not depend on which class was allocated last. `mm_check` verifies each pool and the fallback bookkeeping. The fallback list path has no constant-time bound.

### Minimal usage example

```c
#include "mymalloc.h"

int main(void) {
    mm_t *m = mm_create(NULL);
    if (m == NULL) return 1;
    unsigned char *p = mm_alloc(m, 100); /* selects 128-byte class */
    if (p == NULL) { mm_destroy(m); return 1; }
    p[0] = 42;
    int status = mm_free(m, p);
    mm_destroy(m);
    return status != MM_OK;
}
```

```sh
cc -std=c99 -pedantic -Wall -Wextra -Werror example.c mymalloc.c pool.c -o example
```

`example.c` is the example above. It is not an extra dependency of the package. A single thread can also use `myMalloc(size)`, `myFree(ptr)`, and `myMalloc_shutdown()`. The first allocation creates the default singleton. `shutdown` releases it. `myFree` is a void wrapper. It does not return an error code to the caller.

### Thread ownership and non-goals

**Use one separate `mm_t` for each thread. Only the owner thread can alloc, free, check, or destroy.** Cross-thread free is not supported, even when the calls do not overlap in time. The lazy singleton `myMalloc/myFree` also has no thread safety, no lock, and no TLS. Do not share it.

This delivery does not implement global malloc interposition, realloc/calloc, arbitrary over-alignment, dynamic growth, shared instances, cross-thread reclaim, or a production-grade allocator. These limits keep the implementation small. But you cannot infer shared-use safety from the multi-thread tests of independent instances.

## 4. Correctness evidence

All functional tests check through the public API. They do not look into the opaque struct. The minimal build uses C99. No third-party Python package is needed.

| Test | Main coverage |
|---|---|
| `test_pool.c` | Invalid and overflow creation, backing failure, alignment and non-overlap, LIFO and exhaustion, NULL/foreign/interior/double free, destroy, random oracle |
| `test_alloc.c` | A malloc shim for a separate fault build. It gives valid backing that is not over-aligned on purpose. It checks base alignment and resource release |
| `test_mm.c` | Configuration, class boundaries, canary, alignment, exhaustion and reuse, fallback, error routing, wrappers, cleanup after allocation failure |
| `mm_test_alloc.c` | The `MM_SHIM` build injects creation and fallback allocation failures. It tracks live allocations and requested bytes |
| `test_mm_recent.c` | The class rule for 1..40001, 81 hint/owner orders, 324 range boundary probes |

Pool differential test: 1,000,000 steps, 31 slots, seed=`0x51a7c0de`. An independent shadow state in the test program checks allocation, release, and exhaustion. The `mm` differential test also has 1,000,000 steps, runs `mm_check` at each step, and uses seed=`0x51a7c0de`. There are also four threads. Each has its own instance and 1,000,000 steps, with seeds `0x12345678..0x1234567b`. A seed makes a run repeatable. It does not mean that all execution orders were covered.

`make test` runs three builds: normal, allocator-shim, and `POOL_DEBUG`. `make test-mm` runs normal and `MM_SHIM`. A sanitizer build and a fault build have different purposes. One cannot replace the other.

Mutation testing uses [mutants.sh](mutants.sh) and [mutants_mm.py](mutants_mm.py). It builds copies with deliberate errors in a temporary directory. It does not change the committed implementation. The retained records for two platforms show pool **7/7 killed** and mm **9/9 killed**. This includes the error where an exhausted selected class should not go to the system allocator. "Killed" means that the tests rejected a mutant that compiled. It is not a proof against all possible errors.

### Sanitizers and tool status

On macOS arm64, the strict build, ASan, UBSan, functional tests, mutants, codegen, and smoke runs all completed. The detailed commands, exit codes, and raw diagnostics are in [results/](results/). **Linux was not verified. There are no Linux numbers.**

- ASan and UBSan: the macOS tests pass. UBSan uses `-fno-sanitize-recover=all`. The ASan allocation warning for a deliberately huge allocation comes from a failure-path test. It is set with `allocator_may_return_null=1`.
- macOS `leaks`: it cannot check the ASan heap (exit=255, runtime and inspection limit). The native pool fallback run has exit=0 and 0 leaks. This does not mean that an external leak tool covered all mm paths on macOS.
- TSan: the macOS workload with one instance per thread finished with no report. No program that is known to cause a shared data race was run. The report does not claim that a shared mm_t is safe.
- Valgrind: it is not installed on macOS and was not run. It is not listed as PASS. A fuzzer or a bounded checker is not part of this minimal delivery.

`doctor` records the environment and the optional leak tools. It does not mean that every optional checker passed. A TSan failure still returns non-zero from `make tsan-mm`.

## 5. Performance method and main results

This section cites complete macOS full runs. It does not cite the timing of the fresh verification smoke runs. The raw data, the distributions, and the data summary are in [results/summary.md](results/summary.md). The full-run records identify Darwin arm64 and 10 logical CPUs (4 performance and 6 efficiency). The threads are not pinned to cores. The records do not fully contain the compiler and OS versions of that time. For this reason, the report does not claim that the system state can be fully rebuilt. The environment versions of a fresh build prove only that the build can be reproduced now.

All timed builds use `-O2` and no LTO. `make check-codegen` checks the assembly of the isolated timed functions, the undefined symbols of the objects, and the linked disassembly. It confirms that the optimizer did not remove the malloc/free and pool calls in the slow/MT loops. It does not verify every behavior of every possible compiler. It does not treat smoke timing as speed evidence.

### 5.1 Fixed pool, with payload touch

`bench.c` compares malloc, pool, and `reuse_not_allocator` (it holds the addresses in advance and is not an allocator). The sizes are `16,64,256,4096,40000`. Each cell has 5 reps, and each rep has 20,000 pairs. The patterns are hot reuse, random free after 1000 live objects, and mixed-large-live with at least 64 MiB of live requested payload. Warmup and setup are not counted in the hot operations. For each condition, the raw distribution and the quantiles of each alloc/free are kept.

The table shows the median **ns/op** (one pair has two operations: alloc and free). It is not ns/pair.

| bytes | pattern | malloc | pool | malloc/pool |
|---:|---|---:|---:|---:|
| 40000 | hot_best_case | 79.675 | 46.475 | 1.714x |
| 40000 | batch1000_random_free | 811.100 | 493.350 | 1.644x |
| 40000 | mixed_large_live | 808.025 | 486.950 | 1.659x |
| 16 | hot_best_case | 3.925 | 2.750 | 1.427x |
| 4096 | mixed_large_live | 73.225 | 66.325 | 1.104x |

In 15 of 15 cells, the pool median is lower. But this table has no confidence interval. It does not show a clear statistical advantage in all cases. `hot_best_case` is the best reuse case. It is not the behavior of any application. The clock resolution and the observed minimum positive delta of this program are both **1000 ns**. A batch average can show a per-operation value below one tick. But the quantiles of single calls are often quantized to 0. Do not report them as a cost of 0 ns.

### 5.2 No payload touch: small objects are already fast, large objects and tails still differ

`slow_check.c` uses `mach_absolute_time`. Its tick and its measured granularity are both **41.666666667 ns**. Do not mix it with the 1000-ns timer above. Each condition has one warmup and 5 measured reps. Each rep has 2,000,000 pairs. For the tail, each rep also takes 1,000,000 samples. Each sample wraps one full alloc/free pair (including the sink, the check, and the timer). It is not the latency of a single malloc or a single free. The program touches only pointers and metadata. It does not write the payload. Minor faults are the getrusage delta over the timed interval. Pool creation is not counted.

| bytes | malloc median ns/pair | pool median ns/pair |
|---:|---:|---:|
| 16 | 7.002979 | 4.673000 |
| 4096 | 13.653271 | 4.627667 |
| 40000 | 67.245500 | 4.681354 |
| 1048576 | 68.752271 | 4.710521 |

Small-size malloc is not "very slow". For the 40000-byte malloc/free pair, the per-rep median p50 and p99 are both 83.333333 ns. The worst observed max is 15875 ns. For the pool, p50 is `<tick`, p99 is 41.666667 ns, and the worst max is 8000 ns. These are observed values, not bounds. The 4096-byte pool also has a higher worst max than malloc. The report does not subtract the timer overhead as a constant. It also does not infer that the per-call tail of the mm interface equals that of the raw pool.

### 5.3 Multi-thread degradation is not a proof for a shared allocator

`mt_check.c` tests 64 and 40000 bytes with 1/2/4/8 threads. Each worker has its own pool and 1,000,000 pairs. It first warms up. Then all workers start together after they are ready. The output keeps each worker, the start spread, the mean, the slowest worker, and the distribution of 5 reps. The pool setup and the sync gate are not in the timed loop. The threads are not pinned to cores. The synchronized start still has some start skew.

- 40000-byte malloc: the median worker-mean grows from 63.797375 ns/pair at 1 thread to 8480.721276 at 8 threads. This is about **133x**. For the pool, it grows from 4.649333 to 15.223214. This is about **3.27x**.
- 64 bytes, 8 threads: malloc=11.749917 and pool=11.959990 ns/pair. The pool is not faster. The pool also degrades as the thread count grows.

These values are the rep median of the mean of the worker ns/pair. They are **not the total throughput**. The machine has mixed cores, the threads are not pinned, and scheduling and cache state are mixed together. There is no perf or profile evidence that can assign all of the 133x to the allocator lock.

### 5.4 General interface: synthetic trace, not a real workload

`bench_mm.c` compares malloc, raw pool, explicit mm, and the single-thread wrapper. The MT test compares only malloc and an independent mm for each thread. The sizes are `16,64,256,4096,40000`, each with touch=0/1. A pair has 200,000 pairs per rep. Each cell excludes rep=0 (warmup) and keeps 5 reps. The allocator order rotates with each rep.

The mixed trace has a fixed seed=`0x70ace123`, 16,384 live objects at one time, requested bytes=82,629,072, and 2 rounds per rep with random free. **This is a synthetic trace, not a real workload.** The capacities of the mixed mm are set from the known class peaks. They do not use the default capacities. This is a favorable condition because the load is known in advance. It is not an automatic tuning mechanism. With touch=1, the program does a volatile store at every 64 bytes and at the last byte. This is a fixed 64-byte step. The report does not claim that it equals the cache-line size of the machine.

Setup, pool construction, and warmup are not timed. The raw pool has only 8 slots. The pair mm uses the default reserved amount. For this reason, you cannot treat the hot path speed as the total cost for an equal memory budget. The fallback performance for sizes above the largest class was not measured. The CI comes from two independent bootstraps (one per group). It uses 10,000 draws and seed=7001, and resamples the median ratio of 5 reps. No multiple-comparison correction was done.

| workload / bytes / touch | malloc median ns/pair | mm median ns/pair | malloc/mm | bootstrap 95% CI |
|---|---:|---:|---:|---|
| pair / 40000 / 0 | 67.870625 | 16.361042 | 4.1483 | [4.0869, 4.2751] |
| pair / 40000 / 1 | 236.278333 | 184.616458 | 1.2798 | [1.2614, 1.2945] |
| pair / 4096 / 0 | 13.872708 | 15.383750 | 0.9018 | [0.8214, 0.9161] |
| pair / 4096 / 1 | 30.862708 | 32.177917 | 0.9591 | [0.9396, 0.9822] |

The 40000-byte case gains, but the payload touch reduces the difference. The 4096-byte mm is clearly slower. The 256-byte case and the mixed touch=1 case also have clearly slower results. For the 64-byte wrapper with touch=0, the 4096-byte wrapper with touch=1, and some 64-byte MT cases, the CI crosses 1. You cannot call these clearly faster. The [summary](results/summary.md) lists all of these negative and unclear results. It does not show only the winning rows.

In the same pair loop, the 40000-byte touch=0 mm costs about 9.904167 ns/pair more than the raw pool. This difference includes the NULL check, class selection, the address-range free, and the extra function call. You cannot split it into the causal cost of each part. The convenience of the general interface is not free.

## 6. Memory cost and cold start

The memory measurement uses a separate backing-allocation shim. It counts **requested bytes**, including the handle and the metadata. It is not the same as RSS, the number of pages actually reserved, or the usable size of the allocator. Each class keeps all of its preallocated space even when the pool is empty. Only `destroy` gives it back. A low utilization does not shrink the pool by itself.

- The default mm has 130,368 slots in total. The slot payload space is 10,948,608 bytes in total. `next+state` is 651,840 bytes in total.
- On macOS, creation made 37 backing calls and 37 live blocks. The total is **11,601,552 requested bytes**. This includes the other handles and the alignment padding. After destroy, the live blocks and the requested bytes of the shim are both 0.
- A 4097-byte request goes into the 40000-byte class. The internal waste is **35,903 bytes**. The general interface does not waste only a small amount of padding. `malloc_size` and `malloc_usable_size` are only a lower bound for diagnosis. They are not an RSS comparison.
- `pool_create` and `mm_create` initialize the metadata. They do not guarantee that the payload pages are prefaulted. The page faults and write cost of the first touch of the whole slot batch are not in the hot alloc/free results.
- One cold record of a 40000-byte pool: 1678 slots, 67,120,000 payload bytes, create=6000 ns, first allocation + touch=4,933,000 ns. This is one observation only. It is not a stable cold-start distribution.

The full counts of backing, fragmentation, cold-touch faults, and destroy are in [macos-memory.raw.txt](results/macos-memory.raw.txt). The single cold timing of the pool is in [macos-pool-bench.raw.txt](results/macos-pool-bench.raw.txt). You must evaluate the reserved memory and the creation cost together with the hot-path gain.

## 7. Reproduce

Requirements: a C99 compiler, make, the Python 3 standard library, `nm` for codegen, and `otool` on macOS or `objdump` on Linux. The sanitizer runtime must be available. Valgrind and leaks are optional tools. They are not installed automatically.

```sh
make clean
make verify
make mutants
make doctor
```

Run all commands in the project root. `make verify` runs these steps in order: normal/fault/debug, the recent boundary suite, ASan/UBSan, codegen, slow/MT smoke, pool/mm benchmark smoke, the requested-byte tools, and the strict syntax check. It does not depend on the numbers in this report or on older acceptance sections.

You can also run:

```sh
make test-mm-recent           # recent suite, plus its ASan and UBSan builds
make tsan-mm                  # optional; unavailable runtime returns nonzero
make bench                   # full pool run -> results/bench.txt + summary.txt
make bench-mm                # full synthetic trace -> results/bench-mm.txt + summary
make util memory-mm          # requested-byte accounting, not RSS
make slow mt                 # full no-touch / MT experiments; may take time
python3 summarize.py results/macos-pool-bench.raw.txt
python3 summarize_mm.py results/macos-mm-bench.raw.txt
```

All measurements and tests ran on one macOS arm64 machine. **Linux was not verified. There are no Linux numbers and no cross-platform comparison.** The threads are not pinned (unpinned). The existing load on the machine also affects the timing.

`make clean` removes the build products of this run. It does not delete the curated raw data of the delivery. You can verify the integrity of the results with `shasum -a 256 -c CHECKSUMS.sha256`. The checksum file does not contain itself. After you change any listed file, you must compute the checksums again.

## 8. Navigation, limits, and references

- [MANIFEST.md](MANIFEST.md): the minimal file guide and the mapping to evidence.
- [results/summary.md](results/summary.md): all important tables, and all mm/wrapper cases that are not clearly faster.

The implementation is simplified for a fixed specification and observable errors. It has a capacity limit, reserved memory, significant class fragmentation, no shared thread safety, and no bounded latency for fallback. A single-machine synthetic test and a sanitizer cannot prove that the code has no errors, that it is faster on all platforms, or that it has a worst-case time limit.

This is a teaching implementation written from the public interface and the trade-offs above. The side-index free list, the multiple size classes, preallocation, and bootstrap are all common techniques. It is not a paste of a full existing allocator or textbook example. The report also does not use any public reference as evidence that this implementation passed verification. Background references:

1. [ISO C11 draft N1570, section 7.22.3](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf): the general semantics of malloc/free. This program still builds with C99 flags. It states its own zero-size and capacity behavior.
2. [GNU libc: Aligned Memory Blocks](https://www.gnu.org/software/libc/manual/html_node/Aligned-Memory-Blocks.html): background on alignment. The pool contract follows `pool.h`.
3. [glibc MallocInternals](https://sourceware.org/glibc/wiki/MallocInternals) and [Memory Allocation Tunables](https://sourceware.org/glibc/manual/latest/html_node/Memory-Allocation-Tunables.html): arenas and tcache. They are background for the glibc implementation only.
4. [Apple libmalloc source](https://github.com/apple-oss-distributions/libmalloc): macOS uses a different allocator. The benchmark does not let us infer its internal slow paths.
