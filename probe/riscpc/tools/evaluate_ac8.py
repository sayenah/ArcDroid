#!/usr/bin/env python3
"""Recompute AC-8 from complete raw data. Missing evidence stays MISSING.

A manifest references an immutable pre-run declaration, completion footer,
raw phase labels and independent telemetry. See RUNBOOK.md. GPL-2.0-only.
"""
import argparse
import csv
import json
from pathlib import Path
import statistics

from evidence import core_rows, completion, digest, EvidenceError, quantile, include_csv_cost


def evaluate(path, manifest_path=None, screening=False):
    path = Path(path)
    base = Path(manifest_path).parent if manifest_path else path.parent
    manifest = json.loads(Path(manifest_path).read_text()) if manifest_path else {}
    criteria, detail = {}, {}

    def record(name, status, message):
        criteria[name], detail[name] = status, message

    def raw(section):
        spec = manifest.get(section)
        if not isinstance(spec, dict):
            raise EvidenceError('absent ' + section)
        p = base / spec['raw_file']
        if digest(p) != spec['sha256']:
            raise EvidenceError('raw hash mismatch: ' + section)
        return p

    declaration = {}
    try:
        declaration = json.loads(raw('declaration').read_text())
        origin = declaration['measurement_origin_ns']
        requested = declaration['requested_ns']
        if type(origin) is not int or origin < 0 or type(requested) is not int:
            raise EvidenceError('invalid declared origin/duration')
        if declaration.get('kind') != ('screening' if screening else 'sustained'):
            raise EvidenceError('wrong measurement kind')
        record('predeclared_window', 'PASS', 'immutable declared origin and duration')
    except (EvidenceError, OSError, ValueError, KeyError, TypeError) as e:
        record('predeclared_window', 'MISSING', str(e))
        origin, requested = 0, None
    warm, duration = (0, 300) if screening else (600, 1200)
    start, end = origin + warm * 10**9, origin + (warm + duration) * 10**9
    rows = []
    try:
        rows = core_rows(path)
        cost_path = raw('csv_cost') if manifest.get('csv_cost') else path.parent / 'csv-cost.csv'
        rows = include_csv_cost(cost_path, rows)
        footer_path = raw('footer') if manifest.get('footer') else None
        footer = completion(path, rows, footer=footer_path, expected_ns=requested)
        if footer.get('measurement') == 0:
            raise EvidenceError('instrumentation disabled')
        if footer.get('cost_accounting') != 'deferred-csv-tail-v1':
            raise EvidenceError('missing/current execution-cost accounting version')
        if footer.get('video_renderer') != 'line-v4':
            raise EvidenceError('final line-v4 renderer footer required; old renderer is not final evidence')
        record('run_integrity', 'PASS', f'{len(rows)} rows; final line-v4 footer verified')
    except (EvidenceError, OSError, ValueError, KeyError) as e:
        record('run_integrity', getattr(e, 'status', 'MISSING'), str(e))
        rows = []

    excluded = declaration.get('predeclared_exclusions')
    try:
        if not isinstance(excluded, list):
            raise EvidenceError('must declare exclusions, including []')
        previous = -1
        for x in excluded:
            if type(x['start_ns']) is not int or type(x['end_ns']) is not int or not x['reason'] or x['start_ns'] < previous or not origin <= x['start_ns'] < x['end_ns'] <= end:
                raise EvidenceError('invalid/overlapping exclusions')
            previous = x['end_ns']
        record('predeclared_exclusions', 'PASS', f'{len(excluded)} exclusions')
    except (EvidenceError, KeyError, TypeError) as e:
        record('predeclared_exclusions', 'MISSING', str(e))
        excluded = []
    steady = [r for r in rows if start < r['emu_ns'] <= end and not any(x['start_ns'] <= r['emu_ns'] < x['end_ns'] for x in excluded)]
    duration_pass = bool(rows and rows[-1]['emu_ns'] >= end and requested is not None and requested >= end)
    record('duration', 'PASS' if duration_pass else 'MISSING', f'requires origin={origin} + {warm}s warm-up + {duration}s measured')
    for name, q, bound in [('p95', .95, .80), ('p99', .99, .90), ('p99_9', .999, 1.)]:
        if steady:
            value = quantile([r['exec_ns'] / r['period_ns'] for r in steady], q)
            record(name, 'PASS' if value <= bound else 'FAIL', f'{value:.8f} T <= {bound} T')
        else:
            record(name, 'MISSING', 'no valid measured frames')
    if steady:
        streak = maximum = misses = 0
        previous = None
        for r in steady:
            if previous is not None and r['frame'] != previous + 1:
                streak = 0
            missed = r['exec_ns'] > r['period_ns']
            misses += missed
            streak = streak + 1 if missed else 0
            maximum = max(maximum, streak)
            previous = r['frame']
        record('consecutive_deadline_misses', 'PASS' if maximum < 3 else 'FAIL', f'{misses} misses; longest streak={maximum}')
        record('core_audio', 'PASS' if all(r['underruns'] == r['overruns'] == 0 for r in steady) else 'FAIL', 'zero cumulative counters in measured gameplay required')
        # Exclusions cannot hide wall/guest drift: use the entire declared span.
        span = [r for r in rows if start < r['emu_ns'] <= end]
        guest = span[-1]['emu_ns'] - span[0]['emu_ns']
        wall = span[-1]['wall_ns'] - span[0]['wall_ns']
        drift = abs(guest - wall) / guest if guest > 0 and wall > 0 else None
        record('guest_wall_drift', 'MISSING' if drift is None else 'PASS' if drift <= .001 else 'FAIL', 'missing span' if drift is None else f'{drift:.8%} <= 0.1%')
    else:
        for name in ('consecutive_deadline_misses', 'core_audio', 'guest_wall_drift'):
            record(name, 'MISSING', 'no valid measured frames')

    # Derive expected phase labels from the immutable declaration, then compare
    # every provided label. Unlabelled/overlapping windows never default to phase0.
    try:
        phases = declaration['phases']
        labels_path = raw('phases')
        with labels_path.open(newline='') as f:
            labels = list(csv.DictReader(f))
        labels_map = {int(x['frame']): x['phase'] for x in labels}
        if len(labels_map) != len(labels) or len(labels) != len(steady):
            raise EvidenceError('duplicate/missing/extra phase rows')
        for r in steady:
            matches = [x['phase'] for x in phases if x['start_ns'] <= r['emu_ns'] < x['end_ns']]
            if len(matches) != 1 or not matches[0] or labels_map.get(r['frame']) != matches[0]:
                raise EvidenceError('phase labels do not match declaration')
        if screening:
            raise EvidenceError('sustained phase criterion cannot be decided by screening')
        first = [r for r in steady if r['emu_ns'] <= start + 300 * 10**9]
        last = [r for r in steady if r['emu_ns'] > end - 300 * 10**9]
        names = {labels_map[r['frame']] for r in first}
        if not names or names != {labels_map[r['frame']] for r in last}:
            raise EvidenceError('first/last five minutes contain different phases')
        ratios = {}
        for phase in sorted(names):
            x = [r['exec_ns'] / r['period_ns'] for r in first if labels_map[r['frame']] == phase]
            y = [r['exec_ns'] / r['period_ns'] for r in last if labels_map[r['frame']] == phase]
            if not x or not y or statistics.mean(x) <= 0:
                raise EvidenceError('empty/zero phase cost')
            ratios[phase] = statistics.mean(y) / statistics.mean(x)
        record('matched_phase_last_first', 'PASS' if all(abs(v - 1) <= .10 for v in ratios.values()) else 'FAIL', f'phase normalized-cost ratios {ratios}; within 10%')
    except (EvidenceError, OSError, KeyError, ValueError, TypeError) as e:
        record('matched_phase_last_first', 'MISSING', str(e))

    # These JSON files are the RAW exports/observations, not copied manifest
    # counters. Their source collection method and full interval must be stated.
    def telemetry(section):
        t = json.loads(raw(section).read_text())
        if t['start_ns'] != start or t['end_ns'] != end or not t['method']:
            raise EvidenceError('telemetry window/method mismatch')
        return t

    try:
        t = telemetry('frontend')
        if t.get('source') != 'RetroArch statistics export':
            raise EvidenceError('actual RetroArch statistics export required; ordinary logs may lack counters')
        values = [t[k] for k in ('expected_presentations', 'missed_presentations', 'legitimate_repeated_frames')]
        if any(type(v) is not int or v < 0 for v in values) or values[0] <= 0 or max(values[1:]) > values[0]:
            raise EvidenceError('invalid frontend counts')
        # Preserve the raw RetroArch log/stat capture from which the export was
        # obtained. A method string and self-authored zero counts are insufficient.
        source = base / t['capture_file']
        if digest(source) != t['capture_sha256'] or not t['repeat_classification_method']:
            raise EvidenceError('missing raw frontend capture/repeat classification')
        value = values[1] / values[0]
        record('frontend_missed_presentations', 'PASS' if value < .001 else 'FAIL', f'{value:.8%} <0.1%; legitimate repeats={values[2]}')
    except (EvidenceError, OSError, KeyError, ValueError, TypeError) as e:
        record('frontend_missed_presentations', 'MISSING', str(e))
    try:
        t = telemetry('independent_audio')
        counts = [t[k] for k in ('core_caused_underruns', 'core_caused_overruns', 'audible_discontinuities')]
        if any(type(v) is not int or v < 0 for v in counts) or not t['observer'] or not t['output_device']:
            raise EvidenceError('independent output observer/device/counts required')
        record('independent_audio_output', 'PASS' if counts == [0, 0, 0] else 'FAIL', f'independent output counts {counts}')
    except (EvidenceError, OSError, KeyError, ValueError, TypeError) as e:
        record('independent_audio_output', 'MISSING', str(e))
    try:
        t = telemetry('platform')
        if not t['device'] or not t['performance_settings'] or not t['temperatures'] or not t['frequencies']:
            raise EvidenceError('hardware/settings/temperature/frequency samples required')
        record('platform_conditions', 'PASS', 'recorded platform conditions')
    except (EvidenceError, OSError, KeyError, ValueError, TypeError) as e:
        record('platform_conditions', 'MISSING', str(e))
    try:
        t = telemetry('gameplay')
        if not t['observer'] or not t['checkpoints']:
            raise EvidenceError('independent inspection/checkpoints required')
        record('active_gameplay', 'PASS' if t['substantial_active_gameplay'] is True else 'FAIL', 'independently inspected declared active scene')
    except (EvidenceError, OSError, KeyError, ValueError, TypeError) as e:
        record('active_gameplay', 'MISSING', str(e))
    try:
        from measure_overhead import recompute
        overhead = raw('overhead')
        result = recompute(overhead.parent)
        record('instrumentation_overhead', result['status'], f"diagnostic full overhead interval {result['mean_95_interval_T']}; workload/platform transfer requires custody")
        binding = json.loads(raw('custody').read_text())
        paired = json.loads(overhead.read_text())
        platform_raw = telemetry('platform')
        options = dict(x.split('=', 1) for x in paired['options'])
        if binding['core_sha256'] != paired['core_sha256'] or paired['host']['system'] != platform_raw['host_system'] or paired['host']['machine'] != platform_raw['host_machine']:
            raise EvidenceError('overhead binary/platform custody mismatch')
        for key, value in binding['core_options'].items():
            if key in ('rpcprobe_model', 'rpcprobe_rate', 'rpcprobe_script', 'rpcprobe_ide_baseline', 'rpcprobe_cmos_baseline') and options.get(key) != value:
                raise EvidenceError('overhead workload/initial-state option mismatch')
        if not binding['input_sha256'] or any(value not in paired['immutable_sha256'].values() for value in binding['input_sha256'].values()):
            raise EvidenceError('cold input custody absent/differs from overhead pairs')
        if not all(k in binding['core_options'] for k in ('rpcprobe_model', 'rpcprobe_rate')):
            raise EvidenceError('model/rate custody absent')

    except (EvidenceError, OSError, KeyError, ValueError, TypeError) as e:
        record('instrumentation_overhead', getattr(e, 'status', 'MISSING'), str(e))
    return dict(criteria=criteria, detail=detail,
                p50_T=quantile([r['exec_ns'] / r['period_ns'] for r in steady], .5) if steady else None,
                coverage='Use evaluate_suite.py; one passing run is never suite approval')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv', type=Path)
    p.add_argument('--manifest', type=Path)
    p.add_argument('--screening', action='store_true')
    p.add_argument('--json', type=Path)
    a = p.parse_args()
    try:
        out = evaluate(a.csv, a.manifest, a.screening)
    except (OSError, ValueError, KeyError, TypeError) as e:
        print('MISSING raw evidence:', e)
        return 2
    for name, status in out['criteria'].items():
        print(f'{status:7} {name}: {out["detail"][name]}')
    print('p50 execution/T:', out['p50_T'] if out['p50_T'] is not None else 'MISSING')
    if a.json:
        a.json.write_text(json.dumps(out, indent=2) + '\n')
    values = out['criteria'].values()
    return 1 if 'FAIL' in values else 2 if 'MISSING' in values else 0


if __name__ == '__main__':
    raise SystemExit(main())
