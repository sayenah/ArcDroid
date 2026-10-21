#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Verify exact-once PR partitions, tier coverage and required result checks."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess


PREFIX = 'rpc-host-pr-'
COUNT = 4
SUITE = 'host'
VARIANTS = ('default', '0', '1', '2', '3', '4', 'poison', 'poison3', 'poison4')
MATRIX = "shard: ${{ fromJSON(github.event_name == 'pull_request' && '[0,1,2,3]' || '[0]') }}"
SELECTION = "${{ github.event_name == 'pull_request' && format('-L ^rpc-host-pr-{0}$', matrix.shard) || '' }}"
EXTRA = "github.event_name != 'pull_request' || matrix.shard == 0"
ROUTE_SELECTION = "ctest --test-dir build/ordinary-open --output-on-failure -R '^rpc-(route-|shipping-route|artifact-classifier|lock-object)'"
SANITIZER_REGEX = '^(rpc-.*|left-stick-unit|left-stick-.*-harness|left-stick-input-trace|left-stick-equivalence-process|disc-request-unit|disc-resolver-unit|disc-request-.*-rom|disc-request-.*-process|disc-request-idle-trace|disc-request-commit-checks|disc-request-preflight-real-.*|error-box-unit|error-box-rom|error-box-x-rom|error-box-arbitration-.*|launch-outcome|launch-outcome-rom|hd-product-process|hd-started-process|hd-chooser-process|adf-refusal-rom|hard-disc-scope|hard-disc-program|power-on-chooser-harness|power-on-cmos|power-on-cmos-guest|power-on-shift-harness|power-on-recovery-harness|power-on-input-cancel-harness|boot-watch|boot-watch-.*-harness|boot-observation|boot-observation-harness|boot-observation-hd|boot-observation-.*-harness|cmos-device-bus|cmos-session-calendar|cmos-calendar-launch|cmos-guest-exerciser|hard-disc-formatter|install-ide-registers|native-hard-disc-store|native-hd-guest-process|native-hd-stress-process|zip-content|core-services|launch-ramdisc-unit|launch-ramdisc-harness|short-tail-unit|short-raw-wd|short-raw-newio|short-tail-guest|short-tail-newio-guest|jfd-quarantine-zip-guest|jfd-quarantine-density|jfd-quarantine-adffs|short-tail-payload-guest|short-tail-discovery|adffs-package-media|adffs-shipping-profile|jfd-engine-reader|jfd-identification|jfd-file-data|jfd-file-data-store|jfd-file-data-newio|jfd-file-data-process|jfd-file-data-set-process|jfd-file-data-unavailable-process|jfd-file-data-reasons-process|jfd-file-data-recovery-process|jfd-file-data-gzip-process|jfd-file-data-zip-process|jfd-quarantine|jfd-quarantine-wd-guest|jfd-quarantine-newio-guest|jfd-tab-store|jfd-tab-(guest|routing|set|evidence)-process|jfd-store|jfd-guard-store|jfd-guard-wd|jfd-guard-newio|jfd-guard-process|native-writable-layout|jfd-controller-golden|newio-controller-golden|jfd-writes-libretro-harness|jfd-creation-process|jfd-merge-process|jfd-allocation-process|jfd-identification-process|jfd-gate-short|jfd-gate-irregular|jfd-gate-timing|helper-stress-harness|input-layout-harness|input-source-retirement|libretro-harness|newio-libretro-harness|mouse-script-harness)$'
SANITIZER_TIERS = "${{ (github.event_name == 'push' || github.event_name == 'pull_request') && '-LE slow' || '' }}"
ONCE = ('Lockstep emitter CI subset', 'Configure profiling test build',
        'Build profiling tests', 'Profiling counts, transparency and mutants',
        'Both gate states and authored ordinary route',
        'Closed baseline and allowed open Archimedes projections')


def shard_for(name):
    if SUITE == 'host' and name in ('rpc-orchestration-mutants', 'rpc-session-census-leak-mutants'):
        return 2
    variant = name.removeprefix('rpc-l1-corpus-')
    if name.startswith('rpc-l1-corpus-') and variant in VARIANTS:
        return VARIANTS.index(variant) % COUNT
    return int(hashlib.sha256(name.encode('utf-8')).hexdigest()[:7], 16) % COUNT


def verify(tests, workflow):
    errors = []
    counts = [0] * COUNT
    for test in tests:
        properties = {p['name']: p['value'] for p in test.get('properties', [])}
        labels = properties.get('LABELS', [])
        placed = [label for label in labels if label.startswith(PREFIX)]
        expected = [] if SUITE == 'host' and 'rpc-lockstep-emitter-ci' in labels else [PREFIX + str(shard_for(test['name']))]
        if placed != expected:
            errors.append('incorrect host partition: ' + test['name'])
        if expected:
            counts[shard_for(test['name'])] += 1
    if not all(counts):
        errors.append('empty host partition')
    job = re.search(r'^  ' + JOB + r':\n(.*?)(?=^  [\w-]+:|\Z)', workflow, re.M | re.S)
    if (job is None or MATRIX not in job[1] or SELECTION not in job[1]
            or DISPLAY not in job[1]):
        errors.append('changed host matrix or PR-only selection')
    fan_in = re.search(r'^  ' + JOB + r'-result:\n(.*?)(?=^  [\w-]+:|\Z)', workflow, re.M | re.S)
    if fan_in is None or any(line not in fan_in[1] for line in (
            f'    name: {JOB}\n', f'    needs: {JOB}\n', '    if: always()\n',
            '      - run: test "${{ needs.' + JOB + '.result }}" = success\n')):
        errors.append('missing stable host check or matrix failure propagation')
    if job:
        if job[1].count(ROUTE_SELECTION) != 1:
            errors.append('missing or repeated full route selection')
        if SUITE == 'host' and ("-LE '${{ github.event_name == 'push' && '^rpc-lockstep-emitter-ci$|slow' || '^rpc-lockstep-emitter-ci$' }}'" not in job[1]
                or "if: github.event_name != 'pull_request' || matrix.shard == 3\n        with:\n          persist-credentials: false\n          ref: 3eec7e68c43f57b89fe2560d4eb16b93d0dc67bf" not in job[1]):
            errors.append('changed host tier or baseline checkout placement')
        if SUITE == 'sanitizers' and (SANITIZER_REGEX not in job[1] or SANITIZER_TIERS not in job[1]):
            errors.append('changed sanitizer PR or push/nightly coverage')
        for name in (ONCE if SUITE == 'host' else ('Both gate states and authored ordinary route',)):
            step = re.search(r'      - name: ' + re.escape(name) + r'\n(.*?)(?=      - |\Z)', job[1], re.S)
            owner = (3 if name in ONCE[-2:] else 0) if SUITE == 'host' else 2
            condition = EXTRA.replace('== 0' if SUITE == 'host' else '== 2', f'== {owner}')
            if step is None or f'        if: {condition}\n' not in step[1]:
                errors.append('missing single-run host step: ' + name)
            if name == 'Both gate states and authored ordinary route' and (step is None or
                    sum(line.strip() == ROUTE_SELECTION for line in step[1].splitlines()) != 1):
                errors.append('changed full route comparison selection')
    if errors:
        raise RuntimeError('; '.join(errors))
    return counts


def self_test(tests, workflow):
    ordinary = next(test for test in tests if not any(
        p['name'] == 'LABELS' and 'rpc-lockstep-emitter-ci' in p['value']
        for p in test.get('properties', [])))
    for mutation in ('omitted label', 'repeated label', 'wrong shard', 'emitter leakage'):
        changed = copy.deepcopy(tests)
        test = next(t for t in changed if t['name'] == ordinary['name'])
        labels = next(p['value'] for p in test['properties'] if p['name'] == 'LABELS')
        correct = PREFIX + str(shard_for(test['name']))
        if mutation == 'omitted label':
            labels.remove(correct)
        elif mutation == 'repeated label':
            labels.append(correct)
        elif mutation == 'wrong shard':
            labels.remove(correct)
            labels.append(PREFIX + str((shard_for(test['name']) + 1) % COUNT))
        else:
            labels.append('rpc-lockstep-emitter-ci' if SUITE == 'host' else 'rpc-sanitizer-pr-99')
        try:
            verify(changed, workflow)
        except RuntimeError:
            continue
        raise RuntimeError('surviving host partition mutant: ' + mutation)
    for changed in (workflow.replace("'[0,1,2,3]'" if SUITE == 'host' else "'[0,1,2]'", "'[0,1]'"),
                    workflow.replace("|| '[0]')", "|| '[0,1]')"),
                    workflow.replace(SELECTION, ''),
                    workflow.replace(f'        if: {EXTRA}\n', ''),
                    workflow.replace(f'  {JOB}-result:\n', f'  missing-{JOB}-result:\n'),
                    workflow.replace('test "${{ needs.' + JOB + '.result }}" = success', 'true')):
        try:
            verify(tests, changed)
        except RuntimeError:
            continue
        raise RuntimeError('surviving host workflow mutant')
    print(f'{SUITE} PR shard guard: ten placement/workflow mutants rejected')
    for changed in (workflow.replace(ROUTE_SELECTION, ''),
                    workflow.replace(ROUTE_SELECTION, ROUTE_SELECTION + '\n          ' + ROUTE_SELECTION),
                    workflow.replace(ROUTE_SELECTION, ROUTE_SELECTION + ' -LE slow')):
        try:
            verify(tests, changed)
        except RuntimeError:
            continue
        raise RuntimeError('surviving route selection mutant')
    print(f'{SUITE} PR shard guard: route omission, duplication and slow exclusion mutants rejected')
    if SUITE == 'host':
        for changed in (workflow.replace('^rpc-lockstep-emitter-ci$|slow', '^rpc-lockstep-emitter-ci$'),
                        workflow.replace('matrix.shard == 3\n        with:', 'matrix.shard == 2\n        with:')):
            try:
                verify(tests, changed)
            except RuntimeError:
                continue
            raise RuntimeError('surviving host tier/baseline mutant')
        print('host PR shard guard: push slow-tier and baseline checkout mutants rejected')
    if SUITE == 'sanitizers':
        for changed in (workflow.replace(SANITIZER_REGEX, '.*'), workflow.replace(SANITIZER_TIERS, '')):
            try:
                verify(tests, changed)
            except RuntimeError:
                continue
            raise RuntimeError('surviving sanitizer tier mutant')
        print('sanitizers PR shard guard: PR regex and slow-tier mutants rejected')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=('host', 'sanitizers'), default='host')
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--workflow', type=Path, default=Path(__file__).resolve().parents[1] / '.github/workflows/ci.yml')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    global SUITE, PREFIX, COUNT, MATRIX, SELECTION, JOB, DISPLAY, EXTRA
    SUITE = args.suite
    JOB = 'host' if SUITE == 'host' else 'host-sanitizers'
    DISPLAY = '    name: ' + ('host tests' if SUITE == 'host' else 'host sanitizer tests') + ' (${{ matrix.shard }})\n'
    if SUITE == 'sanitizers':
        PREFIX = 'rpc-sanitizer-pr-'
        COUNT = 3
        EXTRA = EXTRA.replace('== 0', '== 2')
        MATRIX = MATRIX.replace("'[0,1,2,3]'", "'[0,1,2]'")
        SELECTION = SELECTION.replace('rpc-host-pr-', PREFIX)
    tests = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', str(args.build), '--show-only=json-v1'], text=True))['tests']
    workflow = args.workflow.read_text()
    counts = verify(tests, workflow)
    if args.self_test:
        self_test(tests, workflow)
    selected = [test for test in tests if (SUITE == 'host' and not any(
        p['name'] == 'LABELS' and 'rpc-lockstep-emitter-ci' in p['value'] for p in test.get('properties', [])))
        or (SUITE == 'sanitizers' and re.fullmatch(SANITIZER_REGEX, test['name']) and not any(
        p['name'] == 'LABELS' and 'slow' in p['value'] for p in test.get('properties', [])))]
    pr_counts = [sum(shard_for(test['name']) == shard for test in selected) for shard in range(COUNT)]
    print(f'PR selection: {sum(pr_counts)} tests exactly once; shards {pr_counts}')
    print(f'{SUITE} PR shard guard: {sum(counts)} registered tests placed exactly once; shards {counts}; '
          f'integration steps on PR shards {"0/3" if SUITE == "host" else "2"}; push/nightly remain unpartitioned')


if __name__ == '__main__':
    main()
