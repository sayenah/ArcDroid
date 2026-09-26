#!/usr/bin/env python3
"""Build an authored ARMv2 IDE-install guest from a synthetic FileCore tree."""
from pathlib import Path
import argparse
import struct
from generate_fixtures import Arm2, ROM_SIZE, VIDC_MODE

SECTOR = 512
TABLE_OFFSET = 0x4000
SAVE_OFFSET = 0x3000
SAVE_LBA = 31840
IDE_BASE = 0x033107c0


def build(candidate: bytes, target: tuple[int, int, int]) -> bytearray:
    if len(candidate) != 16515072 or any(not 0 <= n <= 255 for n in target):
        raise ValueError("invalid synthetic install inputs")
    sectors = [(i, candidate[i * SECTOR:(i + 1) * SECTOR])
               for i in range(len(candidate) // SECTOR)
               if any(candidate[i * SECTOR:(i + 1) * SECTOR])]
    table = bytearray()
    for lba, data in sectors:
        table += struct.pack("<I", lba) + data
    table += struct.pack("<I", 0xffffffff)
    if TABLE_OFFSET + len(table) > ROM_SIZE:
        raise ValueError("guest write table exceeds ROM")
    rom = bytearray(b"\xff" * ROM_SIZE)
    rom[0:4] = struct.pack("<I", 0xE59FF000)
    rom[8:12] = struct.pack("<I", 0x03800020)
    rom[TABLE_OFFSET:TABLE_OFFSET + len(table)] = table
    rom[SAVE_OFFSET] = 0x5a
    rom[0x1f00:0x1f00 + 32] = b"ARCDROID INSTALL GUEST ROM v1\0".ljust(32, b"\0")
    a = Arm2(0x03800020)

    def lsr(rd: int, rm: int, shift: int) -> None:
        a.emit(0xE1A00020 | (rd << 12) | (shift << 7) | rm)

    def ide_address() -> None:
        # LBA in r4, IDE register base in r0. Count is one sector.
        a.mov(1, 1); a.str(1, 0, 8, byte=True)
        a.str(4, 0, 12, byte=True)
        lsr(1, 4, 8); a.str(1, 0, 16, byte=True)
        lsr(1, 4, 16); a.str(1, 0, 20, byte=True)
        a.mov(1, 0xe0); a.str(1, 0, 24, byte=True)

    def write_sector(tag: str) -> None:
        # r10 points at 512 source bytes and advances to the next record.
        ide_address()
        a.mov(1, 0x30); a.str(1, 0, 28, byte=True)
        a.ldr_const(5, 512)
        a.label(tag + "_data")
        a.ldr_post(7, 10, 1, byte=True)
        a.str(7, 0, byte=True)
        a.subs_imm(5, 5, 1); a.branch(tag + "_data", "ne")
        a.ldr_const(5, 200000)
        a.label(tag + "_wait")
        a.ldr(1, 0, 28, byte=True)
        a.tst_imm(1, 0x80); a.branch(tag + "_tick", "ne")
        a.tst_imm(1, 1); a.branch("failure", "ne")
        a.branch(tag + "_done")
        a.label(tag + "_tick")
        a.subs_imm(5, 5, 1); a.branch(tag + "_wait", "ne")
        a.branch("failure")
        a.label(tag + "_done")

    a.ldr_const(0, 0x036E050C)
    a.mov(1, 0); a.str(1, 0)
    for page in range(6):
        cam = (0x03800000 | ((page & 0x0F) << 3) |
               ((page & 0x10) >> 4) | ((page & 0x20) >> 3) |
               ((page & 0x40) >> 5) | (page & 0x80) |
               ((page & 0x100) << 4) | (3 << 8) |
               ((page & 0xFF) << 15) | ((page & 0x300) << 2))
        a.ldr_const(0, cam); a.str(1, 0)
    a.ldr_const(0, IDE_BASE)
    a.ldr_const(4, SAVE_LBA)
    ide_address()
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
    a.cmp_imm(6, 0x73); a.branch("save_game", "eq")

    a.ldr_const(10, 0x03800000 + TABLE_OFFSET)
    a.ldr_const(8, 0xffffffff)
    a.label("install_next")
    a.ldr_post(4, 10)
    a.cmp_reg(4, 8); a.branch("ring_doorbell", "eq")
    write_sector("install")
    a.ldr_const(5, 500000)
    a.label("install_pace")
    a.subs_imm(5, 5, 1); a.branch("install_pace", "ne")
    a.branch("install_next")

    a.label("ring_doorbell")
    # 82C711: select drive 1, seek, sense completion, issue READ SECTOR.
    a.ldr_const(0, 0x03310FC8)
    a.mov(1, 0x2d); a.str(1, 0, byte=True)
    a.ldr_const(0, 0x03310FDC)
    a.mov(1, 1); a.str(1, 0, byte=True)
    a.ldr_const(0, 0x03310FD4)
    for value in (0x0f, 1, target[0]):
        a.mov(1, value); a.str(1, 0, byte=True)
    a.ldr_const(2, 0x03200020)
    a.ldr_const(5, 500000)
    a.label("seek_wait")
    a.ldr(1, 2, byte=True); a.tst_imm(1, 0x10)
    a.branch("seek_done", "ne")
    a.subs_imm(5, 5, 1); a.branch("seek_wait", "ne")
    a.branch("failure")
    a.label("seek_done")
    a.mov(1, 8); a.str(1, 0, byte=True)
    a.ldr_const(3, 0x03310FD0)
    a.ldr(1, 3, byte=True); a.ldr(1, 0, byte=True)
    for value in (6, 1, target[0], target[1], target[2], 3, 4, 0x1b, 0xff):
        a.mov(1, value); a.str(1, 0, byte=True)
    a.label("doorbell_wait")
    a.branch("doorbell_wait")

    a.label("save_game")
    a.ldr_const(0, IDE_BASE)
    a.ldr_const(4, SAVE_LBA)
    a.ldr_const(10, 0x03800000 + SAVE_OFFSET)
    write_sector("save")
    a.mov(6, 0x31); a.branch("paint")
    a.label("reloaded")
    # Nine whole 256-sector commands exceed the 1 MiB load-time compaction
    # threshold while leaving the four-byte save signature untouched.
    a.ldr_const(0, IDE_BASE)
    a.ldr_const(4, 2000)
    a.mov(9, 9)
    a.label("bulk_next")
    ide_address()
    a.mov(1, 0); a.str(1, 0, 8, byte=True)  # count 0 = 256
    a.mov(1, 0x30); a.str(1, 0, 28, byte=True)
    a.mov(8, 256)
    a.mov(7, 0x5a)
    a.label("bulk_sector")
    a.ldr_const(5, SECTOR)
    a.label("bulk_data")
    a.str(7, 0, byte=True)
    a.subs_imm(5, 5, 1); a.branch("bulk_data", "ne")
    a.subs_imm(8, 8, 1); a.branch("bulk_final", "eq")
    a.ldr_const(5, 500000)
    a.label("bulk_drq_wait")
    a.ldr(1, 0, 28, byte=True)
    a.tst_imm(1, 8); a.branch("bulk_sector", "ne")
    a.tst_imm(1, 1); a.branch("failure", "ne")
    a.subs_imm(5, 5, 1); a.branch("bulk_drq_wait", "ne")
    a.branch("failure")
    a.label("bulk_final")
    a.ldr_const(5, 500000)
    a.label("bulk_wait")
    a.ldr(1, 0, 28, byte=True)
    a.tst_imm(1, 0x80); a.branch("bulk_tick", "ne")
    a.tst_imm(1, 1); a.branch("failure", "ne")
    a.branch("bulk_done")
    a.label("bulk_tick")
    a.subs_imm(5, 5, 1); a.branch("bulk_wait", "ne")
    a.branch("failure")
    a.label("bulk_done")
    a.add_imm(4, 4, 256)
    a.subs_imm(9, 9, 1); a.branch("bulk_next", "ne")
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
    for address in (0x03600000, 0x03620000, 0x0364A000,
                    0x0368A000, 0x036AA0FC, 0x036CA000):
        a.ldr_const(0, address); a.str(1, 0)
    a.ldr_const(0, 0x036E050C); a.str(1, 0)
    a.label("steady")
    a.branch("steady")
    code = a.finish()
    rom[0x20:0x20 + len(code)] = code
    return rom


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    coordinates = tuple(map(int, args.target.read_text().split()))
    args.output.write_bytes(build(args.candidate.read_bytes(), coordinates))
