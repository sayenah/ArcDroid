#!/usr/bin/env python3
"""Single-line existing-core sensitivity variants in disposable objects. GPL-2.0-only.
Infrastructure/compile/load errors never count as detected changes. No repository object changes.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
from rpc_ninja_commands import rebuild_commands
from rpc_lockstep_corpus_test import execute, expectation, projection
from rpc_lockstep_rom import write_item
from rpc_timing_oracle import RATES
ROOT=Path(__file__).resolve().parents[1]
ARM='engine/rpcemu/src/arm.c';CP='engine/rpcemu/src/cp15.c';MEM='engine/rpcemu/src/mem.c';TIM='engine/rpc/rpc_mem_timing.c';SESSION='engine/rpc/rpc_session.c'
INSTALL='''        *entry = (ArcRpcTlbEntry){arc_rpc_phys_page(physical), addr >> 10, physical & ~0x3ffu,
                                  (uint8_t)domain, (uint8_t)ap, (uint8_t)page, 1, (uint8_t)attributes};'''
RECIPES=[
 ('NULL-limit-omitted',SESSION,'++instruction_work; arc_rpc_executed += (unsigned)arc_rpc_arm_exec_batch(1, NULL);',
  '++instruction_work; arc_rpc_executed += (unsigned)arc_rpc_arm_exec_batch(0, NULL);',('@G-1-limit',)),
 ('carry-F',ARM,'\t\tarm.reg[15] += 4;\ninstruction_done:',
  '\t\tarm.reg[15] = (arm.reg[15] & ~arm.r15_mask) | ((arm.reg[15]+4) & arm.r15_mask);\ninstruction_done:',('g-2-',)),
 ('IRQ-one-late',ARM,'} else if ((arm.event & 1) && !(arm.reg[16] & 0x80)) {',
  '} else if ((arm.event & 1) && !(arm.reg[16] & 0x80) && (opcode & 0x0ffffff0u) != 0x0121f000u) {',('g-3-',)),
 ('masked-reg16',ARM,'\t\t\t\tarm.reg[16] &= ~0xc0u;\n\t\t\t\tarm.reg[16] |= ((arm.reg[15] & 0xc000000) >> 20);',
  '\t\t\t\tarm.reg[16] &= ~0xc0u;\n\t\t\t\t((void)0);',('g-3-',)),
 ('FIQ-after-prefetch',ARM,'            arm.reg[15] += 4;\n            goto instruction_done;',
  '            arm.reg[15] += 4;\n            if ((arm.event & 2) && !(arm.reg[16] & 0x40)) exception(FIQ,0x20,0); goto instruction_done;',('prefetch-fiq-',)),
 ('denied-no-eviction',CP,INSTALL,
  INSTALL.replace('*entry =', 'if (cp15_domain_access(domain)!=0 && cp15_domain_access(domain)!=2) *entry ='),('g-4-',)),
 ('data-fill-no-memo-eviction',TIM,'        memo_clear();\n        *t = (Tag){line, physical & ~((1u << c->shift) - 1u), 1, 0};',
  '        if (fetch || m.model==2) memo_clear();\n        *t = (Tag){line, physical & ~((1u << c->shift) - 1u), 1, 0};',('g-6-',)),
 ('CLKCTL-after-drain',TIM,'    if (m.model == 3 && !(iomd.clkctrl & 2)) q *= 2;',
  '    static unsigned delayed_clkctrl; if (!m.bcount) delayed_clkctrl=iomd.clkctrl; if (m.model == 3 && !(delayed_clkctrl & 2)) q *= 2;',('g-8-',)),
 ('ROMCR-after-drain',TIM,'    if (r < 2) {\n        unsigned cr = r ? iomd.romcr1 : iomd.romcr0;',
  '    if (r < 2) {\n        unsigned cr = m.count ? 0 : (r ? iomd.romcr1 : iomd.romcr0);',('g-8-',)),
 ('PC-next-store-unseen',MEM,'    mem_phys_write32(physical, val);',
  '    if (addr != ((arm.reg[15]-4u)&arm.r15_mask)) mem_phys_write32(physical,val);',('g-9-','ac-m2-')),
 ('c7-no-op',TIM,'    if (!arc_rpc_mem_fast.recording || crn != 7) return;',
  '    if (1) return;',('g-10-e11-',)),
 ('idle-no-drain',TIM,'    (void)ticks(now_ns);\n    commands();',
  '    (void)ticks(now_ns);\n    if (m.idle) return; commands();',('g-14-',)),
 ('masked-wake-ignored',SESSION,'\n        if (arc_rpc_cpu_idle && (arm.event & 3)) arc_rpc_cpu_idle = false;',
  '\n        if (arc_rpc_cpu_idle && (((arm.event&1)&&!(arm.reg[16]&0x80)) || ((arm.event&2)&&!(arm.reg[16]&0x40)))) arc_rpc_cpu_idle=false;',('g-14-',)),
 ('DACR2-manager',CP,'    if (access == 0 || access == 2) {',
  '    if (access == 0) {',('g-4-','ac-h2-')),
 ('permission-hit-skipped',CP,'    if (access == 1 && cp15_check_permissions(entry->ap, rw)) {',
  '    static uint32_t previous_walks; bool hit=previous_walks==tlbs; previous_walks=tlbs; if (access == 1 && !hit && cp15_check_permissions(entry->ap, rw)) {',('g-4-',)),
]
# Explicit ownership for mechanisms that do not exist at this head.
DEFERRED=[
 ('hits/fetch_hits swapped','M2b','memory.stats.hits / fast.fetch_hits','awaiting emitter'),
 ('attributes stored on fault','M2b','cp15.arc_rpc_attributes','awaiting emitter'),
 ('cpu_due off by one tick','M2b','fast.cpu_due_ticks','awaiting emitter'),
 ('E2 instructions++/TLB fill','B-0','fast.instructions / tlb.valid','awaiting candidate mechanism'),
 ('block key incomplete','B-0','cpu.reg[15] / cpu.mode','awaiting candidate mechanism'),
 ('VA dropped from key','B-0','cpu.reg[3] (ADR VA) / cpu.reg[4] (BL LR)','awaiting candidate mechanism'),
 ('G-10 generation not advanced','introducing B slice','fetch memo / cache generation','awaiting candidate mechanism'),
 ('compiled-fetch transient','first compiling slice','fast.opcode / cp15.arc_rpc_attributes','awaiting candidate mechanism'),
 ('inlined-helper P1 omitted','M2b','density.P1','awaiting emitter'),
 ('lifecycle reuse / fatal cleanup','M2c','generation / epoch / RAM page hashes','awaiting emitter'),
]
EMITTER_ONLY = {'CLKCTL-after-drain': 'Pending-buffer repricing needs the A-8 stream-state channel; frame-level output did not distinguish this variant.'}


def main():
    p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('runner');p.add_argument('core');p.add_argument('fixtures',type=Path)
    p.add_argument('--only',default='');p.add_argument('--matrix',type=Path);a=p.parse_args();build=a.build.resolve()
    cache=(build/'CMakeCache.txt').read_text()
    ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
    commands=[r['command'] for r in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
    manifest=json.loads((a.fixtures/'manifest.json').read_text());matrix=[];survivors=[]
    for label,relative,old,new,prefixes in RECIPES:
        if a.only and a.only not in label:continue
        text=(ROOT/relative).read_text()
        if text.count(old)!=1:raise RuntimeError((label,'anchor not unique',text.count(old)))
        compile,link=rebuild_commands(commands,ROOT/relative,'arcdroid_rpcplan_libretro_rpc',build,os.name=='nt')
        with tempfile.TemporaryDirectory(prefix='arcdroid-m2a-variant-', dir=build.parent) as temp:
            folder=Path(temp);source=folder/Path(relative).name;obj=folder/'mutant.o';library=folder/Path(a.core).name
            changed=text.replace(old,new)
            before=text.splitlines(); after=changed.splitlines()
            changed_lines=[i+1 for i,(x,y) in enumerate(zip(before,after)) if x!=y]
            if len(before)!=len(after) or len(changed_lines)!=1:raise RuntimeError((label,'variant must change exactly one source line'))
            source.write_bytes(changed.encode('utf-8'))
            original_obj=compile[compile.index('-o')+1]
            for flag,value in (('-c',source),('-o',obj),('-MF',folder/'mutant.d'),('-MT',obj)):
                if flag in compile:compile[compile.index(flag)+1]=str(value)
            compile+=['-I',str((ROOT/relative).parent)]
            link=[str(obj) if x==original_obj else x for x in link];link[link.index('-o')+1]=str(library)
            link=['-Wl,--out-implib,'+str(folder/'mutant.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in link]
            for command in (compile,link):
                result=subprocess.run(command,cwd=build,capture_output=True,text=True,timeout=180)
                if result.returncode:raise RuntimeError((label,'build error (not a kill)',result.stderr[-1800:]))
            killed=[]
            if prefixes==('@G-1-limit',):
                harness=build/('rpc_lockstep_boundary.exe' if os.name=='nt' else 'rpc_lockstep_boundary')
                write_item(folder,'limit',0,[0xea00003e]+[0xeafffffe]*64)
                for model in range(4):
                    args=[str(folder/'limit.rom'),str(model),str(RATES[model]),'limit','0']
                    base=subprocess.run([str(harness),a.core,*args],capture_output=True,text=True,timeout=30)
                    if base.returncode:raise RuntimeError((label,'baseline boundary failure',base.stderr))
                    variant=subprocess.run([str(harness),str(library),*args],capture_output=True,text=True,timeout=30)
                    if 'missing API' in variant.stderr:raise RuntimeError((label,'infrastructure failure',variant.stderr))
                    if variant.returncode!=base.returncode or variant.stdout!=base.stdout:killed.append('G-1-NULL-'+str(model))
            for item in manifest['items']:
                if not any(item['name'].startswith(prefix) for prefix in prefixes):continue
                plan=a.fixtures/(item['name']+'.arcplan')
                base=execute(a.runner,a.core,a.fixtures,plan,folder,len(item['expected']))
                expectation(item,base)
                mutant=execute(a.runner,str(library),a.fixtures,plan,folder,len(item['expected']))
                if any(x in mutant.stderr for x in ('Cannot load core','Missing required symbol')):raise RuntimeError((label,'infrastructure failure',mutant.stderr))
                if mutant.returncode!=base.returncode or projection(mutant.stdout)!=projection(base.stdout):killed.append(item['name'])
            entry=dict(variant=label,source=relative,line=changed_lines[0],expected_rows=list(prefixes),result='behaviour change reported' if killed else 'no behaviour change reported',owner='M2a',level='demonstrated' if killed else 'awaiting emitter',kills=killed,single_item=len(killed)==1,
                       first_field={'masked-reg16':'cpu.reg[16]','carry-F':'cpu.reg[15]','denied-no-eviction':'tlb.tag','CLKCTL-after-drain':'memory.finish'}.get(label,'runner stdout / RAM signature'),
                       port_1a0=relative in (ARM,CP,TIM))
            matrix.append(entry)
            print(json.dumps(entry,sort_keys=True),flush=True)
            if not killed and label not in EMITTER_ONLY:survivors.append(label)
    matrix.extend(dict(variant=name,owner=owner,level=level,first_field=field,kills=[]) for name,owner,field,level in DEFERRED)
    if a.matrix:a.matrix.write_bytes((json.dumps(dict(license='GPL-2.0-only',matrix=matrix),sort_keys=True,indent=2)+'\n').encode('utf-8'))
    if survivors:raise RuntimeError(('sensitivity variants not noticed; M2a evidence incomplete',survivors))


if __name__=='__main__':main()
