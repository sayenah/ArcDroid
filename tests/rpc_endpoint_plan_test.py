#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Functional endpoint prefix versus an unarmed, quota-one reference.

The diagnostic captures CPU/banks/mode, all RAM/VRAM, exact issue ticks and live
scheduler deadlines at the ordinary interpreter return. It never flushes or
services work to capture. Baseline executes on after capture; endpoint stops.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
runner,core,fixtures=sys.argv[1:4];fixtures=Path(fixtures)

def run(plan,folder,extra=()):
    r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(fixtures),
        '--save',str(folder),'--frames','1',*extra],capture_output=True,text=True,timeout=90)
    if r.returncode:raise RuntimeError((plan.stem,r.returncode,r.stderr))
    return r

def prefix(result):
    m=re.search(r'^prefix (.*)$',result.stderr,re.M)
    if not m:raise RuntimeError('prefix missing')
    return m[1]

count=0
with tempfile.TemporaryDirectory(prefix='arcdroid-1a0-prefix-') as tmp:
    folder=Path(tmp)
    ordinary=[f'{kind}-{model}' for model in range(4) for kind in ('matrix','mixed-av','devices','audio')]
    ordinary+=['factory-2','factory-3','ac8','hot']
    for name in ordinary:
        original=(fixtures/(name+'.arcplan')).read_text();plan=folder/'prefix.arcplan';plan.write_text(original)
        r=run(plan,folder);executed=int(re.search(r'^frame=0 ns=\d+ cpu=(\d+)',r.stdout,re.M)[1])
        last=min(347,max(1,executed//2));first=max(1,last-3)
        a=prefix(run(plan,folder,('--prefix-instructions',str(last),'--single-step-prefix')))
        b=prefix(run(plan,folder,('--prefix-instructions',str(last),'--endpoint-instructions',f'{first}:{last}:0')))
        if a!=b:raise RuntimeError((name,'functional prefix differs',a,b))
        count+=1
    for model in range(4):
        original=(fixtures/f'matrix-{model}.arcplan').read_text()
        for quota in (1,7,200):
            for mode in ('off','unmatched','trace','active'):
                suffix=f'observation_quota={quota}\n'
                if mode=='unmatched':suffix+='observation_profile=unmatched\n'
                if mode=='trace':suffix+='observation_trace=prefix.jsonl\n'
                if mode=='active':suffix+='observer_cells=255\nobserver_forms=0xffffffff\nobserver_launch=Run Unmatched\nobserver_boot=launch\n'
                plan=folder/'observed.arcplan';plan.write_text(original+suffix)
                last=47;args=('--prefix-instructions',str(last))
                a=prefix(run(plan,folder,(*args,'--single-step-prefix')))
                b=prefix(run(plan,folder,(*args,'--endpoint-instructions',f'7:{last}:0')))
                if a!=b:raise RuntimeError((model,quota,mode,'functional prefix differs',a,b))
                count+=1
print(f'endpoint prefixes: {count} armed/unarmed comparisons; plan goldens and 4 models x quotas 1/7/200 x off/unmatched/trace/active')
