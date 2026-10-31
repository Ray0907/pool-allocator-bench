# Data Summary

Recomputed summary of the complete macOS full runs. These are not the performance results of the fresh smoke runs.
Hardware fields: Darwin arm64, 10 logical CPUs (4 performance + 6 efficiency). Not pinned to cores.
The original full runs did not record the compiler and OS versions of that time. Do not treat the environment of a fresh build as the original timing environment.

## Pool: batch with payload touch

Each cell has 5 reps. Each rep has 20,000 alloc/free pairs. The ns/op denominator is 40,000 alloc or free operations,
not ns/pair. reuse_not_allocator holds the addresses in advance and does not allocate or free. It is only a lower-bound reference.
ratio > 1 means only that the median is faster. This table has no confidence interval.

| bytes | pattern | malloc ns/op | pool ns/op | reuse ns/op (not allocator) | malloc Mops/s | pool Mops/s | reuse Mops/s | malloc/pool | pool result |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---|
| 40000 | hot_best_case | 79.675 | 46.475 | 44.200 | 12.551 | 21.517 | 22.624 | 1.714x | faster |
| 40000 | batch1000_random_free | 811.100 | 493.350 | 44.075 | 1.233 | 2.027 | 22.689 | 1.644x | faster |
| 40000 | mixed_large_live | 808.025 | 486.950 | 45.125 | 1.238 | 2.054 | 22.161 | 1.659x | faster |
| 16 | hot_best_case | 3.925 | 2.750 | 0.650 | 254.777 | 363.636 | 1538.462 | 1.427x | faster |
| 16 | batch1000_random_free | 3.975 | 1.725 | 0.800 | 251.572 | 579.710 | 1250.000 | 2.304x | faster |
| 16 | mixed_large_live | 55.700 | 16.600 | 4.825 | 17.953 | 60.241 | 207.254 | 3.355x | faster |
| 64 | hot_best_case | 4.200 | 2.800 | 0.625 | 238.095 | 357.143 | 1600.000 | 1.500x | faster |
| 64 | batch1000_random_free | 5.650 | 1.575 | 0.775 | 176.991 | 634.921 | 1290.323 | 3.587x | faster |
| 64 | mixed_large_live | 45.700 | 15.050 | 1.225 | 21.882 | 66.445 | 816.327 | 3.037x | faster |
| 256 | hot_best_case | 4.275 | 2.400 | 0.750 | 233.918 | 416.667 | 1333.333 | 1.781x | faster |
| 256 | batch1000_random_free | 6.250 | 1.950 | 0.775 | 160.000 | 512.821 | 1290.323 | 3.205x | faster |
| 256 | mixed_large_live | 45.450 | 9.625 | 0.750 | 22.002 | 103.896 | 1333.333 | 4.722x | faster |
| 4096 | hot_best_case | 11.925 | 6.275 | 4.800 | 83.857 | 159.363 | 208.333 | 1.900x | faster |
| 4096 | batch1000_random_free | 21.500 | 12.850 | 4.925 | 46.512 | 77.821 | 203.046 | 1.673x | faster |
| 4096 | mixed_large_live | 73.225 | 66.325 | 4.900 | 13.657 | 15.077 | 204.082 | 1.104x | faster |
POOL_BATCH_WINS=15/15 (median only; not a significance test)

Source file `macos-pool-bench.raw.txt`: the clock resolution and the observed minimum positive delta are both 1000 ns.
The p50 of a single call is often quantized to 0. The raw quantiles are kept. Do not read them as zero cost. No timer subtraction was done.

## No payload touch: pair batch and per-pair tail

Source `macos-slow.raw.txt`. 6 sizes, 5 measured reps, and a rep=0 warmup.
Each rep has 2,000,000 pairs. For the tail, each rep has 1,000,000 samples. Each sample wraps one full alloc/free pair (including sink/check/timer).
mach_absolute_time tick / observed granularity = 41.666666667 ns。

| bytes | malloc median ns/pair | pool median ns/pair | malloc/pool |
|---:|---:|---:|---:|
| 16 | 7.002979 | 4.673000 | 1.499 |
| 256 | 7.769833 | 4.679812 | 1.660 |
| 4096 | 13.653271 | 4.627667 | 2.950 |
| 40000 | 67.245500 | 4.681354 | 14.365 |
| 262144 | 68.666104 | 4.713604 | 14.568 |
| 1048576 | 68.752271 | 4.710521 | 14.595 |

| bytes | allocator | p50 ns | p99 ns | p99.9 ns | p99.99 ns | worst max ns |
|---:|---|---:|---:|---:|---:|---:|
| 16 | malloc | <tick | 41.666667 | 41.666667 | 83.333333 | 1458.333333 |
| 16 | pool | <tick | 41.666667 | 41.666667 | 83.333333 | 1458.333333 |
| 256 | malloc | <tick | 41.666667 | 41.666667 | 83.333333 | 750.000000 |
| 256 | pool | <tick | 41.666667 | 41.666667 | 41.666667 | 1125.000000 |
| 4096 | malloc | <tick | 41.666667 | 41.666667 | 166.666667 | 2291.666667 |
| 4096 | pool | <tick | 41.666667 | 41.666667 | 41.666667 | 4625.000000 |
| 40000 | malloc | 83.333333 | 83.333333 | 166.666667 | 250.000000 | 15875.000000 |
| 40000 | pool | <tick | 41.666667 | 41.666667 | 83.333333 | 8000.000000 |

The quantiles are the median of the per-rep quantiles. The worst max is the largest value over all reps. `<tick` is not 0 ns.
This is synthetic data after warmup. The max is not a latency bound. The p50/p99 of 40000-byte malloc are higher than those of small objects,
but this does not show which internal branch of an allocator is the cause. The pool also has outliers (the worst max at 4096 is larger than that of malloc).

## Pool multi-thread scaling

Source `macos-mt.raw.txt`. Each worker has 1,000,000 pairs and 5 measured reps. Each worker has its own pool. No payload touch.
ns/pair is the worker mean of each rep, then the median. It is not total throughput.
N/1 is the ratio of this value for the same allocator. The median slowest-worker and the worst rep are also kept, so that the mean is not the only view.

| bytes | allocator | threads | median worker mean ns/pair | N/1 | median slowest ns/pair | worst mean | worst slowest |
|---:|---|---:|---:|---:|---:|---:|---:|
| 64 | malloc | 1 | 6.976750 | 1.000 | 6.976750 | 7.168458 | 7.168458 |
| 64 | malloc | 2 | 7.464208 | 1.070 | 7.502125 | 7.494792 | 7.553042 |
| 64 | malloc | 4 | 8.962323 | 1.285 | 9.101000 | 10.222875 | 10.356500 |
| 64 | malloc | 8 | 11.749917 | 1.684 | 14.732792 | 12.335729 | 15.192000 |
| 64 | pool | 1 | 5.037042 | 1.000 | 5.037042 | 5.080375 | 5.080375 |
| 64 | pool | 2 | 4.769562 | 0.947 | 4.884167 | 4.872667 | 5.118125 |
| 64 | pool | 4 | 6.964135 | 1.383 | 8.777167 | 7.077229 | 9.222458 |
| 64 | pool | 8 | 11.959990 | 2.374 | 15.621167 | 12.527156 | 16.144625 |
| 40000 | malloc | 1 | 63.797375 | 1.000 | 63.797375 | 64.510083 | 64.510083 |
| 40000 | malloc | 2 | 892.104562 | 13.983 | 892.332125 | 905.132562 | 905.276208 |
| 40000 | malloc | 4 | 2143.413125 | 33.597 | 2175.525875 | 2184.606750 | 2214.098500 |
| 40000 | malloc | 8 | 8480.721276 | 132.932 | 8506.335250 | 8502.785016 | 8522.477000 |
| 40000 | pool | 1 | 4.649333 | 1.000 | 4.649333 | 5.284542 | 5.284542 |
| 40000 | pool | 2 | 6.649625 | 1.430 | 6.754875 | 6.824146 | 6.894750 |
| 40000 | pool | 4 | 7.433615 | 1.599 | 9.543333 | 9.489052 | 11.906083 |
| 40000 | pool | 8 | 15.223214 | 3.274 | 17.549625 | 18.653490 | 21.300042 |

The 8/1 ratio of 40000-byte malloc is about 132.932x. For the pool it is about 3.274x. The 64-byte 8-thread pool does not beat malloc.
This is a single-machine, unpinned observation. It is not a causal proof of lock contention. It is not a safety test of a shared pool.

## General interface：synthetic trace, not a real workload

Source `macos-mm-bench.raw.txt`. 5 measured reps. The warmup rep=0 is excluded.
pair: 200,000 pairs per rep. mixed: 16,384 live allocations x 2 rounds per rep. Live requested bytes=82,629,072.
touch=1: a volatile store every 64 bytes and at the last byte. MT is the worker ns/pair mean, not throughput.
ratio=median(malloc)/median(mm). The bootstrap 95% CI uses independent resampling with replacement for each of the two groups:
10,000 draws, seed=7001, 5 reps per group. The sample is small, no multiple-comparison correction was done, and this is not a causal inference.

| workload | bytes | threads | touch | malloc median ns/pair | mm median ns/pair | mm worst rep | ratio | 95% CI | verdict |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| mixed | 0 | 1 | 0 | 39.899190 | 18.559774 | 20.060221 | 2.1498 | [1.9890, 2.2457] | clearly faster |
| mixed | 0 | 1 | 1 | 172.130585 | 181.975047 | 183.588664 | 0.9459 | [0.9314, 0.9643] | clearly slower |
| mixed-mt | 0 | 1 | 0 | 38.525899 | 19.358317 | 21.715800 | 1.9901 | [1.7741, 2.1963] | clearly faster |
| mixed-mt | 0 | 1 | 1 | 168.000539 | 179.796855 | 180.526733 | 0.9344 | [0.9306, 0.9721] | clearly slower |
| mixed-mt | 0 | 2 | 0 | 110.824585 | 21.985372 | 23.059209 | 5.0408 | [4.8061, 5.4735] | clearly faster |
| mixed-mt | 0 | 2 | 1 | 304.569880 | 266.482035 | 268.851598 | 1.1429 | [1.1250, 1.1594] | clearly faster |
| mixed-mt | 0 | 4 | 0 | 371.083260 | 25.685946 | 30.845960 | 14.4469 | [12.0302, 15.2510] | clearly faster |
| mixed-mt | 0 | 4 | 1 | 567.338943 | 454.500516 | 458.374659 | 1.2483 | [1.1227, 1.3161] | clearly faster |
| mixed-mt | 0 | 8 | 0 | 821.015994 | 36.877473 | 43.292363 | 22.2633 | [18.9645, 24.7577] | clearly faster |
| mixed-mt | 0 | 8 | 1 | 1003.636519 | 834.628105 | 869.529565 | 1.2025 | [1.1374, 1.3369] | clearly faster |
| pair | 16 | 1 | 0 | 7.139375 | 5.917708 | 6.165417 | 1.2064 | [1.1580, 1.2971] | clearly faster |
| pair | 16 | 1 | 1 | 7.525833 | 5.271667 | 5.335625 | 1.4276 | [1.4105, 1.4446] | clearly faster |
| pair | 64 | 1 | 0 | 7.781875 | 7.551458 | 7.560625 | 1.0305 | [1.0260, 1.0367] | clearly faster |
| pair | 64 | 1 | 1 | 8.058750 | 7.826250 | 7.839375 | 1.0297 | [1.0249, 1.0375] | clearly faster |
| pair | 256 | 1 | 0 | 8.053750 | 10.548542 | 10.682708 | 0.7635 | [0.7539, 0.8672] | clearly slower |
| pair | 256 | 1 | 1 | 9.057292 | 11.597292 | 12.219167 | 0.7810 | [0.7412, 0.8758] | clearly slower |
| pair | 4096 | 1 | 0 | 13.872708 | 15.383750 | 16.889583 | 0.9018 | [0.8214, 0.9161] | clearly slower |
| pair | 4096 | 1 | 1 | 30.862708 | 32.177917 | 32.712917 | 0.9591 | [0.9396, 0.9822] | clearly slower |
| pair | 40000 | 1 | 0 | 67.870625 | 16.361042 | 16.588958 | 4.1483 | [4.0869, 4.2751] | clearly faster |
| pair | 40000 | 1 | 1 | 236.278333 | 184.616458 | 185.123542 | 1.2798 | [1.2614, 1.2945] | clearly faster |
| pair-mt | 64 | 1 | 0 | 8.388750 | 6.547292 | 6.563125 | 1.2813 | [1.2497, 1.3328] | clearly faster |
| pair-mt | 64 | 1 | 1 | 8.674792 | 6.824167 | 6.888542 | 1.2712 | [1.2554, 1.2762] | clearly faster |
| pair-mt | 64 | 2 | 0 | 8.475000 | 9.603125 | 10.143125 | 0.8825 | [0.8355, 0.9088] | clearly slower |
| pair-mt | 64 | 2 | 1 | 8.896354 | 10.144896 | 10.283750 | 0.8769 | [0.8603, 1.2071] | no clear difference |
| pair-mt | 64 | 4 | 0 | 10.273802 | 8.474792 | 8.553437 | 1.2123 | [1.1121, 1.2890] | clearly faster |
| pair-mt | 64 | 4 | 1 | 10.761042 | 8.906250 | 9.458385 | 1.2083 | [1.1277, 1.2693] | clearly faster |
| pair-mt | 64 | 8 | 0 | 12.511719 | 12.500312 | 18.995208 | 1.0009 | [0.6587, 1.1212] | no clear difference |
| pair-mt | 64 | 8 | 1 | 13.530938 | 12.528672 | 16.515677 | 1.0800 | [0.8193, 1.3100] | no clear difference |
| pair-mt | 40000 | 1 | 0 | 68.058750 | 14.102083 | 15.368125 | 4.8261 | [4.4286, 5.1077] | clearly faster |
| pair-mt | 40000 | 1 | 1 | 235.208333 | 181.912708 | 185.201042 | 1.2930 | [1.2700, 1.3037] | clearly faster |
| pair-mt | 40000 | 2 | 0 | 999.605625 | 14.795313 | 17.351042 | 67.5623 | [57.6107, 74.8797] | clearly faster |
| pair-mt | 40000 | 2 | 1 | 608.798333 | 184.936354 | 186.225208 | 3.2919 | [3.2052, 4.3989] | clearly faster |
| pair-mt | 40000 | 4 | 0 | 3029.997031 | 22.293437 | 30.115625 | 135.9143 | [100.6121, 137.8922] | clearly faster |
| pair-mt | 40000 | 4 | 1 | 3093.616823 | 210.804531 | 228.644792 | 14.6753 | [13.5302, 15.3530] | clearly faster |
| pair-mt | 40000 | 8 | 0 | 7566.327604 | 26.334557 | 30.875208 | 287.3155 | [245.0616, 296.7933] | clearly faster |
| pair-mt | 40000 | 8 | 1 | 7803.070521 | 236.414740 | 250.014193 | 33.0059 | [31.1061, 33.6154] | clearly faster |

### All mm / wrapper cases that are not clearly faster than malloc

- mixed/0/1/1/mm: clearly slower, ratio=0.9459, CI=[0.9314, 0.9643]
- mixed-mt/0/1/1/mm: clearly slower, ratio=0.9344, CI=[0.9306, 0.9721]
- pair/64/1/0/wrapper: no clear difference, ratio=0.9961, CI=[0.9917, 1.0093]
- pair/256/1/0/mm: clearly slower, ratio=0.7635, CI=[0.7539, 0.8672]
- pair/256/1/0/wrapper: clearly slower, ratio=0.7813, CI=[0.7387, 0.8373]
- pair/256/1/1/mm: clearly slower, ratio=0.7810, CI=[0.7412, 0.8758]
- pair/256/1/1/wrapper: clearly slower, ratio=0.8172, CI=[0.7331, 0.9059]
- pair/4096/1/0/mm: clearly slower, ratio=0.9018, CI=[0.8214, 0.9161]
- pair/4096/1/0/wrapper: clearly slower, ratio=0.8905, CI=[0.8886, 0.9174]
- pair/4096/1/1/mm: clearly slower, ratio=0.9591, CI=[0.9396, 0.9822]
- pair/4096/1/1/wrapper: no clear difference, ratio=0.9905, CI=[0.9379, 1.0353]
- pair-mt/64/2/0/mm: clearly slower, ratio=0.8825, CI=[0.8355, 0.9088]
- pair-mt/64/2/1/mm: no clear difference, ratio=0.8769, CI=[0.8603, 1.2071]
- pair-mt/64/8/0/mm: no clear difference, ratio=1.0009, CI=[0.6587, 1.1212]
- pair-mt/64/8/1/mm: no clear difference, ratio=1.0800, CI=[0.8193, 1.3100]


### Extra cost of the general interface

These differences cannot be split into the cost of one range lookup. They include class selection, the NULL check, the extra call, and so on.
| bytes | touch | median(mm) - median(pool), ns | mm/pool | bootstrap 95% CI |
|---:|---:|---:|---:|---|
| 16 | 0 | -0.048750 | 0.9918 | [0.8502, 1.0627] |
| 16 | 1 | 0.732917 | 1.1615 | [1.1469, 1.1756] |
| 64 | 0 | 1.864166 | 1.3278 | [1.1490, 1.5490] |
| 64 | 1 | 3.283333 | 1.7227 | [1.7125, 1.7372] |
| 256 | 0 | 4.005209 | 1.6121 | [1.4194, 2.9692] |
| 256 | 1 | 6.793959 | 2.4144 | [2.1530, 2.5439] |
| 4096 | 0 | 8.774375 | 2.3276 | [2.2869, 2.5782] |
| 4096 | 1 | 12.455625 | 1.6316 | [1.5769, 1.6752] |
| 40000 | 0 | 9.904167 | 2.5339 | [2.4230, 2.8766] |
| 40000 | 1 | 13.965625 | 1.0818 | [1.0707, 1.0853] |

## Recompute

```sh
python3 summarize.py results/macos-pool-bench.raw.txt
python3 summarize_mm.py results/macos-mm-bench.raw.txt
```

These two programs first check that all required cells and reps are present. Do not use --smoke output as a full benchmark.
The fresh native logs verify only the build, the functions, the checkers, and the smoke runs. They add no Linux performance conclusion.
