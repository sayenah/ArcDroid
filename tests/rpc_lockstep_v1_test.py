#!/usr/bin/env python3
"""v1 ordering, bounded online coverage and reproduction. GPL-2.0-only."""
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import threading
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import rpc_lockstep_compare as c
import rpc_lockstep_stream as w


def require(ok,message):
    if not ok:raise RuntimeError(message)


def main():
    schema,sh=c.load_schema();values=w.defaults(schema)
    def key(k=0,phase=0,site=0):return (1,1,k,phase,site,0)
    def cp(k,phase=0,site=0,value=None,edge=0):
        return w.checkpoint(key(k,phase,site),schema,values if value is None else value,edge=edge)
    def compare(left,right,mode='strict',evidence=False,**flags):
        return c.compare(io.BytesIO(w.stream(sh,left,True,mode=c.MODES.index(mode),**flags)),
            io.BytesIO(w.stream(sh,right,False,mode=c.MODES.index(mode),**flags)),schema,sh,mode,evidence)
    # PR #504: each exception is confined to comparison mode (b). Retain
    # sensitivity to actual compared state and to the defined projection.
    def field_change(name, before, after, mode, overrides=None):
        left=copy.deepcopy(values);left.update(overrides or {});left[name]=before
        right=copy.deepcopy(left);right[name]=after
        return compare([cp(0,value=left)],[cp(0,value=right)],mode)
    quota='rpc_session.arc_rpc_test_quota';kind='memory.active_kind'
    for mode in c.MODES:
        result=field_change(quota,1,200,mode)
        require(result['ok']==(mode=='cross-quota'),'R-M2b-O1 mode scope')
        result=field_change(kind,0,1,mode,{'memory.active_valid':0})
        require(result['ok']==(mode=='cross-quota'),'R-M2b-O2 inactive mode scope')
        require(not field_change('arm.inscount',0,1,mode)['ok'],'R-M2b-O3 compared count sensitivity')
    require(not field_change('cpu.reg',values['cpu.reg'],[1]+values['cpu.reg'][1:],'cross-quota',
        {quota:1})['ok'],'R-M2b-O1 guest-state sensitivity')
    for name,overrides in ((kind,{'memory.active_valid':1}),
                           ('memory.active_valid',{kind:1})):
        result=field_change(name,0,1,'cross-quota',overrides)
        require(not result['ok'] and result['field']==name,'R-M2b-O2 live-kind/validity sensitivity')
    projection=schema['cross_quota_projections']['fields']['arm.inscount']
    for phase,raw,credit,expected in (('P0',100,7,107),('P1',100,7,107),
            ('P2',100,8,108),('P3',108,8,108),('P2',4294967295,1,0)):
        inputs={'raw_inscount':raw,'unbooked_completed':credit}
        projected=c.projection_value(projection[phase],inputs)
        require(projected==expected,'R-M2b-O3 defined phase/wrap projection')
        inputs['raw_inscount']=(raw+1)%4294967296
        require(c.projection_value(projection[phase],inputs)!=projected,
                'R-M2b-O3 raw counter delta disappeared in projection')
        require(not field_change('arm.inscount',projected,projected+1,'cross-quota')['ok'],
                'R-M2b-O3 projected completion divergence escaped')
    k=key(0,1,2);entry=w.checkpoint(k,schema,values,edge=1)
    event=w.event(k,0,agent=1,kind=1,direction=0)
    exit=w.checkpoint(k,schema,values,edge=2)
    ordered=[cp(0),entry,event,exit,cp(0,2)]
    require(compare(ordered,ordered)['ok'],'entry/walk/exit order')
    changed=copy.deepcopy(values);changed['cp15.arc_rpc_attributes']=12
    transient=[cp(0),w.checkpoint(k,schema,changed,edge=1),event,exit,cp(0,2)]
    result=compare(ordered,transient)
    require(not result['ok'] and result['field']=='cp15.arc_rpc_attributes','AS-1 entry transient escaped')
    fatal_key=(*k[:3],5,*k[4:])
    interrupted=[cp(0),entry,w.page(fatal_key,allocation=0,boundary=4),w.fatal(fatal_key,1,2)]
    require(compare(interrupted,interrupted)['ok'],'fatal may terminate an open data access')
    for malformed in ([entry], [exit], [entry,entry], [entry,event,cp(0,2)],
                      [w.checkpoint(k,schema,values)], [event,entry,exit],
                      [entry,w.checkpoint((*k[:2],1,*k[3:]),schema,values,edge=2)]):
        try:compare(malformed,malformed)
        except c.Invalid:pass
        else:raise RuntimeError('malformed data entry/exit accepted')
    require(compare([cp(0),event,cp(0,2)],[cp(0),event,cp(0,2)],p1_off=True)['ok'],'P1-off represented evidence')
    try:
        r=c.Reader(io.BytesIO(w.stream(sh,[cp(0),event,entry,exit,cp(0,2)])),schema,sh,'strict')
        while r.next() is not None:pass
    except c.Invalid:pass
    else:raise RuntimeError('entry emitted after walk accepted')
    idle=[w.page((0,1,0,0,0,0),allocation=0,boundary=1,occurrence=i) for i in range(2)]
    require(compare(idle,idle)['ok'],'consecutive idle frame identities')
    try:compare(idle,[idle[0],idle[0]])
    except c.Invalid:pass
    else:raise RuntimeError('duplicate idle boundary accepted')
    try:compare([w.page(key(0,3),allocation=0,boundary=0,contents=b'bad-output-scan')],[])
    except c.Invalid:pass
    else:raise RuntimeError('P3 output scan digest accepted')
    fatal=[w.page(key(0,5),allocation=0,boundary=4),w.fatal(key(0,5),1,2)]
    require(compare(fatal,fatal)['ok'],'fatal pages/reason/text/End')
    try:compare(fatal,fatal+[w.page(key(0,5),allocation=0,boundary=4,occurrence=1)])
    except c.Invalid:pass
    else:raise RuntimeError('pages after fatal accepted')
    # More than the removed header cap, through actual binary pipes. Each
    # sparse span keeps its mandatory helper; compiled spans omit only fetch.
    baseline=[];candidate=[];spans=[]
    for ordinal in range(600):
        records=[w.checkpoint(key(ordinal,p,1 if p==1 else 0),schema,compact=True) for p in (0,1,2)]
        baseline.extend(records)
        if ordinal%2:candidate.extend((records[0],records[2]));spans.append((1,ordinal,ordinal))
        else:candidate.append(records[1]);spans.append((0,ordinal,ordinal))
    streams=[w.stream(sh,baseline,True),w.stream(sh,candidate,spans=spans,omissions=300)]
    pipes=[os.pipe(),os.pipe()];errors=[]
    def producer(fd,data):
        try:
            with os.fdopen(fd,'wb') as f:f.write(data)
        except BaseException as error:errors.append(error)
    threads=[threading.Thread(target=producer,args=(pipes[i][1],data)) for i,data in enumerate(streams)]
    for thread in threads:thread.start()
    with os.fdopen(pipes[0][0],'rb') as a,os.fdopen(pipes[1][0],'rb') as b:result=c.compare(a,b,schema,sh)
    for thread in threads:thread.join(timeout=10)
    require(not errors and result['ok'] and result['max_buffered_records']<=3,'600 online FIFO spans')
    # Reproduction proves the *original* compact mismatch and matching prefix.
    before=copy.deepcopy(values);after=copy.deepcopy(values);after['cp15.ctrl']=7
    def compact(value,k):
        record=c.Record(1,key(k),value)
        return w.checkpoint(key(k),schema,c.compact_groups(record,schema,'strict'),compact=True)
    original=compare([compact(before,0),compact(before,1)],[compact(before,0),compact(after,1)],evidence=True)
    rerun=compare([cp(0,value=before),cp(1,value=before)],[cp(0,value=before),cp(1,value=after)],evidence=True)
    require(c.reproduced(original,rerun) and rerun['field']=='cp15.ctrl','compact/detail reproduction')
    changed=copy.deepcopy(after);changed['cp15.ctrl']=8
    different=compare([cp(0),cp(1)],[cp(0),cp(1,value=changed)],evidence=True)
    earlier=compare([cp(0),cp(1)],[cp(0,value=after),cp(1,value=after)],evidence=True)
    require(not c.reproduced(original,different) and not c.reproduced(original,earlier),'changed/earlier rerun accepted')
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-compat-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
        path=Path(tmp)/'schema.json';modified=copy.deepcopy(schema)
        modified['grounding_commit']='inventory-metadata-only';modified['fields'][0]['citation']='reviewed new location'
        path.write_text(json.dumps(modified));require(c.load_schema(path)[1]==sh,'metadata changed compatibility')
        modified['fields'][0]['count']+=1;path.write_text(json.dumps(modified))
        require(c.load_schema(path)[1]!=sh,'layout absent from compatibility')
        modified=copy.deepcopy(schema)
        host=copy.deepcopy(next(f for f in schema['fields'] if f['condition'] is False))
        rule=copy.deepcopy(next(r for r in schema['reviewed_exclusions'] if r['field']==host['name']))
        host['name']='transport.new_host_only_state';rule['field']=host['name']
        modified['fields'].insert(0,host);modified['reviewed_exclusions'].append(rule)
        path.write_text(json.dumps(modified));require(c.load_schema(path)[1]==sh,'host-only field changed wire compatibility')
        require(w.checkpoint(key(),modified,w.defaults(modified))==cp(0),'host-only field changed detail wire layout')
    vectors=json.loads((ROOT/'tests/rpc_lockstep_observer_vectors.json').read_text())
    for vector in vectors['vectors']:
        for side in ('cheap','observed'):
            v=vector[side];n=v['instruction']+(v['instructions']-v['batch_start']-v['accounted'] if v['batch_live'] and v['observers_active'] else 0)
            require(n==vector['expected'],vector['name'])
    print('v1: O1/O2/O3 mode scope and sensitivity, entry transient, fatal ordering, idle identities, 600 FIFO spans, compact/detail reproduction, compatibility and observer vectors PASS')


if __name__=='__main__':main()
