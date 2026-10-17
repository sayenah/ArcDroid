#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""AC-1/2: rebuild only timing objects, run immutable corpus under each gate.

All variant timing objects retain the linear cross-check. Debug sanitizer RPC
overlays and the repeated firmware hash helper additionally exercise -O2 with all sanitizer/ABI/preprocessor flags
retained; the primary suite keeps its Debug cores. No repository mutation.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
started=time.monotonic()
sys.dont_write_bytecode=True
from rpc_ninja_commands import rebuild_commands,split_command
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);p.add_argument('--variants',nargs='+',default=['default','0','1','2','poison']);p.add_argument('--goldens-only',action='store_true');a=p.parse_args()
build=a.build.resolve();root=Path(__file__).resolve().parents[1]
cache=(build/'CMakeCache.txt').read_text();ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
tests=json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'--show-only=json-v1'],text=True))['tests']
pattern=r'^rpc-(memory-timing-[0-3]|cpu-trace|cmos-trace|video-(paths|trace)-[0-3]|d2-guest-[0-3]|factory-capture-[23]|plan-(matrix-[0-3]|av-[0-3]|devices)|observation-plan)$'
selected=[t for t in tests if re.match(pattern,t['name'])]
if len(selected)!=30:raise RuntimeError(('corpus registration drift',len(selected)))
if a.goldens_only:selected=[t for t in selected if not t['name'].startswith('rpc-memory-timing-') and t['name']!='rpc-observation-plan']
def output_to(argv,obj):
    # Disposable compiles must not replace the original Ninja depfiles.
    argv[argv.index('-o')+1]=str(obj)
    for flag,value in [('-MF',obj.with_suffix('.d')),('-MT',obj)]:
        if flag in argv:argv[argv.index(flag)+1]=str(value)
def run(cmd,timeout=1800):
    r=subprocess.run(cmd,cwd=build,capture_output=True,text=True,timeout=timeout)
    if r.returncode:raise RuntimeError((cmd,r.stdout[-2000:],r.stderr[-4000:]))
    return r
# The primary sanitizer suite runs the actual Debug cores. Its secondary
# all-gates corpus also exercises optimized, sanitized RPC code, avoiding five
# repetitions of the 485-million-attempt Debug SA calibration. Preserve every
# preprocessor, ABI and sanitizer flag. RPC code and the unchanged firmware
# hash helper use -O2; hashing each fresh synthetic ROM dominates at -O0.
# Shipping/measurement identity uses the default targets, never these overlays.
optimized_sanitizer='ARCDROID_SANITIZERS:BOOL=ON' in cache and 'CMAKE_BUILD_TYPE:STRING=Debug' in cache
with tempfile.TemporaryDirectory(prefix='arcdroid-l1-corpus-common-',dir=build) as common_tmp:
    common_objects={};common_hash=[]
    if optimized_sanitizer:
        for target in ['arcdroid_libretro_test_rpc','arcdroid_rpcplan_libretro_rpc','arcdroid_rpc_cpu','arcdroid_core_test']:
            for command in commands:
                argv=split_command(command,os.name=='nt')
                if '-c' not in argv or '-o' not in argv:continue
                obj=argv[argv.index('-o')+1]
                if target+'.dir/' not in obj.replace('\\','/'):continue
                source=Path(argv[argv.index('-c')+1])
                if source.name in ['rpc_mem_timing.c','rpc_crosscheck.c']:continue
                if target=='arcdroid_core_test' and source.name!='hash.c':continue
                replacement=Path(common_tmp)/(target+'-'+source.name+'.o')
                output_to(argv,replacement)
                run(argv+['-O2'])
                if target=='arcdroid_core_test':common_hash.append(str(replacement))
                else:common_objects[obj]=str(replacement)
        if len(common_hash)!=1:raise RuntimeError('missing unique firmware hash object')
        print('secondary sanitizer RPC/hash overlays: -O2; all sanitizer, ABI and preprocessor flags retained',flush=True)
    for variant in a.variants:
        defs=['-DARCDROID_RPC_CROSSCHECK=1']
        if variant=='poison':defs+=['-DARCDROID_RPC_L1_GATE=2','-DARCDROID_RPC_L1_POISON=1']
        elif variant!='default':defs+=['-DARCDROID_RPC_L1_GATE='+variant]
        with tempfile.TemporaryDirectory(prefix='arcdroid-l1-corpus-',dir=build) as tmp:
            folder=Path(tmp);replacements={}
            for target in ['arcdroid_libretro_test','arcdroid_rpcplan_libretro','rpc_cpu_test']:
                compile,link=rebuild_commands(commands,root/'engine/rpc/rpc_mem_timing.c','arcdroid_rpc_cpu' if target=='rpc_cpu_test' else target+'_rpc',build,os.name=='nt')
                old_obj=compile[compile.index('-o')+1];obj=folder/(target+'.o');binary=folder/(target+Path(link[link.index('-o')+1]).suffix)
                output_to(compile,obj)
                # Strip this build's test overrides, retaining its actual toolchain,
                # optimisation, ABI and sanitizer flags, then apply the named leg.
                applied_defs=[x for x in defs if target!='rpc_cpu_test' or x!='-DARCDROID_RPC_CROSSCHECK=1']
                if optimized_sanitizer:applied_defs+=['-O2']
                compile=[x for x in compile if not x.startswith(('-DARCDROID_RPC_L1_GATE=','-DARCDROID_RPC_L1_POISON=','-DARCDROID_RPC_CROSSCHECK='))]+applied_defs
                run(compile)
                cross=folder/(target+'-cross.o');cross_compile=compile.copy();cross_compile[cross_compile.index('-c')+1]=str(root/'engine/rpc/rpc_crosscheck.c');output_to(cross_compile,cross)
                if target!='rpc_cpu_test':run(cross_compile)
                original_binary=link[link.index('-o')+1]
                link=[str(obj) if x==old_obj else common_objects.get(x,x) for x in link]
                link=[x for x in link if 'rpc_crosscheck.c.o' not in x and 'rpc_crosscheck.c.obj' not in x]
                if optimized_sanitizer and target!='rpc_cpu_test':
                    # Define the unchanged helper before the archive, so its
                    # Debug object is not extracted. All other members stay.
                    at=next(i for i,x in enumerate(link) if Path(x).name=='libarcdroid_core_test.a')
                    link[at:at]=common_hash
                link[link.index('-o')+1]=str(binary);
                if target!='rpc_cpu_test':link.append(str(cross))
                run(link)
                replacements[str((build/original_binary).resolve())]=str(binary)
            totals=[0]*173;comparisons=0
            for test in selected:
                cmd=[replacements.get(str((build/x).resolve()),x) for x in test['command']]
                if test['name'].startswith('rpc-memory-timing-'):print(variant+' '+test['name']+': begin',flush=True)
                r=run(cmd)
                if test['name'].startswith('rpc-memory-timing-'):
                    values=re.search(r'^calibration counters model=\S+ (.+)$',r.stderr,re.M)
                    if not values:raise RuntimeError('missing calibration counters')
                    row=list(map(int,values[1].split()))
                    if len(row)!=173:raise RuntimeError(('counter ABI drift',len(row)))
                    totals=[x+y for x,y in zip(totals,row)]
                    match=re.search(r'L1 crosscheck comparisons=(\d+) mismatches=0',r.stderr)
                    if not match:raise RuntimeError('missing per-interval cross-check evidence')
                    comparisons+=int(match[1]);print(f'{variant} {test["name"]} counters '+values[1],flush=True)
                print(variant+' '+test['name']+': exact',flush=True)
            if a.goldens_only:
                print(f'{variant}: 25 immutable goldens exact',flush=True)
                continue
            if comparisons==0:raise RuntimeError('cross-check never exercised')
            if variant in ['1','2','poison'] and (totals[40]==0 or totals[41]==0):raise RuntimeError('hint accepted/rejected floor')
            if variant=='poison' and totals[42]==0:raise RuntimeError('poison fallback floor')
            if totals[43]:raise RuntimeError('exact mirror recheck failures')
            print(f'{variant}: 25 immutable goldens, 4677 intrusive/endpoint intervals, observation quota/mode matrix exact; crosscheck={comparisons} mismatches=0',flush=True)

print(f"L1 corpus elapsed_seconds={time.monotonic()-started:.2f}",flush=True)
