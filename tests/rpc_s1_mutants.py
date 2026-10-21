#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""S1 mutations: runtime kills and explicit equivalent-source dispositions."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from rpc_ninja_commands import rebuild_commands


def source_contract(source):
    start = source.index('bool arc_rpc_mem_batch_service(ArcRpcCpuBatch *batch)\n{')
    body = source[start:]
    flush = body.index('if (arc_rpc_mem_fast.integer_batch) arc_rpc_mem_batch_flush();')
    return (flush < body.index('bool valid=') < body.index('if (batch->clock->now_ns >= boundary)')
            and 'if (m.count || arc_rpc_mem_fast.pending) commands(); /* S1 outer guard */' in source
            and 'if (m.count || arc_rpc_mem_fast.pending) commands(); /* S1 inner guard */' in source)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('build', type=Path)
    p.add_argument('--crosscheck-only',action='store_true')
    a = p.parse_args(); build = a.build.resolve(); root = Path(__file__).resolve().parents[1]
    source = (root/'engine/rpc/rpc_mem_timing.c').read_text()
    assert source_contract(source), 'unmutated source contract'
    cache = (build/'CMakeCache.txt').read_text()
    ninja = next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
    commands = [x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
    compile_base, link_base = rebuild_commands(commands,root/'tests/rpc_s1_test.c','rpc_s1_test',build,os.name=='nt')
    binary = build/('rpc_s1_test.exe' if os.name=='nt' else 'rpc_s1_test')
    subprocess.run([str(binary)],cwd=build,check=True,capture_output=True)
    flush = '    if (arc_rpc_mem_fast.integer_batch) arc_rpc_mem_batch_flush(); /* S1 service-entry flush */\n'
    predicate = 'fast->cpu_due_ns < visible(m.finish) && fast->cpu_due_ns < boundary;'
    mutants = [
        ('omit-flush', flush, '', 'integer_flush'),
        ('flush-after-boundary', flush, '', 'integer_flush false return'),
        ('floor-finish', 'fast->cpu_due_ns < visible(m.finish)', 'fast->cpu_due_ns < m.finish / fast->denominator', 'matrix fraction below ceiling/per-call assertion'),
        ('ticks-finish', 'fast->cpu_due_ns < visible(m.finish)', 'fast->cpu_due_ticks < m.finish', 'matrix same visible ns'),
        ('next-inclusive', predicate, 'fast->cpu_due_ns < visible(m.finish) && fast->cpu_due_ns <= batch->clock->next_ns && fast->cpu_due_ns < batch->limit_ns;', 'matrix next tie'),
        ('limit-inclusive', predicate, 'fast->cpu_due_ns < visible(m.finish) && fast->cpu_due_ns < batch->clock->next_ns && fast->cpu_due_ns <= batch->limit_ns;', 'matrix limit tied video'),
        ('finish-inclusive', 'fast->cpu_due_ns < visible(m.finish)', 'fast->cpu_due_ns <= visible(m.finish)', 'matrix queued at finish'),
        ('drop-pending', '!fast->pending && !m.video_active && m.cycle_active', '!m.video_active && m.cycle_active', 'matrix pending demand behind buffer'),
        ('drop-now', 'batch->clock->now_ns < fast->cpu_due_ns &&', '', 'matrix overdue/due now'),
        ('drop-services', '++exercise_batch.services; /* S1 service, same rational accounting */', '(void)0; /* omitted service */', 'integer_flush rational count'),
        ('ignore-gate', 'bool take_s1 = s1 && !gate && !s1_forced_off;', 'bool take_s1 = s1 && !s1_forced_off;', 'matrix direct measure gate'),
        ('bus-busy-false', 'fast->issue_ticks = fast->cpu_due_ticks;', 'fast->bus_busy = false; fast->issue_ticks = fast->cpu_due_ticks;', 'matrix busy full raw comparison'),
        ('restart-true', 'fast->issue_ticks = fast->cpu_due_ticks;', 'm.restart = true; fast->issue_ticks = fast->cpu_due_ticks;', 'matrix busy full raw comparison'),
        ('count-only-inner', 'if (m.count || arc_rpc_mem_fast.pending) commands(); /* S1 inner guard */', 'if (m.count) commands(); /* S1 inner guard */', 'pending_video final demand'),
    ]
    if a.crosscheck_only:
        mutants=[('skip-issue-defect','fast->issue_ticks = fast->cpu_due_ticks;',
                  'fast->issue_ticks = fast->cpu_due_ticks + 1;', 'matrix busy/field-named differential')]
    for name,old,new,test in mutants:
        if source.count(old)!=1: raise RuntimeError((name,'nonunique anchor',source.count(old)))
        mutated = source.replace(old,new)
        if name=='flush-after-boundary':
            anchor='    if (batch->clock->now_ns >= boundary) return false;'
            mutated=mutated.replace(anchor,anchor+'\n'+flush)
        with tempfile.TemporaryDirectory(prefix='arcdroid-s1b-mutant-',dir=build.parent) as tmp:
            temp = Path(tmp); unit = temp/'tests/rpc_s1_test.c';unit.parent.mkdir()
            unit.write_text((root/'tests/rpc_s1_test.c').read_text())
            timing = temp/'engine/rpc/rpc_mem_timing.c';timing.parent.mkdir(parents=True);timing.write_text(mutated)
            obj=temp/'s1.o';exe=temp/binary.name
            compile=list(compile_base);old_obj=compile[compile.index('-o')+1]
            compile[compile.index('-o')+1]=str(obj);compile[compile.index('-c')+1]=str(unit)
            compile += ['-I',str(root/'engine/rpc')]
            link=[str(obj) if x==old_obj else x for x in link_base];link[link.index('-o')+1]=str(exe)
            for cmd in (compile,link):
                r=subprocess.run(cmd,cwd=build,capture_output=True,text=True,timeout=90)
                if r.returncode: raise RuntimeError(('build errors are not kills',name,r.stderr[-2000:]))
            r=subprocess.run([str(exe)],cwd=build,capture_output=True,text=True,timeout=15)
            if not r.returncode: raise RuntimeError((name,'SURVIVED'))
            if a.crosscheck_only and 'S1.issue_ticks' not in r.stderr:
                raise RuntimeError(('cross-check did not name the injected field',r.stderr))
            print(f'{name}: demonstrated by rpc-s1-directed/{test}',flush=True)
    if a.crosscheck_only:
        return
    # Equivalent runtime changes enforce the required source contract explicitly.
    for name,old,new in (
        ('outer-guard-omitted','if (m.count || arc_rpc_mem_fast.pending) commands(); /* S1 outer guard */','commands(); /* S1 outer guard */'),
        ('inner-guard-omitted','if (m.count || arc_rpc_mem_fast.pending) commands(); /* S1 inner guard */','commands(); /* S1 inner guard */'),
        ('count-only-outer','if (m.count || arc_rpc_mem_fast.pending) commands(); /* S1 outer guard */','if (m.count) commands(); /* S1 outer guard */')):
        assert not source_contract(source.replace(old,new)),name
        print(name+': equivalent behaviour; source contract demonstrated')
    mutated=source.replace(flush,'').replace('    if (batch->clock->now_ns >= boundary) return false;',flush+'    if (batch->clock->now_ns >= boundary) return false;')
    assert not source_contract(mutated)
    print('flush-after-classification: equivalent values; source order contract demonstrated')
    print('deleted logical equivalents: cycle||video under !video; drop !video under cycle invariant; bus_busy=true; restart=false')

if __name__=='__main__':main()
