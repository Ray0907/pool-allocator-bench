#!/usr/bin/env python3
"""Check experiment output, independently recomputing the reported statistics."""
import math
import platform
from pathlib import Path
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def records(text, kind):
    return [dict(field.split('=', 1) for field in line.split()[1:])
            for line in text.splitlines() if line.startswith(kind + ' ')]


def near(actual, expected):
    return math.isclose(float(actual), expected, rel_tol=0, abs_tol=0.000002)


def validate(slow, mt, smoke):
    reps = 1 if smoke else 5
    pairs = 1000 if smoke else 2000000
    samples = 2000 if smoke else 1000000
    for output in (slow, mt):
        config, = records(output, 'CONFIG')
        require(config['mode'] == ('smoke' if smoke else 'full'), 'mode')
        require(int(config['reps']) == reps and config['warmup'] == '1', 'repetitions')
        clock, = records(output, 'CLOCK')
        require(float(clock['tick_ns']) > 0 and float(clock['granularity_ns']) > 0, 'clock')
        require('quantized' in output and 'not pinned' in output, 'limits')
    config, = records(slow, 'CONFIG')
    require(int(config['pairs']) == pairs and int(config['samples']) == samples, 'loop sizes')
    clock, = records(slow, 'CLOCK')
    tick = float(clock['tick_ns'])
    granularity = float(clock['granularity_ns'])
    overhead = records(slow, 'OVERHEAD')
    require(len(overhead) == reps, 'timer overhead reps')
    require(all(float(r['mean_ns']) >= 0 and int(r['samples']) > 0 for r in overhead), 'overhead')
    batch = records(slow, 'BATCH')
    summaries = records(slow, 'BATCH_SUMMARY')
    require(len(batch) == 12 * (reps + 1) and len(summaries) == 12, 'batch cells')
    for size in (16, 256, 4096, 40000, 262144, 1048576):
        for allocator in ('malloc', 'pool'):
            rows = [r for r in batch if r['size'] == str(size) and r['allocator'] == allocator]
            require(sorted(int(r['rep']) for r in rows) == list(range(reps + 1)), 'batch reps')
            require(all(r['phase'] == ('warmup' if r['rep'] == '0' else 'measured') for r in rows), 'warmup')
            require(all(int(r['minor_faults']) >= 0 and float(r['ns_pair']) > 0 for r in rows), 'faults/time')
            measured = [float(r['ns_pair']) for r in rows if r['phase'] == 'measured']
            summary, = [r for r in summaries if r['size'] == str(size) and r['allocator'] == allocator]
            require(near(summary['median_ns'], statistics.median(measured)), 'batch median')
            require(near(summary['worst_ns'], max(measured)), 'batch worst')
    tails = records(slow, 'TAIL')
    summaries = records(slow, 'TAIL_SUMMARY')
    require(len(tails) == 8 * reps and len(summaries) == 8, 'tail cells')
    names = ('p50', 'p99', 'p99_9', 'p99_99', 'max')
    for size in (16, 256, 4096, 40000):
        for allocator in ('malloc', 'pool'):
            rows = [r for r in tails if r['size'] == str(size) and r['allocator'] == allocator]
            require(sorted(int(r['rep']) for r in rows) == list(range(1, reps + 1)), 'tail reps')
            require(all(int(r['minor_faults']) >= 0 for r in rows), 'tail faults')
            require(all([int(r[n + '_ticks']) for n in names] == sorted(int(r[n + '_ticks']) for n in names)
                        for r in rows), 'percentile order')
            summary, = [r for r in summaries if r['size'] == str(size) and r['allocator'] == allocator]
            for name in names:
                values = [int(r[name + '_ticks']) for r in rows]
                expected = (max(values) if name == 'max' else statistics.median(values)) * tick
                actual = summary[('worst_max' if name == 'max' else name) + '_ns']
                require(actual == '<tick' if expected < granularity else
                        math.isclose(float(actual), expected, rel_tol=0.00001, abs_tol=0.001), 'tail summary')
    config, = records(mt, 'CONFIG')
    require(int(config['pairs']) == (1000 if smoke else 1000000), 'mt loops')
    machine, = records(mt, 'MACHINE')
    require(int(machine['logical_cpus']) > 0, 'CPU count')
    require('performance_cpus' in machine and 'efficiency_cpus' in machine, 'core counts')
    require('cause NOT proven' in mt and 'one machine' in mt, 'causality limits')
    rows = records(mt, 'MT')
    summaries = records(mt, 'MT_SUMMARY')
    require(len(rows) == 16 * (reps + 1) and len(summaries) == 16, 'mt cells')
    for size in (64, 40000):
        for allocator in ('malloc', 'pool'):
            for threads in (1, 2, 4, 8):
                cell = [r for r in rows if r['size'] == str(size) and
                        r['allocator'] == allocator and r['threads'] == str(threads)]
                require(sorted(int(r['rep']) for r in cell) == list(range(reps + 1)), 'mt reps')
                require(all(r['phase'] == ('warmup' if r['rep'] == '0' else 'measured') for r in cell), 'mt warmup')
                require(all(int(r['ready']) == threads and float(r['start_spread_ns']) >= 0 and
                            int(r['minor_faults']) >= 0 for r in cell), 'gate/faults')
                for r in cell:
                    times = [float(v) for v in r['thread_ns'].split(',')]
                    require(len(times) == threads and all(t > 0 for t in times), 'thread times')
                    require(near(r['mean_ns'], statistics.mean(times)), 'thread mean')
                    require(near(r['slowest_ns'], max(times)), 'slowest thread')
                measured = [r for r in cell if r['phase'] == 'measured']
                summary, = [r for r in summaries if r['size'] == str(size) and
                            r['allocator'] == allocator and r['threads'] == str(threads)]
                for field in ('mean', 'slowest'):
                    values = [float(r[field + '_ns']) for r in measured]
                    require(near(summary['median_' + field + '_ns'], statistics.median(values)), 'mt median')
                    require(near(summary['worst_' + field + '_ns'], max(values)), 'mt worst')


def codegen():
    lines = ['Optimized (-O2, no LTO) assembly call checks, isolated timed-loop functions:']
    for filename, function in (('slow_check', 'slow_pairs'), ('mt_check', 'mt_worker')):
        assembly = (ROOT / 'codegen' / (filename + '.s')).read_text()
        match = re.search(r'^_?' + function + r':.*?\.cfi_endproc', assembly, re.M | re.S)
        require(match is not None, filename + ': timed function not found')
        body = match.group()
        for symbol in ('malloc', 'free', 'pool_alloc', 'pool_free'):
            calls = re.findall(r'^.*\b(?:bl|blx|callq?|jal)\s+_?' + symbol + r'(?:@PLT)?\b.*$', body, re.M)
            require(calls, filename + ': missing call to ' + symbol)
            lines.extend(filename + '/' + function + ': ' + line.strip() for line in calls)
        undefined = subprocess.check_output(['nm', '-u', str(ROOT / (filename + '.o'))], text=True)
        for symbol in ('malloc', 'free'):
            matches = re.findall(r'^.*\b_?' + symbol + r'\s*$', undefined, re.M)
            require(matches, filename + ': optimized object missing ' + symbol)
            lines.extend(filename + '.o (nm -u): ' + line.strip() for line in matches)
        if platform.system() == 'Darwin':
            disassembly = subprocess.check_output(['otool', '-tvV', str(ROOT / filename)], text=True)
        else:
            disassembly = subprocess.check_output(['objdump', '-d', str(ROOT / filename)], text=True)
        for symbol in ('malloc', 'free'):
            calls = [line.strip() for line in disassembly.splitlines()
                     if re.search(r'\b(?:bl|blx|callq?|jal)\s', line) and
                     re.search(r'\b_?' + symbol + r'\b', line)]
            require(calls, filename + ': disassembly missing call to ' + symbol)
            lines.extend(filename + ' (disassembly): ' + line for line in calls)
    (ROOT / 'codegen/check-codegen.txt').write_text('\n'.join(lines) + '\nPASS: malloc/free calls survive optimization in both timed loops.\n')


def main():
    if sys.argv[1:] == ['--codegen']:
        codegen()
        print('CODEGEN: PASS (see codegen/check-codegen.txt)')
    elif sys.argv[1:] == ['--smoke']:
        outputs = []
        for name in ('slow', 'mt'):
            result = subprocess.run([str(ROOT / (name + '_check')), '--smoke'],
                                    check=True, capture_output=True, text=True)
            (ROOT / 'codegen' / (name + '-smoke.txt')).write_text(result.stdout)
            outputs.append(result.stdout)
        validate(*outputs, smoke=True)
        print('EXPERIMENT CHECKS: PASS (smoke only, not performance evidence)')
    else:
        raise ValueError('usage: check_experiments.py --codegen|--smoke')


if __name__ == '__main__':
    main()
