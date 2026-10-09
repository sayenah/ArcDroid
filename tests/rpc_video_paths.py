#!/usr/bin/env python3
"""Authored run-D scanout-path guests and independent packed-byte goldens.
GPL-2.0-only. No core source/output or firmware is an input.
"""
from pathlib import Path
import sys
sys.dont_write_bytecode=True
from rpc_video_rom import Guest
PATTERN=(0x7c3a9e15,0x1b6c93e4,0xf00d1234,0x00ff10ef,
         0xa05b28d7,0x92c4163e,0x6d8102b9,0xe7534af0)
CURSOR=(0xe41b936c,0x7c3a9e15)
CASES=(('words',36,8,False,False),('byte-tail',6,12,True,False),
       ('cursor-lanes',36,8,False,True),('right-edge',2064,2048,True,False))

def program(model,case):
    name,width,height,dram,cursor=case;dram=dram or model==3
    base=0x10000000 if dram else 0x02000000
    g=Guest();g.literal(0,base);g.literal(2,256)
    for reg,value in enumerate(PATTERN,4):g.literal(reg,value)
    g.emit(0xe8a00ff0,0xe2522001,0x1afffffc) # STM r0!,r4-r11; SUBS; BNE
    for row in range(8):
        for lane,value in enumerate(CURSOR):g.write(0x10010000+row*8+4*lane,value)
    for i in range(256):g.v(0x10000000|i);g.v(i*0x010101)
    length=32 if name=='byte-tail' else 4096
    for reg,value in ((0x1d8,base),(0x1dc,base),(0x1d4,base+length-(16 if dram else 2048)),
                      (0x1c4,0x10010000),(0x1e0,0x70 if dram else 0x28)):
        g.write(0x03200000+reg,value)
    for value in (0x83000000,0x84000000|width,0x93000002,0x94000000|(height+2),
                  0x96000002,0x9700000a if cursor else 0x97000000,0x86000001,
                  0x500000ff,0x6000ff00,0x70ff0000,
                  0x80000958,0x90000000|(height+6),0xd0000000|(0x203 if model==3 else 0),0xe0000060):g.v(value)
    g.emit(0xeafffffe);return g.w

def expected(model,case):
    name,width,height,dram,cursor=case;dram=dram or model==3
    length=32 if name=='byte-tail' else 4096
    data=b''.join(w.to_bytes(4,'little') for w in PATTERN)
    cur=b''.join(w.to_bytes(4,'little') for w in CURSOR)
    h=14695981039346656037
    for y in range(height):
        for x in range(min(width,2048)):
            value=0xff000000|data[(y*width+x)%length%len(data)]*0x010101
            if cursor and 1<=x<33:
                p=x-1;index=(cur[p//4]>>(2*(p%4)))&3
                if index:value=(0xffff0000,0xff00ff00,0xff0000ff)[index-1]
            for byte in value.to_bytes(4,'little'):h=((h^byte)*1099511628211)&((1<<64)-1)
    pixels=width*height;wraps=pixels//length
    transfers=pixels//(16 if dram else 2048)
    bus=transfers*(350 if dram else 156)+(4*350 if cursor else 0)
    hz=24000000/(2400*(height+8))
    return f'{name} {min(width,2048)}x{height} rate={hz:.6f} hash={h:016x} pixels={pixels} wraps={wraps} transfers={transfers} hold={bus}'

def header(path):
    out=['/* Authored packed video path guests. GPL-2.0-only. */']
    # First 16 stored pixels of row 1 in the right-edge guest. Its 2064-byte
    # scanline advances the declared repeating stream by 16 modulo 32.
    data=b''.join(w.to_bytes(4,'little') for w in PATTERN)
    prefix=[0xff000000|data[(CASES[-1][1]+x)%len(data)]*0x010101 for x in range(16)]
    out.append('static const uint32_t rpc_path_row1_prefix[16]={'+','.join(f'0x{x:08x}u' for x in prefix)+'};')
    for model in range(4):
        for i,case in enumerate(CASES):
            words=program(model,case)
            out.append(f'static const uint32_t rpc_path_{model}_{i}[]={{'+','.join(f'0x{x:08x}u' for x in words)+'};')
    out.append('static const struct RpcVideoGuest rpc_path_guests[4][4]={')
    for model in range(4):
        out.append('{'+','.join(f'{{"{c[0]}",{c[1]},{c[2]},3,0,rpc_path_{model}_{i},sizeof(rpc_path_{model}_{i})/4}}' for i,c in enumerate(CASES))+'},')
    out.append('};');Path(path).write_text('\n'.join(out)+'\n')

if __name__=='__main__':
    if sys.argv[1]=='--header':header(sys.argv[2])
    elif sys.argv[1]=='--goldens':
        for model in range(4):Path(__file__).with_name('fixtures').joinpath(f'rpc-video-paths-{model}.trace').write_text('\n'.join(expected(model,c) for c in CASES)+'\n')
    else:raise SystemExit('use --header PATH or --goldens')
