#!/usr/bin/env python3
"""Check native RA config templates without requiring Windows/PowerShell.
SPDX-License-Identifier: GPL-2.0-only
"""
from pathlib import Path
import importlib.util
import re
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
NATIVE = (ROOT / 'tools/test-retroarch-windows.ps1').read_text(encoding='utf-8')
ORDINARY = (ROOT / 'tools/test-retroarch-rpc-route.ps1').read_text(encoding='utf-8')
SWITCH_SCRIPT = ROOT / 'tools/check-retroarch-rpc-switch.py'
spec = importlib.util.spec_from_file_location('ordinary_switch', SWITCH_SCRIPT)
switch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(switch)


def template(source, variable, operation='Set-Content'):
    # A here-string terminates only at column zero. Check its delimiters as
    # well as the config payload; this is not a full PowerShell parser.
    blocks = re.findall(r'''(?m)^\s*@(["'])\n(.*?)^\1@([^\n]*)''', source, re.S)
    matches = [body for _, body, command in blocks
               if re.search(rf'\| {operation} -Encoding ASCII \${variable}$', command)]
    if len(matches) != 1:
        raise AssertionError(f'Expected one {operation} template for ${variable}')
    return matches[0].splitlines()


def entries(lines):
    result = {}
    for line in lines:
        match = re.fullmatch(r'(\w+) = "(.*)"', line)
        if not match:
            raise AssertionError(f'Malformed config entry: {line!r}')
        key, value = match.groups()
        if key in result:
            # RA uses the first occurrence. A later override cannot repair it.
            raise AssertionError(f'Duplicate config entry: {key}')
        result[key] = value
    return result


def derived(variable, base):
    match = re.search(
        rf"Get-Content \$ordinaryConfig \| Where-Object {{ \$_ -notmatch '([^']+)' }}"
        rf" \| Set-Content -Encoding ASCII \${variable}\n", ORDINARY)
    if not match:
        raise AssertionError(f'Missing base config for ${variable}')
    return [line for line in base if not re.search(match[1], line)] + template(
        ORDINARY, variable, 'Add-Content')


def check_frontend(lines):
    config = entries(lines)
    expected = {
        'video_driver': 'gdi', 'audio_driver': 'sdl2', 'audio_enable': 'true',
        'record_driver': 'wav', 'midi_driver': 'null',
        'midi_input': 'Null', 'midi_output': 'Null', 'input_driver': 'dinput',
        'network_cmd_enable': 'true', 'network_cmd_port': '55355',
        'pause_nonactive': 'false', 'quit_press_twice': 'false',
    }
    for key, value in expected.items():
        if config.get(key) != value:
            raise AssertionError(f'{key}: expected {value}, got {config.get(key)}')
    for key in ('system_directory', 'savefile_directory', 'savestate_directory',
                'libretro_directory', 'libretro_info_path', 'screenshot_directory'):
        if not config.get(key):
            raise AssertionError(f'Missing directory: {key}')
    return config


def check_ordinary(lines):
    config = check_frontend(lines)
    # The real lock and store assertions use the configured save root. RA's
    # default per-core subdirectory would give the guest a different directory.
    for key in ('sort_savefiles_enable', 'sort_savefiles_by_content_enable'):
        if config.get(key) != 'false':
            raise AssertionError(f'{key} must keep the configured save root')
    return config


class WindowsConfig(unittest.TestCase):
    def test_working_and_ordinary_configs(self):
        ordinary = template(ORDINARY, 'ordinaryConfig')
        configs = [template(NATIVE, 'config'), ordinary,
                   derived('closedConfig', ordinary), derived('archiveConfig', ordinary)]
        for lines in configs:
            with self.subTest(config=lines):
                check_frontend(lines)
        opened, closed, archive = map(check_ordinary, configs[1:])
        for key in ('run_ahead_enabled', 'run_ahead_secondary_instance', 'rewind_enable'):
            self.assertEqual(opened[key], 'true')
        self.assertEqual(opened['video_gpu_screenshot'], 'false')
        self.assertEqual(closed['run_ahead_enabled'], 'false')
        self.assertEqual(closed['rewind_enable'], 'false')
        self.assertNotEqual(opened['system_directory'], closed['system_directory'])
        self.assertNotEqual(opened['savefile_directory'], closed['savefile_directory'])
        self.assertNotEqual(opened['savefile_directory'], archive['savefile_directory'])
        self.assertEqual(opened['system_directory'], archive['system_directory'])

    def test_bad_driver_port_and_duplicate_mutants(self):
        ordinary = template(ORDINARY, 'ordinaryConfig')
        for lines in (ordinary, derived('closedConfig', ordinary),
                      derived('archiveConfig', ordinary)):
            for key, value in (('video_driver', 'gl'), ('network_cmd_enable', 'false'),
                               ('network_cmd_port', '55356'), ('midi_driver', 'winmm'),
                               ('sort_savefiles_enable', 'true'),
                               ('sort_savefiles_by_content_enable', 'true')):
                mutant = [re.sub(rf'^{key} = .*', f'{key} = "{value}"', line)
                          for line in lines]
                with self.subTest(key=key), self.assertRaises(AssertionError):
                    check_ordinary(mutant)
        with self.assertRaises(AssertionError):
            check_frontend(['video_driver = "gl"', *ordinary])

    def test_live_runs_have_no_frame_limit(self):
        start = NATIVE.split('function Start-MediaRun(', 1)[1].split(
            'function Send-MediaCommand(', 1)[0]
        self.assertNotIn('--max-frames', start)
        self.assertIn('Wait-RetroArchStartReady', start)
        self.assertIn('55355', start)
        # Frame limits belong only to expected-refusal runs, after live legs.
        live, refusal = ORDINARY.split('function Invoke-OrdinaryRefusal(', 1)
        self.assertNotIn('--max-frames', live)
        self.assertIn('--max-frames=180', refusal)
        self.assertIn('Wait-RetroArchStartReady', refusal)


class SwitchRejection(unittest.TestCase):
    core = 'C:/authored/core dir/arcdroid_rpcroute_libretro.dll'
    command = 'LOAD_CONTENT C:/authored/core dir/arcdroid_rpcroute_libretro.dll|C:/authored/disc.adf'

    def log(self):
        return ['[INFO] RetroArch 1.22.2 (Git 69a4f0e)',
                f'[INFO] [Core] Loading dynamic libretro core from: "{self.core}".']

    def rejection(self, command=None):
        return f'[WARN] Unrecognized command "{command or self.command}" received.'

    def classify(self, tail, before=None):
        initial = self.log() if before is None else before
        return switch.classify(initial + tail, len(initial), self.command, self.core)

    def test_exact_fresh_rejection_only(self):
        rejection = self.rejection()
        self.assertEqual(self.classify([rejection]), 'unsupported')
        for line in ('', rejection[:-1], rejection.lower(), ' ' + rejection,
                     rejection + ' trailing', '[libretro WARN] ' + rejection,
                     rejection.replace('[WARN]', '[INFO]'),
                     rejection.replace('Unrecognized', 'Unrecognised'),
                     self.rejection(self.command + '.other'),
                     self.rejection('LOAD_CONTENT unrelated|unrelated')):
            with self.subTest(line=line):
                self.assertEqual(self.classify([line]), 'strict')
        # An identical old rejection cannot excuse a recognised but inert
        # command. Neither a quiet log nor a normal acknowledgement can skip.
        self.assertEqual(self.classify([], self.log() + [rejection]), 'strict')
        self.assertEqual(self.classify([]), 'strict')
        self.assertEqual(self.classify(['[INFO] Command accepted.']), 'strict')
        self.assertEqual(self.classify(['[libretro INFO] Firmware: ArcDroid test ROM']), 'strict')

    def test_wrong_core_and_conflicting_outcomes_fail(self):
        wrong = '[INFO] [Core] Loading dynamic libretro core from: "C:/authored/other.dll".'
        for tail in ([wrong], [wrong, self.rejection()],
                     [self.rejection(), '[libretro INFO] Firmware: ArcDroid test ROM'],
                     [self.rejection(), self.log()[1]],
                     [self.rejection(), '[ERROR] Failed to load content']):
            with self.subTest(tail=tail), self.assertRaises(ValueError):
                self.classify(tail)
        with self.assertRaises(ValueError):
            self.classify([self.rejection()], [wrong])
        with self.assertRaises(ValueError):
            switch.classify(self.log(), 3, self.command, self.core)
        # A future recognised reload of the requested core still takes the
        # strict path. Windows drive case and slash spelling are equivalent.
        same = self.log()[1].replace(self.core, self.core.upper().replace('/', '\\'))
        self.assertEqual(self.classify([same, '[libretro INFO] Firmware: ArcDroid test ROM']), 'strict')
        relative = self.log()[1].replace(self.core, r'core dir\arcdroid_rpcroute_libretro.dll')
        self.assertEqual(switch.classify([relative, self.rejection()], 1, self.command,
                                        self.core, 'C:/authored'), 'unsupported')

    def test_cli_windows_encoding_and_errors(self):
        with tempfile.TemporaryDirectory(prefix='rpc-switch-') as temp:
            log = Path(temp) / 'frontend.log'
            args = [sys.executable, '-B', str(SWITCH_SCRIPT), str(log),
                    '--after-lines', '2', '--command', self.command, '--core', self.core]
            for tail, expected in (([self.rejection()], 'unsupported'), ([], 'strict')):
                log.write_text('\r\n'.join(self.log() + tail), encoding='utf-8-sig', newline='')
                result = subprocess.run(args, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), expected)
            log.write_text('no core identity', encoding='utf-8')
            result = subprocess.run(args, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, '')

    def test_script_keeps_runtime_and_strict_checks(self):
        attempt = ORDINARY.split('if ($leg -eq 2) {', 1)[1]
        self.assertLess(attempt.index('$switchStart ='), attempt.index('Send-MediaCommand $run $loadCommand'))
        self.assertIn('--after-lines $switchStart --command $loadCommand --core', attempt)
        self.assertLess(attempt.index('if ($run.Process.HasExited)'), attempt.index("if ($switchResult -ceq 'unsupported')"))
        self.assertIn("Capture-ExpectedVisualSignature $run 'rpc-ordinary-command-unsupported' $guest", attempt)
        strict = attempt.split("elseif ($switchResult -ceq 'strict') {", 1)[1].split('} else {', 1)[0]
        self.assertIn("Wait-OrdinaryLog $run 'Firmware: ArcDroid test ROM' $switchStart", strict)
        self.assertIn("Capture-StableRawScreenshot $run 'rpc-ordinary-after-arculator'", strict)
        self.assertIn('if ($run.Process.HasExited)', strict)
        self.assertIn('Stop-MediaRun $run', attempt)
        self.assertIn('$finalSwitchResult -cne $switchResult', attempt)
        self.assertIn('$ordinarySwitchDrivable -and $fresh.VisualSignature -ne $ordinaryArcSignature', attempt)
        self.assertIn('$fresh.Colours -lt 2', attempt)
        self.assertIn('RetroArch ${retroArchVersion}: same-process', attempt)
        self.assertIn('Odin3 assisted J6', attempt)
        self.assertIn("Invoke-OrdinaryRefusal $ordinaryCore 'rpc-ordinary-test-j4'", attempt)
        self.assertIn("'rpc-ordinary-shipping-j3' 'N-1: Needs RiscPC'", attempt)


if __name__ == '__main__':
    unittest.main()
