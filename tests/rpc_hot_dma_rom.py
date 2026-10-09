#!/usr/bin/env python3
"""Authored cache-hot ALU/raster integration and independent CPU/device chronology.
GPL-2.0-only. This extends the documented authored ARM subset, not the core.
"""
from fractions import Fraction as F
from rpc_video_rom import Guest,program as video_program
from rpc_raster_timeline import RasterTimeline
from rpc_authored_timeline import ceil
CODE=0x10003000

def program(c):
    g=Guest();g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
    if c['model']==2:g.emit(0xee0f0f51)
    if c['model']==3:g.write(0x0320003c,7);g.write(0x03200080,32)
    g.w+=video_program(c['model'],32 if c['dram'] else 256,8,3,0,'hot-dram' if c['dram'] else 'hot-vram')[:-1]
    for section,physical,attr in ((0,0,0),(0x32,0x03200000,0),(0x100,0x10000000,8)):
        g.write(0x10004000+section*4,physical|0xc02|attr)
    g.write(CODE,0xe24ff008)
    g.literal(0,0x10004000);g.emit(0xee020f10);g.literal(0,3);g.emit(0xee030f10)
    g.literal(0,0x1075);g.emit(0xee010f10);g.literal(12,CODE);g.emit(0xe1a0f00c)
    return g.w

class HotRaster(RasterTimeline):
    def __init__(self,words,c):
        super().__init__(words);self.model=c['model'];self.dram=c['dram'];self.cached=False
        self.base=max(F(1000,c['rate']),F(125,2)) if self.model==2 else F(1000,c['rate'])
        self.q=F(125,2) if self.model==3 else F(125,4)
        self.fetch=28*self.q if self.model==3 else 7*self.q
        self.io_divided=self.model==3;self.romcr=64 if self.model==3 else 0
        self.rate=c['rate'];self.d=self.v=0
    def delay(self):
        if self.model==3:return super().delay()
        value=F((self.hcycle+8)*1000000000,24000000)+self.remainder
        whole=value.numerator//value.denominator;self.remainder=value-whole;return whole
    def write(self,address,value):
        super().write(address,value)
        if address==0x0320003c:
            self.q=F(125,4)*(1 if value&2 else 2);self.io_divided=not(value&1)
            self.fetch=7*self.q*(2 if self.romcr&64 else 1)*(1 if self.romcr&32 else 2)
        if address==0x03200080:
            self.romcr=value;self.fetch=7*self.q*(2 if value&64 else 1)*(1 if value&32 else 2)
    def service(self,address,byte=False):
        if address==CODE and self.pc==CODE:
            if self.cached:return F(0)
            self.cached=True;cost=(22 if self.model==2 else 11)*self.q
            assert self.next_event is None or self.time+cost<self.next_event,'warm fill crosses a declared edge'
            return cost
        if 0x03000000<=address<0x03200000:
            period=F(125,2)*(2 if self.io_divided else 1)
            return ceil(self.time/period)*period-self.time+9*period
        return super().service(address,byte)
    def event(self,now):
        before=self.pixels;cost=super().event(now)
        if self.dram:
            self.d+=cost//350;return cost
        transfers=self.pixels//2048-before//2048
        self.v+=transfers;return transfers*156
    def step(self):
        # Three authored maintenance/clock opcodes have no external transfer.
        # Decode them separately so the general subset stays strict.
        op=self.read(self.pc)
        if op in (0xee020f10,0xee030f10,0xee0f0f51):
            self.advance_base(self.base);self.ordinal+=1;self.issues[self.ordinal]=ceil(self.time)
            if op==0xee0f0f51:self.base=F(1000,self.rate)
            self.advance_bus(self.service(self.pc));self.pc+=4;return op
        return super().step()

def calibration(c):
    words=program(c);t=HotRaster(words,c)
    while t.pc!=CODE:t.step()
    t.step() # one real executed fill, excluded from the hot interval
    first=t.ordinal+1;start=t.time;d,v=t.d,t.v
    end=start+c['k']*t.base
    while t.next_event<=ceil(end):end+=t.event(t.next_event)
    expected=c['k']*t.base+350*(t.d-d)+156*(t.v-v)
    assert end-start==expected and (t.d-d if c['dram'] else t.v-v)>0
    return words,first,first+c['k']-1,expected
