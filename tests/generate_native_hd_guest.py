#!/usr/bin/env python3
"""Project-authored ARMv2 IDE save/readback ROM; no install doorbell."""
from pathlib import Path
import struct

from generate_fixtures import Arm2, ROM_SIZE, VIDC_MODE

IDE = 0x033107c0
LBA = 15456
rom = bytearray(b"\xff" * ROM_SIZE)
struct.pack_into("<I", rom, 0, 0xE59FF000)
struct.pack_into("<I", rom, 8, 0x03800020)
rom[0x3000] = 0x5a
rom[0x1f00:0x1f20] = b"ARCDROID NATIVE IDE GUEST v1\0".ljust(32, b"\0")
a = Arm2(0x03800020)

# Enable RAM and map six pages, following the other authored ARM guests.
a.ldr_const(0, 0x036E050C)
a.mov(1, 0); a.str(1, 0)
for page in range(6):
    cam = (0x03800000 | ((page & 0x0F) << 3) |
           ((page & 0x10) >> 4) | ((page & 0x20) >> 3) |
           ((page & 0x40) >> 5) | (page & 0x80) |
           ((page & 0x100) << 4) | (3 << 8) |
           ((page & 0xFF) << 15) | ((page & 0x300) << 2))
    a.ldr_const(0, cam); a.str(1, 0)

a.ldr_const(0, IDE)
a.ldr_const(4, LBA)

# The guest also proves that the attached device answers IDENTIFY. Consume
# the whole response so the following READ starts a separate command.
a.mov(1, 0xec); a.str(1, 0, 28, byte=True)
a.ldr_const(5, 200000)
a.label("identify_wait")
a.ldr(1, 0, 28, byte=True)
a.tst_imm(1, 8); a.branch("identify_ready", "ne")
a.tst_imm(1, 1); a.branch("failure", "ne")
a.subs_imm(5, 5, 1); a.branch("identify_wait", "ne")
a.branch("failure")
a.label("identify_ready")
a.ldr(1, 0, byte=True); a.ldr(1, 0, byte=True)
a.ldr(6, 0, byte=True)
a.cmp_imm(6, 32); a.branch("failure", "ne")
a.ldr(1, 0, byte=True)
a.ldr_const(5, 508)
a.label("identify_data")
a.ldr(1, 0, byte=True)
a.subs_imm(5, 5, 1); a.branch("identify_data", "ne")

def address() -> None:
    a.mov(1, 1); a.str(1, 0, 8, byte=True)
    a.str(4, 0, 12, byte=True)
    for shift, offset in ((8, 16), (16, 20)):
        a.emit(0xE1A00020 | (1 << 12) | (shift << 7) | 4)
        a.str(1, 0, offset, byte=True)
    a.mov(1, 0xe0); a.str(1, 0, 24, byte=True)

address()
a.mov(1, 0x20); a.str(1, 0, 28, byte=True)
a.ldr_const(5, 200000)
a.label("read_wait")
a.ldr(1, 0, 28, byte=True)
a.tst_imm(1, 8); a.branch("read_ready", "ne")
a.tst_imm(1, 1); a.branch("failure", "ne")
a.subs_imm(5, 5, 1); a.branch("read_wait", "ne")
a.branch("failure")
a.label("read_ready")
a.ldr(6, 0, byte=True)
a.ldr_const(5, 511)
a.label("read_rest")
a.ldr(1, 0, byte=True)
a.subs_imm(5, 5, 1); a.branch("read_rest", "ne")
a.cmp_imm(6, 0x5a); a.branch("reloaded", "eq")
a.cmp_imm(6, 0); a.branch("write", "eq")
a.branch("failure")
a.label("write")
address()
a.mov(1, 0x30); a.str(1, 0, 28, byte=True)
a.ldr_const(10, 0x03803000)
a.ldr_const(5, 512)
a.label("write_data")
a.ldr_post(7, 10, 1, byte=True)
a.str(7, 0, byte=True)
a.subs_imm(5, 5, 1); a.branch("write_data", "ne")
a.ldr_const(5, 200000)
a.label("write_wait")
a.ldr(1, 0, 28, byte=True)
a.tst_imm(1, 0x80); a.branch("write_done", "eq")
a.subs_imm(5, 5, 1); a.branch("write_wait", "ne")
a.branch("failure")
a.label("write_done")
a.tst_imm(1, 1); a.branch("write_abrt", "ne")
a.label("saved")
a.mov(6, 0x31); a.branch("paint")
a.label("write_abrt")
# A guest IDE software reset must not clear the store's failed-write latch.
# Retry the command header: ABRT must arrive before a data phase begins.
a.ldr_const(8, IDE + (0x3f6 - 0x1f0) * 4)
a.mov(1, 4); a.str(1, 8, byte=True)
a.mov(1, 0); a.str(1, 8, byte=True)
a.ldr_const(5, 200000)
a.label("reset_wait")
a.ldr(1, 0, 28, byte=True)
a.tst_imm(1, 0x80); a.branch("reset_done", "eq")
a.subs_imm(5, 5, 1); a.branch("reset_wait", "ne")
a.branch("unexpected_reset")
a.label("reset_done")
address()
a.mov(1, 0x30); a.str(1, 0, 28, byte=True)
a.ldr(1, 0, 28, byte=True)
a.tst_imm(1, 1); a.branch("failure", "ne")
a.label("unexpected_reset")
a.mov(6, 0x99); a.branch("paint")
a.label("reloaded")
a.mov(6, 0x42); a.branch("paint")
a.label("failure")
a.mov(6, 0xee)
a.label("paint")
a.orr_lsl(6, 6, 6, 8); a.orr_lsl(6, 6, 6, 16)
a.mov(1, 0)
a.ldr_const(5, 40960)
a.label("fill_video")
a.str_post(6, 1)
a.subs_imm(5, 5, 1); a.branch("fill_video", "ne")
a.ldr_const(1, 0x03400000)
for register in VIDC_MODE:
    a.ldr_const(0, register); a.str(0, 1)
for addr in (0x03600000, 0x03620000, 0x0364A000,
             0x0368A000, 0x036AA0FC, 0x036CA000):
    a.ldr_const(0, addr); a.str(1, 0)
a.ldr_const(0, 0x036E050C); a.str(1, 0)
a.label("steady"); a.branch("steady")
code = a.finish()
rom[0x20:0x20 + len(code)] = code
Path(__import__("sys").argv[1]).write_bytes(rom)
