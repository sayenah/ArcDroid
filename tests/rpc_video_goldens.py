#!/usr/bin/env python3
"""Independent analytic D1 raster oracle. GPL-2.0-only.
Regeneration computes declared outcomes, never executes the core.
"""
from pathlib import Path
from rpc_video_rom import cases

def colour(name,depth,x,y):
    if name=='unaligned':
        # Three rows of two 1-bit pixels, then five rows of two 16-bit pixels.
        bit=y*2+x if y<3 else 6+(y-3)*32+x*16
        stream=0x765432107654321076543210765432107654321076543210
        value=(stream>>bit)&((1<<(1 if y<3 else 16))-1)
        if y<3:return 0xff000000|value*0x010101
        return 0xff000000|((value&255)<<16)|(((value>>4)&255)<<8)|((value>>8)&255)
    grey=0xff555555
    if name=='last':return 0xffaaaaaa if (2048+y*2064+x)%4096>=2048 else grey
    if name=='palette' and y>=4:return 0xff0000ff
    if name=='border':return 0xffff0000 if y<4 else 0xff00ff00
    if name=='cursor':return (0xffff0000 if y<4 else 0xff00ff00) if y<4 or x>=4 else grey
    if name=='memory' and y>=4:return 0xff000000
    if name=='current' and y>=4:return 0xffaaaaaa
    if name=='ring' and y>=5:return 0xffaaaaaa
    if name=='depth' and y>=4:return 0xff050505
    if name=='grow' and y<4 and x>=32:return 0xff000000
    if name=='vstart' and y==4:return 0xff00ff00
    if name=='disabled':return 0xffff0000
    if depth==0:return 0xff010101 if x%2==0 else 0xff000000
    if depth==1:return 0xff010101
    if depth==2:return 0xff050505
    return grey

def expected(model):
    out=[]
    for name,width,height,depth,hz,scenario in cases():
        if name in ('frontend','threshold'):continue
        w=min(width,2048);h=min(height,2048)
        if name=='grow':w=2048
        checksum=14695981039346656037
        for y in range(h):
            for x in range(w):
                v=colour(name,depth,x,y)
                for b in v.to_bytes(4,'little'):
                    checksum=((checksum^b)*1099511628211)&((1<<64)-1)
        pixels=width*height
        if name in ('border','disabled'):pixels=0
        if name=='grow':pixels=8384
        if name=='vstart':pixels=224
        wraps=(pixels*[1,2,4,8,16,0,32][depth]//8)//4096
        if name in ('ring','unaligned'):wraps=1
        if name=='init':wraps=0
        rate=hz or 250
        if name=='timing':rate=125
        if name=='interlace' and model!=3:rate=24_000_000/(2400*39.5)
        out.append(f'{name} {w}x{h} rate={rate:.6f} hash={checksum:016x} pixels={pixels} wraps={wraps}')
    return '\n'.join(out)+'\n'

if __name__=='__main__':
    for model in range(4):
        Path(__file__).with_name('fixtures').joinpath(f'rpc-video-{model}.trace').write_text(expected(model))
