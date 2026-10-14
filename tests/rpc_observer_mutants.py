#!/usr/bin/env python3
"""Rebuild copied primitive sources; a compile failure is never a kill. GPL-2.0-only."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from rpc_ninja_commands import rebuild_commands
sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()
target = Path(sys.argv[2]).resolve()
cache = (build/'CMakeCache.txt').read_text()
ninja = next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands = [x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
mutants = [
 ('fast-gate','engine/rpcemu/src/arm.c','#if ARC_RPC_ARM_OBSERVING\n        bool observing=arc_rpc_observer_active;','#if 1\n        bool observing=true;'),
 ('disarm-gate','engine/rpc/rpc_observer.c','if(!arc_rpc_observer_active)return;','if(false)return;'),
 ('condition','engine/rpcemu/src/arm.c','if (flaglookup[opcode >> 28][(*pcpsr) >> 28])','if (1)'),
 ('x-bit','engine/rpc/rpc_observer.c','e.swi=opcode&0xffffffu','e.swi=opcode&0xfdffffu'),
 ('mode-filter','engine/rpc/rpc_observer.c','(f->modes&(1u<<e->mode))','true'),
 ('address-match','engine/rpc/rpc_observer.c','e.pc==observer.watches[i].pc &&','true &&'),
 ('sp-match','engine/rpc/rpc_observer.c','arm.reg[13]==observer.watches[i].sp','true'),
 ('disqualify','engine/rpc/rpc_observer.c','observer.watches[i].live=false; /* end before callbacks can replace */','if(e.matched)observer.watches[i].live=false;'),
 ('same-mode-return','engine/rpc/rpc_observer.c','observer.watches[i].wait_return || arm.mode','false || arm.mode'),
 ('inspector-values','engine/rpc/rpc_inspector.c','return out && arc_rpc_inspect_translate','return false && out && arc_rpc_inspect_translate'),
 ('inspector-permission','engine/rpcemu/src/cp15.c','else if(ap==1 && !privileged)return false;','else if(false)return false;'),
 ('inspector-domain','engine/rpcemu/src/cp15.c','if(access==0 || access==2)return false;','if(false)return false;'),
 ('inspector-alignment','engine/rpc/rpc_inspector.c','!out || (va&3) ||','!out ||'),
 ('inspector-bound','engine/rpc/rpc_inspector.c','length>ARC_RPC_COPY_LIMIT','false'),
 ('inspector-overflow','engine/rpc/rpc_inspector.c','(length && length-1>UINT32_MAX-va)','false'),
 ('inspector-string-bound','engine/rpc/rpc_inspector.c','capacity>ARC_RPC_COPY_LIMIT','false'),
 ('completion-mask','engine/rpc/rpc_observer.c','(mask&~observer.current_results)','false'),
 ('completion-generation','engine/rpc/rpc_observer.c','generation!=observer.current_generation ||','false ||'),
 ('queue-revalidate','engine/rpc/rpc_observer.c','&& work_valid(&w,&h) && h.mutate','&& true && h.mutate'),
 ('queue-epoch','engine/rpc/rpc_observer.c','work->epoch!=observer.epoch ||','false ||'),
 ('epoch-advance','engine/rpc/rpc_observer.c','++observer.epoch;','observer.epoch+=0;'),
 ('epoch-discard','engine/rpc/rpc_observer.c','observer.owners[i].armed=false;memset','observer.owners[i].armed=true;memset'),
 ('trace-selection','engine/rpc/rpc_observer.c','|| !trace_selected(e)','|| false'),
 ('trace-resume','engine/rpc/rpc_observer.c','if(!trace.status.enabled || !trace_selected(e))','if(trace.status.incomplete || !trace.status.enabled || !trace_selected(e))'),
 ('trace-gap-count','engine/rpc/rpc_observer.c','++trace.gap.lost;','trace.gap.lost=1;'),
 ('trace-allowlist','engine/rpc/rpc_observer.c','default:return 0;','default:return 0x1fff;'),
 ('completion-nz','engine/rpc/rpc_observer.c','(cv&~0x30000000u)','(cv&~0xf0000000u)'),
 ('private-path-opt-in','engine/rpc/rpc_observer.c','trace.paths && e->kind','true && e->kind'),
 ('queue-pre-resolution','engine/rpc/rpc_observer.c','if(work_valid(&w,&h) && h.resolve','if(true && h.resolve'),
 ('ldm-return-hook','engine/rpcemu/src/arm_common.c','        arc_rpc_observe_return(arm.mode);','        (void)0;'),
 ('ldm-pc-bit','engine/rpcemu/src/arm_common.c','(opcode&(1u<<15)) &&','true &&'),
 ('ldm-abort-guard','engine/rpcemu/src/arm_common.c','!(arm.event&0x40) &&','true &&'),
 ('watch-outside-swi','engine/rpc/rpc_observer.c','|| !observer.in_swi || observer.current_owner!=(unsigned)owner','|| false || observer.current_owner!=(unsigned)owner'),
 ('handler-replacement','engine/rpc/rpc_observer.c','arc_rpc_observer_cancel(owner);observer.handlers[owner]','(void)0;observer.handlers[owner]'),
 ('watch-exact-deadline','engine/rpc/rpc_observer.c','observer.instruction>observer.watches[i].deadline','observer.instruction>=observer.watches[i].deadline'),
 ('invalid-second-level','engine/rpcemu/src/cp15.c','else if((second&3)==2){pa=','else if(true){pa='),
 ('mask-os-word','engine/rpc/rpc_observer.c','case 7:return reason==21?3:1;','case 7:return 0x1fff;'),
 ('mask-os-file','engine/rpc/rpc_observer.c','case 8:return trace_file_reason(reason)?0xb:1;','case 8:return 0x1fff;'),
 ('mask-wimp-report-error','engine/rpc/rpc_observer.c','case 0x400df:return 0x3f;','case 0x400df:return 0x1fff;'),
 ('trace-only-observer-order','engine/rpc/rpc_observer.c','e.order=observers_active?++observer.order:0;','e.order=++observer.order;'),
 ('trace-only-observer-attempt','engine/rpc/rpc_observer.c','if(observers_active)++observer.instruction;','++observer.instruction;'),
 ('trace-observer-order','engine/rpc/rpc_observer.c','ArcRpcObservation e=capture_event(ARC_RPC_OBS_CONTINUATION,0);e.watch=trace.watches[i].id;','ArcRpcObservation e=event(ARC_RPC_OBS_CONTINUATION,0);e.watch=trace.watches[i].id;'),
 ('trace-observer-watch-id','engine/rpc/rpc_observer.c','trace.watches[slot].id=++trace.watch_id;','trace.watches[slot].id=++observer.watch_id;'),
 ('watch-source-user','engine/rpc/rpc_observer.c','(source==0 || source==16) &&','true &&'),
 ('watch-target-user','engine/rpc/rpc_observer.c','(target==0 || target==16)','true'),
 ('trace-return-eligibility','engine/rpc/rpc_observer.c','trace.watches[i].wait_return || arm.mode','false || arm.mode'),

]
subprocess.run([str(target)],check=True,cwd=build,timeout=30,capture_output=True)
off=subprocess.check_output([str(target),'--identity','0'],cwd=build,timeout=30)
on=subprocess.check_output([str(target),'--identity','1'],cwd=build,timeout=30)
if off!=on:raise RuntimeError('trace altered canonical identities across fresh processes')
print('trace off/on: identical absolute observer order, instruction, epoch and watch identities',flush=True)
for name,relative,old,new in mutants:
    source = root/relative
    text = source.read_text()
    if text.count(old)!=1:raise RuntimeError((name,'anchor',text.count(old)))
    compile,link = rebuild_commands(commands,source,'rpc_observer_test',build,os.name=='nt')
    with tempfile.TemporaryDirectory(prefix='arc-observation-mutant-') as folder:
        folder=Path(folder);copy=folder/source.name;obj=folder/'mutant.o';binary=folder/target.name
        copy.write_text(text.replace(old,new))
        original_obj=compile[compile.index('-o')+1]
        compile[compile.index('-o')+1]=str(obj);compile[compile.index('-c')+1]=str(copy)
        compile+=['-I',str(source.parent)]
        link=[str(obj) if x==original_obj else x for x in link]
        link[link.index('-o')+1]=str(binary)
        for command in (compile,link):
            r=subprocess.run(command,cwd=build,capture_output=True,text=True,timeout=60)
            if r.returncode:raise RuntimeError((name,'build failure',r.stderr))
        r=subprocess.run([str(binary)],cwd=build,capture_output=True,text=True,timeout=30)
        if not r.returncode:raise RuntimeError((name,'SURVIVED'))
        print(name+': killed',flush=True)
print(f'{len(mutants)} primitive mutants killed; no build failures counted')
