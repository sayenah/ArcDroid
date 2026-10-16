#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""AC-5 code/relocation/constant/layout comparisons for Mach-O, ELF and COFF.

Use identically configured builds. No disassembly address masking, relocation
masking or golden regeneration is performed. Measurement's selected RPC objects
must match shipping in full; the per-event exception is confined to session.c.
"""
import argparse
import json
import os
import sys
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tests"))
from rpc_ninja_commands import split_command


def run(args):
    return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT)


def blocks(text,kind):
    return re.findall(r'^  '+kind+r' \{\n(.*?)^  \}',text,re.M|re.S)


def field(block,name):
    m=re.search(r'^    '+re.escape(name)+r': (.*)$',block,re.M)
    if not m:raise RuntimeError('missing object field '+name)
    return m[1]


def number(value):
    return int(value.split()[0],0)


def sections(path,readobj):
    info=run([readobj,'--sections',str(path)])
    result={}
    data=path.read_bytes()
    for block in blocks(info,'Section'):
        name=field(block,'Name').split()[0]
        if 'Mach-O' in info:
            size=number(field(block,'Size'));offset=number(field(block,'Offset'))
            address=number(field(block,'Address'))
            name=field(block,'Segment').split()[0]+','+name
            code='PureInstructions' in block or 'SomeInstructions' in block
            constant=not code and ('Segment: __TEXT ' in block or '__DATA_CONST,' in name or name.endswith(',__const'))
        elif 'COFF' in info:
            size=number(field(block,'RawDataSize'));offset=number(field(block,'PointerToRawData'))
            address=0;code='IMAGE_SCN_MEM_EXECUTE' in block
            constant=name.startswith('.rdata')
        elif 'elf' in info.lower():
            size=number(field(block,'Size'));offset=number(field(block,'Offset'))
            address=number(field(block,'Address'));code='SHF_EXECINSTR' in block
            constant=name.startswith(('.rodata','.data.rel.ro'))
        else:raise RuntimeError('unsupported object format')
        if name in result:raise RuntimeError('duplicate section name '+name)
        result[name]=(data[offset:offset+size],code,constant,address)
    return result


def code(path,readobj,constants=False):
    return {name:data for name,(data,is_code,is_constant,_) in sections(path,readobj).items()
            if is_code or (constants and is_constant)}


def relocations(path,readobj):
    return '\n'.join(line for line in run([readobj,'--relocations',str(path)]).splitlines()
                     if not line.startswith('File:') and line.strip())


def objects(build,target):
    folder=build/'CMakeFiles'/f'{target}.dir'
    result={p.relative_to(folder):p for p in folder.rglob('*') if p.suffix in ('.o','.obj')}
    if not result:raise RuntimeError('missing objects for '+target)
    return result


def compare_objects(left,right,readobj,label,constants=False,relocs=False):
    if left.keys()!=right.keys():raise RuntimeError(label+': object set differs')
    checked=0
    for relative,a in sorted(left.items()):
        b=right[relative]
        if code(a,readobj,constants)!=code(b,readobj,constants):
            raise RuntimeError(label+': code/constants differ: '+str(relative))
        if relocs and relocations(a,readobj)!=relocations(b,readobj):
            raise RuntimeError(label+': relocations differ: '+str(relative))
        checked+=1
    print(f'{label}: {checked} objects identical'+(' (code, relocations, constants)' if relocs else ' (code sections)'))


LAYOUT_FIELDS=[]
for typename,members in (
    ('ArcRpcMemFast','enabled pending bus_busy memo_valid recording rom_sequential after_branch track_rom integer_batch clock fetch_pc opcode instructions memo_shift memo_line fetch_hits denominator issue_ticks cpu_due_ticks base_ticks cpu_due_ns issue_floor due_floor issue_fraction due_fraction base_whole base_fraction'),
    ('__typeof__(m)','clock model rate control opcode switching fast_clock idle restart instruction data command buffer active head count bhead bcount bwords grant_left active_kind active_valid cycle_active video_active cursor bus_cursor finish queued_video video_arrival wait_start reciprocal last_rom stats'),
    ('Cache','tags victim sets ways shift populated'),
    ('Tag','va physical valid dirty'),
    ('Packet','kind count index writes force_n locked fetch fill width address')):
    LAYOUT_FIELDS.extend((f'sizeof({typename})',f'_Alignof({typename})'))
    LAYOUT_FIELDS.extend(f'offsetof({typename},{member})' for member in members.split())
    if typename=="ArcRpcMemFast":FAST_LAYOUT_COUNT=len(LAYOUT_FIELDS)


def layout(build,target,readobj,temp,header_only=False):
    commands=json.loads((build/'compile_commands.json').read_text())
    entry=next(c for c in commands if f'{target}.dir/' in c['command'] and c['file'].endswith('/arcdroid_engine.c' if header_only else '/rpc_mem_timing.c'))
    command=split_command(entry['command'],os.name=='nt')
    # Build a constant layout vector with precisely the source object's flags.
    source=Path(entry['file']);probe=temp/(target+'.c');obj=temp/(target+'.o')
    include=(source.parent/'rpc/rpc_mem_timing.h') if header_only else source
    fields=LAYOUT_FIELDS[:FAST_LAYOUT_COUNT] if header_only else LAYOUT_FIELDS
    probe.write_text('#include <stddef.h>\n#include "'+include.as_posix()+'"\nconst uint64_t arc_rpc_layout[] = {'+','.join(fields)+'};\n')
    command[command.index('-o')+1]=str(obj);command[command.index('-c')+1]=str(probe)
    subprocess.run(command,cwd=entry['directory'],check=True,capture_output=True)
    syms=blocks(run([readobj,'--symbols',str(obj)]),'Symbol')
    symbol=next(b for b in syms if field(b,'Name').split()[0] in ('arc_rpc_layout','_arc_rpc_layout'))
    name=field(symbol,'Section').split()[0]
    offset=number(field(symbol,'Value'))
    choices=[record for key,record in sections(obj,readobj).items() if (key==name or key.endswith(','+name)) and record[3]<=offset<record[3]+len(record[0])]
    if len(choices)!=1:raise RuntimeError('ambiguous layout section')
    data,_,_,address=choices[0]
    start=offset-address
    values=struct.unpack('<'+'Q'*len(fields),data[start:start+8*len(fields)])
    return values


def main():
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('--base',type=Path,required=True);p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--readobj',default=shutil.which('llvm-readobj'))
    args=p.parse_args()
    if not args.readobj:raise RuntimeError('llvm-readobj is required')
    for target in ('arcdroid_libretro_arculator','arcdroid_libretro_rpc','arcdroid_libretro','arcdroid_core','arcdroid_zlib'):
        compare_objects(objects(args.base,target),objects(args.candidate,target),args.readobj,'AC-5(a) '+target)
    commands=json.loads((args.candidate/'compile_commands.json').read_text())
    flags=[]
    for target in ('arcdroid_libretro_rpc','arcdroid_rpcmeasure_libretro_rpc'):
        entry=next(c for c in commands if f'{target}.dir/' in c['command'] and c['file'].endswith('/arm.c'))
        argv=split_command(entry['command'],os.name=='nt')
        flags.append((argv[0],[x for x in argv if x.startswith(('-O','-fvisibility=','--target=','-fno-strict-aliasing','-fno-common','-std='))]))
    if flags[0]!=flags[1]:raise RuntimeError('measurement/shipping compiler, optimisation, target or visibility flags differ')
    print('AC-5(b) compiler/optimisation/target/visibility flags identical')
    names=('arm.c','cp15.c','mem.c','rpc_mem_timing.c')
    shipping=objects(args.candidate,'arcdroid_libretro_rpc');measurement=objects(args.candidate,'arcdroid_rpcmeasure_libretro_rpc')
    left={k:v for k,v in shipping.items() if k.name.removesuffix('.obj').removesuffix('.o') in names}
    right={k:v for k,v in measurement.items() if k.name.removesuffix('.obj').removesuffix('.o') in names}
    if len(left)!=4:raise RuntimeError('missing named hot objects')
    compare_objects(left,right,args.readobj,'AC-5(b) three loops, service/access, translation/fetch, inline callers',True,True)
    with tempfile.TemporaryDirectory(prefix='arcdroid-1a0-layout-') as tmp:
        a=layout(args.candidate,'arcdroid_libretro_rpc',args.readobj,Path(tmp))
        b=layout(args.candidate,'arcdroid_rpcmeasure_libretro_rpc',args.readobj,Path(tmp))
        if a!=b:raise RuntimeError('shared timing layouts differ: '+str([(f,x,y) for f,x,y in zip(LAYOUT_FIELDS,a,b) if x!=y]))
        print(f'AC-5(b) ArcRpcMemFast, timing state, Cache, Tag, Packet: {len(a)} size/alignment/offset values identical')
        outer=layout(args.candidate,'arcdroid_rpcmeasure_libretro',args.readobj,Path(tmp),True)
        if outer!=a[:FAST_LAYOUT_COUNT]:raise RuntimeError('measurement frontend/RPC shared layout mismatch')
        print(f'AC-5(b) measurement frontend/RPC ArcRpcMemFast: {len(outer)} values identical')
        print('unarmed measurement hot objects also match the pinned shipping base; no extra per-instruction work')

if __name__=='__main__':main()
