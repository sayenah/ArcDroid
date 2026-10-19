#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run real inventory/schema checks with Windows checkout and locale semantics."""
import builtins
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import rpc_lockstep_compare as compare
import rpc_lockstep_codegen as codegen
import rpc_lockstep_inventory as inventory
from rpc_lockstep_host_inventory import SOURCES, STATIC_SOURCES


def main():
    real_open = io.open

    def windows_open(file, mode='r', buffering=-1, encoding=None, *args, **kwargs):
        if 'b' not in mode and encoding in (None, 'locale'):
            encoding = 'cp1252'
        return real_open(file, mode, buffering, encoding, *args, **kwargs)

    with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-encoding-',
            dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
        root = Path(tmp)
        (root/'tests').mkdir()
        paths = [root/'tests'/name for name in
                 ('rpc_lockstep_schema.json', 'rpc_lockstep_reviewed_policy.json')]
        for path in paths:
            raw = (ROOT/'tests'/path.name).read_bytes().replace(b'\r\n', b'\n')
            path.write_bytes(raw.replace(b'\n', b'\r\n'))
        for name in (*SOURCES,*STATIC_SOURCES):
            target=root/name;target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes((ROOT/name).read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
        with patch('io.open', side_effect=windows_open), \
             patch.object(builtins, 'open', side_effect=windows_open), \
             patch.object(inventory, 'ROOT', root):
            # Show that the simulated default actually corrupts this checkout.
            with io.open(paths[0]) as file:
                wrong = json.load(file)
            schema, digest = compare.load_schema(paths[0])
            assert wrong['inventory'] != schema['inventory'], 'locale control ineffective'
            assert codegen.load(*paths) == schema
            assert digest == compare.load_schema()[1], 'CRLF changed wire compatibility'
            inventory.main()  # Same real objects and unknown-symbol controls as CTest.
            # Metadata still needs independent review under the simulated locale.
            mutant = copy.deepcopy(schema)
            mutant['inventory'][0]['citation'] += ' unreviewed'
            paths[0].write_text(json.dumps(mutant, ensure_ascii=False), encoding='utf-8')
            assert json.loads(paths[0].read_bytes()) == mutant, 'text write was not UTF-8'
            try:
                codegen.load(*paths)
            except ValueError:
                pass
            else:
                raise AssertionError('schema metadata pin relaxed')
            try:
                inventory.main()
            except RuntimeError as error:
                assert 'independent review' in str(error)
            else:
                raise AssertionError('inventory metadata pin relaxed')
    print('UTF-8/CRLF inventory and schema under CP1252: PASS; unreviewed changes rejected')


if __name__ == '__main__':
    main()
