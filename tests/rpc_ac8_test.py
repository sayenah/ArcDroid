#!/usr/bin/env python3
"""Check the declared workload runs unchanged, not its device budget. GPL-2.0-only."""
import csv
from pathlib import Path
import re
import subprocess
import sys
import tempfile
runner,core,directory=sys.argv[1:4]
with tempfile.TemporaryDirectory(prefix='arc-ac8-timing-') as temp:
    timing=Path(temp)/'frames.csv'
    r=subprocess.run([runner,'--core',core,'--plan',str(Path(directory)/'ac8.arcplan'),
        '--system',directory,'--frames','3','--timing',str(timing),'--ram-offset','131072',
        '--ram-words','3'],capture_output=True,text=True)
    assert r.returncode==0,r.stderr+r.stdout
    rows=list(csv.DictReader(timing.open()))
    assert len(rows)==3 and all(int(row['core_ns'])>0 and int(row['T_ns'])==16666667 for row in rows)
    assert r.stdout.count('rate=100')==3 and r.stdout.count('video=640x480')==3
    observed=[[int(w,16) for w in line.split(',')] for line in re.findall(r'^ram=(.*)$',r.stdout,re.M)]
    assert all(row[0]==1 for row in observed),observed  # denied STRT recovered
    assert observed[1][1]>observed[0][1] and observed[2][1]>observed[1][1]  # live ALU/load/store/branch
    assert [row[2] for row in observed]==[481,961,1441],observed  # one update per active line
    assert 'p50=' in r.stderr and 'p95=' in r.stderr and 'p99=' in r.stderr and 'p99.9=' in r.stderr
    a=re.findall(r'frames=(\d+) generated=(\d+) accepted=(\d+) discarded=(\d+) queued=(\d+) dma=(\d+)/(\d+)',r.stdout)
    assert len(a)==3 and all(x[0]==x[1]==x[2] and x[3:5]==('0','0') and int(x[5])>0 for x in a)
print('AC-8: 100 MIPS, mixed MMU/permission workload, 60 Hz line palette, stereo DMA and engine-only timing passed; no host budget qualification')
