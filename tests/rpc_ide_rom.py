#!/usr/bin/env python3
"""Authored PIO guest: chunk-crossing two-sector write/read, nIEN and IRQ ack. GPL-2.0-only."""
from pathlib import Path
import sys
from rpc_session_rom import Rom
r=Rom();r.mov(0,0x70);r.emit(0xee010f10);r.mov(0,0xd3);r.emit(0xe121f000)
r.load(8,0x10000000);r.load(9,0x03200000);r.load(10,0x030107c0);r.load(11,0x03010fd8)
r.mov(0,2);r.store(0,9,0x28);r.store(0,11,0,True)
def registers(command):
    for offset,value in ((24,0xe0),(20,0),(16,1),(12,255),(8,2),(28,command)):
        r.mov(0,value);r.store(0,10,offset,True)
def branch(target,condition):
    pos=len(r.words);r.emit(condition|((target-pos-2)&0xffffff))
registers(0x30);r.load(2,1024);r.mov(0,0x5a)
loop=len(r.words);r.read(1,11,0,True);r.emit(0xe3110008);branch(loop,0x0a000000)
r.store(0,10,0,True);r.emit(0xe2522001);branch(loop,0x1a000000)
loop=len(r.words);r.read(1,11,0,True);r.emit(0xe3110080);branch(loop,0x1a000000)
r.read(0,9,0x20);r.store(0,8,0);r.mov(0,0);r.store(0,11,0,True)
r.read(0,9,0x20);r.store(0,8,4);r.read(0,11,0,True);r.store(0,8,8)
r.read(0,10,28,True);r.store(0,8,12);r.read(0,9,0x20);r.store(0,8,16)
registers(0x20);r.load(2,1024);r.mov(4,0)
loop=len(r.words);r.read(1,11,0,True);r.emit(0xe3110008);branch(loop,0x0a000000)
r.read(0,10,0,True);r.emit(0xe0844000,0xe2522001);branch(loop,0x1a000000)
r.store(4,8,20);r.mov(0,0xa5);r.store(0,8,24);r.emit(0xeafffffe)
Path(sys.argv[1]).write_text('/* Generated authored PIO instructions. GPL-2.0-only. */\n#include <stdint.h>\nstatic const uint32_t rpc_ide_rom[] = {'+','.join(f'0x{x:08x}u' for x in r.finish())+'};\n')
