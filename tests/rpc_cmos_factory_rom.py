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
    if kind=='delayed':
        # A reset after 550 complete 65536-tick/2-MHz IOMD periods occurs
        # after 18.0224 s, independently of CPU speed. Drain earlier makes,
        # then issue FF and require the held Delete after its ACK/BAT.
        for off,v in [(0x40,255),(0x44,255),(0x48,0)]:g.mov(0,v);g.store(0,9,off)
        g.load(6,550);wait=len(g.words);g.read(0,9,0x10);g.emit(0xe3100020);g.branch(wait,0)
        g.mov(0,32);g.store(0,9,0x14);g.emit(0xe2566001);g.branch(wait,1)
        drain=len(g.words);g.read(0,9,8);g.emit(0xe3100020)
        exit_branch=len(g.words);g.emit(0);g.read(0,9,4);g.branch(drain)
        g.words[exit_branch]=0x0a000000|((len(g.words)-exit_branch-2)&0xffffff)
        g.mov(0,255);g.store(0,9,4)
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
    # Read the real RTC hour before any payload write. Fixed generation epoch
    # is midnight; the other integration plan epoch is 23:31 UTC. Persisting
    # this read makes plan-dependent generation fail the independent oracle,
    # even if a faulty implementation merely labels provenance as fixed.
    for v in [3,2]:g.mov(0,v);g.store(0,9)
    send(0xa0);send(4)
    for v in [3,2]:g.mov(0,v);g.store(0,9)
    send(0xa1);g.mov(7,0);g.mov(5,8);receive=len(g.words)
    for v in [1,3]:g.mov(3,v);g.store(3,9)
    g.read(0,9);g.emit(0xe1a07087,0xe2000001,0xe1877000,0xe2555001);g.branch(receive,1)
    for v in [1,3,0,2,3]:g.mov(3,v);g.store(3,9)
    if kind in ['scatter','incomplete']:
        for address in [16,21,27,38,45,63,82,108,143,163,189,205,234,255]:write(address,[1 if address==63 and kind=="incomplete" else 0])
    elif kind=='ro350':
        write(16,[1 if a==63 else 0 for a in range(16,194)])
        write(196,[0]*60)
    else:
        # First byte uses the sampled hour. The checksum remains one, which is
        # correct exactly for the declared midnight, zero-payload default.
        for v in [3,2]:g.mov(0,v);g.store(0,9)
        send(0xa0);send(16);g.emit(0xe1a00007);calls.append(len(g.words));g.emit(0)
        for a in range(17,256):send(1 if a==63 and kind!='badchecksum' else 0)
        for v in [0,2,3]:g.mov(0,v);g.store(0,9)
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
    for kind in ['full','ro350','empty','scatter','incomplete','late','writer','delayed','badchecksum']:
        words=factory(kind).words
        (directory/f'{kind}.rom').write_bytes(struct.pack('<%dI'%len(words),*words)+struct.pack('<I',0xeafffffe)*(524288-len(words)))
    return directory
if __name__=='__main__':
    import sys
    generate(sys.argv[1])
