#!/usr/bin/env python3
"""Schema, exact MMIO/idle guest, diagnostic purity and sink failures. GPL-2.0-only."""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
runner,core,shipping,fixtures=sys.argv[1:5]
fixtures=Path(fixtures).resolve()
def require(ok,message):
    if not ok:raise RuntimeError(message)
def projection(text):
    return '\n'.join(x for x in text.splitlines() if x.startswith(('frame=','regs=','video=','pcm=','first-raster=','ram=','cmos=')))
def run(plan,folder,extra=(),frames=1,ok=True):
    r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(fixtures),'--save',str(folder),'--frames',str(frames),*extra],capture_output=True,text=True,timeout=40)
    require((r.returncode==0)==ok,(r.returncode,r.stderr))
    return r
with tempfile.TemporaryDirectory(prefix='arc-profile-') as temp:
    folder=Path(temp)
    rom=bytearray(2097152)
    words={0:0xea00003e,0x100:0xe3a00010,0x104:0xee010f10,0x108:0xe321f0d3,
           0x10c:0xe59f1010,0x110:0xe5912000,0x114:0xe5812000,0x118:0xee0f0f58,
           0x124:0x03200000}
    for addr,op in words.items():struct.pack_into('<I',rom,addr,op)
    (folder/'authored.rom').write_bytes(rom);(folder/'seed').write_bytes(bytes(256))
    plan=folder/'idle.arcplan'
    original=f'version=1\nmodel=sa110\nfirmware={folder/"authored.rom"}\ncmos=seed\nseed={folder/"seed"}\nrtc_epoch=0\n'
    plan.write_text(original)
    baseline=run(plan,folder,frames=2)
    for stride in (0,1,1024):
        out=folder/f'profile-{stride}.json'
        r=run(plan,folder,['--profile',str(out),'--profile-interval','1','--profile-sample-stride',str(stride)],2)
        require(projection(r.stdout)==projection(baseline.stdout),'profile changed guest projection')
        j=json.loads(out.read_text());p=j['records'][-1]
        require(j['schema']==1 and j['sample_stride']==stride and j['recent_attempts']==1000000,'schema header')
        require(len(j['records'])==3 and p['kind']=='exit','interval and exit')
        require(p['attempts']==8 and p['fetched']==8 and p['condition_passed']==8,('attempts',p))
        expected=dict(alu=1,multiply=0,branch=1,load=2,store=1,ldm=0,stm=0,swap=0,half_signed=0,status=1,coprocessor=2,swi=0,other=0)
        require(p['classes']==expected,('classes',p['classes']))
        require(p['reads']==2 and p['writes']==1 and p['mmio_reads']==1 and p['mmio_writes']==1,('access counts',p))
        require(p['exits']['E2']==0 and p['exits']['E3']==3 and p['exits']['E4']==5,('exits',p['exits']))
        require(p['batch_idle']==1 and p['exits']['E5']==1 and p['blocks'][:2]==[4,2] and p['block_max']==2 and p['block_instructions']==8 and not p['open_block'],'idle block closure')
        guest_events=[int(x) for x in re.findall(r' events=([^\n]+)',r.stdout)[-1].split(',')]
        require(list(p['events'].values())==guest_events,'device counters not fed')
        require(p['frames']==0 and j['records'][0]['frames']==1,'frame windows')
        require(len(p['blocks'])==65 and p['faults']==0,'histogram/fault')
        require(p['clock_reads']>0 if stride else p['clock_reads']==0,'clock sampling')
        require(p['probe_calibration_iterations']==(8192 if stride else 0),'probe calibration')
        require(sum(p['probes'].values())>0 if stride else sum(p['probes'].values())==0,'charged probes')
        if stride==1:
            require(p['samples']['profiling']>0 and p['samples']['fetch_translation']==8 and p['samples']['mmio']==2 and p['sampled_ns']['devices']>0,('cost scopes not fed',p['samples'],p['sampled_ns']))
        require(not any(key in out.read_text() for key in ('opcode','registers','path_hex','firmware','03200000')),'private guest data in profile')
    # Reset clears page history and rewinds time, with cumulative counters and epoch.
    out=folder/'reset.json'
    run(plan,folder,['--profile',str(out),'--reset-at','1'],2)
    p=json.loads(out.read_text())['records'][-1]
    require(p['attempts']==16 and p['resets']==1 and p['epoch']==1,'reset profile')
    out=folder/'fault.json'
    run(plan,folder,['--profile',str(out),'--fault-at','1'],2,ok=False)
    p=json.loads(out.read_text())['records'][-1]
    require(p['attempts']==8 and p['faults']==1 and p['exits']['E8']==1,'existing fatal boundary profile')
    # A real MSR body fatal unwinds through the retained boundary, with prior
    # effects counted, no next attempt and explicit abandoned timing evidence.
    struct.pack_into('<I',rom,0x108,0xe321f014)
    (folder/'authored.rom').write_bytes(rom)
    out=folder/'body-fault.json'
    run(plan,folder,['--profile',str(out),'--profile-sample-stride','1'],ok=False)
    p=json.loads(out.read_text())['records'][-1]
    require(p['attempts']==4 and p['fetched']==4 and p['faults']==1 and p['exits']['E8']==1 and p['abandoned_samples']==1 and p['block_instructions']==4,'body fatal bookkeeping')
    struct.pack_into('<I',rom,0x108,0xe321f0d3)
    (folder/'authored.rom').write_bytes(rom)
    for extra in (['--profile',str(folder)],['--profile',str(folder/'absent'/'out')],
                  ['--profile-interval','0'],['--profile-sample-stride','1048577'],
                  ['--profile-interval','1']):run(plan,folder,extra,ok=False)
    if os.name!='nt' and Path('/dev/full').exists():
        r=run(plan,folder,['--profile','/dev/full','--profile-interval','1'],ok=False)
        require('profile write failed' in r.stderr,'write failure not surfaced')
    # Every current four-CPU functional golden workload agrees with profiling live.
    for model in range(4):
        for kind in ('matrix','audio','video-geometry','mixed-av'):
            plan.write_text((fixtures/(f'video-{model}-320x256-50.arcplan' if kind=='video-geometry' else f'{kind}-{model}.arcplan')).read_text())
            off=run(plan,folder)
            on=run(plan,folder,['--profile',str(folder/'golden.json')])
            require(projection(off.stdout)==projection(on.stdout),(model,kind,'golden projection changed'))
    plan.write_text((fixtures/'hot.arcplan').read_text())
    out=folder/'hot.json';run(plan,folder,['--profile',str(out),'--profile-sample-stride','0'])
    hot=json.loads(out.read_text())['records'][-1]
    require(hot['cache_fills']>0 and hot['fetch_memo_hits']>1000 and hot['blocks'][0]>1000,'cache/memo/hot blocks not fed')
    # Runtime profiling across the three observer admissions and quotas remains transparent.
    base=(fixtures/'audio-2.arcplan').read_text()
    for suffix in ('','observer_cells=255\nobserver_forms=0xffffffff\n','observation_trace=trace.jsonl\n'):
        for quota in (1,7,200):
            plan.write_text(base+suffix+f'observation_quota={quota}\n')
            off=run(plan,folder);on=run(plan,folder,['--profile',str(folder/'observer.json')])
            require(projection(off.stdout)==projection(on.stdout),'observer path changed')
    r=subprocess.run([runner,'--core',shipping,'--plan',str(plan),'--reject'],capture_output=True,text=True,timeout=30)
    require(r.returncode==0,'shipping plan admission changed')
print('profile runner: exact MMIO/devices, schema/cost scopes, reset/fatal, 4-CPU goldens and observer quotas')
