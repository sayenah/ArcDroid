#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Owning serializers preserve byte text under both compiler char settings."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from rpc_ninja_commands import rebuild_commands
from rpc_lockstep_emitter_test import program,records
import rpc_lockstep_compare as c

p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path)
p.add_argument('loader');p.add_argument('core');a=p.parse_args();schema,sh=c.load_schema()
cache=(a.build/'CMakeCache.txt').read_text();ninja=next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[entry['command'] for entry in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=a.build,text=True))]
with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-char-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
    root=Path(tmp);rom=root/'points.rom';rom.write_bytes(program());variants=[]
    for setting in ('signed','unsigned'):
        replacements={}
        for relative in ('engine/rpc/rpc_observers.c','engine/rpcemu/src/fdc.c'):
            cc,link=rebuild_commands(commands,ROOT/relative,'arcdroid_rpclockstep_libretro_rpc',a.build,os.name=='nt')
            original=cc[cc.index('-o')+1];source=root/(setting+'-'+Path(relative).name);obj=root/(setting+'-'+Path(relative).stem+'.o')
            source.write_text((ROOT/relative).read_text())
            for flag,value in (('-c',source),('-o',obj),('-MF',obj.with_suffix('.d')),('-MT',obj)):
                if flag in cc:cc[cc.index(flag)+1]=str(value)
            cc+=['-I',str((ROOT/relative).parent),'-f'+setting+'-char']
            subprocess.run(cc,cwd=a.build,check=True,capture_output=True,timeout=120);replacements[original]=str(obj)
        library=root/(setting+Path(a.core).suffix);link=[replacements.get(arg,arg) for arg in link]
        link[link.index('-o')+1]=str(library)
        link=['-Wl,--out-implib,'+str(root/(setting+'.dll.a')) if arg.startswith('-Wl,--out-implib,') else arg for arg in link]
        subprocess.run(link,cwd=a.build,check=True,capture_output=True,timeout=120);variants.append(library)
    streams=[];outputs=[]
    for i,library in enumerate([Path(a.core),*variants]):
        stream=root/f'{i}.stream';streams.append(stream)
        result=subprocess.run([a.loader,str(library),str(rom),'0',str(stream),'100000',str(17 if i==0 else 16),'0','0','0','200','jfd-byte-text'],capture_output=True,text=True,timeout=60)
        if result.returncode:raise RuntimeError(('byte-text fixture',i,result.returncode,result.stderr))
        outputs.append(result.stdout)
    if len(set(outputs))!=1:raise RuntimeError('char setting changed guest execution')
    for path in streams[1:]:
        with streams[0].open('rb') as left,path.open('rb') as right:result=c.compare(left,right,schema,sh)
        if not result['ok']:raise RuntimeError(('compiler char setting leaked into wire',result))
    if not any(record.kind==1 and record.value['rpc_observers.state.launch.command'][4:6]==[128,255] for record in records(streams[0],schema,sh)):
        raise RuntimeError('high-byte fixture was not live in the real serializer')
print('real RPC observer and FDC serializers: signed/unsigned/default char preserve 0x80/0xff text and high-byte JFD metadata')
