#!/usr/bin/env python3
"""Independent analytic D1 raster oracle. GPL-2.0-only.
Regeneration computes declared outcomes, never executes the core.
"""
from pathlib import Path
from rpc_video_rom import cases
from functools import cache

@cache
def a7000_metrics(name):
    """Derive splits/counts from the existing independent device timeline."""
    if name not in ('palette','border','cursor','current','depth','grow','vstart','ring','unaligned'):
        return a7000_metrics('palette')
    from rpc_raster_timeline import RasterTimeline
    from rpc_video_rom import program
    scenario=name if name in ('palette','border','cursor','current','depth','grow','vstart','ring','unaligned') else 'palette'
    t=RasterTimeline(program(3,2 if scenario=='unaligned' else 32,8,0 if scenario=='unaligned' else 3,0,scenario)).complete()
    changed={'palette':(0x03400000,0x10000055),'border':(0x03400000,0x4000ff00),
             'cursor':(0x03400000,0x5000ff00),'current':(0x032001d0,0x10001000),
             'depth':(0x03400000,0xe0000040),'grow':(0x03400000,0x84000810)}
    split=0
    if scenario in changed:
        address,value=changed[scenario]
        at=max(now for now,a,v,_ in t.writes if a==address and v==value)
        split=sum(now<at for _,now,_,_ in t.samples)
    return split,t.fields[0][2],t.fields[0][3],sum(depth==1 for _,_,_,depth in t.samples)

def prove_a7000_memory_window():
    # Removing all holds gives an earliest possible store timeline. At every
    # affected leading edge even that prefix ends before the raster's first
    # byte; real DMA contention can only delay the stores further.
    from rpc_authored_timeline import Timeline
    from rpc_video_rom import program
    cpu=Timeline(program(3,32,8,3,0,'memory'),3);control=None
    while control is None or cpu.time<control+1000000:
        cpu.step()
        if cpu.writes and cpu.writes[-1][1]==0x03400000 and cpu.writes[-1][2]>>24==0xe0:
            control=cpu.writes[-1][0]
    zero=[(t-control,a-0x10000000) for t,a,v,_ in cpu.writes if v==0 and 0x10000000<=a<0x10001000]
    for y in range(4,8):
        written=[a+4 for t,a in zero if t<100000*(y+3)]
        assert max(written,default=0)<=32*y

def colour(name,depth,x,y,model=0):
    split=a7000_metrics(name)[0] if model==3 else 4
    if name=='unaligned':
        rows=a7000_metrics(name)[3] if model==3 else 3
        bit=y*2+x if y<rows else 2*rows+(y-rows)*32+x*16
        stream=0x765432107654321076543210765432107654321076543210
        value=(stream>>bit)&((1<<(1 if y<rows else 16))-1)
        if y<rows:return 0xff000000|value*0x010101
        return 0xff000000|((value&255)<<16)|(((value>>4)&255)<<8)|((value>>8)&255)
    grey=0xff555555
    if name=='last':return 0xffaaaaaa if (2048+y*2064+x)%4096>=2048 else grey
    if name=='palette' and y>=split:return 0xff0000ff
    if name=='border':return 0xffff0000 if y<split else 0xff00ff00
    if name=='cursor':return (0xffff0000 if y<split else 0xff00ff00) if y<split or x>=4 else grey
    if name=='memory' and y>=4 and model!=3:return 0xff000000
    if name=='current' and y>=split:return 0xffaaaaaa
    if name=='ring' and y>=5 and model!=3:return 0xffaaaaaa
    if name=='depth' and y>=split:return 0xff050505
    if name=='grow' and y<split and x>=32:return 0xff000000
    if name=='vstart' and y==4 and model!=3:return 0xff00ff00
    if name=='disabled':return 0xffff0000
    if depth==0:return 0xff010101 if x%2==0 else 0xff000000
    if depth==1:return 0xff010101
    if depth==2:return 0xff050505
    return grey

def expected(model):
    if model==3:prove_a7000_memory_window()
    out=[]
    for name,width,height,depth,hz,scenario in cases():
        if name in ('frontend','threshold'):continue
        w=min(width,2048);h=min(height,2048)
        if name=='grow':w=2048
        checksum=14695981039346656037
        for y in range(h):
            for x in range(w):
                v=colour(name,depth,x,y,model)
                for b in v.to_bytes(4,'little'):
                    checksum=((checksum^b)*1099511628211)&((1<<64)-1)
        pixels=width*height
        if name in ('border','disabled'):pixels=0
        if name=='grow':pixels=a7000_metrics(name)[1] if model==3 else 8384
        if name=='vstart':pixels=a7000_metrics(name)[1] if model==3 else 224
        wraps=(pixels*[1,2,4,8,16,0,32][depth]//8)//4096
        if name=='ring':wraps=a7000_metrics(name)[2] if model==3 else 1
        if name=='unaligned':wraps=1
        if name=='init':wraps=0
        rate=hz or 250
        if name=='timing':rate=125
        if name=='interlace' and model!=3:rate=24_000_000/(2400*39.5)
        out.append(f'{name} {w}x{h} rate={rate:.6f} hash={checksum:016x} pixels={pixels} wraps={wraps}')
    return '\n'.join(out)+'\n'

if __name__=='__main__':
    for model in range(4):
        Path(__file__).with_name('fixtures').joinpath(f'rpc-video-{model}.trace').write_text(expected(model))
