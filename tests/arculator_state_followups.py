#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""SS-1b public API regressions on authored media, in independent processes."""
import argparse
import os
from pathlib import Path
import re
import tempfile
import shutil
from arculator_state_matrix import run

p = argparse.ArgumentParser(description=__doc__)
for name in ('harness', 'core', 'fixtures', 'output'):
    p.add_argument(name, type=Path)
p.add_argument('--mode', choices=('notices', 'session-poll', 'first-frame',
                                 'cmos-cursor', 'capacity', 'held-media', 'retained-poll', 'load-rearm'), required=True)
a = p.parse_args()
for name in ('harness', 'core', 'fixtures', 'output'):
    setattr(a, name, getattr(a, name).resolve())
a.output.mkdir(parents=True, exist_ok=True)
for machine in ('A3000', 'A3010', 'A3020', 'A4000'):
    if a.mode in ('notices', 'held-media') and machine != 'A3020':
        continue  # Replacement is HD floppy geometry: new-I/O profile required.
    with tempfile.TemporaryDirectory(dir=a.output) as directory:
        env = dict(os.environ, ARCDROID_SS1_CLASS='adf-shadow' if a.mode in ('notices', 'held-media') else 'adf')
        single = run([a.harness, a.core, a.fixtures / 'arcdroid-test.rom',
                      a.fixtures / 'arcdroid-test.adf', Path(directory),
                      '--ss1-' + a.mode, machine], env, timeout=180)
        print(single, flush=True)
    if a.mode == 'capacity':
        with tempfile.TemporaryDirectory(dir=a.output) as directory:
            root = Path(directory)
            names = [f'disc-{i}.adf' for i in range(32)]
            for name in names:
                shutil.copyfile(a.fixtures / 'arcdroid-test.adf', root / name)
            playlist = root / 'set.m3u'
            playlist.write_text('\n'.join(names) + '\n')
            full = run([a.harness, a.core, a.fixtures / 'arcdroid-test.rom',
                        playlist, root, '--ss1-capacity-set', machine],
                       dict(os.environ, ARCDROID_SS1_CLASS='adf'), timeout=180)
            print(full, flush=True)
            # Compare public capacities on this ABI; long double and aggregate
            # layouts need not give the host arm64 byte count on Windows/Linux.
            one = int(re.search(r'SS1b capacity: .* bound=(\d+)', single)[1])
            thirty_two = int(re.search(r'SS1b full set: .* base=(\d+)', full)[1])
            assert one == thirty_two, f'32-slot reservation differs: one={one}, full={thirty_two}'
