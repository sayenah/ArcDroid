#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compare actual core raster output to the analytic committed D1 golden."""
from pathlib import Path
import subprocess
import sys
r=subprocess.run([sys.argv[1],sys.argv[2],sys.argv[5] if len(sys.argv)>5 else '--guest',sys.argv[3]],capture_output=True,text=True)
if r.returncode:
    sys.stderr.write(r.stdout+r.stderr)
    raise SystemExit(f'raster guest exited {r.returncode}')
expected=Path(sys.argv[4]).read_text()
if r.stdout!=expected:
    import difflib
    sys.stderr.write(''.join(difflib.unified_diff(expected.splitlines(True),r.stdout.splitlines(True),fromfile='declared golden',tofile='core output')))
    raise SystemExit(1)
print('rpc-video trace matches independently computed golden')
