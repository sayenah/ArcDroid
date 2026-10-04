#!/usr/bin/env python3
"""Recompute every unit-P AC-8 threshold from raw measurements.

CSV: frames.csv. Manifest: optional JSON describing predeclared exclusions,
phase assignment, telemetry/custody and expected run duration. See README.
Unknown, malformed, or unverified evidence is MISSING, never numeric zero.
This tool evaluates one run. AC-8 additionally requires all title/platform legs
and three independent passing sustained runs; that is explicit in the output.
"""
import argparse,csv,hashlib,json,math,statistics,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('csv',type=Path);p.add_argument('--manifest',type=Path);p.add_argument('--screening',action='store_true');p.add_argument('--json',type=Path);a=p.parse_args()
results={};detail={}
def result(name,status,info):results[name]=status;detail[name]=info
try:
 with a.csv.open(newline='') as f: rows=list(csv.DictReader(f))
 manifest=json.loads(a.manifest.read_text()) if a.manifest else {}
except (OSError,ValueError) as e:
 print('MISSING raw evidence:',e);sys.exit(2)
required=('frame','emu_ns','period_ns','exec_ns','instructions','audio_frames','underruns','overruns','video_hash','audio_hash','wall_ns','instrument_ns','status')
valid=[]
try:
 for row in rows:
  for field in required:
   if field not in row or row[field]=='':raise ValueError(f'absent {field}')
  r=dict(row)
  for field in required:
   if field not in ('video_hash','audio_hash','status'):r[field]=int(row[field])
  if r['status']!='OK':
   result('run_integrity','FAIL','explicit core failure record');continue
  if r['period_ns']<=0 or r['exec_ns']<0 or r['wall_ns']<0:raise ValueError('invalid timing')
  valid.append(r)
 if not valid:raise ValueError('empty data')
 if any(y['frame']!=x['frame']+1 or y['emu_ns']<=x['emu_ns'] or y['wall_ns']<x['wall_ns'] for x,y in zip(valid,valid[1:])):raise ValueError('nonmonotonic or missing frames')
 if results.get('run_integrity')!='FAIL':result('run_integrity','PASS',f'{len(valid)} complete frame records')
except (KeyError,ValueError) as e:
 if results.get('run_integrity')!='FAIL':result('run_integrity','MISSING',str(e))
 valid=[]
warm=0 if a.screening else 600;duration=300 if a.screening else 1200
exclusions=manifest.get('predeclared_exclusions')
# [] must be explicitly declared. Omission is not permission to discard rows.
if exclusions is None:result('predeclared_exclusions','MISSING','manifest must explicitly declare intervals, including []')
elif not isinstance(exclusions,list):result('predeclared_exclusions','MISSING','invalid intervals')
else:
 try:
  for e in exclusions:
   if not isinstance(e['reason'],str) or not e['reason'] or e['start_ns']>=e['end_ns']:raise ValueError()
  result('predeclared_exclusions','PASS',f'{len(exclusions)} declared intervals')
 except (TypeError,ValueError,KeyError):result('predeclared_exclusions','MISSING','invalid intervals');exclusions=None
steady=[r for r in valid if warm*1e9<r['emu_ns']<=(warm+duration)*1e9 and not any(e['start_ns']<=r['emu_ns']<e['end_ns'] for e in (exclusions or []))]
result('duration','PASS' if valid and valid[-1]['emu_ns']>=(warm+duration)*1e9 else 'MISSING',f'requires {warm}s warm-up + {duration}s measured, no silent shortening')
def quantile(x,p):
 # Nearest-rank quantile, predeclared (no interpolation).
 return sorted(x)[max(0,math.ceil(len(x)*p)-1)]
for name,q,threshold in [('p95',.95,.80),('p99',.99,.90),('p99_9',.999,1.00)]:
 if not steady:result(name,'MISSING','no steady window')
 else:
  v=quantile([r['exec_ns']/r['period_ns'] for r in steady],q);result(name,'PASS' if v<=threshold else 'FAIL',f'{v:.8f} T <= {threshold} T')
if steady:
 consecutive=maximum=misses=0
 for r in steady:
  miss=r['exec_ns']>r['period_ns'];misses+=miss;consecutive=consecutive+1 if miss else 0;maximum=max(maximum,consecutive)
 result('consecutive_deadline_misses','PASS' if maximum<3 else 'FAIL',f'misses={misses}; maximum streak={maximum}')
 result('core_audio','PASS' if all(r['underruns']==0 and r['overruns']==0 for r in steady) else 'FAIL','cumulative core counters; boot reset/exclusion must be separately justified')
 first,last=steady[0],steady[-1];guest=last['emu_ns']-first['emu_ns'];wall=last['wall_ns']-first['wall_ns']
 drift=abs(guest-wall)/guest if guest>0 and wall>0 else None
 result('guest_wall_drift','MISSING' if drift is None else 'PASS' if drift<=.001 else 'FAIL','missing span' if drift is None else f'{drift:.8%} <= 0.1%')
 ratios=[r['instrument_ns']/r['period_ns'] for r in steady]
 result('instrumentation_overhead','MISSING',f'hash-only p95 {quantile(ratios,.95):.8f} T; complete paired measurement-on/off raw evidence required, not a manifest boolean')
else:
 for n in ('consecutive_deadline_misses','core_audio','guest_wall_drift','instrumentation_overhead'):result(n,'MISSING','no steady window')
# Matched-phase cost: per-frame phase labels must come from a predeclared script
# checkpoint assignment file. A missing phase label must never become a phase 0.
phases=manifest.get('phase_file');phase_map={}
if phases:
 try:
  path=(a.manifest.parent/ phases) if a.manifest else Path(phases)
  with path.open(newline='') as f:phase_map={int(r['frame']):r['phase'] for r in csv.DictReader(f)}
 except (OSError,KeyError,ValueError):phase_map={}
if a.screening:result('matched_phase_last_first','MISSING','sustained criterion not decided by screening')
elif not phase_map or any(r['frame'] not in phase_map for r in steady):result('matched_phase_last_first','MISSING','verified phase labels needed for every measured frame')
else:
 first_window=[r for r in steady if r['emu_ns']<=(warm+300)*1e9];last_window=[r for r in steady if r['emu_ns']>(warm+duration-300)*1e9]
 phases1={phase_map[r['frame']] for r in first_window};phases2={phase_map[r['frame']] for r in last_window};common=phases1&phases2
 ratios=[]
 for phase in common:
  x=[r['exec_ns']/r['period_ns'] for r in first_window if phase_map[r['frame']]==phase];y=[r['exec_ns']/r['period_ns'] for r in last_window if phase_map[r['frame']]==phase]
  if x and y and statistics.mean(x)>0:ratios.append(statistics.mean(y)/statistics.mean(x))
 result('matched_phase_last_first','MISSING' if not ratios or phases1!=phases2 else 'PASS' if all(abs(r-1)<=.10 for r in ratios) else 'FAIL',f'phase mean normalized cost ratios={ratios}; every matched phase within ±10%')
# Independent frontend/output telemetry must name raw evidence with a SHA-256.
def telemetry(section):
 t=manifest.get(section)
 if not isinstance(t,dict):return None
 try:
  raw=(a.manifest.parent/t['raw_file']) if a.manifest else Path(t['raw_file'])
  if hashlib.sha256(raw.read_bytes()).hexdigest()!=t['sha256']:return None
  if not t.get('method'):return None
  # Counts/observations are recomputed from the hashed raw statistics export,
  # never accepted as numbers copied into a manifest. Unsupported log formats
  # remain missing; ordinary RA logs do not necessarily expose these counters.
  raw_data=json.loads(raw.read_text())
  if not isinstance(raw_data,dict):return None
  raw_data['_method']=t['method']
  if raw_data.get('start_ns')!=int(warm*1e9) or raw_data.get('end_ns')!=int((warm+duration)*1e9):return None
  return raw_data
 except (OSError,KeyError,TypeError,ValueError):return None
front=telemetry('frontend')
try:
 total=front['expected_presentations'];missed=front['missed_presentations'];repeated=front['legitimate_repeated_frames']
 if not all(isinstance(v,int) and v>=0 for v in (total,missed,repeated)) or total<=0 or missed>total or repeated>total:raise ValueError()
 v=missed/total;result('frontend_missed_presentations','PASS' if v<.001 else 'FAIL',f'{v:.8%}; legitimate repeats={repeated}, distinct from misses')
except (TypeError,KeyError,ValueError):result('frontend_missed_presentations','MISSING','raw RetroArch telemetry and legitimate-repeat distinction absent/unverified')
audio=telemetry('independent_audio')
try:
 if not isinstance(audio['core_caused_underruns'],int) or not isinstance(audio['core_caused_overruns'],int) or not isinstance(audio['audible_discontinuities'],int):raise ValueError()
 counts=[audio[k] for k in ('core_caused_underruns','core_caused_overruns','audible_discontinuities')]
 if any(v<0 for v in counts):raise ValueError()
 result('independent_audio_output','PASS' if counts==[0,0,0] else 'FAIL',f'independent counts={counts}')
except (TypeError,KeyError,ValueError):result('independent_audio_output','MISSING','independent output verification absent/unverified')
platform=telemetry('platform')
result('platform_conditions','PASS' if platform and all(k in platform for k in ('device','temperatures','frequencies','performance_settings')) else 'MISSING','record hardware, temperatures, clocks and fan/performance settings with raw evidence')
active=telemetry('gameplay')
result('active_gameplay','PASS' if active and active.get('substantial_active_gameplay') is True else 'MISSING','representative gameplay/checkpoints require independent inspection')
result('repetitions_and_coverage','MISSING','use evaluate_suite.py for four titles × both platforms × three sustained runs plus alternating-rate screening')
for name,status in results.items():print(f'{status:7} {name}: {detail[name]}')
print('summary p50 execution/T:',quantile([r['exec_ns']/r['period_ns'] for r in steady],.5) if steady else 'MISSING')
if a.json:a.json.write_text(json.dumps({'criteria':results,'detail':detail},indent=2)+'\n')
sys.exit(1 if 'FAIL' in results.values() else 2 if 'MISSING' in results.values() else 0)
