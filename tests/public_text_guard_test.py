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
    homes = ["/" + "Users/" + "example", "/" + "Users/" + "another_example",
             "/" + "Users/" + "Example User"]
    windows = ["C:" + "\\Users", "C:" + "\\Users\\" + "example",
               "c:" + "\\\\users\\\\" + "example", "D:" + "/Users/" + "example"]
    volumes = [prefix + "ABCD" + "-" + "1234"
               for prefix in ("/storage/", "/mnt/media_rw/")]
    return {
        "private-ipv4": [address + ending for address in private for ending in ("", ".", ".md")],
        "mac-user-path": [home + ending for home in homes
                          for ending in ("", "/fixture", ".", " next")],
        "windows-user-path": [home + ending for home in windows
                              for ending in ("", "\\fixture", ".", " next")],
        "android-volume": [volume + ending for volume in volumes
                           for ending in ("", "/fixture", ".", " next")],
    }


class GuardTest(unittest.TestCase):
    def test_mac_paths_are_case_sensitive_and_urls_are_scoped(self):
        local = "/" + "Users/" + "example"
        for spelling in ("users", "USERS", "uSeRs"):
            self.assertEqual(GUARD.scan("example", "/" + spelling + "/example", []), [])
        for scheme in ("https", "http", "HTTPS", "example+docs"):
            for segment in ("users", "Users"):
                url = scheme + "://docs.example.test/support/" + segment + "/example/chapter.htm"
                for citation in (url, "(" + url + ")", '"' + url + '"', "`" + url + "`"):
                    for ending in ("\n", "\r\n"):
                        with self.subTest(scheme=scheme, segment=segment, citation=citation):
                            self.assertEqual(GUARD.scan("docs/example.md", citation + ending, []), [])
                for text in (url + " " + local, local + " " + url,
                             "(" + url + ")" + local, "`" + url + "`" + local):
                    self.assertEqual([hit[2] for hit in GUARD.scan("docs/example.md", text, [])],
                                     ["mac-user-path"])
        for prefix in ("file://", "https://", "https:", "//docs.example.test"):
            self.assertIn("mac-user-path", [hit[2] for hit in GUARD.scan("example", prefix + local, [])])
        for prefix in ("topic_(example)", "topic_(nested_(example))", "topic)"):
            url = "https://docs.example.test/" + prefix + local
            self.assertEqual(GUARD.scan("example", url, []), [])
            if "nested" in prefix:
                self.assertEqual(GUARD.scan("example", "(" + url + ")", []), [])
                self.assertIn("mac-user-path", [hit[2] for hit in
                              GUARD.scan("example", "(" + url + ")" + local, [])])
        # The URL exception applies only to the macOS class.
        for kind in ("private-ipv4", "windows-user-path", "android-volume"):
            text = "https://docs.example.test/" + positives()[kind][0]
            self.assertIn(kind, [hit[2] for hit in GUARD.scan("example", text, [])])

    def test_cli_accepts_documentation_urls(self):
        with tempfile.TemporaryDirectory(prefix="arcdroid-public-urls-") as directory:
            root = Path(directory)
            (root / "tools").mkdir()
            (root / "tools/public-text-allowlist.json").write_text('{"entries": []}\n')
            target = root / "example.md"
            urls = ["https://docs.example.test/support/" + segment + "/example/chapter.htm"
                    for segment in ("users", "Users")]
            target.write_text("\n".join(urls) + "\n")
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            command = [sys.executable, str(ROOT / "tools/check-public-text.py"), "--root", str(root)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            target.write_text("\n".join(urls) + "\n" + "/" + "Users/" + "example")
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("mac-user-path", result.stderr)

    def test_classes_and_boundaries(self):
        for kind, examples in positives().items():
            for example in examples:
                for ending in ("\n", "\r\n"):
                    for path in ("docs/example.md", "tools/example.py"):
                        with self.subTest(kind=kind, path=path):
                            hits = GUARD.scan(path, "prefix " + example + ending, [])
                            self.assertIn(kind, [item[2] for item in hits])
                # Also exercise a bare shape at the exact end of text.
                self.assertIn(kind, [item[2] for item in GUARD.scan(path, example, [])])
        for safe in ("192.0.2.1", "198.51.100.2", "203.0.113.3", "127.0.0.1",
                     "/Users/<name>/", "C:" + "\\example\\", "/storage/emulated/0/",
                     "172.15.0.1", "172.32.0.1", "192.169.0.1", "110.0.0.1",
                     ".".join(map(str, (10, 0, 0, 999))),
                     ".".join(map(str, (10, 0, 0, 1, 2))),
                     ".".join(map(str, (1, 10, 0, 0, 1))),
                     "C:" + "\\UsersExtra", "/storage/" + "ABCD" + "-" + "12345",
                     "/mnt/media_rw/" + "ABCD" + "-" + "1234more"):
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
                for example in examples:
                    with self.subTest(kind=kind):
                        target.write_text("at " + example)
                        result = subprocess.run([sys.executable, str(ROOT / "tools/check-public-text.py"),
                                                 "--root", str(root)], capture_output=True, text=True)
                        self.assertEqual(result.returncode, 1, kind)
                        self.assertIn(kind, result.stderr)
                        self.assertNotIn(example, result.stderr)
            target.write_text("safe\n")
            result = subprocess.run([sys.executable, str(ROOT / "tools/check-public-text.py"),
                                     "--root", str(root)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0)

    def test_cli_ipv4_in_tracked_filenames(self):
        with tempfile.TemporaryDirectory(prefix="arcdroid-public-filenames-") as directory:
            root = Path(directory)
            (root / "tools").mkdir()
            (root / "tools/public-text-allowlist.json").write_text('{"entries": []}\n')
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            examples = [(".".join(map(str, parts)), 1) for parts in
                        ((10, 0, 0, 5), (172, 16, 0, 5), (192, 168, 0, 5))]
            examples += [(".".join(map(str, (10, 0, 0, 1, 2))), 0), ("192.0.2.1", 0)]
            for address, expected in examples:
                name = "example-" + address + ".md"
                path = root / name
                path.write_text("safe\n")
                subprocess.run(["git", "-C", str(root), "add", "--", name], check=True)
                result = subprocess.run([sys.executable, str(ROOT / "tools/check-public-text.py"),
                                         "--root", str(root)], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected)
                if expected:
                    self.assertIn("private-ipv4", result.stderr)
                    self.assertNotIn(address, result.stderr)
                    self.assertNotIn(name, result.stderr)
                subprocess.run(["git", "-C", str(root), "rm", "--cached", "-q", "--", name], check=True)
                path.unlink()


if __name__ == "__main__":
    unittest.main()
