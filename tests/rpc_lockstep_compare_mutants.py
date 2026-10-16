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
              'memory.data.tags.va', 'fast.cpu_due_ticks')
    for field in fields:
        yield ('condition-inverted:'+field, anchor, f"(not {anchor} if f['name'] == {field!r} else {anchor})")
        yield ('condition-ignored:'+field, anchor, f"(True if f['name'] == {field!r} else {anchor})")
