#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""L1 single-site behavioural/invariant kills; build failures never count."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
from rpc_ninja_commands import rebuild_commands,split_command,compile_entry
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);a=p.parse_args()
build=a.build.resolve();root=Path(__file__).resolve().parents[1]
cache=(build/'CMakeCache.txt').read_text();ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
text=(root/'engine/rpc/rpc_mem_timing.c').read_text()
def replace(old,new):
    if text.count(old)!=1:raise RuntimeError(('anchor',old,text.count(old)))
    return text.replace(old,new)
def checked(command):
    r=subprocess.run(command,cwd=build,capture_output=True,text=True,timeout=120)
    if r.returncode:raise RuntimeError(('build error is not a kill',r.stdout[-1200:],r.stderr[-2000:]))
def kill(name,content,target='rpc_l1_directed_test',field=None):
    compile,link=rebuild_commands(commands,root/'tests/rpc_l1_test.c',target,build,os.name=='nt')
    with tempfile.TemporaryDirectory(prefix='arcdroid-l1-mutant-',dir=build) as tmp:
        folder=Path(tmp);source=folder/'tests/rpc_l1_test.c';source.parent.mkdir()
        source.write_text((root/'tests/rpc_l1_test.c').read_text())
        timing=folder/'engine/rpc/rpc_mem_timing.c';timing.parent.mkdir(parents=True);timing.write_text(content)
        obj=folder/'mutant.o';binary=folder/('mutant.exe' if os.name=='nt' else 'mutant')
        old_obj=compile[compile.index('-o')+1];compile[compile.index('-o')+1]=str(obj)
        for flag,value in [('-MF',folder/'mutant.d'),('-MT',obj)]:
            if flag in compile:compile[compile.index(flag)+1]=str(value)
        compile[compile.index('-c')+1]=str(source);compile+=['-I',str(root/'engine/rpc')]
        link=[str(obj) if x==old_obj else x for x in link];link[link.index('-o')+1]=str(binary)
        checked(compile);checked(link)
        r=subprocess.run([str(binary)],cwd=build,capture_output=True,text=True,timeout=30)
        if r.returncode==0:raise RuntimeError((name,'SURVIVED'))
        if field and field not in r.stderr:raise RuntimeError((name,'missing field',r.stderr))
        print(name+': killed',flush=True)
mutants=[
 ('linear-maintains-hints','if (!side->gate || side->gate == 3) return;','if (side->gate == 3) return;'),
 ('unconditional-hint-store','if (side->hints[index] != way) L1_HINT_WRITE(side,index,way);','L1_HINT_WRITE(side,index,way);'),
 ('hit-no-hint-refresh','        if (result) hint_store(side,index,(uint8_t)((unsigned)(result-c->tags) & side->way_mask));','        /* mutant: omit post-hit hint refresh */'),
 ('always-linear','    } else if (side->gate) {','    } else if (false) {'),
 ('hint-no-va',' && c->tags[base+way].va == line',''),
 ('hint-no-valid',' && c->tags[base+way].valid',''),
 ('hint-no-bound','way < c->ways && ','true && '),
 ('mirror-skip-fill','tag_write(c,base+way,(Tag){line, physical & ~((1u << c->shift) - 1u), 1, 0});','c->tags[base+way]=(Tag){line, physical & ~((1u << c->shift) - 1u), 1, 0};'),
 ('mirror-skip-full','        tag_write(c,i,tag);','        c->tags[i]=tag;'),
 ('mirror-skip-single','tag_write(&m.data,slot,tag);','*t=tag;'),
 ('mirror-zero-bind','memset(l1_instruction.keys, 0xff, sizeof(l1_instruction.keys));','memset(l1_instruction.keys, 0, sizeof(l1_instruction.keys));'),
 ('reset-after-null','    lookup_reset();','    if(clock) lookup_reset();'),
 ('hint-mru','            result = &c->tags[base+way];','            result = &c->tags[base+way]; c->victim[base/c->ways]=(uint8_t)way;'),
 ('mask32','UINT64_C(1) << n','(uint64_t)(UINT32_C(1) << (n&31))'),
 ('data-uses-instruction-side','return c == &m.data ? &l1_data : &l1_instruction;','return &l1_instruction;'),
 ('hint-counter-way','unsigned way = result ? (unsigned)(result-c->tags)-base : 0;','unsigned way = result ? side->hints[hint_index(c,line)] : 0;'),
]
for name,old,new in mutants:kill(name,replace(old,new))
way0='    if (start && c->tags[base].valid && c->tags[base].va == line) {\n        result = &c->tags[base];\n'
skip_way0=replace(way0,'    if (false) {\n        result = &c->tags[base];\n')
kill('skip-way0',skip_way0)
# Move the canonical way-0 check behind validation: an accepted hint bypasses
# it, while a rejected hint still finds way 0. This is guest-equivalent under
# uniqueness; the directed no-hint-work expectations make the ordering binding.
rejected='        } else {\n            ARC_RPC_COUNT(l1_hint_rejected,1);'
if skip_way0.count(rejected)!=1:raise RuntimeError('hint-before-way0 anchor')
kill('hint-before-way0',skip_way0.replace(rejected,
    '        } else if (start && c->tags[base].valid && c->tags[base].va == line) {\n'
    '            result = &c->tags[base];\n'+rejected))
geometry='    m.instruction.sets = model == 0 ? 4 : model == 1 ? 128 : model == 2 ? 16 : 64;'
gates='    l1_instruction.gate = lookup_gate(&m.instruction);\n    l1_data.gate = lookup_gate(&m.data);\n'
if text.count(gates)!=1:raise RuntimeError('gate anchor')
kill('gate-before-geometry',text.replace(gates,'').replace(geometry,gates+geometry))
ref='        if (t->valid && t->va == line) return t;'
kill('reference-updates-hint',replace(ref,'        if (t->valid && t->va == line) { lookup_side(c)->hints[hint_index(c,line)]=(uint8_t)way; return t; }'),'rpc_l1_crosscheck_test')
kill('reference-counts',replace(ref,'        if (t->valid && t->va == line) { ARC_RPC_COUNT(l1_hint_accepted,1); return t; }'),'rpc_l1_crosscheck_test')
kill('wrong-slot',replace('            result = &c->tags[base+way];','            result = &c->tags[base+way+1];'),'rpc_l1_crosscheck_test','lookup.cache_slot')
# Preprocess with the measurement core's real flags: TEST_FAULTS is stripped.
rows=json.loads((build/'compile_commands.json').read_text())
entry=compile_entry(rows,'arcdroid_rpcmeasure_libretro_rpc','rpc_mem_timing.c')
def reset_present(source):
    with tempfile.TemporaryDirectory(prefix='arcdroid-l1-reset-',dir=build) as tmp:
        src=Path(tmp)/'timing.c';out=Path(tmp)/'timing.i';src.write_text(source)
        cmd=split_command(entry['command'],os.name=='nt');cmd[cmd.index('-c')]='-E';cmd[cmd.index('-E')+1]=str(src);cmd[cmd.index('-o')+1]=str(out);cmd+=['-P','-I',str(root/'engine/rpc')]
        for flag,value in [('-MF',out.with_suffix('.d')),('-MT',out)]:
            if flag in cmd:cmd[cmd.index(flag)+1]=str(value)
        checked(cmd);body=out.read_text().split('void arc_rpc_mem_bind(',1)[-1]
        # Skip any forward declaration, then isolate the definition.
        body=body[body.index('{'):body.index('if (!clock) return;')]
        return 'lookup_reset();' in body
if not reset_present(text):raise RuntimeError('measurement reset absent')
if reset_present(replace('    lookup_reset();','#if defined(ARCDROID_TEST_FAULTS)\n    lookup_reset();\n#endif')):raise RuntimeError('TEST_FAULTS-only reset survived')
print('reset-TEST_FAULTS-only: killed by measurement preprocessing')
print('Equivalent: mirror-no-recheck / first-key-without-recheck: exact sentinel mirror implies valid and equal VA (AS-L1-7); not counted as kills')
