#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Authored positive/negative shapes and CLI failures; no private values."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("guard", ROOT / "tools/check-public-text.py")
GUARD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GUARD)
# Synthetic repositories must never reuse a caller's alternate review index.
os.environ.pop("GIT_INDEX_FILE", None)


def positives():
    # Construct RFC 1918 shapes at run time; none is a real endpoint.
    private = [".".join(map(str, parts)) for parts in
               [(10, 0, 0, 1), (10, 255, 255, 255), (172, 16, 0, 1),
                (172, 31, 255, 255), (192, 168, 0, 1)]]
    return {
        "private-ipv4": private,
        "mac-user-path": ["/" + "Users/" + "example/fixture", "/" + "users/example/",
                          "/" + "Users/" + "Example User/fixture"],
        "windows-user-path": ["C:" + "\\Users\\" + "example\\fixture",
                              "c:" + "\\\\users\\\\" + "example", "D:" + "/Users/" + "example"],
        "android-volume": ["/storage/" + "ABCD" + "-" + "1234/fixture"],
    }


class GuardTest(unittest.TestCase):
    def test_classes_and_boundaries(self):
        for kind, examples in positives().items():
            for example in examples:
                for ending in ("\n", "\r\n"):
                    for path in ("docs/example.md", "tools/example.py"):
                        with self.subTest(kind=kind, path=path):
                            hits = GUARD.scan(path, "prefix " + example + ending, [])
                            self.assertIn(kind, [item[2] for item in hits])
        for safe in ("192.0.2.1", "198.51.100.2", "203.0.113.3", "127.0.0.1",
                     "/Users/<name>/", "C:" + "\\example\\", "/storage/emulated/0/",
                     "172.15.0.1", "172.32.0.1", "192.169.0.1", "110.0.0.1",
                     "10.0.0.999", "10.0.0.1.2"):
            self.assertEqual(GUARD.scan("example", safe, []), [])

    def test_reviewed_allowlist_is_scoped_to_the_match(self):
        entries = json.loads((ROOT / "tools/public-text-allowlist.json").read_text())["entries"]
        for kind, examples in positives().items():
            for example in examples:
                for name in ("docs/handoff.md", "docs/example.md",
                             "tools/public-text-allowlist.json"):
                    self.assertIn(kind, [x[2] for x in GUARD.scan(name, example, entries)])
        generic = "`" + "C:" + "\\Users\\" + "`"
        self.assertEqual(GUARD.scan("docs/handoff.md", generic, entries), [])
        self.assertTrue(GUARD.scan("docs/example.md", generic, entries))
        injected = generic + " " + positives()["windows-user-path"][0]
        hits = GUARD.scan("docs/handoff.md", injected, entries)
        self.assertEqual([hit[2] for hit in hits], ["windows-user-path"])

    def test_cli_rejects_tracked_examples_and_redacts_values(self):
        with tempfile.TemporaryDirectory(prefix="arcdroid-public-text-") as directory:
            root = Path(directory)
            (root / "tools").mkdir()
            (root / "tools/public-text-allowlist.json").write_text('{"entries": []}\n')
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            target = root / "example.txt"
            target.write_text("safe\n")
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            for kind, examples in positives().items():
                target.write_text(examples[0] + "\n")
                result = subprocess.run([sys.executable, str(ROOT / "tools/check-public-text.py"),
                                         "--root", str(root)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 1, kind)
                self.assertIn(kind, result.stderr)
                self.assertNotIn(examples[0], result.stderr)
            target.write_text("safe\n")
            result = subprocess.run([sys.executable, str(ROOT / "tools/check-public-text.py"),
                                     "--root", str(root)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
