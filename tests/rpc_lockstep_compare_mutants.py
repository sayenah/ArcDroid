#!/usr/bin/env python3
"""Single-site comparator mutants; never modify installed sources. GPL-2.0-only."""
import copy
import struct
import rpc_lockstep_stream as w


def target_cases(label, schema, schema_hash):
    """Focused witnesses, also run unchanged against the reference comparator.

    The full independent contract suite still runs once. Do not repeat its
    hundreds of unrelated, full-schema checkpoint constructions per mutant.
    Each family below exercises its changed rule and stops at its first kill.
    """
    base = w.defaults(schema)
    def key(k=1, phase=0): return (1, 0, k, phase, int(phase == 1), 0)
    p0, p1, p2, p3 = [key(phase=p) for p in range(4)]
    def cp(k=p0, values=None): return w.checkpoint(k, schema, base if values is None else values)
    def stream(records, baseline, spans=(), **kwargs):
        return w.stream(schema_hash,records,baseline,spans=spans,**kwargs)
    def compare(left, right): return ('compare', (left, right, schema, schema_hash))
    def records(left, right, spans=()):
        return compare(stream(left, True), stream(right, False, spans))
    def changed(name, values=None, phase=0, index=0):
        a = copy.deepcopy(base if values is None else values)
        b = copy.deepcopy(a)
        if isinstance(b[name], list): b[name][index] = 77
        else: b[name] = 77
        return records([cp(key(phase=phase), a)], [cp(key(phase=phase), b)])

    if label.startswith('fatal-cpu-'):
        text = ('frame=0 cpu=11 fault=0\nframe=1 cpu=22 fault=1\n'
                'frame=2 cpu=33 fault=1\n')
        mode = 'strict' if label == 'fatal-cpu-in-strict' else 'cross-quota'
        return [('stdout_projection', (text, mode, schema))]
    if label.startswith('condition-'):
        name = label.split(':', 1)[1]
        controls = {
            'tlb.physical': ('tlb.valid', 256), 'fast.memo_line': ('fast.memo_valid', 1),
            'memory.finish': ('memory.cycle_active', 1),
            'memory.active.kind': ('memory.active_valid', 1),
            'scheduler.deadlines': ('scheduler.armed', 9),
            'observer.watches.pc': ('observer.watches.live', 8),
            'observer.owners.value': ('observer.owners.armed', 4),
            'observer.work.ns': ('observer.work_count', 1),
            'observer.work.bytes': ('observer.work.size', 16),
            'cpu.reg': ('terminal.fatal', 1),
            'memory.command.address': ('memory.command.count', 128),
            'memory.buffer.kind': ('memory.bcount', 1),
            'memory.instruction.tags.va': ('memory.instruction.tags.valid', 512),
            'memory.data.tags.va': ('memory.data.tags.valid', 512)}
        control, count = controls.get(name, ('fast.pending', 1))
        cases = []
        for enabled in (False, True):
            values = copy.deepcopy(base)
            values[control] = [int(enabled)]*count if count > 1 else int(enabled)
            if control == 'memory.command.count': values['memory.count'] = 1
            if name == 'observer.work.bytes': values['observer.work_count'] = 1
            cases.append(changed(name, values, 2 if control == 'fast.pending' else 0,
                                 16 if name == 'cpu.reg' else 0))
        return cases
    if label == 'ignore-field': return [changed('cpu.reg')]
    if label == 'drop-phase-condition':
        values = copy.deepcopy(base); values['fast.pending'] = 1
        return [changed('fast.cpu_due_ns', values)]
    if label == 'qa-inclusion-asymmetry':
        values = copy.deepcopy(base)
        values['tlb.valid'][0] = 1; values['tlb.physical'][0] = 77
        return [records([cp()], [cp(values=values)])]
    if label == 'align-by-index':
        return [records([cp(), cp(p1)], [cp(p1)], [(0, 1, 1)])]
    if label == 'accept-extra-key':
        return [records([cp()], [cp(), cp(key(2))])]
    if label == 'accept-missing-P1-P3': return [records([cp(p1)], [])]
    if label == 'report-last-divergence':
        values = copy.deepcopy(base); values['cpu.reg'][0] = 1
        return [records([cp(), cp(key(2))], [cp(values=values), cp(key(2), values)])]
    if label == 'pass-non-reproducing-rerun':
        return [('reproduced', ({'ok': False}, {'ok': True}))]
    if label == 'ignore-log-order' or label.startswith('ignore-event-'):
        field = label.removeprefix('ignore-event-')
        field = {'kind-direction': 'kind', 'address-size': 'address',
                 'ignore-log-order': 'value'}.get(field, field)
        opts = dict(time=19, kind=int(field == 'direction'), direction=1)
        other = dict(opts)
        other[field] = {'agent': 1, 'kind': 1, 'direction': 0, 'address': 0x10000004,
                        'size': 2, 'value': 4}[field]
        return [records([w.event(p1, 0, **opts)], [w.event(p1, 0, **other)])]
    if label == 'qa-fatal-reason':
        return [records([w.fatal(p3)], [w.fatal(p3, reason=2)])]
    if label == 'qa-compact-later-groups':
        hashes = [0]*len(schema['groups']); hashes[-1] = 1
        return [records([w.checkpoint(p0, schema, compact=True)],
                        [w.checkpoint(p0, schema, hashes, compact=True)])]
    if label.startswith('qa-header-'):
        kwargs = {'model': 1} if label == 'qa-header-model' else {'configuration': bytes([1])*32}
        return [compare(stream([cp()], True), stream([cp()], False, **kwargs))]
    if label == 'qa-event-sequence':
        return [records([w.event(p1, 1)], [w.event(p1, 1)])]
    if label == 'qa-after-fatal':
        return [records([w.fatal(p3), cp(key(2))], [w.fatal(p3), cp(key(2))])]
    if label.startswith('qa-pointer-'):
        values = copy.deepcopy(base)
        values['rpc_observers.state.resolve'] = {
            'qa-pointer-unmapped': [255, 0], 'qa-pointer-class': [2, 0],
            'qa-pointer-null-offset': [0, 1]}[label]
        return [records([cp(values=values)], [cp(values=values)])]
    if label == 'qa-duplicate-baseline': return [records([cp(), cp()], [cp()])]
    good, candidate = stream([cp()], True), stream([cp()], False)
    if label == 'accept-truncation': return [compare(good, candidate[:-1])]
    if label == 'qa-end-counts':
        # Change only the aggregate checkpoint count. Phase counts remain
        # truthful, so their independent v1 check cannot mask this mutant.
        return [compare(good[:-64]+bytes(8)+good[-56:], candidate[:-64]+bytes(8)+candidate[-56:])]
    if label == 'qa-trailing-bytes': return [compare(good+b'x', candidate+b'x')]
    if label in ('qa-stale-version', 'qa-stale-schema', 'unchecked-sparse-declaration'):
        left, right = bytearray(good), bytearray(candidate)
        if label == 'unchecked-sparse-declaration':
            for data in (left, right): struct.pack_into('<Q', data, len(data)-64+24, 2)
        else:
            offset = 13 if label == 'qa-stale-version' else 17
            left[offset] ^= 1; right[offset] ^= 1
        return [compare(left, right)]
    raise RuntimeError('no focused witness for '+label)


def mutations():
    yield ('fatal-cpu-in-strict', "mode not in rule['modes'] or rule['name'] in applied or", "rule['name'] in applied or")
    yield ('fatal-cpu-on-later-frames', "mode not in rule['modes'] or rule['name'] in applied or", "mode not in rule['modes'] or")
    yield ('fatal-cpu-rule-inert', "words[i] = rule['field']+'=<excluded:'+rule['name']+'>'", "words[i] = word")
    yield ('ignore-field', "av, bv = a.value[f['name']], b.value[f['name']]", "av, bv = a.value[f['name']], b.value[f['name']]\n            if f['name'] == 'cpu.reg': continue")
    yield ('align-by-index', "if b.optional(ar): ar = a.next(); continue", "if False: ar = a.next(); continue")
    yield ('drop-phase-condition', "if 'phase' in e:\n        return phase", "if 'phase' in e:\n        return 2")
    yield ('accept-truncation', "kind, data = self.frame()\n        if kind == 6:", "try: kind, data = self.frame()\n        except Invalid: self.declared=(*self.phase_counts,self.omissions); self.ended = True; return None\n        if kind == 6:")
    yield ('accept-extra-key', "return failure(br.key, 'candidate.key', None, br.key)", "br = b.next(); continue")
    yield ('accept-missing-P1-P3', "if b.optional(ar): ar = a.next(); continue", "if b.optional(ar) or ar.key[3] in (1, 3): ar = a.next(); continue")
    yield ('unchecked-sparse-declaration', "require(tuple(self.phase_counts) == self.declared[:2], 'sparse exit counters mismatch')", "pass # mutant: unchecked exit counters")
    yield ('report-last-divergence', "if diff: return failure(ar.key, *diff)", """if diff:
            result = failure(ar.key, *diff)
            while True:
                ar, br = a.next(), b.next()
                if ar is None or br is None: break
                later = difference(ar, br, schema, mode)
                if later: result = failure(ar.key, *later)
            return result""")
    yield ('ignore-log-order', "elif a.value != b.value:", "elif a.value != b.value and a.kind != 3:")
    yield ('pass-non-reproducing-rerun', "if original.get('ok') or rerun.get('ok') or original.get('key')!=rerun.get('key'):return False", "if False:return False")
    anchor = "bool(evaluate(f['condition'], values, phase, index))"
    fields = ('tlb.physical', 'fast.memo_line', 'memory.finish', 'memory.active.kind',
              'scheduler.deadlines', 'observer.watches.pc', 'observer.owners.value', 'observer.work.ns', 'observer.work.bytes', 'cpu.reg',
              'memory.command.address', 'memory.buffer.kind', 'memory.instruction.tags.va',
              'memory.data.tags.va', 'fast.cpu_due_ticks', 'fast.cpu_due_ns', 'fast.due_floor', 'fast.due_fraction')
    for field in fields:
        yield ('condition-inverted:'+field, anchor, f"(not {anchor} if f['name'] == {field!r} else {anchor})")
        yield ('condition-ignored:'+field, anchor, f"(True if f['name'] == {field!r} else {anchor})")

    # QA event-content variants: retain all other tuple fields, including time.
    for label,indices in (('agent',(8,)),('kind',(9,)),('direction',(12,)),
                          ('value',(13,)),('address',(10,)),('size',(11,)),
                          ('kind-direction',(9,12)),('address-size',(10,11))):
        comparison=f"tuple(v for i,v in enumerate(a.value) if i not in {indices!r}) == tuple(v for i,v in enumerate(b.value) if i not in {indices!r})"
        yield ('ignore-event-'+label, 'elif a.value != b.value:',
               'elif a.value != b.value and not (a.kind == 3 and '+comparison+'):')
    for label,anchor in (
        ('end-counts',"require(totals[:3] == tuple(self.counts), 'end counts mismatch')"),
        ('trailing-bytes',"require(not self.stream.read(1), 'trailing bytes after stream end')"),
        ('event-sequence',"require(seq == expected, 'event sequence discontinuity')"),
        ('after-fatal',"require(not self.fatal, 'records after fatal')"),
        ('pointer-unmapped',"require(all(str(words[i]) in self.schema['allocations'] for i in range(0, count, 2)), 'unmapped pointer identity')"),
        ('pointer-class',"require(all(words[i] in f.get('allowed_allocations', range(256)) for i in range(0,count,2)), 'wrong pointer allocation class')"),
        ('pointer-null-offset',"require(all(words[i] or not words[i+1] for i in range(0, count, 2)), 'nonzero null-pointer offset')"),
        ('duplicate-baseline',"require(order != self.last_order or kind in (1, 2) and not self.flags & 1, 'duplicate baseline/non-checkpoint')")):
        yield ('qa-'+label,anchor,'pass # sensitivity variant')
    for label,index in (('model',2),('configuration',6)):
        anchor="require(a.header[i] == b.header[i], 'header '+label+' mismatch')"
        yield ('qa-header-'+label,anchor,f"require(i == {index} or a.header[i] == b.header[i], 'header '+label+' mismatch')")
    yield ('qa-inclusion-asymmetry','if ai != bi or ai and x != y:','if ai and x != y:')
    yield ('qa-fatal-reason','elif a.value != b.value:','elif a.value != b.value and a.kind != 5:')
    yield ('qa-compact-later-groups','elif a.value != b.value:',
           'elif a.value != b.value and not (a.kind == 2 and a.value[0] == b.value[0]):')

    for label,check in (('version','sh == schema_hash'),('schema','version == 1')):
        yield ('qa-stale-'+label,"require(version == 1 and sh == schema_hash, 'version/schema hash mismatch')",
               "require("+check+", 'version/schema hash mismatch')")
