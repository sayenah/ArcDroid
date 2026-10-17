#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Missing source licenses and untracked sidecars cannot pass the guard."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("guard", ROOT / "tools/check-licenses.py")
GUARD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GUARD)
os.environ.pop("GIT_INDEX_FILE", None)


class LicenseTest(unittest.TestCase):
    def test_coverage(self):
        with tempfile.TemporaryDirectory(prefix="arcdroid-license-") as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            path = root / "example.c"
            path.write_text("int example;\n")
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            self.assertEqual(GUARD.verify(root), ["example.c"])
            sidecar = root / "example.c.license"
            sidecar.write_text("SPDX-License-Identifier: GPL-2.0-only\n")
            self.assertEqual(GUARD.verify(root), ["example.c"])
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            self.assertEqual(GUARD.verify(root), [])
            sidecar.write_text("no license statement\n")
            self.assertIn("example.c", GUARD.verify(root))
            path.write_text("/* SPDX-License-Identifier: GPL-2.0-only */\nint example;\n")
            sidecar.write_text("SPDX-License-Identifier: GPL-2.0-only\n")
            self.assertEqual(GUARD.verify(root), [])


if __name__ == "__main__":
    unittest.main()
