#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Preparation-owned retirement lease changes between real emitter checkpoints."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from rpc_ninja_commands import rebuild_commands
from rpc_lockstep_emitter_test import program,records
import rpc_lockstep_compare as c


def require(ok,message):
    if not ok:raise RuntimeError(message)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path)
    p.add_argument('loader');p.add_argument('core');a=p.parse_args();schema,sh=c.load_schema()
    cache=(a.build/'CMakeCache.txt').read_text(encoding='utf-8')
    ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
    commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=a.build,text=True,encoding='utf-8'))]
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-preparation-cache-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
        root=Path(tmp);rom=root/'points.rom';rom.write_bytes(program())
        def compile_unit(relative,target,text):
            cc,link=rebuild_commands(commands,ROOT/relative,target,a.build,os.name=='nt')
            original=cc[cc.index('-o')+1];source=root/Path(relative).name;obj=source.with_suffix('.o')
            source.write_text(text,encoding='utf-8')
            for flag,value in (('-c',source),('-o',obj),('-MF',obj.with_suffix('.d')),('-MT',obj)):
                if flag in cc:cc[cc.index(flag)+1]=str(value)
            cc+=['-I',str((ROOT/relative).parent)]
            result=subprocess.run(cc,cwd=a.build,capture_output=True,text=True,encoding='utf-8',timeout=120)
            require(result.returncode==0,('fixture compile error, not a kill',result.stderr))
            return original,str(obj),link
        prep='libretro/rpc_preparation.c'
        old,new,link=compile_unit(prep,'arcdroid_rpclockstep_libretro',
            (ROOT/prep).read_text(encoding='utf-8')+'\n'+(ROOT/'tests/rpc_lockstep_preparation_fixture.c').read_text(encoding='utf-8'))
        replacements={old:new}
        host='libretro/rpc_preparation_host.c'
        fixture='''
/* Disposable owning-TU probe, no fixture symbol in repository targets. */
ArcRpcPreparationHost arc_test_lockstep_host_context(ArcRpcPreparation *p,bool change)
{
    static ArcRpcFrontendHost host;
    host.preparation=p;
    if(change){
        host.released=true;host.unloading=true;
        if(!arc_riscpc_host_reset(&host))return (ArcRpcPreparationHost){0};
    }
    return callbacks(&host);
}
'''
        old,new,_=compile_unit(host,'arcdroid_rpclockstep_libretro',
            (ROOT/host).read_text(encoding='utf-8')+fixture)
        replacements[old]=new
        def library(label):
            path=root/(label+Path(a.core).suffix)
            command=[replacements.get(x,x) for x in link];command[command.index('-o')+1]=str(path)
            command=['-Wl,--out-implib,'+str(root/(label+'.dll.a')) if x.startswith('-Wl,--out-implib,') else x for x in command]
            result=subprocess.run(command,cwd=a.build,capture_output=True,text=True,encoding='utf-8',timeout=120)
            require(result.returncode==0,('fixture link error, not a kill',result.stderr))
            return path
        reference=library('reference')
        emitter='engine/rpc/rpc_lockstep.c';source=(ROOT/emitter).read_text(encoding='utf-8')
        anchor='if(tu==ARC_RPC_LS_TU_rpc_observers && ls.preparation_generation!=arc_rpc_lockstep_preparation_generation)'
        require(source.count(anchor)==1,'preparation invalidation mutant anchor')
        old,new,_=compile_unit(emitter,'arcdroid_rpclockstep_libretro_rpc',source.replace(anchor,'if(0)'))
        replacements[old]=new;mutant=library('mutant')
        def run(label,core,uncached=False):
            path=root/(label+'.stream')
            args=[a.loader,str(core),str(rom),'0',str(path),'1000',str(16|(32 if uncached else 0)),
                  '0','0','0','200','preparation-retirement']
            result=subprocess.run(args,capture_output=True,text=True,encoding='utf-8',timeout=60)
            return result,path
        cached,stream=run('cached',reference)
        require(cached.returncode==0,('cached preparation fixture failed',cached.stderr))
        field='rpc_observers.state.binding.opaque.retirement_pending'
        checkpoints=[r for r in records(stream,schema,sh) if r.kind==1]
        require(checkpoints[0].key[3]==0 and checkpoints[0].value[field]==0,
                'retirement lease not initially clear at P0')
        require(checkpoints[1].key[2:5]==(0,1,1) and checkpoints[1].value[field]==1,
                'real preparation consumed callback did not change the next emitted lease')
        prefix='rpc_observers.state.binding.opaque.'
        changed={name.removeprefix(prefix):value for name,value in checkpoints[1].value.items()
                 if name.startswith(prefix)}
        for name in ('content_waiting','defaults_waiting','observations_begun','hd_close_ok',
                     'stopped_window','marker_checked','released','ownership_lost',
                     'draining','drain_started','drain_ok','content_owner.valid',
                     'content_owner.opened','content_owner.held','content_owner.parents_done',
                     'defaults_owner.lost','record.marker_checked','record.marker_present',
                     'defaults.marker_checked','defaults.marker_present','host.queued_reset',
                     'host.released','host.unloading','record.owner','observations.owner'):
            require(checkpoints[0].value[prefix+name]==0 and changed[name]==1,
                    ('new host state not emitted',name,changed.get(name)))
        require(changed['defaults.owner']==2 and changed['drain_frames']==7,
                'typed owner role or bounded drain ordinal missing')
        import hashlib
        expected=int.from_bytes(hashlib.sha256(bytes([0x5a])*240).digest()[:8],'little')
        require(changed['drain_payload']==expected,'live drain payload contents missing')
        ref=(cached.stdout,stream.read_bytes())
        for label,core in (('reference-uncached',reference),('mutant-uncached',mutant)):
            result,path=run(label,core,True)
            require(result.returncode==0 and (result.stdout,path.read_bytes())==ref,
                    ('uncached positive control changed execution or stream',label,result.stderr))
        result,_=run('mutant-cached',mutant)
        require(result.returncode==1 and 'lockstep mutating projection' in result.stderr,
                ('preparation-generation mutant survived or failed for unrelated reason',result.returncode,result.stderr))
        print('retirement_pending and new host/ownership/marker/drain fields changed at next checkpoint; cache on/off identical; '
              'KILLED preparation-generation-invalidation (uncached mutant control passes)')
        # Isolate the host write: no consumed callback or preparation mutation
        # can accidentally rescue a missing host invalidation mark.
        replacements.clear()
        old,new,_=compile_unit(prep,'arcdroid_rpclockstep_libretro',
            (ROOT/prep).read_text(encoding='utf-8')+'\n#define ARC_LOCKSTEP_HOST_ONLY_PROBE\n'+
            (ROOT/'tests/rpc_lockstep_preparation_fixture.c').read_text(encoding='utf-8'))
        replacements[old]=new
        original=(ROOT/host).read_text(encoding='utf-8')
        old,new,_=compile_unit(host,'arcdroid_rpclockstep_libretro',original+fixture)
        replacements[old]=new
        host_reference=library('host-reference')
        anchor='bool arc_riscpc_host_reset(ArcRpcFrontendHost *h)\n{\n    ARC_RPC_HOST_DIRTY();'
        require(original.count(anchor)==1,'host reset dirty mutant anchor')
        old,new,_=compile_unit(host,'arcdroid_rpclockstep_libretro',
            original.replace(anchor,anchor.replace('    ARC_RPC_HOST_DIRTY();',''))+fixture)
        replacements[old]=new
        host_mutant=library('host-mutant')
        result,path=run('host-cached',host_reference)
        require(result.returncode==0,('host reset positive control failed',result.stderr))
        checkpoints=[r for r in records(path,schema,sh) if r.kind==1]
        require(checkpoints[0].value[prefix+'host.queued_reset']==0 and
                checkpoints[1].value[prefix+'host.queued_reset']==1,
                'real host reset did not queue the request in the next checkpoint')
        ref=(result.stdout,path.read_bytes())
        for label,core in (('host-reference-uncached',host_reference),('host-mutant-uncached',host_mutant)):
            result,path=run(label,core,True)
            require(result.returncode==0 and (result.stdout,path.read_bytes())==ref,
                    ('host uncached control changed behaviour',label,result.stderr))
        result,_=run('host-mutant-cached',host_mutant)
        require(result.returncode==1 and 'lockstep mutating projection' in result.stderr,
                ('host reset dirty mutant survived or failed for unrelated reason',result.returncode,result.stderr))
        print('KILLED host-reset-dirty-mark; real queued reset observed; cache on/off and uncached mutant controls identical')


if __name__=='__main__':main()
