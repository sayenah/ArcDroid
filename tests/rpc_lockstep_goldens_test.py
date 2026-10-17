#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Replay the 25 existing RPC golden fixtures without changing their vectors.

CPU/CMOS are standalone component executables with no DLL/sink surface. Their
ordinary golden checks remain part of this list and are labelled explicitly;
the remaining 23 checks load the real lockstep DLL with the selected density.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('build',type=Path);p.add_argument('plan_core');p.add_argument('core')
p.add_argument('density',choices=('baseline','every-instruction','full-audit'))
p.add_argument('--only',default='');a=p.parse_args()
cache=(a.build/'CMakeCache.txt').read_text()
cmake=next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_COMMAND:INTERNAL='))
tests=json.loads(subprocess.check_output([cmake,'-E','chdir',str(a.build),'ctest','--show-only=json-v1'],text=True))['tests']
pattern=r'rpc-(?:cpu-trace|cmos-trace|video-trace-[0-3]|video-paths-[0-3]|d2-guest-[0-3]|factory-capture-[23]|plan-(?:matrix-[0-3]|av-[0-3]|devices))'
selected=[test for test in tests if re.fullmatch(pattern,test['name'])]
if len(selected)!=25:raise RuntimeError(('golden fixture census changed',len(selected)))
env=dict(os.environ,ARCDROID_RPC_LOCKSTEP_TEST_DENSITY=a.density,ARCDROID_RPC_LOCKSTEP_PLAN_CORE=a.plan_core)
started=time.monotonic();count=0;maximum=0
for test in selected:
    if a.only and a.only not in test['name']:continue
    command=test['command'];component=test['name'] in ('rpc-cpu-trace','rpc-cmos-trace')
    if not component:
        command=[a.core if Path(arg).name.startswith(('arcdroid_libretro_test.','arcdroid_rpcplan_libretro.')) else arg for arg in command]
    begin=time.monotonic()
    result=subprocess.run(command,capture_output=True,env=env,text=True,timeout=3600)
    elapsed=time.monotonic()-begin;maximum=max(maximum,elapsed)
    if result.returncode:raise RuntimeError((test['name'],a.density,result.returncode,result.stdout[-2000:],result.stderr[-4000:]))
    print('PASS',test['name'],'unchanged standalone component' if component else a.density,round(elapsed,3),flush=True);count+=1
if not count:raise RuntimeError('empty golden selection')
print(json.dumps(dict(goldens=count,density=a.density,maximum_case_seconds=maximum,elapsed_seconds=time.monotonic()-started)),flush=True)
