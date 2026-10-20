#!/usr/bin/env python3
"""ArcDroid authored video guests, GPL-2.0-only. No upstream/firmware bytes.
Every peripheral transition is an ARM store. See rpc-video-expectations.md.
"""
from pathlib import Path
import sys
sys.dont_write_bytecode = True
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
    g=Guest();dram=model==3 or scenario in ('ring','packed','unaligned','hot-dram');base=0x10000000 if dram else 0x02000000
    g.fill(base,2048,0x76543210 if scenario=='unaligned' else 0x55555555)
    g.fill(base+4096,1024,0xaaaaaaaa)
    # The route fixture must expose both colours inside its active 4 KiB ring.
    if scenario in ('last','route'):g.fill(base+2048,512,0xaaaaaaaa)
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

def scroll_program(model):
    """Patterned 16 KiB screen ring, 640x32/8bpp.
    The guest enables video by reading VIDCR and setting only E. Scrolls are
    640 bytes per field; both the initial and final half-SAM cases are used.
    """
    g=Guest();base=0x10000000 if model==3 else 0x02000000
    g.literal(0,base);g.literal(2,0);g.literal(3,4096)
    loop=len(g.w)
    # Four equal bytes: (word_index XOR (word_index >> 6)) & 255.
    g.emit(0xe0221322,0xe20110ff,0xe1811401,0xe1811801,0xe4801004,
           0xe2822001,0xe1520003)
    g.emit(0x1a000000 | ((loop-len(g.w)-2)&0xffffff))
    for i in range(256):g.v(0x10000000|i);g.v(i*0x010101)
    g.write(0x032001d8,base);g.write(0x032001d4,base+16384-(16 if model==3 else 2048))
    g.write(0x032001e0,0 if model==3 else 8)
    # Real read-modify-write: never supply the desired enabled value directly.
    g.literal(0,0x032001e0);g.emit(0xe5901000,0xe3811020,0xe5801000)
    g.v(0x83000000);g.v(0x84000280);g.v(0x93000002);g.v(0x94000022)
    g.v(0x80000958);g.v(0x90000026);g.v(0xd0000000|(0x203 if model==3 else 0))
    for step in range(8):
        offset=(16384-2672+step*640)%16384
        init=base+offset
        if model!=3 and offset>=16384-2048:init|=0x40000000
        g.write(0x032001dc,init)
        if step==0:g.v(0xe0000060)
        # Wait for active video, then sample after five complete 640-byte rows.
        g.literal(0,0x03200000);g.emit(0xe5901000,0xe3110080,0x1afffffc)
        g.wait(450)
        for reg,slot in [(0x1e0,0),(0x1d0,4),(0x1dc,8)]:
            g.literal(0,0x03200000+reg);g.emit(0xe5901000)
            g.literal(0,0x10020000+step*16+slot);g.emit(0xe5801000)
        # The next INIT write occurs in flyback; it cannot repaint this field.
        g.literal(0,0x03200000);g.emit(0xe5901000,0xe3110080,0x0afffffc)
    g.emit(0xeafffffe)
    return g.w

def extreme_program(model, fast):
    words = program(model, 16382, 8191, 6, 0, 'plain')
    g = Guest(); g.w = words[:-1]
    g.v(0x80000000)  # shortest legal line, eight pixel clocks
    g.v(0x90001fff)  # largest vertical period
    g.v(0xd0003f00 if fast else (0xd0000203 if model == 3 else 0xd0000000))
    g.emit(0xeafffffe)
    return g.w

def pixel_clock_program(model, clock):
    # Small visible area with VESA 1280x1024-class total timing. PLL values
    # give exactly 108/120 MHz, or deliberately excessive 1536/2048 MHz.
    g = Guest()
    g.v(0x83000000); g.v(0x84000020)
    g.v(0x93000002); g.v(0x9400000a)
    g.v(0x80000000 | (1688-8)); g.v(0x90000000 | (1066-2))
    synth = ((0x1a07, 0x0e03, 0x3f00) if model == 3 else
             (0x0801, 0x0400, 0x3f00))[clock]
    g.v(0xd0000000 | synth); g.v(0xe0000060)
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
    from rpc_raster_timeline import readbacks,scroll_readbacks
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
    for model in range(4):
        words=scroll_program(model)
        out.append(f'static const uint32_t rpc_scroll_{model}[]={{'+','.join(f'0x{x:08x}u' for x in words)+'};')
    for model in range(4):
        for fast in range(2):
            words=extreme_program(model, fast)
            out.append(f'static const uint32_t rpc_extreme_{model}_{fast}[]={{'+','.join(f'0x{x:08x}u' for x in words)+'};')
    out.append('static const struct RpcVideoGuest rpc_extreme_guests[4][2]={')
    for model in range(4):
        out.append('{'+','.join(f'{{"extreme",16382,8191,6,0,rpc_extreme_{model}_{fast},sizeof(rpc_extreme_{model}_{fast})/4}}' for fast in range(2))+'},')
    out.append('};')
    for model in range(4):
        for clock in range(3):
            words=pixel_clock_program(model, clock)
            out.append(f'static const uint32_t rpc_clock_{model}_{clock}[]={{'+','.join(f'0x{x:08x}u' for x in words)+'};')
    out.append('static const struct RpcVideoGuest rpc_clock_guests[4][3]={')
    for model in range(4):
        out.append('{'+','.join(f'{{"pixel-clock",32,8,3,0,rpc_clock_{model}_{clock},sizeof(rpc_clock_{model}_{clock})/4}}' for clock in range(3))+'},')
    out.append('};')
    for model in range(4):
        words=program(model,32,8,3,0,'plain')[:-1]
        g=Guest();g.w=words;g.v(0x90001fff);g.emit(0xeafffffe)
        out.append(f'static const uint32_t rpc_slow_{model}[]={{'+','.join(f'0x{x:08x}u' for x in g.w)+'};')
    out.append('static const struct RpcVideoGuest rpc_slow_guests[]={')
    for model in range(4):
        out.append(f'{{"slow",32,8,3,0,rpc_slow_{model},sizeof(rpc_slow_{model})/4}},')
    out.append('};')
    out.append('static const struct RpcVideoGuest rpc_scroll_guests[]={')
    for model in range(4):
        out.append(f'{{"scroll",640,32,3,0,rpc_scroll_{model},sizeof(rpc_scroll_{model})/4}},')
    out.append('};')
    for scenario in ('hclip','vclip','packed'):
        out.append(f'static const uint32_t rpc_a7000_{scenario}_readback[]={{'+','.join(f'0x{v:08x}u' for v in readbacks(scenario))+'};')
    out.append('static const uint32_t rpc_a7000_scroll_readback[]={'+','.join(f'0x{v:08x}u' for v in scroll_readbacks())+'};')
    Path(sys.argv[1]).write_text('\n'.join(out)+'\n', encoding='utf-8')
if __name__=='__main__':main()
