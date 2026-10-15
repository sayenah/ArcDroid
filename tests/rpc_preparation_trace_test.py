#!/usr/bin/env python3
"""Preparation owns complete trace drains before generation retirement/failure.
Authored CMOS writers only; no firmware inputs. GPL-2.0-only.
"""
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from rpc_cmos_factory_rom import generate, factory
runner, core = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='arc-preparation-trace-') as temporary:
    root = Path(temporary)
    for kind in ('full', 'badchecksum'):
        folder = root / kind
        roms = generate(folder / 'roms')
        firmware = roms / (kind + '.rom')
        image = bytearray(firmware.read_bytes())
        # Reset first emits a trace marker through the real SWI vector, then
        # runs the existing factory guest. Its status/CMOS semantics are intact.
        struct.pack_into('<I', image, 0, 0xea000000 | ((0x1f0000 - 8) // 4))
        struct.pack_into('<I', image, 8, 0xe1b0f00e)
        struct.pack_into('<II', image, 0x1f0000, 0xef000042,
                         0xea000000 | (((0x100 - 0x1f0004 - 8) // 4) & 0xffffff))
        # Spend the stability interval asleep on the SA-110 instead of
        # tracing hundreds of millions of irrelevant branch fetches. The full
        # 45-second factory schedule, keyboard Delete and IIC writes still run.
        terminal = factory(kind).words.index(0xeafffffe, 64) * 4
        struct.pack_into('<I', image, terminal,
                         0xea000000 | (((0x1f0100 - terminal - 8) // 4) & 0xffffff))
        struct.pack_into('<II', image, 0x1f0100, 0xee0f0f58, 0xeafffffd)
        firmware.write_bytes(image)
        plan = folder / 'trace.arcplan'
        plan.write_text(f'version=2\nmodel=sa110\nfirmware={firmware}\ncmos=persist\ncmos_layout=ro370\ncontent_id={"c" * 40}\nrtc_epoch=0\nhard_disc=off\nobservation_trace=preparation.jsonl\nobservation_swis=0x42\n')
        result = subprocess.run([runner, '--core', core, '--plan', str(plan),
                                 '--system', str(folder), '--save', str(folder),
                                 '--frames', '1'], capture_output=True, text=True, timeout=180)
        assert result.returncode == (0 if kind == 'full' else 1), result.stderr
        rows = [json.loads(line) for line in (folder / 'preparation.jsonl').read_text().splitlines()]
        assert len(rows) == (2 if kind == 'full' else 1), rows
        assert all(row['kind'] == 1 and row['swi'] == 0x42 and not row.get('incomplete') and not row.get('lost') for row in rows), rows
        assert len({row['epoch'] for row in rows}) == len(rows), rows
        assert 'incomplete' not in result.stderr, result.stderr
        if kind == 'badchecksum': assert 'generation failed' in result.stderr
        print(f'{kind}: preparation trace complete, {len(rows)} generation/session marker(s), exit={result.returncode}')
