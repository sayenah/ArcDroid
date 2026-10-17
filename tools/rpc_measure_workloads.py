#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Frozen 1a-0 authored workloads. No external guest inputs.

All recipes: realtime 60 s warm-up then 300 s measured. The stream ring is
16384 words (64 KiB); warm uses 16 load/store pairs; LDM/STM transfers 8 regs.
"""
import argparse
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from rpc_timing_rom import Guest

KINDS=('warm','stream-sa110','stream-arm710','ldm-stm8')

def generate(out,kind):
    model=1 if kind=='stream-arm710' else 2
    g=Guest();g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
    if model==2:g.emit(0xee0f0f51)
    data_attributes=12 if kind in ('warm','ldm-stm8') else 4
    for section,physical,attributes in ((0,0,0),(0x100,0x10000000,8),(0x201,0x10100000,data_attributes)):
        g.store(0x10004000+section*4,physical|0xc02|attributes)
    if kind=='warm':body=[0xe59a1000,0xe58a1000]*16+[0xeaffffde]
    elif kind=='ldm-stm8':body=[0xe89a00ff,0xe88a00ff,0xeafffffc]
    else:
        # r9 is ring base; r7 counts words. Reset the pointer every 64 KiB.
        body=[0xe48a1004,0xe2577001,0x1afffffc,0xe1a0a009,0xe3a07901,0xeafffff9]
    for i,word in enumerate(body):g.store(0x10001000+4*i,word)
    g.literal(0,0x10004000);g.emit(0xee020f10);g.literal(0,3);g.emit(0xee030f10)
    g.literal(9,0x20100000);g.emit(0xe1a0a009);g.literal(7,16384)
    g.literal(0,0x107d if model==2 else 0x7d);g.emit(0xee010f10)
    g.literal(11,0x10001000);g.emit(0xe1a0f00b)
    rom=bytearray(struct.pack('<I',0xeafffffe)*(2097152//4))
    rom[:4*len(g.words)]=struct.pack('<'+'I'*len(g.words),*g.words)
    out.mkdir(parents=True,exist_ok=True)
    (out/(kind+'-authored.rom')).write_bytes(rom)
    (out/'authored.seed').write_bytes(bytes(256))
    template=Path(__file__).resolve().parents[1]/'tests/fixtures/measure'/(kind+'.arcplan')
    (out/(kind+'.arcplan')).write_bytes(template.read_bytes())

if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--kind',choices=(*KINDS,'all'),default='all');a=p.parse_args()
    for kind in KINDS if a.kind=='all' else (a.kind,):generate(a.out,kind)
