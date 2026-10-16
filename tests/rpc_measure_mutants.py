#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Endpoint, exercise and schema-2 ranking mutation evidence. Never edits source."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
from rpc_ninja_commands import rebuild_commands
root=Path(__file__).resolve().parents[1];build=Path(sys.argv[1]).resolve()
cache=(build/'CMakeCache.txt').read_text()
ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
exe='.exe' if os.name=='nt' else ''


def check(command):
    r=subprocess.run(command,cwd=build,capture_output=True,text=True,timeout=90)
    if r.returncode:raise RuntimeError(('build error is not a kill',r.stdout[-1000:],r.stderr[-2000:]))


def kill(name,target,source,header=None,content=None):
    compile,link=rebuild_commands(commands,root/source,target,build,os.name=='nt')
    with tempfile.TemporaryDirectory(prefix='arcdroid-1a0-mutant-') as tmp:
        folder=Path(tmp);obj=folder/'mutant.o';binary=folder/(target+exe)
        old_obj=compile[compile.index('-o')+1];compile[compile.index('-o')+1]=str(obj)
        if header:
            h=folder/'rpc_exercise.h';h.write_text(header);compile+=['-include',str(h)]
        if content:
            # Unit source includes the private timing source; preserve that
            # relative path in this temporary authored mutation tree.
            if source=='tests/rpc_endpoint_test.c':
                copy=folder/source;copy.parent.mkdir(parents=True)
                copy.write_text((root/source).read_text())
                timing=folder/'engine/rpc/rpc_mem_timing.c';timing.parent.mkdir(parents=True);timing.write_text(content)
            else:copy=folder/Path(source).name;copy.write_text(content)
            compile[compile.index('-c')+1]=str(copy);compile+=['-I',str((root/source).parent),'-I',str(root/'engine/rpc')]
        link=[str(obj) if x==old_obj else x for x in link];link[link.index('-o')+1]=str(binary)
        check(compile);check(link)
        r=subprocess.run([str(binary)],cwd=build,capture_output=True,text=True,timeout=30)
        if r.returncode==0:raise RuntimeError((name,'SURVIVED'))
        print(name+': killed',flush=True)

for target in ('rpc_endpoint_test','rpc_counter_profile_test','rpc_endpoint_cpu_test','rpc_profile_test'):
    r=subprocess.run([str(build/(target+exe))],capture_output=True,text=True,timeout=30)
    if r.returncode:raise RuntimeError(('original fixture fails',target,r.stderr))
header=(root/'engine/rpc/rpc_exercise.h').read_text()
import re
names=re.findall(r' X\((\w+)\)',header)
if len(names)!=40 or names[0]!='lookup_access_empty' or names[-1]!='rational_cheap':raise RuntimeError('counter schema/fixture mismatch')
# The first X follows a newline rather than a leading space; the regex still
# captures it because each declaration is indented by one space.
for index,name in enumerate(names):
    is_cpu=name.startswith(('tlb_','integer_','rational_'))
    target='rpc_endpoint_cpu_test' if is_cpu else 'rpc_endpoint_test'
    source='engine/rpcemu/src/cp15.c' if name.startswith('tlb_') else 'engine/rpc/rpc_mem_timing.c' if is_cpu else 'tests/rpc_endpoint_test.c'
    mutated=header.replace('    arc_rpc_exercise.values[index] += amount;',f'    if(index=={index})return;\n    arc_rpc_exercise.values[index] += amount;')
    kill('exercise-'+name,target,source,header=mutated)
    # Every ranking increment is independently suppressed while exercise
    # counts remain intact, so the equality assertion is an independent feed check.
    profile=(root/'engine/rpc/rpc_profile.c').read_text()
    profile=profile.replace('if(profile.enabled)profile.counts.ranking.values[index]+=amount;',f'if(profile.enabled && index!={index})profile.counts.ranking.values[index]+=amount;')
    kill('ranking-'+name,'rpc_endpoint_cpu_test' if is_cpu else 'rpc_counter_profile_test','engine/rpc/rpc_profile.c',content=profile)
for maintenance in (0,1):
    index=len(names)+64*maintenance+3
    mutated=header.replace('    arc_rpc_exercise.values[index] += amount;',f'    if(index=={index})return;\n    arc_rpc_exercise.values[index] += amount;')
    kill('hit-way-'+str(maintenance),'rpc_endpoint_test','tests/rpc_endpoint_test.c',header=mutated)
    profile=(root/'engine/rpc/rpc_profile.c').read_text()
    profile=profile.replace('if(profile.enabled)profile.counts.ranking.values[index]+=amount;',f'if(profile.enabled && index!={index})profile.counts.ranking.values[index]+=amount;')
    kill('ranking-hit-way-'+str(maintenance),'rpc_counter_profile_test','engine/rpc/rpc_profile.c',content=profile)

profile=(root/'engine/rpc/rpc_profile.c').read_text()
kill('AL-increment','rpc_profile_test','engine/rpc/rpc_profile.c',content=profile.replace(
    'if((op>>28)==14)++profile.counts.al;', '((void)0);'))
kill('ranking-denominator','rpc_endpoint_cpu_test','engine/rpc/rpc_profile.c',content=profile.replace(
    '++profile.counts.ranking_attempts;', '((void)0);'))
kill('ranking-epoch','rpc_endpoint_cpu_test','engine/rpc/rpc_profile.c',content=profile.replace(
    '++profile.counts.ranking_epoch;', '((void)0);'))
kill('ranking-reset','rpc_endpoint_cpu_test','engine/rpc/rpc_profile.c',content=profile.replace(
    'memset(&profile.counts.ranking,0,sizeof(profile.counts.ranking));', '((void)0);'))
for name,anchor in (('ranking-fetch-denominator','++profile.counts.ranking_fetched;'),('ranking-fetch-reset','profile.counts.ranking_fetched=0;'),('AL-reset','profile.counts.al=0;')):
    kill(name,'rpc_endpoint_cpu_test','engine/rpc/rpc_profile.c',content=profile.replace(anchor,'((void)0);'))
cp15=(root/'engine/rpcemu/src/cp15.c').read_text()
kill('swap-data-fetch','rpc_endpoint_cpu_test','engine/rpcemu/src/cp15.c',content=cp15.replace(
    'prefetch?ARC_RPC_COUNT_tlb_fetch_walk:ARC_RPC_COUNT_tlb_data_walk',
    'prefetch?ARC_RPC_COUNT_tlb_data_walk:ARC_RPC_COUNT_tlb_fetch_walk'))
text=(root/'engine/rpc/rpc_mem_timing.c').read_text()
mutants=[
 ('drain-no-interval-exercise','interval.end_ticks = at; interval.done = true; pending_end = false;\n    interval_exercise_end=arc_rpc_exercise;\n}',
  'interval.end_ticks = at; interval.done = true; pending_end = false;\n}'),
 ('s1-boundary-inclusive','due<visible(m.finish) && due<boundary)','due<visible(m.finish) && due<=boundary)'),
 ('s1-drop-now-lt-due','m.cycle_active && now<due && due<visible(m.finish)','m.cycle_active && due<visible(m.finish)'),
 ('shortcut-fill-any-region','if(packet->fill && memory_region(region(packet->address[0])))','if(packet->fill)'),
 ('stale-previous','previous_completion = at; previous_valid = true;','previous_completion = 0; previous_valid = true;'),
 ('issue-plus-base-start','interval.start_ticks = previous_completion;','interval.start_ticks = arc_rpc_mem_fast.issue_ticks-arc_rpc_mem_fast.base_ticks;'),
 ('premature-drained','if (!measure_drained || (!m.count && !m.bcount))','if (true)'),
 ('lost-fraction','endpoint_complete(arc_rpc_mem_fast.issue_ticks);','endpoint_complete(arc_rpc_mem_fast.issue_floor*arc_rpc_mem_fast.denominator);'),
]
for name,old,new in mutants:
    if text.count(old)!=1:raise RuntimeError((name,'anchor',text.count(old)))
    kill(name,'rpc_endpoint_test','tests/rpc_endpoint_test.c',content=text.replace(old,new))
kill('rational-ignores-services','rpc_endpoint_cpu_test','engine/rpc/rpc_mem_timing.c',content=text.replace(
    'if(exercise_batch.batched) rational-=exercise_batch.services;', 'if(exercise_batch.batched) (void)0;'))
kill('maintenance-caller','rpc_endpoint_test','tests/rpc_endpoint_test.c',content=text.replace(
    'LOOKUP(&m.data, va, true)', 'LOOKUP(&m.data, va, false)'))
kill('swap-service-classes','rpc_endpoint_test','tests/rpc_endpoint_test.c',content=text.replace(
    'else if(m.video_active)kind=ARC_RPC_COUNT_service_video;',
    'else if(m.video_active)kind=ARC_RPC_COUNT_service_busy;'))
kill('invalid-finish-read','rpc_endpoint_test','tests/rpc_endpoint_test.c',content=text.replace(
    'else if(valid && due>=visible(m.finish))','else if(due>=visible(m.finish))'))
kill('wrong-integer-boundary','rpc_endpoint_cpu_test','engine/rpc/rpc_mem_timing.c',content=text.replace(
    'exercise_batch.start=arc_rpc_mem_fast.instructions;', 'exercise_batch.start=arc_rpc_mem_fast.instructions+1;'))
start=text.index('                for(unsigned k=0;k<m.count;++k){')
end=text.index('\n#endif',start)
fill_loop=text[start:end]
before=text[:start]+text[end:]
before=before.replace('            if (ns < boundary) {',
    '#if defined(ARCDROID_TEST_FAULTS)\n'+fill_loop+'\n#endif\n            if (ns < boundary) {')
kill('count-fill-before-commit','rpc_endpoint_test','tests/rpc_endpoint_test.c',content=before)
arm=(root/'engine/rpcemu/src/arm.c').read_text()
old='count = arc_rpc_mem_endpoint_admit(count);'
for name,new in [('one-extra','count = arc_rpc_mem_endpoint_admit(count)+1;'),('missing-null-stop','if(batch)count = arc_rpc_mem_endpoint_admit(count);')]:
    kill(name,'rpc_endpoint_cpu_test','engine/rpcemu/src/arm.c',content=arm.replace(old,new))
# Capture is forbidden from canonicalising. A direct flush at admission is
# currently a no-op after the existing quota flush; enforce the source contract
# too, rather than pretending that equivalent runtime mutant was detected.
def capture_guard(source):
    for function in ('endpoint_drain','endpoint_complete','arc_rpc_mem_endpoint_admit','arc_rpc_mem_endpoint_return'):
        body=source.split(function+'(',2)[-1] if function.startswith('endpoint_') else source.split(function+'(',1)[1]
        # Find the definition and its balanced body, ignoring comments.
        matches=list(re.finditer(r'(?:static void|unsigned|void) '+function+r'\([^;]*?\)\n\{',source,re.S))
        body=source[matches[0].end():];level=1;end=0
        for end,ch in enumerate(body):
            level+=(ch=='{')-(ch=='}')
            if not level:break
        body=re.sub(r'/\*.*?\*/','',body[:end],flags=re.S)
        if re.search(r'\b(?:arc_rpc_mem_batch_flush|next_base|arc_rpc_mem_run)\s*\(',body):return False
    return True
if not capture_guard(text):raise RuntimeError('capture guard fails on original')
if capture_guard(text.replace('if (!endpoint_mode) return count;','if (!endpoint_mode) return count;\n    arc_rpc_mem_batch_flush();')):raise RuntimeError('capture flush mutant survived')
print('capture-induced-flush: killed by no-canonicalisation guard')
print(f'{2*len(names)+29} endpoint/counter mutants killed; no compile errors counted')
