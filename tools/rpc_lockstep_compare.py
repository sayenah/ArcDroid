#!/usr/bin/env python3
"""Independent bounded lockstep reader/comparator, contract v1. GPL-2.0-only.
No C, emulator, guest generator or expectation imports. Files and binary pipes.
"""
import argparse
from dataclasses import dataclass
import hashlib
import itertools
import json
from pathlib import Path
import struct
import sys

MAGIC = b'ARCLKv1\0'
MAX_FRAME = 16 * 1024 * 1024
CLASSES = {'canonical', 'phase-conditional', 'measurement', 'batch-local',
           'host-only/excluded', 'diagnostic', 'output-compared'}
MODES = ('strict', 'cross-quota', 'guest-output', 'trace-toggle')
OPS = {'and', 'or', 'not', 'eq', 'ne', 'lt', 'add', 'neg', 'mod', 'div'}
DEFAULT_SCHEMA = Path(__file__).resolve().parents[1] / 'tests/rpc_lockstep_schema.json'
KEY = struct.Struct('<6Q')
HEADER = struct.Struct('<I32sI32sQ32s32sI32s')


class Invalid(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise Invalid(message)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def validate_schema(schema):
    require(schema.get('license') == 'GPL-2.0-only' and schema.get('version') == 1, 'schema version/license')
    require(schema.get('modes') == list(MODES), 'schema modes')
    stdout = schema['stdout_projection']
    require(stdout['prefixes'] and all(isinstance(x,str) and x.endswith('=') for x in stdout['prefixes']), 'stdout prefixes')
    rules = stdout['rules']
    require(len({r['name'] for r in rules}) == len(rules), 'duplicate stdout rule')
    for rule in rules:
        require(rule['modes'] and set(rule['modes']) <= set(MODES)-{'strict'}, 'strict stdout exclusion')
        require(rule['record_prefix'] in stdout['prefixes'] and rule['field'] and '=' not in rule['field'], 'stdout rule field')
        require(rule['first_frame_where'] and all(isinstance(k,str) and isinstance(v,str) for k,v in rule['first_frame_where'].items()), 'stdout rule condition')
        require(rule.get('citation') and rule.get('reason'), 'stdout rule rationale')
    names = [f.get('name') for f in schema['fields']]
    require(all(isinstance(n, str) and n for n in names) and len(names) == len(set(names)), 'duplicate/invalid field name')
    names = set(names)

    def expression(e):
        if type(e) in (bool, int):
            return
        require(isinstance(e, dict) and e, 'condition must be declarative data')
        if 'field' in e:
            require(e['field'] in names and set(e) <= {'field', 'index'}, 'condition references unknown field')
            if 'index' in e and e['index'] != 'i':
                expression(e['index'])
        elif 'index' in e or 'phase' in e:
            require(len(e) == 1 and next(iter(e.values())) is True, 'condition axis')
        else:
            require(len(e) == 1 and next(iter(e)) in OPS, 'unknown condition operator')
            operands = next(iter(e.values()))
            require(isinstance(operands, list) and operands, 'condition operands')
            for item in operands:
                expression(item)
    for f in schema['fields']:
        require(f.get('class') in CLASSES, 'unclassified field ' + f['name'])
        require(f.get('type') in ('u64', 'index', 'allocation-offset'), 'untyped field/pointer ' + f['name'])
        require(type(f.get('count')) is int and 0 < f['count'] <= 262144, 'invalid capacity')
        require(f.get('phases') and set(f['phases']) <= {0, 1, 2, 3}, 'missing phase condition')
        require(bool(f.get('citation')) and bool(f.get('exclusion_reason')), 'missing field rationale/citation')
        require(f.get('group') in schema['groups'], 'unclassified group')
        require(set(f.get('mode_exclusions', {})) <= set(MODES) - {'strict'}, 'strict-mode exclusion')
        require('condition' in f, 'missing condition')
        expression(f['condition'])
        # Class is executable policy, not a documentary tag. Review lists are
        # exact field/condition contracts: a broad exclusion cannot self-authorise.
        require(f['phases'] == [0, 1, 2, 3], 'unreviewed phase restriction ' + f['name'])
        if f['class'] == 'phase-conditional':
            rules = [r for r in schema['reviewed_conditions'] if f['name'] in r['fields']]
            require(len(rules) == 1 and rules[0].get('citation') and
                    rules[0]['condition'] == f['condition'] and rules[0]['phases'] == f['phases'],
                    'unreviewed conditional exclusion ' + f['name'])
        elif f['class'] in ('canonical', 'batch-local', 'measurement'):
            require(f['condition'] is True, 'excluded included-class field ' + f['name'])
            require(not any(f['name'] in r['fields'] for r in schema['reviewed_conditions']),
                    'conditional class changed ' + f['name'])
        elif f['class']=='output-compared':
            require(f['condition'] is True and f['name'] in schema['output_compared']['fields'] and
                    schema['output_compared'].get('citation'),'unreviewed output boundary')
        else:
            rules = [r for r in schema['reviewed_exclusions'] if r['field'] == f['name']]
            require(f['condition'] is False and len(rules) == 1 and
                    rules[0]['class'] == f['class'] and rules[0].get('citation'),
                    'unreviewed permanent exclusion ' + f['name'])
        for mode, reason in f['mode_exclusions'].items():
            rules = [r for r in schema['reviewed_mode_exclusions']
                     if r['mode'] == mode and f['name'] in r['fields']]
            require(len(rules) == 1 and rules[0].get('citation') and rules[0]['reason'] == reason,
                    'unreviewed mode exclusion ' + f['name'])
    projections = schema['cross_quota_projections']
    require(projections.get('citation') and projections.get('owner') and projections['modes']==['cross-quota'] and
            all(name in names for name in projections['fields']), 'driver projection contract')
    for name, rule in projections.get('conditions', {}).items():
        require(name in names and rule.get('citation') and rule.get('reason'), 'cross-quota condition rationale')
        expression(rule['condition'])
        field = next(f for f in schema['fields'] if f['name'] == name)
        require(field.get('mode_conditions', {}) == {'cross-quota': rule['condition']},
                'unreviewed cross-quota condition ' + name)
    def projection_expression(e):
        if type(e) is int: return
        require(isinstance(e,dict) and len(e)==1,'projection expression')
        if 'input' in e:
            require(e['input'] in projections['inputs'],'unknown projection input')
        else:
            op,args=next(iter(e.items()))
            require(op in ('add','sub','mod') and isinstance(args,list) and len(args)>=2 and
                    (op=='add' or len(args)==2),'projection arithmetic')
            if op=='mod':require(type(args[1]) is int and args[1]>0,'projection modulus')
            for arg in args:projection_expression(arg)
    for spec in projections['fields'].values():
        for path in ('batch','null_batch') if 'batch' in spec else (None,):
            phases=spec[path] if path else spec
            require(all(p in phases for p in ('P0','P1','P2','P3')),'projection phases')
            for phase in ('P0','P1','P2','P3'):projection_expression(phases[phase])
        if 'remaining' in spec:projection_expression(spec['remaining'])
    require(sum(f['count'] * (2 if f['type'] == 'allocation-offset' else 1) * 8 for f in schema['fields']) + 48 <= MAX_FRAME, 'detail frame exceeds bound')
    return schema


def load_schema(path=DEFAULT_SCHEMA):
    raw = Path(path).read_bytes()
    schema=validate_schema(json.loads(raw))
    keys=('name','group','type','count','class','phases','condition',
          'allowed_allocations','index_limit','index_values','mode_conditions')
    fields=[]
    for field in schema['fields']:
        if field['condition'] is False:continue
        row={k:field[k] for k in keys if k in field}
        row['mode_exclusions']=sorted(field['mode_exclusions']);fields.append(row)
    layout=dict(fields=fields,groups=schema['groups'],allocations=schema['allocations'],
        wire=schema['wire'],observer={k:v for k,v in schema['observer_projections'].items()
            if k not in ('citation','vectors')},driver=schema['cross_quota_projections']['fields'],reachable=schema['reachable_types'])
    return schema,bytes.fromhex(digest(layout))


def projection_value(expression, inputs):
    """Reference arithmetic only; M2b supplies and serialises actual inputs."""
    if type(expression) is int: return expression
    if 'input' in expression: return inputs[expression['input']]
    op,args=next(iter(expression.items()))
    values=[projection_value(arg,inputs) for arg in args]
    if op=='add':return sum(values)
    return values[0]%values[1] if op=='mod' else values[0]-values[1]


def stdout_projection(text, mode='strict', schema=None):
    """Runner projection; named exclusions apply once, independently per run."""
    require(mode in MODES, 'stdout comparison mode')
    if schema is None:
        schema, _ = load_schema()
    spec = schema['stdout_projection']; applied = set(); lines = []
    for line in text.splitlines():
        if not line.startswith(tuple(spec['prefixes'])):
            continue
        fields = dict(word.split('=',1) for word in line.split() if '=' in word)
        for rule in spec['rules']:
            if (mode not in rule['modes'] or rule['name'] in applied or
                not line.startswith(rule['record_prefix']) or
                not all(fields.get(k)==v for k,v in rule['first_frame_where'].items())):
                continue
            applied.add(rule['name'])
            words = line.split(' ')
            for i, word in enumerate(words):
                if word.startswith(rule['field']+'='):
                    words[i] = rule['field']+'=<excluded:'+rule['name']+'>'
                    break
            line = ' '.join(words)
        lines.append(line)
    return '\n'.join(lines)


def evaluate(e, values, phase, index):
    if type(e) in (bool, int):
        return e
    if 'field' in e:
        value = values[e['field']]
        slot = e.get('index', 0)
        slot = index if slot == 'i' else evaluate(slot, values, phase, index)
        return value[slot] if isinstance(value, list) else value
    if 'phase' in e:
        return phase
    if 'index' in e:
        return index
    name, operands = next(iter(e.items()))
    v = [evaluate(x, values, phase, index) for x in operands]
    if name == 'and': return all(v)
    if name == 'or': return any(v)
    if name == 'not': return not v[0]
    if name == 'eq': return v[0] == v[1]
    if name == 'ne': return v[0] != v[1]
    if name == 'lt': return v[0] < v[1]
    if name == 'add': return sum(v)
    if name == 'neg': return -v[0]
    if name == 'mod': return v[0] % v[1]
    if name == 'div': return v[0] // v[1]
    raise Invalid('unknown condition')


def included(f, values, phase, index, mode):
    return (f['class']!='output-compared' and phase in f['phases'] and mode not in f['mode_exclusions'] and
            bool(evaluate(f['condition'], values, phase, index)) and
            bool(evaluate(f.get('mode_conditions',{}).get(mode,True),values,phase,index)))


@dataclass
class Record:
    kind: int
    key: tuple
    value: object
    edge: int = 0

    def order(self):
        g, epoch, k, phase, site, sequence = self.key
        # Events/pages precede the checkpoint covering them. P1 sequence is
        # global within k, so alternating read/write helpers keep their order.
        rank = {3: 1, 4: -1, 1: 0 if self.edge==1 else 2,
                2: 0 if self.edge==1 else 2, 5: 3, 7: -3, 8: -3}[self.kind]
        tail = self.value[2] if self.kind == 3 else self.value[:4] if self.kind == 4 else ()
        return (g, epoch, k, phase, sequence, site, rank, tail)


class Reader:
    def __init__(self, stream, schema, schema_hash, mode):
        self.stream, self.schema, self.mode = stream, schema, mode
        self.counts = [0, 0, 0]
        self.phase_counts = [0, 0]
        self.last_order = None
        self.last_event = None
        self.last_pages = {}
        self.open_access = None
        self.fatal = False
        self.ended = False
        self.spans = {}
        self.span_end = {}
        self.span_last = {}
        self.span_counts = [0,0]
        require(self.exact(8) == MAGIC, 'bad stream magic')
        kind, data = self.frame()
        require(kind == 0 and len(data) == HEADER.size, 'missing header')
        h = HEADER.unpack_from(data)
        version, sh, model, plan, seed, schedule, config, flags, inventory = h
        require(version == 1 and sh == schema_hash, 'version/schema hash mismatch')
        require(model < 4 and flags & ~0x31f == 0, 'invalid model/flags')
        require((flags >> 8) == MODES.index(mode), 'stream projection mode mismatch')
        self.header = h[:7]
        self.flags, self.declared = flags, None
        self.omissions = 0

    def exact(self, count):
        data = bytearray()
        while len(data) < count:
            part = self.stream.read(count-len(data))
            require(bool(part), 'truncated stream')
            data.extend(part)
        return bytes(data)

    def frame(self):
        kind, length = struct.unpack('<BI', self.exact(5))
        require(0 < length <= MAX_FRAME and 0 <= kind <= 8, 'invalid frame kind/size')
        return kind, self.exact(length)

    def optional(self, record):
        if self.mode in ('cross-quota','guest-output','trace-toggle') and record.key[3] == 3 and record.kind in (1, 2, 4):
            return True
        for kind, first in self.spans.items():
            if first[:2] == record.key[:2] and first[2] <= record.key[2] <= self.span_end.get(kind,(1<<64)-1):
                if kind == 0 and record.kind in (1, 2) and record.key[3] in (0, 2): return True
                if kind == 1 and record.kind in (1, 2) and record.key[3:5] == (1, 1):
                    self.omissions += 1
                    return True
        return False

    def close_span(self, record):
        kind=record.value
        require(kind in self.spans,'span close without open')
        self.span_last[kind]=record.key[:3]
        del self.spans[kind];self.span_end.pop(kind,None)
        self.span_counts[1]+=1

    def next(self):
        if self.ended:
            return None
        kind, data = self.frame()
        if kind == 6:
            require(self.open_access is None, 'End inside data access')
            require(len(data) == 64, 'end width')
            totals=struct.unpack('<8Q',data)
            require(totals[:3] == tuple(self.counts), 'end counts mismatch')
            self.declared=totals[3:6]
            require(tuple(self.phase_counts) == self.declared[:2], 'sparse exit counters mismatch')
            require(not self.spans and totals[6:] == tuple(self.span_counts), 'online span accounting mismatch')
            require(not self.stream.read(1), 'trailing bytes after stream end')
            self.ended = True
            return None
        require(not self.fatal, 'records after fatal')
        edge=0
        if kind in (7,8):
            require(self.open_access is None, 'span inside data access')
            require(len(data)==56,'span width')
            key=KEY.unpack_from(data);span_kind=struct.unpack_from('<Q',data,48)[0]
            require(key[3:]==(0,0,0) and span_kind in (0,1),'invalid span identity')
            require(not self.flags&1,'baseline must be dense')
            if kind==7:
                require(span_kind not in self.spans,'overlapping online spans')
                require(span_kind not in self.span_last or key[:3]>=self.span_last[span_kind], 'regressing online span')
                require(not self.flags&2 or span_kind!=0,'every-instruction schedule cannot be sparse')
                require(not self.flags&4 or span_kind!=1,'full audit cannot omit fetch continuation')
                self.spans[span_kind]=key[:3];self.span_counts[0]+=1
            else:
                require(span_kind in self.spans and key[:2]==self.spans[span_kind][:2] and
                    key[2]>self.spans[span_kind][2],'invalid span close')
                self.span_end[span_kind]=key[2]-1
            return Record(kind,key,span_kind)
        if kind == 3:
            require(len(data) == 112, 'event width')
            value = struct.unpack('<14Q', data)
            g, epoch, seq, k, phase, site, sequence, time, agent, event_kind, address, size, direction, val = value
            key = (g, epoch, k, phase, site, sequence)
            require(agent <= 3 and event_kind <= 1 and size in (1, 2, 4, 8) and direction <= 1, 'event type/agent/width')
            require(event_kind != 0 or direction == 1, 'RAM log must be a write')
            expected = 0 if self.last_event is None or self.last_event[:2] != (g, epoch) else self.last_event[2]+1
            require(seq == expected, 'event sequence discontinuity')
            self.last_event = (g, epoch, seq)
            self.counts[1] += 1
        else:
            require(len(data) >= 48, 'checkpoint key width')
            key = KEY.unpack_from(data)
            payload = data[48:]
            if kind in (1,2):
                require(len(payload)>=8,'checkpoint edge width')
                edge=struct.unpack_from('<Q',payload)[0];payload=payload[8:]
                require(edge in (0,1,2) and (edge==0 or key[3]==1 and key[4] in (2,3)), 'invalid checkpoint edge')
                require(key[3:5] not in ((1,2),(1,3)) or edge in (1,2), 'data access requires entry/exit edge')
            if kind == 1:
                value = {}; offset = 0
                for f in self.schema['fields']:
                    if f['condition'] is False or f['class']=='output-compared':
                        default=(0,0) if f['type']=='allocation-offset' else 0
                        value[f['name']]=default if f['count']==1 else [default]*f['count']
                        continue
                    count = f['count'] * (2 if f['type'] == 'allocation-offset' else 1)
                    require(offset+8*count <= len(payload), 'detail field width ' + f['name'])
                    words = list(struct.unpack_from('<'+'Q'*count, payload, offset)); offset += 8*count
                    if f['type'] == 'allocation-offset':
                        require(all(str(words[i]) in self.schema['allocations'] for i in range(0, count, 2)), 'unmapped pointer identity')
                        require(all(words[i] in f.get('allowed_allocations', range(256)) for i in range(0,count,2)), 'wrong pointer allocation class')
                        require(all(words[i] or not words[i+1] for i in range(0, count, 2)), 'nonzero null-pointer offset')
                        words = [tuple(words[i:i+2]) for i in range(0, count, 2)]
                    if f['type'] == 'index':
                        require(all(word <= f.get('index_limit', (1<<64)-1) for word in words), 'invalid normalized index')
                    value[f['name']] = words[0] if f['count'] == 1 else words
                require(offset == len(payload), 'extra detail bytes')
            elif kind == 2:
                require(len(payload) == 8*len(self.schema['groups']), 'compact group width')
                value = struct.unpack('<'+'Q'*len(self.schema['groups']), payload)
            elif kind == 4:
                require(len(payload) == 64, 'page width')
                boundary,occurrence,alloc,page = struct.unpack_from('<4Q', payload)
                require(boundary<=4 and alloc in (0,2,3,4), 'page allocation/boundary')
                require(boundary!=0 or alloc!=0 or payload[32:]==hashlib.sha256(b'').digest(),'P3 output digest must be empty')
                identity=(key[0],key[1],occurrence,alloc,page)
                prior=self.last_pages.get(boundary)
                require(prior is None or identity>prior,'page occurrence/order regressed')
                if prior is None or identity[:3]!=prior[:3]:
                    require(alloc==0 and page<=1,'missing page-boundary marker')
                self.last_pages[boundary]=identity
                value = (boundary,occurrence,alloc,page,payload[32:].hex()); self.counts[2] += 1
            elif kind == 5:
                require(len(payload) == 16, 'fatal reason width')
                value = struct.unpack('<2Q', payload); self.fatal = True
            else:
                raise Invalid('unexpected frame')
            if kind in (1, 2):
                self.counts[0] += 1
                if key[3] in (0, 2): self.phase_counts[key[3]//2] += 1
        require(key[3] <= 5 and (key[3] in (1,5) and key[4]<=8 or key[4:]==(0,0)) and
                (key[3]!=1 or key[4]>=1) and (kind!=5 or key[3]==5), 'invalid checkpoint key/site')
        if self.open_access is not None:
            if kind == 3:
                require(key == self.open_access, 'event outside open data access')
            elif kind in (4,5):
                require(key[3]==5 and key[:3]+key[4:]==self.open_access[:3]+self.open_access[4:]
                        and (kind==5 or value[0]==4), 'boundary inside data access')
                if kind==5:self.open_access=None
            else:
                require(kind in (1,2) and edge==2 and key==self.open_access, 'data access exit missing/wrong identity')
                self.open_access=None
        elif kind in (1,2) and key[3:5] in ((1,2),(1,3)):
            require(edge==1, 'data access exit without entry')
            self.open_access=key
        elif kind==3 and key[3:5] in ((1,2),(1,3)):
            require(self.flags&8, 'data event without entry')
        record = Record(kind, key, value, edge)
        order = record.order()
        require(kind==4 or self.last_order is None or order >= self.last_order, 'record order regressed')
        # Only checkpoint repeats are legal, never duplicated events/pages.
        require(order != self.last_order or kind in (1, 2) and not self.flags & 1, 'duplicate baseline/non-checkpoint')
        if kind!=4:self.last_order = order
        return record


def difference(a, b, schema, mode):
    if a.edge != b.edge:return ('record.edge',a.edge,b.edge)
    if a.kind != b.kind:
        return ('record.kind', a.kind, b.kind)
    if a.kind == 1:
        for f in schema['fields']:
            av, bv = a.value[f['name']], b.value[f['name']]
            if av == bv:
                continue
            aa = [av] if f['count'] == 1 else av
            bb = [bv] if f['count'] == 1 else bv
            for i, (x, y) in enumerate(zip(aa, bb)):
                ai = included(f, a.value, a.key[3], i, mode)
                bi = included(f, b.value, b.key[3], i, mode)
                if ai != bi or ai and x != y:
                    return (f['name'] + (f'[{i}]' if f['count'] > 1 else ''), x, y)
    elif a.value != b.value:
        if a.kind == 2:
            i = next(i for i, (x, y) in enumerate(zip(a.value, b.value)) if x != y)
            return ('group.'+schema['groups'][i], a.value[i], b.value[i])
        return ({3: 'event', 4: 'page', 5: 'fatal'}[a.kind], a.value, b.value)
    return None


def compact_groups(record,schema,mode):
    """Independent v1 field/group hash recurrence, also used for reproduction."""
    if record.kind==2:return record.value
    groups=[hashlib.sha256() for _ in schema['groups']]
    for f in schema['fields']:
        h=hashlib.sha256();value=record.value[f['name']]
        values=[value] if f['count']==1 else value
        if f['condition'] is False or f['class']=='output-compared':continue
        if mode in f['mode_exclusions']:
            groups[schema['groups'].index(f['group'])].update(h.digest()[:8]);continue
        if f['condition'] is True and mode not in f.get('mode_conditions',{}) and f['type']!='allocation-offset':
            words=itertools.chain.from_iterable(zip(range(f['count']),values))
            h.update(struct.pack('<'+'Q'*(2*f['count']),*words))
            groups[schema['groups'].index(f['group'])].update(h.digest()[:8]);continue
        for i,value in enumerate(values):
            if included(f,record.value,record.key[3],i,mode):
                h.update(struct.pack('<Q',i))
                words=value if f['type']=='allocation-offset' else [value]
                h.update(struct.pack('<'+'Q'*len(words),*words))
        groups[schema['groups'].index(f['group'])].update(h.digest()[:8])
    return tuple(int.from_bytes(h.digest()[:8],'little') for h in groups)


def compare(left, right, schema=None, schema_hash=None, mode='strict', capture_evidence=False):
    if schema is None:
        schema, schema_hash = load_schema()
    a, b = Reader(left, schema, schema_hash, mode), Reader(right, schema, schema_hash, mode)
    require(a.flags & 1 and not b.flags & 1, 'baseline/candidate roles')
    for i, label in ((0, 'version'), (1, 'schema'), (2, 'model'), (3, 'plan'), (4, 'seed'), (5, 'schedule'), (6, 'configuration')):
        if i == 5 and mode == 'cross-quota' or i == 6 and mode == 'guest-output': continue
        require(a.header[i] == b.header[i], 'header '+label+' mismatch')
    previous = None; last_matching = None; peak = 0;prefix=hashlib.sha256()
    ar, br = a.next(), b.next()

    def failure(key, field, av, bv):
        result=dict(ok=False, key=key, phase=key[3] if key else None,
                    site=key[4] if key else None, field=field, baseline=av,
                    candidate=bv, last_matching=last_matching,
                    prefix=prefix.hexdigest(),rerun=[last_matching or key, key], max_buffered_records=peak)
        if capture_evidence and ar and br and ar.kind in (1,2) and br.kind in (1,2) and ar.key==br.key:
            result['compact_baseline']=compact_groups(ar,schema,mode)
            result['compact_candidate']=compact_groups(br,schema,mode)
        return result

    while ar is not None or br is not None:
        peak = max(peak, int(ar is not None)+int(br is not None)+int(previous is not None))
        if br and br.kind==7:
            br=b.next();continue
        if br and br.kind==8:
            if ar and ar.key[:3]<br.key[:3]:
                if b.optional(ar):ar=a.next();continue
                return failure(ar.key,'density',ar.key,None)
            b.close_span(br);br=b.next();continue
        if mode in ('cross-quota','guest-output','trace-toggle') and ar and ar.key[3] == 3 and ar.kind in (1, 2, 4):
            ar = a.next(); continue
        if mode in ('cross-quota','guest-output','trace-toggle') and br and br.key[3] == 3 and br.kind in (1, 2, 4):
            br = b.next(); continue
        if br is not None and previous is not None and br.kind in (1, 2) and br.key == previous.key and br.edge==previous.edge:
            diff = difference(previous, br, schema, mode)
            if diff: return failure(br.key, *diff)
            br = b.next(); continue
        if ar is None or br is not None and br.order() < ar.order():
            return failure(br.key, 'candidate.key', None, br.key)
        if br is None or ar.order() < br.order():
            if b.optional(ar): ar = a.next(); continue
            return failure(ar.key, 'density', ar.key, None)
        diff = difference(ar, br, schema, mode)
        if diff: return failure(ar.key, *diff)
        if capture_evidence:
            value=compact_groups(ar,schema,mode) if ar.kind in (1,2) else ar.value
            prefix.update(json.dumps((ar.kind if ar.kind not in (1,2) else 2,ar.key,ar.edge,value),separators=(',',':')).encode())
        last_matching = ar.key
        if ar.kind in (1, 2): previous = ar
        ar, br = a.next(), b.next()
    require(b.omissions == b.declared[2], 'compiled-fetch omission counter mismatch')
    return dict(ok=True, max_buffered_records=peak)


def reproduced(original, rerun):
    if original.get('ok') or rerun.get('ok') or original.get('key')!=rerun.get('key'):return False
    if original.get('prefix')!=rerun.get('prefix'):return False
    if 'compact_baseline' in original:
        return all(original.get(k)==rerun.get(k) for k in ('compact_baseline','compact_candidate'))
    return all(original.get(k)==rerun.get(k) for k in ('field','baseline','candidate'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline'); p.add_argument('candidate')
    p.add_argument('--schema', type=Path, default=DEFAULT_SCHEMA)
    p.add_argument('--mode', choices=MODES, default='strict')
    p.add_argument('--hash-only', action='store_true')
    p.add_argument('--reproduce', nargs=2, metavar=('BASELINE', 'CANDIDATE'))
    args = p.parse_args()
    schema, sh = load_schema(args.schema)
    def run(paths):
        # '-' can be used by one input; the other may be a named pipe.
        require(paths != ['-', '-'], 'two streams require distinct pipes')
        from contextlib import ExitStack
        with ExitStack() as stack:
            handles = [sys.stdin.buffer if name == '-' else stack.enter_context(open(name, 'rb')) for name in paths]
            return compare(*handles, schema, sh, args.mode, True)
    try:
        result = run([args.baseline, args.candidate])
        if args.reproduce and not reproduced(result, run(args.reproduce)):
            result = dict(ok=False, field='non-deterministic', max_buffered_records=result.get('max_buffered_records', 0))
        if args.hash_only:
            result = {k: v if k in ('ok', 'max_buffered_records') else digest(v) for k, v in result.items()}
        sys.stdout.buffer.write((json.dumps(result, sort_keys=True)+'\n').encode('utf-8'))
        return 0 if result['ok'] else 1
    except (Invalid, OSError, ValueError, KeyError, IndexError, struct.error) as error:
        sys.stdout.buffer.write((json.dumps({'ok': False, 'refused': digest(str(error)) if args.hash_only else str(error)})+'\n').encode('utf-8'))
        return 2


if __name__ == '__main__':
    sys.exit(main())
