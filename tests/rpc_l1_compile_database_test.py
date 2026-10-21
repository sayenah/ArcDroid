#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise L1's measurement lookup on a real host or cross compile database."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from rpc_ninja_commands import compile_entry, split_command

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('build', type=Path)
a = p.parse_args()
a.build = a.build.resolve()
rows = json.loads((a.build / 'compile_commands.json').read_text())
pairs = (('arcdroid_rpcmeasure_libretro_rpc', 'rpc_mem_timing.c'),
         ('arcdroid_libretro_rpc', 'rpc_mem_timing.c'),
         ('arcdroid_rpcmeasure_libretro', 'arcdroid_engine.c'),
         ('arcdroid_libretro_rpc', 'arm.c'),
         ('arcdroid_rpcmeasure_libretro_rpc', 'arm.c'),
         ('arcdroid_libretro_test_rpc', 'arm.c'))
# Re-spell the actual generated database, including all competing targets,
# rather than a one-row fixture that could conceal an ambiguous selection.
for target, source in pairs:
    entry = compile_entry(rows, target, source)
    for command_backslash, file_backslash in ((True, True), (True, False), (False, True)):
        changed = [dict(row, command=row['command'].replace('/', '\\') if command_backslash else row['command'],
                        file=row['file'].replace('/', '\\') if file_backslash else row['file']) for row in rows]
        selected = compile_entry(changed, target, source)
        if selected['command'].replace('\\', '/') != entry['command'].replace('\\', '/'):
            raise RuntimeError('Windows spelling selected a different compile row')
        if any(f'{target}.dir/' in row['command'] and row['file'].endswith('/' + source) for row in changed):
            raise RuntimeError('former slash-only lookup unexpectedly survived the Windows control')
target, source = pairs[0]
entry = compile_entry(rows, target, source)
for changed in ([row for row in rows if row is not entry], rows + [entry]):
    try:
        compile_entry(changed, target, source)
    except RuntimeError:
        pass
    else:
        raise RuntimeError('missing/ambiguous compile row survived')
# Use the real compiler and flags, including the cross compiler, to exercise
# the preprocessing operation that failed before the nightly mutants ran.
with tempfile.TemporaryDirectory(prefix='arcdroid-l1-compdb-', dir=a.build) as tmp:
    for index, (target, source) in enumerate(pairs):
        entry = compile_entry(rows, target, source)
        out = Path(tmp) / f'source-{index}.i'
        cmd = split_command(entry['command'], os.name == 'nt')
        cmd[cmd.index('-c')] = '-E'
        cmd[cmd.index('-o') + 1] = str(out)
        for flag, value in (('-MF', out.with_suffix('.d')), ('-MT', out)):
            if flag in cmd:
                cmd[cmd.index(flag) + 1] = str(value)
        subprocess.run(cmd + ['-P'], cwd=a.build, check=True, capture_output=True, text=True)
        body = out.read_text()
        if not body.strip():
            raise RuntimeError('empty preprocessing output')
        if index == 0:
            body = body.split('void arc_rpc_mem_bind(', 1)[-1]
            body = body[body.index('{'):body.index('if (!clock) return;')]
            if 'lookup_reset();' not in body:
                raise RuntimeError('measurement reset absent')
print(f'L1 compile database: {len(rows)} real rows; native, backslash and both mixed spellings; '
      'six L1/S1 lookups and real preprocessing passed; slash-only/missing/duplicate controls rejected')
