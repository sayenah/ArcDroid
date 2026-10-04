#!/usr/bin/env python3
"""Compare deterministic guest evidence; never compare host execution timings."""
import argparse,csv,hashlib,json,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('a',type=Path);p.add_argument('b',type=Path);p.add_argument('--minimum-seconds',type=float,default=600);args=p.parse_args()
fields=('frame','emu_ns','period_ns','instructions','audio_frames','underruns','overruns','video_hash','audio_hash','status')
def read(directory):
 with (directory/'frames.csv').open(newline='') as f: rows=list(csv.DictReader(f))
 if not rows or any(r.get('status')!='OK' for r in rows):raise ValueError('empty/failed run')
 if int(rows[-1]['emu_ns'])<args.minimum_seconds*1e9:raise ValueError('shortened run')
 return rows
try:
 a,b=read(args.a),read(args.b)
 if len(a)!=len(b):raise ValueError('frame count differs')
 for i,(x,y) in enumerate(zip(a,b)):
  for k in fields:
   if x[k]!=y[k]:raise ValueError(f'frame {i}: {k} differs')
 ea=(args.a/'events.csv').read_bytes();eb=(args.b/'events.csv').read_bytes()
 if ea!=eb:raise ValueError('delivery/checkpoint log differs')
 print(json.dumps({'status':'PASS','frames':len(a),'emu_ns':int(a[-1]['emu_ns']),'audio_frames':sum(int(r['audio_frames']) for r in a),'audio_hash':a[-1]['audio_hash'],'events_sha256':hashlib.sha256(ea).hexdigest()},sort_keys=True))
except (ValueError,OSError,KeyError) as e:
 print(f'FAIL {e}');sys.exit(1)
