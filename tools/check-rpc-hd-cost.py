#!/usr/bin/env python3
"""Check runner HD work against D-160's namespace-aware HD work bounds. GPL-2.0-only."""
import csv
import argparse
import sys
from pathlib import Path


def check(path, frames=None):
    rows = list(csv.DictReader(Path(path).open(newline='')))
    assert rows, 'empty HD report'
    assert frames is None or len(rows) == frames, 'incomplete HD report'
    steps = {
        'IDLE': {'admit'}, 'ROTATE': {'prepare', 'switch'},
        'MATERIALIZE': {'reconstruct', 'write', 'verify', 'destination', 'publish'},
        'MANIFEST': {'write', 'verify'}, 'PUBLISH': {'rename', 'verify'},
        'RETIRE': {'manifest', 'header', 'length-binding', 'remove-manifest', 'remove-journal'},
        'GC': {'inventory'}, 'READ_ONLY': {'stopped'}, 'DAMAGED': {'stopped'},
    }
    namespace_steps = {('ROTATE', 'prepare'), ('MATERIALIZE', 'write'),
                       ('MATERIALIZE', 'destination'), ('MATERIALIZE', 'publish'),
                       ('MANIFEST', 'write'), ('PUBLISH', 'rename'),
                       ('RETIRE', 'remove-manifest'), ('RETIRE', 'remove-journal'), ('GC', 'inventory')}
    maxima = {}
    over = []
    for frame, row in enumerate(rows):
        assert int(row['frame']) == frame, 'missing or reordered frame'
        values = [int(row[k]) for k in
                  ('T_ns', 'read_ns', 'acknowledgement_ns', 'maintenance_ns', 'open_ns', 'total_ns')]
        period, read, ack, maintenance, opened, total = values
        assert period > 0 and min(values) >= 0 and total == read + ack + maintenance + opened
        assert row['attached'] in ('0', '1')
        phase, substep = row['phase'], row['substep']
        assert phase in steps and substep in steps[phase], 'unknown maintenance substep'
        namespace = int(row['namespace_operations'])
        assert namespace in (0, 1), 'namespace budget exceeded'
        assert not namespace or (phase, substep) in namespace_steps, 'namespace call in read-only substep'
        if row['attached'] == '0':
            # A failed open can consume time without attaching a backend.
            assert read == ack == maintenance == namespace == 0
            assert int(row['write_commands']) == int(row['read_commands']) == 0
        label = f'{phase}/{substep}'
        maxima[label] = max(maxima.get(label, 0), total)
        budget = period * (2 if namespace else 1) // 5
        if total * 5 > period * (2 if namespace else 1):
            over.append((frame, label, total, budget))
        if ack * 5 > period:
            over.append((frame, 'acknowledgement', ack, period // 5))
    for phase, maximum in sorted(maxima.items()):
        print(f'{phase}: max HD ns={maximum}')
    print(f'frames={len(rows)} acknowledgement max ns={max(int(r["acknowledgement_ns"]) for r in rows)} '
          f'over_D160={len({entry[0] for entry in over})}')
    for frame, phase, total, budget in over:
        print(f'FAIL frame={frame} phase={phase} HD_ns={total} budget_ns={budget}')
    return bool(over)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv')
    parser.add_argument('--frames', type=int, help='require the exact requested frame count')
    args = parser.parse_args()
    try:
        sys.exit(check(args.csv, args.frames))
    except (AssertionError, KeyError, ValueError, OSError, IndexError) as error:
        print(f'Invalid HD report: {error}', file=sys.stderr)
        sys.exit(2)
