#!/usr/bin/env python3
"""Contract counterexamples, sensitivity and constant buffering. GPL-2.0-only."""
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import threading
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import rpc_lockstep_compare as reference
import rpc_lockstep_stream as w


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def suite(c=reference, stress=False):
    schema, sh = c.load_schema()
    base = w.defaults(schema)
    fields = {f['name']: f for f in schema['fields']}
    checks = 0

    # A-33 / Q-9: only cpu on the first fatal frame, and only cross-quota.
    stdout = ('frame=0 ns=10 cpu=11 idle=0 rate=40 irq=0 fiq=0 fault=0\n'
              'frame=1 ns=20 cpu=22 idle=0 rate=40 irq=0 fiq=0 fault=1\n'
              'regs=0,1 events=2\nvideo=3\npcm=4\nfirst-raster=5\nram=6\ncmos=7\n'
              'frame=2 ns=30 cpu=33 idle=0 rate=40 irq=0 fiq=0 fault=1\n'
              'frame=3 ns=40 cpu=44 idle=0 rate=40 irq=0 fiq=0 fault=0')
    expected = stdout.replace('cpu=22', 'cpu=<excluded:q9_first_fatal_frame_cpu>')
    require(c.stdout_projection(stdout, 'cross-quota', schema) == expected,
            'A-33 projection changed another frame, field or byte')
    checks += 1
    rule = schema['stdout_projection']['rules'][0]
    require(rule['name'] == 'q9_first_fatal_frame_cpu' and 'Q-9' in rule['citation'],
            'A-33 named schema rule must cite Q-9')
    for mode in c.MODES:
        changed = stdout.replace('cpu=22', 'cpu=99')
        equal = c.stdout_projection(stdout, mode, schema) == c.stdout_projection(changed, mode, schema)
        require(equal == (mode == 'cross-quota'), ('A-33 mode scope', mode))
        checks += 1
    for old, new in [('cpu=11','cpu=99'), ('cpu=33','cpu=99'), ('cpu=44','cpu=99'),
                     ('frame=1','frame=9'), ('ns=20','ns=21'),
                     ('cpu=22 idle=0','cpu=22 idle=1'), ('cpu=22 idle=0 rate=40','cpu=22 idle=0 rate=41'),
                     ('fiq=0 fault=1','fiq=1 fault=1'), ('irq=0 fiq=0 fault=1','irq=1 fiq=0 fault=1'),
                     ('fault=1','fault=0'), ('fault=1','fault=10'),
                     ('regs=0,1','regs=0,2'), ('events=2','events=3'),
                     ('video=3','video=9'), ('pcm=4','pcm=9'),
                     ('first-raster=5','first-raster=9'), ('ram=6','ram=9'), ('cmos=7','cmos=9')]:
        changed = stdout.replace(old, new, 1)
        require(c.stdout_projection(stdout, 'cross-quota', schema) !=
                c.stdout_projection(changed, 'cross-quota', schema), ('A-33 overbroad exclusion', old))
        checks += 1
    no_fatal = stdout.replace('fault=1','fault=0')
    require(c.stdout_projection(no_fatal, 'cross-quota', schema) == no_fatal,
            'A-33 changed a run with no fatal')
    require(c.stdout_projection(stdout, 'strict', schema) == stdout,
            'strict stdout must retain fatal cpu')
    checks += 2

    def key(k=1, phase=0, site=0): return (1, 0, k, phase, site, 0)

    def run(records_a, records_b, expected=True, field=None, mode='strict', spans=(), omissions=0, full=False):
        nonlocal checks
        def data(records, baseline):
            counts = [sum(kind in (1, 2) and k[3] == p for kind, k, _ in records) for p in (0, 2)]
            payload = b''.join(data for _, _, data in records)
            return w.header(sh, baseline, *counts, spans=() if baseline else spans,
                            mode=c.MODES.index(mode), full=full, omissions=omissions if not baseline else 0) + payload + w.end(
                                sum(kind in (1, 2) for kind, _, _ in records),
                                sum(kind == 3 for kind, _, _ in records), sum(kind == 4 for kind, _, _ in records))
        result = c.compare(io.BytesIO(data(records_a, True)), io.BytesIO(data(records_b, False)), schema, sh, mode)
        require(result['ok'] == expected, ('wrong verdict', field, result))
        if field is not None and not expected:
            require(result['field'] == field, ('wrong first field', field, result))
        checks += 1
        return result

    def cp(k, v=None): return (1, k, w.checkpoint(k, schema, base if v is None else v))
    def perturb(name, phase=0, setup=None, index=0, mode='strict', should_pass=False):
        a = copy.deepcopy(base)
        if setup: setup(a)
        b = copy.deepcopy(a)
        f = fields[name]
        if f['type'] == 'allocation-offset': replacement = [next((x for x in f.get('allowed_allocations',[1]) if x),1), 4]
        elif f['type'] == 'index': replacement = min(1,f.get('index_limit',1))
        else: replacement = 77
        if f['count'] == 1: b[name] = replacement
        else: b[name][index] = replacement
        return run([cp(key(phase=phase, site=1 if phase==1 else 0), a)],
                   [cp(key(phase=phase, site=1 if phase==1 else 0), b)],
                   should_pass, None if should_pass else name+(f'[{index}]' if f['count'] > 1 else ''), mode)

    def setv(**kw):
        def apply(values):
            for n, v in kw.items(): values[n] = v
        return apply

    # The full schema, not a private test schema, supplies phase/mode conditions.
    run([cp(key())], [cp(key())])
    compact=(2,key(),w.checkpoint(key(),schema,compact=True))
    hashes=[0]*len(schema['groups']);hashes[0]=1
    run([compact],[(2,key(),w.checkpoint(key(),schema,hashes,compact=True))],False,'group.'+schema['groups'][0])
    perturb('cpu.reg', index=16)
    # Every conditional pair from A-10, with included and excluded controls.
    pairs = [
        ('tlb.physical', 'tlb.valid', 256, 0, False),
        ('fast.memo_line', 'fast.memo_valid', 1, 0, False),
        ('memory.finish', 'memory.cycle_active', 1, 0, False),
        ('memory.active.kind', 'memory.active_valid', 1, 0, False),
        ('scheduler.deadlines', 'scheduler.armed', 9, 0, False),
        ('observer.watches.pc', 'observer.watches.live', 8, 0, False),
        ('observer.work.ns', 'observer.work_count', 1, 0, False),
        ('cpu.reg', 'terminal.fatal', 1, 16, True),
        ('memory.command.address', 'memory.command.count', 128, 0, False),
        ('memory.buffer.kind', 'memory.bcount', 1, 0, False),
        ('fast.cpu_due_ticks', 'fast.pending', 1, 0, True),
    ]
    for name, control, count, index, inverted in pairs:
        for enabled in (False, True):
            def setup(v, control=control, count=count, enabled=enabled):
                v[control] = [int(enabled)]*count if count > 1 else int(enabled)
                if control == 'memory.command.count': v['memory.count'] = 1
            perturb(name, 2 if control == 'fast.pending' else 0, setup, index,
                    should_pass=(enabled == inverted))
    # Alternate activation of finish, invalid tags and inactive owner slots.
    perturb('memory.finish', setup=setv(**{'memory.video_active': 1}))
    for prefix in ('memory.instruction.tags', 'memory.data.tags'):
        for enabled in (False, True):
            def setup(v, prefix=prefix, enabled=enabled): v[prefix+'.valid'] = [int(enabled)]*512
            perturb(prefix+'.va', setup=setup, should_pass=not enabled)
            perturb(prefix+'.physical', setup=setup, should_pass=not enabled)
    for enabled in (False, True):
        def setup(v, enabled=enabled): v['observer.owners.armed'] = [int(enabled)]*4
        perturb('observer.owners.value', setup=setup, should_pass=not enabled)
        def work_setup(v, enabled=enabled):
            v['observer.work_count']=1
            v['observer.work.size']=[int(enabled)]*16
        perturb('observer.work.bytes',setup=work_setup,should_pass=not enabled)
    # All phase axes and included classes; mode exclusions must be inert in strict.
    for phase in range(4):
        for name in ('cpu.reg', 'cp15.ctrl', 'fast.instructions', 'fast.fetch_hits', 'memory.stats.hits', 'driver.instruction_work', 'fast.test_measure'):
            perturb(name, phase)
        perturb('fast.memo_line', phase, setv(**{'fast.memo_valid': 1}))
        for field_class in ('diagnostic', 'host-only/excluded'):
            excluded=next(f for f in schema['fields'] if f['class']==field_class and f['condition'] is False)
            perturb(excluded['name'], phase, should_pass=True)
        perturb('fast.cpu_due_ticks', phase, setv(**{'fast.pending': 1}), should_pass=phase == 2)
    def active_observers(v):
        v['observer.owners.armed'] = [1]*4
        v['observer.watches.live'] = [1]*8
        v['observer.work_count'] = 16
        v['observer.work.size'] = [256]*16
    for f in schema['fields']:
        if f['mode_exclusions'] and f['condition'] is not False:
            perturb(f['name'], setup=active_observers)
            for mode in f['mode_exclusions']: perturb(f['name'], setup=active_observers, mode=mode, should_pass=True)
    for f in schema['fields']:
        if f['class'] in ('diagnostic', 'host-only/excluded') and f['condition'] is False:
            perturb(f['name'], should_pass=True)
    # Distinct logical keys and full P1/P3 density, including unexpected records.
    p0, p1, p2, p3 = key(), key(phase=1, site=1), key(phase=2), key(phase=3)
    dense = [cp(k) for k in (p0, p1, p2, p3)]
    run(dense, [cp(p1), cp(p3)], spans=[(0, 1, 1)])
    run(dense, dense + [cp(key(2))], False, 'candidate.key')
    for missing in (p1, p3):
        run(dense, [x for x in dense if x[1] != missing], False, 'density')
        run([x for x in dense if x[1] != missing], dense, False, 'candidate.key')
    run([cp(p0)], [cp(p0), cp(p0)])
    changed = copy.deepcopy(base); changed['cpu.reg'][0] = 1
    run([cp(p0)], [cp(p0), cp(p0, changed)], False, 'cpu.reg[0]')
    first = run([cp(p0), cp(key(2))], [cp(p0, changed), cp(key(2), changed)], False, 'cpu.reg[0]')
    require(first['key'] == p0 and first['last_matching'] is None, 'reported last divergence')
    for bad in ((2, 0, 1, 0, 0, 0), (1, 1, 1, 0, 0, 0)):
        run([cp(p0)], [cp(bad)], False)
    # One shared chronology, sparse packaging, order, timestamp, identity and agent.
    ev = (3, p1, w.event(p1, 0, time=19))
    ev2 = (3, p2, w.event(p2, 1, time=20, kind=1, direction=0))
    stream = [cp(p0), ev, cp(p1), ev2, cp(p2), cp(p3)]
    run(stream, [ev, cp(p1), ev2, cp(p3)], spans=[(0, 1, 1)])
    for opts in ({'time': 20}, {'agent': 1}, {'value': 4}, {'kind': 1, 'direction': 0}):
        different = (3, p1, w.event(p1, 0, **opts))
        run([ev], [different], False, 'event')
    run([ev], [], False, 'density')
    run([], [ev], False, 'candidate.key')
    swapped = [(3, p1, w.event(p1, 0, kind=1, direction=0)), (3, p2, w.event(p2, 1))]
    run([ev, ev2], swapped, False, 'event')
    page = (4, p3, w.page(p3)); otherpage = (4, p3, w.page(p3, contents=b'changed'))
    run([page], [otherpage], False, 'page')
    ft = (5, p3, w.fatal(p3)); ft2 = (5, key(2, 3), w.fatal(key(2, 3)))
    run([page, ft], [page, ft]); run([ft], [ft2], False)
    # A changed represented write restored later still fails its ordered log.
    run([ev, ev2], [(3, p1, w.event(p1, 0, value=99)), ev2], False, 'event')
    require(not c.reproduced(first, {'ok': True}), 'non-reproducing rerun passed')
    require(c.reproduced(first, first), 'reproducing rerun failed')
    # Ordinary compiled fetch omission requires a checked count; full audit forbids it.
    run(dense, [cp(p0), cp(p2), cp(p3)], spans=[(1, 1, 1)], omissions=1)
    # Malformed/truncated streams must refuse, not compare equal.
    good = w.header(sh, True, 1, 0) + cp(p0)[2] + w.end(1)
    candidate = w.header(sh, False, 1, 0) + cp(p0)[2] + w.end(1)
    for cut in (1, 4, 24, len(candidate)-8):
        try: c.compare(io.BytesIO(good), io.BytesIO(candidate[:-cut]), schema, sh)
        except c.Invalid: pass
        else: raise RuntimeError('truncation accepted')
    for offset in (13, 17):
        bad = bytearray(candidate); bad[offset] ^= 1
        try: c.compare(io.BytesIO(good), io.BytesIO(bad), schema, sh)
        except c.Invalid: pass
        else: raise RuntimeError('header mismatch accepted')
    invalid_density=[
        w.header(sh,False,0,0,spans=[(0,1,1)],every=True)+w.end(0),
        w.header(sh,False,1,0,spans=[(1,1,1)],full=True)+cp(p0)[2]+w.end(1),
        w.header(sh,False,1,0,omissions=1)+cp(p0)[2]+w.end(1),
    ]
    for bad in invalid_density:
        try:c.compare(io.BytesIO(good),io.BytesIO(bad),schema,sh)
        except c.Invalid:pass
        else:raise RuntimeError('invalid every-instruction/full-audit/omission declaration accepted')
    counters = bytearray(candidate)
    # Independent wire offset: magic + frame prefix + fields before P0 count.
    import struct
    struct.pack_into('<Q', counters, 8+5+148, 2)
    try: c.compare(io.BytesIO(good), io.BytesIO(counters), schema, sh)
    except c.Invalid: pass
    else: raise RuntimeError('unchecked sparse declaration')
    for badschema in ('class', 'pointer', 'duplicate'):
        mutant = copy.deepcopy(schema)
        if badschema == 'class': del mutant['fields'][0]['class']
        elif badschema == 'pointer': mutant['fields'][0]['type'] = 'address'
        else: mutant['fields'].append(mutant['fields'][0])
        try: c.validate_schema(mutant)
        except c.Invalid: pass
        else: raise RuntimeError('schema mutant accepted: '+badschema)
    if stress:
        for length in (100000, 1000000):
            errors = []
            def producer(fd, baseline):
                try:
                    with os.fdopen(fd, 'wb') as out:
                        out.write(w.header(sh, baseline, length, 0))
                        for k in range(length): out.write(w.checkpoint(key(k), schema, compact=True))
                        out.write(w.end(length))
                except BaseException as error: errors.append(error)
            pipes = [os.pipe(), os.pipe()]
            threads = [threading.Thread(target=producer, args=(wr, i == 0)) for i, (_, wr) in enumerate(pipes)]
            for t in threads: t.start()
            with os.fdopen(pipes[0][0], 'rb') as a, os.fdopen(pipes[1][0], 'rb') as b:
                result = c.compare(a, b, schema, sh)
            for t in threads: t.join()
            require(not errors and result['ok'] and result['max_buffered_records'] <= 3, ('unbounded buffer', result, errors))
            print('pipe records=%d max_buffered_records=%d' % (length, result['max_buffered_records']))
    return checks


def cli_checks():
    schema, sh = reference.load_schema()
    key=(1,0,1,0,0,0)
    values=w.defaults(schema)
    baseline=w.header(sh,True,1,0)+w.checkpoint(key,schema,values)+w.end(1)
    equal=w.header(sh,False,1,0)+w.checkpoint(key,schema,values)+w.end(1)
    values['cpu.reg'][0]=0xface
    changed=w.header(sh,False,1,0)+w.checkpoint(key,schema,values)+w.end(1)
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2a-cli-') as temp:
        paths=[Path(temp)/name for name in ('baseline','candidate','equal')]
        for path,data in zip(paths,(baseline,changed,equal)):path.write_bytes(data)
        command=[sys.executable,'-B',str(ROOT/'tools/rpc_lockstep_compare.py')]
        result=subprocess.run([*command,str(paths[0]),'-'],input=changed,capture_output=True,timeout=30)
        require(result.returncode==1 and json.loads(result.stdout)['field']=='cpu.reg[0]','CLI stdin mismatch')
        for extra,field in (([],None),(['--reproduce',str(paths[0]),str(paths[1])],None),
                            (['--reproduce',str(paths[0]),str(paths[2])],'non-deterministic')):
            result=subprocess.run([*command,str(paths[0]),str(paths[1]),'--hash-only',*extra],capture_output=True,text=True,timeout=30)
            record=json.loads(result.stdout)
            require(result.returncode==1 and record['field']==reference.digest(field or 'cpu.reg[0]'),'CLI rerun/hash-only')
            require(all(type(value) is bool or name=='max_buffered_records' or isinstance(value,str) and len(value)==64
                        for name,value in record.items()),'hash-only exposed values')
    print('CLI stdin, hash-only, reproduced and non-deterministic checks pass')


def main():
    print('comparator cases:', suite(stress='--stress' in sys.argv))
    cli_checks()
    if '--mutants' in sys.argv:
        from rpc_lockstep_compare_mutants import mutations
        original = (ROOT/'tools/rpc_lockstep_compare.py').read_text()
        for label, old, new in mutations():
            require(original.count(old) == 1, ('mutant anchor not unique', label))
            module = type(reference)('lockstep_mutant')
            module.__file__ = str(ROOT/'tools/rpc_lockstep_compare.py')
            sys.modules[module.__name__] = module
            exec(compile(original.replace(old, new), module.__file__, 'exec'), module.__dict__)
            try: suite(module)
            except (RuntimeError, module.Invalid): print('killed comparator', label)
            else: raise RuntimeError('comparator mutant survived: '+label)


if __name__ == '__main__': main()
