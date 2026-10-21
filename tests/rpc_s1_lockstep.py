#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compare S1 with its exact predecessor, using each build's own loader/core.

The independent no-emitter oracle chooses the complete program endpoint plus
200 attempts; all declared RAM signatures must be present before comparison.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import rpc_lockstep_compare as c

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path)
p.add_argument('--fixtures',type=Path,help='additional local-full manifest; defaults to the build CI corpus')
a=p.parse_args();base=a.baseline.resolve();cand=a.candidate.resolve()
fixtures=(a.fixtures or cand/'generated/rpc-lockstep').resolve();items=json.loads((fixtures/'manifest.json').read_text())['items']
schema,sh=c.load_schema()
def core(build,stem):
    matches=[build/(stem+suffix) for suffix in ('.dylib','.so','.dll') if (build/(stem+suffix)).exists()]
    if len(matches)!=1:raise RuntimeError(('core missing/ambiguous',stem))
    return matches[0]
def run(build,library,item,sink,endpoint,flags=0):
    cmd=[str(build/('rpc_lockstep_emitter'+('.exe' if os.name=='nt' else ''))),str(library),str(fixtures/(item['name']+'.rom')),
        str(item['model']),str(sink),'16666667',str(flags),'0','0','0','200',f'end:{endpoint}','-','-',str(len(item['expected'])),'2']
    r=subprocess.run(cmd,capture_output=True,timeout=120)
    if r.returncode:raise RuntimeError((item['name'],'loader refused',r.returncode,r.stderr[-1000:]))
    return r.stdout
oracle=core(cand,'arcdroid_rpcplan_libretro');left=core(base,'arcdroid_rpclockstep_libretro');right=core(cand,'arcdroid_rpclockstep_libretro')
with tempfile.TemporaryDirectory(prefix='arcdroid-s1b-lockstep-',dir=cand.parent) as tmp:
    folder=Path(tmp)
    for item in items:
        endpoint=200000
        def complete(n):
            out=run(cand,oracle,item,'off',n).decode()
            match=re.search(r'^ram=(.*)$',out,re.M)
            return match is not None and list(map(lambda x:int(x,16),match[1].split(',')))==item['expected']
        if not item['fatal']:
            low,high=0,endpoint
            if not complete(high):raise RuntimeError((item['name'],'independent complete-program signature absent'))
            while high-low>1:
                middle=(low+high)//2
                if complete(middle):high=middle
                else:low=middle
            endpoint=min(endpoint,high+200)
        one=run(base,left,item,folder/'base',endpoint,1)
        two=run(cand,right,item,folder/'candidate',endpoint)
        if one!=two:raise RuntimeError((item['name'],'predecessor stdout differs'))
        with (folder/'base').open('rb') as l,(folder/'candidate').open('rb') as r:result=c.compare(l,r,schema,sh,'strict')
        if not result['ok']:raise RuntimeError((item['name'],result))
        (folder/'base').unlink();(folder/'candidate').unlink()
        print('PASS',item['name'],'endpoint='+str(endpoint),'predecessor strict lockstep',flush=True)
print(f'{len(items)} directed/fixed-seed items: zero predecessor divergences')
