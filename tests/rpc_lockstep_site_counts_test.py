#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Per-item P1 oracle: no-emitter profile counters plus declared guest structure.

The Matrix ABT/PABT handlers each execute two unconditional MRCs. All other
corpus coprocessor instructions are MCRs; SA idle's warm call skips one NE MCR.
Each partial-STM body has one aborted write. The separate prefetch/FIQ guest
also aborts its FIQ-mask STR (section 0x32 is unmapped), taking its authored
data-abort trampoline; the remaining data aborts are reads.
Modes contributes eight transitions, carry26 two, permission-hit two explicit
privilege changes, and masked-refresh one. Each returning IRQ/FIQ/data/prefetch
exception contributes entry+return. The separate walk guest enters ABT once;
the separate prefetch/FIQ guest has six declared transitions. No emitter count
is used to construct these expected values.
"""
import argparse
import collections
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import rpc_lockstep_compare as c
from rpc_lockstep_corpus_test import expectation


def expected(item,profile):
    features=item['markers'];exits=profile['exits'];name=item['name']
    aborts=exits['E7a']+exits['E7b'];write_abort=int('partial-stm' in features)+int(name.startswith('prefetch-fiq-'))
    reads=0 if name.startswith(('walk-mmio-','prefetch-fiq-')) else 2*aborts
    writes=profile['classes']['coprocessor']-reads-int(item['model']==2 and 'idle' in features)
    swi=2*int('modes' in features)+int('permission-hit' in features)
    modes=1+8*int('modes' in features)+2*int('carry26' in features)+2*int('permission-hit' in features)+int('masked-refresh' in features)+2*(aborts+exits['E6'])
    if name.startswith('walk-mmio-'):modes=2
    if name.startswith('prefetch-fiq-'):modes=6
    if item['fatal']:modes=0
    return {1:profile['fetched'],2:profile['reads']+exits['E7a']-write_abort,
        3:profile['writes']+write_abort,4:swi,5:reads,6:writes,
        7:sum(exits[key] for key in ('E6','E7a','E7b','E10')),8:modes}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('runner');p.add_argument('profile_core');p.add_argument('core')
    p.add_argument('fixtures',type=Path);p.add_argument('--model',type=int,required=True)
    p.add_argument('--only',default='');a=p.parse_args();schema,sh=c.load_schema();start=time.monotonic()
    items=[item for item in json.loads((a.fixtures/'manifest.json').read_text(encoding='utf-8'))['items'] if item['model']==a.model and a.only in item['name']]
    if not items:raise RuntimeError('empty site-count corpus')
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-site-counts-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
        root=Path(tmp)
        for item in items:
            profile=root/'profile.json';stream=root/'stream.bin'
            common=['--plan',str(a.fixtures/(item['name']+'.arcplan')),'--system',str(a.fixtures),
                '--save',str(root),'--frames','2','--ram-offset','262144','--ram-words',str(len(item['expected'])),
                '--endpoint-instructions','1:30000:0']
            oracle=subprocess.run([a.runner,'--core',a.profile_core,*common,'--profile',str(profile),
                '--profile-sample-stride','0'],capture_output=True,text=True,timeout=60, encoding='utf-8')
            expectation(item,oracle)
            actual=subprocess.run([a.runner,'--core',a.core,*common,'--lockstep-stream',str(stream),
                '--lockstep-role','baseline','--lockstep-model',str(a.model)],capture_output=True,text=True,timeout=120, encoding='utf-8')
            if actual.returncode!=oracle.returncode or actual.stdout!=oracle.stdout:raise RuntimeError((item['name'],'profile/lockstep guest effect',oracle.returncode,actual.returncode,actual.stderr[-1000:]))
            counters=json.loads(profile.read_text(encoding='utf-8'))['records'][-1];want=expected(item,counters);got=collections.Counter();edges=collections.Counter();attempts=0
            with stream.open('rb') as file:
                reader=c.Reader(file,schema,sh,'strict')
                while (record:=reader.next()) is not None:
                    if record.kind not in (1,2):continue
                    if record.key[3]==0:attempts+=1
                    if record.key[3]==1:
                        edges[record.key[4],record.edge]+=1
                        if record.edge!=2:got[record.key[4]]+=1
            if attempts!=counters['attempts'] or any(got[site]!=count for site,count in want.items()):
                raise RuntimeError((item['name'],'independent P1 oracle disagreement',dict(got),want,attempts,counters['attempts']))
            if any(edges[site,1]!=want[site] or edges[site,2]!=want[site] for site in (2,3)):
                raise RuntimeError((item['name'],'accessor entry/exit counts',dict(edges),want))
            print('PASS',item['name'],'attempts='+str(attempts),'sites='+json.dumps(want,sort_keys=True),flush=True)
            stream.unlink()
    print('per-item independent site counts:',len(items),'elapsed',round(time.monotonic()-start,3),flush=True)


if __name__=='__main__':main()
