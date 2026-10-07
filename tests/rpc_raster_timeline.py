#!/usr/bin/env python3
"""Independent leading-edge/quadword oracle for authored A7000 readbacks.
GPL-2.0-only. Timing: riscpc-timing.md; raster/DMA: rpc-video-expectations.md.
No core code, traces or captures are inputs. DRAM quad requests retain unused
bits, compare END at the request address and sample registers at line edges.
"""
from fractions import Fraction as F
from rpc_authored_timeline import Timeline,ceil
from rpc_video_rom import program,scroll_program

class RasterTimeline(Timeline):
    def __init__(self,words):
        super().__init__(words,3)
        self.hcycle=self.vcycle=self.hstart=self.hend=self.vstart=self.vend=0
        self.control=0;self.synth=0;self.remainder=F(0);self.raster=0;self.fields=[]
        self.blank=True;self.cur=0;self.bits=0;self.wraps=0;self.pixels=0
    def delay(self):
        clock=F(32000000*((self.synth>>8&63)+1),(self.synth&63)+1)
        clock/=((self.control>>2)&7)+1;clock=min(clock,120000000)
        value=F((self.hcycle+8)*1000000000,clock)+self.remainder
        whole=value.numerator//value.denominator;self.remainder=value-whole
        return max(whole,1)
    def reload(self):self.cur=self.memory.get(0x032001dc,0)&0x1ffffff0;self.bits=0
    def read(self,address):
        if address==0x03200000:return 128 if self.blank else 0
        if address==0x032001d0:return self.cur
        return super().read(address)
    def write(self,address,value):
        super().write(address,value)
        if address==0x032001d0:self.cur=value&0x1ffffff0;self.bits=0
        if address==0x032001dc and self.blank:self.reload()
        if address!=0x03400000:return
        register=value>>24;value &=0xffffff
        names={0x80:'hcycle',0x83:'hstart',0x84:'hend',0x90:'vcycle',0x93:'vstart',0x94:'vend',0xd0:'synth',0xe0:'control'}
        if register in names:setattr(self,names[register],value)
        if register==0xe0 and self.next_event is None:self.next_event=ceil(self.time)+self.delay()
    def event(self,now):
        self.raster=(self.raster+1)%(self.vcycle+2)
        self.blank=not(self.vstart<self.raster<=self.vend)
        transfers=0
        if self.blank:self.reload()
        else:
            depth=(1,2,4,8,16,0,32,0)[self.control>>5&7]
            count=max(0,self.hend-self.hstart)
            if self.memory.get(0x032001e0,0)&32 and not(self.control&0x4000):
                self.pixels+=count;remaining=count*depth
                while remaining:
                    if not self.bits:
                        address=self.cur;self.cur=(address+16)&0x1fffffff;self.bits=128;transfers+=1
                        if address&0xffffff==self.memory.get(0x032001d4,0)&0xffffff:
                            self.cur=self.memory.get(0x032001d8,0)&0x1fffffff;self.wraps+=1
                    take=min(remaining,self.bits);self.bits-=take;remaining-=take
        if self.raster==0:self.fields.append((dict(self.memory),self.cur,self.pixels,self.wraps,now))
        self.next_event=now+self.delay()
        return transfers*350
    def complete(self,count=1):
        while len(self.fields)<count:self.step()
        return self

def readbacks(scenario):
    dimensions={'hclip':(2064,8,0),'vclip':(32,2056,50),'packed':(2,8,0)}
    w,h,hz=dimensions[scenario]
    depth=0 if scenario=='packed' else 3
    t=RasterTimeline(program(3,w,h,depth,hz,scenario)).complete()
    return tuple(t.fields[0][0].get(0x10020000+4*i,0) for i in range(3))

def scroll_readbacks():
    t=RasterTimeline(scroll_program(3)).complete(8)
    return tuple(t.fields[i][0].get(0x10020000+16*i+4,0) for i in range(8))
