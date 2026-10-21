#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Authored measurement/plan transparency, timer and explicit refusals."""
import csv
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import time
root=Path(__file__).resolve().parents[1]
sys.dont_write_bytecode=True
sys.path.insert(0,str(root/'tools'))
from rpc_measure_workloads import KINDS,generate
from rpc_hot_workload import generate as hot_generate
runner,plan_core,measurement=sys.argv[1:4]

def run(core,plan,extra=()):
    start=time.perf_counter_ns()
    result=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(plan.parent),'--save',str(plan.parent),'--frames','3',*extra],capture_output=True,text=True,timeout=90)
    result.wall_ns=time.perf_counter_ns()-start
    return result

with tempfile.TemporaryDirectory(prefix='arcdroid-1a0-core-') as tmp:
    directory=Path(tmp)
    hot_generate(directory/'hot')
    for kind in KINDS:generate(directory,kind)
    plans=[directory/'hot/hot.arcplan',*[directory/(k+'.arcplan') for k in KINDS]]
    for index,plan in enumerate(plans):
        timing=directory/(str(index)+'.csv')
        a=run(plan_core,plan);b=run(measurement,plan,('--timing',str(timing)))
        if a.returncode or b.returncode or a.stdout!=b.stdout:raise RuntimeError(('transparency',plan.stem,a.returncode,b.returncode,a.stderr,b.stderr))
        rows=list(csv.DictReader(timing.open()))
        times=[int(r['core_ns']) for r in rows]
        if len(rows)!=3 or any(t<1_000 for t in times) or sum(times)>b.wall_ns+100_000_000:
            raise RuntimeError(('frame timer absent/implausible',times,b.wall_ns))
    hot=plans[0]
    for bad in ('-1:2:0','1:-1:0','18446744073709551616:2:0','1:18446744073709551616:0','1:2:2','1::2:0','1:2:0x','0:2:0'):
        for option in ('--endpoint-instructions','--intrusive-instructions'):
            r=run(measurement,hot,(option,bad))
            if r.returncode!=2 or 'rpc_runner --core LIB --plan FILE' not in r.stderr:raise RuntimeError(('invalid window accepted',bad,r.returncode))
    for args,facility in ((('--s1-off',),'S1 force-off'),(('--ranking',),'ranking counters'),(('--exercise',),'exercise counters'),(('--intrusive-instructions','1:2:0'),'intrusive measurement'),
                          (('--endpoint-instructions','1:2:0'),'endpoint measurement'),(('--fail-alloc','1'),'allocation fault injection'),
                          (('--fault-at','0'),'entry fault injection')):
        r=run(measurement,hot,args)
        if not r.returncode or facility not in r.stderr:raise RuntimeError(('refusal',args,r.returncode,r.stderr))
    for key,value,facility in (('observation_quota','7','quota clamp'),('observation_trace','trace.jsonl','observer trace')):
        refused=hot.with_name('refused.arcplan');refused.write_text(hot.read_text()+f'{key}={value}\n')
        r=run(measurement,refused)
        if not r.returncode or facility not in r.stderr:raise RuntimeError(('plan refusal',key,r.returncode,r.stderr))
print('measurement: hot + four synthetics stdout/counts equal plan; core_ns present; nine unsupported requests refused')
