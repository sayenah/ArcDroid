#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""SS-1 AC-2: separate-process snapshots, all admitted machines/media routes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(command, env, timeout=90):
    result = subprocess.run(list(map(str, command)), env=env, text=True, errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=timeout)
    if result.returncode:
        raise RuntimeError(f'{command[-2:]} returned {result.returncode}\n{result.stdout}')
    return result.stdout.strip()


def store_bytes(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('harness', type=Path)
    parser.add_argument('core', type=Path)
    parser.add_argument('fixtures', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--class', dest='only_class')
    parser.add_argument('--machine')
    args = parser.parse_args()
    for attr in ('harness', 'core', 'fixtures', 'output'):
        setattr(args, attr, getattr(args, attr).resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    cases = {
        'adf': 'arcdroid-test.adf',
        'cmos-pending': 'arcdroid-test.adf',
        'adf-shadow': 'arcdroid-test.adf',
        'adf-original': 'arcdroid-test.adf',
        'adl': 'arcdroid-application-l.adl',
        'jfd': 'arcdroid-test.jfd',
        'jfd-shadow': 'adffs/writable.jfd',
        'jfd-swap': 'adffs/Writable-folder.jfd/Writable-folder.jfd',
        'jfd-gzip': 'adffs/writable-gzip.jfd',
        'zip': 'zip-content/stored.zip',
        'folder': 'adffs/Writable-folder.jfd/Writable-folder.jfd',
        'playlist': 'arcdroid-media.m3u',
        'adffs': 'adffs/eligible.jfd',
        'hd-media': 'hd-family/valid-f.adf',
        'hd': 'hd-family/valid-f.adf',
        'hd-dirty': 'hd-family/valid-f.adf',
        'hd-jfd': 'adffs/eligible-hd-rw.jfd',
    }
    machines = ['A3000', 'A3010', 'A3020', 'A4000']
    if args.only_class:
        cases = {args.only_class: cases[args.only_class]}
    if args.machine:
        machines = [args.machine]
    results = []
    for media_class, media in cases.items():
        for machine in machines:
            if (media_class.startswith('hd') and machine == 'A3000') or (
                    media_class in ('hd', 'hd-jfd','hd-dirty') and machine != 'A3020'):
                continue  # Existing admission: no old-I/O HD floppies, IDE only A3020.
            with tempfile.TemporaryDirectory(prefix=f'{media_class}-{machine}-',
                                             dir=args.output) as directory:
                root = Path(directory)
                if media_class == 'adffs':
                    package_dir = root / 'system/arcdroid/adffs'
                    package_dir.mkdir(parents=True)
                    shutil.copyfile(args.fixtures / 'adffs/synthetic.zip',
                                    package_dir / 'synthetic.zip')
                content = args.fixtures / media
                if media_class == 'adf-original':
                    content = root / 'authored-original.adf'
                    shutil.copyfile(args.fixtures / media, content)
                env = dict(os.environ, ARCDROID_SS1_CLASS=media_class)
                command = [args.harness, args.core,
                           args.fixtures / 'arcdroid-test.rom',
                           content, root]
                produced = run(command + ['--ss1-produce', machine], env)
                published = store_bytes(root / 'save')
                original = content.read_bytes() if media_class == 'adf-original' else None
                # Independent continuations start from the same committed frontier.
                # The core never rolls a real store back: these are disposable,
                # authored test branches, copied at the save boundary.
                shutil.rmtree(root / 'save')
                shutil.copytree(root / 'checkpoint-save', root / 'save')
                if media_class == 'adf-original':
                    shutil.copyfile(root / 'checkpoint-original.adf', content)
                consumed = run(command + ['--ss1-consume', machine], env)
                assert store_bytes(root / 'save') == published, f'{media_class}/{machine}: publication differs after load'
                if original is not None: assert content.read_bytes() == original
                results.append({'class': media_class, 'machine': machine,
                                'producer': produced, 'consumer': consumed})
                print(consumed, flush=True)
    (args.output / 'matrix.json').write_text(json.dumps(results, indent=2) + '\n')
    print(f'SS1 fresh-process matrix: {len(results)} admitted profile/media pairs passed')


if __name__ == '__main__':
    main()
