#!/usr/bin/env python3
"""Compare authored component observations to independently stated goldens."""
from pathlib import Path
import subprocess
import sys
import difflib

expected = Path(sys.argv[2]).read_text()
result = subprocess.run([sys.argv[1]], text=True, capture_output=True)
if result.returncode or result.stdout != expected:
    sys.stderr.write(result.stderr)
    sys.stderr.writelines(difflib.unified_diff(expected.splitlines(True),
        result.stdout.splitlines(True), fromfile="authored expectation", tofile="engine observation"))
    raise SystemExit(1)
print("rpc canonical component trace matches authored expectations")
