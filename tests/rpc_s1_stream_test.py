#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exact on/off output and exercise proof on authored streaming guest code."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from rpc_measure_workloads import generate


def compare(runner, core, kind, restart=()):
    with tempfile.TemporaryDirectory(prefix='arcdroid-s1c-stream-') as temp:
        root = Path(temp)
        generate(root, kind)
        counts = []
        outputs = []
        for off in (False, True):
            save = root / ('off' if off else 'on')
            save.mkdir()
            command = [runner, '--core', core, '--plan', str(root / (kind + '.arcplan')),
                       '--system', str(root), '--save', str(save), '--frames', '1',
                       '--pacing', 'flat', '--exercise', *restart]
            if not restart:
                # Enough real code to enter the stream loop; keep hash-based
                # cross-checks cheap even in the Debug sanitizer CI job.
                command += ['--endpoint-instructions', '1:4096:0']
            if off:
                command += ['--s1-off']
            result = subprocess.run(command, capture_output=True, timeout=100)
            if result.returncode:
                raise RuntimeError(result.stderr.decode(errors='replace'))
            if not result.stdout.startswith(b'frame=0 '):
                raise AssertionError('runner omitted the guest frame trace')
            values = dict((name.decode(), int(value)) for name, value in re.findall(
                rb'^exercise (\w+)=(\d+)\r?$', result.stderr, re.M))
            if not values:
                raise AssertionError('runner omitted exercise counters')
            counts.append(values)
            outputs.append(result.stdout)
        if outputs[0] != outputs[1]:
            raise AssertionError('S1 on/off guest output differs')
        taken = counts[0].pop('service_s1_taken')
        if taken <= 0 or counts[1].pop('service_s1_taken') != 0:
            raise AssertionError('S1 on must take skips and off must take none')
        if counts[0] != counts[1]:
            raise AssertionError('S1 on/off existing scalar counters differ')
        print(f'{kind} restart={list(restart)}: byte-identical output, '
              f'on taken={taken}, off taken=0; existing scalar counters identical')


if __name__ == '__main__':
    compare(*sys.argv[1:])
