#!/usr/bin/env python3
"""Only authored firmware, boot packages and carriers. GPL-2.0-only."""
import hashlib
from pathlib import Path
import struct
import sys
import zlib
from rpc_cmos_factory_rom import generate
from rpc_d2_rom import Guest
from rpc_video_rom import program
from rpc_boot_packages_test import inputs
from generate_fixtures import build_extended_jfd

def generate_route(out):
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    system=inputs(out)
    roms=generate(out/'roms')
    firmware=system/'arcdroid/firmware';firmware.mkdir(parents=True,exist_ok=True)
    rows=[]
    for os,name in [(1,'ro350'),(2,'ro360'),(3,'ro370'),(4,'ro371')]:
        data=(roms/(name+'.rom')).read_bytes()
        # A bounded keyboard window selects authored factory capture only when
        # Delete is held. Ordinary boots render a visible guest raster instead.
        words=list(struct.unpack('<%dI'%(len(data)//4),data))
        resume=next(i+2 for i,w in enumerate(words) if w==0xe3500071)
        stub=Guest();stub.init();stub.mov(0,0);stub.emit(0xee010f10)
        stub.mov(0,255);stub.store(0,9,0x40);stub.store(0,9,0x44);stub.mov(0,0);stub.store(0,9,0x48)
        stub.mov(6,4);loop=len(stub.words);stub.read(0,9,8);stub.emit(0xe3100020)
        no_byte=len(stub.words);stub.emit(0);stub.read(0,9,4);stub.emit(0xe3500071)
        found=len(stub.words);stub.emit(0)
        timer=len(stub.words);stub.read(0,9,0x10);stub.emit(0xe3100020);stub.branch(loop,0)
        stub.mov(0,32);stub.store(0,9,0x14);stub.emit(0xe2566001);stub.branch(loop,1)
        stub.words[no_byte]=0x0a000000|((timer-no_byte-2)&0xffffff)
        # ARM7500 has DRAM video and its own synthesizer; select it by the
        # architectural CPU ID, so the same authored OS tuple serves its pairs.
        stub.emit(0xee100f10);stub.load(1,0x41027100);stub.emit(0xe1500001)
        a7000=len(stub.words);stub.emit(0)
        ordinary=program(2,640,256,3,60,'plain');dram=program(3,640,256,3,60,'plain')
        stub.words[a7000]=0x0a000000|((len(stub.words)+len(ordinary)-a7000-2)&0xffffff)
        base=4096;stub.words[found]=0x0a000000|((resume-(base+found)-2)&0xffffff)
        combined=stub.words+ordinary+dram
        words[0]=0xea000000|((base-2)&0xffffff);words[base:base+len(combined)]=combined
        data=struct.pack('<%dI'%len(words),*words)
        (firmware/(name+'.rom')).write_bytes(data)
        rows.append(f'{{{os},{len(data)}u,UINT32_C(0x{zlib.crc32(data):08x}),"{hashlib.sha1(data).hexdigest()}"}}')
    (out/'rpc_route_catalogue.c').write_text('/* Authored catalogue substitution. GPL-2.0-only. */\n#include "rpc_selection.h"\nstatic const ArcRpcFirmwareFingerprint table[]={'+','.join(rows)+'};\nconst ArcRpcFirmwareFingerprint *arc_riscpc_firmware_catalogue(size_t *n){if(n)*n=4;return table;}\n')
    disc=bytearray(build_extended_jfd(bytes(256),b'Authored route disc'))
    struct.pack_into('<I',disc,304,(16<<8)|(8<<16))
    (out/'route.jfd').write_bytes(disc)
    return out

if __name__=='__main__':generate_route(sys.argv[1])
