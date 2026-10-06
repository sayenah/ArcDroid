#!/usr/bin/env python3
"""Authored D2 ARM guests and declared MMIO chronology. GPL-2.0-only.
No firmware, captured CMOS, or implementation output is an input to this file.
"""
from pathlib import Path
import sys
RATES=[20,35,100,35]
PCM=[0x7fff8000,0xfb2e04d2,0x04d2fb2e,0x80007fff]
class Guest:
    def __init__(self):
        self.words=[0xeafffffe]*64;self.words[0]=0xea00003e
        self.n=1;self.actions=[]
    def emit(self,*words):self.words.extend(words);self.n+=len(words)
    def load(self,reg,value):self.emit(0xe59f0000|reg<<12,0xea000000,value);self.n-=1 # literal not executed
    def mov(self,reg,value):assert 0<=value<256;self.emit(0xe3a00000|reg<<12|value)
    def store(self,reg,base,offset=0):self.emit(0xe5800000|base<<16|reg<<12|offset)
    def read(self,reg,base,offset=0):self.emit(0xe5900000|base<<16|reg<<12|offset)
    def branch(self,target,condition=14,link=False):
        pos=len(self.words);self.emit(condition<<28|0x0a000000|(0x01000000 if link else 0)|((target-pos-2)&0xffffff))
    def init(self):
        self.mov(0,0x70);self.emit(0xee010f10);self.mov(0,0xd3);self.emit(0xe121f000)
        self.load(8,0x10020000);self.load(9,0x03200000);self.load(10,0x03400000)
    def delay(self,rate):
        loops=rate*50;self.load(6,loops);pos=len(self.words)
        self.emit(0xe2566001);self.branch(pos,1);self.n+=2*(loops-1)
    def write(self,base,offset,value,action=None):
        self.load(0,value);self.store(0,base,offset)
        if action:self.actions.append((self.n,action,value))

def audio(rate,compat=False):
    r=Guest();r.init();r.load(1,0x10010000)
    for i,w in enumerate(PCM):r.write(1,i*4,w)
    r.write(10,0,0xb0000002,('format',2,0))
    r.write(10,0,0xb1000002 if not compat else 0xb1000000,('format',2,0 if compat else 2))
    r.write(9,0x190,0x80,('control',))
    def pointers():
        for i,v in enumerate([0x10010000,0x10010000,0x10010000,0x10010000]):
            r.write(9,0x180+4*i,v,('pointer',i))
    pointers();r.write(9,0x190,0x20,('control',))
    r.delay(rate);r.read(2,9,0x194);r.store(2,8,24);r.write(10,0,0xb1000003 if not compat else 0xb1000000,('format',2,0 if compat else 3))
    r.delay(rate);r.read(2,9,0x194);r.store(2,8,28);r.read(2,9,0x1f0);r.store(2,8,32);r.write(9,0x190,0,('control',))
    r.delay(rate);pointers();r.write(9,0x190,0x20,('control',))
    r.delay(rate);r.write(10,0,0xb1000000,('format',2,0))
    r.mov(0,0xa5);r.store(0,8,16);r.emit(0xeafffffe)
    return r

def dma_state(rate):
    """IOMD state-machine observations independent of the PCM chronology."""
    r=Guest();r.init()
    r.write(10,0,0xb0000002);r.write(10,0,0xb1000002)
    r.write(9,0x190,0x80)
    offset=0
    def observe():
        nonlocal offset
        r.read(2,9,0x194);r.store(2,8,offset);offset+=4
    def bank(which,stop=False):
        r.write(9,0x180+8*which,0x10010000)
        r.write(9,0x184+8*which,0x90010000 if stop else 0x10010000)
    observe()                 # clear -> OIA (6)
    bank(1);observe()         # wrong bank in OIA -> OIA
    bank(0);observe()         # A -> IA (2), still requests B
    r.write(9,0x190,0x20);r.delay(rate);observe() # A finishes -> OIB (7)
    bank(0);observe()         # wrong bank in OIB -> OIB
    bank(1);observe()         # B -> IB (3), still requests A
    r.delay(rate);observe()   # B finishes -> OIA (6)
    bank(0);observe();bank(1);observe() # IA (2) -> busy A (0)
    r.delay(rate);r.delay(rate);observe() # both finish -> OIA (6)
    bank(0,True);observe();bank(1);observe() # IA -> busy A
    r.delay(rate);observe()   # StopA -> OIB even with B ready
    bank(1);observe();r.delay(rate);observe() # explicitly rearm B -> IB -> OIA
    # DMA must cross a 1 KiB pointer boundary, then wrap within its 4 KiB page.
    r.write(9,0x180,0x10010ff0);r.write(9,0x184,0x10010000)
    r.delay(rate);r.delay(rate);r.delay(rate);observe() # two quads -> OIB
    r.mov(0,0xa5);r.store(0,8,100);r.emit(0xeafffffe)
    return r

def inputs(model,typematic=False,factory=False,write_cmos=True):
    r=Guest();r.init()
    r.mov(7,0);r.mov(5,0);r.mov(11,0)
    if factory:r.mov(0,0x80);r.store(0,9,0x28) # wake on keyboard, IRQ remains masked in CPSR
    if typematic:
        # One command + parameter, 250 ms delay, 30 repeats/s.
        r.mov(0,0xf3);r.store(0,9,4);r.mov(0,0);r.store(0,9,4)
    if model==3 and not factory:r.mov(0,0xf4);r.store(0,9,0xa8)
    if not factory and model>=2:
        # Author a 1 ms timer wake for polling quadrature on idle-capable CPUs.
        for off,value in [(0x40,207),(0x44,7),(0x48,0),(0x18,32),(0x28,128),(0x78,1)]:
            r.mov(0,value);r.store(0,9,off)
    loop=len(r.words)
    r.read(0,9,8);r.emit(0xe3100020)
    skip=len(r.words);r.emit(0)
    r.read(0,9,4)
    # byte stream as words at RAM+256, count at RAM+0 (bounded to 256).
    r.emit(0xe3570c02 if factory else 0xe3570c01) # bounded 512/256 words
    r.emit(0x32882c01) # ADDLO r2,r8,#256
    r.emit(0x37820700) # STRLO r0,[r2,r7,LSL #2] (patched below)
    r.words[-1]=0x37820107 # shifted register offset r7 LSL2
    r.emit(0x32877001);r.store(7,8)
    if factory and write_cmos:
        r.emit(0xe3500071);skipwrite=len(r.words);r.emit(0)
        r.emit(0xe35b0000);skipdone=len(r.words);r.emit(0)
        r.mov(11,1)
        # Start, address, register zero, 256 authored data bytes, stop.
        r.mov(0,3);r.store(0,9);r.mov(0,2);r.store(0,9)
        calls=[]
        r.mov(0,0xa0);calls.append(len(r.words));r.emit(0)
        r.mov(0,0);calls.append(len(r.words));r.emit(0)
        r.mov(12,0);write_loop=len(r.words)
        r.emit(0xe22c005a) # EOR r0,r12,#0x5a
        calls.append(len(r.words));r.emit(0)
        r.emit(0xe28cc001,0xe35c0c01);r.branch(write_loop,1)
        for v in [0,2,3]:r.mov(0,v);r.store(0,9)
        end=len(r.words)
        for p in [skipwrite,skipdone]:r.words[p]=0x1a000000|((end-p-2)&0xffffff)
    r.words[skip]=0x0a000000|((len(r.words)-skip-2)&0xffffff)
    if not factory:
        # Snapshot quadrature and active-low button latch via SuperIO GPIO.
        r.read(0,9,0xa0);r.store(0,8,8);r.read(0,9,0xa4);r.store(0,8,12)
        r.load(1,0x03310000);r.read(0,1);r.store(0,8,20)
        if model==3:
            r.read(0,9,0xac);r.emit(0xe3100020);mskip=len(r.words);r.emit(0)
            r.read(0,9,0xa8);r.emit(0xe3550040)
            r.emit(0x32882c02) # ADDLO r2,r8,#512
            r.emit(0x37820105,0x32855001);r.store(5,8,4)
            r.words[mskip]=0x0a000000|((len(r.words)-mskip-2)&0xffffff)
    else:
        r.read(0,9,8);r.emit(0xe3100020)
        if model==2:r.emit(0x0e0f0f58) # conditional idle if no unread byte
        elif model==3:
            r.mov(0,0);r.emit(0x0589001c)
    if not factory and model>=2:
        r.mov(0,32);r.store(0,9,0x14)
        if model==2:r.emit(0xee0f0f58)
        else:r.mov(0,0);r.store(0,9,0x1c)
    r.branch(loop)
    if factory and write_cmos:
        sub=len(r.words)
        for p in calls:r.words[p]=0xeb000000|((sub-p-2)&0xffffff)
        r.mov(4,128);bit=len(r.words)
        r.emit(0xe1100004) # TST r0,r4
        r.mov(3,0);r.emit(0x13a03001)
        r.store(3,9);r.emit(0xe3833002);r.store(3,9)
        r.emit(0xe1b040a4);r.branch(bit,1) # MOVS r4,r4,LSR1
        # ACK ninth clock with SDA released, then return.
        r.mov(3,1);r.store(3,9);r.mov(3,3);r.store(3,9);r.emit(0xe1a0f00e)
    return r

def main():
    out=['/* Generated authored D2 ARM instructions. GPL-2.0-only. */','#include <stdint.h>']
    for model,rate in enumerate(RATES):
        for name,g in [('audio',audio(rate)),('compat',audio(rate,True)),('dma',dma_state(rate)),('input',inputs(model)),('typematic',inputs(model,True))]:
            out.append(f'static const uint32_t rpc_d2_{name}_{model}[] = {{'+','.join(f'0x{w:08x}u' for w in g.words)+'};')
    for model in [2,3]:
        for name,writes in [('factory',True),('empty',False)]:
            g=inputs(model,factory=True,write_cmos=writes)
            out.append(f'static const uint32_t rpc_d2_{name}_{model}[] = {{'+','.join(f'0x{w:08x}u' for w in g.words)+'};')
    Path(sys.argv[1]).write_text('\n'.join(out)+'\n')
if __name__=='__main__':main()
