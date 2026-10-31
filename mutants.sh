#!/bin/sh
# All processes are synchronous children; never kill by process name.
set -eu
cd "$(dirname "$0")"
python3 - <<'PY'
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path.cwd()
scratch = root / ".mut"
evidence = root / "results" / "mutants"
scratch.mkdir(exist_ok=True)
evidence.mkdir(parents=True, exist_ok=True)
source = (root / "pool.c").read_text()
mutations = [
    ("double_free_check",
     "    if (p->state[idx] != 1) return POOL_ERR_DOUBLE_FREE;",
     "    /* mutant: omit double-free check */"),
    ("slot_start_check",
     "    if (delta % p->stride != 0) return POOL_ERR_NOT_START;",
     "    /* mutant: omit slot-start check */"),
    ("index_bound_check",
     "    if (idx >= p->nslots) return POOL_ERR_OUTSIDE;",
     "    /* mutant: omit index bound */"),
    ("base_alignment",
     "    uintptr_t aligned = (address + align - 1) & ~(uintptr_t)(align - 1);\n"
     "    p->base = (unsigned char *)aligned;",
     "    p->base = p->raw; /* mutant: raw, unaligned base */"),
    ("exhaustion_off_by_one",
     "p->head == UINT32_MAX",
     "p->free_count <= 1"),
    ("free_state_reset",
     "    p->state[idx] = 0;",
     "    /* mutant: forget to clear allocated state */"),
    ("power_of_two_check",
     " || (align & (align - 1)) != 0",
     ""),
]
flags = ["-std=c99", "-pedantic", "-Wall", "-Wextra", "-Werror", "-O1",
         "-g", "-fsanitize=address", "-fno-omit-frame-pointer"]
env = dict(os.environ, ASAN_OPTIONS="allocator_may_return_null=1:detect_leaks=0")
rows = []
failed = False
for name, old, new in mutations:
    work = scratch / name
    work.mkdir(exist_ok=True)
    shutil.copyfile(root / "pool.c", work / "pool.c")
    if source.count(old) != 1:
        rows.append((name, "ERROR: textual anchor not unique"))
        failed = True
        continue
    (work / "pool.c").write_text(source.replace(old, new, 1))
    commands = [
        ["cc", *flags, "-I", str(root), "-include", str(root / "test_alloc.h"),
         "-Dmalloc=q2_test_malloc", "-Dfree=q2_test_free", "-c",
         str(work / "pool.c"), "-o", str(work / "pool.o")],
        ["cc", *flags, "-I", str(root), str(root / "test_pool.c"),
         str(root / "test_alloc.c"), str(work / "pool.o"),
         "-o", str(work / "test_pool")],
    ]
    log = []
    built = True
    for command in commands:
        run = subprocess.run(command, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
        log.append("$ " + " ".join(command) + "\n" + run.stdout)
        if run.returncode:
            built = False
            break
    if not built:
        result = "ERROR: build failed (not a kill)"
        failed = True
    else:
        run = subprocess.run([str(work / "test_pool")], env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log.append(run.stdout + f"\nexit={run.returncode}\n")
        if run.returncode == 0:
            result = "SURVIVED"
            print("SURVIVED " + name)
            failed = True
        elif "FAIL " in run.stdout or "ERROR: AddressSanitizer" in run.stdout:
            result = "KILLED"
        else:
            result = "ERROR: unexplained failure"
            failed = True
    (evidence / (name + ".txt")).write_text("\n".join(log))
    rows.append((name, result))
    print(f"{name}: {result}")
table = "mutant\tresult\n" + "".join(f"{a}\t{b}\n" for a, b in rows)
table += f"KILLED {sum(b == 'KILLED' for _, b in rows)}/{len(mutations)}\n"
(root / "results" / "mutants.txt").write_text(table)
print(table, end="")
sys.exit(1 if failed else 0)
PY
python3 ./mutants_mm.py
