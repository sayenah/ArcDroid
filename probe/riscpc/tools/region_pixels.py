#!/usr/bin/env python3
"""Validate bounded changed-only HUD tiles against every-frame hashes.
GPL-2.0-only. No OCR or inferred game counter values are supplied.
"""
import argparse
import csv
import json
import re
import struct
from pathlib import Path
from evidence import EvidenceError
from frame_cadence import analyse


def decode(row):
    width, height = int(row['width']), int(row['height'])
    if not 0 < width <= 2048 or not 0 < height <= 2048 or width*height > 4096:
        raise EvidenceError('HUD tile exceeds bounds', 'FAIL')
    if row['hash'] == 'MISSING':
        if row['rle'] != 'MISSING':
            raise EvidenceError('missing region has pixel data', 'FAIL')
        return None
    pixels = []
    parts = row['rle'].split(';')
    if parts.pop() != '':
        raise EvidenceError('unterminated RLE', 'FAIL')
    for part in parts:
        if not re.fullmatch(r'[1-9][0-9]*:[0-9a-f]{8}', part):
            raise EvidenceError('invalid RLE token', 'FAIL')
        count, value = part.split(':')
        count = int(count)
        if count > width*height-len(pixels):
            raise EvidenceError('RLE exceeds tile', 'FAIL')
        pixels.extend([int(value, 16)]*count)
    if len(pixels) != width*height:
        raise EvidenceError('short RLE tile', 'FAIL')
    hash_value = 1469598103934665603
    for value in pixels:
        for byte in struct.pack('<I', value):
            hash_value = ((hash_value ^ byte)*1099511628211) & ((1 << 64)-1)
    if f'{hash_value:016x}' != row['hash']:
        raise EvidenceError('tile hash mismatch', 'FAIL')
    return pixels


def validate(directory, names):
    directory = Path(directory)
    # Validate exact coverage and the core completion footer first.
    with (directory/'frame-hashes.csv').open() as f:
        hashes = list(csv.DictReader(f))
    if len(hashes) < 2:
        raise EvidenceError('insufficient per-frame observations')
    analyse(directory, [(int(hashes[0]['emu_ns'])/1e9, int(hashes[-1]['emu_ns'])/1e9)])
    with (directory/'region-pixels.csv').open() as f:
        records = list(csv.DictReader(f))
    if not names or len(set(names)) != len(names) or any(n not in hashes[0] for n in names):
        raise EvidenceError('invalid selected region declaration', 'FAIL')
    indexed = {}
    previous = -1
    for row in records:
        frame = int(row['frame']); key = (frame, row['region'])
        if frame < previous or key in indexed or row['region'] not in names or not 0 <= frame < len(hashes):
            raise EvidenceError('invalid tile ordering/selection', 'FAIL')
        previous = frame
        if row['emu_ns'] != hashes[frame]['emu_ns'] or row['hash'] != hashes[frame][row['region']]:
            raise EvidenceError('tile does not match per-frame observation', 'FAIL')
        decode(row); indexed[key] = row
    state = {}
    for frame, observed in enumerate(hashes):
        for name in names:
            key = (frame, name)
            if key in indexed:
                if name in state and indexed[key]['hash'] == state[name]['hash']:
                    raise EvidenceError('redundant unchanged tile', 'FAIL')
                state[name] = indexed[key]
            if name not in state or state[name]['hash'] != observed[name]:
                raise EvidenceError('missing tile transition', 'FAIL')
    return {'status': 'PASS', 'frames': len(hashes), 'tiles': len(records), 'regions': names}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--names', required=True)
    a = p.parse_args()
    try:
        print(json.dumps(validate(a.directory, a.names.split(','))))
        return 0
    except (EvidenceError, OSError, ValueError, KeyError, TypeError) as e:
        print(getattr(e, 'status', 'MISSING'), str(e))
        return 1 if getattr(e, 'status', 'MISSING') == 'FAIL' else 2


if __name__ == '__main__':
    raise SystemExit(main())
