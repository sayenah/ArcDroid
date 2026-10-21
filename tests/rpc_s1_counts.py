#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read S1 counts at every G-13 reset in disposable, otherwise unchanged cores.

A read-only bind diagnostic avoids losing counters across golden-client resets.
No instrumentation enters the repository core or any performance measurement.
Every golden still runs its original independent assertions and frozen fixture.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from rpc_measure_workloads import generate
import tempfile
sys.dont_write_bytecode=True
from rpc_ninja_commands import rebuild_commands

p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);a=p.parse_args()
build=a.build.resolve();root=Path(__file__).resolve().parents[1]
cache=(build/'CMakeCache.txt').read_text()
ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
tests=json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'--show-only=json-v1'],text=True))['tests']
selected=[t for t in tests if re.match(r'^rpc-(cpu-trace|cmos-trace|video-(paths|trace)-[0-3]|d2-guest-[0-3]|factory-capture-[23]|plan-(matrix-[0-3]|av-[0-3]|devices))$',t['name'])]
if len(selected)!=25:raise RuntimeError('golden census drift')
source=(root/'engine/rpc/rpc_mem_timing.c').read_text()
anchor='void arc_rpc_mem_bind(ArcRpcScheduler *clock, unsigned model, unsigned rate)\n{'
if source.count(anchor)!=1:raise RuntimeError('bind anchor drift')
logger='''
    /* Disposable evidence only: read counters before the mandated G-13 reset. */
    if(m.clock){
        const char *file=getenv("ARC_RPC_S1_COUNTS");
        if(!file)abort();
        FILE *out=fopen(file,"a");if(!out)abort();
        if(fprintf(out,"%u %llu %llu %llu\\n",m.model,
            (unsigned long long)arc_rpc_exercise.values[ARC_RPC_COUNT_service_s1_taken],
            (unsigned long long)arc_rpc_exercise.values[ARC_RPC_COUNT_service_s1],
            (unsigned long long)arc_rpc_mem_fast.instructions)<0)abort();
        if(fclose(out))abort();
    }
'''
with tempfile.TemporaryDirectory(prefix='arcdroid-s1b-counts-',dir=build.parent) as tmp:
    folder=Path(tmp);timing=folder/'rpc_mem_timing.c'
    timing.write_text('#include <stdio.h>\n#include <stdlib.h>\n'+source.replace(anchor,anchor+logger))
    replacements={}
    for target in ('arcdroid_libretro_test','arcdroid_rpcplan_libretro'):
        cc,link=rebuild_commands(commands,root/'engine/rpc/rpc_mem_timing.c',target+'_rpc',build,os.name=='nt')
        old=cc[cc.index('-o')+1];obj=folder/(target+'.o');binary=folder/(target+Path(link[link.index('-o')+1]).suffix)
        cc[cc.index('-o')+1]=str(obj);cc[cc.index('-c')+1]=str(timing);cc+=['-I',str(root/'engine/rpc')]
        link=[str(obj) if x==old else x for x in link];original=link[link.index('-o')+1];link[link.index('-o')+1]=str(binary)
        for cmd in (cc,link):
            r=subprocess.run(cmd,cwd=build,capture_output=True,text=True,timeout=120)
            if r.returncode:raise RuntimeError(('diagnostic overlay build',r.stderr[-2000:]))
        replacements[str((build/original).resolve())]=str(binary)
    totals={group:[0]*4 for group in ('goldens','observation')}
    eligible_totals={group:[0]*4 for group in totals}
    instruction_totals={group:[0]*4 for group in totals}
    log=folder/'counts.txt';env=dict(os.environ,ARC_RPC_S1_COUNTS=str(log))
    # Positive control: compare the log with the real API before unloading.
    public=folder/'public';generate(public,'stream-arm710')
    (folder/'control-save').mkdir()
    log.write_text('')
    plan_overlay=next(v for k,v in replacements.items() if Path(k).stem=='arcdroid_rpcplan_libretro')
    control=subprocess.run([str(build/('rpc_runner.exe' if os.name=='nt' else 'rpc_runner')),
        '--core',plan_overlay,'--plan',str(public/'stream-arm710.arcplan'),'--system',str(public),
        '--save',str(folder/'control-save'),'--frames','3','--exercise'],env=env,capture_output=True,text=True,timeout=120)
    value=re.search(r'^exercise service_s1_taken=(\d+)$',control.stderr,re.M)
    logged=sum(int(line.split()[1]) for line in log.read_text().splitlines())
    if control.returncode or not value or int(value[1])<=0 or logged!=int(value[1]):
        raise RuntimeError(('reset logger positive control',logged,value[1] if value else None,control.stderr[-1000:]))
    print('positive control: stream-arm710 taken='+str(logged)+' equals real exercise API',flush=True)
    for test in selected+[next(t for t in tests if t['name']=='rpc-observation-plan')]:
        log.write_text('')
        cmd=[replacements.get(str(Path(x).resolve()),x) for x in test['command']]
        r=subprocess.run(cmd,cwd=build,env=env,capture_output=True,text=True,timeout=600)
        if r.returncode:raise RuntimeError((test['name'],r.stdout[-1000:],r.stderr[-2000:]))
        rows=log.read_text().splitlines()
        if not rows and test['name'] not in ('rpc-cpu-trace','rpc-cmos-trace'):
            raise RuntimeError((test['name'],'diagnostic overlay did not observe a binding'))
        counts=[0]*4;eligible=[0]*4;instructions=[0]*4
        for line in rows:
            model,n,e,i=map(int,line.split());counts[model]+=n;eligible[model]+=e;instructions[model]+=i
        group='observation' if test['name']=='rpc-observation-plan' else 'goldens'
        totals[group]=[x+y for x,y in zip(totals[group],counts)]
        eligible_totals[group]=[x+y for x,y in zip(eligible_totals[group],eligible)]
        instruction_totals[group]=[x+y for x,y in zip(instruction_totals[group],instructions)]
        print(test['name']+': taken='+','.join(map(str,counts))+' eligible='+','.join(map(str,eligible))+' instructions='+','.join(map(str,instructions)),flush=True)
    print(json.dumps(dict(taken=totals,eligible=eligible_totals,instructions=instruction_totals),sort_keys=True),flush=True)
