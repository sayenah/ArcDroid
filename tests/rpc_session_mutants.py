#!/usr/bin/env python3
"""Recreate defective session boundaries; run behavioural oracles. GPL-2.0-only."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
from rpc_ninja_commands import rebuild_commands,same_path
from rpc_session_hygiene_test import run
build,exe,core,fixtures,session,video=map(Path,sys.argv[1:])
build=build.resolve();exe=exe.resolve();core=core.resolve();fixtures=fixtures.resolve()
root=Path(__file__).resolve().parents[1]
cache=(build/'CMakeCache.txt').read_text()
ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
mutants=[
 ('stopped-identity','engine/arcdroid_engine.c','rpc_identity_clear();arc_lifecycle=false;','rpc_identity_clear();'),
 ('stopped-capabilities','engine/arcdroid_engine.c','arc_lifecycle ? arc_backend->capabilities() : (ArcEngineCapabilities){0}','arc_backend->capabilities()'),
 ('stopped-fault-text','engine/arcdroid_engine.c','if (!arc_lifecycle) { if (message) *message = NULL; return false; }',''),
 ('failed-start-diagnostics','engine/arcdroid_engine.c','if (!ok) arc_backend->stop();','if (!ok) { arc_backend->stop(); arc_lifecycle=false; }'),
 ('rpc-refusal-callback','engine/arcdroid_engine.c','arc_rpc_backend.adf_refusal_callback_set(callback);','(void)callback;'),
 ('rpc-log-callback','engine/arcdroid_engine.c','arc_rpc_backend.set_log_callback(callback ? arc_test_log_proxy : NULL, NULL);','(void)0;'),
 ('frontend-empty-kind','libretro/arcdroid_libretro.c','arc_kind = ARC_SESSION_KIND_EMPTY;','(void)0;'),
 ('rpc-storage-notice-dedup','libretro/arcdroid_libretro.c','if (fresh) {','if (fresh && !arc_rpc_refusal_noticed) {'),
 ('rpc-write-observer','libretro/arcdroid_libretro.c','arc_io_set_write_observer(arc_disc_write_observer);','(void)0;',3),
 ('rpc-max-geometry','libretro/arcdroid_libretro.c','info->geometry.max_width = 4096u;','info->geometry.max_width = arc_kind == ARC_SESSION_KIND_RISCPC ? 2048u : 4096u;'),
 ('raw-deleted-refusal','engine/rpc/rpc_floppy.c','{refused(d,r,"deleted-data");return ARC_RPC_FLOPPY_WRITE_PROTECT;}','{return ARC_RPC_FLOPPY_WRITE_PROTECT;}'),
 ('raw-storage-warning','engine/rpc/rpc_floppy.c','storage_failures|=1u<<drive;','',2),
]
with tempfile.TemporaryDirectory(prefix='arc-session-mutants-') as directory:
    temp=Path(directory)
    for row in mutants:
        label,relative,old,new=row[:4];expected=row[4] if len(row)>4 else 1
        source=root/relative;original=source.read_text()
        if original.count(old)!=expected:raise RuntimeError(f'{label}: anchor count {original.count(old)} != {expected}')
        target='arcdroid_rpcplan_libretro_rpc' if relative.startswith('engine/rpc/') else 'arcdroid_rpcplan_libretro'
        cc,link=rebuild_commands(commands,source,target,build,os.name=='nt')
        out=temp/(source.stem+'.o');mutated=temp/source.name;library=temp/core.name
        mutated.write_text(original.replace(old,new))
        previous=cc[cc.index('-o')+1];cc[cc.index('-o')+1]=str(out);cc[cc.index('-c')+1]=str(mutated)
        if '-MF' in cc:cc[cc.index('-MF')+1]=str(temp/'mutant.d')
        link=[str(out) if same_path(x,previous,build,os.name=='nt') else x for x in link]
        link[link.index('-o')+1]=str(library)
        link=['-Wl,--out-implib,'+str(temp/'mutant.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in link]
        subprocess.run(cc,cwd=build,check=True,capture_output=True,timeout=90)
        subprocess.run(link,cwd=build,check=True,capture_output=True,timeout=90)
        if label in ('stopped-fault-text','failed-start-diagnostics'):
            result=subprocess.run([str(session),str(library),'--lifecycle'],capture_output=True,text=True,timeout=60)
            killed=result.returncode!=0
        elif label=='rpc-log-callback':
            result=subprocess.run([str(video),str(library),'--paths','0'],capture_output=True,text=True,timeout=60)
            killed=result.returncode!=0
        else:
            try:run(exe,library,fixtures,'probe');killed=False
            except RuntimeError:killed=True
        if not killed:raise RuntimeError('session mutant survived: '+label)
        print('session mutant killed: '+label,flush=True)
with tempfile.TemporaryDirectory(prefix='arc-session-id-mutants-') as directory:
    temp=Path(directory);source=(root/'libretro/arc_session.c').read_text()
    compiler=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_C_COMPILER:FILEPATH='))
    for label,old,new in [
        ('zero-first','*out = ++source->last;','*out = source->last++;'),
        ('wrap-exhaustion','source->last == UINT64_MAX','false'),
        ('repeat-id','*out = ++source->last;','*out = 1;'),
        ('refuse-last-valid','source->last == UINT64_MAX','source->last >= UINT64_MAX-1'),
    ]:
        if source.count(old)!=1:raise RuntimeError(label+': allocator anchor')
        mutated=temp/'arc_session.c';mutated.write_text(source.replace(old,new));binary=temp/('probe.exe' if os.name=='nt' else 'probe')
        subprocess.run([compiler,'-std=c17','-I'+str(root/'libretro'),str(mutated),str(root/'tests/rpc_session_id_test.c'),'-o',str(binary)],check=True,capture_output=True,timeout=30)
        result=subprocess.run([str(binary)],capture_output=True,timeout=30)
        if not result.returncode:raise RuntimeError('allocator mutant survived: '+label)
        print('session allocator mutant killed: '+label,flush=True)
print('Equivalent isolated reversions: frontend policy predicates to the lifecycle-aware engine query after successful admission; raw observer installation at either one of its two RPC sites; CMOS persistence guard (the retained record cannot decode against the RPC plan and sync is guarded); sync/seed/HD/boot/outcome guards behind the admission and dispatch guards. These cannot recreate stale backend identity alone; the stopped/failed-start/chooser behavioural mutations above and the source guard cover their boundaries.')
