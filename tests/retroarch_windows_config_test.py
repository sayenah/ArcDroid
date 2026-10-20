#!/usr/bin/env python3
"""Check native RA config templates without requiring Windows/PowerShell.
SPDX-License-Identifier: GPL-2.0-only
"""
from pathlib import Path
import hashlib
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
STANDALONE = (ROOT / 'tools/test-retroarch-rpc-ordinary-windows.ps1').read_text(encoding='utf-8')
SWITCH_SCRIPT = ROOT / 'tools/check-retroarch-rpc-switch.py'
spec = importlib.util.spec_from_file_location('ordinary_switch', SWITCH_SCRIPT)
switch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(switch)
OPTIONS_SCRIPT = ROOT / 'tools/check-retroarch-rpc-options.py'
spec = importlib.util.spec_from_file_location('ordinary_options', OPTIONS_SCRIPT)
options = importlib.util.module_from_spec(spec)
spec.loader.exec_module(options)


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
    if config.get('user_language') != '0':
        raise AssertionError('Exact frontend rejection requires English log text')
    for key, value in {'global_core_options': 'true', 'game_specific_options': 'false',
                       'auto_overrides_enable': 'false', 'config_save_on_exit': 'false',
                       'frontend_log_level': '0', 'log_to_file_timestamp': 'false'}.items():
        if config.get(key) != value:
            raise AssertionError(f'{key} must isolate options and record actual callback values')
    # The real lock and store assertions use the configured save root. RA's
    # default per-core subdirectory would give the guest a different directory.
    for key in ('sort_savefiles_enable', 'sort_savefiles_by_content_enable'):
        if config.get(key) != 'false':
            raise AssertionError(f'{key} must keep the configured save root')
    return config


class WindowsConfig(unittest.TestCase):
    def test_authored_raster_wait(self):
        # Independent 8bpp ring expectation, encoded exactly like the raw
        # screenshot helper's opaque BGRA rows. Reject the old uniform ROM,
        # a black pending frame, inverted halves and a one-pixel corruption.
        expected = b''.join(bytes([0x55 if i % 4096 < 2048 else 0xaa])*3 + b'\xff'
                            for i in range(640*256))
        digest = hashlib.sha256(expected).hexdigest()
        self.assertIn(f"Dimensions = '640x256'; VisualSignature = '{digest}'", ORDINARY)
        for wrong in (bytes([85,85,85,255])*(640*256), bytes([0,0,0,255])*(640*256),
                      expected[2048*4:]+expected[:2048*4], b'\x00'+expected[1:]):
            self.assertNotEqual(hashlib.sha256(wrong).hexdigest(), digest)
        self.assertIn('Capture-ExpectedVisualSignature $run "rpc-ordinary-guest-$leg" $expectedRaster', ORDINARY)
        self.assertNotIn('Capture-StableRawScreenshot $run "rpc-ordinary-guest-$leg"', ORDINARY)
        self.assertIn("$guest.Colours -lt 2", ORDINARY)
        wait = NATIVE.split('function Capture-ExpectedVisualSignature(', 1)[1].split('function ', 1)[0]
        for required in ('AddSeconds(12)', 'Capture-RawScreenshot', '$Expected.VisualSignature',
                         '$Expected.Dimensions', 'throw'):
            self.assertIn(required, wait)
        raw = NATIVE.split('function Capture-RawScreenshot(', 1)[1].split('function ', 1)[0]
        self.assertIn('$Run.Process.HasExited', raw)

    def test_secondary_commands_wait_for_observed_effects(self):
        preempt = ORDINARY.split('function Invoke-OrdinaryPreemptRefusal(', 1)[1].split('function ', 1)[0]
        self.assertLess(preempt.index('$boundary ='), preempt.index("Send-MediaCommand $Run 'PREEMPT_TOGGLE'"))
        self.assertIn('does not support save states', preempt)
        self.assertIn('$boundary)', preempt)
        commands = ORDINARY.split('# Initial size-zero run-ahead fails;', 1)[1].split('if ($leg -eq 2)', 1)[0]
        self.assertIn("Wait-OrdinaryPlayback $run 'PAUSED'", commands)
        self.assertIn("Wait-OrdinaryPlayback $run 'PLAYING'", commands)
        self.assertNotIn('Start-Sleep', commands)
        self.assertEqual(commands.count('Invoke-OrdinaryPreemptRefusal $run'), 2)
        self.assertIn('Wait-OrdinarySecondaryCount $run ($beforeSecondary + 1)', commands)
        self.assertIn("'ON' } else { 'OFF'", commands)
        self.assertIn('Authored no-op', commands)
        self.assertIn("Capture-ExpectedVisualSignature $run 'rpc-ordinary-after-secondary-refusals' $guest", commands)
        pause = ORDINARY.split('function Wait-OrdinaryPlayback(', 1)[1].split('function ', 1)[0]
        for required in ("Send-MediaCommand $Run 'GET_STATUS'", 'AddSeconds(10)',
                         "'^GET_STATUS '", '$Run.Udp.Receive', 'throw'):
            self.assertIn(required, pause)

    def test_absolute_root_and_real_contention(self):
        resolve = '$ordinaryRoot = (Resolve-Path -LiteralPath $ordinaryRoot).Path'
        self.assertIn(resolve, ORDINARY)
        self.assertLess(ORDINARY.index(resolve), ORDINARY.index('$ordinarySave ='))
        for name in ('ordinarySystem', 'ordinarySave', 'closedSystem', 'closedSave', 'archiveSave'):
            self.assertRegex(ORDINARY, rf'\${name} = Join-Path \$ordinaryRoot ')
        self.assertIn('$pendingOwner.Lock(0, 1)', ORDINARY)
        self.assertIn('[IO.FileShare]::ReadWrite', ORDINARY)
        self.assertIn('d-$key-m3-p$procedure-t$timing.lock', ORDINARY)
        self.assertLess(ORDINARY.index('$pendingOwner.Lock(0, 1)'), ORDINARY.index('"rpc-ordinary-$leg"'))
        self.assertIn('[UInt64]$attempts -le 2', ORDINARY)
        self.assertIn('[UInt64]$waits -eq 0', ORDINARY)
        self.assertIn("'rpc-ordinary-contention.txt'", ORDINARY)
        wait = ORDINARY.split('function Wait-OrdinaryLog(', 1)[1].split('function Get-OrdinarySecondaryCount', 1)[0]
        self.assertLess(wait.index('state=11'), wait.index('if ($text -match $Pattern)'))

    def test_standalone_imports_only_existing_helpers(self):
        block = STANDALONE.split('$helperNames = @(', 1)[1].split('\n)', 1)[0]
        names = re.findall(r"'([^']+)'", block)
        definitions = re.findall(r'^function ([\w-]+)\b', NATIVE, re.M)
        self.assertEqual(len(names), 15)
        for name in names:
            self.assertEqual(definitions.count(name), 1, name)
        self.assertIn('FunctionDefinitionAst', STANDALONE)
        self.assertIn('[scriptblock]::Create($definitions[0].Extent.Text)', STANDALONE)
        self.assertIn(". (Join-Path $PSScriptRoot 'test-retroarch-rpc-route.ps1')", STANDALONE)
        self.assertNotIn('. (Join-Path $PSScriptRoot \'test-retroarch-windows.ps1\')', STANDALONE)
        self.assertNotIn('Invoke-ArcDroid', names)
        self.assertIn('WorkDirectory must be new', STANDALONE)
        self.assertIn('ordinary-transcript.txt', STANDALONE)
        self.assertIn('R3d-4 ONLY PASS:', STANDALONE)
        self.assertIn("-ArgumentList '--version'", STANDALONE)
        self.assertIn("Get-Command python -ErrorAction Stop", STANDALONE)
        self.assertIn("-Filter '*.dll' -File", STANDALONE)
        self.assertNotIn('curl.exe', STANDALONE)
        self.assertIn('test-retroarch-rpc-ordinary-windows.ps1', (ROOT / '.github/workflows/ci.yml').read_text())

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
                               ('sort_savefiles_by_content_enable', 'true'),
                               ('global_core_options', 'false'), ('game_specific_options', 'true'),
                               ('auto_overrides_enable', 'true'), ('config_save_on_exit', 'true'),
                               ('frontend_log_level', '1'), ('log_to_file_timestamp', 'true')):
                mutant = [re.sub(rf'^{key} = .*', f'{key} = "{value}"', line)
                          for line in lines]
                with self.subTest(key=key), self.assertRaises(AssertionError):
                    check_ordinary(mutant)
        for key in ('global_core_options', 'game_specific_options', 'auto_overrides_enable',
                    'config_save_on_exit', 'frontend_log_level', 'log_to_file_timestamp'):
            with self.subTest(missing=key), self.assertRaises(AssertionError):
                check_ordinary([line for line in ordinary if not line.startswith(key+' =')])
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


class DeliveredOptions(unittest.TestCase):
    def reads(self):
        return [f'[DEBUG] [Environ] GET_VARIABLE: {key} = "Auto"' for key in options.EXPECTED]

    def test_actual_callbacks_and_leaks(self):
        self.assertEqual(options.verify(self.reads()), dict.fromkeys(options.EXPECTED, 1))
        self.assertEqual(options.verify(self.reads()*2), dict.fromkeys(options.EXPECTED, 2))
        for key in options.EXPECTED:
            for value in ('A3000', 'ARM2', 'RISC OS 3.11', 'auto', ''):
                wrong = f'[DEBUG] [Environ] GET_VARIABLE: {key} = "{value}"'
                for lines in ([wrong]+self.reads(), self.reads()+[wrong]):
                    with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                        options.verify(lines)
        for key in options.EXPECTED:
            error = f'[ERROR] [Environ] GET_VARIABLE: {key} - Invalid value.'
            with self.assertRaises(ValueError):
                options.verify(self.reads()+[error])
        for lines in ([], self.reads()[:-1], ['arcdroid_machine = "Auto"'],
                      [line.replace('[DEBUG]', '[libretro DEBUG]') for line in self.reads()],
                      [line.replace('GET_VARIABLE:', 'SET_VARIABLE:') for line in self.reads()]):
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                options.verify(lines)

    def test_cli_windows_log_encoding(self):
        with tempfile.TemporaryDirectory(prefix='rpc-options-') as temp:
            log = Path(temp) / 'frontend.log'
            args = [sys.executable, '-B', str(OPTIONS_SCRIPT), str(log)]
            for lines, good in ((self.reads(), True), (self.reads()[:-1], False),
                                (self.reads()+[self.reads()[0].replace('Auto', 'A3000')], False)):
                log.write_text('\r\n'.join(lines), encoding='utf-8-sig', newline='')
                result = subprocess.run(args, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode == 0, good, result.stderr)
                self.assertEqual('PASS' in result.stdout, good)

    def test_every_process_gets_fresh_options_and_checks(self):
        fresh = ORDINARY.split('function New-OrdinaryLegConfig(', 1)[1].split('function ', 1)[0]
        self.assertIn('"options-$Name"', fresh)
        self.assertIn('Copy-Item -LiteralPath $ordinaryOptions $legOptions', fresh)
        self.assertIn("$_ -notmatch '^core_options_path\\s*='", fresh)
        self.assertIn('New-Item -ItemType Directory $directory', fresh)
        self.assertNotIn('-Force', fresh)
        self.assertIn('ConfigPath $legOptions', fresh)
        for base, name in (('closedConfig', "'rpc-ordinary-closed-legacy'"),
                           ('ordinaryConfig', '"rpc-ordinary-$leg"'),
                           ('ordinaryConfig', "'rpc-ordinary-fresh-arculator'"),
                           ('archiveConfig', '$Name')):
            self.assertIn(f'New-OrdinaryLegConfig ${base} {name}', ORDINARY)
        self.assertEqual(ORDINARY.count('Assert-OrdinaryOptions $run.RetroLog $run.Name'), 6)
        refusal = ORDINARY.split('function Invoke-OrdinaryRefusal(', 1)[1]
        self.assertIn('"--config=$legConfig"', refusal)
        self.assertEqual(refusal.count('Assert-OrdinaryOptions $refusalLog $Name'), 2)
        self.assertLess(refusal.index('Assert-OrdinaryOptions $refusalLog $Name'), refusal.index('if ($text -notmatch $Pattern'))
        self.assertIn('"$Name-options.txt"', ORDINARY)
        self.assertIn('if ($LASTEXITCODE -ne 0)', ORDINARY)


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
        self.assertIn("Capture-StableRawScreenshot $run 'rpc-ordinary-after-arculator' 2", attempt)
        self.assertIn("Capture-StableRawScreenshot $run 'rpc-ordinary-fresh-arculator' 2", attempt)
        stable = NATIVE.split('function Capture-StableRawScreenshot(', 1)[1].split('Add-Type', 1)[0]
        self.assertIn('[int]$MinimumColours = 0', stable)
        self.assertIn('$capture.Colours -ge $MinimumColours', stable)
        self.assertIn('AddSeconds(12)', stable)
        self.assertIn('RetroArch ${retroArchVersion}: same-process', attempt)
        self.assertIn('Odin3 assisted J6', attempt)
        self.assertIn("Invoke-OrdinaryRefusal $ordinaryCore 'rpc-ordinary-test-j4'", attempt)
        self.assertIn("'rpc-ordinary-shipping-j3' 'N-1: Needs RiscPC'", attempt)


if __name__ == '__main__':
    unittest.main()
