#!/usr/bin/env python3
"""Independent synthetic v1 writer (no emitter or comparator imports). GPL-2.0-only."""
import hashlib
import struct


def frame(kind, payload):
    return struct.pack('<BI', kind, len(payload)) + payload


def header(schema_hash, baseline=False, p0=0, p2=0, spans=(), mode=0,
           every=False, full=False, omissions=0, **kwargs):
    if spans: raise ValueError('v1 spans belong online; use stream() or span()')
    h = struct.pack('<I32sI32sQ32s32sI32s', 1, schema_hash,
                    kwargs.get('model', 0), kwargs.get('plan', bytes(32)),
                    kwargs.get('seed', 1), kwargs.get('schedule', bytes(32)),
                    kwargs.get('configuration', bytes(32)),
                    int(baseline) | (2 if every else 0) | (4 if full else 0) |
                    (8 if kwargs.get('p1_off',False) else 0) | mode << 8,
                    kwargs.get('inventory',bytes(32)))
    return b'ARCLKv1\0' + frame(0, h)


def defaults(schema):
    def scalar(f):
        return [f.get('allowed_allocations',[0])[0],0] if f['type']=='allocation-offset' else 0
    return {f['name']: scalar(f) if f['count']==1 else [scalar(f) for _ in range(f['count'])]
            for f in schema['fields']}


def checkpoint(key, schema, values=None, compact=False, edge=0):
    if compact:
        data = struct.pack('<'+'Q'*len(schema['groups']), *([0]*len(schema['groups']) if values is None else values))
    else:
        values = defaults(schema) if values is None else values
        chunks = []
        for f in schema['fields']:
            if f['condition'] is False or f['class']=='output-compared':continue
            v = [values[f['name']]] if f['count'] == 1 else values[f['name']]
            if f['type'] == 'allocation-offset': v = [word for pair in v for word in pair]
            chunks.append(struct.pack('<'+'Q'*len(v), *v))
        data = b''.join(chunks)
    return frame(2 if compact else 1, struct.pack('<7Q', *key, edge) + data)


def event(key, sequence, time=0, agent=0, kind=0, address=0x10000000, size=4, direction=1, value=0):
    g, epoch, k, phase, site, site_sequence = key
    return frame(3, struct.pack('<14Q', g, epoch, sequence, k, phase, site, site_sequence,
                               time, agent, kind, address, size, direction, value))


def page(key, allocation=2, index=0, contents=b'', boundary=0, occurrence=0):
    return frame(4, struct.pack('<10Q', *key, boundary, occurrence, allocation, index) + hashlib.sha256(contents).digest())


def fatal(key, reason=1, text=0):
    key=(*key[:3],5,*key[4:])
    return frame(5, struct.pack('<8Q', *key, reason,text))


def end(checkpoints, events=0, pages=0, p0=None, p2=0, omissions=0, opened=0, closed=0):
    return frame(6, struct.pack('<8Q', checkpoints, events, pages,
        checkpoints if p0 is None else p0,p2,omissions,opened,closed))


def span(key,kind,close=False):
    return frame(8 if close else 7,struct.pack('<7Q',*key,kind))


def stream(schema_hash,records,baseline=False,spans=(),omissions=0,**kwargs):
    """Place online declarations around the represented inclusive k intervals."""
    result=[header(schema_hash,baseline,**kwargs)];active={};pending=iter(sorted(spans,key=lambda x:x[1]));next_span=next(pending,None)
    counts=[0,0,0];phases=[0,0]
    identity=(1,0)
    for record in records:
        key=struct.unpack_from('<6Q',record,5) if record[0]!=3 else None
        if record[0]==3:
            g,e,_,k,phase,site,seq=struct.unpack_from('<7Q',record,5);key=(g,e,k,phase,site,seq)
        identity=key[:2]
        for kind,(first,last) in list(active.items()):
            if key[2]>last:
                result.append(span((*identity,last+1,0,0,0),kind,True));del active[kind]
        while next_span and next_span[1]<=key[2]:
            kind,first,last=next_span;result.append(span((*identity,first,0,0,0),kind));active[kind]=(first,last);next_span=next(pending,None)
        result.append(record)
        if record[0] in (1,2):
            counts[0]+=1
            if key[3] in (0,2):phases[key[3]//2]+=1
        elif record[0] in (3,4):counts[record[0]-2]+=1
    while next_span:
        kind,first,last=next_span;result.append(span((*identity,first,0,0,0),kind));active[kind]=(first,last);next_span=next(pending,None)
    for kind,(_,last) in active.items():result.append(span((*identity,last+1,0,0,0),kind,True))
    result.append(end(*counts,*phases,omissions,len(spans),len(spans)))
    return b''.join(result)
