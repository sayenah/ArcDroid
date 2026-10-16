#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""The retired source, selection API and hashed identifiers stay rejected."""
from contextlib import redirect_stderr
import hashlib
import importlib.util
from io import StringIO
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("retirement", ROOT / "tests/check_input_source_retirement.py")
GUARD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GUARD)


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
                              "MAPPING_SOURCE_PRESET"):
                    (root / name).write_text(token + "\n")
                    self.assertEqual(run(), 1)
                (root / name).write_text("/* generic authored source */\n")
            self.assertEqual(run(), 0)

    def test_hashed_identifier_mutants(self):
        # The persistent fixture uses only a generic identifier and its digest.
        candidate = "_".join(("authored", "retired", "example"))
        digest = hashlib.sha256(candidate.casefold().encode()).hexdigest()
        with patch.object(GUARD, "RETIRED_NAME_SHA256", digest):
            self.assertFalse(GUARD.contains_retired_name("int ordinary_identifier;"))
            self.assertFalse(GUARD.contains_retired_name(candidate + "_suffix"))
            self.assertFalse(GUARD.contains_retired_name("prefix_" + candidate))
            with tempfile.TemporaryDirectory(prefix="arcdroid-hashed-retirement-") as directory:
                root = Path(directory)
                names = ("core/input_mapping.c", "core/arcdroid_core.h", "libretro/arcdroid_libretro.c")
                for name in names:
                    path = root / name
                    path.parent.mkdir(exist_ok=True)
                    path.write_text("/* generic authored source */\n")
                for name in names:
                    for token in (candidate, candidate.upper(), candidate.title()):
                        source = "const char *selection = \"" + token + "\";\n"
                        (root / name).write_text(source)
                        diagnostic = StringIO()
                        with redirect_stderr(diagnostic), self.assertRaises(SystemExit) as exit:
                            GUARD.main(root)
                        self.assertEqual(exit.exception.code, 1)
                        self.assertIn("retired selection name", diagnostic.getvalue())
                        self.assertNotIn(token, diagnostic.getvalue())
                    (root / name).write_text("/* generic authored source */\n")


if __name__ == "__main__":
    unittest.main()
