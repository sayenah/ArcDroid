#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""AY-1: fresh-process AC-2, all overlay write masks and reset continuations."""
from pathlib import Path
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
from arc_calendar_fixture import firmware
from arculator_state_matrix import store_bytes

exe, core, fixtures = (Path(x).resolve() for x in sys.argv[1:])
objects = (
    'engine/arculator/src/cmos.c:year_original',
    'engine/arculator/src/cmos.c:year_written',
    'engine/arculator/src/cmos.c:year_overlay',
    'libretro/arcdroid_libretro.c:arc_cmos_session_year_valid',
    'libretro/arcdroid_libretro.c:arc_cmos_session_year',
)


def produce(command, directory):
    """Copy the committed capture frontier while the producer is quiescent."""
    with (directory / 'producer.log').open('w') as errors:
        child = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=errors, text=True)
        checkpoint = queue.Queue()
        def first_checkpoint():
            lines = []
            for line in child.stdout:
                lines.append(line)
                if line == 'SS1 calendar checkpoint\n':
                    break
            checkpoint.put(''.join(lines))
        reader = threading.Thread(target=first_checkpoint, daemon=True)
        reader.start()
        try:
            prefix = checkpoint.get(timeout=180)
            reader.join()
            assert prefix.endswith('SS1 calendar checkpoint\n'), prefix + (directory / 'producer.log').read_text()
            shutil.copytree(directory / 'save', directory / 'checkpoint-save')
            output, _ = child.communicate(input='c\n', timeout=180)
            assert child.returncode == 0, prefix + output + (directory / 'producer.log').read_text()
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()


with tempfile.TemporaryDirectory(prefix='ss1-calendar-') as temp:
    root = Path(temp)
    def case(machine, year, mask, drop=None):
        with tempfile.TemporaryDirectory(prefix=machine + '-' + str(year) + '-', dir=root) as temp_case:
            directory = Path(temp_case)
            system, save = directory / 'system', directory / 'save'
            rom = system / 'arcdroid/firmware/authored.rom'
            rom.parent.mkdir(parents=True)
            rom.write_bytes(firmware())
            save.mkdir()
            command = [str(exe), str(core), str(system), str(save),
                       str(fixtures / 'arcdroid-test.adf'), 'ss1-produce',
                       str(year), machine, str(mask), str(directory)]
            produce(command, directory)
            published = store_bytes(save)
            shutil.rmtree(save)
            shutil.copytree(directory / 'checkpoint-save', save)
            env = dict(os.environ)
            if drop:
                env['ARCDROID_SS1_DROP_OBJECT'] = drop
            command[5] = 'ss1-consume'
            result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=180)
            output = result.stdout + result.stderr
            if not drop:
                assert result.returncode == 0, output
                assert output.count('canonical/24-frame CPU/PCM/guest/CMOS exact (12 before and 12 after reset); reset after load') == 1, output
                assert store_bytes(save) == published, 'calendar continuation native stores differ'
                print(output, end='', flush=True)
            else:
                assert result.returncode != 0 and 'memcmp(saved,again,bytes)' in output, output
                assert 'frontier.' not in output, output
                assert store_bytes(save) == store_bytes(directory / 'checkpoint-save')
                print('SS1 calendar AC-2 killed: ' + drop, flush=True)
    for machine in ('A3000', 'A3010', 'A3020', 'A4000'):
        for year in (2025, 2099):
            for mask in range(8):
                case(machine, year, mask)
    for owner in objects:
        case('A3000', 2025, 0, owner)
print('SS1 calendar: 64 fresh-process overlay/write-mask cases, 1536 differential frames (768 before and 768 after reset), 64 reset differentials; native stores exact; 5/5 calendar omission mutants killed')
