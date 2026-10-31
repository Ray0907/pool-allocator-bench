#!/usr/bin/env python3
"""stdlib bootstrap: independent resampling per allocator, fixed seed, median ratio."""
import collections
from pathlib import Path
import random
import statistics as st
import sys

SEED = 7001
DRAWS = 10000

def interval(a, b):
    rng = random.Random(SEED)
    values = sorted(st.median(rng.choices(a, k=len(a))) /
                    st.median(rng.choices(b, k=len(b))) for _ in range(DRAWS))
    return values[249], values[9749]


def summarize(path):
    cells = collections.defaultdict(dict)
    for line in Path(path).read_text().splitlines():
        if not line.startswith('CELL '):
            continue
        r = dict(x.split('=', 1) for x in line.split()[1:])
        if r['phase'] == 'warmup':
            assert r['rep'] == '0'
            continue
        key = tuple(r[k] for k in ('workload', 'size', 'threads', 'touch'))
        reps = cells[key].setdefault(r['allocator'], {})
        assert int(r['rep']) not in reps
        reps[int(r['rep'])] = float(r['ns_pair'])
    expected = {('pair', str(s), '1', str(t)) for s in (16,64,256,4096,40000) for t in (0,1)}
    expected |= {('mixed','0','1',str(t)) for t in (0,1)}
    expected |= {(w,str(s),str(n),str(t)) for w,s in (('pair-mt',64),('pair-mt',40000),('mixed-mt',0)) for n in (1,2,4,8) for t in (0,1)}
    assert set(cells) == expected, 'required benchmark coverage'
    lines = ['## General-interface benchmark: synthetic trace, not a real workload', '',
             'ns/pair. Each row lists the full distribution in rep order. MT = mean of worker ns/pair, not total throughput.',
             'Each cell excludes warmup and has 5 reps. ratio = median(malloc) / median(allocator).',
             f'Bootstrap 95% CI: independent resampling of the per-rep values of two groups, {DRAWS} draws, seed={SEED}. Not pinned to cores. Not a causal inference.', '',
             '| workload | bytes | threads | touch | allocator | per-rep ns/pair | median | worst rep | malloc/allocator | 95% CI | verdict |',
             '|---|---:|---:|---:|---|---|---:|---:|---:|---|---|']
    unclear, overhead = [], []
    for key in sorted(cells, key=lambda k:(k[0],int(k[1]),int(k[2]),int(k[3]))):
        group = cells[key]
        required = {'malloc','mm'}
        if key[0] == 'pair':
            required = {'malloc','mm','wrapper','pool'}
        assert set(group) == required
        data = {}
        for allocator, reps in group.items():
            assert sorted(reps) == [1,2,3,4,5]
            data[allocator] = [reps[i] for i in range(1,6)]
            assert min(data[allocator]) > 0
        for allocator in ('malloc','pool','mm','wrapper'):
            if allocator not in data:
                continue
            values = data[allocator]
            ratio = st.median(data['malloc']) / st.median(values)
            lo, hi = (1.0,1.0) if allocator == 'malloc' else interval(data['malloc'], values)
            status = ('baseline' if allocator == 'malloc' else
                      'clearly faster' if lo > 1 else 'clearly slower' if hi < 1 else 'no clear difference')
            lines.append('| ' + ' | '.join((*key, allocator, ', '.join(f'{v:.6f}' for v in values),
                f'{st.median(values):.6f}', f'{max(values):.6f}', f'{ratio:.4f}', f'[{lo:.4f}, {hi:.4f}]', status)) + ' |')
            if allocator in ('mm','wrapper') and lo <= 1:
                unclear.append(f'{"/".join(key)}/{allocator}: {status}, ratio={ratio:.4f}, CI=[{lo:.4f}, {hi:.4f}]')
        if 'mm' in data and 'pool' in data:
            lo, hi = interval(data['mm'], data['pool'])
            overhead.append('| ' + ' | '.join((key[1],key[3],
                f'{st.median(data["mm"]) - st.median(data["pool"]):.6f}',
                f'{st.median(data["mm"]) / st.median(data["pool"]):.4f}', f'[{lo:.4f}, {hi:.4f}]')) + ' |')
    lines += ['', '### Every row that is not clearly faster than malloc (synthetic trace, not a real workload)', '']
    lines += ['- ' + r for r in unclear] or ['- None. In this run, the CI lower bound of every mm/wrapper row is above 1. Do not extend this to other workloads.']
    lines += ['', '### Extra cost of class selection + address-range free (synthetic trace, not a real workload)', '',
              'Same size, same loop. The difference includes the NULL check of mm, class selection, range search, and the extra function call. Do not split the total difference into causal costs of each part.',
              '| bytes | touch | median(mm) - median(pool), ns | mm/pool | bootstrap 95% CI |',
              '|---:|---:|---:|---:|---|', *overhead]
    return '\n'.join(lines) + '\n'

if __name__ == '__main__':
    assert interval([2]*5, [1]*5) == (2,2)
    print(summarize(sys.argv[1]), end='')
