"""Summarize the raw C benchmark; no third-party packages.
Checks completeness before computing medians. Smoke data is never accepted here.
"""
import csv
from collections import defaultdict
from pathlib import Path
from statistics import median
import sys

def summarize(path):
    lines = Path(path).read_text().splitlines()
    assert any("smoke=0 reps=5" in line for line in lines), "not a 5-rep full run"
    assert lines.count("BENCH: PASS") == 1, "benchmark incomplete"
    header = next(line for line in lines if line.startswith("RAW_HEADER,")).split(",")[1:]
    rows = [dict(zip(header, next(csv.reader([line]))[1:]))
            for line in lines if line.startswith("RAW,")]
    groups = defaultdict(list)
    for row in rows:
        groups[(int(row["size"]), row["pattern"], row["method"])].append(row)
        assert int(row["pairs"]) == 20000
        assert int(row["batch_ns"]) > 0
        assert abs(float(row["ns_per_op"]) - int(row["batch_ns"]) / 40000) < 1e-5
        for call in ("alloc", "free"):
            values = [int(row[call + "_" + field + "_ns"]) for field in ("p50", "p99", "p999", "max")]
            assert values == sorted(values), "invalid tail ordering"
        if row["pattern"] == "mixed_large_live":
            assert int(row["live_payload_bytes"]) >= 64 * 1024 * 1024
    sizes = (40000, 16, 64, 256, 4096)
    patterns = ("hot_best_case", "batch1000_random_free", "mixed_large_live")
    methods = ("malloc", "pool", "reuse_not_allocator")
    expected = {(s, p, m) for s in sizes for p in patterns for m in methods}
    assert set(groups) == expected
    for key in expected:
        assert len(groups[key]) == 5
        assert {int(row["rep"]) for row in groups[key]} == set(range(1, 6))
    def med(key, field):
        return median(float(row[field]) for row in groups[key])
    print("SUMMARY: 225 raw cells; 45 size/pattern/method groups; 5 reps/group; 20000 pairs/rep.")
    for line in lines:
        if line.startswith(("CONFIG ", "CLOCK ", "COLD,")):
            print(line)
    timer = [next(csv.reader([line]))[1:] for line in lines if line.startswith("TIMER,")]
    assert len(timer) == 5
    print("TIMER medians p50/p99/p99.9/max ns: " +
          "/".join(str(int(median(int(r[i]) for r in timer))) for i in range(2, 6)) +
          "; worst p99.9/max ns: " + str(max(int(r[4]) for r in timer)) +
          "/" + str(max(int(r[5]) for r in timer)))
    print("\n## Batch: median across five reps, payload touch included")
    print("| bytes | pattern | malloc ns/op | pool ns/op | reuse ns/op (not allocator) | malloc Mops/s | pool Mops/s | reuse Mops/s | malloc/pool | pool result |")
    print("|---:|---|---:|---:|---:|---:|---:|---:|---:|---|")
    wins = 0
    for size in sizes:
        for pattern in patterns:
            keys = [(size, pattern, method) for method in methods]
            ns = [med(k, "ns_per_op") for k in keys]
            throughput = [med(k, "mops_s") for k in keys]
            ratio = ns[0] / ns[1]
            faster = ratio > 1
            wins += faster
            print(f"| {size} | {pattern} | " + " | ".join(f"{v:.3f}" for v in ns + throughput) +
                  f" | {ratio:.3f}x | {'faster' if faster else 'NOT faster'} |")
    print(f"POOL_BATCH_WINS={wins}/15 (median only; not a significance test)")
    print("\n## Individual call tails: ns, uncorrected; timer/dispatch overhead included")
    print("Each slash tuple is p50/p99/p99.9/max (medians of per-rep quantiles). Worst columns are worst-rep p99.9/max.")
    print("| bytes | pattern | method | alloc median tails | alloc worst | free median tails | free worst |")
    print("|---:|---|---|---|---|---|---|")
    for size in sizes:
        for pattern in patterns:
            for method in methods:
                key = size, pattern, method
                values = []
                for call in ("alloc", "free"):
                    values.append("/".join(str(int(med(key, call+"_"+q+"_ns")))
                                           for q in ("p50", "p99", "p999", "max")))
                    values.append("/".join(str(max(int(r[call+"_"+q+"_ns"]) for r in groups[key]))
                                           for q in ("p999", "max")))
                print(f"| {size} | {pattern} | {method} | " + " | ".join(values) + " |")
    print("SUMMARY: PASS")

if __name__ == "__main__":
    summarize(sys.argv[1] if len(sys.argv) == 2 else "results/bench.txt")
