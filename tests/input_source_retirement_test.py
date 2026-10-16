#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""The retired source and selection API remain rejected with generic fixtures."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RetirementTest(unittest.TestCase):
    def test_retired_source_and_tokens(self):
        with tempfile.TemporaryDirectory(prefix="arcdroid-retirement-") as directory:
            root = Path(directory)
            names = ("core/input_mapping.c", "core/arcdroid_core.h", "libretro/arcdroid_libretro.c")
            for name in names:
                path = root / name
                path.parent.mkdir(exist_ok=True)
                path.write_text("/* generic authored source */\n")

            def run():
                return subprocess.run([sys.executable, str(ROOT / "tests/check_input_source_retirement.py"),
                                       str(root)], capture_output=True).returncode

            self.assertEqual(run(), 0)
            retired = root / "core/input_profiles.c"
            retired.write_text("/* retired source mutant */\n")
            self.assertEqual(run(), 1)
            retired.unlink()
            for name in names:
                for token in ("input_profile", "InputProfile", "MAPPING_SOURCE_PROFILE",
                              "MAPPING_SOURCE_PRESET", "retired_profile_example"):
                    (root / name).write_text(token + "\n")
                    self.assertEqual(run(), 1)
                (root / name).write_text("/* generic authored source */\n")
            self.assertEqual(run(), 0)


if __name__ == "__main__":
    unittest.main()
