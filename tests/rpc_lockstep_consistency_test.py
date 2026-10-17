#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Two isolated C emitters and no-hook oracle over complete authored programs.

No stream hash is pinned. The bounded endpoint must contain every independently
declared RAM result and feature marker; an incomplete prefix fails this test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import rpc_lockstep_compare as c


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('loader');p.add_argument('plan_core');p.add_argument('lockstep_core')
    p.add_argument('fixtures',type=Path);p.add_argument('--model',type=int)
    p.add_argument('--only',default='');p.add_argument('--audit',action='store_true')
    p.add_argument('--audit-fuzz',action='store_true',help='local full-audit coverage including fuzz; CI --audit remains directed-only')
    p.add_argument('--cross-quota-only',action='store_true',help='rerun full-checkpoint quotas and oracle without unchanged strict density legs')
    p.add_argument('--endpoint',type=int,default=200000)
    a=p.parse_args();started=time.monotonic()
    items=json.loads((a.fixtures/'manifest.json').read_text())['items']
    items=[x for x in items if (a.model is None or x['model']==a.model) and a.only in x['name']]
    if not items:raise RuntimeError('empty corpus')
    maximum=0;total_bytes=0;quota_comparisons=0;schema,sh=c.load_schema()
    def run(item,core,sink,flags,endpoint=None,probe=False,quota=200):
        command=[a.loader,core,str(a.fixtures/(item['name']+'.rom')),str(item['model']),sink,
            '16666667',str(flags),'0','0','0',str(quota),f'end:{a.endpoint if endpoint is None else endpoint}','-','-',
            str(len(item['expected'])),'2']
        begin=time.monotonic()
        r=subprocess.run(command,capture_output=True,timeout=120)
        elapsed=time.monotonic()-begin
        if r.returncode:raise RuntimeError((item['name'],sink,flags,r.returncode,r.stderr[-1000:]))
        text=r.stdout.decode()
        if item['fatal']:
            if 'fault=1' not in text:raise RuntimeError((item['name'],'missing fatal'))
            registers=[int(x,16) for x in re.search(r'^regs=(.*)$',text,re.M)[1].split(',')]
            for index,value in item['terminal_registers'].items():
                if registers[int(index)]!=value:raise RuntimeError((item['name'],'terminal register',index))
        else:
            actual=[int(x,16) for x in re.search(r'^ram=(.*)$',text,re.M)[1].split(',')]
            if probe:return actual==item['expected']
            if actual!=item['expected']:raise RuntimeError((item['name'],'incomplete program or RAM disagreement',
                [(i,x,y) for i,(x,y) in enumerate(zip(actual,item['expected'])) if x!=y][:8]))
            for feature,offset in item['markers'].items():
                if not actual[offset]:raise RuntimeError((item['name'],'feature floor',feature))
            if item.get('terminal_cpsr') is not None:
                registers=[int(x,16) for x in re.search(r'^regs=(.*)$',text,re.M)[1].split(',')]
                if registers[16]&255!=item['terminal_cpsr']:raise RuntimeError((item['name'],'terminal CPSR'))
        return r,elapsed
    for item in items:
        # Bound terminal-loop cost using only the no-emitter oracle and the
        # independently declared signature. Keep 200 further attempts and both
        # whole frontend periods; never select a prefix from emitter state.
        endpoint=a.endpoint
        if not item['fatal']:
            low,high=0,endpoint
            if not run(item,a.plan_core,'off',0,endpoint=high,probe=True):raise RuntimeError((item['name'],'oracle program exceeds endpoint'))
            while high-low>1:
                middle=(low+high)//2
                if run(item,a.plan_core,'off',0,endpoint=middle,probe=True):high=middle
                else:low=middle
            endpoint=min(a.endpoint,high+200)
        oracle,_=run(item,a.plan_core,'off',0,endpoint=endpoint)
        plain,_=run(item,a.lockstep_core,'off',0,endpoint=endpoint)
        if oracle.stdout!=plain.stdout:raise RuntimeError((item['name'],'class-on sink-off guest effect'))
        # Mode (b) compares every represented P0/P1 entry/exit/P2 group,
        # event and non-optional page boundary, with only reviewed projections.
        # File readers keep bounded state; each complete stream is discarded
        # before the next item. No authored item or seed is omitted.
        with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-quotas-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
            left=Path(tmp)/'baseline.stream';right=Path(tmp)/'candidate.stream'
            baseline,elapsed=run(item,a.lockstep_core,str(left),257,endpoint=endpoint)
            maximum=max(maximum,elapsed);total_bytes+=left.stat().st_size
            expected=c.stdout_projection(oracle.stdout.decode(),'cross-quota',schema)
            if c.stdout_projection(baseline.stdout.decode(),'cross-quota',schema)!=expected:
                raise RuntimeError((item['name'],'cross-quota baseline guest effect'))
            for quota in (1,7,200):
                candidate,elapsed=run(item,a.lockstep_core,str(right),256,endpoint=endpoint,quota=quota)
                maximum=max(maximum,elapsed);total_bytes+=right.stat().st_size
                if c.stdout_projection(candidate.stdout.decode(),'cross-quota',schema)!=expected:
                    raise RuntimeError((item['name'],'cross-quota guest effect',quota))
                with left.open('rb') as l,right.open('rb') as r:result=c.compare(l,r,schema,sh,'cross-quota')
                if not result['ok']:raise RuntimeError((item['name'],'full-checkpoint quota disagreement',quota,result))
                quota_comparisons+=1;right.unlink()
        if a.cross_quota_only:
            print('PASS',item['name'],'endpoint='+str(endpoint),'full-checkpoint-quotas=1,7,200',flush=True)
            continue
        hashes=[]
        # The same baseline role makes the two complete wire streams literally
        # byte-identical, including header flags and terminal counters.
        for _ in range(2):
            result,elapsed=run(item,a.lockstep_core,'hash',1,endpoint=endpoint);maximum=max(maximum,elapsed)
            if result.stdout!=oracle.stdout:raise RuntimeError((item['name'],'dense sink guest effect'))
            match=re.search(rb'stream-sha256=([0-9a-f]{64}) bytes=(\d+)',result.stderr)
            if not match:raise RuntimeError((item['name'],'missing complete stream hash'))
            hashes.append(match[1]);total_bytes+=int(match[2])
        if hashes[0]!=hashes[1]:raise RuntimeError((item['name'],'two-process wire disagreement'))
        # Every-checkpoint density, P1 disabled, and full-audit are independent
        # guest-equivalence legs. Full audit is directed-only in CI (R-M2b-8).
        densities=[2,8]
        if a.audit_fuzz or a.audit and not item['name'].startswith('fuzz-'):densities.append(4)
        for flags in densities:
            result,elapsed=run(item,a.lockstep_core,'discard',flags,endpoint=endpoint);maximum=max(maximum,elapsed)
            if result.stdout!=oracle.stdout:raise RuntimeError((item['name'],'density guest effect',flags))
        print('PASS',item['name'],'endpoint='+str(endpoint),'full-checkpoint-quotas=1,7,200','wire-sha256='+hashes[0].decode(),
            'stdout-sha256='+hashlib.sha256(oracle.stdout).hexdigest(),flush=True)
    print(json.dumps(dict(items=len(items),endpoint=a.endpoint,maximum_process_seconds=maximum,
        stream_bytes=total_bytes,quota_comparisons=quota_comparisons,elapsed_seconds=time.monotonic()-started),sort_keys=True),flush=True)


if __name__=='__main__':main()
