#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
import subprocess
import sys
for args,expected in (([],0),(['--mismatch'],1)):
    r=subprocess.run([sys.argv[1],*args],capture_output=True,text=True,timeout=30)
    if r.returncode!=expected or r.stdout or (expected and 'mismatch: issue_ticks' not in r.stderr):
        raise RuntimeError((args,r.returncode,r.stdout,r.stderr))
print('cross-check: success, injected field-named failure, stderr only, reset')
