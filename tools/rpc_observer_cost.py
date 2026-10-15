#!/usr/bin/env python3
"""Paired authored no-watch/live-watch interpreter cost. GPL-2.0-only.
Timing evidence is external to CTest: host load must not become a CI oracle.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess
from rpc_hot_workload import generate

parser=argparse.ArgumentParser()
parser.add_argument('--runner',required=True)
parser.add_argument('--core',required=True)
parser.add_argument('--output',required=True,type=Path)
parser.add_argument('--frames',type=int,default=480)
parser.add_argument('--warmup-frames',type=int,default=60)
parser.add_argument('--alternations',type=int,default=3)
parser.add_argument('--max-overhead-percent',type=float,default=3.0)
parser.add_argument('--workload',choices=['all','hot','held','swi-dense'],default='all')
a=parser.parse_args()
if not 2<=a.frames<=590 or not 0<=a.warmup_frames<a.frames-1 or not 1<=a.alternations<=10:
    parser.error('2..590 frames, a shorter warmup, and 1..10 alternations required (live watch limit is one billion attempts)')
a.output.mkdir(parents=True,exist_ok=True)
normal=a.output/'normal';held=a.output/'held';dense=a.output/'swi-dense'
generate(normal);generate(held,True);generate(dense,swi_dense=True)
plans={}
for name,folder,cells,workload in [('off',normal,0,'hot'),('no-watch',normal,252,'hot'),('held-off',held,0,'held'),('held',held,252,'held'),('swi-off',dense,0,'swi-dense'),('swi-no-watch',dense,252,'swi-dense')]:
    if a.workload not in ('all',workload):continue
    path=folder/(name+'.arcplan')
    path.write_text((folder/'hot.arcplan').read_text()+(f'observer_cells={cells}\nobserver_forms=16392\nobserver_error_numbers=ordinary\n' if cells else ''))
    plans[name]=(path.resolve(),folder.resolve())
results=[];samples={name:[] for name in plans}
for iteration in range(a.alternations):
    order=list(plans) if iteration%2==0 else list(reversed(plans))
    for name in order:
        plan,system=plans[name];stem=a.output/f'{iteration}-{name}'
        with stem.with_suffix('.trace').open('w') as out,stem.with_suffix('.log').open('w') as err:
            subprocess.run([a.runner,'--core',a.core,'--plan',str(plan),'--system',str(system),
                '--pacing','flat','--frames',str(a.frames),'--timing',str(stem.with_suffix('.csv'))],
                stdout=out,stderr=err,check=True,timeout=180)
        log=stem.with_suffix('.log').read_text()
        if name in ('no-watch','swi-no-watch','held'):assert 'TEST OVERRIDE' in log,'observer override not applied'
        if name=='held':
            assert 'recognised-open=1' in log and 'modal-unknown=1' not in log,'watch not held throughout measurement'
        elif name in ('no-watch','swi-no-watch'):assert 'recognised-open=1' not in log,'unexpected watch'
        rows=list(csv.DictReader(stem.with_suffix('.csv').open()))[a.warmup_frames:]
        times=[int(r['core_ns']) for r in rows]
        for r in rows:
            if not name.startswith('swi-'):assert abs(10*int(r['guest_instructions'])-int(r['guest_ns']))<=9,'guest progress drift'
            else:assert int(r['guest_instructions'])>0,'SWI guest made no progress'
        p50=statistics.median(times);p95=sorted(times)[math.ceil(.95*len(times))-1]
        results.append(dict(iteration=iteration,name=name,p50_ns=p50,p95_ns=p95))
        samples[name].extend(times)
        print(f'{iteration} {name}: p50={p50/1e6:.4f} ms p95={p95/1e6:.4f} ms',flush=True)
    for observed,baseline in [('no-watch','off'),('held','held-off'),('swi-no-watch','swi-off')]:
        if observed not in plans:continue
        assert (a.output/f'{iteration}-{observed}.trace').read_bytes()==(a.output/f'{iteration}-{baseline}.trace').read_bytes(),'observer changed the measured guest projection'
summary={}
for name,times in samples.items():summary[name]={'p50_ns':statistics.median(times),'p95_ns':sorted(times)[math.ceil(.95*len(times))-1]}
for name,baseline in [('no-watch','off'),('held','held-off'),('swi-no-watch','swi-off')]:
    if name not in plans:continue
    for percentile in ['p50','p95']:
        summary[name][percentile+'_overhead_percent']=100*(summary[name][percentile+'_ns']/summary[baseline][percentile+'_ns']-1)
report={'frames':a.frames,'warmup_frames':a.warmup_frames,'alternations':a.alternations,'workload':a.workload,'swi_density':'one user SWI per 35 executed instructions','cells':252,'runs':results,'summary':summary}
(a.output/'cost.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(summary,indent=2),flush=True)
if any(summary[name][p+'_overhead_percent']>a.max_overhead_percent for name in ('no-watch','swi-no-watch') if name in summary for p in ['p50','p95']):
    raise SystemExit('no-watch overhead exceeds requested bound; see cost.json')
