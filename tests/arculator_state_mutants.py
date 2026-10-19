#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""AC-3: test-core omission of one changed field per serialized subsystem.

The fault hook consumes and validates the wire field, then omits its restore.
AC-2 itself (canonical identity or the following video/PCM/CPU trace) must kill
it. No guard failure or a stale test store is counted as a kill.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from arculator_state_matrix import run

CASES = [
    ('cpu', 'adf', 'A3000', 'engine/arculator/src/arm.c:armregs'),
    ('memory-memc', 'adf', 'A3000', 'memory.ram'),
    ('scheduler', 'adf', 'A3000', 'engine/arculator/src/timer.c:tsc'),
    ('cmos-rtc', 'adf', 'A3000', 'engine/arculator/src/cmos.c:systemtime'),
    ('audio-engine', 'adf', 'A3000', 'engine/arculator/src/sound.c:samp_rp'),
    ('audio-queue', 'adf', 'A3000', 'platform/arcdroid_sound.c:arc_audio_read'),
    ('raster', 'adf', 'A3000', 'video.bitmap.pixels'),
    ('platform-video', 'adf', 'A3000', 'video.platform.pixels'),
    ('frontend-video', 'adf', 'A3000', 'libretro/arcdroid_libretro.c:arc_present_frame'),
    ('input-kart', 'adf', 'A3000', 'libretro/arcdroid_libretro.c:arc_inputs'),
    ('osk-touch', 'adf', 'A3000', 'libretro/arcdroid_libretro.c:arc_osk'),
    ('frontend-session', 'adf', 'A3000', 'libretro/arcdroid_libretro.c:arc_frame_number'),
    ('observers', 'adf', 'A3000', 'engine/arculator_backend.c:arc_boot_watch'),
    ('wd1770', 'adf', 'A3000', 'engine/arculator/src/wd1770.c:wd1770'),
    ('82c711', 'adf', 'A3010', 'engine/arculator/src/82c711_fdc.c:_fdc'),
    ('raw-transfer', 'adf', 'A3000', 'engine/arculator/src/disc_adf.c:adf_readpos'),
    ('raw-cache-delta', 'adf', 'A3000', 'adf.cache.0'),
    ('native-hd-delta', 'hd-dirty', 'A3020', 'hd.image'),
    ('raw-medium', 'adf-shadow', 'A3000', 'media.raw-image'),
    ('jfd-transfer', 'jfd', 'A3000', 'engine/arculator/src/disc_jfd.c:jfd_phase_us'),
    ('jfd-overlay', 'jfd-shadow', 'A3000', 'media.0.jfd.overlay'),
    ('ide', 'hd', 'A3020', 'engine/arculator/src/ide.c:ide_internal'),
    ('native-hd', 'hd', 'A3020', 'hd.store'),
    ('pending-effects', 'jfd-shadow', 'A3000', 'stores.pending'),
    ('fpa', 'adf', 'A3000', 'engine/arculator/src/fpa.c:fparegs'),
    ('cp15', 'adf', 'A3000', 'engine/arculator/src/cp15.c:arm3cp'),
    ('one-wire', 'adf', 'A3000', 'engine/arculator/src/ds2401.c:ds2401'),
    ('ioeb', 'adf', 'A3000', 'engine/arculator/src/ioeb.c:hs_invert'),
    ('printer', 'adf', 'A3000', 'engine/arculator/src/printer.c:printer_busy'),
    ('superio', 'adf', 'A3010', 'engine/arculator/src/82c711.c:configregs'),
    ('disc-poll', 'adf', 'A3000', 'engine/arculator/src/disc.c:disc_timer'),
    ('memc-dma', 'adf', 'A3000', 'engine/arculator/src/memc.c:memc_dma_video_req_ts'),
    ('ioc', 'adf', 'A3000', 'engine/arculator/src/ioc.c:ioc'),
    ('keyboard', 'adf', 'A3000', 'engine/arculator/src/keyboard.c:keyboard_timer'),
    ('platform-joystick', 'adf', 'A3010', 'platform/arcdroid_joystick.c:joystick_state'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('harness', type=Path)
    parser.add_argument('core', type=Path)
    parser.add_argument('fixtures', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.harness, args.core, args.fixtures, args.output = (
        p.resolve() for p in (args.harness, args.core, args.fixtures, args.output))
    args.output.mkdir(parents=True, exist_ok=True)
    groups = {}
    for label, media_class, machine, owner in CASES:
        groups.setdefault((media_class, machine), []).append((label, owner))
    killed = 0
    for (media_class, machine), mutants in groups.items():
        with tempfile.TemporaryDirectory(prefix=f'{media_class}-{machine}-', dir=args.output) as directory:
            root = Path(directory)
            media = {'adf': 'arcdroid-test.adf', 'adf-shadow': 'arcdroid-test.adf',
                     'jfd': 'arcdroid-test.jfd', 'jfd-shadow': 'adffs/writable.jfd',
                     'hd': 'hd-family/valid-f.adf','hd-dirty': 'hd-family/valid-f.adf'}[media_class]
            env = dict(os.environ, ARCDROID_SS1_CLASS=media_class)
            command = [args.harness, args.core, args.fixtures / 'arcdroid-test.rom',
                       args.fixtures / media, root]
            run(command + ['--ss1-produce', machine], env)
            for label, owner in mutants:
                shutil.rmtree(root / 'save')
                shutil.copytree(root / 'checkpoint-save', root / 'save')
                result = subprocess.run(list(map(str, command + ['--ss1-consume', machine])),
                                        env=dict(env, ARCDROID_SS1_DROP_OBJECT=owner),
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, timeout=90)
                text = result.stdout
                if result.returncode == 0 or 'frontier.' in text or not (
                        'memcmp(saved,again,bytes)' in text or
                        'SS1 canonical serialization failed after successful restore' in text or
                        'SS1 fresh-process mismatch' in text or
                        'retro_unserialize(saved,bytes)' in text):
                    raise RuntimeError(f'{label}: survived or failed outside AC-2\n{text}')
                print(f'AC-2 killed {label}: {owner}', flush=True)
                killed += 1
    print(f'SS1 field-drop mutants: {killed}/{len(CASES)} killed by AC-2')


if __name__ == '__main__':
    main()
