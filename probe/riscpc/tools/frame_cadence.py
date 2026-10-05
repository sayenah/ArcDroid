#!/usr/bin/env python3
"""Recompute per-frame game/HUD transition rates; never invent missing pixels.
GPL-2.0-only. --window uses absolute emulated seconds from the cold origin.
"""
import argparse
import csv
import json
import re
from pathlib import Path
from evidence import EvidenceError, completion, core_rows, digest


def analyse(directory, windows):
    directory = Path(directory)
    rows = core_rows(directory / 'frames.csv')
    footer = completion(directory / 'frames.csv', rows)
    with (directory / 'frame-hashes.csv').open(newline='') as f:
        hashes = list(csv.DictReader(f))
    if len(hashes) != len(rows):
        raise EvidenceError('per-frame hash coverage differs from core CSV', 'FAIL')
    required = {'frame', 'emu_ns', 'width', 'height', 'full_hash'}
    fields = set(hashes[0])
    if not required <= fields or not fields - required:
        raise EvidenceError('missing HUD region fields')
    hud = sorted(fields - required)
    for row, observed in zip(rows, hashes):
        if int(observed['frame']) != row['frame'] or int(observed['emu_ns']) != row['emu_ns']:
            raise EvidenceError('per-frame hashes have different guest timestamps', 'FAIL')
        if observed['full_hash'] != row['video_hash']:
            raise EvidenceError('independent full-frame hash differs', 'FAIL')
        if not (0 < int(observed['width']) <= 2048 and 0 < int(observed['height']) <= 2048):
            raise EvidenceError('invalid observed geometry', 'FAIL')
        for key in hud:
            if observed[key] != 'MISSING' and not re.fullmatch('[0-9a-f]{16}', observed[key]):
                raise EvidenceError('invalid HUD hash', 'FAIL')
    out = []
    for start, end in windows:
        start_ns, end_ns = round(start * 1e9), round(end * 1e9)
        if not start_ns < end_ns or rows[-1]['emu_ns'] < end_ns:
            raise EvidenceError('declared window not covered')
        # Include the preceding frame, so a transition at the first in-window
        # frame is observed. Half-open windows avoid double-counting boundaries.
        values = [(a, b) for a, b in zip(hashes, hashes[1:])
                  if start_ns <= int(b['emu_ns']) < end_ns]
        if not values or int(values[0][0]['emu_ns']) > start_ns:
            raise EvidenceError('no frame preceding declared window')
        observations = {}
        for key in ['full_hash'] + hud:
            if any(a[key] == 'MISSING' or b[key] == 'MISSING' for a, b in values):
                observations[key] = {'status': 'MISSING', 'reason': 'region outside captured geometry'}
                continue
            transitions = [{'ns': int(b['emu_ns']), 'previous_ns': int(a['emu_ns']),
                            'before': a[key], 'after': b[key]}
                           for a, b in values if a[key] != b[key]]
            observations[key] = {'status': 'PASS', 'changes': len(transitions),
                                 'changes_per_second': len(transitions) / (end-start),
                                 'transitions': transitions}
        out.append({'start': start, 'end': end, 'frames': len(values),
                    'largest_frame_ns': max(int(b['emu_ns'])-int(a['emu_ns']) for a, b in values),
                    'observations': observations})
    return {'status': 'PASS', 'footer': footer, 'regions': hud, 'windows': out,
            'hashes_sha256': digest(directory/'frame-hashes.csv'),
            'frames_sha256': digest(directory/'frames.csv')}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--window', nargs=2, type=float, action='append', required=True)
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    try:
        result = analyse(a.directory, a.window)
        if a.output:
            a.output.write_text(json.dumps(result, indent=2)+'\n')
        compact = [{**{k:v for k,v in w.items() if k!='observations'},
                    'observations': {k:{j:v for j,v in o.items() if j!='transitions'}
                                     for k,o in w['observations'].items()}}
                   for w in result['windows']]
        print(json.dumps({'status':'PASS', 'windows':compact}))
    except (EvidenceError, OSError, ValueError, KeyError, TypeError) as e:
        print(getattr(e, 'status', 'MISSING'), str(e))
        return 1 if getattr(e, 'status', 'MISSING') == 'FAIL' else 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
