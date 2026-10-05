#!/usr/bin/env python3
"""Generate phase labels from a hashed, pre-run declaration and exact input log."""
import argparse
import csv
import json
from pathlib import Path
from evidence import core_rows, completion, digest, EvidenceError


def label(csv_path, declaration_path, script_path, events_path, output):
    rows = core_rows(csv_path)
    declaration = json.loads(Path(declaration_path).read_text())
    completion(csv_path, rows, expected_ns=declaration['requested_ns'])
    if digest(script_path) != declaration['script_sha256']:
        raise EvidenceError('script differs from predeclared script', 'FAIL')
    expected = []
    for line in Path(script_path).read_text().splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        t, verb, a, b, *tag = line.split()
        expected.append((int(t)*1000, verb, int(a), int(b)))
    with Path(events_path).open(newline='') as f:
        events = list(csv.DictReader(f))
    actual = [(int(r['scheduled_ns']), r['event'], int(r['a']), int(r['b'])) for r in events]
    if actual != expected or any(int(r['ordinal']) != i+1 or int(r['delivered_ns']) != int(r['scheduled_ns']) for i, r in enumerate(events)):
        raise EvidenceError('missing/out-of-order/late normal-device events', 'FAIL')
    warm, duration = (0, 300) if declaration['kind'] == 'screening' else (600, 1200)
    start = declaration['measurement_origin_ns'] + warm*10**9
    end = start + duration*10**9
    labels = []
    for r in rows:
        t = r['emu_ns']
        if not start < t <= end or any(x['start_ns'] <= t < x['end_ns'] for x in declaration['predeclared_exclusions']):
            continue
        matches = [x['phase'] for x in declaration['phases'] if x['start_ns'] <= t < x['end_ns']]
        if len(matches) != 1 or not matches[0]:
            raise EvidenceError('missing/overlapping phase at frame '+str(r['frame']))
        labels.append((r['frame'], matches[0]))
    with Path(output).open('w', newline='') as f:
        w = csv.writer(f);w.writerow(('frame','phase'));w.writerows(labels)
    return len(labels)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('csv','declaration','script','events','output'):
        p.add_argument('--'+name, type=Path, required=True)
    a=p.parse_args()
    try:
        n=label(a.csv,a.declaration,a.script,a.events,a.output)
        print('PASS:',n,'labels; exact script delivery verified; independent active-gameplay inspection still required')
    except (EvidenceError,OSError,ValueError,KeyError,TypeError) as e:
        print(getattr(e,'status','MISSING'),str(e));return 1 if getattr(e,'status','MISSING')=='FAIL' else 2
    return 0

if __name__=='__main__':
    raise SystemExit(main())
