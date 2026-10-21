#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Pin the pre-W10c Cartesian corpus; optionally count a completed CTest log."""
import argparse
from collections import Counter
import copy
import json
from pathlib import Path
import re
import subprocess


# Independent of the runner's selection regex and CMake's partition loop.
VARIANTS = ('default', '0', '1', '2', '3', '4', 'poison', 'poison3', 'poison4')
GOLDENS = {'rpc-cpu-trace', 'rpc-cmos-trace', 'rpc-plan-devices'} | {
    f'rpc-{kind}-{model}' for kind in
    ('video-paths', 'video-trace', 'd2-guest', 'plan-matrix', 'plan-av')
    for model in range(4)
} | {'rpc-factory-capture-2', 'rpc-factory-capture-3'}
INTERVALS = {f'rpc-memory-timing-{model}' for model in range(4)}
CORPUS = GOLDENS | INTERVALS | {'rpc-observation-plan'}
# Accepted L1 merge evidence in docs/handoff.md, before W10c.
COMPARISONS = 2_166_327_630


def verify(tests):
    by_name = {test['name']: test for test in tests}
    missing = CORPUS - by_name.keys()
    if missing:
        raise RuntimeError(('missing original corpus tests', sorted(missing)))
    legs = {name for name in by_name if name.startswith('rpc-l1-corpus-')
            and name != 'rpc-l1-corpus-count'}
    expected = {f'rpc-l1-corpus-{variant}' for variant in VARIANTS}
    if legs != expected or 'rpc-l1-corpus' in by_name:
        raise RuntimeError(('corpus partition drift', sorted(legs)))
    for variant in VARIANTS:
        test = by_name[f'rpc-l1-corpus-{variant}']
        command = test['command']
        script = next(i for i, word in enumerate(command)
                      if Path(word).name == 'rpc_l1_corpus_test.py')
        if command[script + 2:] != ['--variants', variant]:
            raise RuntimeError(('changed corpus leg', command))
        properties = {prop['name']: prop['value'] for prop in test['properties']}
        if 'slow' not in properties.get('LABELS', []) or properties.get('RUN_SERIAL', False):
            raise RuntimeError(('corpus scheduling drift', test['name']))


def self_test(tests):
    for mutation in ('omit gate', 'repeat gate', 'goldens only', 'lose slow',
                     'serialize', 'omit calibration'):
        changed = copy.deepcopy(tests)
        leg = next(test for test in changed if test['name'] == 'rpc-l1-corpus-0')
        if mutation == 'omit gate':
            changed.remove(leg)
        elif mutation == 'repeat gate':
            leg['command'][-1] = 'default'
        elif mutation == 'goldens only':
            leg['command'].append('--goldens-only')
        elif mutation == 'lose slow':
            leg['properties'] = [p for p in leg['properties'] if p['name'] != 'LABELS']
        elif mutation == 'serialize':
            leg['properties'].append({'name': 'RUN_SERIAL', 'value': True})
        else:
            changed = [test for test in changed if test['name'] != 'rpc-memory-timing-2']
        try:
            verify(changed)
        except RuntimeError:
            continue
        raise RuntimeError(('surviving corpus count mutant', mutation))
    print('corpus count guard: six omission/duplication/selection/scheduling mutants rejected')


def results(log, baseline=None):
    rows = [json.loads(line.split('L1 corpus counts ', 1)[1])
            for line in log.splitlines() if line.startswith('L1 corpus counts ')]
    if Counter(row['variant'] for row in rows) != Counter(VARIANTS):
        raise RuntimeError('missing or duplicate completed gate results')
    for row in rows:
        if Counter(row['tests']) != Counter(CORPUS) or row['intervals'] != 4677:
            raise RuntimeError(('completed corpus differs', row))
        if row['crosscheck'] <= 0:
            raise RuntimeError('empty cross-check')
    counts = {row['crosscheck'] for row in rows}
    if len(counts) != 1:
        raise RuntimeError(('gates performed different comparison counts', sorted(counts)))
    if sum(row['crosscheck'] for row in rows) != COMPARISONS:
        raise RuntimeError('total differs from the accepted pre-W10c comparison count')
    if baseline is not None:
        reference = re.findall(r'^default: 25 immutable goldens, 4677 intrusive/endpoint '
                               r'intervals, observation quota/mode matrix exact; '
                               r'crosscheck=(\d+) mismatches=0$', baseline, re.M)
        if len(reference) != 1 or counts != {int(reference[0])}:
            raise RuntimeError('comparison count differs from the pre-W10c default gate')
        print(f'pre-W10c default gate: {int(reference[0]):,} comparisons; '
              'every partition matches it exactly')
    print(f"completed count: 225 golden checks, {sum(row['intervals'] for row in rows):,} "
          f"intervals in both modes, 9 observation matrices; "
          f"{sum(row['crosscheck'] for row in rows):,} retained-reference comparisons, zero mismatches")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--results', type=Path)
    parser.add_argument('--baseline', type=Path, help='pre-W10c default-gate output')
    args = parser.parse_args()
    tests = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', str(args.build), '--show-only=json-v1'], text=True))['tests']
    verify(tests)
    self_test(tests)
    print('corpus count: 9 gates x (25 frozen goldens + 4 calibration tests + '
          '1 unchanged observation matrix) = 270 invocations; '
          '225 golden checks, 42,093 intervals in both modes, 9 observation matrices')
    if args.results:
        results(args.results.read_text(), args.baseline.read_text() if args.baseline else None)
    elif args.baseline:
        parser.error('--baseline requires --results')


if __name__ == '__main__':
    main()
