#!/usr/bin/env python3
"""Recompute all sustained legs and screening coverage. No suite shortcuts."""
import argparse
import json
from pathlib import Path
from evaluate_ac8 import evaluate
from evidence import digest

PLATFORMS=('windows','odin3')
GAMEPLAY=('trial-a','trial-b','trial-c-1','trial-c-2','trial-d')
AUTHORED=('loop','mmu-permissions','ram-smc','framebuffer-dma','stereo-rate-change','floppy-ide','video-low-50','video-low-60','video-640-50','video-640-60','video-800-50','video-800-60')


def suite(path):
    path=Path(path);base=path.parent;m=json.loads(path.read_text());criteria={};detail={};legs=[]
    def record(k,s,d):criteria[k]=s;detail[k]=d
    # Individual recomputation is mandatory; never trust cached PASS summaries.
    identities=set();ids=[]
    for r in m.get('runs',[]):
        csv_path=base/r['csv'];manifest_path=base/r['manifest'];kind=r['kind']
        outcome=evaluate(csv_path,manifest_path,kind=='screening')
        key=(r['platform'],r['workload'],kind,r['repetition'],r['rate'])
        raw=digest(csv_path)
        if key in identities or raw in ids:
            record('independent_runs','FAIL','duplicate leg identity/raw CSV')
        identities.add(key);ids.append(raw)
        values=outcome['criteria'].values()
        # Matched-phase sustained comparison does not apply to screening.
        selected={k:v for k,v in outcome['criteria'].items() if kind!='screening' or k!='matched_phase_last_first'}
        status='FAIL' if 'FAIL' in selected.values() else 'MISSING' if 'MISSING' in selected.values() else 'PASS'
        legs.append(dict(identity=key,status=status,evaluation=outcome,sha256=raw))
    if 'independent_runs' not in criteria:
        record('independent_runs','PASS' if legs else 'MISSING','distinct raw legs required')
    frozen=m.get('frozen_model_rates',{})
    record('frozen_calibration','PASS' if frozen and m.get('calibration_qualified') is True else 'MISSING','empirical AC7 must be qualified; model-wide rate, no per-title choice')
    expected=[(p,w) for p in PLATFORMS for w in GAMEPLAY+AUTHORED]
    for platform,workload in expected:
        sustained=[(r,l) for r,l in zip(m.get('runs',[]),legs) if r['platform']==platform and r['workload']==workload and r['kind']=='sustained']
        models={r['model'] for r,l in sustained}
        sufficient=len(sustained)==3 and {r['repetition'] for r,l in sustained}=={1,2,3} and len(models)==1
        if sufficient:
            model=next(iter(models));sufficient=model in frozen and all(r['rate']==frozen[model] for r,l in sustained)
        statuses=[l['status'] for r,l in sustained]
        status='FAIL' if 'FAIL' in statuses else 'PASS' if sufficient and all(s=='PASS' for s in statuses) else 'MISSING'
        record('sustained_'+platform+'_'+workload,status,'three independent 600s warm +1200s measured legs at one qualified model rate')
        screening=[(r,l) for r,l in zip(m.get('runs',[]),legs) if r['platform']==platform and r['workload']==workload and r['kind']=='screening']
        screening.sort(key=lambda x:x[0].get('order',-1))
        # Screening precedes sustained qualification. Derive its model from
        # its own records so incomplete calibration cannot hide screening data.
        screen_models={r['model'] for r,l in screening}
        rates=m.get('screening_rates',{}).get(next(iter(screen_models)) if len(screen_models)==1 else '',[])
        # Two series of every model-relevant rate, forward and reverse; require
        # a recorded identical warm state/phase declaration for their starts.
        sequence=[r['rate'] for r,l in screening]
        orders=[r.get('order') for r,l in screening]
        okay=len(rates)>=2 and len(set(rates))==len(rates) and sequence==rates+list(reversed(rates)) and len(set(orders))==len(orders) and all(type(x) is int and x>=0 for x in orders) and len({r.get('warm_state_sha256') for r,l in screening})==1 and all(r.get('warm_state_sha256') for r,l in screening) and (not models or models==screen_models)
        statuses=[l['status'] for r,l in screening]
        status='FAIL' if 'FAIL' in statuses else 'PASS' if okay and all(s=='PASS' for s in statuses) else 'MISSING'
        record('screening_'+platform+'_'+workload,status,'300s screening at same recorded warm phase; alternating forward/reverse rate order; every raw leg must qualify')
    record('individual_leg_results','FAIL' if any(l['status']=='FAIL' for l in legs) else 'MISSING' if not legs or any(l['status']=='MISSING' for l in legs) else 'PASS','every leg independently recomputed')
    return dict(criteria=criteria,detail=detail,legs=legs,qualification='AC8/9 evidence coverage only; no QA/GO/release approval')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('suite',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
    try:out=suite(a.suite)
    except (OSError,ValueError,KeyError,TypeError) as e:print('MISSING:',e);return 2
    for k,s in out['criteria'].items():print(f'{s:7} {k}: {out["detail"][k]}')
    if a.output:a.output.write_text(json.dumps(out,indent=2)+'\n')
    values=out['criteria'].values();return 1 if 'FAIL' in values else 2 if 'MISSING' in values else 0
if __name__=='__main__':raise SystemExit(main())
