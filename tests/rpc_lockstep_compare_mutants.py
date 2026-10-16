#!/usr/bin/env python3
"""Single-site comparator mutants; never modify installed sources. GPL-2.0-only."""


def mutations():
    yield ('fatal-cpu-in-strict', "mode not in rule['modes'] or rule['name'] in applied or", "rule['name'] in applied or")
    yield ('fatal-cpu-on-later-frames', "mode not in rule['modes'] or rule['name'] in applied or", "mode not in rule['modes'] or")
    yield ('fatal-cpu-rule-inert', "words[i] = rule['field']+'=<excluded:'+rule['name']+'>'", "words[i] = word")
    yield ('ignore-field', "av, bv = a.value[f['name']], b.value[f['name']]", "av, bv = a.value[f['name']], b.value[f['name']]\n            if f['name'] == 'cpu.reg': continue")
    yield ('align-by-index', "if b.optional(ar): ar = a.next(); continue", "if False: ar = a.next(); continue")
    yield ('drop-phase-condition', "if 'phase' in e:\n        return phase", "if 'phase' in e:\n        return 2")
    yield ('accept-truncation', "kind, data = self.frame()\n        if kind == 6:", "try: kind, data = self.frame()\n        except Invalid: self.ended = True; return None\n        if kind == 6:")
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
    yield ('pass-non-reproducing-rerun', "return bool(not original.get('ok') and not rerun.get('ok') and", "return bool(True or not original.get('ok') and not rerun.get('ok') and")
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
        ('end-counts',"require(len(data) == 24 and struct.unpack('<3Q', data) == tuple(self.counts), 'end counts mismatch')"),
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

    for label,check in (('version','sh == schema_hash'),('schema','version == 0')):
        yield ('qa-stale-'+label,"require(version == 0 and sh == schema_hash, 'version/schema hash mismatch')",
               "require("+check+", 'version/schema hash mismatch')")
