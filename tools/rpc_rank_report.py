#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Schema-2 ranking window, aggregate integer/rational counts only.

Record every frame for exact windows. This reports evidence; Fable chooses the
L1/S1 order. Paths and guest identities are never included in the report.
"""
import argparse
import json
from pathlib import Path
p=argparse.ArgumentParser(__doc__);p.add_argument('--profile',type=Path,required=True)
p.add_argument('--first-frame',type=int,default=6400);p.add_argument('--last-frame',type=int,default=7400)
a=p.parse_args()
if not 0<a.first_frame<=a.last_frame:p.error('require 0 < first <= last')
j=json.loads(a.profile.read_text())
if j.get('schema')!=2:raise SystemExit('requires profile schema 2')
records={r['frame']:r for r in j['records']}
try:before=records[a.first_frame-1];after=records[a.last_frame]
except KeyError:raise SystemExit('exact window endpoints missing; record with --profile-interval 1')
if before['ranking_epoch']!=after['ranking_epoch'] or before['epoch']!=after['epoch'] or before['faults']!=after['faults']:raise SystemExit('window crosses a ranking reset; choose one binding')
counts={key:after['ranking'][key]-before['ranking'][key] for key in after['ranking'] if not isinstance(after['ranking'][key],list)}
for key in ('lookup_access_ways','lookup_maintenance_ways'):
    counts[key]=[x-y for x,y in zip(after['ranking'][key],before['ranking'][key])]
instructions=after['ranking_attempts']-before['ranking_attempts']
if instructions<=0 or any(v<0 for v in counts.values() if isinstance(v,int)):raise SystemExit('invalid/nonexecuting interval')
report={'schema':1,'first_frame':a.first_frame,'last_frame':a.last_frame,'attempts':instructions,'counts':counts,
    'AL_share':{'numerator':after['al']-before['al'],'denominator':after['ranking_fetched']-before['ranking_fetched']},
    'condition_pass_share':{'numerator':after['condition_passed']-before['condition_passed'],'denominator':instructions},
    'fetch_share':{'numerator':after['ranking_fetched']-before['ranking_fetched'],'denominator':instructions},
    'per_instruction':{key:{'numerator':value,'denominator':instructions} for key,value in counts.items() if isinstance(value,int)},
    'probes_per_hit':{caller:{'numerator':counts['lookup_'+caller+'_hit_probes'],'denominator':counts['lookup_'+caller+'_hit']} for caller in ('access','maintenance')}}
print(json.dumps(report,indent=2,sort_keys=True))
