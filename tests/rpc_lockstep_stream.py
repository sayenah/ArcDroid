#!/usr/bin/env python3
"""Independent synthetic v0 writer (no emitter or comparator imports). GPL-2.0-only."""
import hashlib
import struct


def frame(kind, payload):
    return struct.pack('<BI', kind, len(payload)) + payload


def header(schema_hash, baseline=False, p0=0, p2=0, spans=(), mode=0,
           every=False, full=False, omissions=0, **kwargs):
    h = struct.pack('<I32sI32sQ32s32sIQQQI', 0, schema_hash,
                    kwargs.get('model', 0), kwargs.get('plan', bytes(32)),
                    kwargs.get('seed', 1), kwargs.get('schedule', bytes(32)),
                    kwargs.get('configuration', bytes(32)),
                    int(baseline) | (2 if every else 0) | (4 if full else 0) | mode << 8,
                    p0, p2, omissions, len(spans))
    return b'ARCLKv0\0' + frame(0, h + b''.join(struct.pack('<BQQ', *x) for x in spans))


def defaults(schema):
    def scalar(f):
        return [f.get('allowed_allocations',[0])[0],0] if f['type']=='allocation-offset' else 0
    return {f['name']: scalar(f) if f['count']==1 else [scalar(f) for _ in range(f['count'])]
            for f in schema['fields']}


def checkpoint(key, schema, values=None, compact=False):
    if compact:
        data = struct.pack('<'+'Q'*len(schema['groups']), *([0]*len(schema['groups']) if values is None else values))
    else:
        values = defaults(schema) if values is None else values
        chunks = []
        for f in schema['fields']:
            v = [values[f['name']]] if f['count'] == 1 else values[f['name']]
            if f['type'] == 'allocation-offset': v = [word for pair in v for word in pair]
            chunks.append(struct.pack('<'+'Q'*len(v), *v))
        data = b''.join(chunks)
    return frame(2 if compact else 1, struct.pack('<6Q', *key) + data)


def event(key, sequence, time=0, agent=0, kind=0, address=0x10000000, size=4, direction=1, value=0):
    g, epoch, k, phase, site, site_sequence = key
    return frame(3, struct.pack('<14Q', g, epoch, sequence, k, phase, site, site_sequence,
                               time, agent, kind, address, size, direction, value))


def page(key, allocation=2, index=0, contents=b''):
    return frame(4, struct.pack('<8Q', *key, allocation, index) + hashlib.sha256(contents).digest())


def fatal(key, reason=1):
    return frame(5, struct.pack('<7Q', *key, reason))


def end(checkpoints, events=0, pages=0):
    return frame(6, struct.pack('<3Q', checkpoints, events, pages))
