#!/usr/bin/env python3
"""Generate normal guest keyboard setup for the common R10-1 IDE boot disc.

GPL-2.0-only. No supplied bytes, filesystem patching or CMOS editing.
For a carried, merged R7-1 disc with the three authored root assets, --entries
also writes !Boot.!Run and !Boot.ProbeTasks via the guest's Build command.
New --resources-only images already contain those files; merge !System first.
"""
import argparse
import hashlib
import json
from pathlib import Path


SCANS = dict(zip('abcdefghijklmnopqrstuvwxyz',
                 (28, 50, 33, 35, 36, 43, 52, 51, 67, 59, 66, 75, 58,
                  49, 68, 77, 21, 45, 27, 44, 60, 42, 29, 34, 53, 26)))
SCANS.update(dict(zip('1234567890', (22, 30, 38, 37, 46, 54, 61, 62, 70, 69))))
SCANS.update({' ': 41, '.': 73, ',': 65, '-': 78, '/': 74, '\n': 90})
SHIFT = {':': 76, '$': 37, '!': 22}


def setup(entries=False):
    events = ['0 checkpoint 0 0 origin', '14000000 key_down 7 0 cli',
              '14040000 key_up 7 0 cli']
    timestamp = 15_000_000
    effective = {}

    def key(scan, tag):
        nonlocal timestamp
        events.extend((f'{timestamp + 10000} key_down {scan} 0 {tag}',
                       f'{timestamp + 40000} key_up {scan} 0 {tag}'))

    def text(value):
        nonlocal timestamp
        for char in value:
            shifted = char.isupper() or char in SHIFT
            scan = SHIFT[char] if char in SHIFT else SCANS[char.lower()]
            if shifted:
                events.append(f'{timestamp} key_down 18 0 shift')
            key(scan, 'setup')
            if shifted:
                events.append(f'{timestamp + 60000} key_up 18 0 shift')
            timestamp += 80_000 + (2_000_000 if char == '\n' else 0)

    assets = Path(__file__).resolve().parents[1] / 'boot'
    if entries:
        for target, source, filetype in (('!Run', 'HDRun', 'Obey'),
                                         ('ProbeTasks', 'HDTasks', 'Desktop')):
            lines = [line for line in (assets / source).read_text().splitlines()
                     if line and not line.startswith('|')]
            # Build expands variables; these invariant paths have the same meaning.
            lines = [line.replace('<System$Dir>', 'ADFS::4.$.!Boot.Resources.!System')
                     for line in lines]
            effective[target] = '\r'.join(lines) + '\r'
            text(f'Build ADFS::4.$.!Boot.{target}\n' + '\n'.join(lines) + '\n')
            key(118, 'build_end')
            timestamp += 2_000_000
            text(f'SetType ADFS::4.$.!Boot.{target} {filetype}\n')

    commands = ('ADFS\nDir ADFS::4.$\nOpt 4,2\nConfigure Floppies 1\n'
                'Configure IDEDiscs 1\nConfigure Boot\nConfigure FileSystem ADFS\n'
                'Configure Drive 4\nStatus Floppies\nStatus IDEDiscs\n'
                'Status Boot\nStatus Drive\n')
    text(commands)
    events.append(f'{timestamp + 3000000} end 0 0 release')
    return '\n'.join(events) + '\n', dict(
        commands=commands, effective_entries=effective,
        duration_seconds=(timestamp + 5_000_000) / 1_000_000,
        authored_sha256={name: hashlib.sha256((assets / name).read_bytes()).hexdigest()
                         for name in ('Multisync', 'BootEnv', 'BootTasks', 'HDRun', 'HDTasks')},
        sources=['https://www.riscos.com/support/developers/prm/adfs.html',
                 'https://www.riscos.com/support/developers/prm/wimp.html'],
        qualification='PENDING: verify guest-written files/CMOS, then cold boot all pairs')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True, help='new private script path')
    parser.add_argument('--entries', action='store_true', help='replace carried root entries in guest')
    args = parser.parse_args()
    script, custody = setup(args.entries)
    with args.out.open('x', encoding='ascii') as stream:
        stream.write(script)
    custody['script_sha256'] = hashlib.sha256(script.encode('ascii')).hexdigest()
    with args.out.with_suffix('.json').open('x', encoding='ascii') as stream:
        json.dump(custody, stream, indent=2)
    print('Setup duration seconds:', custody['duration_seconds'])
