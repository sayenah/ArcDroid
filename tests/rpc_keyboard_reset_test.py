#!/usr/bin/env python3
"""A physical key held across firmware PS/2 reset/enable is rescanned. GPL-2.0-only."""
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
from rpc_d2_rom import Guest

runner, core = sys.argv[1:3]


def guest(enable, rate, blocked):
    g = Guest()
    g.init()
    # Delay 10 ms: the scripted physical switch is already held before reset.
    g.load(6, rate*5000)
    loop = len(g.words)
    g.emit(0xe2566001)
    g.branch(loop, 1)
    # Open the IOMD link while its pre-enable make is still unread. TX must
    # become ready independently of RX so firmware can issue PS/2 reset.
    g.write(9, 8, 8)
    ready = len(g.words)
    g.read(0, 9, 8)
    g.emit(0xe3100080)
    g.branch(ready, 0)
    g.write(9, 4, 0xff)
    if blocked:
        g.load(6, rate*10000)  # 20 ms, past the 4 ms reset ACK deadline
        loop = len(g.words)
        g.emit(0xe2566001)
        g.branch(loop, 1)
    # Every reply is consumed in wire order, with a bounded recording buffer.
    g.mov(7, 0)
    read = len(g.words)
    g.read(0, 9, 8)
    g.emit(0xe3100020)
    g.branch(read, 0)
    g.read(0, 9, 4)
    g.emit(0xe3570040, 0x37880107, 0x32877001)
    g.store(7, 8, 256)
    if enable:
        g.emit(0xe35000aa)
        skip = len(g.words)
        g.emit(0)
        g.write(9, 4, 0xf4)
        g.words[skip] = 0x1a000000 | ((len(g.words)-skip-2) & 0xffffff)
    g.branch(read)
    return struct.pack('<'+'I'*len(g.words), *g.words)


with tempfile.TemporaryDirectory(prefix='arc-held-reset-') as temp:
    d = Path(temp)
    (d/'seed').write_bytes(bytes(256))
    for model, rate in [('arm610', 20), ('arm710', 35), ('sa110', 100), ('arm7500', 35)]:
        for key, make in [(127, [0xe0, 0x71]), (304, [0x12])]:
            (d/'held.input').write_text(f'0 key {key} 1\n')
            for enable in [False, True]:
                rom = guest(enable, rate, key == 304)
                (d/'authored.rom').write_bytes(rom + bytes(2097152-len(rom)))
                (d/'held.arcplan').write_text(f'version=1\nmodel={model}\nrate_mips={rate}\nfirmware=authored.rom\ncmos=seed\nseed=seed\nrtc_epoch=0\ninput=held.input\n')
                r = subprocess.run([runner, '--core', core, '--plan', str(d/'held.arcplan'),
                    '--system', str(d), '--frames', '40', '--ram-offset', '131072',
                    '--ram-words', '65'], capture_output=True, text=True)
                assert r.returncode == 0, r.stderr
                words = [int(w, 16) for w in re.findall(r'^ram=(.*)$', r.stdout, re.M)[-1].split(',')]
                wire = words[:min(words[64], 64)]
                # Reset ACK, successful BAT, optional enable ACK, then held make.
                expected = [0xfa, 0xaa] + ([0xfa] if enable else []) + make
                start = wire.index(0xfa) if 0xfa in wire else len(wire)
                if key == 304:
                    assert wire[:start] == [0x12], 'single-byte make was not unread at reset'
                assert wire[start:start+len(expected)] == expected, (model, key, enable, wire)
                assert 0xf0 not in wire, 'reset fabricated a physical release'
print('held-from-power-on: all models reset ACK/BAT, default scanning and explicit enable rescan passed')
