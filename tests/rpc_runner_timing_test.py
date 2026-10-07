#!/usr/bin/env python3
"""Frontend wall-window qualification independent of guest throughput. GPL-2.0-only."""
import csv
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

runner,core,directory=sys.argv[1:4]
directory=Path(directory).resolve()
with tempfile.TemporaryDirectory(prefix='arc-runner-window-') as temp:
    temp=Path(temp)
    # A separate ARM610 plan keeps this frontend test inexpensive. AC-8's
    # SA-110/100 MIPS plan and guest are unchanged and separately asserted.
    plan=temp/'window.arcplan'
    plan.write_text((directory/'audio-0.arcplan').read_text()+'rate_mips=1\n')
    timing=temp/'window.csv'
    started=time.monotonic()
    r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(directory),
        '--warmup-seconds','1','--measure-seconds','1','--pacing','realtime',
        '--timing',str(timing)],capture_output=True,text=True)
    elapsed=time.monotonic()-started
    assert r.returncode==0,r.stderr
    assert elapsed>=2,'runner ended before its wall-clock measurement window'
    rows=list(csv.DictReader(timing.open()))
    levels=[int(row['measured']) for row in rows]
    assert 0 in levels and 1 in levels and levels==sorted(levels),'warm-up exclusion/window ordering'
    assert all(int(row['core_ns'])>0 and int(row['T_ns'])>0 for row in rows)
    count=re.search(r'timing frames=(\d+)',r.stderr)
    assert count and int(count[1])==sum(levels),'summary included excluded warm-up frames'
print('runner: wall-clock warm-up and measured window, CSV exclusion and percentile sample counts passed')
