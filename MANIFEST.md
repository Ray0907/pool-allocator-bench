# Delivery File Guide

Run make in the project root only. All paths below are relative to the project root.

## Main report and integrity

| File | Purpose |
|---|---|
| `README.md` | The only main report: problem, design, API contract, correctness, performance and limits, reproduction commands, and references |
| `Makefile` | C99 strict builds, normal/fault/debug, sanitizers, mutants, codegen, smoke/full experiments, doctor |
| `CHECKSUMS.sha256` | SHA-256 of each regular file (it does not list itself). It is not a digital signature |

## Unchanged core

`pool.c`, `pool.h`, `pool_util.h`, `mymalloc.c`, `mymalloc.h`: byte-identical to the source working directory. They were only copied. The allocator behavior and the public API are not changed.

## Minimal test dependencies

| File | Purpose |
|---|---|
| `test_pool.c` | Pool public API, overflow, alignment, error handling, and a seeded oracle |
| `test_alloc.c`, `test_alloc.h` | Pool fault/alignment build. `util.c` also uses the declarations, but it supplies its own counters |
| `test_mm.c` | General-interface public API, fallback/failure cleanup, differential test, and owner-thread test |
| `test_mm_recent.c` | Extra public-API tests for the class rule and the hint/range boundaries |
| `mm_test_alloc.c`, `mm_test_alloc.h` | mm fault build and requested-byte counters |
| `mutants.sh`, `mutants_mm.py` | 7 pool mutants and 9 mm mutants on temporary copies. The first script calls the second |
| `check_mm_tools.sh` | Separate optional TSan check. One instance per thread. A failure returns a real non-zero status |
| `leakcheck.sh` | Linux LSan (only where available on Linux). Raw diagnostics of the macOS leaks/ASan limit and of the native pool fallback |

## Experiments and required small tools

| File | Purpose |
|---|---|
| `bench.c` | Fixed-pool payload-touch benchmark, cold record, per-call tails |
| `bench_mm.c` | General-interface synthetic pair/mixed/MT trace |
| `trace_mm.c`, `trace_mm.h` | Fixed seed, mixed size/order/class peaks |
| `slow_check.c` | Batch and per-pair alloc/free latency with no payload touch |
| `mt_check.c` | Synchronized-start scaling with one independent pool per thread |
| `check_common.h` | Platform timer, machine metadata, statistics, and small assertion tools |
| `util.c` | Requested backing bytes of the fixed pool at different live ratios |
| `memory_mm.c` | mm requested bytes, class fragmentation, cold-touch faults, destroy reclaim |
| `summarize.py` | Check of complete pool cells/reps, median and tail summary |
| `summarize_mm.py` | Check of complete mm cells/reps, bootstrap CI, all negative cases, and wrapper/pool cost |
| `check_experiments.py` | Structure and distribution checks for the slow/MT smoke runs. Call-preservation checks of assembly, objects, and disassembly. It has no text-based acceptance dependency on an old report |

## Condensed evidence set

| File | Conclusion it supports |
|---|---|
| `results/summary.md` | 15 pool batch cells, no-touch/tail, MT scaling, 36 mm configs, and all mm/wrapper cases that are not clearly faster |
| `results/macos-pool-bench.raw.txt` | The retained full payload-touch run. 225 measured raw cells. Duplicate appended derived summaries are removed |
| `results/macos-mm-bench.raw.txt` | The retained full synthetic trace, with warmup and measured reps |
| `results/macos-slow.raw.txt` | The retained full run with no payload touch, with timer overhead, faults, and batch/tail distributions |
| `results/macos-mt.raw.txt` | The retained full MT run with independent pools, with all workers, ready/start spread, and reps |
| `results/macos-memory.raw.txt` | The retained pool/mm requested-byte and cold-touch counts. Not RSS |
| `results/macos-verification.txt` | Native clean/verify/mutants/doctor from a clean extraction, with optional TSan listed separately. The exit code of each item is kept |
| `results/macos-codegen.txt` | Results of the macOS optimized assembly/object/disassembly checks |

The performance raw files are completed macOS full runs. The fresh verification re-ran only the functional tests, the tools, and the smoke runs. It did not use smoke results to overwrite the full-run performance evidence. In a few logs, private scratch paths are hidden as `<relocated>`. The exit codes, the test counts, the timing numbers, and the tool errors are not changed.

The delivery has no binaries, object files, full disassembly text, Python caches, temporary mutant trees, old review documents, duplicate reports, remote-machine connection settings, or whole historical results trees. The build and results files that appear after you run make are not part of the original delivery. `make clean` does not delete the curated evidence.
