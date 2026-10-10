#!/usr/bin/env python3
"""Check runner HD work against D-152's 0.20 T bound. GPL-2.0-only."""
import csv
import argparse
import sys
from pathlib import Path


def check(path, frames=None):
    rows = list(csv.DictReader(Path(path).open(newline='')))
    assert rows, 'empty HD report'
    assert frames is None or len(rows) == frames, 'incomplete HD report'
    maxima = {}
    over = []
    for frame, row in enumerate(rows):
        assert int(row['frame']) == frame, 'missing or reordered frame'
        values = [int(row[k]) for k in
                  ('T_ns', 'read_ns', 'acknowledgement_ns', 'maintenance_ns', 'total_ns')]
        period, read, ack, maintenance, total = values
        assert period > 0 and min(values) >= 0 and total == read + ack + maintenance
        assert row['attached'] in ('0', '1')
        if row['attached'] == '0':
            assert total == 0 and int(row['write_commands']) == int(row['read_commands']) == 0
        label = row['phase']
        maxima[label] = max(maxima.get(label, 0), total)
        if total * 5 > period:
            over.append((frame, label, total, period // 5))
    for phase, maximum in sorted(maxima.items()):
        print(f'{phase}: max HD ns={maximum}')
    print(f'frames={len(rows)} acknowledgement max ns={max(int(r["acknowledgement_ns"]) for r in rows)} '
          f'over_0.20T={len(over)}')
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
