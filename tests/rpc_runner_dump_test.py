#!/usr/bin/env python3
"""Exact RGB PPM captures through the real frontend, canonical trace unchanged. GPL-2.0-only."""
import csv
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from rpc_video_goldens import colour

runner, core, directory = sys.argv[1:4]
args = [runner, '--core', core, '--plan', str(Path(directory)/'video-2-palette.arcplan'),
        '--system', directory, '--frames', '2']
with tempfile.TemporaryDirectory(prefix='arc-rpc-dump-') as temp:
    d = Path(temp)
    plain = subprocess.run(args, capture_output=True, check=True)
    result = subprocess.run(args + ['--dump-frame', '0', str(d/'first.ppm'),
        '--dump-frame', '1', str(d/'second.ppm'), '--dump-frame', '0', str(d/'again.ppm'),
        '--timing', str(d/'timing.csv')], capture_output=True, check=True)
    assert result.stdout == plain.stdout, 'frame dumping changed canonical output'
    assert (d/'first.ppm').read_bytes() == (d/'again.ppm').read_bytes()
    headers = re.findall(rb'^video=(\d+)x(\d+) hash=([0-9a-f]+)', result.stdout, re.M)
    for i, name in enumerate(['first', 'second']):
        magic, dims, maximum, rgb = (d/(name+'.ppm')).read_bytes().split(b'\n', 3)
        width, height = map(int, dims.split())
        assert magic == b'P6' and maximum == b'255' and len(rgb) == width*height*3
        assert (width, height) == tuple(int(n) for n in headers[i][:2])
        checksum = 14695981039346656037
        for offset in range(0, len(rgb), 3):
            r, g, b = rgb[offset:offset+3]
            for byte in [b, g, r, 255]:
                checksum = ((checksum ^ byte)*1099511628211) & ((1<<64)-1)
        assert f'{checksum:016x}'.encode() == headers[i][2], 'PPM lost pixel/pitch/colour data'
        if i == 0:
            expected = b''.join(bytes([(colour('palette',3,x,y)>>16)&255,
                (colour('palette',3,x,y)>>8)&255, colour('palette',3,x,y)&255])
                for y in range(height) for x in range(width))
            assert rgb == expected, 'first raster differs from independent palette oracle'
    rows = list(csv.DictReader((d/'timing.csv').open()))
    assert len(rows) == 2 and all(int(row['core_ns']) > 0 for row in rows)
    for extra in [ ['--dump-frame', '3', str(d/'missing.ppm')],
                   ['--dump-frame', '0', str(d/'absent/failed.ppm')],
                   ['--dump-frame', 'invalid', str(d/'invalid.ppm')],
                   ['--dump-frame', '0'] ]:
        bad = subprocess.run(args + extra, capture_output=True)
        assert bad.returncode != 0, 'invalid/incomplete frame capture succeeded'
print('rpc-runner-dump: repeatable exact PPM RGB/pitch, independent palette oracle, canonical identity and failures passed')
