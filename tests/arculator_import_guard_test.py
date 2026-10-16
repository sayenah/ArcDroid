#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Mutate offline import notices, provenance, binary data and tree membership."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("guard", ROOT / "tools/check-arculator-import.py")
GUARD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GUARD)
# Every synthetic repository owns its own index.
os.environ.pop("GIT_INDEX_FILE", None)


class ImportTest(unittest.TestCase):
    def test_notices_and_mutants(self):
        with tempfile.TemporaryDirectory(prefix="arcdroid-import-") as directory:
            root = Path(directory)
            tracked = subprocess.check_output(
                ["git", "ls-files", "-z", "--", "engine/arculator/"], cwd=ROOT)
            for name in filter(None, tracked.decode().split("\0")):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / name, path)
            shutil.copyfile(ROOT / "engine/UPSTREAM.md", root / "engine/UPSTREAM.md")
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "add", "--", "engine"], check=True)
            count, changed = GUARD.verify(root)
            self.assertEqual(count, 62)
            self.assertTrue({"src/ioeb.c", "src/disc.h", "src/wd1770.h"} <= set(changed))
            for name in changed:
                path = root / "engine/arculator" / name
                original = path.read_bytes()
                path.write_bytes(re.sub(rb"/\*.*?\*/|//[^\r\n]*", b"", original, flags=re.S))
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, "missing dated"):
                    GUARD.verify(root)
                path.write_bytes(original)
            for name in ("src/ioeb.h", "cmos/arthur/cmos.bin"):
                path = root / "engine/arculator" / name
                original = path.read_bytes()
                path.write_bytes(original + b"\n/* authored mutant */\n")
                with self.assertRaises(ValueError):
                    GUARD.verify(root)
                path.write_bytes(original)
            # Binary data never gets the text checkout newline exemption.
            path = root / "engine/arculator/cmos/arthur/cmos.bin"
            original = path.read_bytes()
            mutated_binary = original.replace(b"\n", b"\r\n")
            self.assertNotEqual(mutated_binary, original)
            path.write_bytes(mutated_binary)
            with self.assertRaises(ValueError):
                GUARD.verify(root)
            path.write_bytes(original)
            extra = root / "engine/arculator/unlisted"
            extra.write_bytes(b"\0authored binary\1")
            self.assertEqual(GUARD.verify(root), (count, changed))
            (root / ".gitignore").write_text("/engine/arculator/unlisted\n")
            subprocess.run(["git", "-C", str(root), "check-ignore", "-q", "--",
                            "engine/arculator/unlisted"], check=True)
            self.assertEqual(GUARD.verify(root), (count, changed))
            subprocess.run(["git", "-C", str(root), "add", "-f", "--",
                            "engine/arculator/unlisted"], check=True)
            with self.assertRaisesRegex(ValueError, "allowlist"):
                GUARD.verify(root)
            extra.unlink()
            subprocess.run(["git", "-C", str(root), "update-index", "--force-remove", "--",
                            "engine/arculator/unlisted"], check=True)
            missing = root / "engine/arculator/src/ioeb.h"
            original = missing.read_bytes()
            missing.unlink()
            with self.assertRaises(OSError):
                GUARD.verify(root)
            missing.write_bytes(original)
            # Windows checkout conventions must not create spurious changes.
            for path in (root / "engine/arculator/src").iterdir():
                original = path.read_bytes().replace(b"\r\n", b"\n")
                path.write_bytes(original.replace(b"\n", b"\r\n"))
            self.assertEqual(GUARD.verify(root), (count, changed))


if __name__ == "__main__":
    unittest.main()
