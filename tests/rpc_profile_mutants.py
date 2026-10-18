#!/usr/bin/env python3
"""Instrumented source hooks and diagnostic aggregation mutants. GPL-2.0-only."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from rpc_ninja_commands import rebuild_commands
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[1];build=Path(sys.argv[1]).resolve();target=Path(sys.argv[2]).resolve()
cache=(build/'CMakeCache.txt').read_text();ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
mutants=[
 ('nested-sampling','engine/rpc/rpc_profile.c','if(!profile.attempt && !profile.depth){','if(!profile.attempt && profile.cost==ARC_RPC_COST_COUNT){'),
 ('attempt-hook','engine/rpcemu/src/arm.c','ARC_RPC_PROFILE(begin(PC,arm.mode,ARM_MODE_32(arm.mode)?arm.reg[16]&0xc0u:(arm.reg[15]>>20)&0xc0u));','((void)0);'),
 ('fetch-hook','engine/rpcemu/src/arm.c','ARC_RPC_PROFILE(fetched(pccache2,opcode,flaglookup[opcode >> 28][(*pcpsr) >> 28]!=0));','((void)0);'),
 ('exception-hook','engine/rpcemu/src/arm.c','ARC_RPC_PROFILE(exception(address));','((void)0);'),
 ('end-hook','engine/rpcemu/src/arm.c','ARC_RPC_PROFILE(end(PC,arm.mode,ARM_MODE_32(arm.mode)?arm.reg[16]&0xc0u:(arm.reg[15]>>20)&0xc0u,arc_rpc_cpu_idle));','((void)0);'),
 ('write-word-hook','engine/rpcemu/src/mem.c','ARC_RPC_PROFILE(store(ram00,addr&mem_rammask));','((void)0);'),
 ('write-byte-hook','engine/rpcemu/src/mem.c','ARC_RPC_PROFILE(store(ram00,addr&mem_rammask));','((void)0);'),
 ('condition','engine/rpc/rpc_profile.c','if(!passed)return;','if(false)return;'),
 ('class-multiple','engine/rpc/rpc_profile.c','return op&(1u<<20)?5:6;','return 0;'),
 ('alias-page','engine/rpc/rpc_profile.c','profile.fetched_at[key]=profile.counts.attempts;','profile.fetched_at[key]=0;'),
 ('recent-bound','engine/rpc/rpc_profile.c','profile.counts.attempts-at<=ARC_RPC_PROFILE_RECENT','profile.counts.attempts-at<ARC_RPC_PROFILE_RECENT'),
 ('timer-feed','engine/rpc/rpc_profile.c','profile.counts.sampled_ns[profile.cost]+=now-profile.stamp;','profile.counts.sampled_ns[profile.cost]+=0;'),
 ('pc-write','engine/rpc/rpc_profile.c','profile.pc_destination=pc_write;','profile.pc_destination=false;'),
 ('reset-history','engine/rpc/rpc_profile.c','memset(profile.written,0,sizeof(profile.written));','((void)0);'),
 ('e11-hook','engine/rpcemu/src/cp15.c','        arc_rpc_profile_maintenance();','        ((void)0);'),
 # Opus QA M-1 survivors, retained individually with directed expectations.
 ('bucket-off-by-one','engine/rpc/rpc_profile.c','length>64?64:','length>=64?64:'),
 ('pcw-ldr','engine/rpc/rpc_profile.c','(c==3 || c==8)','(c==8)'),
 ('pcw-ldm','engine/rpc/rpc_profile.c','(c==5 && (op&(1u<<15)))','false'),
 ('pcw-alu-cmp','engine/rpc/rpc_profile.c','(((op>>21)&15)<8 || ((op>>21)&15)>11)','true'),
 ('mask-e4','engine/rpc/rpc_profile.c',' || masks!=profile.masks',''),
 ('idle-e4','engine/rpc/rpc_profile.c',' || idle)profile.causes',')profile.causes'),
 ('class-long-mul','engine/rpc/rpc_profile.c',' || (op&0x0f8000f0u)==0x00800090u',''),
 ('candidate-class','engine/rpc/rpc_profile.c','c<=4','c<=3'),
 ('reset-fetched','engine/rpc/rpc_profile.c','memset(profile.fetched_at,0,sizeof(profile.fetched_at));','((void)0);'),
 ('vram-byte-hook','engine/rpcemu/src/mem.c','vramb[addr & mem_vrammask] = val;\n    ARC_RPC_PROFILE(store(vram,addr&mem_vrammask));','vramb[addr & mem_vrammask] = val;'),
 ('read-byte-access','engine/rpcemu/src/mem.c','ARC_RPC_PROFILE(access(physical,false));\n    arc_rpc_mem_access(addr, physical, 1,','((void)0);\n    arc_rpc_mem_access(addr, physical, 1,'),
 ('write-byte-access','engine/rpcemu/src/mem.c','ARC_RPC_PROFILE(access(physical,true));\n    arc_rpc_mem_access(addr, physical, 1,','((void)0);\n    arc_rpc_mem_access(addr, physical, 1,'),
 ('cp15-read-side-exit','engine/rpcemu/src/cp15.c','cp15_read(uint32_t opcode)\n{\n#if defined(ARCDROID_RPC_LOCKSTEP)\n    ARC_RPC_LOCKSTEP(p1(5,0));\n#endif\n    ARC_RPC_PROFILE(side_exit());','cp15_read(uint32_t opcode)\n{\n#if defined(ARCDROID_RPC_LOCKSTEP)\n    ARC_RPC_LOCKSTEP(p1(5,0));\n#endif'),
]
subprocess.run([str(target)],check=True,cwd=build,capture_output=True,timeout=30)
for name,relative,old,new in mutants:
    source=root/relative;text=source.read_text()
    # Word and byte anchors share spelling; mutate each physical writer separately.
    if name=='write-word-hook':text=text.replace(old,new,1)
    elif name=='write-byte-hook':
        at=text.rindex(old);text=text[:at]+text[at:].replace(old,new,1)
    else:
        if text.count(old)!=1:raise RuntimeError((name,'anchor',text.count(old)))
        text=text.replace(old,new)
    compile,link=rebuild_commands(commands,source,'rpc_profile_test',build,os.name=='nt')
    with tempfile.TemporaryDirectory(prefix='arc-profile-mutant-') as temp:
        folder=Path(temp);copy=folder/source.name;obj=folder/'mutant.o';binary=folder/target.name;copy.write_text(text)
        original_obj=compile[compile.index('-o')+1];compile[compile.index('-o')+1]=str(obj);compile[compile.index('-c')+1]=str(copy);compile+=['-I',str(source.parent)]
        link=[str(obj) if x==original_obj else x for x in link];link[link.index('-o')+1]=str(binary)
        for command in (compile,link):
            r=subprocess.run(command,cwd=build,capture_output=True,text=True,timeout=60)
            if r.returncode:raise RuntimeError((name,'build failed (not a kill)',r.stderr))
        r=subprocess.run([str(binary)],cwd=build,capture_output=True,text=True,timeout=30)
        if r.returncode==0:raise RuntimeError((name,'SURVIVED'))
        print(name+': killed',flush=True)
runner,core,shipping,fixtures,lifecycle=sys.argv[3:8]
runtime=[
 ('device-event-hook','engine/rpc/rpc_session.c','arc_rpc_profile_event((unsigned)event);','((void)0);',None),
 ('mmio-read-hook','engine/rpcemu/src/mem.c','arc_rpc_profile_mmio(false);','((void)0);',None),
 ('data-write-hook','engine/rpcemu/src/mem.c','ARC_RPC_PROFILE(access(physical,true));','((void)0);',None),
 ('cache-fill-hook','engine/rpc/rpc_mem_timing.c','ARC_RPC_PROFILE(fill());','((void)0);',None),
 ('memo-hook','engine/rpcemu/src/cp15.c','ARC_RPC_PROFILE(memo());','((void)0);','engine/rpc/rpc_mem_timing.h'),
 ('mmio-read8-hook','engine/rpcemu/src/mem.c','if(!io)return mem_phys_read8_profile_body(addr);\n    arc_rpc_profile_mmio(false);','if(!io)return mem_phys_read8_profile_body(addr);',None),
 ('stop-reset-hook','engine/rpc/rpc_session.c','arc_rpc_observers_retire();\n    ARC_RPC_PROFILE(reset());','arc_rpc_observers_retire();',None),
]
for name,relative,old,new,header in runtime:
    source=root/relative
    compile,link=rebuild_commands(commands,source,'arcdroid_rpcplan_libretro_rpc',build,os.name=='nt')
    with tempfile.TemporaryDirectory(prefix='arc-profile-runtime-mutant-') as temp:
        folder=Path(temp);copy=folder/source.name;obj=folder/'mutant.o';library=folder/Path(core).name
        text=source.read_text()
        if header:
            h=root/header;content=h.read_text()
            if content.count(old)!=1:raise RuntimeError((name,'header anchor'))
            (folder/h.name).write_text(content.replace(old,new))
        else:
            if not text.count(old):raise RuntimeError((name,'anchor'))
            text=text.replace(old,new,1)
        copy.write_text(text)
        original_obj=compile[compile.index('-o')+1];compile[compile.index('-o')+1]=str(obj);compile[compile.index('-c')+1]=str(copy)
        compile+=['-I',str(source.parent)]
        link=[str(obj) if x==original_obj else x for x in link];link[link.index('-o')+1]=str(library)
        for command in (compile,link):
            r=subprocess.run(command,cwd=build,capture_output=True,text=True,timeout=60)
            if r.returncode:raise RuntimeError((name,'build failed (not a kill)',r.stderr))
        r=subprocess.run([sys.executable,'-B',str(root/'tests/rpc_profile_runner_test.py'),runner,str(library),shipping,fixtures,lifecycle],capture_output=True,text=True,timeout=60)
        if r.returncode==0:raise RuntimeError((name,'SURVIVED'))
        # Infrastructure errors are not counter kills.
        if 'RuntimeError' not in r.stderr or 'Cannot load core' in r.stderr or 'Missing required symbol' in r.stderr:raise RuntimeError((name,'not an assertion kill',r.stderr))
        print(name+': killed',flush=True)
print(f'{len(mutants)+len(runtime)} profile mutants killed; no build failure counted')
