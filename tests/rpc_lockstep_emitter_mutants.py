#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Real-C emitter sensitivity variants, built into disposable objects only.

An anchor must match once. Compile/link/load failures never count as kills,
except the explicitly named added-member compile-rejection control.
"""
import argparse
import io
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import time
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import rpc_lockstep_compare as c
from rpc_ninja_commands import rebuild_commands
from rpc_lockstep_emitter_test import program
E='engine/rpc/rpc_lockstep.c';A='engine/rpcemu/src/arm.c';M='engine/rpcemu/src/mem.c';T='engine/rpc/rpc_mem_timing.c';P='engine/rpcemu/src/cp15.c';S='engine/rpc/rpc_session.c';C='engine/rpcemu/src/arm_common.c'
# Each row changes executable C, never an expected result or a golden.
RECIPES=[('NULL-batch-booked-work', E,
 'ls.base_work-(ls.null_batch?1u:0u)+ls.completed',
 'ls.base_work+ls.completed', 'null-batch'),('added-m-member', 'engine/rpc/rpc_mem_timing.c', '    ArcRpcMemStats stats;', '    ArcRpcMemStats stats; uint64_t unclassified_member;', 'compile'),
 ('projected-inscount-one-completion', E,
  'if(ls.mode==1 && ls.phase!=3)v[LS_arm__inscount]=(uint32_t)(v[LS_arm__inscount]+ls.completed);',
  'if(ls.mode==1 && ls.phase!=3)v[LS_arm__inscount]=(uint32_t)(v[LS_arm__inscount]+ls.completed+1);', 'cross-quota'),
 ('raw-inscount-delta', A, '        ARC_RPC_LOCKSTEP(p0(arm.mode));',
  '        ++inscount;ARC_RPC_LOCKSTEP(p0(arm.mode));', 'cross-quota'),
 ('serialiser-drop',
  'engine/rpcemu/src/arm.c',
  'ARC_RPC_LS_SERIALIZE_arm',
  '#define arc_rpc_lockstep_collect_arm arc_rpc_lockstep_collect_arm_original\n'
  'ARC_RPC_LS_SERIALIZE_arm\n'
  '#undef arc_rpc_lockstep_collect_arm\n'
  'void arc_rpc_lockstep_collect_arm(uint64_t *out){arc_rpc_lockstep_collect_arm_original(out);out[LS_cpu__reg]=0;}',
  'points'),
 ('raw-pointer', 'engine/rpc/rpc_lockstep.c', 'out[0]=id;out[1]=p-base;return;', 'out[0]=id;out[1]=p;return;', 'points'),
 ('SWI-P1-missing', 'engine/rpcemu/src/arm.c', '                ARC_RPC_LOCKSTEP(p1(4,0));', '                ((void)0);', 'consume'),
 ('data-P1-double',
  'engine/rpcemu/src/mem.c',
  'uint32_t readmemfl(uint32_t addr)\n{',
  'uint32_t readmemfl(uint32_t addr)\n{\n    arc_rpc_lockstep_p1(2,0);',
  'points'),
 ('cp15-P1-outside-attempt',
  'engine/rpc/rpc_lockstep.c',
  'if(!ls.open){++ls.outside_helpers;return;}',
  'if(!ls.open){++ls.outside_helpers;checkpoint(1,0);return;}',
  'rate'),
 ('inlined-helper-P1-missing',
  'engine/rpcemu/src/mem.c',
  'uint32_t readmemfl(uint32_t addr)\n{\n#if defined(ARCDROID_RPC_LOCKSTEP)\n    ARC_RPC_LOCKSTEP(p1(2,1));',
  'uint32_t readmemfl(uint32_t addr)\n{\n#if defined(ARCDROID_RPC_LOCKSTEP)\n    ((void)0);',
  'points'),
 ('entry-transient-repaired',
  'engine/rpcemu/src/mem.c',
  'uint32_t readmemfl(uint32_t addr)\n{\n#if defined(ARCDROID_RPC_LOCKSTEP)\n    ARC_RPC_LOCKSTEP(p1(2,1));',
  'uint32_t readmemfl(uint32_t addr)\n{\n#if defined(ARCDROID_RPC_LOCKSTEP)\n    extern void arc_rpc_lockstep_probe_entry(void);\n    if(mmu && addr==0x10040000u)arc_rpc_lockstep_probe_entry();\n    ARC_RPC_LOCKSTEP(p1(2,1));',
  'entry'),
 ('projection-flushes', 'engine/rpc/rpc_lockstep.c', '    project();', '    project();arc_rpc_mem_batch_flush();', 'points'),
 ('P0-stale-due', 'engine/rpc/rpc_lockstep.c', '            uint64_t due=issue;', '            uint64_t due=issue+1;', 'points'),
 ('P2-stale-due',
  'engine/rpc/rpc_lockstep.c',
  'if(ls.phase==2 && !add(issue,v[LS_fast__base_ticks],&due))return;',
  'if(ls.phase==2 && !add(issue,0,&due))return;',
  'cache'),
 ('projection-next-base',
  'engine/rpc/rpc_lockstep.c',
  '    project();',
  '    project();arc_rpc_lockstep_probe_next_base(ls.projected[LS_fast__issue_ticks]);',
  'cache'),
 ('event-time-shift',
  'engine/rpc/rpc_lockstep.c',
  '        issue_time(),ls.agent,kind,address,size,write,value',
  '        issue_time()+1,ls.agent,kind,address,size,write,value',
  'points'),
 ('event-kind-swap',
  'engine/rpc/rpc_lockstep.c',
  'issue_time(),ls.agent,kind,address,size,write,value',
  'issue_time(),ls.agent,kind^1u,address,size,write,value',
  'points'),
 ('MMIO-unmasked-address', M, 'arc_rpc_lockstep_mmio(pa,4,false,result);',
  'arc_rpc_lockstep_mmio(addr,4,false,result);', 'io-alias'),
 ('output-pixel-bit', 'engine/rpcemu/src/vidc20.c',
  'arc_rpc_lockstep_output_word(presentation[(size_t)y*ARC_RPC_VIDEO_LIMIT+x]);',
  'arc_rpc_lockstep_output_word(presentation[(size_t)y*ARC_RPC_VIDEO_LIMIT+x]^1u);', 'video-output'),
 ('output-PCM-bit', 'engine/rpc/rpc_audio.c',
  'arc_rpc_lockstep_output_word((uint64_t)(int64_t)arc_rpc_pcm[at*2]);',
  'arc_rpc_lockstep_output_word(((uint64_t)(int64_t)arc_rpc_pcm[at*2])^1u);', 'audio-output'),
 ('P3-output-scan', E,
  'if(boundary!=0){arc_rpc_lockstep_video_output();arc_rpc_lockstep_audio_output();}',
  'if(true){arc_rpc_lockstep_video_output();arc_rpc_lockstep_audio_output();}', 'points'),
 ('unlogged-store', 'engine/rpcemu/src/mem.c', '    ARC_RPC_LOCKSTEP(ram_write(addr,4,val));', '    ((void)0);', 'store'),
 ('dropped-dirty-bit',
  'engine/rpc/rpc_lockstep.c',
  'ls.dirty[0][n]=ls.dirty[1][n]=true;ls.page_valid[n]=false;',
  'ls.dirty[0][n]=ls.dirty[1][n]=false;ls.page_valid[n]=false;',
  'points'),
 ('walk-tagged-CPU',
  'engine/rpc/rpc_lockstep.c',
  'void arc_rpc_lockstep_agent(unsigned agent) { ls.agent=agent; }',
  'void arc_rpc_lockstep_agent(unsigned agent) { (void)agent;ls.agent=0; }',
  'walk'),
 ('fatal-after-retire', 'engine/rpc/rpc_session.c', '    ARC_RPC_LOCKSTEP(fatal(format,arc_rpc_fault_text));', '    ((void)0);', 'fatal-late'),
 ('k-from-instructions',
  'engine/rpc/rpc_lockstep.c',
  'ls.k=ls.next_k++;ls.site=ls.sequence=ls.next_sequence=0;',
  'ls.k=arc_rpc_mem_fast.instructions;ls.next_k=ls.k+1;ls.site=ls.sequence=ls.next_sequence=0;',
  'points'),
 ('guard-miss-instructions',
  'engine/rpcemu/src/arm.c',
  '        ARC_RPC_LOCKSTEP(p0(arm.mode));',
  '        ++arc_rpc_mem_fast.instructions;ARC_RPC_LOCKSTEP(p0(arm.mode));',
  'points'),
 ('P3-zero-admission', 'engine/rpcemu/src/arm.c', '    if (!count) return 0;', '    if (!count){arc_rpc_lockstep_p3(1);return 0;}', 'zero'),
 ('cpu-due-one-tick', 'engine/rpc/rpc_lockstep.c', 'v[LS_fast__cpu_due_ticks]=due;', 'v[LS_fast__cpu_due_ticks]=due+1;', 'points'),
 ('late-cpu-due-one-tick', 'engine/rpc/rpc_lockstep.c', 'v[LS_fast__cpu_due_ticks]=due;', 'v[LS_fast__cpu_due_ticks]=due+(ls.k==5000);', 'late'),
 ('post-reset-cpu-due', E, 'v[LS_fast__cpu_due_ticks]=due;', 'v[LS_fast__cpu_due_ticks]=due+(ls.epoch==3 && ls.k==5);', 'epoch'),
 ('non-deterministic-detail-rerun',
  'engine/rpc/rpc_lockstep.c',
  'v[LS_fast__cpu_due_ticks]=due;',
  'v[LS_fast__cpu_due_ticks]=due+((ls.config.flags&ARC_RPC_LOCKSTEP_DETAIL)?2:1);',
  'points'),
 ('attributes-on-fault',
  'engine/rpcemu/src/cp15.c',
  'do_fault:\n    arm.event |= 0x40;',
  'do_fault:\n    arc_rpc_attributes=15;arm.event |= 0x40;',
  'fault'),
 ('reg16-refresh-skipped',
  'engine/rpcemu/src/arm.c',
  '\t\t\t\tarm.reg[16] &= ~0xc0u;\n\t\t\t\tarm.reg[16] |= ((arm.reg[15] & 0xc000000) >> 20);',
  '\t\t\t\tarm.reg[16] &= ~0xc0u;\n\t\t\t\t((void)0);',
  'reg16'),
 ('hits-fetch-hits-swapped',
  'engine/rpc/rpc_lockstep.c',
  '    project();',
  '    project();{uint64_t '
  't=ls.projected[LS_memory__stats__hits];ls.projected[LS_memory__stats__hits]=ls.projected[LS_fast__fetch_hits];ls.projected[LS_fast__fetch_hits]=t;}',
  'cache'),
 ('extra-P1-in-STM',
  'engine/rpcemu/src/arm_common.c',
  'arm_store_multiple(uint32_t opcode, uint32_t address, uint32_t writeback)\n{',
  'arm_store_multiple(uint32_t opcode, uint32_t address, uint32_t writeback)\n{arc_rpc_lockstep_p1(3,0);',
  'stm'),
 ('extra-P1-in-mem-access',
  'engine/rpc/rpc_mem_timing.c',
  '    if (!arc_rpc_mem_fast.recording) return; /* no descriptor walks, DMA or component charge */',
  '    arc_rpc_lockstep_p1(2,0);if (!arc_rpc_mem_fast.recording) return; /* no descriptor walks, DMA or component charge */',
  'points'),
 ('missing-reset-bump',
  'engine/rpc/rpc_session.c',
  '    ARC_RPC_LOCKSTEP(epoch_begin());\n    if (setjmp(arc_rpc_jump)) {',
  '    ((void)0);\n    if (setjmp(arc_rpc_jump)) {',
  'reset'),
 ('replay-after-helper',
  'engine/rpcemu/src/mem.c',
  '    arc_rpc_mem_access(addr, physical, 4, false, false, arc_rpc_cp15_attributes(),\n                       arc_rpc_phys_page(physical) != NULL);',
  '    arc_rpc_mem_access(addr, physical, 4, false, false, arc_rpc_cp15_attributes(),arc_rpc_phys_page(physical) != NULL);arc_rpc_mem_access(addr, '
  'physical, 4, false, false, arc_rpc_cp15_attributes(),arc_rpc_phys_page(physical) != NULL);',
  'points'),
 ('repeated-exception',
  'engine/rpcemu/src/arm_common.c',
  '\texception(SUPERVISOR, 0xc, 4);',
  '\texception(SUPERVISOR, 0xc, 4);exception(SUPERVISOR, 0xc, 4);',
  'points'),
 ('one-instruction-quota',
  'engine/rpc/rpc_session.c',
  '            if (quota > 200u) quota = 200u;',
  '            if (quota > 1u) quota = 1u;',
  'points'),
 ('queued-video-at-grant-end',
  'engine/rpc/rpc_mem_timing.c',
  '        if (m.queued_video && !m.grant_left) {',
  '        if (m.queued_video && m.grant_left) {',
  'video'),
 ('delayed-video-completion',
  'engine/rpc/rpc_mem_timing.c',
  '            m.video_active = true; m.finish = add(m.cursor, m.queued_video);',
  '            m.video_active = true; m.finish = add(m.cursor, m.queued_video+arc_rpc_mem_fast.denominator);',
  'video'),
 ('CLKCTL-after-drain', T,
  '    if (m.model == 3 && !(iomd.clkctrl & 2)) q *= 2;',
  '    static unsigned delayed_clkctrl; if (!m.bcount) delayed_clkctrl=iomd.clkctrl; if (m.model == 3 && !(delayed_clkctrl & 2)) q *= 2;',
  'clock'),
 ('demand-before-buffered-store',
  'engine/rpc/rpc_mem_timing.c',
  '        if (m.bcount) { m.active = m.buffer[m.bhead]; m.active_kind = BUFFERED; }\n'
  '        else if (m.count && m.command[m.head].kind == DEMAND) {\n'
  '            m.active = m.command[m.head]; m.active_kind = DEMAND;\n'
  '        } else break;',
  '        if (m.count && m.command[m.head].kind == DEMAND) {m.active=m.command[m.head];m.active_kind=DEMAND;}\n'
  '        else if (m.bcount) {m.active=m.buffer[m.bhead];m.active_kind=BUFFERED;}\n'
  '        else break;',
  'buffer')]


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);p.add_argument('loader');p.add_argument('core');p.add_argument('fixtures',type=Path);p.add_argument('--only',default='');p.add_argument('--matrix',type=Path)
 a=p.parse_args();schema,sh=c.load_schema();build=a.build.resolve();start=time.monotonic()
 cache=(build/'CMakeCache.txt').read_text(encoding='utf-8');ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
 commands=[r['command'] for r in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True, encoding='utf-8'))];matrix=[]
 with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-mutants-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
  root=Path(tmp);rom=root/'points.rom';rom.write_bytes(program());baselines={}
  plan=root/'points.arcplan';plan.write_text('version=1\nmodel=arm610\nfirmware=points.rom\ncmos=seed\nseed=authored.seed\nrtc_epoch=0\n', encoding='utf-8')
  (root/'authored.seed').write_bytes(bytes(256))
  from rpc_timing_rom import Guest
  g=Guest();g.literal(9,0x10040000);g.literal(0,0x55);g.emit(0xe8a90001)
  stm=root/'stm.rom';data=struct.pack('<'+'I'*len(g.words),*g.words);stm.write_bytes(data+bytes(2097152-len(data)))
  # Real canonical transient: the accessor's normal successful translation
  # repairs arc_rpc_attributes. Only its entry checkpoint can retain the error.
  g=Guest();g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
  for address,value in ((0x10004000,0xc02),(0x10004400,0x10000c02)):g.store(address,value)
  g.literal(0,0x10004000);g.emit(0xee020f10);g.literal(0,3);g.emit(0xee030f10)
  g.literal(0,0x71);g.emit(0xee010f10);g.literal(0,0x10040000);g.emit(0xe5901000)
  entry_steps=g.steps;entry_rom=root/'entry-transient.rom';data=struct.pack('<'+'I'*len(g.words),*g.words);entry_rom.write_bytes(data+bytes(2097152-len(data)))
  g=Guest();g.store(0x23400000,0);g.literal(0,0x23000000);g.emit(0xe5901000,0xe5d01000)
  io_steps=g.steps;io_rom=root/'io-alias.rom';data=struct.pack('<'+'I'*len(g.words),*g.words);io_rom.write_bytes(data+bytes(2097152-len(data)))
  from rpc_lockstep_rom import Corpus,buffer
  class LiteCorpus(Corpus):
   def fill(self,address,count,value):
    if address!=0x10004000 or value!=0:return super().fill(address,count,value)
  g=LiteCorpus(2);g.normal()
  for reg,value in ((0x1d8,0x10000000),(0x1dc,0x10000000),(0x1d4,0x10001000),(0x1e0,0x70)):g.write(0x03200000+reg,value)
  for v in (0x83000000,0x84000010,0x93000002,0x9400000a,0x80000038,0x9000000e,0xd0000000,0xe0000080):g.write(0x03400000,v)
  for i in range(16):buffer(g,0x55+i)
  video=root/'video.rom';data=struct.pack('<'+'I'*len(g.w),*g.w);video.write_bytes(data+bytes(2097152-len(data)))
  from rpc_d2_rom import audio
  audio_words=audio(20).words;audio_rom=root/'audio-output.rom';data=struct.pack('<'+'I'*len(audio_words),*audio_words);audio_rom.write_bytes(data+bytes(2097152-len(data)))
  def cases(kind):
   if kind=='null-batch':return [(rom,m,1,'none',16,first) for m,first in ((0,50),(2,63))]
   if kind=='cross-quota':return [(rom,m,17,'end:17',(1<<8)|16) for m in (0,2)]
   if kind=='stm':return [(stm,0,6,'end:6',0)]
   if kind=='entry':return [(entry_rom,model,entry_steps,f'end:{entry_steps}',16) for model in (0,2)]
   if kind=='late':return [(rom,0,5001,'end:5001',0,16666667)]
   if kind=='epoch':return [(rom,0,0,'reset-run',0)]
   if kind=='io-alias':return [(io_rom,0,io_steps,f'end:{io_steps}',0)]
   if kind=='video-output':return [(video,2,5000,'end:5000',0,1000000)]
   if kind=='audio-output':return [(audio_rom,0,5000,'end:5000',0,1000000)]
   if kind=='rate':return [(rom,0,0,'rate',0,0)]
   if kind=='reset':return [(rom,0,0,'reset',0,0)]
   if kind=='video':return [(video,2,1000,'end:1000',0)]
   if kind=='buffer':return [(a.fixtures/'g-8-2.rom',2,20000,'end:20000',0,10000000)]
   if kind=='clock':return [(a.fixtures/'g-8-3.rom',3,50000,'end:50000',0,16666667,2)]
   if kind=='consume':return [(rom,0,17,'consume',0)]
   if kind=='store':return [(rom,0,17,'end:17',4)]
   if kind=='walk':return [(a.fixtures/'walk-mmio-2.rom',2,80,'end:80',0)]
   if kind=='fault':return [(a.fixtures/'g-4-2.rom',2,20000,'end:20000',0,10000000)]
   if kind=='reg16':return [(a.fixtures/'g-3-0.rom',0,20000,'end:20000',0,10000000)]
   if kind=='cache':return [(a.fixtures/'g-6-0.rom',0,20000,'end:20000',0,10000000)]
   if kind=='fatal-late':return [(a.fixtures/'fatal-mode-0.rom',0,0,'none',0)]
   if kind=='zero':return [(rom,0,1,'end:1',0)]
   return [(rom,m,17,'end:17',0) for m in (0,2)]
  def execute(core,case,tag,baseline=False):
   path,model,count,operation,flags=case[:5];duration=case[5] if len(case)>5 else 100000;stream=root/(tag+'.stream')
   flags=(flags|1) if baseline else (flags&~1)
   args=[a.loader,str(core),str(path),str(model),str(stream),str(duration),str(flags),'0','0','0','200',operation]
   if len(case)>6:args+=['-','-','0',str(case[6])]
   r=subprocess.run(args,capture_output=True,text=True,timeout=120, encoding='utf-8')
   if r.returncode not in (0,1):raise RuntimeError(('load/run error, not a kill',r.returncode,r.stderr[-1000:]))
   if r.returncode and not any(x in r.stderr for x in ('lockstep mutating projection','lockstep unlogged write','lockstep projection changed','lockstep late fatal emission')):raise RuntimeError(('unexpected failure, not a kill',r.stderr[-1000:]))
   return stream,r
  def fixture_object(relative,content,stem):
   cc,link=rebuild_commands(commands,ROOT/relative,'arcdroid_rpclockstep_libretro_rpc',build,os.name=='nt')
   source=root/(stem+'-'+Path(relative).name);obj=root/(stem+'.o');source.write_text(content, encoding='utf-8')
   original=cc[cc.index('-o')+1]
   for flag,value in (('-c',source),('-o',obj),('-MF',root/(stem+'.d')),('-MT',obj)):
    if flag in cc:cc[cc.index(flag)+1]=str(value)
   cc+=['-I',str((ROOT/relative).parent)]
   result=subprocess.run(cc,cwd=build,capture_output=True,text=True,timeout=180, encoding='utf-8')
   if result.returncode:raise RuntimeError(('fixture compile, not a kill',stem,result.stderr[-1500:]))
   return original,obj,link
  for label,relative,old,new,kind in RECIPES:
   if a.only and a.only not in label:continue
   if kind=='unused':continue
   text=(ROOT/relative).read_text(encoding='utf-8');extra=None
   if label=='unlogged-store':
    # Select one physical RAM funnel, not all same-width stores.
    old='    ARC_RPC_LOCKSTEP(ram_write(addr,4,val));\n'
    at=text.index('ARC_RPC_PROFILE(store(ram00,addr&mem_rammask));');newtext=text[:at]+text[at:].replace(old,'    ((void)0);\n',1)
   elif label=='fatal-after-retire':
    if text.count(old)!=1:raise RuntimeError((label,'anchor',text.count(old)))
    newtext=text.replace(old,new).replace('    arc_rpc_observer_retire(false);','    arc_rpc_observer_retire(false);ARC_RPC_LOCKSTEP(fatal(format,arc_rpc_fault_text));',1)
   else:
    if text.count(old)!=1:raise RuntimeError((label,'anchor',text.count(old)))
    newtext=text.replace(old,new)
   extras=[];control_core=a.core;context=None
   if label=='entry-transient-repaired':
    extras.append((P,(ROOT/P).read_text(encoding='utf-8')+'\nvoid arc_rpc_lockstep_probe_entry(void){arc_rpc_attributes^=4u;}\n'))
   if label=='projection-next-base':
    newtext='void arc_rpc_lockstep_probe_next_base(uint64_t);\n'+newtext
    extras.append((T,(ROOT/T).read_text(encoding='utf-8')+'\nvoid arc_rpc_lockstep_probe_next_base(uint64_t issue){next_base(issue); }\n'))
   if label=='P3-zero-admission':
    end_text=(ROOT/E).read_text(encoding='utf-8');anchor='    if(ls.ended)return !ls.error;'
    if end_text.count(anchor)!=1:raise RuntimeError('zero-admission fixture anchor')
    end_text=end_text.replace('#include "arm.h"','#include "arm.h"\nextern int arc_rpc_arm_exec_batch(unsigned,ArcRpcCpuBatch *);').replace(anchor,anchor+'\n    if(active() && ls.phase!=5)(void)arc_rpc_arm_exec_batch(0,NULL);')
    extras.append((E,end_text));context='injected zero-admission wrapper probe'
   if label=='guard-miss-instructions':
    context='injected pre-P0 instruction-counter corruption; no candidate guard mechanism exists at this head'
   if label=='k-from-instructions':
    probe='if(ls.next_k==3)++arc_rpc_mem_fast.instructions;'
    newtext=newtext.replace(new,probe+new)
    common=text.replace(old,probe+old)
    original_control,control_obj,control_link=fixture_object(E,common,'counter-control')
    control_core=root/('control'+Path(a.core).suffix)
    control_link=[str(control_obj) if x==original_control else x for x in control_link]
    control_link[control_link.index('-o')+1]=str(control_core)
    control_link=['-Wl,--out-implib,'+str(root/'control.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in control_link]
    result=subprocess.run(control_link,cwd=build,capture_output=True,text=True,timeout=180, encoding='utf-8')
    if result.returncode:raise RuntimeError(('counter fixture link',result.stderr[-1500:]))
    context='injected timing-counter perturbation in both control and candidate'
   compile,link=rebuild_commands(commands,ROOT/relative,'arcdroid_rpclockstep_libretro_rpc',build,os.name=='nt')
   source=root/Path(relative).name;obj=root/'mutant.o';library=root/Path(a.core).name;source.write_text(newtext, encoding='utf-8')
   original=compile[compile.index('-o')+1]
   for flag,value in (('-c',source),('-o',obj),('-MF',root/'mutant.d'),('-MT',obj)):
    if flag in compile:compile[compile.index(flag)+1]=str(value)
   compile+=['-I',str((ROOT/relative).parent)]
   link=[str(obj) if x==original else x for x in link];link[link.index('-o')+1]=str(library)
   link=['-Wl,--out-implib,'+str(root/'mutant.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in link]
   result=subprocess.run(compile,cwd=build,capture_output=True,text=True,timeout=180, encoding='utf-8')
   if kind=='compile':
    if result.returncode==0 or 'unclassified_member' not in result.stderr or 'missing' not in result.stderr:raise RuntimeError((label,'member control not rejected',result.stderr[-1000:]))
    matrix.append(dict(mutant=label,detected_by=['classified-member compiler'],status='demonstrated',single_item=True));continue
   if result.returncode:raise RuntimeError((label,'compile error, not a kill',result.stderr[-1600:]))
   for j,(path,content) in enumerate(extras):
    original_extra,extra_obj,_=fixture_object(path,content,'extra-'+str(j))
    link=[str(extra_obj) if x==original_extra else x for x in link]
   result=subprocess.run(link,cwd=build,capture_output=True,text=True,timeout=180, encoding='utf-8')
   if result.returncode:raise RuntimeError((label,'link error, not a kill',result.stderr[-1600:]))
   killed=[]
   for i,case in enumerate(cases(kind)):
    key=(str(control_core),*tuple(map(str,case)))
    if key not in baselines:
     baseline,r=execute(control_core,case,'base-'+str(len(baselines)),True)
     if r.returncode:raise RuntimeError(('baseline',r.stderr))
     control_path,control=execute(control_core,case,'control-'+str(len(baselines)))
     if control.returncode:raise RuntimeError(('unmodified candidate status',control.stderr))
     with baseline.open('rb') as l,control_path.open('rb') as r:unchanged=c.compare(l,r,schema,sh,c.MODES[case[4]>>8])
     if not unchanged['ok']:raise RuntimeError(('unmodified candidate failed, not a kill',kind,unchanged))
     control_path.unlink();baselines[key]=baseline
    variant,r=execute(library,case,'variant')
    if r.returncode:killed.append(dict(case=key,evidence=r.stderr.strip()));continue
    try:
     with baselines[key].open('rb') as left,variant.open('rb') as right:diff=c.compare(left,right,schema,sh,c.MODES[case[4]>>8])
    except c.Invalid as error:
     if str(error).startswith(('baseline/candidate roles','header ','version/schema','bad stream magic','missing header')):
      raise RuntimeError((label,'harness/header refusal, not a kill',str(error)))
     diff=dict(ok=False,field='stream contract',reason=str(error))
    if not diff['ok']:killed.append(dict(case=key,evidence=diff))
   if not killed:raise RuntimeError((label,'survived'))
   if label in ('projected-inscount-one-completion','raw-inscount-delta') and any(
       x['evidence'].get('field')!='arm.inscount' for x in killed):
    raise RuntimeError('O3 mutant did not fail on the projected completion value')
   if label=='entry-transient-repaired':
    if any(x['evidence'].get('field')!='cp15.arc_rpc_attributes' for x in killed):raise RuntimeError('entry transient was not a canonical attribute difference')
    # All exits and non-entry boundaries must show the naturally repaired state.
    for case in cases(kind):
     key=(str(control_core),*tuple(map(str,case)))
     variant,r=execute(library,case,'entry-repair-proof')
     def non_entry(path):
      with path.open('rb') as f:
       reader=c.Reader(f,schema,sh,'strict');rows=[]
       while (record:=reader.next()) is not None:
        if not(record.kind in (1,2) and record.key[3:5]==(1,2) and record.edge==1):rows.append(record)
       return rows
     if r.returncode or non_entry(baselines[key])!=non_entry(variant):raise RuntimeError('translation did not repair the entry transient')
    case=(*cases(kind)[0][:4],8)
    left,control=execute(a.core,case,'entry-off-base',True);right,variant=execute(library,case,'entry-off-variant')
    with left.open('rb') as l,right.open('rb') as r:escaped=c.compare(l,r,schema,sh)
    if control.returncode or variant.returncode or not escaped['ok']:raise RuntimeError('entry transient did not escape with P1 off')
   # A second transport checking the same guest item is additional evidence,
   # not another detecting corpus item for AC-13's single-item warning.
   detecting_items=len(killed)
   if label in ('cpu-due-one-tick','late-cpu-due-one-tick','post-reset-cpu-due','non-deterministic-detail-rerun'):
    runner=build/('rpc_runner.exe' if os.name=='nt' else 'rpc_runner')
    args=[sys.executable,'-B',str(ROOT/'tools/rpc_lockstep_run.py'),
      '--baseline-runner',str(runner),'--baseline-core',a.core,
      '--candidate-runner',str(runner),'--candidate-core',str(library),
      '--plan',str(plan),'--system',str(root),
      '--runner-arg=--endpoint-instructions','--runner-arg='+('1:5001:0' if label=='late-cpu-due-one-tick' else '1:17:0')]
    if label=='late-cpu-due-one-tick':args+=['--max-bytes',str(16*1024*1024)]
    if label=='post-reset-cpu-due':args+=['--frames','3','--runner-arg=--reset-at','--runner-arg=1']
    result=subprocess.run(args,capture_output=True,text=True,timeout=60, encoding='utf-8')
    finding=json.loads(result.stdout)
    expected='non-deterministic' if label=='non-deterministic-detail-rerun' else 'fast.cpu_due_ticks'
    if result.returncode!=1 or finding.get('field')!=expected:raise RuntimeError(('real driver reproduction',label,result.returncode,result.stdout,result.stderr))
    killed.append(dict(case='two explicit runner/core pairs over FIFO/bounded files',evidence=finding))
   matrix.append(dict(mutant=label,detected_by=killed,status='demonstrated',single_item=detecting_items==1,detecting_items=detecting_items,context=context))
   print('killed real C',label,'cases',len(killed),flush=True)
 if a.matrix:a.matrix.write_text(json.dumps(dict(license='GPL-2.0-only',mutants=matrix,elapsed=time.monotonic()-start),indent=2)+'\n', encoding='utf-8')
 print('real C variants:',len(matrix),'elapsed',round(time.monotonic()-start,3))


if __name__=='__main__':main()
