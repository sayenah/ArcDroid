#!/usr/bin/env python3
"""Deterministic authored H-3 corpus, generated only in the build tree. GPL-2.0-only."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from rpc_lockstep_rom import build, directed, write_item
from rpc_lockstep_expect import FEATURES


def generate(directory, full=False):
    directory.mkdir(parents=True, exist_ok=True)
    (directory/'authored.seed').write_bytes(bytes(256))
    items = directed(directory)
    for line in Path(__file__).with_name('rpc_lockstep_seeds.txt').read_text().splitlines():
        if not line or line.startswith('#'): continue
        subset, value = line.split()
        if subset != 'ci' and not full: continue
        seed = int(value, 0)
        for model in range(4):
            words, expected, markers = build(model, seed)
            name = 'fuzz-%016x-%d' % (seed, model)
            write_item(directory, name, model, words)
            items.append(dict(name=name,model=model,seed=seed,expected=expected,markers=markers,fatal=False,budget=200000,terminal_cpsr=0xc3 if 'masked-refresh' in markers else None))
    hashes = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(directory.iterdir()) if p.suffix in ('.rom','.arcplan','.seed')}
    manifest = dict(license='GPL-2.0-only',version=0,items=items,hashes=hashes,features=FEATURES)
    (directory/'manifest.json').write_bytes((json.dumps(manifest, sort_keys=True, indent=2)+'\n').encode('utf-8'))
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('directory', type=Path); parser.add_argument('--full', action='store_true')
    args = parser.parse_args(); generate(args.directory,args.full)
