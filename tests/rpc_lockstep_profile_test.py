#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Physical events/fetch density cross-checked against the separate profile core."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import rpc_lockstep_compare as c
from rpc_timing_rom import Guest
p=argparse.ArgumentParser();p.add_argument('runner');p.add_argument('profile_core');p.add_argument('lockstep_core');p.add_argument('fixtures',type=Path);a=p.parse_args();schema,sh=c.load_schema()
with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-profile-counts-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
 root=Path(tmp);(root/'authored.seed').write_bytes(bytes(256))
 g=Guest();g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
 # Each physical dispatch is counted once, including absorbing/absent ranges.
 writes=(0x03310000,0x03400000,0x03f00000,0x08000000,0x03d00000,0x23400000,0x23400000)
 reads=(0x03310000,0x03000000,0x03f00000,0x08000000,0x03d00000,0x23000000,0x23000000)
 for i,address in enumerate(writes):
  g.literal(0,address);g.literal(1,0);g.emit(0xe5c01000 if i==6 else 0xe5801000)
 for i,address in enumerate(reads):g.literal(0,address);g.emit(0xe5d01000 if i==6 else 0xe5901000)
 payload=struct.pack('<'+'I'*len(g.words),*g.words);(root/'dispatch.rom').write_bytes(payload+bytes(2097152-len(payload)))
 for model in range(4):
  plan=root/'dispatch.arcplan';plan.write_text(f'version=1\nmodel={("arm610","arm710","sa110","arm7500")[model]}\nfirmware=dispatch.rom\ncmos=seed\nseed=authored.seed\nrtc_epoch=0\n')
  for kind,source,system,endpoint in [('dispatch',plan,root,g.steps),('walk',a.fixtures/f'walk-mmio-{model}.arcplan',a.fixtures,80)]:
   common=['--plan',str(source),'--system',str(system),'--save',str(root),'--frames','1','--endpoint-instructions',f'1:{endpoint}:0']
   profile=root/'profile.json';stream=root/'stream.bin'
   prof=subprocess.run([a.runner,'--core',a.profile_core,*common,'--profile',str(profile),'--profile-sample-stride','0'],capture_output=True,text=True,timeout=60)
   if prof.returncode:raise RuntimeError(('profile baseline',model,kind,prof.stderr[-1000:]))
   actual=subprocess.run([a.runner,'--core',a.lockstep_core,*common,'--lockstep-stream',str(stream),'--lockstep-model',str(model),'--lockstep-role','baseline'],capture_output=True,text=True,timeout=60)
   if actual.returncode or actual.stdout!=prof.stdout:raise RuntimeError(('stream/class guest effect',model,kind,actual.returncode,actual.stderr[-1000:]))
   record=json.loads(profile.read_text())['records'][-1];counts=dict(reads=0,writes=0,walk=0,attempts=0,fetched=0);addresses=[]
   with stream.open('rb') as f:
    reader=c.Reader(f,schema,sh,'strict')
    while (r:=reader.next()) is not None:
     if r.kind in (1,2):
      counts['attempts']+=r.key[3]==0;counts['fetched']+=r.key[3:5]==(1,1)
     if r.kind==3 and r.value[9]==1:
      counts['writes' if r.value[12] else 'reads']+=1;counts['walk']+=r.value[8]==1;addresses.append(r.value[10])
   if (counts['reads'],counts['writes'],counts['attempts'],counts['fetched'])!=(record['mmio_reads'],record['mmio_writes'],record['attempts'],record['fetched']):raise RuntimeError(('independent profile counts',model,kind,counts,record))
   if kind=='dispatch' and ((counts['reads'],counts['writes'])!=(7,7) or addresses!=[address&0x1fffffff for address in (*writes,*reads)]):raise RuntimeError(('authored static MMIO counts/bus addresses',model,counts,addresses))
   if kind=='walk' and not counts['walk']:raise RuntimeError(('walk agent absent',model))
   print(model,kind,json.dumps(counts,sort_keys=True),flush=True);stream.unlink()
print('four models: static physical dispatch counts and no-emitter profile attempts/fetch/MMIO counts agree')
