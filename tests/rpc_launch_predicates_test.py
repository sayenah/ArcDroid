#!/usr/bin/env python3
"""Launch audit checkout conversion and content-change controls. GPL-2.0-only."""
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import rpc_launch_predicates as audit

ROOT = Path(__file__).resolve().parents[1]


class CheckoutPins(unittest.TestCase):
    def test_checkout_line_endings(self):
        original = json.loads((ROOT/'tests/rpc_launch_predicates.json').read_text())
        real_open = io.open

        def windows_open(file, mode='r', buffering=-1, encoding=None, *args, **kwargs):
            if 'b' not in mode and encoding in (None, 'locale'):
                encoding = 'cp1252'
            return real_open(file, mode, buffering, encoding, *args, **kwargs)

        with tempfile.TemporaryDirectory(prefix='arcdroid-r3c3b-pins-') as temp:
            root = Path(temp)
            (root/'tests').mkdir()
            (root/'tests/rpc_launch_predicates.json').write_text(json.dumps(original))
            for ending in (b'\n', b'\r\n'):
                with self.subTest(ending=ending):
                    for name in original['sources']:
                        target = root/name
                        target.parent.mkdir(parents=True, exist_ok=True)
                        data = (ROOT/name).read_bytes().replace(b'\r\n', b'\n')
                        target.write_bytes(data.replace(b'\n', ending))
                    output = root/'written.json'
                    # Windows' locale must not shift character offsets in UTF-8
                    # comments once the CRLF-independent byte hash has passed.
                    with mock.patch('io.open', side_effect=windows_open):
                        self.assertEqual(audit.checked_inventory(root), original['predicates'])
                        audit.write_inventory(root, output, original['predicates'])
                    written = json.loads(output.read_text())
                    self.assertEqual(written['sources'], original['sources'])
                    self.assertEqual(written['predicates'], original['predicates'])
                    # A whitespace-only edit leaves every predicate offset intact;
                    # the source hash must still refuse it, on either checkout.
                    source = root/next(iter(original['sources']))
                    source.write_bytes(source.read_bytes() + b' ')
                    with self.assertRaisesRegex(RuntimeError, 'refresh mutation audit after source change:'):
                        audit.checked_inventory(root)
                    audit.write_inventory(root, output, original['predicates'])
                    self.assertNotEqual(json.loads(output.read_text())['sources'], original['sources'])

    def test_standalone_cr_is_content(self):
        with tempfile.TemporaryDirectory(prefix='arcdroid-r3c3b-pins-') as temp:
            source = Path(temp)/'source.c'
            source.write_bytes(b'one\ntwo\n')
            digest = audit.source_hash(source)
            source.write_bytes(b'one\rtwo\n')
            self.assertNotEqual(audit.source_hash(source), digest)


if __name__ == '__main__':
    unittest.main()
