#!/usr/bin/env python3
"""Independent per-field sensitivity for generated emitter tables. GPL-2.0-only."""
import copy
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import rpc_lockstep_compare as c
import rpc_lockstep_stream as w


def digest(f,values):
    h=hashlib.sha256();v=values[f['name']];items=[v] if f['count']==1 else v
    for i,v in enumerate(items):
        if c.included(f,values,0,i,'strict'):
            words=[i,*v] if f['type']=='allocation-offset' else [i,v]
            h.update(struct.pack('<'+'Q'*len(words),*words))
    return int.from_bytes(h.digest()[:8],'little')


schema,_=c.load_schema();values=w.defaults(schema)
for name,value in {'fast.memo_valid':1,'fast.pending':0,'memory.active_valid':1,
    'memory.cycle_active':1,'memory.video_active':1,'memory.count':128,'memory.bcount':8,
    'memory.active.count':16,'observer.work_count':16,'observer.in_swi':1}.items():values[name]=value
for f in schema['fields']:
    if f['type']!='allocation-offset' and (f['name'].endswith('.valid') or f['name'] in ('scheduler.armed','observer.owners.armed','observer.watches.live')):
        values[f['name']]=[1]*f['count'] if f['count']>1 else 1
for name,count in [('memory.command.count',128),('memory.buffer.count',8),('observer.work.size',16)]:values[name]=[16 if 'memory.' in name else 256]*count
with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-fields-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
    vector=Path(tmp)/'fields.bin'
    words=[]
    for f in schema['fields']:
        items=[values[f['name']]] if f['count']==1 else values[f['name']]
        words.extend(n for value in items for n in (value if f['type']=='allocation-offset' else [value]))
    vector.write_bytes(struct.pack('<'+'Q'*len(words),*words))
    result=subprocess.run([sys.argv[1],str(vector)],capture_output=True,text=True,timeout=60)
    if result.returncode:raise RuntimeError(('C field digest',result.returncode,result.stderr))
    lines=result.stdout.splitlines()
    if len(lines)!=len(schema['fields']):raise RuntimeError('field coverage count')
    sensitive=0;excluded=0
    for f,line in zip(schema['fields'],lines):
        i,a,b=line.split();expected=digest(f,values)
        old=copy.deepcopy(values[f['name']])
        if f['count']==1:
            if f['type']=='allocation-offset':values[f['name']][0]^=1
            else:values[f['name']]^=1
        elif f['type']=='allocation-offset':values[f['name']][0][0]^=1
        else:values[f['name']][0]^=1
        changed=digest(f,values);values[f['name']]=old
        if int(a,16)!=expected or int(b,16)!=changed:raise RuntimeError(('C/Python field recurrence',f['name']))
        if expected!=changed:sensitive+=1
        else:excluded+=1
    print(f'generated fields={len(lines)} sensitive={sensitive} conditional/excluded/output-boundary={excluded}; actual C emitter digest equals independent Python recurrence')
