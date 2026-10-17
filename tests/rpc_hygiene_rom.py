#!/usr/bin/env python3
"""Small authored FDC guest for refusal notices and format invalidation. GPL-2.0-only."""
import struct
from rpc_d2_rom import Guest

def generate(path, formatting=False):
    g=Guest();g.init();g.load(10,0x03010fc0)
    def port(p,v):g.mov(0,v);g.emit(0xe5ca0000|((p-0x3f0)*4))
    def command(op,*args):
        port(0x3f5,op)
        for v in args:port(0x3f5,v)
    def irq():
        at=len(g.words);g.read(0,9,0x24);g.emit(0xe3100010);g.branch(at,0)
    def result(n):
        at=len(g.words);g.read(0,10,16);g.emit(0xe20000f0,0xe35000d0);g.branch(at,1)
        for _ in range(n):g.read(0,10,20)
    g.write(9,0x28,0x10);port(0x3f2,0x3c);irq()
    for _ in range(4):command(8);result(2)
    command(3,0xf1,2);port(0x3f7,2)
    # Masked guest IRQs; the 65536/2 MHz timer yields real frontend frames.
    for off,value in [(0x40,255),(0x44,255),(0x48,0)]:g.mov(0,value);g.store(0,9,off)
    def delay(ticks):
        g.mov(7,ticks);at=len(g.words);g.read(0,9,0x10);g.emit(0xe3100020);g.branch(at,0)
        g.mov(0,32);g.store(0,9,0x14);g.emit(0xe2577001);g.branch(at,1)
    if not formatting:
        # Same-sector refusals separated by at least three frontend frames.
        # INFO is once per session, independently of sector-log deduplication.
        for marker in range(1,4):
            command(0x49,0,0,0,0,3,0,0x35,255);result(7)
            g.mov(0,marker);g.store(0,8);delay(2)
    delay(20)
    if formatting:
        command(0x4d,0,3,5,0x32,0)
        payload=[v for sector in range(5) for v in (0,0,sector,3)]
    else:
        command(0x45,0,0,0,0,3,0,0x35,255);payload=[0x55]*1024
    for i,value in enumerate(payload):
        at=len(g.words);g.read(0,9,0x30);g.emit(0xe3100001);g.branch(at,0)
        g.load(1,0x0302a000 if i+1==len(payload) else 0x03012000)
        g.mov(0,value);g.emit(0xe5c10000)
    result(7);g.mov(0,2 if formatting else 4);g.store(0,8);g.emit(0xeafffffe)
    data=struct.pack('<%dI'%len(g.words),*g.words)
    path.write_bytes(data+bytes(2097152-len(data)))

def error_guest(path):
    # Same authored user ReportError/ROM SVC poll shape as the observer suite.
    words=[0xeafffffe]*(2097152//4)
    def put(at,values):words[at//4:at//4+len(values)]=values
    words[0]=0xea00003e;words[2]=0xea00005c
    put(0x100,[0xe3a0d201,0xe321f010,0xe3a0d201,0xe3a00c08,0xe3a01003,0xef0400df,0xeafffffe])
    words[0x800//4]=0
    put(0x180,[0xe3500081,0x01b0f00e,0xe1a0800e,0xe14f9000,0xe3a00081,0xe3a01000,0xe3a02000,0xef000006,0xe351000d,0x0a000003,0xe351001b,0x0a000001,0xeafffff6,0xe1a00000,0xe3a07201,0xe5871000,0xe169f009,0xe1b0f008])
    path.write_bytes(struct.pack('<%dI'%len(words),*words))
