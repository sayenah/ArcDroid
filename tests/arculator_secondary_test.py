#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""R-SS3g: public RetroArch secondary routes in either shipping gate state."""
from pathlib import Path
import os, shutil, subprocess, sys, tempfile
exe, core, fixtures = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='ss1-secondary-') as temporary:
    root = Path(temporary)
    system = root/'system'
    firmware = system/'arcdroid/firmware'
    firmware.mkdir(parents=True)
    shutil.copy2(Path(fixtures)/'arcdroid-test.rom', firmware/'control.rom')
    content = root/'control.adf'
    shutil.copy2(Path(fixtures)/'arcdroid-test.adf', content)
    copied = root/'retroarch_temp'/Path(core).name
    copied.parent.mkdir()
    shutil.copy2(core, copied)
    for mode in ['ss1-secondary', 'ss1-secondary-no-path', 'ss1-secondary-cold', 'ss1-secondary-no-path-cold']:
        save = root/mode
        save.mkdir()
        env = dict(os.environ)
        # Two independent C core modules intentionally have same-named globals;
        # match the existing copied-module harness without disabling memory checks.
        env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '')+':detect_odr_violation=0'
        result = subprocess.run([exe, core, str(system), str(save), str(content), mode, str(copied)], capture_output=True, text=True, timeout=150, env=env)
        assert result.returncode == 0, result.stdout+result.stderr
        expected = 4 if '-no-path' in mode else 8
        if mode.endswith('-cold'): expected //= 2
        assert result.stdout.count('zero writes/registrations;') == expected, result.stdout
        print(result.stdout, end='')
