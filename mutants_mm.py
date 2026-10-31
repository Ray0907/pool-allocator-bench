#!/usr/bin/env python3
"""Compile each mutation; only an explicit public-API assertion counts as a kill."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parent
mm = (root / 'mymalloc.c').read_text()
pool = (root / 'pool.c').read_text()
mutations = [
    ('mm_boundary', 'mm', 'size <= m->cfg.sizes[i]', 'size < m->cfg.sizes[i]'),
    ('contains_upper', 'pool', 'address - base < p->nslots * p->stride', 'address - base <= p->nslots * p->stride'),
    ('contains_lower', 'pool', 'address >= base &&', 'address > base &&'),
    ('mm_fallthrough', 'mm', 'return pool_alloc(m->pools[i]);', '{ void *p = pool_alloc(m->pools[i]); if (p != NULL) return p; }'),
    ('mm_foreign', 'mm', 'if (!m->cfg.fallback_to_system) return MM_ERR_OUTSIDE;', 'if (!m->cfg.fallback_to_system) return MM_OK;'),
    ('mm_null', 'mm', 'if (ptr == NULL) return MM_OK;', 'if (ptr == NULL) return MM_ERR_OUTSIDE;'),
    ('mm_fallback', 'mm', 'if (!m->cfg.fallback_to_system) return NULL;', 'return NULL; /* mutant: fallback ignored */'),
    ('mm_alignment', 'mm', ' || cfg->sizes[i] % 16 != 0', ''),
    ('mm_exhaustion_uses_system', 'mm', 'return pool_alloc(m->pools[i]);',
     '{ void *p = pool_alloc(m->pools[i]); if (p != NULL || !m->cfg.fallback_to_system) return p; break; }'),
]
flags = ['-std=c99', '-pedantic', '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-pthread']
rows = []
for name, target, old, new in mutations:
    work = root / '.mut' / name
    work.mkdir(parents=True, exist_ok=True)
    sources = {'mm': mm, 'pool': pool}
    assert sources[target].count(old) == 1, name
    sources[target] = sources[target].replace(old, new)
    (work / 'mymalloc.c').write_text(sources['mm'])
    (work / 'pool.c').write_text(sources['pool'])
    cmd = ['cc', *flags, '-I', str(root), str(root / 'test_mm.c'),
           str(work / 'mymalloc.c'), str(work / 'pool.c'), '-o', str(work / 'test')]
    build = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log = '$ ' + ' '.join(cmd) + '\n' + build.stdout
    if build.returncode:
        result = 'ERROR build (not a kill)'
    else:
        run = subprocess.run([str(work / 'test'), '--quick'], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, timeout=30,
                             env=dict(os.environ))
        log += run.stdout + f'\nexit={run.returncode}\n'
        result = 'KILLED' if run.returncode != 0 and 'FAIL ' in run.stdout else 'SURVIVED'
    (root / 'results' / 'mutants' / (name + '.txt')).write_text(log)
    rows.append((name, result))
    print(f'{name}: {result}')
summary = 'mutant\tresult\n' + ''.join(f'{a}\t{b}\n' for a, b in rows)
summary += f'KILLED {sum(b == "KILLED" for _, b in rows)}/{len(rows)}\n'
(root / 'results' / 'mutants-mm.txt').write_text(summary)
print(summary, end='')
sys.exit(0 if all(b == 'KILLED' for _, b in rows) else 1)
