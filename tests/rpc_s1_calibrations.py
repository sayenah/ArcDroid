#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compare complete interval bytes using each revision's own calibration client.

The disposable clients add only a hex dump after their original assertions.
Counter ABI differences never cross a runner/core boundary. No fixture changes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
from rpc_ninja_commands import rebuild_commands

p=argparse.ArgumentParser(description=__doc__);p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path)
p.add_argument('--legacy-anchor',action='store_true',help='compare all pre-E0 interval bytes; the anchor has only intrusive mode and no exercise schema')
a=p.parse_args();builds=[a.baseline.resolve(),a.candidate.resolve()]
with tempfile.TemporaryDirectory(prefix='arcdroid-s1b-calibrations-',dir=builds[1].parent) as tmp:
    folder=Path(tmp);clients=[];cores=[];schemas=[]
    for index,build in enumerate(builds):
        cache=(build/'CMakeCache.txt').read_text()
        source=Path(next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')))
        exercise_header=source/'engine/rpc/rpc_exercise.h'
        schemas.append(re.findall(r' X\((\w+)\)',exercise_header.read_text()) if exercise_header.exists() else None)
        ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
        commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
        cc,link=rebuild_commands(commands,source/'tests/rpc_timing_test.c','rpc_timing_test',build,os.name=='nt')
        text=(source/'tests/rpc_timing_test.c').read_text()
        anchor='        uint64_t actual=t.end_ticks-t.start_ticks;'
        if text.count(anchor)!=1:raise RuntimeError('calibration dump anchor drift')
        omitted=''
        if a.legacy_anchor and 'uint64_t start_fetch_hits, end_fetch_hits;' in (source/'engine/rpc/rpc_mem_timing.h').read_text():
            # D-195(5), E0 adds two diagnostic endpoints. Every pre-existing
            # byte remains compared; neither client nor any assertion changes.
            omitted='if(byte>=offsetof(ArcRpcMemInterval,start_fetch_hits) && byte<offsetof(ArcRpcMemInterval,start))continue;'
        text='#include <stddef.h>\n'+text.replace(anchor,anchor+'\n        for(size_t byte=0;byte<sizeof(t);++byte){'+omitted+'printf("%02x",((const unsigned char *)&t)[byte]);}\n        putchar(\'\\n\');')
        unit=folder/(str(index)+'.c');unit.write_text(text);obj=folder/(str(index)+'.o');exe=folder/(str(index)+('.exe' if os.name=='nt' else ''))
        old=cc[cc.index('-o')+1];cc[cc.index('-o')+1]=str(obj);cc[cc.index('-c')+1]=str(unit)
        for flag,value in [('-MF',obj.with_suffix('.d')),('-MT',obj)]:
            if flag in cc:cc[cc.index(flag)+1]=str(value)
        link=[str(obj) if x==old else x for x in link];link[link.index('-o')+1]=str(exe)
        for cmd in (cc,link):
            r=subprocess.run(cmd,cwd=build,capture_output=True,text=True,timeout=120)
            if r.returncode:raise RuntimeError(('dump client build',r.stderr[-2000:]))
        clients.append(exe)
        matches=[build/('arcdroid_libretro_test'+suffix) for suffix in ('.dylib','.so','.dll') if (build/('arcdroid_libretro_test'+suffix)).exists()]
        if len(matches)!=1:raise RuntimeError('missing/ambiguous matching core')
        cores.append(matches[0])
    if a.legacy_anchor and (schemas[0] is not None or schemas[1] is None):
        raise RuntimeError('legacy comparison requires a pre-E0 anchor and a current candidate')
    for model in range(4):
        reference=None;reference_counters=None
        for label,index,extra in [('predecessor',0,[]),('S1 on',1,[]),('S1 off',1,['','--s1-off'])]:
            r=subprocess.run([str(clients[index]),str(cores[index]),str(model),*extra],capture_output=True,timeout=1800)
            if r.returncode:raise RuntimeError((model,label,r.returncode,r.stderr[-2000:]))
            rows=[x for x in r.stdout.splitlines() if re.fullmatch(b'[0-9a-f]+',x)]
            if not rows or (not a.legacy_anchor and len(rows)%2):raise RuntimeError('missing complete interval records')
            if a.legacy_anchor and index==1:rows=rows[::2] # Original client also asserts endpoint == intrusive.
            if reference is None:reference=rows
            elif rows!=reference:raise RuntimeError((model,label,'full interval bytes differ',next((n for n,(x,y) in enumerate(zip(reference,rows)) if x!=y),None)))
            counter_line=re.search(rb'^calibration counters model=\S+ (.+)$',r.stderr,re.M)
            if not counter_line and not a.legacy_anchor:raise RuntimeError('missing calibration counters')
            if a.legacy_anchor:
                print('model='+str(model),label,'legacy-records='+str(len(rows)),
                    'sha256='+hashlib.sha256(b'\n'.join(rows)).hexdigest(),'all-pre-existing-interval-bytes=exact',flush=True)
                continue
            counters=list(map(int,counter_line[1].split()));names=schemas[index]
            if len(counters)!=len(names)+128:raise RuntimeError('counter ABI length mismatch')
            projected=counters.copy()
            if 'service_s1_taken' in names:del projected[names.index('service_s1_taken')]
            if reference_counters is None:reference_counters=projected
            elif projected!=reference_counters:raise RuntimeError((model,label,'pre-existing exercise counter differs',
                [(n,x,y) for n,(x,y) in enumerate(zip(reference_counters,projected)) if x!=y][:8]))
            taken=re.search(rb'S1 taken=(\d+) eligible=(\d+)',r.stderr)
            paths=re.search(rb'endpoint measured intervals: integer=(\d+) rational=(\d+)',r.stderr)
            print('model='+str(model),label,'records='+str(len(rows)),
                'sha256='+hashlib.sha256(b'\n'.join(rows)).hexdigest(),
                ('taken='+taken[1].decode()+' eligible='+taken[2].decode()) if taken else 'predecessor counter ABI',
                ('integer='+paths[1].decode()+' rational='+paths[2].decode()) if paths else '',
                'existing-counters=exact',flush=True)
print('four models: all pre-existing interval bytes equal anchor/on/off; original independent duration and endpoint assertions retained' if a.legacy_anchor else 'four models: complete interval bytes and all pre-existing counters equal predecessor/on/off; independent duration assertions retained')
