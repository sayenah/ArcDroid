#!/usr/bin/env python3
"""ArcDroid authored video guests, GPL-2.0-only. No upstream/firmware bytes.
Every peripheral transition is an ARM store. See rpc-video-expectations.md.
"""
from pathlib import Path
import sys
class Guest:
    def __init__(self): self.w=[]
    def emit(self,*words): self.w.extend(words)
    def literal(self,reg,value):
        self.emit(0xe59f0000 | reg<<12,0xea000000,value)
    def write(self,address,value):
        self.emit(0xe59f0008,0xe59f1008,0xe5801000,0xea000001,address,value)
    def wait(self,us):
        if us>30000:
            self.wait(30000);self.wait(us-30000);return
        ticks=us*2-1
        self.write(0x03200040,ticks&255);self.write(0x03200044,ticks>>8)
        self.write(0x03200014,0x20);self.write(0x03200048,0)
        self.literal(0,0x03200010)
        self.emit(0xe5901000,0xe3110020,0x0afffffc)
    def fill(self,address,count,value):
        self.literal(0,address);self.literal(1,value);self.literal(2,count)
        self.emit(0xe4801004,0xe2522001,0x1afffffc)
    def readback(self):
        for address,offset in [(0x032001d0,0),(0x03200000,4),(0x03200010,8)]:
            self.literal(0,address);self.emit(0xe5901000);self.literal(0,0x10020000+offset);self.emit(0xe5801000)
    def v(self,value): self.write(0x03400000,value)

def program(model,width,height,depth,hz,scenario):
    g=Guest();dram=model==3 or scenario in ('ring','packed','unaligned');base=0x10000000 if dram else 0x02000000
    g.fill(base,2048,0x76543210 if scenario=='unaligned' else 0x55555555)
    g.fill(base+4096,1024,0xaaaaaaaa)
    if scenario=='last':g.fill(base+2048,512,0xaaaaaaaa)
    g.fill(0x10010000,64,0x55555555)
    for i in range(256):g.v(0x10000000|i);g.v(i*0x010101)
    # Palette and cursor; explicit bus/RAM flags. Ring is 4096 bytes.
    for reg,value in [(0x1d8,base),(0x1dc,base),(0x1d4,base+4096-(16 if dram else 2048)),
                      (0x1c4,0x10010000),(0x1e0,(0x50 if dram else 8)| (0 if scenario=='border' else 0x20))]:
        g.write(0x03200000+reg,value)
    g.v(0x83000000);g.v(0x84000000|width);g.v(0x93000002);g.v(0x94000000|height+2)
    g.v(0x96000002);g.v(0x9700000a if scenario=='cursor' else 0x97000000);g.v(0x500000ff)
    g.v(0x400000ff if scenario in ('border','disabled') else 0x40000000)
    if scenario=='last':g.write(0x032001dc,(base+2048)|(0x40000000 if not dram else 0))
    if scenario=='packed':g.v(0xf0001001)
    if hz:
        if width<=640:clock=24;htotal=800;total=600 if hz==50 else 500
        else:clock=48;htotal=2400 if width>2048 else 1000;total=clock*1000000//(htotal*hz)
        if height>2048:clock=24;htotal=160;total=3000
    else:clock=24;htotal=2400;total=40
    if scenario=='threshold':clock=24;htotal=64;total=7500
    synth=(0x203 if clock==24 else 0x201) if model==3 else (0 if clock==24 else 0x100)
    g.v(0x80000000|htotal-8);g.v(0x90000000|total-2);g.v(0xd0000000|synth)
    g.v(0xe0000000|depth<<5|(0x4000 if scenario=='disabled' else 0)|(0x1000 if scenario=='interlace' else 0))
    if scenario=='unaligned':
        g.write(0x032001d4,base);g.wait(550);g.v(0xe0000080)
    if scenario in ('palette','border','cursor','memory','current','init','depth','grow','vstart','timing'):
        g.wait(650)
        if scenario=='palette':g.v(0x10000055);g.v(0x00ff0000)
        elif scenario=='border':g.v(0x4000ff00)
        elif scenario=='cursor':g.v(0x5000ff00);g.v(0x86000004)
        elif scenario=='memory':g.fill(base,1024,0)
        elif scenario=='current':g.write(0x032001d0,base+4096)
        elif scenario=='init':g.write(0x032001dc,base+4096)
        elif scenario=='depth':g.v(0xe0000040)
        elif scenario=='grow':g.v(0x84000810)
        elif scenario=='vstart':g.v(0x4000ff00);g.v(0x93000007)
        elif scenario=='timing':g.v(0x800012b8)
        g.readback()
    elif scenario in ('hclip','packed'):g.wait(650);g.readback()
    elif scenario=='vclip':
        # Last active raster 2058: timer expires between it and flyback 2059.
        g.wait(13721);g.readback()
    elif scenario=='frontend':
        g.wait(80000);g.v(0x90000256);g.v(0x84000280)
        g.wait(80000);g.v(0x84000320)
    elif scenario=='threshold':g.wait(80000);g.v(0x90001d4b)
    elif scenario=='ring':g.wait(650);g.write(0x032001d8,base+4096);g.write(0x032001d4,base+160-(16 if dram else 2048))
    g.emit(0xeafffffe)
    return g.w

def cases():
    out=[]
    for w,h in [(320,256),(640,480),(800,600)]:
        for hz in [50,60]:out.append((f'{w}x{h}-{hz}',w,h,3,hz,'plain'))
    for d in [0,1,2,3,4,6]:out.append((f'depth-{d}',32,8,d,0,'plain'))
    for scenario in ['palette','border','cursor','memory','current','init','depth','grow','vstart','timing','ring','disabled','interlace']:
        out.append((scenario,32,8,3,0,scenario))
    out.extend([('hclip',2064,8,3,0,'hclip'),('vclip',32,2056,3,50,'vclip'),('frontend',320,256,3,60,'frontend'),('threshold',32,8,3,0,'threshold'),('last',2064,8,3,0,'last'),('packed',2,8,0,0,'packed'),('unaligned',2,8,0,0,'unaligned')])
    return out

def main():
    out=['/* Generated authored ARM programs. GPL-2.0-only. */','#include <stdint.h>',
         'struct RpcVideoGuest {const char *name; unsigned width,height,depth,hz; const uint32_t *words; unsigned count;};']
    for model in range(4):
        for i,(name,w,h,d,hz,sc) in enumerate(cases()):
            words=program(model,w,h,d,hz,sc)
            out.append(f'static const uint32_t rpc_video_{model}_{i}[]={{'+','.join(f'0x{x:08x}u' for x in words)+'};')
        out.append(f'static const struct RpcVideoGuest rpc_video_guests_{model}[]={{')
        for i,(name,w,h,d,hz,sc) in enumerate(cases()):out.append(f'{{"{name}",{w},{h},{d},{hz},rpc_video_{model}_{i},sizeof(rpc_video_{model}_{i})/4}},')
        out.append('};')
    out.append('static const struct RpcVideoGuest *rpc_video_guests[]={rpc_video_guests_0,rpc_video_guests_1,rpc_video_guests_2,rpc_video_guests_3};')
    Path(sys.argv[1]).write_text('\n'.join(out)+'\n')
if __name__=='__main__':main()
