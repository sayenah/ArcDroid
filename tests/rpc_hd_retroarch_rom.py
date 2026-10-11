#!/usr/bin/env python3
"""Authored native-RetroArch IDE persistence guest/fixtures. SPDX-License-Identifier: GPL-2.0-only.

One ROM, three disc-selected sessions. No firmware or external assets.
PIO words = (word_index ^ (word_index >> 16) ^ generation_seed) & 65535.
The completion sector is committed only after payload acknowledgement and a
2-second virtual timer wait (120 maintenance ticks at this guest's 60 Hz).
"""
from pathlib import Path
import hashlib
import struct
import sys
import zlib
sys.dont_write_bytecode = True
from rpc_video_rom import Guest
from generate_fixtures import build_d_discovery

CONTENT = 'c' * 40
MARKER_LBA = 4096
DATA_LBA = 8192
DATA_BYTES = 2 * 1024 * 1024
COLOURS = {1: (0, 255, 255), 2: (0, 0, 255), 3: (0, 255, 0), 255: (255, 0, 0)}

class Program(Guest):
    def __init__(self):
        super().__init__()
        self.w = [0xeafffffe] * 64
        self.w[0] = 0xea00003e
        self.labels = {}
        self.branches = []
    def label(self, name): self.labels[name] = len(self.w)
    def branch(self, name, cond=14):
        self.branches.append((len(self.w), name, cond)); self.emit(0)
    def ready(self, drq=False):
        start = len(self.w)
        self.emit(0xe5da0000, 0xe3100001)  # alternate status; ERR -> red
        self.branch('fail', 1)
        self.emit(0xe3100080)
        self.emit(0x1a000000 | ((start-len(self.w)-2) & 0xffffff))
        if drq:
            self.emit(0xe3100008)
            self.emit(0x0a000000 | ((start-len(self.w)-2) & 0xffffff))
    def command(self, opcode, count):
        self.ready()
        # LBA28 master, count=0 denotes 256 sectors.
        self.emit(0xe3a000e0, 0xe5c90018, 0xe1a00827, 0xe5c90014,
                  0xe1a00427, 0xe5c90010, 0xe5c9700c)
        self.literal(0, count); self.emit(0xe5c90008)
        self.literal(0, opcode); self.emit(0xe5c9001c)
    def identity(self, compare=False):
        self.command(0xec, 1); self.ready(True)
        self.literal(2, 256); self.literal(11, 0x10030000)
        start=len(self.w)
        self.emit(0xe5990000)
        if compare:
            self.emit(0xe49b1004, 0xe1500001); self.branch('fail', 1)
        else: self.emit(0xe48b0004)
        self.emit(0xe2522001)
        self.emit(0x1a000000 | ((start-len(self.w)-2) & 0xffffff))
        self.ready()
        # Independent geometry/capacity checks, plus full 512-byte identity
        # comparison across the real frontend floppy eject/insert.
        # ATA byte offsets; saved PIO words occupy four RAM bytes each.
        for offset, value in ((2, 256), (6, 16), (12, 63), (120, 0xf000), (122, 3)):
            self.literal(11, 0x10030000 + offset*2)  # each PIO word stored as uint32
            self.emit(0xe59b0000); self.literal(1, value)
            self.emit(0xe1500001); self.branch('fail', 1)
    def pattern(self):
        self.emit(0xe0250825, 0xe0200006, 0xe1a00800, 0xe1a00820)
    def payload(self, write):
        self.literal(7, DATA_LBA); self.literal(4, 16); self.literal(5, 0)
        command=len(self.w); self.command(0x30 if write else 0x20, 0)
        self.literal(2, 256)
        sector=len(self.w); self.ready(True); self.literal(3, 256)
        word=len(self.w); self.pattern()
        if write: self.emit(0xe5890000)
        else:
            self.emit(0xe5991000, 0xe1500001); self.branch('fail', 1)
        self.emit(0xe2855001, 0xe2533001)
        self.emit(0x1a000000 | ((word-len(self.w)-2) & 0xffffff))
        self.emit(0xe2522001)
        self.emit(0x1a000000 | ((sector-len(self.w)-2) & 0xffffff))
        self.ready(); self.emit(0xe2877c01, 0xe2544001)  # LBA += 256
        self.emit(0x1a000000 | ((command-len(self.w)-2) & 0xffffff))
    def screen(self, status):
        red, green, blue = COLOURS[status]
        self.v(0x10000000); self.v(red | green<<8 | blue<<16)
        self.literal(0, status); self.literal(1, 0x10020000); self.emit(0xe5810000, 0xeafffffe)
    def finish(self):
        for pos, name, cond in self.branches:
            self.w[pos] = cond<<28 | 0x0a000000 | ((self.labels[name]-pos-2) & 0xffffff)
        raw=struct.pack('<'+'I'*len(self.w), *self.w)
        assert len(raw)<2097152
        return raw + struct.pack('<I', 0xeafffffe)*((2097152-len(raw))//4)

def rom():
    g=Program(); g.literal(0, 0x70); g.emit(0xee010f10)
    g.literal(0, 0xd3); g.emit(0xe121f000)
    # Visible 320x256/8bpp 60 Hz raster, all pixels palette index zero.
    g.fill(0x02000000, 20480, 0)
    for address,value in ((0x032001d8,0x02000000),(0x032001dc,0x02000000),
                          (0x032001d4,0x02013800),(0x032001e0,0x28)):
        g.write(address,value)
    for value in (0x10000000,0,0x83000000,0x84000140,0x93000002,
                  0x94000102,0x80000318,0x900001f2,0xd0000000,0xe0000060): g.v(value)
    g.literal(9,0x030107c0);g.literal(10,0x03010fd8)
    g.literal(0,2);g.emit(0xe5ca0000)  # nIEN, polling guest
    g.literal(7,0);g.identity()
    g.wait(300000)  # floppy script frames 4/6 run between IDENTIFYs
    g.identity(True)
    g.literal(7,MARKER_LBA);g.command(0x20,1);g.ready(True)
    g.literal(2,256);g.literal(11,0x10031000)
    loop=len(g.w);g.emit(0xe5990000,0xe48b0004,0xe2522001)
    g.emit(0x1a000000 | ((loop-len(g.w)-2)&0xffffff));g.ready()
    g.literal(11,0x10031000);g.emit(0xe59b0000,0xe3500000);g.branch('blank',0)
    for offset,value in ((0,0x4331),(4,0x5248)):
        g.emit(0xe59b0000|offset);g.literal(1,value);g.emit(0xe1500001);g.branch('fail',1)
    g.emit(0xe59bc008,0xe35c0001);g.branch('valid',0)
    g.emit(0xe35c0002);g.branch('fail',1)
    g.label('valid');g.emit(0xe59b000c,0xe1e0100c,0xe1a01801,0xe1a01821,0xe1500001);g.branch('fail',1)
    g.literal(2,252);g.emit(0xe28bb010);g.branch('marker_tail')
    g.label('blank');g.literal(12,0);g.literal(2,256)
    g.label('marker_tail');loop=len(g.w);g.emit(0xe49b0004,0xe3500000);g.branch('fail',1)
    g.emit(0xe2522001);g.emit(0x1a000000|((loop-len(g.w)-2)&0xffffff))
    g.emit(0xe35c0000);g.branch('write1',0)
    g.literal(6,0x1357);g.emit(0xe35c0001);g.branch('read',0);g.literal(6,0x2468)
    g.label('read');g.payload(False)
    g.emit(0xe35c0002);g.branch('pass',0)
    g.literal(12,2);g.literal(6,0x2468);g.branch('write')
    g.label('write1');g.literal(12,1);g.literal(6,0x1357)
    g.label('write');g.payload(True)
    g.wait(2000000)  # drain complete checkpoint/retirement before completion
    g.literal(7,MARKER_LBA);g.command(0x30,1);g.ready(True)
    for value in (0x4331,0x5248):g.literal(0,value);g.emit(0xe5890000)
    g.emit(0xe589c000,0xe1e0000c,0xe5890000)
    g.literal(0,0);g.literal(2,252);loop=len(g.w)
    g.emit(0xe5890000,0xe2522001);g.emit(0x1a000000|((loop-len(g.w)-2)&0xffffff));g.ready()
    g.emit(0xe35c0001);g.branch('written1',0);g.screen(2)
    g.label('written1');g.screen(1)
    g.label('pass');g.screen(3)
    g.label('fail');g.screen(255)
    return g.finish()

def fixtures(directory):
    d=Path(directory);d.mkdir(parents=True,exist_ok=True)
    raw=rom();(d/'hd.rom').write_bytes(raw)
    (d/'floppy.adf').write_bytes(build_d_discovery([],disc_name=b'R2cGuest'))
    (d/'disk.script').write_text('4 eject\n6 insert\n',encoding='ascii')
    (d/'hd.arcplan').write_text(f'version=2\nmodel=arm610\nfirmware=hd.rom\ncmos=persist\ncmos_layout=ro370\ncontent_id={CONTENT}\nrtc_epoch=946684800\nhard_disc=on\nmedia=floppy.adf\ndisk_control=disk.script\n',encoding='ascii')
    digest=hashlib.sha256(raw).digest();b=bytearray(512);b[:8]=b'ARCRPCM1'
    struct.pack_into('<IIIIQ',b,8,1,512,1,0x10003,1);b[32:72]=CONTENT.encode();b[72:104]=digest
    struct.pack_into('<I',b,508,zlib.crc32(b[:508]))
    root=d/'save/arcdroid/cmos-riscpc';root.mkdir(parents=True,exist_ok=True)
    (root/f'{CONTENT}-m1-{digest.hex()}.1').write_bytes(b)

if __name__=='__main__':fixtures(sys.argv[1])
