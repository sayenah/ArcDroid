#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read actual compiler offsetof/sizeof vectors from disposable owning-TU objects."""
import argparse
import itertools
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from rpc_measure_compare import sections,blocks,field,number,run
from rpc_ninja_commands import split_command
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);p.add_argument('readobj');a=p.parse_args()
manifest=json.loads((a.build/'generated/rpc-lockstep-tables/rpc_lockstep_codegen.json').read_text())
commands=json.loads((a.build/'compile_commands.json').read_text());roots=0;classified=0;padding=0;members=0
with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-layout-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
 folder=Path(tmp)
 for source,rows in manifest['layouts'].items():
  if not rows:continue
  entry=next(e for e in commands if 'arcdroid_rpclockstep_libretro_rpc.dir/' in e['command'].replace('\\','/') and Path(e['file']).as_posix().endswith('/'+source))
  cc=split_command(entry['command'],os.name=='nt');original=Path(entry['file']);copy=folder/original.name;obj=folder/(original.stem+'.o')
  copy.write_text(original.read_text()+'\n#if !defined(ARC_RPC_ARM_LOOP)\nARC_RPC_LS_LAYOUT_'+original.stem+'\n#endif\n')
  for flag,value in (('-c',copy),('-o',obj),('-MF',folder/(original.stem+'.d')),('-MT',obj)):
   if flag in cc:cc[cc.index(flag)+1]=str(value)
  cc+=['-I',str(original.parent)]
  result=subprocess.run(cc,cwd=entry['directory'],capture_output=True,text=True,timeout=120)
  if result.returncode:raise RuntimeError((source,'layout fixture compile',result.stderr[-1800:]))
  symbols=blocks(run([a.readobj,'--symbols',str(obj)]),'Symbol')
  symbol=next(s for s in symbols if field(s,'Name').split()[0] in ('arc_lockstep_layout','_arc_lockstep_layout'))
  section=field(symbol,'Section').split()[0];offset=number(field(symbol,'Value'))
  choices=[record for key,record in sections(obj,a.readobj).items() if (key==section or key.endswith(','+section)) and record[3]<=offset<record[3]+len(record[0])]
  if len(choices)!=1:raise RuntimeError(('layout section',source))
  data,_,_,address=choices[0];start=offset-address;values=struct.unpack('<'+'Q'*(13*len(rows)),data[start:start+8*13*len(rows)])
  coverage={}
  for i,row in enumerate(rows):
   _,offset,size,total,dimensions,*axes=values[13*i:13*(i+1)]
   if dimensions!=row['dimensions'] or not size or not total:raise RuntimeError(('layout bounds',source,row))
   key=row['root'];mask=coverage.setdefault(key,bytearray(total))
   if len(mask)!=total:raise RuntimeError('root size changed within fixture')
   ranges=[range(axes[2*d]) for d in range(dimensions)]
   for index in itertools.product(*ranges):
    begin=offset+sum(j*axes[2*d+1] for d,j in enumerate(index));end=begin+size
    if end>total:raise RuntimeError(('layout extends beyond root',source,row,begin,end,total))
    mask[begin:end]=bytes([1])*size
   members+=1
  for mask in coverage.values():
   classified+=sum(mask);padding+=len(mask)-sum(mask);roots+=1
 print(f'compiler layout: {members} field/member rows, {roots} roots/types, {classified} classified native bytes, {padding} padding bytes; complete-member and capacity compile probes passed')
