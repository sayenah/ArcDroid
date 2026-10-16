#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Seed an invented product through the store and check a fresh process."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import stat


def main() -> None:
    harness, core, fixtures, output = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    command = [str(harness), str(core),
               str(fixtures / "arcdroid-outcome-exact.rom"),
               str(fixtures / "hd-family" / "valid-f.adf"), str(root),
               "--real-boot", "--machine", "A3020", "--boot-hard-disc-auto",
               "--boot-frames", "30"]
    first = subprocess.run(command + ["--boot-seed-product"],
                           capture_output=True, text=True)
    assert first.returncode == 0, first.stdout + first.stderr
    second = subprocess.run(command, capture_output=True, text=True)
    assert second.returncode == 0, second.stdout + second.stderr
    log = (root / "evidence" / "real-boot.log").read_text()
    assert "Hard disc scope: state=completed copy=no product=yes" in log
    assert "reason=hd-product" in log
    helpers = list((root / "save" / "arcdroid" / "launch").glob("*.adl"))
    assert len(helpers) == 1, helpers
    assert b"%Filer_Run ADFS::4.$.ArcDroid.!Result" in helpers[0].read_bytes()
    for name, changes, extra in (
        ("launch-off", {}, ["--boot-launch-off"]),
        ("explicit", {}, ["--boot-launch-app1"]),
        ("hd-off", {8: None}, []),
        ("a3010", {7: "A3010"}, []),
        ("a4000", {7: "A4000"}, []),
    ):
        selected = [changes.get(index, value) for index, value in
                    enumerate(command) if changes.get(index, value) is not None]
        result = subprocess.run(selected + extra, capture_output=True, text=True)
        assert result.returncode == 0, (name, result.stdout + result.stderr)
        negative = (root / "evidence" / "real-boot.log").read_text()
        assert "reason=hd-product" not in negative, name
    store = next((root / "save" / "arcdroid" / "hd").iterdir())
    original_mode = stat.S_IMODE(store.stat().st_mode)
    try:
        os.chmod(store, original_mode & ~0o222)
        read_only = subprocess.run(command, capture_output=True, text=True)
        assert read_only.returncode == 0, read_only.stdout + read_only.stderr
        read_only_log = (root / "evidence" / "real-boot.log").read_text()
        assert "mode=read-only reason=read-only:write-probe" in read_only_log
        assert "reason=hd-product" in read_only_log
    finally:
        os.chmod(store, original_mode)


if __name__ == "__main__":
    main()
