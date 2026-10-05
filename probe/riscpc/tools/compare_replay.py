#!/usr/bin/env python3
"""Compare deterministic guest evidence; never compare host execution timings."""
import argparse,csv,hashlib,json,sys,math
from pathlib import Path
from evidence import core_rows, completion, EvidenceError
p=argparse.ArgumentParser();p.add_argument('a',type=Path);p.add_argument('b',type=Path);p.add_argument('--minimum-seconds',type=float,default=600);args=p.parse_args()
if not math.isfinite(args.minimum_seconds) or args.minimum_seconds<600:
 p.error('minimum replay duration must be finite and at least600 seconds')
fields=('frame','emu_ns','period_ns','instructions','audio_frames','underruns','overruns','video_hash','audio_hash','status')
def read(directory):
 rows=core_rows(directory/'frames.csv')
 footer=completion(directory/'frames.csv',rows)
 if footer.get('video_renderer')!='line-v4':raise ValueError('requires final line renderer footer')
 if int(rows[-1]['emu_ns'])<args.minimum_seconds*1e9:raise ValueError('shortened run')
 if sum(r['period_ns'] for r in rows)!=rows[-1]['emu_ns']:raise ValueError('frame periods do not cover guest time')
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
except (ValueError,OSError,KeyError,EvidenceError) as e:
 status=getattr(e,'status','MISSING' if isinstance(e,(OSError,KeyError)) else 'FAIL')
 print(f'{status} {e}');sys.exit(2 if status=='MISSING' else 1)
