#!/usr/bin/env python3
"""Pinned 30c0bab comparison; authored inputs and explicit guest projection. GPL-2.0-only.
Usage: script RUNNER BASE_CORE CANDIDATE_CORE FIXTURES [full|sanitizer]
BASE_CORE must have been built from git archive 30c0bab, same compiler/settings.
Host durations/paths and added diagnostics excluded. No golden rewriting.
"""
from pathlib import Path
import hashlib
import re
import runpy
import struct
import subprocess
import sys
import tempfile
import zlib
sys.dont_write_bytecode=True
runner,baseline,candidate,fixtures=sys.argv[1:5]
fixtures=Path(fixtures).resolve()
profile=sys.argv[5] if len(sys.argv)>5 else 'full'
def projection(text):
    return '\n'.join(x for x in text.splitlines() if x.startswith(('frame=','regs=','video=','pcm=','first-raster=','ram=','cmos=')))
def run(core,plan,system,save,frames,pacing='flat'):
    r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(system),'--save',str(save),
        '--frames',str(frames),'--pacing',pacing,'--ram-offset','0','--ram-words','64'],capture_output=True,text=True,timeout=120)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    return projection(r.stdout)
with tempfile.TemporaryDirectory(prefix='arc-observation-baseline-') as temp:
    root=Path(temp)
    compared=0
    for model in range(4):
        for workload,frames in [('audio',3),('mixed-av',3),('matrix',6 if model==3 else 2),('input',7)]:
            if profile=='sanitizer' and workload!='audio':continue
            original=(fixtures/f'{workload}-{model}.arcplan').read_text()
            plan=root/'test.arcplan';plan.write_text(original)
            old=run(baseline,plan,fixtures,root,frames)
            new=run(candidate,plan,fixtures,root,frames)
            if new!=old:raise RuntimeError((model,workload,'baseline/off divergence'))
            compared+=1
    plan=root/'hot.arcplan';plan.write_text((fixtures/'hot.arcplan').read_text())
    if run(baseline,plan,fixtures,root,3)!=run(candidate,plan,fixtures,root,3):raise RuntimeError('hot baseline mismatch')
    compared+=1
    # 126 MiB sparse IDE plus CMOS records, initially identical; guest saves
    # and timed media changes use today's plan clients/stores in both cores.
    generator=Path(__file__).with_name('rpc_ide_rom.py');header=root/'ide.h'
    before=sys.argv;sys.argv=[str(generator),str(header)];runpy.run_path(str(generator),run_name='__main__');sys.argv=before
    words=[int(x,16) for x in re.findall(r'0x([0-9a-f]{8})u',header.read_text())]
    rom=struct.pack('<'+'I'*len(words),*words);rom+=struct.pack('<I',0xeafffffe)*((2097152-len(rom))//4)
    firmware=root/'ide.rom';firmware.write_bytes(rom);digest=hashlib.sha256(rom).digest();content='c'*40
    def initial(save):
        directory=save/'arcdroid/cmos-riscpc';directory.mkdir(parents=True)
        record=bytearray(512);record[:8]=b'ARCRPCM1';struct.pack_into('<IIIIQ',record,8,1,512,1,0x10003,1)
        record[32:72]=content.encode();record[72:104]=digest;struct.pack_into('<I',record,508,zlib.crc32(record[:508]))
        (directory/f'{content}-m1-{digest.hex()}.1').write_bytes(record)
    text=f'version=2\nmodel=arm610\nfirmware={firmware}\ncmos=persist\ncmos_layout=ro370\ncontent_id={content}\nrtc_epoch=946684800\nhard_disc=on\n'
    results=[]
    configs=[(baseline,'baseline',''),(candidate,'off',''),(candidate,'unmatched','observation_profile=unmatched\n'),
             (candidate,'trace','observation_trace=trace.jsonl\n')]
    for core,label,suffix in configs:
        save=root/label;save.mkdir();initial(save);plan=root/(label+'.arcplan');plan.write_text(text+suffix)
        output=run(core,plan,root,save,8)
        files={p.relative_to(save).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
            for p in save.rglob('*') if p.is_file() and p.name!='trace.jsonl'}
        results.append((output,files))
    if any(x!=results[0] for x in results[1:]):raise RuntimeError('IDE/CMOS outputs or persisted files differ')
print(f'30c0bab baseline/off: {compared} authored CPU/AV/input plans + IDE/CMOS persisted files identical; off/unmatched/trace store parity')
