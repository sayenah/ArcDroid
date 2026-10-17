#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""M2b AC-3(c,d): compare all non-stub test/measurement code sections."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
from rpc_measure_compare import objects,compare_objects,run,code
from rpc_ninja_commands import split_command


def only_stubs(base,candidate,build,target,readobj):
    """Remove only the three enumerated definitions in a disposable source.

    Comparing the resulting whole code sections avoids disassembly/relocation
    masking. It also detects unrelated changes hidden by the object exception.
    """
    commands=json.loads((build/'compile_commands.json').read_text())
    entry=next(e for e in commands if target+'.dir/' in e['command'].replace('\\','/')
        and Path(e['file']).name=='rpc_session.c')
    original=Path(entry['file']);source=original.read_text()
    anchor='#if !defined(ARCDROID_RPC_LOCKSTEP)\nRETRO_API bool arc_rpc_test_lockstep_sink'
    if source.count(anchor)!=1:raise RuntimeError('enumerated stub definition anchor')
    first=source.index(anchor);last=source.index('#endif',first)+len('#endif')
    source=source[:first]+source[last:]
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-stub-identity-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
        root=Path(tmp);copy=root/'rpc_session.c';obj=root/'session.o';copy.write_text(source)
        command=split_command(entry['command'],os.name=='nt')
        for flag,value in (('-c',copy),('-o',obj),('-MF',root/'session.d'),('-MT',obj)):
            if flag in command:command[command.index(flag)+1]=str(value)
        command+=['-I',str(original.parent)]
        result=subprocess.run(command,cwd=entry['directory'],capture_output=True,text=True,timeout=120)
        if result.returncode:raise RuntimeError(('stub identity fixture compile',result.stderr[-2000:]))
        if code(base,readobj)!=code(obj,readobj):raise RuntimeError(target+': rpc_session differs beyond enumerated refusing definitions')
    print(target+': removing only sink/status/end definitions restores complete base code sections')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base',type=Path,required=True);p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--readobj',required=True)
    p.add_argument('--core',action='append',help='restrict to a built test core (e.g. Android measurement)')
    a=p.parse_args()
    for core in (a.core or ('arcdroid_libretro_test','arcdroid_rpcplan_libretro','arcdroid_rpcmeasure_libretro')):
        for target in (core,core+'_arculator',core+'_rpc'):
            left=objects(a.base,target);right=objects(a.candidate,target)
            if target.endswith('_rpc'):
                session=[k for k in left if k.name in ('rpc_session.c.o','rpc_session.c.obj')]
                if len(session)!=1:raise RuntimeError('missing enumerated session object')
                old=left.pop(session[0]);new=right.pop(session[0])
                # All three functions are the enumerated test refusal surface;
                # implementation is deliberately confined to this one object.
                symbols=run([a.readobj,'--symbols',str(new)])
                for suffix in ('sink','status','end'):
                    if 'arc_rpc_test_lockstep_'+suffix not in symbols:raise RuntimeError('missing refusing stub')
                only_stubs(old,new,a.candidate,target,a.readobj)
                print(core+': rpc_session.o enumerated exception: sink/status/end refusal stubs')
            compare_objects(left,right,a.readobj,'M2b AC-3 '+target)


if __name__=='__main__':main()
