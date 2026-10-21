"""Authored ARM2 calendar reader over IOC/I2C; no external inputs. GPL-2.0-only."""
import struct, subprocess, sys, tempfile
from pathlib import Path
from generate_fixtures import Arm2

def firmware():
    a = Arm2(0x03800100)
    a.conditions = {**a.conditions, 'cc': 3}
    a.mov(9, 0x03200000)
    a.mov(8, 0x02000000)
    def pins(*values):
        for value in values:
            a.mov(3, value); a.str(3, 9, byte=True)
    def send(value):
        for shift in range(7, -1, -1):
            bit = (value >> shift) & 1
            pins(bit, bit | 2)
        pins(1, 3, 0)
    pins(3, 2); send(0xa0); send(0); pins(0, 2, 3)
    pins(3, 2); send(0xa1)
    a.mov(6, 0); a.mov(11, 1)
    a.label('byte'); a.mov(7, 0); a.mov(5, 8)
    a.label('bit'); pins(1, 3); a.ldr(0, 9, byte=True)
    for word in (0xe1a07087, 0xe2000001, 0xe1877000, 0xe2555001): a.emit(word)
    a.branch('bit', 'ne')
    for word in (0xe7887106, 0xe35600ff, 0xe3a03000, 0x03a03001): a.emit(word)
    a.str(3, 9, byte=True); a.emit(0xe3833002); a.str(3, 9, byte=True)
    a.emit(0xe356003f); a.emit(0x01a0c007)
    a.cmp_imm(6, 16); a.branch('skip', 'cc')
    a.emit(0xe356003f); a.emit(0x108bb007)
    a.label('skip'); a.emit(0xe2866001); a.cmp_imm(6, 256); a.branch('byte','ne')
    pins(0, 2, 3)
    for word in (0xe20bb0ff,0xe15b000c,0xe3a00000,0x03a00001): a.emit(word)
    a.str(0,8,0x404)
    a.ldr(0,8,0xc1*4); a.mov(1,100); a.emit(0xe0020190)
    a.ldr(0,8,0xc0*4); a.emit(0xe0822000)
    a.ldr(0,8,5*4)
    for word in (0xe1a00320,0xe0400002,0xe2000003,0xe0822000): a.emit(word)
    a.str(2,8,0x400); a.mov(0,0xa5); a.str(0,8,0x408)
    a.label('idle'); a.branch('idle')
    rom=bytearray(struct.pack('<I',0xeafffffe)*(524288//4))
    struct.pack_into('<III',rom,0,0xe59ff000,0xeafffffe,0x03800100)
    code=a.finish(); rom[0x100:0x100+len(code)]=code
    return rom

if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='arc-calendar-') as temp:
        rom=Path(temp)/'calendar.rom'; rom.write_bytes(firmware())
        subprocess.run([sys.argv[1],sys.argv[2],str(rom)],check=True,timeout=180)
