#!/usr/bin/env python3
"""Summarize complete raw core measurements without inventing frontend data."""
import argparse,csv,json,math,statistics,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('csv',type=Path);p.add_argument('--start-seconds',type=float,default=0);p.add_argument('--end-seconds',type=float);p.add_argument('--output',type=Path);a=p.parse_args()
try:
 with a.csv.open(newline='') as f: allrows=list(csv.DictReader(f))
 rows=[r for r in allrows if float(r['emu_ns'])>a.start_seconds*1e9 and (a.end_seconds is None or float(r['emu_ns'])<=a.end_seconds*1e9)]
 if not rows:raise ValueError('empty window')
 if any(r['status']!='OK' for r in allrows):raise ValueError('explicit failed run')
 def q(v,p):return sorted(v)[max(0,math.ceil(len(v)*p)-1)]
 ratios=[int(r['exec_ns'])/int(r['period_ns']) for r in rows]
 streak=maximum=misses=0
 for r in rows:
  miss=int(r['exec_ns'])>int(r['period_ns']);misses+=miss;streak=streak+1 if miss else 0;maximum=max(maximum,streak)
 guest=int(rows[-1]['emu_ns'])-int(rows[0]['emu_ns']);wall=int(rows[-1]['wall_ns'])-int(rows[0]['wall_ns'])
 out={'status':'COMPLETE' if all(r['status']=='OK' for r in allrows) else 'FAIL','frames':len(rows),'complete_run_emu_ns':int(allrows[-1]['emu_ns']),'window_first_ns':int(rows[0]['emu_ns']),'window_last_ns':int(rows[-1]['emu_ns']),'execution_T':{str(x):q(ratios,x) for x in [.5,.95,.99,.999]},'deadline_misses':misses,'maximum_consecutive_misses':maximum,'guest_wall_drift':abs(guest-wall)/guest if guest>0 and wall>0 else None,'audio_pairs':sum(int(r['audio_frames']) for r in rows),'final_audio_hash':rows[-1]['audio_hash'],'hash_cost_p95_T':q([int(r['instrument_ns'])/int(r['period_ns']) for r in rows],.95),'frontend_missed_presentations':'MISSING','independent_audio_output':'MISSING','matched_phase_first_last':'MISSING without predeclared phase labels','full_instrumentation_AB_overhead':'MISSING without paired on/off runs'}
 print(json.dumps(out,indent=2))
 if a.output:a.output.write_text(json.dumps(out,indent=2)+'\n')
except (OSError,ValueError,KeyError,ZeroDivisionError) as e:print('MISSING:',e,file=sys.stderr);sys.exit(2)
