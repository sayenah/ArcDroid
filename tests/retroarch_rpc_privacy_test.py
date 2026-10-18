#!/usr/bin/env python3
"""Authored raw log forms and fail-closed F4 regression cases.
SPDX-License-Identifier: GPL-2.0-only
"""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

SCRIPT = Path(__file__).resolve().parents[1] / 'tools/check-retroarch-rpc-privacy.py'
spec = importlib.util.spec_from_file_location('privacy', SCRIPT)
privacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(privacy)
SENTINELS = ('R3D3-CONTENT-SENTINEL', 'R3D3-SAVE-SENTINEL')
NOTICE = 'Checking the RiscPC hard disc'
FRONTEND = [
    f'[INFO] [Environ] GET_SAVE_DIRECTORY: "C:/authored/{SENTINELS[1]}".',
    f'[INFO] [Environ] GET_SYSTEM_DIRECTORY: "/authored/{SENTINELS[0]}".',
    f'[INFO] [Override] Redirecting save file to "C:/{SENTINELS[0]}.srm".',
    f'[INFO] [Override] Redirecting save state to "C:/{SENTINELS[0]}.state".',
    f'[INFO] [Override] Redirecting cheat file to "C:/{SENTINELS[0]}.cht".',
    f'[INFO] [Content] Loading content file: "C:/{SENTINELS[0]}.arcplan".',
    f'[INFO] [Core] Using content: "/authored/{SENTINELS[0]}.arcplan".',
    f'[INFO] [Runtime] Saving runtime log file: "C:/{SENTINELS[0]}.lrtl".',
]


def baseline(mode, android=False):
    marker = 'SET_MESSAGE_EXT' if mode == 'extended' else 'SET_MESSAGE'
    lines = [*FRONTEND, f'[libretro INFO] N-7: {NOTICE}',
             f'[libretro ERROR] RPC privacy probe: {marker} accepted']
    if mode == 'legacy':
        lines.append(f'[INFO] [Environ] SET_MESSAGE: {NOTICE}')
    if android:
        lines = [s.removeprefix('[INFO] ').removeprefix('[libretro INFO] ').removeprefix('[libretro ERROR] ') for s in lines]
    return lines


def run(lines, mode='extended', expected=True):
    output = []
    actual = privacy.check(lines, SENTINELS, NOTICE, mode, output.append)
    assert actual == expected, '\n'.join(output)
    return output


def main():
    cases = 0
    for mode in ('extended', 'legacy'):
        for android in (False, True):
            lines = baseline(mode, android)
            output = run(lines, mode)
            assert len([s for s in output if 'source=' in s]) == len(FRONTEND)
            assert all('form=retroarch-' in s for s in output if 'source=' in s)
            cases += 1
        for level in ('DEBUG', 'INFO', 'WARN', 'ERROR'):
            # Callback payloads that resemble exempt frontend lines remain core
            # output. No ID or severity restriction can hide a raw staging leak.
            for payload in (SENTINELS[0], FRONTEND[0], 'N-7: ' + SENTINELS[1]):
                run(baseline(mode) + [f'[libretro {level}] {payload}'], mode, False)
                cases += 1
            for command in ('SET_MESSAGE', 'SET_MESSAGE_EXT'):
                run(baseline(mode) + [f'[{level}] [Environ] {command}: {SENTINELS[0]}'], mode, False)
                cases += 1
        for leak in (f'[INFO] [Content] Unknown future form: {SENTINELS[0]}',
                     f'[INFO] [Environ] GET_SAVE_DIRECTORY: "x". {SENTINELS[0]}',
                     f'[INFO] [Override] {SENTINELS[0]}',
                     SENTINELS[0].lower(), 'N-99: ' + SENTINELS[1],
                     'raw staging: ' + SENTINELS[0]):
            output = run(baseline(mode) + [leak], mode, False)
            assert any('line ' in s and 'form=' in s for s in output)
            cases += 1
        # Missing log, a quiet core, frontend-only notice text, absent delivery,
        # absent actual legacy echo, or missing either path marker cannot pass.
        for lines in ([], FRONTEND, FRONTEND + ['[INFO] ' + NOTICE],
                      [s for s in baseline(mode) if 'N-7:' not in s and 'SET_MESSAGE: ' not in s],
                      [s for s in baseline(mode) if 'probe:' not in s],
                      [s for s in baseline(mode) if SENTINELS[0] not in s],
                      [s for s in baseline(mode) if SENTINELS[1] not in s]):
            run(lines, mode, False)
            cases += 1
    run([s for s in baseline('legacy') if '[Environ] SET_MESSAGE:' not in s], 'legacy', False)
    # CLI retains raw lines, reports each owned sentinel line, and returns failure
    # for an unknown form; encoding accepts Windows UTF-8 BOM and CRLF.
    with tempfile.TemporaryDirectory(prefix='arc-f4-') as temp:
        log = Path(temp) / 'authored.log'
        args = [sys.executable, '-B', str(SCRIPT), str(log), '--message-path', 'legacy']
        for sentinel in SENTINELS:
            args += ['--sentinel', sentinel]
        for bad in (False, True):
            content = '\r\n'.join(baseline('legacy') + ([SENTINELS[0]] if bad else []))
            log.write_text(content, encoding='utf-8-sig', newline='')
            result = subprocess.run(args, capture_output=True, text=True, timeout=10)
            assert result.returncode == int(bad), result.stdout + result.stderr
            assert 'form=retroarch-directory-query' in result.stdout
            assert log.read_text(encoding='utf-8-sig').splitlines() == content.splitlines()
            cases += 1
    print(f'F4 classifier: {cases + 1} authored positive/negative cases passed')


if __name__ == '__main__':
    main()
