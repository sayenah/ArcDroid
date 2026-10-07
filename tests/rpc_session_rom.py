#!/usr/bin/env python3
"""Project-authored ROM instruction arrays for integrated device tests. GPL-2.0-only.
No firmware bytes or guest binaries are stored. Expected results are stated in
rpc-session-expectations.md independently of interpreter/device implementation.
"""
from pathlib import Path
import sys
class Rom:
    def __init__(self):
        self.words = [0xeafffffe] * 64
        self.words[0] = 0xea00003e  # Reset vector -> 0x100
        self.words[6] = 0xea00000c  # IRQ vector -> 0x50
        self.words[20:29] = [0xe598b000, 0xe28bb001, 0xe588b000,
                            0xe599b014, 0xe598c004, 0xe18cc00b, 0xe588c004,
                            0xe589b014, 0xe25ef004]
        self.pool = []
    def emit(self, *words): self.words.extend(words)
    def load(self, reg, value):
        self.pool.append((len(self.words),reg,value)); self.emit(0)
    def mov(self,reg,value):
        assert 0 <= value <= 255; self.emit(0xe3a00000 | reg<<12 | value)
    def store(self,reg,base,offset=0,byte=False):
        self.emit((0xe5c00000 if byte else 0xe5800000) | base<<16 | reg<<12 | offset)
    def read(self,reg,base,offset=0,byte=False):
        self.emit((0xe5d00000 if byte else 0xe5900000) | base<<16 | reg<<12 | offset)
    def finish(self):
        for pos,reg,value in self.pool:
            offset=len(self.words)*4-(pos*4+8)
            assert 0<=offset<4096
            self.words[pos]=0xe59f0000 | reg<<12 | offset; self.words.append(value)
        return self.words

def devices(rate,model):
    r=Rom()
    r.mov(0,0x70);r.emit(0xee010f10) # CP15 control: permit 32-bit exceptions
    r.mov(0,0xd3);r.emit(0xe121f000) # SVC32, IRQ/FIQ disabled
    r.load(8,0x10000000);r.load(9,0x03200000);r.load(10,0x03010fd0)
    # Start equal deadlines: timer0=400 ticks, timer1=398 ticks,
    # GO1 is exactly 1 us (rate instructions) after GO0.
    for off,val in [(0x40,143),(0x44,1),(0x50,141),(0x54,1)]:
        r.mov(0,val);r.store(0,9,off)
    r.mov(0,0);r.store(0,9,0x48)
    r.emit(*([0xe1a00000]*(rate-1)));r.store(0,9,0x58)
    r.mov(0,0x60);r.store(0,9,0x18)
    r.mov(0,0x53);r.emit(0xe121f000) # Enable IRQ, leave FIQ disabled
    # PS/2 ACK at 1 ms; unsupported mouse command requests resend at 5 ms.
    r.mov(0,0xf4);r.store(0,9,4)
    if model==3:
        r.mov(0,0xf0);r.store(0,9,0xa8)
    # Empty READ ID opcode without MFM bit completes asynchronously at 4 ms.
    r.mov(0,10);r.store(0,10,4,True);r.mov(0,0);r.store(0,10,4,True)
    # Suspend/idle guest instructions only on chips that implement them.
    if model==2:r.emit(0xee0f0f58) # SA-110 MCR p15,0,r0,c15,c8,2
    elif model==3:r.store(0,9,0x1c) # ARM7500 SUSMODE
    poll=len(r.words)
    r.read(0,10,0,True);r.emit(0xe3100040)
    branch_pos=len(r.words);r.emit(0x0a000000|((poll-branch_pos-2)&0xffffff))
    for i in range(7):r.read(0,10,4,True);r.store(0,8,8+i*4)
    # Defined absent buses: status=0, data=ffff, podule and EASI floating high.
    r.load(1,0x030107dc);r.read(0,1,0,True);r.store(0,8,36)
    r.load(1,0x030107c0);r.read(0,1);r.store(0,8,40)
    r.load(1,0x03040000);r.read(0,1);r.store(0,8,44)
    r.load(1,0x08000000);r.read(0,1);r.store(0,8,48)
    r.read(0,9,4);r.store(0,8,56)
    if model==3:
        r.load(1,0x032000ac)
        poll=len(r.words)
        r.read(0,1,0,True);r.emit(0xe3100020)
        pos=len(r.words);r.emit(0x0a000000|((poll-pos-2)&0xffffff))
        r.load(1,0x032000a8);r.read(0,1,0,True)
    else:
        r.load(0,0xffffffff) # RiscPC mouse uses quadrature, not PS/2.
    r.store(0,8,60)
    # Program a timed, unrendered raster. RCLK/6, HCR=92 -> 25 us lines;
    # VCR=38 -> 40 lines/frame. Visible interval lines 2 through 20.
    r.load(1,0x03400000)
    for val in [0x8000005c,0x90000026,0x93000001,0x94000013,0xe0000016]:
        r.load(0,val);r.store(0,1)
    r.mov(0,0xa5);r.store(0,8,52);r.emit(0xeafffffe)
    return r.finish()

def main():
    output=['/* Generated project-authored guest instructions. GPL-2.0-only. */','#include <stdint.h>']
    for model,rate in enumerate([20,35,100,35]):
        words=devices(rate,model)
        output.append(f'static const uint32_t rpc_session_rom_{model}[] = {{'+','.join(f'0x{w:08x}u' for w in words)+'};')
    Path(sys.argv[1]).write_text('\n'.join(output)+'\n')
if __name__=='__main__':main()
