#!/usr/bin/env python3
"""Authored trace schema/privacy/loss and off/unmatched/trace parity. GPL-2.0-only."""
from pathlib import Path
import json
import re
import struct
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
runner, core, shipping, fixtures = sys.argv[1:5]
fixtures = Path(fixtures).resolve()
profile = sys.argv[5] if len(sys.argv)>5 else 'full'
# Frozen input schedule, RTC and generated seed; no host timing is compared.
def projection(text):
    return '\n'.join(x for x in text.splitlines() if x.startswith(('frame=','regs=','video=','pcm=','first-raster=','ram=','cmos=')))
def run(plan, folder, library=core, frames=3, pacing='flat', extra=()):
    r=subprocess.run([runner,'--core',library,'--plan',str(plan),'--system',str(fixtures),
        '--save',str(folder),'--frames',str(frames),'--pacing',pacing,*extra],capture_output=True,text=True,timeout=60)
    if r.returncode:raise RuntimeError(r.stderr+r.stdout)
    return r
with tempfile.TemporaryDirectory(prefix='arc-observation-plan-') as temp:
    folder=Path(temp)
    # All four CPU/board variants, batching remains enabled (no measurement).
    for model in range(4):
        for workload in (['audio','mixed-av'] if profile=='full' else ['audio']):
            original=(fixtures/f'{workload}-{model}.arcplan').read_text()
            plan=folder/'test.arcplan';plan.write_text(original)
            off=projection(run(plan,folder).stdout)
            for quota in (1,7,200):
                for mode in ('off','unmatched','trace'):
                    suffix=f'observation_quota={quota}\n'
                    if mode=='unmatched':suffix+='observation_profile=unmatched\n'
                    if mode=='trace':suffix+='observation_trace=numeric.jsonl\n'
                    plan.write_text(original+suffix)
                    result=run(plan,folder)
                    if projection(result.stdout)!=off:raise RuntimeError((model,workload,quota,mode,'projection changed'))
            if profile=='full':
                plan.write_text(original+'observation_trace=numeric.jsonl\n')
                if projection(run(plan,folder,pacing='burst').stdout)!=off:raise RuntimeError('pacing changed')
    # The hot SA workload enables the fast clock and warms the instruction
    # cache. Active diagnostics prove integer batches actually executed.
    original=(fixtures/'hot.arcplan').read_text();plan=folder/'hot.arcplan';plan.write_text(original)
    off=projection(run(plan,folder,frames=3).stdout)
    for quota in (1,7,200):
        for mode in ('unmatched','trace'):
            suffix=f'observation_quota={quota}\n'
            suffix+= 'observation_profile=unmatched\n' if mode=='unmatched' else 'observation_trace=hot.jsonl\n'
            plan.write_text(original+suffix);r=run(plan,folder,frames=3)
            if projection(r.stdout)!=off or not re.search(r'integer_attempts=[1-9]',r.stderr):raise RuntimeError('integer batching parity/coverage')
    # Compact guest emits a defined User32 SWI, a returned error number, an
    # unknown SWI with forbidden payload and then an SVC32 SWI storm.
    rom=bytearray(struct.pack('<I',0xeafffffe)*(2097152//4))
    # Reset branches over vectors. SWI vector jumps to handler.
    program={0:0xea00003e,8:0xea000046,0x100:0xe321f010,0x104:0xe3a00005,
             0x108:0xef020008,0x10c:0xef123456,0x110:0xe321f013,
             0x114:0xef000007,0x118:0xeafffffd,0x120:0x10000000,
             0x128:0xe1b0f00e}
    # Supervisor handler restores the saved status; no error/message bytes.
    for offset,word in program.items():struct.pack_into('<I',rom,offset,word)
    (folder/'trace.rom').write_bytes(rom);(folder/'seed').write_bytes(bytes(256))
    original=f'version=1\nmodel=sa110\nfirmware={folder/"trace.rom"}\ncmos=seed\nseed={folder/"seed"}\nrtc_epoch=0\n'
    plan=folder/'trace.arcplan';plan.write_text(original);off=projection(run(plan,folder,frames=1).stdout)
    plan.write_text(original+'observation_trace=trace.jsonl\n');r=run(plan,folder,frames=1)
    if projection(r.stdout)!=off:raise RuntimeError('overflow trace changed guest')
    rows=[json.loads(x) for x in (folder/'trace.jsonl').read_text().splitlines()]
    if not rows or not rows[-1].get('incomplete') or rows[-1]['lost']<1:raise RuntimeError('loss marker missing')
    events=rows[:-1]
    if any(x['schema']!=1 or 'instruction_bytes' in x for x in events):raise RuntimeError('schema')
    if any(x['path_hex'] for x in events):raise RuntimeError('path leaked without private opt-in')
    if not any(x['swi']==0x20008 for x in events):raise RuntimeError('X SWI missing')
    if not any(x['kind']==2 and x['matched'] for x in events):raise RuntimeError('continuation missing')
    unknown=next(x for x in events if x['swi']==0x123456)
    if any(unknown['registers'][:13]) or unknown['mask']!=0x6000:raise RuntimeError('unknown SWI payload leaked')
    if any(a['order']>=b['order'] for a,b in zip(events,events[1:])):raise RuntimeError('event order')
    # Failure to open/write has its own explicit diagnostic and unchanged guest.
    plan.write_text(original+'observation_trace=missing/trace.jsonl\n');r=run(plan,folder,frames=1)
    if projection(r.stdout)!=off or 'incomplete write-failure' not in r.stderr:raise RuntimeError(('write failure parity/marker',r.stderr,r.stdout,projection(r.stdout)==off))
    close_original=(fixtures/'audio-0.arcplan').read_text();plan.write_text(close_original+'observation_trace=close.jsonl\n')
    good=run(plan,folder,frames=1)
    if 'incomplete' in good.stderr:raise RuntimeError('close fixture must be complete before injection')
    r=run(plan,folder,frames=1,extra=['--fail-observation-close'])
    if projection(r.stdout)!=projection(good.stdout) or 'incomplete final sink/close' not in r.stderr:raise RuntimeError('close failure parity/marker')
    # Shipping admission remains unavailable, with no test trace symbol/profile.
    r=subprocess.run([runner,'--core',shipping,'--plan',str(plan),'--reject'],capture_output=True,text=True,timeout=30)
    if r.returncode:raise RuntimeError('shipping trace admission')
print('observation plan: 4 CPUs, quotas 1/7/200, off/unmatched/trace parity; schema, allowlist, overflow and failed sink')
