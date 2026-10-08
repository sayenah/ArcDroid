#!/usr/bin/env python3
"""Authored factory imitation using keyboard Delete and real IOMD/IIC writes.
No firmware or core output is used. GPL-2.0-only.
"""
from pathlib import Path
import struct
from rpc_d2_rom import Guest

def factory(kind='full'):
    g=Guest();g.init()
    # Cache off keeps the idle workload cheaper than a cache-hot ceiling loop;
    # production rates and memory timing remain the ordinary backend's.
    g.mov(0,0);g.emit(0xee010f10)
    if kind!='writer':
        loop=len(g.words);g.read(0,9,8);g.emit(0xe3100020);g.branch(loop,0)
        g.read(0,9,4);g.emit(0xe3500071);g.branch(loop,1)
    if kind=='empty':g.emit(0xeafffffe);return g
    calls=[]
    def send(value):g.mov(0,value);calls.append(len(g.words));g.emit(0)
    def write(address,data):
        for v in [3,2]:g.mov(0,v);g.store(0,9)
        send(0xa0);send(address)
        for value in data:send(value)
        for v in [0,2,3]:g.mov(0,v);g.store(0,9)
    if kind in ['scatter','incomplete']:
        for address in [16,21,27,38,45,63,82,108,143,163,189,205,234,255]:write(address,[1 if address==63 and kind=="incomplete" else 0])
    elif kind=='ro350':
        write(16,[1 if a==63 else 0 for a in range(16,194)])
        write(196,[0]*60)
    else:write(16,[1 if a==63 else 0 for a in range(16,256)])
    if kind=='late':
        # 800 complete 65536-tick timer periods exceed 26.2 s. Thereafter
        # repeatedly write the unchanged checksum byte until capture ends.
        for off,v in [(0x40,255),(0x44,255),(0x48,0)]:g.mov(0,v);g.store(0,9,off)
        g.load(6,800);wait=len(g.words);g.read(0,9,0x10);g.emit(0xe3100020);g.branch(wait,0)
        g.mov(0,32);g.store(0,9,0x14);g.emit(0xe2566001);g.branch(wait,1)
        repeat=len(g.words);write(63,[1]);g.branch(repeat)
    else:g.emit(0xeafffffe)
    sub=len(g.words)
    for p in calls:g.words[p]=0xeb000000|((sub-p-2)&0xffffff)
    g.mov(4,128);bit=len(g.words);g.emit(0xe1100004);g.mov(3,0);g.emit(0x13a03001)
    g.store(3,9);g.emit(0xe3833002);g.store(3,9);g.emit(0xe1b040a4);g.branch(bit,1)
    g.mov(3,1);g.store(3,9);g.mov(3,3);g.store(3,9);g.emit(0xe1a0f00e)
    return g

def generate(directory):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    for kind in ['full','ro350','empty','scatter','incomplete','late','writer']:
        words=factory(kind).words
        (directory/f'{kind}.rom').write_bytes(struct.pack('<%dI'%len(words),*words)+struct.pack('<I',0xeafffffe)*(524288-len(words)))
    return directory
if __name__=='__main__':
    import sys
    generate(sys.argv[1])
