#!/usr/bin/env python3
"""Compare all guest event fields with zero and sub-floor left-stick noise.

The scenarios run in fresh processes/directories so persistent CMOS and adapter
video-rate state cannot change the starting machine between comparisons.
Project-authored synthetic fixtures only. GPL-2.0-only.
"""
import difflib
from pathlib import Path
import subprocess
import sys
import tempfile

harness, core, fixtures = map(Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="arcdroid-input-equivalence-") as root:
    traces = []
    for name, mode in (("zero", "--input-trace"), ("noise", "--input-noise-trace")):
        directory = Path(root) / name
        directory.mkdir()
        trace = directory / "input.trace"
        subprocess.run([harness.as_posix(), core.as_posix(), (fixtures / "arcdroid-test.rom").as_posix(),
                        (fixtures / "arcdroid-test.adf").as_posix(), directory.as_posix(),
                        mode, trace.as_posix()], check=True)
        traces.append(trace.read_text())
    if traces[0] != traces[1]:
        sys.stderr.writelines(difflib.unified_diff(traces[0].splitlines(True),
                                               traces[1].splitlines(True),
                                               fromfile="zero", tofile="noise"))
        raise SystemExit("input equivalence failed")
print("input-equivalence: identical events, timestamps, observers and A3010 samples")
