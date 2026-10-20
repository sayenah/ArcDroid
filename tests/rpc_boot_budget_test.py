#!/usr/bin/env python3
"""Independent VFS budgets on a larger authored package. GPL-2.0-only."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile
from rpc_boot_packages_test import BASE, EXPECTED, IDENTITY, inputs, compare, require


def owned(save):
    root = save / 'arcdroid/hd-riscpc' / IDENTITY
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.iterdir() if p.is_file()}


def authored():
    hard, expected = dict(BASE), dict(EXPECTED)
    # 240 files, 8 directories; many partial sectors, 1 MiB members and
    # directory/chunk boundaries. About 64 MiB expanded; no third-party bytes.
    for i in range(240):
        size = (1, 16383, 65537, 262145, 1048576)[i % 5]
        pattern = hashlib.sha256(f'ArcDroid-authored-{i}'.encode()).digest()
        data = (pattern * ((size + 31) // 32))[:size]
        name = f'Big/G{i // 30}/F{i:03d}'
        metadata = data, 0xffffff00, i, 3
        hard['HardDisc4/' + name] = metadata
        expected['$.' + name.replace('/', '.')] = metadata
    return hard, expected


def cases(root, profile):
    hard, expected = authored()
    system = inputs(root, hard=hard)
    reference = None
    # The sanitizer runs the whole large workload at one quantum; ordinary
    # builds additionally compare every owned file at quanta 1, 8 and 32.
    for quantum in ((8,) if profile == 'sanitizer' else (1, 8, 32)):
        save = root / f'save-{quantum}'
        save.mkdir()
        result = subprocess.run([sys.argv[1], str(system), str(save), str(quantum),
                                 'budgets', '0', '0'], capture_output=True,
                                # W10b C: sanitizer 240.84 s censored at 240;
                                # twice that observation, rounded to a minute.
                                text=True, timeout=540 if profile == 'sanitizer' else 240)
        require(result.returncode == 0, result.stdout + result.stderr)
        stats = next(json.loads(line) for line in result.stdout.splitlines()
                     if line.startswith('{'))
        require(stats['phase'] == 14 and stats['files'] == len(expected),
                'large package did not finish')
        compare(save, expected)
        digest = owned(save)
        if reference is None:
            reference = digest
        else:
            require(digest == reference, 'owned bytes differ with step quantum')
        print(f'quantum={quantum} files={stats["files"]} bytes={stats["bytes"]} ' +
              next(line for line in result.stdout.splitlines()
                   if line.startswith('vfs-max-')), flush=True)
    print('rpc-boot-budget: independent VFS limits, exact tree and owned bytes pass', flush=True)


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='arcdroid-boot-budget-') as temp:
        cases(Path(temp), sys.argv[2] if len(sys.argv) > 2 else 'full')
