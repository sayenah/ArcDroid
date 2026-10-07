#!/usr/bin/env python3
"""Authored RiscPC board reset-gate behavior; no firmware. GPL-2.0-only."""
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
from rpc_d2_rom import Guest

runner, core = sys.argv[1:3]
g = Guest()
g.init()
g.load(1, 0x03310000)
g.read(0, 1)
g.store(0, 8)
for i, (value, byte) in enumerate([(0x80, False), (0, False),
                                  (0xff, True), (0x7f, False), (0x80, True)], 1):
    g.mov(0, value)
    g.emit(0xe5c10000 if byte else 0xe5810000)
    g.read(0, 1)
    g.store(0, 8, i*4)
g.emit(0xeafffffe)
rom = struct.pack('<'+'I'*len(g.words), *g.words)
with tempfile.TemporaryDirectory(prefix='arc-cmos-gate-') as temp:
    d = Path(temp)
    (d/'authored.rom').write_bytes(rom + bytes(2097152-len(rom)))
    (d/'seed').write_bytes(bytes(256))
    for model in ['arm610', 'arm710', 'sa110']:
        (d/'gate.arcplan').write_text(f'version=1\nmodel={model}\nrate_mips=20\nfirmware=authored.rom\ncmos=seed\nseed=seed\nrtc_epoch=0\n')
        cmd = [runner, '--core', core, '--plan', str(d/'gate.arcplan'),
               '--system', str(d), '--frames', '2', '--reset-at', '1',
               '--ram-offset', '131072', '--ram-words', '6']
        for _ in range(2):  # fresh loads as well as frontend reset
            r = subprocess.run(cmd, capture_output=True, text=True)
            assert r.returncode == 0, r.stderr
            observations = [[int(w, 16) for w in line.split(',')]
                            for line in re.findall(r'^ram=(.*)$', r.stdout, re.M)]
            expected = [0x70, 0xf0, 0x70, 0xf0, 0x70, 0xf0]
            assert observations == [expected]*2, (model, observations)
print('CMOS gate: byte/word bit-7 echo, read-only inputs, all RiscPC CPUs, reset/fresh load passed')
