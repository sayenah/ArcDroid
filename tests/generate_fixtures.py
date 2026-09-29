#!/usr/bin/env python3
"""Generate ArcDroid's deterministic validation ROM and floppy images.

The tiny assembler emits ARM2 words directly because current GCC and LLVM no
longer target ARMv2. The generated firmware configures MEMC/VIDC, reads sector
zero through WD1772, seeds the picture from that data, enables sound DMA, and
alternates two programmed field rates. No third-party firmware is used.

Copyright 2026 ArcDroid contributors. GPL-2.0-only.
"""
from pathlib import Path
import argparse
import gzip
import io
import struct
import zipfile
import zlib

ROM_SIZE = 512 * 1024
ADF_SIZE = 800 * 1024
ADL_SIZE = 640 * 1024
OLD_MAP_SIZE = 512
OLD_DIRECTORY_SIZE = 1280
SECTOR_SIZE = 256


class Arm2:
    """Deliberately small ARMv2 encoder with branch and literal fixups."""

    conditions = {"eq": 0x0, "ne": 0x1, "al": 0xE}

    def __init__(self, base: int):
        self.base = base
        self.words: list[int] = []
        self.labels: dict[str, int] = {}
        self.branches: list[tuple[int, str, int]] = []
        self.literals: list[tuple[int, int]] = []

    def emit(self, word: int) -> None:
        self.words.append(word & 0xFFFFFFFF)

    def label(self, name: str) -> None:
        self.labels[name] = len(self.words)

    @staticmethod
    def _imm(value: int) -> int:
        value &= 0xFFFFFFFF
        for rotate in range(16):
            amount = rotate * 2
            candidate = ((value << amount) | (value >> (32 - amount))) & 0xFFFFFFFF
            if candidate <= 0xFF:
                restored = ((candidate >> amount) | (candidate << (32 - amount))) & 0xFFFFFFFF
                if restored == value:
                    return (rotate << 8) | candidate
        raise ValueError(f"ARM immediate is not encodable: {value:#x}")

    def mov(self, rd: int, value: int) -> None:
        self.emit(0xE3A00000 | (rd << 12) | self._imm(value))

    def mov_reg(self, rd: int, rm: int) -> None:
        self.emit(0xE1A00000 | (rd << 12) | rm)

    def subs_imm(self, rd: int, rn: int, value: int) -> None:
        self.emit(0xE2500000 | (rn << 16) | (rd << 12) | self._imm(value))

    def add_imm(self, rd: int, rn: int, value: int) -> None:
        self.emit(0xE2800000 | (rn << 16) | (rd << 12) | self._imm(value))

    def eor(self, rd: int, rn: int, rm: int) -> None:
        self.emit(0xE0200000 | (rn << 16) | (rd << 12) | rm)

    def orr_lsl(self, rd: int, rn: int, rm: int, shift: int) -> None:
        self.emit(0xE1800000 | (rn << 16) | (rd << 12) | (shift << 7) | rm)

    def add_lsl(self, rd: int, rn: int, rm: int, shift: int) -> None:
        self.emit(0xE0800000 | (rn << 16) | (rd << 12) | (shift << 7) | rm)

    def tst_imm(self, rn: int, value: int) -> None:
        self.emit(0xE3100000 | (rn << 16) | self._imm(value))

    def cmp_imm(self, rn: int, value: int) -> None:
        self.emit(0xE3500000 | (rn << 16) | self._imm(value))

    def cmp_reg(self, rn: int, rm: int) -> None:
        self.emit(0xE1500000 | (rn << 16) | rm)

    def swp(self, rd: int, rm: int, rn: int, byte: bool = False) -> None:
        self.emit(0xE1000090 | (0x00400000 if byte else 0) |
                  (rn << 16) | (rd << 12) | rm)

    def ldr_const(self, rd: int, value: int) -> None:
        index = len(self.words)
        self.emit(rd << 12)
        self.literals.append((index, value & 0xFFFFFFFF))

    def ldr(self, rd: int, rn: int, offset: int = 0, byte: bool = False) -> None:
        self.emit(0xE5900000 | (0x00400000 if byte else 0) |
                  (rn << 16) | (rd << 12) | offset)

    def ldr_post(self, rd: int, rn: int, offset: int = 4,
                 byte: bool = False) -> None:
        self.emit(0xE4900000 | (0x00400000 if byte else 0) |
                  (rn << 16) | (rd << 12) | offset)

    def str(self, rd: int, rn: int, offset: int = 0, byte: bool = False) -> None:
        self.emit(0xE5800000 | (0x00400000 if byte else 0) |
                  (rn << 16) | (rd << 12) | offset)

    def str_post(self, rd: int, rn: int, offset: int = 4,
                 byte: bool = False) -> None:
        self.emit(0xE4800000 | (0x00400000 if byte else 0) |
                  (rn << 16) | (rd << 12) | offset)

    def branch(self, label: str, condition: str = "al") -> None:
        index = len(self.words)
        self.emit(0)
        self.branches.append((index, label, self.conditions[condition]))

    def finish(self) -> bytes:
        for index, label, condition in self.branches:
            displacement = self.labels[label] - index - 2
            self.words[index] = ((condition << 28) | 0x0A000000 |
                                 (displacement & 0xFFFFFF))
        for index, value in self.literals:
            literal_index = len(self.words)
            offset = (self.base + literal_index * 4) - (self.base + index * 4 + 8)
            if not 0 <= offset <= 0xFFF:
                raise ValueError("literal pool out of range")
            rd = (self.words[index] >> 12) & 0xF
            self.words[index] = 0xE59F0000 | (rd << 12) | offset
            self.words.append(value)
        if len(self.words) * 4 >= 0x1EE0:
            raise ValueError("validation program exceeds its reserved ROM area")
        return b"".join(struct.pack("<I", word) for word in self.words)


VIDC_MODE = (
    0xC0000100, 0x807FC000, 0x8408C000, 0x8810C000, 0x8C1B4000,
    0x906B4000, 0x9476C000, 0x9C400000, 0xA04DC000, 0xA4008000,
    0xA8048000, 0xAC08C000, 0xB048C000, 0xB44D0000, 0xE00000AE,
    0xB8000000, 0xBC000000,
    0x00000000, 0x04000111, 0x08000222, 0x0C000333,
    0x10000004, 0x14000115, 0x18000226, 0x1C000337,
    0x20000400, 0x24000511, 0x28000622, 0x2C000733,
    0x30000404, 0x34000515, 0x38000626, 0x3C000737, 0x40000FFF,
    0x60000003, 0x64000003, 0x68000003, 0x6C000003,
    0x70000003, 0x74000003, 0x78000003, 0x7C000003,
)

# Keep the chooser application ROM at exactly 50 Hz while retaining the same
# 640-pixel guest display: 1000 pixel clocks per line * 320 lines at 16 MHz.
CHOOSER_VIDC_MODE = (
    VIDC_MODE[0], 0x807CC000, *VIDC_MODE[2:8], 0xA04FC000, *VIDC_MODE[9:]
)


def build_rom(media_exerciser: bool = False,
              jfd_save_probe: bool = False, newio_save_probe: bool = False,
              write_sector: int = 1,
              write_payload: bytes | None = None,
              creation_probe: bool = False,
              virtual_merge_probe: bool = False,
              merge_read_sector: int = 3,
              allocation_mode: str | None = None,
              boot_watch_pattern: str | None = None,
              boot_write_sectors: tuple[int, ...] = (1, 2),
              cmos_exerciser: bool = False,
              short_tail_probe: bool = False,
              quarantine_probe: bool = False,
              power_on_probe: str | None = None,
              boot_watch_kart: bool = False) -> bytearray:
    if power_on_probe not in (None, "early-swi", "early-xswi", "late-kart", "cmos-boot"):
        raise ValueError("unknown power-on probe")
    if boot_watch_pattern not in (None, "prompt", "prompt-write", "prompt-structure", "two-phase", "cli", "cli-confirm", "horizon-edge",
                                  "cli-then-prompt", "desktop"):
        raise ValueError("unknown synthetic boot observation")
    if boot_watch_kart and boot_watch_pattern not in ("prompt", "cli-then-prompt"):
        raise ValueError("KART boot probe needs a supported observation pattern")
    expected_payload = (1024 * len(boot_write_sectors)
                        if boot_watch_pattern == "prompt-structure" else 1024)
    if boot_watch_pattern == "prompt-structure" and (
            not boot_write_sectors or any(not 0 <= sector < 16
                                          for sector in boot_write_sectors)):
        raise ValueError("invalid synthetic structural write sectors")
    if not 0 <= write_sector <= 4 or (write_payload is not None and
                                      len(write_payload) != expected_payload):
        raise ValueError("the synthetic D-format writer needs one full sector")
    if virtual_merge_probe and not (jfd_save_probe and creation_probe):
        raise ValueError("the virtual merge needs the JFD creation probe")
    if not 0 <= merge_read_sector <= 4 or (not virtual_merge_probe and
                                         merge_read_sector != 3):
        raise ValueError("the virtual merge read needs a P-DD5 sector")
    if allocation_mode not in (None, "veto", "claim", "read", "probe-write") or (allocation_mode
            and (not jfd_save_probe or write_payload is None)):
        raise ValueError("allocation ROM needs a mode, JFD result and map bytes")
    rom = bytearray(b"\xFF" * ROM_SIZE)
    rom[0:4] = struct.pack("<I", 0xE59FF000)  # ldr pc, [pc]
    rom[8:12] = struct.pack("<I", 0x03800020)
    a = Arm2(0x03800020)
    if power_on_probe in ("early-swi", "early-xswi"):
        a.mov(0, 15)
        a.emit(0xEF020029 if power_on_probe == "early-xswi" else 0xEF000029)
        # OS_FSControl 15, including X form; the authored vector returns.
    if boot_watch_pattern:
        a.branch("boot_watch_start")
    if cmos_exerciser:
        # Invented guest program; all transfers go through the IOC's I2C pins.
        a.mov(0, 0x03000000)
        a.add_imm(0, 0, 0x00200000)

        def pins(value: int) -> None:
            a.mov(1, value)
            a.str(1, 0, byte=True)

        def start() -> None:
            pins(3); pins(2)

        def stop() -> None:
            pins(0); pins(2); pins(3)

        def send(value: int) -> None:
            for shift in range(7, -1, -1):
                bit = (value >> shift) & 1
                pins(bit); pins(2 | bit)
            pins(1); pins(3); pins(0)  # ACK clock

        def write(raw: int, value: int) -> None:
            start(); send(0xa0); send(raw); send(value); stop()

        def read_check(raw: int, expected: int, failure: str) -> None:
            start(); send(0xa0); send(raw); stop()
            start(); send(0xa1)
            a.mov(4, 0)
            for _ in range(8):
                pins(1); pins(3)
                a.ldr(2, 0, byte=True)
                a.emit(0xE1A04084)  # mov r4, r4, lsl #1
                a.emit(0xE2022001)  # and r2, r2, #1
                a.emit(0xE1844002)  # orr r4, r4, r2
            pins(1); pins(3)  # NACK
            a.cmp_imm(4, expected)
            a.branch(failure, "ne")

        read_check(0x58, 0x77, "first_pass")
        if power_on_probe == "cmos-boot":
            read_check(0x50, 0x10, "failed")
            write(0x59, 0xa5)
            a.branch("idle")
            a.label("first_pass")
            write(0x50, 0x10)  # guest *Configure Boot bit
            write(0x58, 0x77)
            a.mov(15, 0x03800000)  # guest branches through reset vector
            a.label("failed")
            write(0x59, 0xee)
            a.label("idle")
            a.branch("idle")
            code = a.finish()
            rom[0x20:0x20 + len(code)] = code
            marker = b"ARCDROID INVENTED POWER-ON CMOS GUEST v1\0"
            rom[0x1f00:0x1f00 + len(marker)] = marker
            return rom
        write(0xed, 0x96)  # repeat the locked write on every later boot
        read_check(0xed, 0x00, "failed")
        read_check(0x58, 0x77, "failed")
        write(0x59, 0xa5)  # result visible in the persisted record
        a.branch("idle")
        a.label("first_pass")
        write(0xed, 0x96)  # RISC OS 3 ROM-module unplug byte
        write(0x58, 0x77)  # unlocked marker
        write(0x3f, 0x42)  # guest-maintained checksum byte
        a.mov(15, 0x03800000)  # branch through reset vector
        a.label("failed")
        write(0x59, 0xee)
        a.label("idle")
        a.branch("idle")
        code = a.finish()
        rom[0x20:0x20 + len(code)] = code
        marker = b"ARCDROID INVENTED CMOS I2C EXERCISER v1\0"
        rom[0x1f00:0x1f00 + len(marker)] = marker
        return rom

    def poison_merge_buffer(prefix: str) -> None:
        # A missing read must not inherit a zero-filled machine RAM page.
        a.ldr_const(10, 0x00012000)
        a.ldr_const(11, 1024)
        a.mov(7, 0xA5)
        a.label(prefix + "_poison")
        a.str_post(7, 10, 1, byte=True)
        a.subs_imm(11, 11, 1)
        a.branch(prefix + "_poison", "ne")

    def validate_merge_read(prefix: str, pristine: str,
                            written: str, other: str) -> None:
        # Verify all bytes in the ROM. The host's effective-view inspection
        # alone cannot prove that the guest received a successful read.
        a.ldr_const(10, 0x00012000)
        a.ldr(7, 10, byte=True)
        a.cmp_imm(7, 0)
        a.branch(prefix + "_zero", "eq")
        a.cmp_imm(7, 0x5A)
        a.branch(prefix + "_partial", "eq")
        a.branch(other)
        a.label(prefix + "_zero")
        a.ldr_const(11, 1024)
        a.label(prefix + "_zero_loop")
        a.ldr_post(7, 10, 1, byte=True)
        a.cmp_imm(7, 0)
        a.branch(other, "ne")
        a.subs_imm(11, 11, 1)
        a.branch(prefix + "_zero_loop", "ne")
        a.branch(pristine)
        a.label(prefix + "_partial")
        a.ldr_const(10, 0x00012000)
        a.mov(11, 128)
        a.label(prefix + "_partial_head")
        a.ldr_post(7, 10, 1, byte=True)
        a.cmp_imm(7, 0x5A)
        a.branch(other, "ne")
        a.subs_imm(11, 11, 1)
        a.branch(prefix + "_partial_head", "ne")
        a.ldr_const(11, 896)
        a.label(prefix + "_partial_tail")
        a.ldr_post(7, 10, 1, byte=True)
        a.cmp_imm(7, 0)
        a.branch(other, "ne")
        a.subs_imm(11, 11, 1)
        a.branch(prefix + "_partial_tail", "ne")
        a.branch(written)

    def allocation_new_read(prefix: str, expect_rnf: bool) -> None:
        a.ldr_const(0, 0x03310FD4)
        for value in (0x06, 0, 0, 0, 3, 3, 3, 0x1B, 0xFF):
            a.mov(1, value); a.str(1, 0, byte=True)
        if not expect_rnf:
            a.ldr_const(2, 0x03200030)
            a.ldr_const(3, 0x03012000)
            a.ldr_const(5, 1024)
            a.ldr_const(8, 250000)
            a.label(prefix + "_byte_wait")
            a.ldr(7, 2, byte=True)
            a.tst_imm(7, 1); a.branch(prefix + "_byte", "ne")
            a.subs_imm(8, 8, 1)
            a.branch(prefix + "_byte_wait", "ne")
            a.branch("alloc_other")
            a.label(prefix + "_byte")
            a.ldr_const(8, 250000)
            a.subs_imm(5, 5, 1)
            a.branch(prefix + "_last", "eq")
            a.ldr(7, 3, byte=True)
            a.cmp_imm(7, 0); a.branch("alloc_other", "ne")
            a.branch(prefix + "_byte_wait")
            a.label(prefix + "_last")
            a.ldr_const(3, 0x0302A000)
            a.ldr(7, 3, byte=True)
            a.cmp_imm(7, 0); a.branch("alloc_other", "ne")
        a.ldr_const(2, 0x03200020)
        a.ldr_const(8, 500000)
        a.label(prefix + "_irq")
        a.ldr(7, 2, byte=True)
        a.tst_imm(7, 0x10); a.branch(prefix + "_result", "ne")
        a.subs_imm(8, 8, 1); a.branch(prefix + "_irq", "ne")
        a.branch("alloc_other")
        a.label(prefix + "_result")
        a.ldr_const(3, 0x03310FD0); a.ldr(7, 3, byte=True)
        a.ldr(4, 0, byte=True)  # ST0
        a.ldr(7, 0, byte=True)  # ST1
        if expect_rnf:
            a.cmp_imm(4, 0x40); a.branch("alloc_other", "ne")
            a.cmp_imm(7, 0x05); a.branch("alloc_other", "ne")
        else:
            a.cmp_imm(4, 0); a.branch("alloc_other", "ne")
            a.cmp_imm(7, 0); a.branch("alloc_other", "ne")
        for _ in range(5): a.ldr(7, 0, byte=True)

    def allocation_new_write(prefix: str, expect_veto: bool,
                             sector: int = 0) -> None:
        a.ldr_const(0, 0x03310FD4)
        for value in (0x05, 0, 0, 0, sector, 3, sector, 0x1B, 0xFF):
            a.mov(1, value); a.str(1, 0, byte=True)
        a.ldr_const(2, 0x03200030)
        a.ldr_const(3, 0x03012000)
        a.ldr_const(10, 0x03802000)
        a.ldr_const(5, 1024)
        a.ldr_const(8, 250000)
        if sector == 3:
            a.ldr_const(2, 0x03200020)
            a.ldr_const(8, 500000)
            a.branch(prefix + "_irq")
        a.label(prefix + "_byte_wait")
        a.ldr(7, 2, byte=True)
        a.tst_imm(7, 1); a.branch(prefix + "_byte", "ne")
        a.subs_imm(8, 8, 1); a.branch(prefix + "_byte_wait", "ne")
        a.branch("alloc_other")
        a.label(prefix + "_byte")
        a.ldr_const(8, 250000)
        a.ldr_post(7, 10, 1, byte=True)
        a.subs_imm(5, 5, 1)
        a.branch(prefix + "_last", "eq")
        a.str(7, 3, byte=True)
        a.branch(prefix + "_byte_wait")
        a.label(prefix + "_last")
        a.ldr_const(3, 0x0302A000); a.str(7, 3, byte=True)
        a.ldr_const(2, 0x03200020)
        a.ldr_const(8, 500000)
        a.label(prefix + "_irq")
        a.ldr(7, 2, byte=True)
        a.tst_imm(7, 0x10); a.branch(prefix + "_result", "ne")
        a.subs_imm(8, 8, 1); a.branch(prefix + "_irq", "ne")
        a.branch("alloc_other")
        a.label(prefix + "_result")
        a.ldr_const(3, 0x03310FD0); a.ldr(7, 3, byte=True)
        a.ldr(4, 0, byte=True)
        a.ldr(7, 0, byte=True)
        if sector != 3:
            a.cmp_imm(4, 0x40 if expect_veto else 0)
            a.branch("alloc_other", "ne")
            a.cmp_imm(7, 0x20 if expect_veto else 0)
            a.branch("alloc_other", "ne")
        for _ in range(5): a.ldr(7, 0, byte=True)

    def allocation_wd_read(prefix: str, expect_rnf: bool) -> None:
        a.ldr_const(0, 0x03310000)
        a.mov(1, 3); a.str(1, 0, 8, byte=True)
        a.mov(1, 0x80); a.str(1, 0, byte=True)
        a.ldr_const(5, 1024)
        a.ldr_const(8, 500000)
        a.label(prefix + "_wait")
        a.ldr(4, 0, byte=True)
        if not expect_rnf:
            a.tst_imm(4, 2); a.branch(prefix + "_byte", "ne")
        a.tst_imm(4, 1); a.branch(prefix + "_done", "eq")
        a.subs_imm(8, 8, 1); a.branch(prefix + "_wait", "ne")
        a.branch("alloc_other")
        if not expect_rnf:
            a.label(prefix + "_byte")
            a.ldr(7, 0, 12, byte=True)
            a.cmp_imm(7, 0); a.branch("alloc_other", "ne")
            a.subs_imm(5, 5, 1)
            a.ldr_const(8, 500000)
            a.branch(prefix + "_tail", "eq")
            a.branch(prefix + "_wait")
            a.label(prefix + "_tail")
            a.ldr(4, 0, byte=True)
            a.tst_imm(4, 1); a.branch(prefix + "_done", "eq")
            a.subs_imm(8, 8, 1); a.branch(prefix + "_tail", "ne")
            a.branch("alloc_other")
        a.label(prefix + "_done")
        a.cmp_imm(4, 0x10 if expect_rnf else 0x80)
        a.branch("alloc_other", "ne")
        if not expect_rnf:
            a.cmp_imm(5, 0); a.branch("alloc_other", "ne")

    def allocation_wd_write(prefix: str, expect_veto: bool,
                            sector: int = 0) -> None:
        a.ldr_const(0, 0x03310000)
        a.mov(1, sector); a.str(1, 0, 8, byte=True)
        a.mov(1, 0xA0); a.str(1, 0, byte=True)
        a.ldr_const(10, 0x03802000)
        a.ldr_const(5, 1024)
        a.ldr_const(8, 500000)
        a.label(prefix + "_wait")
        a.ldr(4, 0, byte=True)
        a.tst_imm(4, 2); a.branch(prefix + "_byte", "ne")
        a.tst_imm(4, 1); a.branch(prefix + "_done", "eq")
        a.subs_imm(8, 8, 1); a.branch(prefix + "_wait", "ne")
        a.branch("alloc_other")
        a.label(prefix + "_byte")
        a.ldr_post(7, 10, 1, byte=True)
        a.str(7, 0, 12, byte=True)
        a.subs_imm(5, 5, 1)
        a.ldr_const(8, 500000)
        a.branch(prefix + "_tail", "eq")
        a.branch(prefix + "_wait")
        a.label(prefix + "_tail")
        a.ldr(4, 0, byte=True)
        a.tst_imm(4, 1); a.branch(prefix + "_done", "eq")
        a.subs_imm(8, 8, 1); a.branch(prefix + "_tail", "ne")
        a.branch("alloc_other")
        a.label(prefix + "_done")
        if sector != 3:
            a.cmp_imm(4, 0x08 if expect_veto else 0x80)
            a.branch("alloc_other", "ne")
            a.cmp_imm(5, 0); a.branch("alloc_other", "ne")
    a.ldr_const(0, 0x036E050C)  # 32 KiB pages, video DMA, flyback refresh
    a.mov(1, 0)
    a.str(1, 0)

    # Map the first six physical 32 KiB pages at the same logical addresses.
    # MEMC's CAM is programmed by address bits; the stored value is ignored.
    for page in range(6):
        cam = (0x03800000 | ((page & 0x0F) << 3) |
               ((page & 0x10) >> 4) | ((page & 0x20) >> 3) |
               ((page & 0x40) >> 5) | (page & 0x80) |
               ((page & 0x100) << 4) | (3 << 8) |
               ((page & 0xFF) << 15) | ((page & 0x300) << 2))
        a.ldr_const(0, cam)
        a.str(1, 0)

    # Install a minimal undefined-instruction handler in the mapped low page.
    # The A3000 path below proves that both SWP encodings reach it on ARM2.
    a.ldr_const(0, 0x00000008)
    a.ldr_const(1, 0xE28CC001)  # add r12, r12, #1
    a.str(1, 0)
    a.ldr_const(1, 0xE1B0F00E)  # movs pc, lr
    a.str(1, 0, 4)

    # IOEB device ID distinguishes New-IO without relying on a machine option.
    a.ldr_const(0, 0x03350050)
    a.ldr(1, 0, byte=True)
    a.cmp_imm(1, 5)
    a.branch("old_io_fdc", "ne")
    # The New-IO IDE interface is deliberately an absent bus. Exercise both
    # register ranges and require the open-bus value before touching the FDC.
    for address in tuple(0x033107C0 + register * 4 for register in range(8)) + \
                   (0x03310FD8,):
        a.ldr_const(0, address)
        a.ldr(1, 0, byte=True)
        a.cmp_imm(1, 0xFF)
        a.branch("newio_fail", "ne")
        a.mov(1, 0x5A)
        a.str(1, 0, byte=True)
    # ARM250 implements both SWP forms. Validate the returned and stored values
    # before continuing; this block is unreachable on the A3000 ARM2 path.
    a.ldr_const(0, 0x00010000)
    a.mov(1, 0x5A)
    a.str(1, 0)
    a.mov(2, 0xA5)
    a.swp(3, 2, 0)
    a.cmp_imm(3, 0x5A)
    a.branch("newio_fail", "ne")
    a.ldr(3, 0)
    a.cmp_imm(3, 0xA5)
    a.branch("newio_fail", "ne")
    a.mov(1, 0x3C)
    a.str(1, 0, byte=True)
    a.mov(2, 0xC3)
    a.swp(3, 2, 0, byte=True)
    a.cmp_imm(3, 0x3C)
    a.branch("newio_fail", "ne")
    a.ldr(3, 0, byte=True)
    a.cmp_imm(3, 0xC3)
    a.branch("newio_fail", "ne")
    a.ldr_const(0, 0x03310FC8)
    a.mov(1, 0x1C)
    a.str(1, 0, byte=True)
    a.ldr_const(0, 0x03310FDC)
    a.mov(1, 0x01)
    a.str(1, 0, byte=True)
    a.ldr_const(0, 0x03310FD4)
    for value in (0x03, 0xDF, 0x02, 0x07, 0x00):
        a.mov(1, value); a.str(1, 0, byte=True)
    a.ldr_const(2, 0x03200020)
    a.label("new_recal_irq")
    a.ldr(1, 2, byte=True); a.tst_imm(1, 0x10); a.branch("new_recal_irq", "eq")
    a.ldr_const(3, 0x03310FD0); a.ldr(1, 3, byte=True)
    a.mov(1, 0x08); a.str(1, 0, byte=True)
    a.ldr(1, 0, byte=True); a.ldr(1, 0, byte=True)
    for value in (0x0F, 0x00, 0x00):
        a.mov(1, value); a.str(1, 0, byte=True)
    a.label("new_seek_irq")
    a.ldr(1, 2, byte=True); a.tst_imm(1, 0x10); a.branch("new_seek_irq", "eq")
    a.ldr(1, 3, byte=True)
    a.mov(1, 0x08); a.str(1, 0, byte=True)
    a.ldr(1, 0, byte=True); a.ldr(1, 0, byte=True)
    for value in (0x0A, 0x00):
        a.mov(1, value); a.str(1, 0, byte=True)
    a.label("new_read_id_irq")
    a.ldr(1, 2, byte=True); a.tst_imm(1, 0x10); a.branch("new_read_id_irq", "eq")
    a.ldr(1, 3, byte=True)
    for _ in range(7): a.ldr(1, 0, byte=True)
    if short_tail_probe or quarantine_probe:
        # A3020/82C711 guest counterpart to the WD boundary probe. Seek to
        # the final cylinder and select its second side in the command byte.
        for value in (0x0F, 0, 79):
            a.mov(1, value); a.str(1, 0, byte=True)
        a.ldr_const(2, 0x03200020)
        a.ldr_const(8, 2500000)
        a.label("short_new_seek_irq")
        a.ldr(1, 2, byte=True); a.tst_imm(1, 0x10)
        a.branch("short_new_seek_done", "ne")
        a.subs_imm(8, 8, 1); a.branch("short_new_seek_irq", "ne")
        a.branch("short_new_fail")
        a.label("short_new_seek_done")
        a.ldr_const(0, 0x03310FD4)
        a.mov(1, 0x08); a.str(1, 0, byte=True)
        a.ldr(1, 0, byte=True); a.ldr(1, 0, byte=True)
        for value in (0x06, 4, 79, 1, 3, 3, 4, 0x1B, 0xFF):
            a.mov(1, value); a.str(1, 0, byte=True)
        a.ldr_const(2, 0x03200030)
        a.ldr_const(3, 0x03012000)
        a.ldr_const(5, 1024)
        a.ldr_const(8, 2500000)
        a.mov(6, 0)
        a.label("short_new_read_wait")
        a.ldr(7, 2, byte=True); a.tst_imm(7, 1)
        a.branch("short_new_read_byte", "ne")
        a.subs_imm(8, 8, 1); a.branch("short_new_read_wait", "ne")
        a.branch("short_new_fail")
        a.label("short_new_read_byte")
        a.ldr_const(8, 2500000)
        a.subs_imm(5, 5, 1); a.branch("short_new_read_last", "eq")
        a.ldr(7, 3, byte=True)
        a.add_lsl(6, 6, 6, 5); a.eor(6, 6, 7)
        a.branch("short_new_read_wait")
        a.label("short_new_read_last")
        a.ldr_const(3, 0x0302A000); a.ldr(7, 3, byte=True)
        a.add_lsl(6, 6, 6, 5); a.eor(6, 6, 7)
        a.ldr_const(2, 0x03200020)
        a.label("short_new_read_irq")
        a.ldr(7, 2, byte=True); a.tst_imm(7, 0x10)
        a.branch("short_new_read_irq", "eq")
        a.ldr_const(3, 0x03310FD0); a.ldr(7, 3, byte=True)
        a.ldr(4, 0, byte=True)
        for _ in range(6): a.ldr(7, 0, byte=True)
        a.tst_imm(4, 0xC0); a.branch("short_new_fail", "ne")
        a.ldr_const(7, 0x53E21000); a.cmp_reg(6, 7)
        a.branch("short_new_read_ok", "eq")
        a.ldr_const(7, 0x26568000); a.cmp_reg(6, 7)
        a.branch("short_new_fail", "ne")
        a.label("short_new_read_ok")
        for value in ((0x06, 0, 79, 0, 3, 3, 3, 0x1B, 0xFF)
                      if quarantine_probe else
                      (0x06, 4, 79, 1, 4, 3, 4, 0x1B, 0xFF)):
            a.mov(1, value); a.str(1, 0, byte=True)
        a.ldr_const(2, 0x03200030)
        a.ldr_const(8, 2500000)
        a.label("short_new_absent_wait")
        a.ldr_const(3, 0x03200020); a.ldr(7, 3, byte=True)
        a.tst_imm(7, 0x10); a.branch("short_new_absent_done", "ne")
        a.subs_imm(8, 8, 1); a.branch("short_new_absent_wait", "ne")
        a.branch("short_new_fail")
        a.label("short_new_absent_done")
        a.ldr_const(3, 0x03310FD0); a.ldr(7, 3, byte=True)
        a.ldr(4, 0, byte=True); a.ldr(7, 0, byte=True)
        for _ in range(5): a.ldr(1, 0, byte=True)
        a.tst_imm(4, 0x40); a.branch("short_new_fail", "eq")
        a.tst_imm(7, 0x04); a.branch("short_new_fail", "eq")
        if quarantine_probe:
            a.mov(6, 0x10); a.branch("fdc_done")
        for value in (0x05, 4, 79, 1, 3, 3, 4, 0x1B, 0xFF):
            a.mov(1, value); a.str(1, 0, byte=True)
        a.ldr_const(2, 0x03200030)
        a.ldr_const(3, 0x03012000)
        a.mov(7, 0x5A); a.ldr_const(5, 1024)
        a.ldr_const(8, 2500000)
        a.label("short_new_write_wait")
        a.ldr(4, 2, byte=True); a.tst_imm(4, 1)
        a.branch("short_new_write_byte", "ne")
        a.subs_imm(8, 8, 1); a.branch("short_new_write_wait", "ne")
        a.branch("short_new_fail")
        a.label("short_new_write_byte")
        a.ldr_const(8, 2500000)
        a.subs_imm(5, 5, 1); a.branch("short_new_write_last", "eq")
        a.str(7, 3, byte=True); a.branch("short_new_write_wait")
        a.label("short_new_write_last")
        a.ldr_const(3, 0x0302A000); a.str(7, 3, byte=True)
        a.ldr_const(2, 0x03200020)
        a.label("short_new_write_irq")
        a.ldr(4, 2, byte=True); a.tst_imm(4, 0x10)
        a.branch("short_new_write_irq", "eq")
        a.ldr_const(3, 0x03310FD0); a.ldr(7, 3, byte=True)
        a.ldr(4, 0, byte=True)
        for _ in range(6): a.ldr(1, 0, byte=True)
        a.tst_imm(4, 0xC0); a.branch("short_new_fail", "ne")
        a.mov(6, 0x10); a.branch("fdc_done")
        a.label("short_new_fail")
        a.mov(6, 0x80); a.branch("fdc_done")
    if allocation_mode:
        if allocation_mode in ("veto", "probe-write"):
            allocation_new_read("alloc_new_probe", True)
        if allocation_mode != "read":
            allocation_new_write("alloc_new_map", allocation_mode != "claim",
                                 3 if allocation_mode == "probe-write" else 0)
        allocation_new_read("alloc_new_verify", allocation_mode in
                            ("veto", "probe-write"))
        a.mov(6, 0x20 if allocation_mode != "claim" else 0x10)
        a.branch("fdc_done")
    if newio_save_probe:
        # Read the entire sector before writing. The last DMA access asserts
        # TC, and the completed seven-byte result is checked on both paths.
        if virtual_merge_probe:
            poison_merge_buffer("save_new_merge")
        for value in (0x06, 0, 0, 0, merge_read_sector if virtual_merge_probe
                      else 3 if creation_probe else 1,
                      3, 3 if creation_probe else 1, 0x1B, 0xFF):
            a.mov(1, value); a.str(1, 0, byte=True)
        a.ldr_const(2, 0x03200030)
        a.ldr_const(3, 0x03012000)
        a.ldr_const(5, 1024)
        a.ldr_const(8, 250000)
        a.mov(6, 0)
        if virtual_merge_probe:
            a.ldr_const(10, 0x00012000)
        a.label("save_new_read_wait")
        a.ldr(7, 2, byte=True)
        a.tst_imm(7, 1); a.branch("save_new_read_byte", "ne")
        a.ldr_const(1, 0x03200020)
        a.ldr(7, 1, byte=True)
        a.tst_imm(7, 0x10)
        a.branch("save_new_missing" if creation_probe and not virtual_merge_probe
                 else "save_new_other", "ne")
        a.subs_imm(8, 8, 1); a.branch("save_new_read_wait", "ne")
        a.branch("save_new_other")
        a.label("save_new_read_byte")
        a.ldr_const(8, 250000)
        a.subs_imm(5, 5, 1)
        a.branch("save_new_read_last", "eq")
        a.ldr(7, 3, byte=True)
        if virtual_merge_probe:
            a.str_post(7, 10, 1, byte=True)
        a.add_lsl(6, 6, 6, 5); a.eor(6, 6, 7)
        a.branch("save_new_read_wait")
        a.label("save_new_read_last")
        a.ldr_const(3, 0x0302A000)
        a.ldr(7, 3, byte=True)
        if virtual_merge_probe:
            a.str_post(7, 10, 1, byte=True)
        a.add_lsl(6, 6, 6, 5); a.eor(6, 6, 7)
        a.ldr_const(2, 0x03200020)
        a.label("save_new_read_irq")
        a.ldr(7, 2, byte=True); a.tst_imm(7, 0x10)
        a.branch("save_new_read_irq", "eq")
        a.ldr_const(3, 0x03310FD0); a.ldr(7, 3, byte=True)
        a.ldr(4, 0, byte=True)
        for _ in range(6): a.ldr(7, 0, byte=True)
        a.tst_imm(4, 0xC0); a.branch("save_new_other", "ne")
        if virtual_merge_probe:
            validate_merge_read("save_new_merge", "save_new_pristine",
                                "save_new_written", "save_new_other")
        else:
            a.ldr_const(7, 0 if creation_probe else 0xEEFAB800)
            a.cmp_reg(6, 7)
            a.branch("save_new_pristine", "eq")
            a.ldr_const(7, 0x26568000)
            a.cmp_reg(6, 7)
            a.branch("save_new_written", "eq")
            a.branch("save_new_other")
        a.label("save_new_pristine")
        a.mov(9, 1); a.branch("save_new_start_write")
        a.label("save_new_written")
        a.mov(9, 2)
        a.label("save_new_start_write")
        if creation_probe:
            # A missing data sector is the pristine state. A committed one
            # ends here, before *any* write, proving a fresh-store read.
            a.cmp_imm(9, 2); a.branch("save_new_creation_saved", "eq")
            a.branch("save_new_creation_prefix")
            a.label("save_new_missing")
            a.ldr_const(3, 0x03310FD0); a.ldr(7, 3, byte=True)
            a.ldr(4, 0, byte=True)  # ST0
            a.ldr(7, 0, byte=True)  # ST1
            for _ in range(5): a.ldr(1, 0, byte=True)
            a.tst_imm(4, 0x40); a.branch("save_new_other", "eq")
            a.tst_imm(7, 0x04); a.branch("save_new_other", "eq")
            a.mov(9, 1)
            a.branch("save_new_creation_prefix")
            a.label("save_new_creation_saved")
            a.mov(6, 0x20); a.branch("fdc_done")
            a.label("save_new_creation_wp")
            a.mov(6, 0x40); a.branch("fdc_done")
            a.label("save_new_creation_prefix")
            if virtual_merge_probe:
                a.ldr_const(10, 0x00012000)
                a.mov(11, 128)
                a.mov(7, 0x5A)
                a.label("save_new_merge_update")
                a.str_post(7, 10, 1, byte=True)
                a.subs_imm(11, 11, 1)
                a.branch("save_new_merge_update", "ne")
            # RISC OS allocation ordering: map, directory, then omitted data.
            # Prefix sectors already exist; only the final data ID is created.
            for prefix_sector in (() if virtual_merge_probe else (0, 2)):
                tag = f"save_new_create_{prefix_sector}"
                for value in (0x05, 0, 0, 0, prefix_sector, 3,
                              prefix_sector, 0x1B, 0xFF):
                    a.mov(1, value); a.str(1, 0, byte=True)
                a.ldr_const(2, 0x03200030)
                a.ldr_const(3, 0x03012000)
                a.ldr_const(5, 1024)
                a.ldr_const(8, 250000)
                a.mov(7, 0x5A)
                a.label(tag + "_wait")
                a.ldr(4, 2, byte=True)
                a.tst_imm(4, 1); a.branch(tag + "_byte", "ne")
                a.ldr_const(1, 0x03200020)
                a.ldr(4, 1, byte=True)
                a.tst_imm(4, 0x10); a.branch(tag + "_done", "ne")
                a.subs_imm(8, 8, 1); a.branch(tag + "_wait", "ne")
                a.branch("save_new_other")
                a.label(tag + "_byte")
                a.ldr_const(8, 250000)
                a.subs_imm(5, 5, 1)
                a.branch(tag + "_last", "eq")
                a.str(7, 3, byte=True)
                a.branch(tag + "_wait")
                a.label(tag + "_last")
                a.ldr_const(3, 0x0302A000)
                a.str(7, 3, byte=True)
                a.branch(tag + "_wait")
                a.label(tag + "_done")
                a.ldr_const(3, 0x03310FD0); a.ldr(7, 3, byte=True)
                a.ldr(4, 0, byte=True)
                a.ldr(7, 0, byte=True)
                for _ in range(5): a.ldr(1, 0, byte=True)
                a.cmp_imm(4, 0); a.branch(tag + "_ok", "eq")
                a.tst_imm(4, 0x40); a.branch("save_new_other", "eq")
                a.tst_imm(7, 0x02); a.branch("save_new_other", "eq")
                a.branch("save_new_creation_wp")
                a.label(tag + "_ok")
                a.cmp_imm(5, 0); a.branch("save_new_other", "ne")

        for value in (0x05, 0, 0, 0, 3 if creation_probe else 1,
                      3, 3 if creation_probe else 1, 0x1B, 0xFF):
            a.mov(1, value); a.str(1, 0, byte=True)
        a.ldr_const(2, 0x03200030)
        a.ldr_const(3, 0x03012000)
        a.ldr_const(5, 1024)
        a.ldr_const(8, 250000)
        if virtual_merge_probe:
            a.ldr_const(10, 0x00012000)
        else:
            a.mov(7, 0x5A)
        a.label("save_new_write_wait")
        a.ldr(4, 2, byte=True)
        a.tst_imm(4, 1); a.branch("save_new_write_byte", "ne")
        a.ldr_const(1, 0x03200020)
        a.ldr(4, 1, byte=True)
        a.tst_imm(4, 0x10); a.branch("save_new_write_done", "ne")
        a.subs_imm(8, 8, 1); a.branch("save_new_write_wait", "ne")
        a.branch("save_new_other")
        a.label("save_new_write_byte")
        a.ldr_const(8, 250000)
        if virtual_merge_probe:
            a.ldr_post(7, 10, 1, byte=True)
        a.subs_imm(5, 5, 1)
        a.branch("save_new_write_last", "eq")
        a.str(7, 3, byte=True)
        a.branch("save_new_write_wait")
        a.label("save_new_write_last")
        a.ldr_const(3, 0x0302A000)
        a.str(7, 3, byte=True)
        a.branch("save_new_write_wait")
        a.label("save_new_write_done")
        a.ldr_const(3, 0x03310FD0); a.ldr(7, 3, byte=True)
        a.ldr(4, 0, byte=True)
        a.ldr(7, 0, byte=True)
        for _ in range(5): a.ldr(1, 0, byte=True)
        a.cmp_imm(4, 0)
        a.branch("save_new_write_ok", "eq")
        a.tst_imm(4, 0x40); a.branch("save_new_other", "eq")
        a.tst_imm(7, 2); a.branch("save_new_other", "eq")
        a.cmp_imm(9, 1); a.branch("save_new_other", "ne")
        a.cmp_imm(5, 1024); a.branch("save_new_other", "ne")
        a.mov(6, 0x40); a.branch("fdc_done")
        a.label("save_new_write_ok")
        a.cmp_imm(5, 0); a.branch("save_new_other", "ne")
        a.cmp_imm(9, 1); a.branch("save_new_first", "eq")
        a.cmp_imm(9, 2); a.branch("save_new_other", "ne")
        a.mov(6, 0x20); a.branch("fdc_done")
        a.label("save_new_first")
        a.mov(6, 0x10); a.branch("fdc_done")
        a.label("save_new_other")
        a.mov(6, 0x80); a.branch("fdc_done")
    for value in (0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x1B, 0xFF):
        a.mov(1, value); a.str(1, 0, byte=True)
    a.ldr_const(2, 0x03200030)
    a.ldr_const(3, 0x03012000)
    a.mov(5, 0x100); a.mov(6, 0)
    a.label("new_dma_wait")
    a.ldr(7, 2, byte=True); a.tst_imm(7, 1); a.branch("new_dma_wait", "eq")
    a.subs_imm(5, 5, 1); a.branch("new_dma_last", "eq")
    a.ldr(7, 3, byte=True); a.eor(6, 6, 7); a.branch("new_dma_wait")
    a.label("new_dma_last")
    a.ldr_const(3, 0x0302A000); a.ldr(7, 3, byte=True); a.eor(6, 6, 7)
    a.ldr_const(2, 0x03200020)
    a.label("new_data_irq")
    a.ldr(1, 2, byte=True); a.tst_imm(1, 0x10); a.branch("new_data_irq", "eq")
    a.ldr_const(3, 0x03310FD0); a.ldr(1, 3, byte=True)
    for _ in range(7): a.ldr(1, 0, byte=True)
    a.mov(7, 0x5A); a.eor(6, 6, 7)  # native-frontend branch proof
    a.branch("fdc_done")

    a.label("newio_fail")
    a.ldr_const(6, 0x00DEAD00)
    a.branch("fdc_done")

    a.label("old_io_fdc")
    a.mov(12, 0)
    a.ldr_const(0, 0x00010000)
    a.mov(2, 0x5A)
    a.swp(3, 2, 0)
    a.swp(3, 2, 0, byte=True)
    a.cmp_imm(12, 2)
    a.branch("newio_fail", "ne")
    a.ldr_const(0, 0x0002FFF0)
    a.mov(1, 0xA2)
    a.str(1, 0, byte=True)
    # Select drive 0, side 0, motor on, double density; read track 0 sector 0.
    for address, value in ((0x03350040, 0x9E), (0x03350018, 0x00)):
        a.ldr_const(0, address)
        a.mov(1, value)
        a.str(1, 0, byte=True)
    a.ldr_const(0, 0x03310000)
    a.mov(1, 0)
    a.str(1, 0, 4, byte=True)
    if short_tail_probe or quarantine_probe:
        # WD1772, cylinder 79/head 1: sector 3 is the last present sector
        # of the one-sector-short D fixture; sector 4 is absent.
        a.mov(1, 79); a.str(1, 0, 12, byte=True)
        a.mov(1, 0x10); a.str(1, 0, byte=True)
        a.ldr_const(3, 300000)
        a.label("short_seek_wait")
        a.ldr(4, 0, byte=True); a.tst_imm(4, 1)
        a.branch("short_seek_done", "eq")
        a.subs_imm(3, 3, 1); a.branch("short_seek_wait", "ne")
        a.branch("short_fail")
        a.label("short_seek_done")
        a.ldr_const(2, 0x03350040)
        a.mov(1, 0x8E); a.str(1, 2, byte=True)
        a.mov(1, 3); a.str(1, 0, 8, byte=True)
        a.mov(1, 0x80); a.str(1, 0, byte=True)
        a.ldr_const(5, 1024); a.ldr_const(3, 2500000)
        a.label("short_present_wait")
        a.ldr(4, 0, byte=True); a.tst_imm(4, 2)
        a.branch("short_present_byte", "ne")
        a.tst_imm(4, 1); a.branch("short_fail", "eq")
        a.subs_imm(3, 3, 1); a.branch("short_present_wait", "ne")
        a.branch("short_fail")
        a.label("short_present_byte")
        a.ldr(7, 0, 12, byte=True)
        a.ldr_const(3, 2500000)
        a.subs_imm(5, 5, 1); a.branch("short_present_wait", "ne")
        a.label("short_present_finish")
        a.ldr(4, 0, byte=True); a.tst_imm(4, 1)
        a.branch("short_present_finish", "ne")
        a.cmp_imm(4, 0x80); a.branch("short_fail", "ne")
        if quarantine_probe:
            a.ldr_const(2, 0x03350040)
            a.mov(1, 0x9E); a.str(1, 2, byte=True)
        a.mov(1, 3 if quarantine_probe else 4); a.str(1, 0, 8, byte=True)
        a.mov(1, 0x80); a.str(1, 0, byte=True)
        a.ldr_const(3, 2500000)
        a.label("short_absent_wait")
        a.ldr(4, 0, byte=True); a.tst_imm(4, 2)
        a.branch("short_fail", "ne")
        a.tst_imm(4, 1); a.branch("short_absent_done", "eq")
        a.subs_imm(3, 3, 1); a.branch("short_absent_wait", "ne")
        a.branch("short_fail")
        a.label("short_absent_done")
        a.tst_imm(4, 0x10); a.branch("short_fail", "eq")
        if quarantine_probe:
            a.mov(6, 0x10); a.branch("fdc_done")
        a.mov(1, 3); a.str(1, 0, 8, byte=True)
        a.mov(1, 0xA0); a.str(1, 0, byte=True)
        a.ldr_const(5, 1024); a.ldr_const(3, 2500000)
        a.mov(7, 0x5A)
        a.label("short_write_wait")
        a.ldr(4, 0, byte=True); a.tst_imm(4, 1)
        a.branch("short_write_done", "eq")
        a.tst_imm(4, 2); a.branch("short_write_tick", "eq")
        a.str(7, 0, 12, byte=True)
        a.subs_imm(5, 5, 1); a.branch("short_write_tick", "eq")
        a.label("short_write_tick")
        a.subs_imm(3, 3, 1); a.branch("short_write_wait", "ne")
        a.branch("short_fail")
        a.label("short_write_done")
        a.cmp_imm(5, 0); a.branch("short_fail", "ne")
        a.cmp_imm(4, 0x80); a.branch("short_fail", "ne")
        a.mov(6, 0x10); a.branch("fdc_done")
        a.label("short_fail")
        a.mov(6, 0x80); a.branch("fdc_done")
    if allocation_mode:
        if allocation_mode in ("veto", "probe-write"):
            allocation_wd_read("alloc_wd_probe", True)
        if allocation_mode != "read":
            allocation_wd_write("alloc_wd_map", allocation_mode != "claim",
                                3 if allocation_mode == "probe-write" else 0)
        allocation_wd_read("alloc_wd_verify", allocation_mode in
                           ("veto", "probe-write"))
        a.mov(6, 0x20 if allocation_mode != "claim" else 0x10)
        a.branch("fdc_done")
        a.label("alloc_other")
        a.mov(6, 0x80)
        a.branch("fdc_done")
    if jfd_save_probe:
        a.mov(1, merge_read_sector if virtual_merge_probe else
              3 if creation_probe else 1)
    a.str(1, 0, 8, byte=True)
    if virtual_merge_probe:
        poison_merge_buffer("save_merge")
    a.mov(1, 0x80)
    a.str(1, 0, byte=True)
    if jfd_save_probe:
        a.ldr_const(5, 1024)
    else:
        a.mov(5, 0x100)
    a.mov(6, 0)
    if jfd_save_probe:
        a.ldr_const(3, 250000)
    if virtual_merge_probe:
        a.ldr_const(10, 0x00012000)
    a.label("fdc_wait")
    a.ldr(7, 0, byte=True)
    a.tst_imm(7, 2)
    if jfd_save_probe:
        a.branch("fdc_read_byte", "ne")
        a.tst_imm(7, 1)
        if creation_probe:
            a.branch("save_other" if virtual_merge_probe else "save_absent", "eq")
        else:
            a.branch("save_other", "eq")
        a.subs_imm(3, 3, 1)
        a.branch("fdc_wait", "ne")
        a.mov(4, 0xD0)
        a.str(4, 0, byte=True)
        a.branch("save_other")
        a.label("fdc_read_byte")
        a.ldr_const(3, 250000)
    else:
        a.branch("fdc_wait", "eq")
    a.ldr(7, 0, 12, byte=True)
    if virtual_merge_probe:
        a.str_post(7, 10, 1, byte=True)
    if jfd_save_probe:
        a.add_lsl(6, 6, 6, 5)
    a.eor(6, 6, 7)
    a.subs_imm(5, 5, 1)
    a.branch("fdc_wait", "ne")
    if jfd_save_probe:
        a.ldr_const(3, 250000)
    a.label("fdc_read_finish")
    a.ldr(4, 0, byte=True)
    a.tst_imm(4, 1)
    if jfd_save_probe:
        a.branch("fdc_read_complete", "eq")
        a.subs_imm(3, 3, 1)
        a.branch("fdc_read_finish", "ne")
        a.mov(4, 0xD0)
        a.str(4, 0, byte=True)
        a.branch("save_other")
        a.label("fdc_read_complete")
    else:
        a.branch("fdc_read_finish", "ne")
    if jfd_save_probe:
        # Classify the full sector before writing. Ordinary save probes use
        # rolling hashes; merge probes check every byte in guest RAM.
        # An unknown read or controller error cannot report a save.
        # WD1770 leaves bit 7 (ready) set on a successful completed command.
        a.cmp_imm(4, 0x80)
        a.branch("save_unknown", "ne")
        if virtual_merge_probe:
            validate_merge_read("save_merge", "save_pristine",
                                "save_written", "save_unknown")
        else:
            a.ldr_const(7, 0 if creation_probe else 0xEEFAB800)
            a.cmp_reg(6, 7)
            a.branch("save_pristine", "eq")
            a.ldr_const(7, 0x26568000)
            a.cmp_reg(6, 7)
            a.branch("save_written", "eq")
            a.branch("save_unknown")
        a.label("save_pristine")
        a.mov(9, 1)
        a.branch("save_read_done")
        a.label("save_written")
        a.mov(9, 2)
        a.branch("save_read_done")
        a.label("save_unknown")
        a.mov(9, 3)
        a.label("save_read_done")
        if creation_probe:
            a.cmp_imm(9, 2); a.branch("save_creation_saved", "eq")
            a.branch("save_creation_prefix")
            a.label("save_absent")
            a.tst_imm(7, 0x10); a.branch("save_other", "eq")
            a.mov(9, 1)
            a.branch("save_creation_prefix")
            a.label("save_creation_saved")
            a.mov(6, 0x20); a.branch("fdc_done")
            a.label("save_creation_prefix")
            if virtual_merge_probe:
                a.ldr_const(10, 0x00012000)
                a.mov(11, 128)
                a.mov(7, 0x5A)
                a.label("save_merge_update")
                a.str_post(7, 10, 1, byte=True)
                a.subs_imm(11, 11, 1)
                a.branch("save_merge_update", "ne")
            # Map and directory writes precede allocation into missing ID 3.
            for prefix_sector in (() if virtual_merge_probe else (0, 2)):
                tag = f"save_create_{prefix_sector}"
                a.mov(1, prefix_sector); a.str(1, 0, 8, byte=True)
                a.mov(1, 0xA0); a.str(1, 0, byte=True)
                a.ldr_const(5, 1024)
                a.mov(7, 0x5A)
                a.ldr_const(3, 250000)
                a.label(tag + "_wait")
                a.ldr(4, 0, byte=True)
                a.tst_imm(4, 1); a.branch(tag + "_done", "eq")
                a.tst_imm(4, 2); a.branch(tag + "_tick", "eq")
                a.str(7, 0, 12, byte=True)
                a.subs_imm(5, 5, 1)
                a.branch(tag + "_done", "eq")
                a.label(tag + "_tick")
                a.subs_imm(3, 3, 1); a.branch(tag + "_wait", "ne")
                a.mov(4, 0xD0); a.str(4, 0, byte=True)
                a.branch("save_other")
                a.label(tag + "_done")
                a.ldr(4, 0, byte=True)
                a.tst_imm(4, 1); a.branch(tag + "_done", "ne")
                a.cmp_imm(4, 0x80); a.branch(tag + "_ok", "eq")
                a.tst_imm(4, 0x40); a.branch("save_other", "eq")
                a.mov(6, 0x40); a.branch("fdc_done")
                a.label(tag + "_ok")
                a.cmp_imm(5, 0); a.branch("save_other", "ne")

    # Write sector one through the same controller. The source ADF is immutable;
    # the harness verifies that only the content-addressed shadow changes.
    a.mov(1, write_sector)
    a.str(1, 0, 8, byte=True)
    a.mov(1, 0xA0)
    a.str(1, 0, byte=True)
    a.ldr_const(5, 1024)
    if virtual_merge_probe:
        a.ldr_const(10, 0x00012000)
    elif write_payload is None:
        a.mov(7, 0x5A)
    else:
        a.ldr_const(2, 0x03802000)
    if media_exerciser:
        a.ldr_const(3, 250000)
        a.label("fdc_write_wait")
        a.ldr(4, 0, byte=True)
        a.tst_imm(4, 1)
        a.branch("fdc_write_done", "eq")
        a.tst_imm(4, 2)
        a.branch("fdc_write_tick", "eq")
        if virtual_merge_probe:
            a.ldr_post(7, 10, 1, byte=True)
        elif write_payload is not None:
            a.ldr_post(7, 2, 1, byte=True)
        a.str(7, 0, 12, byte=True)
        a.subs_imm(5, 5, 1)
        a.branch("fdc_write_done", "eq")
        a.label("fdc_write_tick")
        a.subs_imm(3, 3, 1)
        a.branch("fdc_write_wait", "ne")
        if jfd_save_probe:
            a.mov(9, 3)  # cancellation is never reported as a save
        a.mov(4, 0xD0)  # bounded cancellation if a reader never completes
        a.str(4, 0, byte=True)
        a.label("fdc_write_done")
    else:
        a.label("fdc_write_wait")
        a.ldr(4, 0, byte=True)
        a.tst_imm(4, 2)
        a.branch("fdc_write_wait", "eq")
        if virtual_merge_probe:
            a.ldr_post(7, 10, 1, byte=True)
        elif write_payload is not None:
            a.ldr_post(7, 2, 1, byte=True)
        a.str(7, 0, 12, byte=True)
        a.subs_imm(5, 5, 1)
        a.branch("fdc_write_wait", "ne")
    if not media_exerciser or jfd_save_probe:
        a.label("fdc_write_finish")
        a.ldr(4, 0, byte=True)
        a.tst_imm(4, 1)
        a.branch("fdc_write_finish", "ne")

    if jfd_save_probe:
        # Publish one final, steady 8-bpp palette index only after the write
        # command ends. 0x10 = pristine read + accepted write; 0x20 = saved
        # data read before this run's write; 0x40 = pristine read + WP refusal;
        # 0x80 = any other read, write or controller outcome.
        a.cmp_imm(4, 0x80)
        a.branch("save_write_ok", "eq")
        a.tst_imm(4, 0x40)
        a.branch("save_other", "eq")
        a.cmp_imm(9, 1)
        a.branch("save_other", "ne")
        a.mov(6, 0x40)
        a.branch("fdc_done")
        a.label("save_write_ok")
        a.cmp_imm(5, 0)
        a.branch("save_other", "ne")
        a.cmp_imm(9, 1)
        a.branch("save_first", "eq")
        a.cmp_imm(9, 2)
        a.branch("save_other", "ne")
        a.mov(6, 0x20)
        a.branch("fdc_done")
        a.label("save_first")
        a.mov(6, 0x10)
        a.branch("fdc_done")
        a.label("save_other")
        a.mov(6, 0x80)

    a.label("fdc_done")
    # Expand the guest checksum to all bytes and paint 640x256 at 8 bpp. The
    # alternating complement makes the payload-derived signature visibly
    # distinctive in native-frontend screenshots instead of a uniform colour.
    a.orr_lsl(6, 6, 6, 8)
    a.orr_lsl(6, 6, 6, 16)
    if not jfd_save_probe:
        a.ldr_const(7, 0x00FFFFFF)
    a.mov(1, 0)
    a.ldr_const(5, 40960)
    a.label("fill_video")
    a.str_post(6, 1)
    if not jfd_save_probe:
        a.eor(6, 6, 7)
    a.subs_imm(5, 5, 1)
    a.branch("fill_video", "ne")

    # A separate deterministic logarithmic waveform feeds real sound DMA.
    a.ldr_const(1, 0x00028000)
    a.mov(5, 0x100)
    a.mov(7, 0xFE)
    a.orr_lsl(7, 7, 7, 8)
    a.orr_lsl(7, 7, 7, 16)
    a.label("fill_sound")
    a.str_post(7, 1)
    a.subs_imm(5, 5, 1)
    a.branch("fill_sound", "ne")

    a.ldr_const(1, 0x03400000)
    for register in VIDC_MODE:
        a.ldr_const(0, register)
        a.str(0, 1)
    for address in (0x03600000, 0x03620000, 0x0364A000,
                    0x0368A000, 0x036AA0FC, 0x036CA000):
        a.ldr_const(0, address)
        a.str(1, 0)
    a.ldr_const(0, 0x036E0D0C)  # sound + video DMA on
    a.str(1, 0)

    if power_on_probe == "late-kart":
        # Leave both Shift transitions queued before enabling the controller.
        a.ldr_const(5, 1000000)
        a.label("power_on_late_kart")
        a.subs_imm(5, 5, 1)
        a.branch("power_on_late_kart", "ne")
    # Complete the standard KART reset handshake and enable keyboard + mouse.
    # Event bytes handled below are folded into the first displayed guest word.
    a.ldr_const(11, 0x03300000)
    a.mov(0, 0xFD)
    a.str(0, 11, 4, byte=True)
    a.ldr_const(5, 5000)
    a.label("kart_reset_wait")
    a.subs_imm(5, 5, 1)
    a.branch("kart_reset_wait", "ne")
    a.ldr(0, 11, 4, byte=True)
    a.mov(0, 0x33)
    a.str(0, 11, 4, byte=True)
    a.mov(8, 0)

    # Exercise the bounded fallback with a zero-line VIDC program, then recover
    # to the normal mode. A transient episode is debug-logged without an OSD
    # warning (R-36), and must never terminate the frontend.
    a.ldr_const(0, 0xA0000000)
    a.str(0, 1)
    a.ldr_const(5, 25000)
    a.label("invalid_vidc_wait")
    a.subs_imm(5, 5, 1)
    a.branch("invalid_vidc_wait", "ne")
    a.ldr_const(0, 0xA04DC000)
    a.str(0, 1)

    # Two modes are programmed by the guest, with enough dwell for coalescing.
    a.mov(9, 0)
    if power_on_probe == "early-swi":
        a.mov(12, 0)  # count complete KART pairs since this guest reset
    a.ldr_const(10, 250000)
    a.label("idle")
    if jfd_save_probe:
        a.branch("idle")  # the completed outcome cannot change with time
    a.ldr(0, 11, 0x20, byte=True)
    a.tst_imm(0, 0x80)
    a.branch("input_event", "ne")
    a.subs_imm(10, 10, 1)
    a.branch("idle", "ne")
    a.ldr_const(10, 250000)
    if media_exerciser:
        # Re-read sector zero on each dwell. The first 256 bytes are reduced to
        # a result byte and painted into a full row, so a disk swap is visible
        # and machine-decodable without firmware or game assets.
        a.ldr_const(0, 0x03310000)
        a.mov(4, 0)
        a.str(4, 0, 4, byte=True)
        a.str(4, 0, 8, byte=True)
        a.mov(4, 0x80)
        a.str(4, 0, byte=True)
        a.mov(5, 0x100)
        a.mov(6, 0)
        a.ldr_const(3, 250000)
        a.label("media_read_wait")
        a.ldr(4, 0, byte=True)
        a.tst_imm(4, 2)
        a.branch("media_read_tick", "eq")
        a.ldr(7, 0, 12, byte=True)
        a.eor(6, 6, 7)
        a.subs_imm(5, 5, 1)
        a.branch("media_read_done", "eq")
        a.label("media_read_tick")
        a.subs_imm(3, 3, 1)
        a.branch("media_read_wait", "ne")
        a.mov(4, 0xD0)
        a.str(4, 0, byte=True)
        a.label("media_read_done")
        a.orr_lsl(6, 6, 6, 8)
        a.orr_lsl(6, 6, 6, 16)
        a.mov(8, 0)
        a.mov(2, 0xA0)
        a.label("media_row")
        a.str_post(6, 8)
        a.subs_imm(2, 2, 1)
        a.branch("media_row", "ne")
        a.mov(8, 0)
    a.tst_imm(9, 1)
    a.branch("mode_fast", "ne")
    a.ldr_const(0, 0xA063C000)  # 400 total lines
    a.mov(9, 1)
    a.str(0, 1)
    a.branch("idle")

    a.label("input_event")
    a.ldr(0, 11, 4, byte=True)
    # signature = signature * 33 XOR byte. Unlike the old XOR-only fold this
    # proves the KART receive order (modifier,key differs from key,modifier).
    a.add_lsl(6, 6, 6, 5)
    a.eor(6, 6, 0)
    a.mov(4, 0x3F)
    a.str(4, 11, 4, byte=True)
    a.label("input_second")
    a.ldr(0, 11, 0x20, byte=True)
    a.tst_imm(0, 0x80)
    a.branch("input_second", "eq")
    a.ldr(0, 11, 4, byte=True)
    a.add_lsl(6, 6, 6, 5)
    a.eor(6, 6, 0)
    a.mov(4, 0x33)
    a.str(4, 11, 4, byte=True)
    a.str(6, 8)
    if power_on_probe == "early-swi":
        # The boot program immediately resets after consuming Shift key-up.
        # No core construction occurs, so the hold must not be re-armed.
        a.add_imm(12, 12, 1)
        a.cmp_imm(12, 2)
        a.branch("power_on_reset_after_up", "eq")
    a.branch("idle")
    if power_on_probe == "early-swi":
        a.label("power_on_reset_after_up")
        a.ldr_const(2, 0x03310000)
        a.mov(4, 0xD0)
        a.str(4, 2, byte=True)
        a.ldr_const(15, 0x03800000)
    a.label("mode_fast")
    a.ldr_const(0, 0xA04DC000)  # 312 total lines
    a.mov(9, 0)
    a.str(0, 1)
    a.branch("idle")

    if boot_watch_pattern:
        # Run at 50 Hz with a functioning VIDC field, then execute real SWIs.
        a.label("boot_watch_start")
        a.ldr_const(1, 0x03400000)
        for register in CHOOSER_VIDC_MODE:
            a.ldr_const(0, register)
            a.str(0, 1)
        for address in (0x03600000, 0x03620000, 0x0364A000,
                        0x0368A000, 0x036AA0FC, 0x036CA000):
            a.ldr_const(0, address)
            a.str(1, 0)
        a.ldr_const(0, 0x036E0D0C)
        a.str(1, 0)
        if boot_watch_kart:
            # Guest handshake lets the ordinary FIFO -> KART route deliver
            # the construction's Shift transitions during recovery/chooser.
            a.ldr_const(11, 0x03300000)
            a.mov(0, 0xFD); a.str(0, 11, 4, byte=True)
            a.ldr_const(5, 5000)
            a.label("boot_kart_reset_wait")
            a.subs_imm(5, 5, 1); a.branch("boot_kart_reset_wait", "ne")
            a.ldr(0, 11, 4, byte=True)
            a.mov(0, 0x33); a.str(0, 11, 4, byte=True)
            a.mov(9, 2)  # complete Shift down and up before the boot SWI
            a.label("boot_watch_kart_wait")
            a.ldr(0, 11, 0x20, byte=True)
            a.tst_imm(0, 0x80); a.branch("boot_watch_kart_wait", "eq")
            a.ldr(0, 11, 4, byte=True)
            a.mov(4, 0x3F); a.str(4, 11, 4, byte=True)
            a.label("boot_watch_kart_second")
            a.ldr(0, 11, 0x20, byte=True)
            a.tst_imm(0, 0x80); a.branch("boot_watch_kart_second", "eq")
            a.ldr(0, 11, 4, byte=True)
            a.mov(4, 0x33); a.str(4, 11, 4, byte=True)
            a.subs_imm(9, 9, 1); a.branch("boot_watch_kart_wait", "ne")
        if boot_watch_pattern == "two-phase":
            # The first boot writes a CMOS user byte and reaches the command
            # prompt. The same disc reaches the desktop when that byte is set.
            # Both branches execute from ROM; the flag is guest I2C state.
            a.ldr_const(0, 0x03200000)

            def cmos_pins(value: int) -> None:
                a.mov(1, value)
                a.str(1, 0, byte=True)

            def cmos_start() -> None:
                cmos_pins(3); cmos_pins(2)

            def cmos_stop() -> None:
                cmos_pins(0); cmos_pins(2); cmos_pins(3)

            def cmos_send(value: int) -> None:
                for shift in range(7, -1, -1):
                    bit = (value >> shift) & 1
                    cmos_pins(bit); cmos_pins(2 | bit)
                cmos_pins(1); cmos_pins(3); cmos_pins(0)

            cmos_start(); cmos_send(0xA0); cmos_send(0x58); cmos_stop()
            cmos_start(); cmos_send(0xA1)
            a.mov(4, 0)
            for _ in range(8):
                cmos_pins(1); cmos_pins(3)
                a.ldr(2, 0, byte=True)
                a.emit(0xE1A04084)  # mov r4, r4, lsl #1
                a.emit(0xE2022001)  # and r2, r2, #1
                a.emit(0xE1844002)  # orr r4, r4, r2
            cmos_pins(1); cmos_pins(3)
            a.cmp_imm(4, 0x77)
            a.branch("two_phase_desktop", "eq")
            cmos_start(); cmos_send(0xA0); cmos_send(0x58)
            cmos_send(0x77); cmos_stop()
            a.branch("two_phase_prompt")
            a.label("two_phase_desktop")
            a.emit(0xEF0400C0)  # Wimp_Initialise
            a.branch("two_phase_probe")
            a.label("two_phase_prompt")
        if boot_watch_pattern == "prompt-structure":
            # Two guest FDC writes replace both sectors of a D-format root
            # directory before the qualifying ROM SWI. The target directory
            # bytes are embedded only in this invented test ROM.
            a.ldr_const(0, 0x03350040)
            a.mov(1, 0x9E)
            a.str(1, 0, byte=True)
            a.ldr_const(0, 0x03350018)
            a.mov(1, 0)
            a.str(1, 0, byte=True)
            a.ldr_const(6, 0x03802000)
            for sector in boot_write_sectors:
                prefix = f"boot_structure_{sector}"
                a.ldr_const(0, 0x03350040)
                a.mov(1, 0x9E if sector < 5 else 0x8E)
                a.str(1, 0, byte=True)
                a.ldr_const(0, 0x03310000)
                a.mov(1, 0)
                a.str(1, 0, 4, byte=True)
                a.mov(1, sector % 5)
                a.str(1, 0, 8, byte=True)
                a.mov(1, 0xA0)
                a.str(1, 0, byte=True)
                a.ldr_const(5, 1024)
                a.ldr_const(3, 2000000)
                a.label(prefix + "_wait")
                a.ldr(4, 0, byte=True)
                a.tst_imm(4, 1)
                a.branch(prefix + "_done", "eq")
                a.tst_imm(4, 2)
                a.branch(prefix + "_tick", "eq")
                a.ldr_post(7, 6, 1, byte=True)
                a.str(7, 0, 12, byte=True)
                a.subs_imm(5, 5, 1)
                a.branch(prefix + "_done", "eq")
                a.label(prefix + "_tick")
                a.subs_imm(3, 3, 1)
                a.branch(prefix + "_wait", "ne")
                a.label(prefix + "_done")
                a.ldr_const(3, 2000000)
                a.label(prefix + "_finish")
                a.ldr(4, 0, byte=True)
                a.tst_imm(4, 1)
                a.branch(prefix + "_next", "eq")
                a.subs_imm(3, 3, 1)
                a.branch(prefix + "_finish", "ne")
                a.label(prefix + "_next")
        if boot_watch_pattern == "prompt-write":
            # Write an invented payload through the guest WD1770 before the
            # qualifying read. Recovery must retain the staged shadow.
            a.ldr_const(0, 0x03350040)
            a.mov(1, 0x9E)
            a.str(1, 0, byte=True)
            a.ldr_const(0, 0x03350018)
            a.mov(1, 0)
            a.str(1, 0, byte=True)
            a.ldr_const(0, 0x03310000)
            a.mov(1, 0)
            a.str(1, 0, 4, byte=True)
            a.mov(1, 4)
            a.str(1, 0, 8, byte=True)
            a.mov(1, 0xA0)
            a.str(1, 0, byte=True)
            a.ldr_const(5, 256)
            a.ldr_const(3, 250000)
            a.mov(7, 0x5A)
            a.label("boot_write_wait")
            a.ldr(4, 0, byte=True)
            a.tst_imm(4, 1)
            a.branch("boot_write_done", "eq")
            a.tst_imm(4, 2)
            a.branch("boot_write_tick", "eq")
            a.str(7, 0, 12, byte=True)
            a.subs_imm(5, 5, 1)
            a.branch("boot_write_done", "eq")
            a.label("boot_write_tick")
            a.subs_imm(3, 3, 1)
            a.branch("boot_write_wait", "ne")
            a.label("boot_write_done")
            a.ldr(4, 0, byte=True)
            a.tst_imm(4, 1)
            a.branch("boot_write_done", "ne")
        a.ldr_const(0, 0x89C)
        if boot_watch_pattern in ("cli-confirm", "horizon-edge"):
            a.ldr_const(5, 103000 if boot_watch_pattern == "cli-confirm"
                        else 12320000)
            a.label("boot_pre_read")
            a.subs_imm(5, 5, 1)
            a.branch("boot_pre_read", "ne")
        if boot_watch_pattern == "desktop":
            a.emit(0xEF0400C0)  # Wimp_Initialise
        else:
            a.emit(0xEF00000E)  # OS_ReadLine
            if boot_watch_pattern in ("cli", "cli-then-prompt"):
                a.emit(0xEF000005)  # OS_CLI cancels the pending read
            if boot_watch_pattern == "cli-confirm":
                a.ldr_const(5, 648000)
                a.label("boot_pre_cli")
                a.subs_imm(5, 5, 1)
                a.branch("boot_pre_cli", "ne")
                a.emit(0xEF000005)
            if boot_watch_pattern == "cli-then-prompt":
                a.emit(0xEF02000E)  # XOS_ReadLine starts a new wait
        if boot_watch_pattern == "two-phase":
            a.label("two_phase_probe")
        # Probe drive 1 after the observation. On a recovered construction it
        # holds the project-authored helper; on the original boot it is empty.
        a.ldr_const(0, 0x03350040)
        a.mov(1, 0x8D)
        a.str(1, 0, byte=True)
        a.ldr_const(0, 0x03350018)
        a.mov(1, 0)
        a.str(1, 0, byte=True)
        a.ldr_const(0, 0x03310000)
        a.mov(1, 0)
        a.str(1, 0, 4, byte=True)
        a.str(1, 0, 8, byte=True)
        a.mov(1, 0x80)
        a.str(1, 0, byte=True)
        a.ldr_const(5, 250000)
        a.label("boot_helper_wait")
        a.ldr(2, 0, byte=True)
        a.tst_imm(2, 2)
        a.branch("boot_helper_byte", "ne")
        a.tst_imm(2, 1)
        a.branch("boot_helper_done", "eq")
        a.subs_imm(5, 5, 1)
        a.branch("boot_helper_wait", "ne")
        a.branch("boot_helper_done")
        a.label("boot_helper_byte")
        a.ldr(2, 0, 12, byte=True)
        a.label("boot_helper_done")
        a.label("boot_watch_wait")
        a.branch("boot_watch_wait")

    code = a.finish()
    rom[0x20:0x20 + len(code)] = code
    if boot_watch_pattern or power_on_probe in ("early-swi", "early-xswi"):
        rom[0:4] = struct.pack("<I", 0xE59FF004)  # reset literal at 0x0c
        rom[8:12] = struct.pack("<I", 0xEA000000)  # SWI vector -> 0x10
        rom[12:16] = struct.pack("<I", 0x03800020)
        rom[16:20] = struct.pack("<I", 0xE1B0F00E)  # movs pc, lr
    marker = (b"ARCDROID BOOT OBSERVER ROM " + boot_watch_pattern.encode() + b"\0"
              if boot_watch_pattern else
              b"ARCDROID ARMV2 MEDIA EXERCISER ROM v1\0" if media_exerciser
              else b"ARCDROID ARMV2 TEST ROM v2\0")
    rom[0x1F00:0x1F00 + len(marker)] = marker
    if write_payload is not None:
        rom[0x2000:0x2000 + len(write_payload)] = write_payload
    return rom


def _chooser_vdu(title: bytes, colour: int) -> bytes:
    return bytes((22, 12, 17, colour, 12, 31, 8, 15)) + title


def _chooser_signature_application(title: bytes, colour: int) -> bytes:
    """One-sector RISC OS Absolute app also executable by the chooser ROM."""
    vdu = _chooser_vdu(title, colour)
    words = [
        0xE59F1014,             # ldr r1, [pc, #20]
        0xE59F2014,             # ldr r2, [pc, #20]
        0xE4D10001,             # ldrb r0, [r1], #1
        0xEF000000,             # swi OS_WriteC
        0xE2522001,             # subs r2, r2, #1
        0x1AFFFFFB,             # bne byte loop
        0xEAFFFFFE,             # b .
        0x00008024,             # VDU byte stream address
        len(vdu),
    ]
    program = b"".join(struct.pack("<I", word) for word in words) + vdu
    if len(program) > SECTOR_SIZE:
        raise ValueError("chooser signature application exceeds one sector")
    return program


def build_chooser_rom(helper_visual_oracle: bool = False) -> bytearray:
    """Run the executable named by ArcDroid's drive-1 launch helper.

    The helper's ArcLaunch file is fixed at logical sector 1280, which is
    physical track 0, side 1, sector 0 on an L-format disc. The two invented
    fixture applications keep their Absolute executables in physical sectors
    7 and 8. This ROM reads the helper target, loads the matching executable at
    its declared &8000 address, and runs it. A tiny OS_WriteC handler folds the
    application's VDU stream into a full-width framebuffer signature row.
    The D-format transport variant renders the same signature directly from
    the helper selection; the real-RISC OS checkpoint tests command execution.
    """
    rom = bytearray(b"\xFF" * ROM_SIZE)
    rom[0:4] = struct.pack("<I", 0xE59FF000)
    rom[8:12] = struct.pack("<I", 0x03800020)
    a = Arm2(0x03800020)

    a.ldr_const(0, 0x036E050C)
    a.mov(1, 0)
    a.str(1, 0)
    for page in range(6):
        cam = (0x03800000 | ((page & 0x0F) << 3) |
               ((page & 0x10) >> 4) | ((page & 0x20) >> 3) |
               ((page & 0x40) >> 5) | (page & 0x80) |
               ((page & 0x100) << 4) | (3 << 8) |
               ((page & 0xFF) << 15) | ((page & 0x300) << 2))
        a.ldr_const(0, cam)
        a.str(1, 0)

    a.ldr_const(6, 0x22222222)
    a.mov(1, 0)
    a.ldr_const(5, 40960)
    a.label("chooser_fill_video")
    a.str_post(6, 1)
    a.subs_imm(5, 5, 1)
    a.branch("chooser_fill_video", "ne")
    a.ldr_const(1, 0x00028000)
    a.mov(5, 0x100)
    a.ldr_const(7, 0xFEFEFEFE)
    a.label("chooser_fill_sound")
    a.str_post(7, 1)
    a.subs_imm(5, 5, 1)
    a.branch("chooser_fill_sound", "ne")

    a.ldr_const(1, 0x03400000)
    for register in CHOOSER_VIDC_MODE:
        a.ldr_const(0, register)
        a.str(0, 1)
    for address in (0x03600000, 0x03620000, 0x0364A000,
                    0x0368A000, 0x036AA0FC, 0x036CA000):
        a.ldr_const(0, address)
        a.str(1, 0)
    a.ldr_const(0, 0x036E0D0C)
    a.str(1, 0)

    handler_address = 0x0002F000
    handler = Arm2(handler_address)
    handler.add_lsl(12, 12, 12, 5)
    handler.eor(12, 12, 0)
    handler.mov(4, 0)
    handler.mov(3, 0xA0)
    handler.label("chooser_signature_row")
    handler.str_post(12, 4)
    handler.subs_imm(3, 3, 1)
    handler.branch("chooser_signature_row", "ne")
    handler.emit(0xE1B0F00E)  # movs pc, lr
    handler_code = handler.finish()
    vector_branch = 0xEA000000 | (((handler_address - 0x10) // 4) & 0xFFFFFF)
    a.ldr_const(0, 0x00000008)
    a.ldr_const(1, vector_branch)
    a.str(1, 0)
    a.ldr_const(0, handler_address)
    for offset in range(0, len(handler_code), 4):
        a.ldr_const(1, struct.unpack_from("<I", handler_code, offset)[0])
        a.str(1, 0, offset)

    # ArcLaunch is on drive 1, side 1. Byte 52 is the first target byte after !.
    a.ldr_const(0, 0x03350040)
    a.mov(1, 0x8D)
    a.str(1, 0, byte=True)
    a.ldr_const(0, 0x03350018)
    a.mov(1, 0)
    a.str(1, 0, byte=True)
    a.ldr_const(0, 0x03310000)
    a.mov(1, 0)
    a.str(1, 0, 4, byte=True)
    a.str(1, 0, 8, byte=True)
    a.mov(1, 0x80)
    a.str(1, 0, byte=True)
    a.mov(5, 0x100)
    a.mov(6, 0)
    a.label("chooser_helper_wait")
    a.ldr(7, 0, byte=True)
    a.tst_imm(7, 2)
    a.branch("chooser_helper_wait", "eq")
    a.ldr(7, 0, 12, byte=True)
    a.cmp_imm(7, ord("N"))
    a.branch("chooser_helper_target", "eq")
    a.cmp_imm(7, ord("Q"))
    a.branch("chooser_helper_not_target", "ne")
    a.label("chooser_helper_target")
    a.mov_reg(6, 7)
    a.label("chooser_helper_not_target")
    a.subs_imm(5, 5, 1)
    a.branch("chooser_helper_wait", "ne")
    a.label("chooser_helper_finish")
    a.ldr(7, 0, byte=True)
    a.tst_imm(7, 1)
    a.branch("chooser_helper_finish", "ne")

    a.cmp_imm(6, ord("N"))
    a.branch("chooser_app_one", "eq")
    a.cmp_imm(6, ord("Q"))
    a.branch("chooser_app_two", "eq")
    a.label("chooser_bad_helper")
    a.ldr_const(12, 0x00FF00FF)
    a.mov(4, 0)
    a.mov(3, 0xA0)
    a.label("chooser_bad_row")
    a.str_post(12, 4)
    a.subs_imm(3, 3, 1)
    a.branch("chooser_bad_row", "ne")
    a.branch("chooser_bad_helper")
    a.label("chooser_app_one")
    if helper_visual_oracle:
        signature = 0
        for value in _chooser_vdu(b"ARCDROID - NULVANE SELECTED", 3):
            signature = ((signature * 33) ^ value) & 0xFFFFFFFF
        a.ldr_const(12, signature)
    else:
        a.mov(6, 7)
    a.branch("chooser_load_app")
    a.label("chooser_app_two")
    if helper_visual_oracle:
        signature = 0
        for value in _chooser_vdu(b"ARCDROID - QAVENTRA SELECTED", 6):
            signature = ((signature * 33) ^ value) & 0xFFFFFFFF
        a.ldr_const(12, signature)
    else:
        a.mov(6, 8)

    a.label("chooser_load_app")
    if helper_visual_oracle:
        a.mov(4, 0)
        a.mov(3, 0xA0)
        a.label("chooser_oracle_row")
        a.str_post(12, 4)
        a.subs_imm(3, 3, 1)
        a.branch("chooser_oracle_row", "ne")
        a.label("chooser_oracle_hold")
        a.branch("chooser_oracle_hold")
    else:
        a.ldr_const(0, 0x03350040)
        a.mov(1, 0x9E)
        a.str(1, 0, byte=True)
        a.ldr_const(0, 0x03310000)
        a.mov(1, 0)
        a.str(1, 0, 4, byte=True)
        a.str(6, 0, 8, byte=True)
        a.mov(1, 0x80)
        a.str(1, 0, byte=True)
        a.mov(5, 0x100)
        a.ldr_const(8, 0x00008000)
        a.label("chooser_app_wait")
        a.ldr(7, 0, byte=True)
        a.tst_imm(7, 2)
        a.branch("chooser_app_wait", "eq")
        a.ldr(7, 0, 12, byte=True)
        a.str_post(7, 8, 1, byte=True)
        a.subs_imm(5, 5, 1)
        a.branch("chooser_app_wait", "ne")
        a.label("chooser_app_finish")
        a.ldr(7, 0, byte=True)
        a.tst_imm(7, 1)
        a.branch("chooser_app_finish", "ne")
    if not helper_visual_oracle:
        a.mov(12, 0)
        a.ldr_const(0, 0x00008000)
        a.mov_reg(15, 0)

    code = a.finish()
    rom[0x20:0x20 + len(code)] = code
    marker = b"ARCDROID ARMV2 CHOOSER APPLICATION ROM v1\0"
    rom[0x1F00:0x1F00 + len(marker)] = marker
    return rom


def build_adf(seed: int = 0xA5, marker: bytes = b"ARCDROID WD1772 PAYLOAD v2\0") -> bytearray:
    adf = bytearray(ADF_SIZE)
    adf[:len(marker)] = marker
    for index in range(len(marker), ADF_SIZE):
        adf[index] = ((index * 73) ^ (index >> 3) ^ seed) & 0xFF
    return adf


def build_jfd(payload: bytes, title: bytes = b"ArcDroid synthetic JFD") -> bytearray:
    """Build the documented legacy-header JFD subset with one 256-byte sector."""
    track_offset, sector_offset, data_offset = 304, 312, 324
    image = bytearray(data_offset + len(payload))
    image[:4] = b"JFDI"
    struct.pack_into("<I", image, 8, len(image))
    struct.pack_into("<I", image, 24, track_offset)
    struct.pack_into("<I", image, 28, sector_offset)
    struct.pack_into("<I", image, 32, data_offset)
    image[48:48 + min(len(title), 256)] = title[:256]
    struct.pack_into("<II", image, track_offset, 0, 0xFFFFFFFF)
    # time=0, block flags=0, density=2, ID=0, CRC flags=0, size code=1
    struct.pack_into("<II", image, sector_offset, (2 << 16) | 1, 0)
    struct.pack_into("<I", image, sector_offset + 8, 0xFFFFFFFF)
    image[data_offset:] = payload
    return image


def build_extended_jfd(payload: bytes,
                       title: bytes = b"ArcDroid synthetic JFD",
                       disk_geometry: str | None = None) -> bytearray:
    """Build an extended-header JFD whose table begins at byte 316."""
    if disk_geometry is not None:
        return _build_jfd_disk(payload, disk_geometry, title, True)
    track_offset, sector_offset, data_offset = 316, 324, 336
    image = bytearray(data_offset + len(payload))
    image[:4] = b"JFDI"
    struct.pack_into("<I", image, 8, len(image))
    struct.pack_into("<III", image, 24, track_offset, sector_offset, data_offset)
    image[48:48 + min(len(title), 256)] = title[:256]
    struct.pack_into("<II", image, track_offset, 0, 0xFFFFFFFF)
    struct.pack_into("<II", image, sector_offset, (2 << 16) | 1, 0)
    struct.pack_into("<I", image, sector_offset + 8, 0xFFFFFFFF)
    image[data_offset:] = payload
    return image


def gzip_member(payload: bytes) -> bytes:
    """Create a deterministic gzip member marked as Acorn RISC OS (OS 13)."""
    member = bytearray(gzip.compress(payload, mtime=0))
    member[9] = 13
    return bytes(member)


def _zip_bytes(entries: list[tuple[str, bytes, bool]],
               compression: int = zipfile.ZIP_DEFLATED,
               streaming: bool = False) -> bytes:
    class StreamingBuffer(io.BytesIO):
        def seekable(self) -> bool:
            return False

        def seek(self, *args, **kwargs):
            raise io.UnsupportedOperation

    output = StreamingBuffer() if streaming else io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=compression) as archive:
        for name, payload, metadata in entries:
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = compression
            if metadata:
                info.extra = struct.pack("<HH4sIII", 0x4341, 16,
                                         b"ARC0", 0xFFF00000,
                                         0, 0x33)
            archive.writestr(info, payload)
    return output.getvalue()


def _zip_name_records(payload: bytes | bytearray) -> \
        list[tuple[int, int, bytes, int, int, bytes]]:
    """Return exact local/central name fields without zipfile normalisation."""
    eocd = payload.rfind(b"PK\x05\x06")
    if eocd < 0 or eocd + 22 > len(payload):
        raise ValueError("generated ZIP has no complete end record")
    count = struct.unpack_from("<H", payload, eocd + 10)[0]
    central_size, central = struct.unpack_from("<II", payload, eocd + 12)
    central_end = central + central_size
    if central_end > eocd:
        raise ValueError("generated ZIP central directory is out of bounds")
    records = []
    position = central
    for _ in range(count):
        if position + 46 > central_end or payload[position:position + 4] != b"PK\x01\x02":
            raise ValueError("generated ZIP central entry is invalid")
        name_size, extra_size, comment_size = struct.unpack_from(
            "<HHH", payload, position + 28)
        record_end = position + 46 + name_size + extra_size + comment_size
        if record_end > central_end:
            raise ValueError("generated ZIP central name is out of bounds")
        local = struct.unpack_from("<I", payload, position + 42)[0]
        if local + 30 > len(payload) or payload[local:local + 4] != b"PK\x03\x04":
            raise ValueError("generated ZIP local entry is invalid")
        local_name_size = struct.unpack_from("<H", payload, local + 26)[0]
        local_name = local + 30
        if local_name + local_name_size > len(payload):
            raise ValueError("generated ZIP local name is out of bounds")
        central_name = position + 46
        records.append((local_name, local_name_size,
                        bytes(payload[local_name:local_name + local_name_size]),
                        central_name, name_size,
                        bytes(payload[central_name:central_name + name_size])))
        position = record_end
    if position != central_end:
        raise ValueError("generated ZIP central directory size disagrees")
    return records


def _write_raw_name_fixture(path: Path,
                            entries: list[tuple[str, bytes, bool]],
                            raw_name: bytes, member: bytes, metadata: bool,
                            expected_count: int = 1) -> None:
    """Write an exact malformed name, bypassing host path normalisation."""
    placeholder = b"X" * len(raw_name)
    archive = bytearray(_zip_bytes(
        entries + [(placeholder.decode("ascii"), member, metadata)]))
    records = _zip_name_records(archive)
    local, local_size, local_value, central, central_size, central_value = records[-1]
    if (local_size != len(raw_name) or central_size != len(raw_name) or
            local_value != placeholder or central_value != placeholder):
        raise ValueError(f"cannot patch exact ZIP name for {path.name}")
    archive[local:local + local_size] = raw_name
    archive[central:central + central_size] = raw_name
    path.write_bytes(archive)

    # Read the completed fixture back and fail generation if either header was
    # normalised, omitted, or patched differently from the requested bytes.
    checked = _zip_name_records(path.read_bytes())
    local_names = [record[2] for record in checked]
    central_names = [record[5] for record in checked]
    if (local_names[-1] != raw_name or central_names[-1] != raw_name or
            local_names.count(raw_name) != expected_count or
            central_names.count(raw_name) != expected_count):
        raise ValueError(f"raw ZIP name self-check failed for {path.name}")


def _manual_zip(entries: list[dict], comment: bytes = b"") -> bytes:
    """Build a deterministic classic ZIP for structural boundary fixtures."""
    local = bytearray()
    central = bytearray()
    for item in entries:
        name = item["name"]
        if isinstance(name, str):
            name = name.encode("utf-8")
        payload = item.get("payload", b"")
        method = item.get("method", 0)
        flags = item.get("flags", 0)
        local_extra = item.get("local_extra", b"")
        central_extra = item.get("central_extra", local_extra)
        descriptor = item.get("descriptor", False)
        if method == 8:
            compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
            packed_data = compressor.compress(payload) + compressor.flush()
        else:
            packed_data = item.get("packed_data", payload)
        crc = item.get("crc", zlib.crc32(payload) & 0xFFFFFFFF)
        packed = item.get("packed", len(packed_data))
        size = item.get("size", len(payload))
        local_at = len(local)
        local_flags = flags | (8 if descriptor else 0)
        local_crc = 0 if descriptor else crc
        local_packed = 0 if descriptor else packed
        local_size = 0 if descriptor else size
        local += struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, local_flags,
                             method, 0, 0, local_crc, local_packed, local_size,
                             len(name), len(local_extra))
        local += name + local_extra + packed_data
        if descriptor:
            local += struct.pack("<IIII", 0x08074B50, crc, packed, size)
        version_made = item.get("version_made", 20)
        external = item.get("external", 0)
        disk = item.get("disk", 0)
        central += struct.pack(
            "<IHHHHHHIIIHHHHHII", 0x02014B50, version_made, 20,
            local_flags, method, 0, 0, crc, packed, size, len(name),
            len(central_extra), 0, disk, 0, external, local_at)
        central += name + central_extra
    central_at = len(local)
    result = local + central
    result += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, len(entries),
                          len(entries), len(central), central_at, len(comment))
    result += comment
    return bytes(result)


def build_zip_content_fixtures(output: Path, disc1: bytes, disc2: bytes,
                               jfd: bytes, adffs_jfd: bytes,
                               too_large_adffs_jfd: bytes,
                               decoded_too_large_adffs_jfd: bytes) -> None:
    """Invented ZIP content and hostile mutants; no external content."""
    output.mkdir(exist_ok=True)
    media = [{"name": "Disc1.adf", "payload": disc1},
             {"name": "Disc2.adf", "payload": disc2}]
    (output / "stored.zip").write_bytes(_manual_zip([media[0]]))
    (output / "deflated.zip").write_bytes(_manual_zip([
        {**media[0], "method": 8}]))
    (output / "descriptor.zip").write_bytes(_manual_zip([
        {**media[0], "method": 8, "descriptor": True}]))
    # Info-ZIP commonly emits longer local timestamp/UID records than central
    # records. These are exactly 28 and 24 bytes respectively.
    ut_local = struct.pack("<HHBII", 0x5455, 9, 3, 1, 2) + \
        struct.pack("<HHBBIBI", 0x7875, 11, 1, 4, 3, 4, 4)
    ut_central = struct.pack("<HHBI", 0x5455, 5, 1, 1) + \
        struct.pack("<HHBBIBI", 0x7875, 11, 1, 4, 3, 4, 4)
    assert len(ut_local) == 28 and len(ut_central) == 24
    (output / "infozip-extra.zip").write_bytes(_manual_zip([
        {**media[0], "local_extra": ut_local,
         "central_extra": ut_central}]))
    ditto_local = struct.pack("<HHIII", 0x5455, 12, 1, 2, 3)
    ditto_central = struct.pack("<HHII", 0x5455, 8, 1, 2)
    (output / "ditto-extra.zip").write_bytes(_manual_zip([
        {**media[0], "method": 8, "descriptor": True,
         "local_extra": ditto_local, "central_extra": ditto_central}]))
    (output / "utf8.zip").write_bytes(_manual_zip([
        {"name": "Élan.adf", "payload": disc1, "flags": 1 << 11}]))
    (output / "nested.zip").write_bytes(_manual_zip([
        {"name": "Games/Arc/Disc1.adf", "payload": disc1}]))
    (output / "mixed.zip").write_bytes(_manual_zip([
        media[0], {"name": "Disc2.jfd", "payload": jfd}]))
    (output / "unsupported-nonmedia.zip").write_bytes(_manual_zip([
        media[0], {"name": "notes.bin", "payload": b"", "method": 99}]))
    (output / "oversize-nonmedia.zip").write_bytes(_manual_zip([
        media[0], {"name": "large.bin", "payload": b"", "size": 8 * 1024 * 1024 + 1}]))
    (output / "false-eocd-comment.zip").write_bytes(_manual_zip(
        [media[0]], b"comment PK\x05\x06 false"))
    unix_version = (3 << 8) | 20
    (output / "unix-mode-0600.zip").write_bytes(_manual_zip([{
        **media[0], "external": 0o600 << 16, "version_made": unix_version}]))
    (output / "unix-mode-symlink.zip").write_bytes(_manual_zip([{
        **media[0], "external": 0o120777 << 16,
        "version_made": unix_version}]))
    (output / "unix-mode-directory.zip").write_bytes(_manual_zip([{
        **media[0], "external": 0o40755 << 16,
        "version_made": unix_version}]))
    ordered = [
        {"name": "Disc10.adf", "payload": disc1},
        {"name": "disc3.adf", "payload": disc1},
        {"name": "Disc2.adf", "payload": disc1},
        {"name": "a/Disc0.adf", "payload": disc1},
        {"name": "Disc1.adf", "payload": disc1},
        {"name": "__MACOSX/Disc1.adf", "payload": disc2},
        {"name": ".hidden/Disc1.adf", "payload": disc2},
        {"name": "._Disc1.adf", "payload": disc2},
        {"name": "folder/", "payload": b"", "external": 0o40755 << 16,
         "version_made": (3 << 8) | 20},
        {"name": "link.adf", "payload": b"target", "external": 0o120777 << 16,
         "version_made": (3 << 8) | 20},
    ]
    (output / "order.zip").write_bytes(_manual_zip(ordered))
    (output / "order-reversed.zip").write_bytes(_manual_zip(list(reversed(ordered))))

    def selection_pair(name: str, names: list[str]) -> None:
        entries = [{"name": member,
                    "payload": b"ArcDroid invented selection fixture"}
                   for member in names]
        (output / f"{name}.zip").write_bytes(_manual_zip(entries))
        (output / f"{name}-reversed.zip").write_bytes(
            _manual_zip(list(reversed(entries))))

    selection_pair("select-library-jfd", [
        "Title (1997) (Pub) F1012502.jfd", "Title.jfd"])
    selection_pair("select-title-adf", [
        "Title.adf", "Title (Europe) (Disk 2).adf"])
    selection_pair("select-disc1-bonus", [
        "Game (Bonus).adf", "Game (Disk 1).adf"])
    selection_pair("grammar-disc1", ["Game Disc1.adf", "Zzz.adf"])
    selection_pair("grammar-disk01", ["Game (Disk 01).adf", "Zzz.adf"])
    selection_pair("grammar-disc-1", ["Game disc-1.adf", "Zzz.adf"])
    selection_pair("grammar-disk-of", ["Game Disk 1 of 3.adf", "Zzz.adf"])
    selection_pair("grammar-disk10", ["Game Disk10.adf", "Zzz.adf"])
    selection_pair("grammar-disk1a", ["Game Disk1a.adf", "Zzz.adf"])
    selection_pair("grammar-harddisk1", ["Game HardDisk1.adf", "Zzz.adf"])
    selection_pair("grammar-side-a", ["Game Side A.adf", "Zzz.adf"])
    selection_pair("grammar-contradictory", [
        "Game Disk 1 Disk 2.adf", "Zzz.adf"])
    selection_pair("select-t1-over-name", [
        "Title.adf", "Other (Disk 1).adf"])
    selection_pair("select-t2-tie", ["Title.adf", "Title.jfd"])
    selection_pair("select-folder", [
        "Game.adf/Game.adf", "Game.adf/Game (Disk 2).adf"])
    selection_pair("select-folder-stem-negative", [
        "Game/Game.adf", "Game/Game 2.adf"])
    selection_pair("select-folder-isolated", [
        "Extras/Extras.adf", "Root.adf", "Root 2.adf"])
    selection_pair("select-folder-prefix", [
        "Game.adf/Game.adf", "Game.adfX/other.adf"])
    selection_pair("select-folder-peers", [
        "Game.adf/Game.adf", "Othe.adf/Othe.adf"])
    selection_pair("select-folder-depth", [
        "Game.adf/Game.adf", "Game.adf/sub/Game.adf"])
    selection_pair("select-folder-single", ["Solo.adf/Solo.adf"])
    selection_pair("select-windows-name", ["Title.adf"])
    selection_pair("select-nested", [
        "outer/Game (Disk 2).adf", "inner/Game (Disk 1).adf", "A.adf"])
    selection_pair("select-32", [f"Image {i:02d}.adf" for i in range(1, 33)])
    selection_pair("select-33", [f"Image {i:02d}.adf" for i in range(1, 34)])

    (output / "single.zip").write_bytes(_manual_zip([media[0]]))
    (output / "multi.zip").write_bytes(_manual_zip([
        {"name": "multi (Disk 2).adf", "payload": disc2},
        {"name": "multi.adf", "payload": disc1}]))
    (output / "natural.zip").write_bytes(_manual_zip([
        {"name": "Alpha.adf", "payload": disc1},
        {"name": "Beta.adf", "payload": disc2}]))
    (output / "Title.zip").write_bytes(_manual_zip([
        {"name": "Title (Europe) (Disk 2).adf", "payload": disc2},
        {"name": "Title.adf", "payload": disc1}]))
    (output / "DiscRule.zip").write_bytes(_manual_zip([
        {"name": "Game (Bonus).adf", "payload": disc2},
        {"name": "Game (Disk 1).adf", "payload": disc1}]))
    (output / "reset-multi.zip").write_bytes(_manual_zip(media))
    (output / "multi-replaced.zip").write_bytes(_manual_zip([
        {"name": "Disc1.adf", "payload": disc2}, media[1]]))
    (output / "multi-second-replaced.zip").write_bytes(_manual_zip([
        media[0], {"name": "Disc2.adf", "payload": disc1}]))
    (output / "single-jfd.zip").write_bytes(_manual_zip([
        {"name": "Game.jfd", "payload": adffs_jfd}]))
    (output / "too-large-jfd.zip").write_bytes(_manual_zip([
        {"name": "Game.jfd", "payload": too_large_adffs_jfd}]))
    (output / "decoded-too-large-jfd.zip").write_bytes(_manual_zip([
        {"name": "Game.jfd", "payload": decoded_too_large_adffs_jfd}]))
    (output / "two-jfd.zip").write_bytes(_manual_zip([
        {"name": "Disc1.jfd", "payload": adffs_jfd},
        {"name": "Disc2.jfd", "payload": adffs_jfd}]))
    bad_first = _manual_zip(media)
    bad_first = bytearray(bad_first)
    first_central = bad_first.index(b"PK\x01\x02")
    struct.pack_into("<I", bad_first, 14,
                     struct.unpack_from("<I", bad_first, 14)[0] ^ 1)
    struct.pack_into("<I", bad_first, first_central + 16,
                     struct.unpack_from("<I", bad_first, first_central + 16)[0] ^ 1)
    (output / "bad-crc.zip").write_bytes(bad_first)
    partial_first = _manual_zip([
        {"name": 'Game " (Disk 1).adf', "payload": disc1}, media[1]])
    partial_first = bytearray(partial_first)
    partial_central = partial_first.index(b"PK\x01\x02")
    struct.pack_into("<I", partial_first, 14,
                     struct.unpack_from("<I", partial_first, 14)[0] ^ 1)
    struct.pack_into("<I", partial_first, partial_central + 16,
                     struct.unpack_from("<I", partial_first,
                                        partial_central + 16)[0] ^ 1)
    (output / "partial-first.zip").write_bytes(partial_first)
    (output / "partial-multiple.zip").write_bytes(_manual_zip([
        {"name": "Game (Disk 1).adf", "payload": disc1, "crc": 1},
        {"name": "Broken.adf", "payload": disc2, "crc": 1},
        {"name": "Survivor.adf", "payload": disc2}]))
    (output / "only-corrupt.zip").write_bytes(_manual_zip([
        {"name": "Disc1.adf", "payload": disc1, "crc": 1}]))
    (output / "no-media.zip").write_bytes(_manual_zip([
        {"name": "readme.txt", "payload": b"invented notes"}]))
    (output / "too-many.zip").write_bytes(_manual_zip([
        {"name": f"Disc{i:02d}.adf", "payload": disc1} for i in range(33)]))
    (output / "entry-limit.zip").write_bytes(_manual_zip([
        {"name": f"unused/{i:04d}.txt", "payload": b""}
        for i in range(1025)]))
    (output / "zip-entry.m3u").write_text("single.zip\n")
    (output / "zip-member-entry.m3u").write_text("single.zip#Disc1.adf\n")
    (output / "unsupported.7z").write_bytes(b"7z\xbc\xaf\x27\x1c invented")
    (output / "oversize-media.zip").write_bytes(_manual_zip([
        {"name": "Large.adf", "payload": b"", "size": 8 * 1024 * 1024 + 1},
        media[1]]))
    (output / "unsupported-media.zip").write_bytes(_manual_zip([
        {"name": "Broken.adf", "payload": b"", "method": 99}, media[1]]))

    def mutant(name: str, data: bytes, change) -> None:
        value = bytearray(data)
        change(value)
        (output / name).write_bytes(value)

    base = _manual_zip([media[0]])
    two = _manual_zip(media)
    mutant("encrypted.zip", base, lambda b: (
        struct.pack_into("<H", b, 6, struct.unpack_from("<H", b, 6)[0] | 1),
        struct.pack_into("<H", b, b.index(b"PK\x01\x02") + 8,
                         struct.unpack_from("<H", b, b.index(b"PK\x01\x02") + 8)[0] | 1)))
    mutant("zip64.zip", base, lambda b: struct.pack_into(
        "<H", b, b.rindex(b"PK\x05\x06") + 10, 0xFFFF))
    zip64_extra = struct.pack("<HH", 1, 0)
    (output / "zip64-extra.zip").write_bytes(_manual_zip([
        {**media[0], "local_extra": zip64_extra,
         "central_extra": zip64_extra}]))
    duplicate = bytearray(two)
    records = _zip_name_records(duplicate)
    for at, size, _, cat, csize, _ in [records[1]]:
        duplicate[at:at + size] = b"Disc1.adf"
        duplicate[cat:cat + csize] = b"Disc1.adf"
    (output / "duplicate.zip").write_bytes(duplicate)
    (output / "truncated.zip").write_bytes(base[:-5])
    descriptor = bytearray(_manual_zip([{**media[0], "descriptor": True}]))
    first_central = descriptor.index(b"PK\x01\x02")
    descriptor[first_central - 12] ^= 1
    (output / "descriptor-mismatch.zip").write_bytes(descriptor)
    mutant("local-mismatch.zip", base, lambda b: struct.pack_into("<H", b, 8, 8))
    mutant("central-size-mismatch.zip", base, lambda b: struct.pack_into(
        "<I", b, b.rindex(b"PK\x05\x06") + 12,
        struct.unpack_from("<I", b, b.rindex(b"PK\x05\x06") + 12)[0] - 1))
    for filename, raw_name in (("unsafe.zip", b"../bad.adf"),
                               ("absolute.zip", b"/badxxxxx"),
                               ("backslash.zip", b"bad\\x.adf"),
                               ("dot-component.zip", b"./bad.adf"),
                               ("nul-name.zip", b"bad\0x.adf")):
        value = bytearray(base)
        record = _zip_name_records(value)[0]
        raw_name = raw_name.ljust(record[1], b"x")[:record[1]]
        value[record[0]:record[0] + record[1]] = raw_name
        value[record[3]:record[3] + record[4]] = raw_name
        (output / filename).write_bytes(value)
    malformed = struct.pack("<HH", 0x5455, 2) + b"x"
    (output / "malformed-extra.zip").write_bytes(_manual_zip([
        {**media[0], "local_extra": malformed,
         "central_extra": malformed}]))
    arc0_a = struct.pack("<HH4sIII", 0x4341, 16, b"ARC0", 1, 2, 3)
    arc0_b = struct.pack("<HH4sIII", 0x4341, 16, b"ARC0", 4, 5, 6)
    (output / "conflicting-arc0.zip").write_bytes(_manual_zip([
        {**media[0], "local_extra": arc0_a, "central_extra": arc0_b}]))
    mutant("entry-disk.zip", base, lambda b: struct.pack_into(
        "<H", b, b.index(b"PK\x01\x02") + 34, 1))
    inflated = bytearray(_manual_zip([{**media[0], "method": 8}, media[1]]))
    central_at = inflated.index(b"PK\x01\x02")
    struct.pack_into("<I", inflated, 22,
                     struct.unpack_from("<I", inflated, 22)[0] + 1)
    struct.pack_into("<I", inflated, central_at + 24,
                     struct.unpack_from("<I", inflated, central_at + 24)[0] + 1)
    (output / "inflated-lie.zip").write_bytes(inflated)
    overlap = bytearray(two)
    overlap_records = _zip_name_records(overlap)
    first_central = overlap.index(b"PK\x01\x02")
    first_data = overlap_records[0][0] + overlap_records[0][1]
    second_local = struct.unpack_from("<I", overlap,
        overlap.index(b"PK\x01\x02", first_central + 4) + 42)[0]
    overlap_size = second_local - first_data + 8
    struct.pack_into("<II", overlap, 18, overlap_size, overlap_size)
    struct.pack_into("<II", overlap, first_central + 20,
                     overlap_size, overlap_size)
    (output / "overlap.zip").write_bytes(overlap)
    central_overlap = bytearray(base)
    central_at = central_overlap.index(b"PK\x01\x02")
    struct.pack_into("<I", central_overlap, 18, central_at)
    struct.pack_into("<I", central_overlap, central_at + 20, central_at)
    (output / "central-overlap.zip").write_bytes(central_overlap)


def build_adffs_fixtures(output: Path) -> None:
    """Invented package/JFD data; no JASPP byte is an input."""
    output.mkdir(exist_ok=True)
    built_in = b"Set ArcFixture Builtin\rADFFS:trackers.QTMModule.extra\r"
    obey_entries = [
        ("F0012301", built_in, False),
        ("F0012301A", b"Set ArcFixture Auxiliary\r", False),
        ("ChainA", b"ADFFS:extras.JoyMod\rADFFS:obey.ChainB\r", False),
        ("ChainB", b"adffs:TRACKERS.CaseTrack.ext\r", False),
        ("CycleA", b"ADFFS:obey.CycleB\r", False),
        ("CycleB", b"ADFFS:obey.CycleA\r", False),
        ("Depth1", b"ADFFS:obey.Depth2\r", False),
        ("Depth2", b"ADFFS:obey.Depth3\r", False),
        ("Depth3", b"ADFFS:obey.Depth4\r", False),
        ("Depth4", b"ADFFS:obey.Depth5\r", False),
        ("Depth5", b"Set ArcFixture TooDeep\r", False),
    ]
    obey = _zip_bytes(obey_entries)
    runtime_names = ("ADFFS", "SetVars", "CLib", "CallASWI", "ZLib", "BootCmds")
    runtime_paths = ("!ADFFS/ADFFS", "!ADFFS/SetVars",
                     "!ADFFS/3rdParty/CLib", "!ADFFS/3rdParty/CallASWI",
                     "!ADFFS/3rdParty/ZLib", "!ADFFS/3rdParty/BootCmds")
    module = bytearray(24)
    struct.pack_into("<I", module, 20, len(module))
    module += b"Synthetic 1.0\0"
    entries = [(runtime_paths[0], bytes(module), True)]
    entries += [(path, f"ArcDroid synthetic ADFFS {name} fixture\n".encode(), True)
                for name, path in zip(runtime_names[1:], runtime_paths[1:])]
    entries += [("!ADFFS/obey", obey, True),
                ("!ADFFS/!RunTypes", b"synthetic run types\n", True),
                ("!ADFFS/!fixObey", b"synthetic fix obey\n", True),
                ("!ADFFS/trackers/QTMModule", b"synthetic tracker\n", True),
                ("!ADFFS/trackers/CaseTrack", b"case tracker\n", True),
                ("!ADFFS/trackers/Huge", bytes(780 * 1024), True),
                ("!ADFFS/extras/JoyMod", b"synthetic extra\n", True),
                ("unrelated/readme", b"ignored", False)]
    package = _zip_bytes(entries)
    (output / "synthetic.zip").write_bytes(package)
    (output / "missing-install-resource.zip").write_bytes(_zip_bytes([
        entry for entry in entries if entry[0] != "!ADFFS/trackers/QTMModule"]))
    changed_obey = _zip_bytes([
        (name, payload + b"\r" if name == "F0012301" else payload, meta)
        for name, payload, meta in obey_entries])
    (output / "script-changed.zip").write_bytes(_zip_bytes([
        (name, changed_obey if name == "!ADFFS/obey" else payload, metadata)
        for name, payload, metadata in entries]))
    (output / "stored.zip").write_bytes(_zip_bytes(entries, zipfile.ZIP_STORED))
    (output / "descriptor.zip").write_bytes(_zip_bytes(
        entries, zipfile.ZIP_DEFLATED, streaming=True))
    (output / "reordered.zip").write_bytes(_zip_bytes(list(reversed(entries))))
    untested_entries = list(entries)
    flipped_runtime = bytearray(untested_entries[0][1])
    flipped_runtime[0] ^= 1
    untested_entries[0] = (untested_entries[0][0], bytes(flipped_runtime), True)
    (output / "untested.zip").write_bytes(_zip_bytes(untested_entries))
    (output / "truncated.zip").write_bytes(package[:-7])

    def patched(payload: bytes, local_change=None, central_change=None,
                eocd_change=None) -> bytes:
        value = bytearray(payload)
        local = value.index(b"PK\x03\x04")
        central = value.index(b"PK\x01\x02")
        eocd = value.rindex(b"PK\x05\x06")
        if local_change:
            local_change(value, local)
        if central_change:
            central_change(value, central)
        if eocd_change:
            eocd_change(value, eocd)
        return bytes(value)

    (output / "bad-crc.zip").write_bytes(patched(
        package,
        lambda b, p: struct.pack_into("<I", b, p + 14,
                                       struct.unpack_from("<I", b, p + 14)[0] ^ 1),
        lambda b, p: struct.pack_into("<I", b, p + 16,
                                       struct.unpack_from("<I", b, p + 16)[0] ^ 1)))
    (output / "encrypted.zip").write_bytes(patched(
        package,
        lambda b, p: struct.pack_into("<H", b, p + 6,
                                       struct.unpack_from("<H", b, p + 6)[0] | 1),
        lambda b, p: struct.pack_into("<H", b, p + 8,
                                       struct.unpack_from("<H", b, p + 8)[0] | 1)))
    (output / "zip64.zip").write_bytes(patched(
        package, eocd_change=lambda b, p: struct.pack_into("<H", b, p + 10, 0xFFFF)))
    zip64_extra = bytearray(package)
    for search_at in (0, zip64_extra.index(b"PK\x01\x02")):
        marker = zip64_extra.index(b"ARC0", search_at)
        struct.pack_into("<H", zip64_extra, marker - 4, 0x0001)
    (output / "zip64-extra.zip").write_bytes(zip64_extra)
    (output / "descriptor-mismatch.zip").write_bytes(patched(
        package,
        lambda b, p: (struct.pack_into("<H", b, p + 6,
                                        struct.unpack_from("<H", b, p + 6)[0] | 8),
                       struct.pack_into("<III", b, p + 14, 0, 0, 0)),
        lambda b, p: struct.pack_into("<H", b, p + 8,
                                       struct.unpack_from("<H", b, p + 8)[0] | 8)))
    (output / "local-mismatch.zip").write_bytes(patched(
        package, local_change=lambda b, p: struct.pack_into("<H", b, p + 8, 0)))
    extra_mismatch = bytearray(package)
    first_central = extra_mismatch.index(b"PK\x01\x02")
    local_arc0 = extra_mismatch.index(b"ARC0", 0, first_central)
    extra_mismatch[local_arc0] ^= 1
    (output / "extra-mismatch.zip").write_bytes(extra_mismatch)
    (output / "central-size-mismatch.zip").write_bytes(patched(
        package, eocd_change=lambda b, p: struct.pack_into(
            "<I", b, p + 12, struct.unpack_from("<I", b, p + 12)[0] - 1)))
    bomb = patched(package,
        local_change=lambda b, p: struct.pack_into("<I", b, p + 22,
             struct.unpack_from("<I", b, p + 22)[0] + 1),
        central_change=lambda b, p: struct.pack_into("<I", b, p + 24,
             struct.unpack_from("<I", b, p + 24)[0] + 1))
    (output / "inflated-lie.zip").write_bytes(bomb)
    no_arc = bytearray(package.replace(b"ARC0", b"NOPE"))
    (output / "missing-arc0.zip").write_bytes(no_arc)
    _write_raw_name_fixture(output / "duplicate.zip", entries,
                            b"!ADFFS/ADFFS", entries[0][1], True, 2)
    _write_raw_name_fixture(output / "case-collision.zip", entries,
                            b"!adffs/adffs", b"case collision", True)
    _write_raw_name_fixture(output / "unsafe-name.zip", entries,
                            b"../bad", b"x", False)
    _write_raw_name_fixture(output / "absolute-name.zip", entries,
                            b"/bad", b"x", False)
    _write_raw_name_fixture(output / "backslash-name.zip", entries,
                            b"bad\\name", b"x", False)
    (output / "entry-limit.zip").write_bytes(_zip_bytes(
        [(f"unused/{i:04d}", b"", False) for i in range(1025)], zipfile.ZIP_STORED))
    (output / "oversize.zip").write_bytes(_zip_bytes(
        entries + [("unused/large", bytes(2 * 1024 * 1024 + 1), False)]))
    (output / "used-total.zip").write_bytes(_zip_bytes(entries + [
        ("!ADFFS/trackers/LimitA", bytes(2 * 1024 * 1024), True),
        ("!ADFFS/trackers/LimitB", bytes(2 * 1024 * 1024), True),
    ]))
    (output / "excluded-trackers.zip").write_bytes(_zip_bytes(entries + [
        ("!ADFFS/trackers/TimPlayer", b"excluded player", False),
        ("!ADFFS/trackers/!Help", b"excluded help", False),
    ]))
    for filename, help_text in (
            ("version-control.zip", b"Synthetic\x01control\0"),
            ("version-quote.zip", b'Synthetic "quote\0'),
            ("version-backslash.zip", b"Synthetic \\path\0")):
        changed_module = bytearray(24)
        struct.pack_into("<I", changed_module, 20, len(changed_module))
        changed_module += help_text
        (output / filename).write_bytes(_zip_bytes([
            (name, bytes(changed_module) if name == "!ADFFS/ADFFS" else payload,
             metadata)
            for name, payload, metadata in entries]))
    for filename, omitted in (("missing-obey.zip", "!ADFFS/obey"),
                              ("missing-runtypes.zip", "!ADFFS/!RunTypes"),
                              ("missing-fixobey.zip", "!ADFFS/!fixObey")):
        (output / filename).write_bytes(_zip_bytes(
            [entry for entry in entries if entry[0] != omitted]))
    invalid_obey = _zip_bytes(obey_entries + [("Bad-Name", b"x", False)])
    (output / "invalid-obey-name.zip").write_bytes(_zip_bytes([
        (name, invalid_obey if name == "!ADFFS/obey" else payload, metadata)
        for name, payload, metadata in entries]))

    def adffs_jfd(game: int, disc: int = 1, count: int = 1,
                  embedded: bytes = b"", decoded_size: int | None = None) -> bytearray:
        image = build_extended_jfd(bytes(256), b"ArcDroid ADFFS fixture")
        if decoded_size is not None:
            assert decoded_size >= len(image) + len(embedded)
            image += bytes(decoded_size - len(image) - len(embedded))
        image += embedded
        struct.pack_into("<I", image, 8, len(image))
        struct.pack_into("<HHI", image, 12, disc, count, game)
        struct.pack_into("<I", image, 312, len(embedded))
        return image

    embedded = b"Set ArcFixture Embedded\rADFFS:extras.JoyMod\r"
    (output / "eligible.jfd").write_bytes(adffs_jfd(123, embedded=embedded))
    # Two complete D-format track-sides satisfy the D-076 layout planner;
    # the remaining sides are empty and the captured image stays small enough
    # for the ADFFS carrier. All identities and content here are invented.
    writable = build_pdd5_jfd(bytes(819200),
                              {0: list(range(5)), 1: list(range(5))},
                              title=b"ArcDroid writable media")
    writable += embedded
    struct.pack_into("<I", writable, 8, len(writable))
    struct.pack_into("<HHI", writable, 12, 1, 1, 54321)
    struct.pack_into("<I", writable, 312, len(embedded))
    (output / "writable.jfd").write_bytes(writable)
    quarantine_image = adffs_jfd(54321, embedded=embedded)
    track_table = struct.unpack_from("<I", quarantine_image, 24)[0]
    sector_table = struct.unpack_from("<I", quarantine_image, 28)[0]
    first_descriptor = sector_table + struct.unpack_from(
        "<I", quarantine_image, track_table)[0]
    header = struct.unpack_from("<I", quarantine_image, first_descriptor)[0]
    struct.pack_into("<I", quarantine_image, first_descriptor,
                     (header & ~(0xF << 16)) | (8 << 16))
    (output / "quarantine-adffs.jfd").write_bytes(quarantine_image)
    (output / "writable-gzip.jfd").write_bytes(gzip_member(bytes(writable)))
    (output / "writable.zip").write_bytes(_zip_bytes([
        ("ArcDroid writable.jfd", bytes(writable), True)]))
    (output / "writable-multi.m3u").write_text("writable.jfd\nembedded-only.jfd\n")
    writable_folder = output / "Writable-folder.jfd"
    writable_folder.mkdir(exist_ok=True)
    (writable_folder / "Writable-folder.jfd").write_bytes(writable)
    (writable_folder / "Other.jfd").write_bytes(adffs_jfd(99999, embedded=embedded))
    different = bytearray(writable)
    different[-1] ^= 1
    (output / "writable-digest.jfd").write_bytes(different)
    longer = bytearray(writable) + b"x"
    struct.pack_into("<I", longer, 8, len(longer))
    (output / "writable-size.jfd").write_bytes(longer)
    for name, disc, count, game in (
            ("writable-game.jfd", 1, 1, 54322),
            ("writable-install.jfd", 1, 1, 10587),
            ("writable-disc.jfd", 2, 1, 54321),
            ("writable-count.jfd", 1, 2, 54321)):
        changed = bytearray(writable)
        struct.pack_into("<HHI", changed, 12, disc, count, game)
        (output / name).write_bytes(changed)
    for name, flags in (("writable-wp.jfd", 3),
                        ("writable-no-flag.jfd", 0),
                        ("writable-os31.jfd", 2 | (1 << 17)),
                        ("writable-os16-17.jfd", 2 | (1 << 16) | (1 << 17)),
                        ("writable-os17-22.jfd", 2 | (0x7e << 16)),
                        ("writable-os2.jfd", 2 | (1 << 16)),
                        ("writable-os18.jfd", 2 | (1 << 18)),
                        ("writable-os22.jfd", 2 | (1 << 22))):
        changed = bytearray(writable)
        struct.pack_into("<I", changed, 304, flags)
        (output / name).write_bytes(changed)
    # A legacy header is provided by the ordinary single-sector generator.
    legacy = build_jfd(bytes(256))
    struct.pack_into("<HHI", legacy, 12, 1, 1, 54321)
    (output / "writable-legacy.jfd").write_bytes(legacy)
    unsupported = build_pdd5_jfd(bytes(819200),
                                 {0: list(range(5)), 1: list(range(5))},
                                 track_count=159)
    unsupported += embedded
    struct.pack_into("<I", unsupported, 8, len(unsupported))
    struct.pack_into("<HHI", unsupported, 12, 1, 1, 54321)
    struct.pack_into("<I", unsupported, 312, len(embedded))
    (output / "writable-layout-fail.jfd").write_bytes(unsupported)
    (output / "embedded-only.jfd").write_bytes(adffs_jfd(99999, embedded=embedded))
    (output / "no-script.jfd").write_bytes(adffs_jfd(0))
    (output / "game-only.jfd").write_bytes(adffs_jfd(99999))
    (output / "disc-two.jfd").write_bytes(adffs_jfd(123, disc=2))
    (output / "count-two.jfd").write_bytes(adffs_jfd(123, count=2))
    (output / "missing-resource.jfd").write_bytes(adffs_jfd(
        0, embedded=b"ADFFS:ExTrAs.Absent.module\r"))
    (output / "comment-reference.jfd").write_bytes(adffs_jfd(
        0, embedded=b" \t | ADFFS:ExTrAs.Absent.module\r"))
    (output / "closure-too-large.jfd").write_bytes(adffs_jfd(
        0, embedded=b"ADFFS:trackers.Huge\r"))
    carrier_capacity = 819200 - 4096
    (output / "capacity.jfd").write_bytes(adffs_jfd(
        123, decoded_size=carrier_capacity))
    (output / "too-large.jfd").write_bytes(adffs_jfd(
        123, decoded_size=carrier_capacity + 1))
    decoded_limit = 2048 * 1024
    (output / "decoded-limit.jfd").write_bytes(gzip_member(bytes(adffs_jfd(
        123, decoded_size=decoded_limit))))
    (output / "decoded-too-large.jfd").write_bytes(gzip_member(bytes(adffs_jfd(
        123, decoded_size=decoded_limit + 256))))
    (output / "multi.m3u").write_text("eligible.jfd\nembedded-only.jfd\n")
    for disc in (1, 2, 3):
        image = adffs_jfd(123, disc=disc, count=4)
        if disc == 1:
            image = build_pdd5_jfd(bytes(819200), {0: list(range(5))},
                                    title=b"ArcDroid synthetic install P-DD5")
            struct.pack_into("<HHI", image, 12, disc, 4, 123)
        image[-1] = disc
        (output / f"install-{disc}.jfd").write_bytes(image)
    (output / "install.m3u").write_text(
        "install-1.jfd\ninstall-2.jfd\ninstall-3.jfd\n")
    (output / "install-first.m3u").write_text(
        "install-1.jfd\ninstall-2.jfd\n")
    discs = [(output / f"install-{disc}.jfd").read_bytes()
             for disc in (1, 2, 3)]
    (output / "install.zip").write_bytes(_zip_bytes([
        (f"Install (Disk {disc}).jfd", image, True)
        for disc, image in enumerate(discs, 1)]))
    folder = output / "Install.jfd"
    folder.mkdir(exist_ok=True)
    for disc, image in enumerate(discs, 1):
        (folder / ("Install.jfd" if disc == 1 else
                   f"Install {disc}.jfd")).write_bytes(image)
    compressed = bytearray(gzip_member(discs[2]))
    compressed[3] |= 0x10  # FCOMMENT: raw wrapper grows; decoded identity stays.
    compressed[10:10] = b"X" * (810 * 1024) + b"\0"
    (output / "install-fat-3.jfd").write_bytes(compressed)
    (output / "install-fat.m3u").write_text(
        "install-1.jfd\ninstall-2.jfd\ninstall-fat-3.jfd\n")
    (output / "install-too-large.m3u").write_text(
        "install-1.jfd\ninstall-fat-3.jfd\n")
    raw_limit = bytearray(gzip_member(discs[1]))
    raw_limit[3] |= 0x10
    raw_limit[10:10] = b"Y" * (2 * 1024 * 1024) + b"\0"
    (output / "install-raw-limit-2.jfd").write_bytes(raw_limit)
    (output / "install-raw-limit.m3u").write_text(
        "install-1.jfd\ninstall-raw-limit-2.jfd\n")
    wrong = bytearray(discs[1])
    wrong[-1] ^= 0x40
    (output / "install-wrong-2.jfd").write_bytes(wrong)
    (output / "install-wrong.m3u").write_text(
        "install-1.jfd\ninstall-wrong-2.jfd\n")
    hd = adffs_jfd(123)
    struct.pack_into("<I", hd, 324, (4 << 16) | 1)
    (output / "eligible-hd.jfd").write_bytes(hd)


def build_jfd_family(descriptors: list[tuple[int, int, int, int, int, bytes | None]],
                     title: bytes = b"ArcDroid JFD family",
                     disk_payload: bytes | None = None,
                     disk_geometry: str | None = None,
                     extended: bool = False,
                     second_descriptors: tuple = ()) -> bytearray:
    """Build one track-side from (time, B, density, id, C, payload)."""
    if disk_payload is not None and disk_geometry is not None:
        return _build_jfd_disk(disk_payload, disk_geometry, title, False)
    track_offset = 316 if extended else 304
    sector_offset = track_offset + 8
    data_offset = sector_offset + (len(descriptors) + len(second_descriptors)) * 8 + (8 if second_descriptors else 4)
    payloads = bytearray()
    table = bytearray()
    for index, (time, block, density, sector_id, crc, payload) in enumerate(
            list(descriptors) + list(second_descriptors)):
        if index == len(descriptors):
            table += struct.pack("<I", 0xFFFFFFFF)
        size = len(payload) if payload is not None else 128
        assert size >= 128 and size <= 128 << 15 and size & (size - 1) == 0
        size_code = size.bit_length() - 8
        header = ((time & 0xFF) << 24) | ((block & 0xF) << 20) | \
                 ((density & 0xF) << 16) | ((sector_id & 0xFF) << 8) | \
                 ((crc & 0xF) << 4) | size_code
        offset = 0xFFFFFFFF if payload is None else len(payloads)
        table += struct.pack("<II", header, offset)
        if payload is not None:
            payloads += payload
    table += struct.pack("<I", 0xFFFFFFFF)
    image = bytearray(data_offset + len(payloads))
    image[:4] = b"JFDI"
    struct.pack_into("<I", image, 8, len(image))
    struct.pack_into("<III", image, 24, track_offset, sector_offset, data_offset)
    image[48:48 + min(len(title), 256)] = title[:256]
    if extended:
        struct.pack_into("<I", image, 304, 2)
    struct.pack_into("<II", image, track_offset, 0,
                     len(descriptors) * 8 + 4 if second_descriptors else 0xFFFFFFFF)
    image[sector_offset:data_offset] = table
    image[data_offset:] = payloads
    return image


def _store_u24(image: bytearray, offset: int, value: int) -> None:
    image[offset:offset + 3] = value.to_bytes(3, "little")


def _old_map_checksum(block: bytes) -> int:
    """Return FileCore's byte checksum for a 255-byte old-map block."""
    checksum = 0xFF
    for byte in reversed(block):
        if checksum > 0xFF:
            checksum = (checksum + 1) & 0xFF
        checksum += byte
    return checksum & 0xFF


def _build_absolute_boot(
        title: bytes = b"ARCDROID - PROJECT AUTHORED BOOT DISC") -> bytes:
    """Build a tiny ARM2 Absolute program that paints a stable VDU pattern."""
    vdu = bytearray((22, 12, 17, 128 + 1, 12, 23, 1, 0, 0, 0, 0, 0, 0, 0, 0))
    for row in range(4, 28):
        vdu.extend((31, 8, row, 17, 2 + (row % 6)))
        vdu.extend((b"##  " if row & 1 else b"  ##") * 16)
    vdu.extend((31, 20, 15, 17, 7))
    vdu.extend(title)

    # Load at &8000. The two literals point to VDU data and give its length.
    # OS_WriteC is SWI 0; the final self-branch keeps the pattern presented.
    words = [
        0xE59F1014,             # ldr r1, [pc, #20]
        0xE59F2014,             # ldr r2, [pc, #20]
        0xE4D10001,             # ldrb r0, [r1], #1
        0xEF000000,             # swi OS_WriteC
        0xE2522001,             # subs r2, r2, #1
        0x1AFFFFFB,             # bne byte loop
        0xEAFFFFFE,             # b .
        0x00008024,             # VDU byte stream address
        len(vdu),
    ]
    return b"".join(struct.pack("<I", word) for word in words) + vdu


def build_demo_adl() -> bytearray:
    """Build a 640 KiB FileCore L-format disc with a runnable root !Boot.

    The old-map and old-directory layouts are from the RISC OS 3 FileCore PRM:
    https://www.riscos.com/support/developers/prm/filecore.html#69647
    https://www.riscos.com/support/developers/prm/filecore.html#66109
    The disc boot option is 2 (`*Opt 4,2`, Run). The directory checksum is zero,
    the defined old-directory compatibility sentinel used by 32-bit ADFS.
    """
    image = bytearray(ADL_SIZE)
    boot = _build_absolute_boot()
    boot_sectors = (len(boot) + SECTOR_SIZE - 1) // SECTOR_SIZE
    data_sector = (OLD_MAP_SIZE + OLD_DIRECTORY_SIZE) // SECTOR_SIZE
    free_sector = data_sector + boot_sectors
    total_sectors = ADL_SIZE // SECTOR_SIZE

    # Old map: one contiguous free extent after the root directory and !Boot.
    _store_u24(image, 0, free_sector)
    image[247:252] = b"ACDMD"
    _store_u24(image, 252, total_sectors)
    image[255] = _old_map_checksum(image[0:255])
    _store_u24(image, 256, total_sectors - free_sector)
    image[502:507] = b"ROISO"
    image[507:509] = (0xA6D1).to_bytes(2, "little")
    image[509] = 2              # OldBoot: Run $.!Boot
    image[510] = 3              # one three-byte free-list entry
    image[511] = _old_map_checksum(image[256:511])

    root = OLD_MAP_SIZE
    image[root] = 1
    image[root + 1:root + 5] = b"Hugo"
    entry = root + 5
    image[entry:entry + 10] = b"!Boot\r\r\r\r\r"
    image[entry] |= 0x80        # owner read access
    image[entry + 1] |= 0x80    # owner write access
    image[entry + 10:entry + 14] = (0x8000).to_bytes(4, "little")
    image[entry + 14:entry + 18] = (0x8000).to_bytes(4, "little")
    image[entry + 18:entry + 22] = len(boot).to_bytes(4, "little")
    _store_u24(image, entry + 22, data_sector)
    image[entry + 25] = 0

    tail = root + OLD_DIRECTORY_SIZE - 53
    image[tail] = 0
    image[tail + 1:tail + 11] = b"$".ljust(10, b"\r")
    _store_u24(image, tail + 11, 2)
    image[tail + 14:tail + 33] = b"ArcDroid demo".ljust(19, b"\0")
    image[tail + 47] = 1
    image[tail + 48:tail + 52] = b"Hugo"
    image[tail + 52] = 0
    data_offset = data_sector * SECTOR_SIZE
    image[data_offset:data_offset + len(boot)] = boot

    # Deterministic construction invariants double as a format round-trip.
    assert int.from_bytes(image[252:255], "little") * SECTOR_SIZE == len(image)
    assert int.from_bytes(image[entry + 22:entry + 25], "little") == data_sector
    assert image[509] == 2
    assert bytes(byte & 0x7F for byte in image[entry:entry + 5]) == b"!Boot"
    assert image[255] == _old_map_checksum(image[0:255])
    assert image[511] == _old_map_checksum(image[256:511])
    return image


def _l_physical_sector(logical_sector: int) -> int:
    track = logical_sector // 16
    side = track // 80
    cylinder = track % 80
    return (cylinder * 2 + side) * 16 + logical_sector % 16


def _l_write(image: bytearray, logical_sector: int, payload: bytes) -> None:
    for at in range(0, len(payload), SECTOR_SIZE):
        physical = _l_physical_sector(logical_sector + at // SECTOR_SIZE)
        chunk = payload[at:at + SECTOR_SIZE]
        image[physical * SECTOR_SIZE:physical * SECTOR_SIZE + len(chunk)] = chunk


def _old_entry(directory: bytearray, index: int, name: bytes, length: int,
               sector: int, directory_object: bool = False,
               load: int = 0xFFF00000) -> None:
    at = 5 + index * 26
    directory[at:at + 10] = name.ljust(10, b"\r")
    directory[at] |= 0x80
    directory[at + 1] |= 0x80
    if directory_object:
        directory[at + 3] |= 0x80
    # Observed on RISC OS 3.11: a typed file is not runnable when its exec
    # field repeats its load address. Absolute/untyped objects retain load.
    exec_address = 0 if load >> 20 == 0xFFF else load
    struct.pack_into("<II", directory, at + 10, load, exec_address)
    struct.pack_into("<I", directory, at + 18, length)
    _store_u24(directory, at + 22, sector)


def _old_directory(name: bytes = b"$") -> bytearray:
    directory = bytearray(OLD_DIRECTORY_SIZE)
    directory[0] = 1
    directory[1:5] = b"Hugo"
    tail = OLD_DIRECTORY_SIZE - 53
    directory[tail] = 0
    directory[tail + 1:tail + 11] = name.ljust(10, b"\r")
    _store_u24(directory, tail + 11, 2)
    directory[tail + 14:tail + 33] = b"ArcDroid fixture".ljust(19, b"\0")
    directory[tail + 47] = 1
    directory[tail + 48:tail + 52] = b"Hugo"
    return directory


def build_application_l() -> bytearray:
    """Option-0 L disc with one application and a cross-side Absolute file."""
    image = bytearray(ADL_SIZE)
    total_sectors = ADL_SIZE // SECTOR_SIZE
    child_sector, run_sector, image_sector = 20, 40, 1270
    run = b"Run <Obey$Dir>.!RunImage\r"
    absolute = _build_absolute_boot()
    run_image = absolute + bytes(
        ((index * 37) ^ (index >> 8) ^ 0xA5) & 0xFF
        for index in range(20 * SECTOR_SIZE - len(absolute)))
    free_extents = ((7, 13), (25, 15), (41, image_sector - 41),
                    (image_sector + 20, total_sectors - image_sector - 20))
    for index, (start, length) in enumerate(free_extents):
        _store_u24(image, index * 3, start)
        _store_u24(image, 256 + index * 3, length)
    image[247:252] = b"Dm\r\r\r"
    image[502:507] = b"eo\r\r\r"
    _store_u24(image, 252, total_sectors)
    image[507:509] = (0xA2B0).to_bytes(2, "little")
    image[509] = 0
    image[510] = len(free_extents) * 3
    image[255] = _old_map_checksum(image[:255])
    image[511] = _old_map_checksum(image[256:511])

    root = _old_directory()
    _old_entry(root, 0, b"!Demo", OLD_DIRECTORY_SIZE, child_sector, True)
    child = _old_directory(b"!Demo")
    _old_entry(child, 0, b"!Run", len(run), run_sector,
               load=0xFFFFEB00)
    _old_entry(child, 1, b"!RunImage", len(run_image), image_sector,
               load=0x00008000)
    _l_write(image, 2, root)
    _l_write(image, child_sector, child)
    _l_write(image, run_sector, run)
    _l_write(image, image_sector, run_image)
    return image


def build_chooser_application_l() -> bytearray:
    """Two invented runnable applications with distinct guest video output."""
    image = bytearray(ADL_SIZE)
    total_sectors = ADL_SIZE // SECTOR_SIZE
    children = (20, 26)
    runs = (40, 42)
    images = (7, 8)
    names = (b"!Nulvane", b"!Qaventra")
    titles = (b"ARCDROID - NULVANE SELECTED",
              b"ARCDROID - QAVENTRA SELECTED")
    colours = (3, 6)
    free_extents = ((9, 11), (25, 1), (31, 9), (41, 1),
                    (43, total_sectors - 43))
    for index, (start, length) in enumerate(free_extents):
        _store_u24(image, index * 3, start)
        _store_u24(image, 256 + index * 3, length)
    _set_old_disc_name(image, b"Other")
    _store_u24(image, 252, total_sectors)
    image[509] = 0
    image[510] = len(free_extents) * 3
    image[255] = _old_map_checksum(image[:255])
    image[511] = _old_map_checksum(image[256:511])
    root = _old_directory()
    for index, name in enumerate(names):
        run = b"Run <Obey$Dir>.!RunImage\r"
        run_image = _chooser_signature_application(titles[index], colours[index])
        _old_entry(root, index, name, OLD_DIRECTORY_SIZE, children[index], True)
        child = _old_directory(name)
        _old_entry(child, 0, b"!Run", len(run), runs[index],
                   load=0xFFFFEB00)
        _old_entry(child, 1, b"!RunImage", len(run_image), images[index],
                   load=0x00008000)
        _l_write(image, children[index], child)
        _l_write(image, runs[index], run)
        _l_write(image, images[index], run_image)
    _l_write(image, 2, root)
    return image


def build_nested_chooser_l(parent_name: bytes) -> bytearray:
    """Move two executable fixture applications one directory below root."""
    image = build_chooser_application_l()
    root = _old_directory()
    parent = _old_directory(parent_name)
    _old_entry(root, 0, parent_name, OLD_DIRECTORY_SIZE, 10, True)
    _old_entry(parent, 0, b"!Nulvane", OLD_DIRECTORY_SIZE, 20, True)
    _old_entry(parent, 1, b"!Qaventra", OLD_DIRECTORY_SIZE, 26, True)
    _l_write(image, 2, root)
    _l_write(image, 10, parent)
    free_extents = ((9, 1), (15, 5), (25, 1), (31, 9),
                    (41, 1), (43, ADL_SIZE // 256 - 43))
    for index, (start, length) in enumerate(free_extents):
        _store_u24(image, index * 3, start)
        _store_u24(image, 256 + index * 3, length)
    image[510] = len(free_extents) * 3
    image[255] = _old_map_checksum(image[:255])
    image[511] = _old_map_checksum(image[256:511])
    return image


def build_nested_chooser_d(parent_name: bytes) -> bytearray:
    """D-format counterpart preserving an extended parent-name byte."""
    image = bytearray(ADF_SIZE)
    total_sectors = ADF_SIZE // 256
    _store_u24(image, 252, total_sectors)
    _set_old_disc_name(image, b"Other")
    image[509] = 0
    root = _new_directory()
    parent_offset = 3072
    _new_entry(root, 0, parent_name, 2048, parent_offset // 256, True)
    _finish_new_directory(root, 1, 4)
    image[1024:3072] = root
    parent = _new_directory(parent_name)
    names = (b"!Nulvane", b"!Qaventra")
    titles = (b"ARCDROID - NULVANE SELECTED",
              b"ARCDROID - QAVENTRA SELECTED")
    for index, name in enumerate(names):
        child_offset = 10240 + index * 2048
        _new_entry(parent, index, name, 2048, child_offset // 256, True)
        child = _new_directory(name)
        run = b"Run <Obey$Dir>.!RunImage\r"
        run_image = _chooser_signature_application(titles[index], (3, 6)[index])
        run_offset = 10240 + len(names) * 2048 + index * 1024
        image_offset = (7 + index) * 1024
        _new_entry(child, 0, b"!Run", len(run), run_offset // 256,
                   load=0xFFFFEB00)
        image[run_offset:run_offset + len(run)] = run
        _new_entry(child, 1, b"!RunImage", len(run_image), image_offset // 256,
                   load=0x00008000)
        image[image_offset:image_offset + len(run_image)] = run_image
        _finish_new_directory(child, 2, parent_offset // 256)
        image[child_offset:child_offset + 2048] = child
    _finish_new_directory(parent, len(names), 4)
    image[parent_offset:parent_offset + 2048] = parent
    free_start = 64
    _store_u24(image, 0, free_start)
    _store_u24(image, 256, total_sectors - free_start)
    image[510] = 3
    image[255] = _old_map_checksum(image[:255])
    image[511] = _old_map_checksum(image[256:511])
    return image


def build_g1_chooser_l() -> bytearray:
    """Boot-option disc with one executable application and no root !Boot."""
    image = build_chooser_application_l()
    root = _old_directory()
    _old_entry(root, 0, b"!Nulvane", OLD_DIRECTORY_SIZE, 20, True)
    _l_write(image, 2, root)
    image[509] = 3
    image[511] = _old_map_checksum(image[256:511])
    return image


def _set_old_disc_name(image: bytearray, name: bytes) -> None:
    padded = name[:10].ljust(10, b"\r")
    image[247:252] = padded[0::2]
    image[502:507] = padded[1::2]


def build_l_discovery(candidates: list[tuple[bytes, bool]],
                      disc_name: bytes = b"Fixture", boot: int = 0,
                      incomplete: int | None = None,
                      boot_children: set[int] | None = None,
                      duplicate_run: int | None = None,
                      duplicate_boot: int | None = None,
                      root_attribute_high: int | None = None,
                      root_boot: str | None = None) -> bytearray:
    image = bytearray(ADL_SIZE)
    _store_u24(image, 252, ADL_SIZE // 256)
    _set_old_disc_name(image, disc_name)
    image[509] = boot
    image[510] = 0
    image[255] = _old_map_checksum(image[:255])
    image[511] = _old_map_checksum(image[256:511])
    root = _old_directory()
    for index, (name, application) in enumerate(candidates):
        child_sector = 10 + index * 6
        _old_entry(root, index, name, OLD_DIRECTORY_SIZE, child_sector, True)
        if root_attribute_high == index:
            root[5 + index * 26 + 6] |= 0x80
            root[5 + index * 26 + 7] |= 0x80
        if incomplete != index:
            child = _old_directory(name)
            child_entry = 0
            if application:
                _old_entry(child, child_entry, b"!Run", 1, 400 + index)
                child_entry += 1
                if duplicate_run == index:
                    _old_entry(child, child_entry, b"!rUN", 1, 400 + index)
                    child_entry += 1
                _l_write(image, 400 + index, b"\r")
            if boot_children and index in boot_children:
                _old_entry(child, child_entry, b"!Boot", 1, 500 + index)
                child_entry += 1
                if duplicate_boot == index:
                    _old_entry(child, child_entry, b"!bOOT", 1, 500 + index)
                _l_write(image, 500 + index, b"\r")
            _l_write(image, child_sector, child)
    if root_boot:
        name = b"!bOOT" if root_boot == "file" else b"!Boot"
        _old_entry(root, len(candidates), name,
                   OLD_DIRECTORY_SIZE if root_boot == "directory" else 1,
                   700, root_boot == "directory")
        if root_boot == "directory":
            _l_write(image, 700, _old_directory(name))
        else:
            _l_write(image, 700, b"\r")
    _l_write(image, 2, root)
    return image


def _new_entry(directory: bytearray, index: int, name: bytes, length: int,
               address: int, directory_object: bool = False,
               load: int = 0xFFF00000) -> None:
    at = 5 + index * 26
    directory[at:at + 10] = name.ljust(10, b"\r")
    exec_address = 0 if load >> 20 == 0xFFF else load
    struct.pack_into("<II", directory, at + 10, load, exec_address)
    struct.pack_into("<I", directory, at + 18, length)
    _store_u24(directory, at + 22, address)
    directory[at + 25] = 0x33 | (0x08 if directory_object else 0)


def _stale_new_name(directory: bytearray, index: int, name: bytes) -> None:
    """Leave formatter-style stale bytes after the first name terminator."""
    at = 5 + index * 26
    remaining = 10 - len(name)
    stale = bytes((0x0D, 0x0A, 0x01, 0xFF)) + b"\r" * 10
    directory[at + len(name):at + 10] = stale[:remaining]


def _new_directory(name: bytes = b"$", signature: bytes = b"Hugo") -> bytearray:
    directory = bytearray(2048)
    directory[0] = 1
    directory[1:5] = signature
    tail = 2007
    directory[tail] = 0
    directory[tail + 3:tail + 6] = (2).to_bytes(3, "little")
    directory[tail + 6:tail + 25] = b"ArcDroid fixture".ljust(19, b"\0")
    directory[tail + 25:tail + 35] = name.ljust(10, b"\r")
    directory[tail + 35] = 1
    directory[tail + 36:tail + 40] = signature
    return directory


def _adfs_name_key(name: bytes) -> bytes:
    """Return FileCore's case-insensitive byte ordering key."""
    return bytes(value + 0x20 if 0x41 <= value <= 0x5A else value
                 for value in name)


def _ror32(value: int, shift: int) -> int:
    return ((value >> shift) | (value << (32 - shift))) & 0xFFFFFFFF


def _finish_new_directory(directory: bytearray, entry_count: int,
                          parent: int) -> None:
    """Set the parent and FileCore new-directory check byte."""
    if not 0 <= entry_count <= 77:
        raise ValueError("new-directory entry count is out of range")
    tail = 2007
    _store_u24(directory, tail + 3, parent)
    used_end = 5 + entry_count * 26
    check = 0
    word_end = used_end & ~3
    for offset in range(0, word_end, 4):
        value = int.from_bytes(directory[offset:offset + 4], "little")
        check = value ^ _ror32(check, 13)
    for offset in range(word_end, used_end):
        check = directory[offset] ^ _ror32(check, 13)
    for offset in range(tail + 1, tail + 37, 4):
        value = int.from_bytes(directory[offset:offset + 4], "little")
        check = value ^ _ror32(check, 13)
    directory[2047] = (check ^ (check >> 8) ^
                       (check >> 16) ^ (check >> 24)) & 0xFF


def build_d_discovery(candidates: list[tuple[bytes, bool]],
                      disc_name: bytes = b"Fixture", boot: int = 0,
                      signature: bytes = b"Hugo",
                      incomplete: int | None = None,
                      declared_sectors: int = ADF_SIZE // 256,
                      stale_names: bool = False,
                      boot_children: set[int] | None = None,
                      duplicate_run: int | None = None,
                      duplicate_boot: int | None = None,
                      root_attribute_high: int | None = None,
                      irrelevant_malformed: bool = False,
                      duplicate_root_file: int | None = None,
                      root_boot: str | None = None) -> bytearray:
    image = bytearray(ADF_SIZE)
    _store_u24(image, 252, declared_sectors)
    _set_old_disc_name(image, disc_name)
    image[509] = boot
    image[510] = 0
    image[255] = _old_map_checksum(image[:255])
    image[511] = _old_map_checksum(image[256:511])
    root = _new_directory(signature=signature)
    for index, (name, application) in enumerate(candidates):
        child_offset = 4096 + index * 2048
        _new_entry(root, index, name, 2048, child_offset // 256, True)
        if root_attribute_high == index:
            root[5 + index * 26 + 25] = 0x88
        if stale_names:
            _stale_new_name(root, index, name)
        if incomplete != index:
            child = _new_directory(name, signature)
            child_entry = 0
            if application:
                run_offset = 65536 + index * 1024
                _new_entry(child, child_entry, b"!Run", 1, run_offset // 256)
                child_entry += 1
                if duplicate_run == index:
                    _new_entry(child, child_entry, b"!rUN", 1,
                               run_offset // 256)
                    child_entry += 1
                if stale_names:
                    _stale_new_name(child, 0, b"!Run")
                image[run_offset] = 13
            if boot_children and index in boot_children:
                boot_offset = 131072 + index * 1024
                _new_entry(child, child_entry, b"!Boot", 1,
                           boot_offset // 256)
                child_entry += 1
                if duplicate_boot == index:
                    _new_entry(child, child_entry, b"!bOOT", 1,
                               boot_offset // 256)
                image[boot_offset] = 13
            image[child_offset:child_offset + 2048] = child
    if irrelevant_malformed:
        _new_entry(root, len(candidates), b"Oddity", 0xFFFFFFFF, 0xFFFFFF)
    if duplicate_root_file is not None:
        _new_entry(root, len(candidates), candidates[duplicate_root_file][0],
                   1, 0xFFFFFF)
    if root_boot:
        at = len(candidates) + (irrelevant_malformed is True) + \
             (duplicate_root_file is not None)
        name = b"!bOOT" if root_boot == "file" else b"!Boot"
        _new_entry(root, at, name,
                   2048 if root_boot == "directory" else 1,
                   700416 // 256, root_boot == "directory")
        if root_boot == "directory":
            image[700416:702464] = _new_directory(name, signature)
        else:
            image[700416] = 13
    image[1024:3072] = root
    return image


def build_nested_d_discovery(
        parents: list[tuple[bytes, list[tuple[bytes, bool, bool]]]],
        disc_name: bytes = b"Fixture", root_files: tuple[bytes, ...] = (),
        unreadable_parent: int | None = None,
        unreadable_child: int | None = None,
        duplicate_parent_file: int | None = None,
        duplicate_child_file: int | None = None) -> bytearray:
    """Invented one-level FileCore hierarchy; offsets never overlap."""
    image = build_d_discovery([], disc_name)
    root = _new_directory()
    next_app = 0
    for parent_index, (parent_name, children) in enumerate(parents):
        parent_offset = 400384 + parent_index * 2048
        _new_entry(root, parent_index, parent_name, 2048,
                   parent_offset // 256, True)
        if unreadable_parent == parent_index:
            continue
        directory = _new_directory(parent_name)
        for child_index, (leaf, application, bootable) in enumerate(children):
            app_offset = 10240 + next_app * 2048
            run_offset = 200704 + next_app * 1024
            boot_offset = 300032 + next_app * 1024
            _new_entry(directory, child_index, leaf, 2048,
                       app_offset // 256, True)
            app = _new_directory(leaf)
            slot = 0
            if application:
                _new_entry(app, slot, b"!Run", 1, run_offset // 256)
                image[run_offset] = 13
                slot += 1
            if bootable:
                _new_entry(app, slot, b"!Boot", 1, boot_offset // 256)
                image[boot_offset] = 13
            if unreadable_child != next_app:
                image[app_offset:app_offset + 2048] = app
            next_app += 1
        if duplicate_child_file == parent_index and children:
            _new_entry(directory, len(children), children[0][0], 1,
                       600064 // 256)
        image[parent_offset:parent_offset + 2048] = directory
    root_at = len(parents)
    for filename in root_files:
        _new_entry(root, root_at, filename, 1, 600064 // 256)
        root_at += 1
    if duplicate_parent_file is not None:
        _new_entry(root, root_at, parents[duplicate_parent_file][0], 1,
                   600064 // 256)
    image[1024:3072] = root
    return image


def build_nested_with_root_d(root_name: bytes, state: str) -> bytearray:
    """A root ! directory suppresses descent into an ordinary folder."""
    image = build_nested_d_discovery([
        (b"Folder", [(b"!Nested", True, False)])])
    root = bytearray(image[1024:3072])
    _new_entry(root, 1, root_name, 1 if state == "unknown" else 2048,
               700416 // 256, True)
    image[1024:3072] = root
    if state != "unknown":
        child = _new_directory(root_name)
        if state == "application":
            _new_entry(child, 0, b"!Run", 1, 702464 // 256)
            image[702464] = 13
        image[700416:702464] = child
    return image


def build_depth_two_only_d() -> bytearray:
    image = build_nested_d_discovery([
        (b"Folder", [(b"Sub", False, False)])])
    sub = _new_directory(b"Sub")
    _new_entry(sub, 0, b"!Deep", 2048, 600064 // 256, True)
    image[10240:12288] = sub
    image[600064:602112] = _new_directory(b"!Deep")
    return image


def _set_bits(image: bytearray, bit: int, count: int, value: int) -> None:
    for index in range(count):
        if value & (1 << index):
            image[(bit + index) // 8] |= 1 << ((bit + index) & 7)


def build_e_discovery(candidates: list[tuple[bytes, bool]],
                      disc_name: bytes = b"Fixture", boot: int = 0,
                      signature: bytes = b"Hugo", fragmented: int | None = None,
                      unresolved: int | None = None, stale_names: bool = False,
                      late_target: bool = False,
                      boot_children: set[int] | None = None,
                      duplicate_run: int | None = None,
                      duplicate_boot: int | None = None,
                      root_attribute_high: int | None = None,
                      irrelevant_malformed: bool = False,
                      root_boot: str | None = None) -> bytearray:
    image = bytearray(ADF_SIZE)
    record = memoryview(image)[4:64]
    record[0:6] = bytes((10, 5, 2, 2, 15, 7))
    record[7] = boot
    record[9] = 1
    record[10:12] = (0x0520).to_bytes(2, "little")
    record[12:16] = (0x00000203).to_bytes(4, "little")
    record[16:20] = ADF_SIZE.to_bytes(4, "little")
    record[20:22] = (0x2B00).to_bytes(2, "little")
    record[22:32] = disc_name[:10].ljust(10, b"\r")
    image[3] = 0xFF

    # Fragment 2 contains map + root (four sectors). Each later 16-map-bit
    # fragment occupies two 1024-byte sectors. A late-target fixture inserts
    # one large allocated filler so its application directory lands beyond
    # the parser's former bit-6432 cutoff. The final fragment is free.
    bit = 512
    _set_bits(image, bit, 15, 2); _set_bits(image, bit + 31, 1, 1)
    bit += 32
    if late_target:
        _set_bits(image, bit, 15, 0x123)
        _set_bits(image, 6447, 1, 1)
        bit = 6448
    allocations: list[tuple[int, int]] = []
    for index, _candidate in enumerate(candidates):
        child_id = 3 + index * 2
        _set_bits(image, bit, 15, child_id); _set_bits(image, bit + 15, 1, 1)
        child_offset = (bit - 512) * 128
        bit += 16
        run_id = child_id + 1
        if fragmented == index:
            _set_bits(image, bit, 15, run_id); _set_bits(image, bit + 15, 1, 1)
            run_offset = (bit - 512) * 128
            bit += 16
            _set_bits(image, bit, 15, 100 + index); _set_bits(image, bit + 15, 1, 1)
            bit += 16
            _set_bits(image, bit, 15, run_id); _set_bits(image, bit + 15, 1, 1)
            bit += 16
        else:
            _set_bits(image, bit, 15, run_id); _set_bits(image, bit + 15, 1, 1)
            run_offset = (bit - 512) * 128
            bit += 16
        allocations.append((child_offset, run_offset))
    _set_bits(image, 8, 15, bit - 8)
    _set_bits(image, bit, 15, 0)
    _set_bits(image, 6911, 1, 1)

    root = _new_directory(signature=signature)
    for index, (name, application) in enumerate(candidates):
        child_id = 3 + index * 2
        address = 0x7F00 if unresolved == index else child_id << 8
        _new_entry(root, index, name, 2048, address, True)
        if root_attribute_high == index:
            root[5 + index * 26 + 25] = 0x88
        if stale_names:
            _stale_new_name(root, index, name)
        child_offset, run_offset = allocations[index]
        child = _new_directory(name, signature)
        child_entry = 0
        if application:
            run_id = child_id + 1
            run_length = 3072 if fragmented == index else 1
            _new_entry(child, child_entry, b"!Run", run_length, run_id << 8)
            child_entry += 1
            if duplicate_run == index:
                _new_entry(child, child_entry, b"!rUN", run_length,
                           run_id << 8)
                child_entry += 1
            if stale_names:
                _stale_new_name(child, 0, b"!Run")
            image[run_offset] = 13
        if boot_children and index in boot_children:
            _new_entry(child, child_entry, b"!Boot", 1,
                       (3 + index * 2 + 1) << 8)
            child_entry += 1
            if duplicate_boot == index:
                _new_entry(child, child_entry, b"!bOOT", 1,
                           (3 + index * 2 + 1) << 8)
        image[child_offset:child_offset + 2048] = child
    if irrelevant_malformed:
        _new_entry(root, len(candidates), b"Oddity", 0xFFFFFFFF, 0xFFFFFF)
    if root_boot:
        at = len(candidates) + (irrelevant_malformed is True)
        _new_entry(root, at, b"!bOOT" if root_boot == "file" else b"!Boot",
                   2048 if root_boot == "directory" else 1,
                   0x7f00, root_boot == "directory")
    image[2048:4096] = root
    image[0] = 0
    sums = [0, 0, 0, 0]
    rover = 1020
    while rover > 0:
        sums[0] += image[rover] + (sums[3] >> 8); sums[3] &= 0xFF
        sums[1] += image[rover + 1] + (sums[0] >> 8); sums[0] &= 0xFF
        sums[2] += image[rover + 2] + (sums[1] >> 8); sums[1] &= 0xFF
        sums[3] += image[rover + 3] + (sums[2] >> 8); sums[2] &= 0xFF
        rover -= 4
    sums[0] += sums[3] >> 8
    sums[1] += image[1] + (sums[0] >> 8)
    sums[2] += image[2] + (sums[1] >> 8)
    sums[3] += image[3] + (sums[2] >> 8)
    image[0] = (sums[0] ^ sums[1] ^ sums[2] ^ sums[3]) & 0xFF
    return image


def build_nested_e_discovery(parent_name: bytes) -> bytearray:
    """New-map nested application reusing allocated fragments 3 and 5."""
    image = build_e_discovery([(b"Folder", False), (b"!Demo", True)], b"Demo")
    root = bytearray(image[2048:4096])
    _new_entry(root, 0, parent_name, 2048, 3 << 8, True)
    root[31:57] = bytes(26)
    image[2048:4096] = root
    parent = _new_directory(parent_name)
    _new_entry(parent, 0, b"!Demo", 2048, 5 << 8, True)
    image[4096:6144] = parent
    return image


def _build_jfd_disk(payload: bytes, geometry: str,
                    title: bytes = b"ArcDroid application fixture",
                    extended: bool = False) -> bytearray:
    sector_size, sectors = (256, 16) if geometry == "L" else (1024, 5)
    track_count = 160
    track_offset = 316 if extended else 304
    sector_offset = track_offset + track_count * 4
    tables = bytearray()
    track_offsets = []
    data = bytearray()
    for track_side in range(track_count):
        track_offsets.append(len(tables))
        for sector_id in range(sectors):
            physical = (track_side * sectors + sector_id) * sector_size
            header = (2 << 16) | (sector_id << 8) | {256: 1, 1024: 3}[sector_size]
            tables += struct.pack("<II", header, len(data))
            data += payload[physical:physical + sector_size]
        tables += struct.pack("<I", 0xFFFFFFFF)
    data_offset = sector_offset + len(tables)
    image = bytearray(data_offset + len(data))
    image[:4] = b"JFDI"
    struct.pack_into("<I", image, 8, len(image))
    struct.pack_into("<III", image, 24, track_offset, sector_offset, data_offset)
    image[48:48 + min(len(title), 256)] = title[:256]
    if extended:
        struct.pack_into("<I", image, 304, 0)
    for index, relative in enumerate(track_offsets):
        struct.pack_into("<I", image, track_offset + index * 4, relative)
    image[sector_offset:data_offset] = tables
    image[data_offset:] = data
    return image


def build_pdd5_jfd(payload: bytes, tracks: dict[int, list[int]],
                   times: dict[int, dict[int, int]] | None = None,
                   track_count: int = 160,
                   title: bytes = b"ArcDroid P-DD5 creation fixture") -> bytearray:
    """Project-owned JFD with an explicit captured subset on each track-side."""
    times = times or {}
    track_offset = 316
    sector_offset = track_offset + track_count * 4
    table = bytearray()
    data = bytearray()
    starts = []
    for track_side in range(track_count):
        starts.append(len(table))
        for sector_id in tracks.get(track_side, []):
            time = times.get(track_side, {}).get(sector_id, 0xFF)
            header = (time << 24) | (2 << 16) | (sector_id << 8) | 3
            table += struct.pack("<II", header, len(data))
            logical = (track_side * 5 + sector_id) * 1024
            segment = payload[logical:logical + 1024] if sector_id < 5 \
                else b""
            data += segment if len(segment) == 1024 else \
                bytes((sector_id,)) * 1024
        table += struct.pack("<I", 0xFFFFFFFF)
    data_offset = sector_offset + len(table)
    image = bytearray(data_offset + len(data))
    image[:4] = b"JFDI"
    struct.pack_into("<I", image, 8, len(image))
    struct.pack_into("<III", image, 24, track_offset, sector_offset, data_offset)
    image[48:48 + min(len(title), 256)] = title[:256]
    struct.pack_into("<I", image, 304, 2)
    for index, start in enumerate(starts):
        struct.pack_into("<I", image, track_offset + index * 4, start)
    image[sector_offset:data_offset] = table
    image[data_offset:] = data
    return image


def build_file_data_fixtures(output: Path) -> None:
    """Invented FileCore images for the captured-only R1' proof."""
    output.mkdir(parents=True, exist_ok=True)
    full = list(range(5))
    d = build_d_discovery([], b"Invented")
    root = _new_directory()
    _new_entry(root, 0, b"Payload", 1100, (5120 + 256) // 256)
    _finish_new_directory(root, 1, 0)
    d[1024:3072] = root
    _store_u24(d, 0, 40)
    _store_u24(d, 256, 3160)
    d[510] = 3
    d[255] = _old_map_checksum(d[:255])
    d[511] = _old_map_checksum(d[256:511])
    tracks = {0: full, 1: [0, 2, 3, 4]}
    def protected(payload: bytes, captured: dict[int, list[int]]) -> bytes:
        jfd = build_pdd5_jfd(payload, captured)
        struct.pack_into("<I", jfd, 304, 1)  # Header's write-protect flag.
        return bytes(jfd)

    terminal_jfd = protected(bytes(d), tracks)
    (output / "d-terminal.jfd").write_bytes(terminal_jfd)
    recovery = bytearray(d)
    recovery_root = bytearray(recovery[1024:3072])
    _new_entry(recovery_root, 1, b"!Demo", 2048, 8192 // 256, True)
    _new_entry(recovery_root, 2, b"!Boot", 1, 3072 // 256)
    _finish_new_directory(recovery_root, 3, 0)
    recovery[1024:3072] = recovery_root
    recovery_child = _new_directory(b"!Demo")
    _new_entry(recovery_child, 0, b"!Run", 1, 4096 // 256)
    _finish_new_directory(recovery_child, 1, 2 << 8)
    recovery[8192:10240] = recovery_child
    recovery[4096] = 13
    recovery[3072] = 13
    recovery[509] = 3
    recovery[511] = _old_map_checksum(recovery[256:511])
    (output / "d-recovery.jfd").write_bytes(protected(
        bytes(recovery), tracks))
    (output / "other-format.jfd").write_bytes(protected(
        bytes(819200), tracks))
    writable_terminal = bytearray(terminal_jfd)
    struct.pack_into("<I", writable_terminal, 304, 2)
    (output / "d-terminal-rw.jfd").write_bytes(writable_terminal)
    wp_rw = bytearray(writable_terminal)
    struct.pack_into("<I", wp_rw, 304, 3)
    (output / "d-terminal-wp-rw.jfd").write_bytes(wp_rw)
    layout_unsupported = build_pdd5_jfd(bytes(d), tracks, track_count=159)
    struct.pack_into("<I", layout_unsupported, 304, 2)
    (output / "d-layout-unsupported-rw.jfd").write_bytes(layout_unsupported)
    (output / "mixed.m3u").write_text("d-terminal.jfd\nd-terminal-rw.jfd\n")
    crc_sector = bytearray(terminal_jfd)
    crc_at = pdd5_descriptor_offset(crc_sector, 0, 4)
    crc_header = struct.unpack_from("<I", crc_sector, crc_at)[0]
    struct.pack_into("<I", crc_sector, crc_at, crc_header | (2 << 4))
    (output / "d-captured-data-crc.jfd").write_bytes(crc_sector)
    missing_sector = bytearray(terminal_jfd)
    struct.pack_into("<I", missing_sector,
                     pdd5_descriptor_offset(missing_sector, 0, 4) + 4,
                     0xffffffff)
    (output / "d-captured-missing-data.jfd").write_bytes(missing_sector)
    terminal_166 = build_pdd5_jfd(bytes(d), tracks, track_count=166)
    struct.pack_into("<I", terminal_166, 304, 1)
    (output / "d-terminal-166.jfd").write_bytes(terminal_166)
    (output / "d-terminal-gzip.jfd").write_bytes(gzip.compress(terminal_jfd, mtime=0))
    with zipfile.ZipFile(output / "d-terminal.zip", "w") as archive:
        archive.writestr("Invented.jfd", terminal_jfd,
                         compress_type=zipfile.ZIP_STORED)
    (output / "d-slack.jfd").write_bytes(protected(
        bytes(d), {0: full, 1: [0, 2, 4]}))
    bad_root = bytearray(d)
    bad_root[1024 + 2047] ^= 1
    (output / "d-root-invalid.jfd").write_bytes(protected(bytes(bad_root), tracks))
    bad_sequence = bytearray(d)
    sequence_root = bytearray(bad_sequence[1024:3072])
    sequence_root[2042] ^= 1
    _finish_new_directory(sequence_root, 1, 0)
    bad_sequence[1024:3072] = sequence_root
    (output / "d-sequence-invalid.jfd").write_bytes(protected(
        bytes(bad_sequence), tracks))
    bad_marker = bytearray(d)
    marker_root = bytearray(bad_marker[1024:3072])
    marker_root[1:5] = b"Bork"
    marker_root[2043:2047] = b"Bork"
    _finish_new_directory(marker_root, 1, 0)
    bad_marker[1024:3072] = marker_root
    (output / "d-marker-invalid.jfd").write_bytes(protected(
        bytes(bad_marker), tracks))
    nick = bytearray(d)
    nick_root = _new_directory(signature=b"Nick")
    _new_entry(nick_root, 0, b"Payload", 1100, (5120 + 256) // 256)
    _finish_new_directory(nick_root, 1, 0)
    nick[1024:3072] = nick_root
    (output / "d-nick-valid.jfd").write_bytes(protected(bytes(nick), tracks))
    nick_root[1:5] = b"Nock"
    nick_root[2043:2047] = b"Nock"
    _finish_new_directory(nick_root, 1, 0)
    nick[1024:3072] = nick_root
    (output / "d-nick-invalid.jfd").write_bytes(protected(bytes(nick), tracks))
    bad_map = bytearray(d)
    bad_map[255] ^= 1
    (output / "d-map-first-check.jfd").write_bytes(protected(bytes(bad_map), tracks))
    (output / "d-no-valid-map.jfd").write_bytes(protected(bytes(bad_map), tracks))
    bad_map_second = bytearray(d)
    bad_map_second[511] ^= 1
    (output / "d-map-second-check.jfd").write_bytes(protected(
        bytes(bad_map_second), tracks))
    zero = bytearray(d)
    _new_entry(root, 0, b"Payload", 0, (5120 + 256) // 256)
    _finish_new_directory(root, 1, 0)
    zero[1024:3072] = root
    (output / "d-zero-length.jfd").write_bytes(protected(bytes(zero), tracks))
    two_zero = bytearray(zero)
    _new_entry(root, 1, b"Empty", 0, (5120 + 256) // 256)
    _finish_new_directory(root, 2, 0)
    two_zero[1024:3072] = root
    (output / "d-two-empty.jfd").write_bytes(protected(bytes(two_zero), tracks))
    boundary = bytearray(d)
    root = _new_directory()
    _new_entry(root, 0, b"Payload", 1024, 5120 // 256)
    _finish_new_directory(root, 1, 0)
    boundary[1024:3072] = root
    (output / "d-boundary.jfd").write_bytes(protected(bytes(boundary), tracks))
    tiny = bytearray(d)
    root = _new_directory()
    _new_entry(root, 0, b"Payload", 1, 3072 // 256)
    _finish_new_directory(root, 1, 0)
    tiny[1024:3072] = root
    (output / "d-one-byte.jfd").write_bytes(protected(
        bytes(tiny), {0: [0, 1, 2, 4]}))
    invalid = bytearray(d)
    root = _new_directory()
    _new_entry(root, 0, b"Payload", 1, ADF_SIZE // 256)
    _finish_new_directory(root, 1, 0)
    invalid[1024:3072] = root
    (output / "d-invalid-object.jfd").write_bytes(protected(bytes(invalid), tracks))
    outside_declared = bytearray(d)
    _store_u24(outside_declared, 252, 100)
    _store_u24(outside_declared, 256, 60)
    outside_root = _new_directory()
    _new_entry(outside_root, 0, b"Payload", 1, 101)
    _finish_new_directory(outside_root, 1, 0)
    outside_declared[1024:3072] = outside_root
    outside_declared[255] = _old_map_checksum(outside_declared[:255])
    outside_declared[511] = _old_map_checksum(outside_declared[256:511])
    (output / "d-outside-declared.jfd").write_bytes(protected(
        bytes(outside_declared), tracks))
    free_overlap = bytearray(d)
    _store_u24(free_overlap, 0, 21)
    _store_u24(free_overlap, 3, 40)
    _store_u24(free_overlap, 256, 1)
    _store_u24(free_overlap, 259, 3160)
    free_overlap[510] = 6
    free_overlap[255] = _old_map_checksum(free_overlap[:255])
    free_overlap[511] = _old_map_checksum(free_overlap[256:511])
    (output / "d-free-overlap.jfd").write_bytes(protected(
        bytes(free_overlap), tracks))
    cycle = bytearray(d)
    root = _new_directory()
    _new_entry(root, 0, b"Again", 2048, 4, True)
    _finish_new_directory(root, 1, 0)
    cycle[1024:3072] = root
    (output / "d-cycle.jfd").write_bytes(protected(bytes(cycle), tracks))
    for name, second_address in (("alias", (5120 + 256) // 256),
                                 ("overlap", (5120 + 512) // 256)):
        duplicate = bytearray(d)
        root = _new_directory()
        _new_entry(root, 0, b"First", 512, (5120 + 256) // 256)
        _new_entry(root, 1, b"Second", 512, second_address)
        _finish_new_directory(root, 2, 0)
        duplicate[1024:3072] = root
        (output / f"d-{name}.jfd").write_bytes(protected(
            bytes(duplicate), tracks))
    child = bytearray(d)
    root = _new_directory()
    _new_entry(root, 0, b"Child", 2048, 5120 // 256, True)
    _finish_new_directory(root, 1, 0)
    child[1024:3072] = root
    (output / "d-child-incomplete.jfd").write_bytes(protected(bytes(child), tracks))
    bad_child = bytearray(child)
    live_child = _new_directory(b"Child")
    _new_entry(live_child, 0, b"Descendant", 1, 8192 // 256)
    _finish_new_directory(live_child, 1, 4)
    live_child[2047] ^= 1
    bad_child[5120:7168] = live_child
    (output / "d-child-bad-check.jfd").write_bytes(protected(
        bytes(bad_child), {0: full, 1: [0, 1, 2, 3, 4], 2: [0, 1, 2, 3, 4]}))
    (output / "d-child-bad-check-omitted.jfd").write_bytes(protected(
        bytes(bad_child), {0: full, 1: [0, 2, 3, 4], 2: [0, 1, 2, 3, 4]}))
    small = bytearray(d)
    _store_u24(small, 252, 100)
    _store_u24(small, 256, 60)
    small[255] = _old_map_checksum(small[:255])
    small[511] = _old_map_checksum(small[256:511])
    (output / "d-small-declaration.jfd").write_bytes(protected(bytes(small), tracks))
    large = bytearray(d)
    _store_u24(large, 252, 3300)
    _store_u24(large, 256, 3260)
    large[255] = _old_map_checksum(large[:255])
    large[511] = _old_map_checksum(large[256:511])
    (output / "d-clamped-declaration.jfd").write_bytes(protected(bytes(large), tracks))
    (output / "d-irregular.jfd").write_bytes(protected(
        bytes(d), {0: full, 1: [0, 2, 3, 4, 5]}))
    for depth in (16, 17):
        chain = bytearray(d)
        _store_u24(chain, 0, 200)
        _store_u24(chain, 256, 3000)
        chain[255] = _old_map_checksum(chain[:255])
        chain[511] = _old_map_checksum(chain[256:511])
        for level in range(depth):
            directory = _new_directory()
            next_byte = 1024 + (level + 1) * 2048
            if level + 1 < depth:
                _new_entry(directory, 0, b"Child", 2048,
                           next_byte // 256, True)
            else:
                _new_entry(directory, 0, b"Payload", 1,
                           next_byte // 256)
            _finish_new_directory(directory, 1, 0)
            at = 1024 + level * 2048
            chain[at:at + 2048] = directory
        final_sector = next_byte // 1024
        captured = {side: full.copy() for side in range(final_sector // 5 + 1)}
        captured[final_sector // 5].remove(final_sector % 5)
        (output / f"d-depth-{depth}.jfd").write_bytes(protected(
            bytes(chain), captured))
    e = build_e_discovery([(b"Folder", True)], b"Invented")
    child = bytearray(e[4096:6144])
    _new_entry(child, 0, b"!Run", 1300, 4 << 8)
    _finish_new_directory(child, 1, 2 << 8)
    e[4096:6144] = child
    root = bytearray(e[2048:4096])
    _finish_new_directory(root, 1, 2 << 8)
    e[2048:4096] = root
    e[1024:2048] = e[:1024]
    tracks = {0: full, 1: [0, 1, 3, 4]}
    (output / "e-terminal.jfd").write_bytes(protected(bytes(e), tracks))
    (output / "e-mixed.jfd").write_bytes(protected(
        bytes(e), {0: full, 1: [0, 1, 4]}))
    fragmented = build_e_discovery([(b"Folder", True)], b"Invented",
                                   fragmented=0)
    child = bytearray(fragmented[4096:6144])
    _finish_new_directory(child, 1, 2 << 8)
    fragmented[4096:6144] = child
    root = bytearray(fragmented[2048:4096])
    _finish_new_directory(root, 1, 2 << 8)
    fragmented[2048:4096] = root
    fragmented[1024:2048] = fragmented[:1024]
    (output / "e-fragmented.jfd").write_bytes(protected(
        bytes(fragmented), {0: full, 1: [0, 1, 3, 4], 2: [1, 2, 3, 4]}))
    shared = bytearray(e)
    child = bytearray(shared[4096:6144])
    _new_entry(child, 0, b"!Run", 1, (4 << 8) | 2)
    _finish_new_directory(child, 1, 2 << 8)
    shared[4096:6144] = child
    (output / "e-shared.jfd").write_bytes(protected(bytes(shared), tracks))
    system_shared = bytearray(e)
    block = bytearray(system_shared[:1024])
    for bit in range(8, 23):
        block[bit // 8] &= ~(1 << (bit & 7))
    for bit in range(512, 6912):
        block[bit // 8] &= ~(1 << (bit & 7))
    _set_bits(block, 512, 15, 2); _set_bits(block, 559, 1, 1)
    _set_bits(block, 560, 15, 3); _set_bits(block, 575, 1, 1)
    _set_bits(block, 576, 15, 4); _set_bits(block, 591, 1, 1)
    _set_bits(block, 8, 15, 592 - 8)
    _set_bits(block, 6911, 1, 1)
    block[0] = _zone_check(block)
    system_shared[:1024] = block
    system_shared[1024:2048] = block
    child = _new_directory(b"Folder")
    _new_entry(child, 0, b"!Run", 1, (2 << 8) | 5)
    _finish_new_directory(child, 1, 2 << 8)
    system_shared[6144:8192] = child
    (output / "e-system-shared.jfd").write_bytes(protected(
        bytes(system_shared), {0: [0, 1, 2, 3], 1: [1, 2]}))
    conflict = bytearray(e)
    # A structurally valid second map copy with a different root identity.
    backup = bytearray(conflict[1024:2048])
    backup[4 + 12] ^= 1
    backup[0] = _zone_check(backup)
    conflict[1024:2048] = backup
    (output / "e-conflict.jfd").write_bytes(protected(bytes(conflict), tracks))
    object_conflict = bytearray(e)
    backup = bytearray(object_conflict[1024:2048])
    for bit in range(560, 575):
        backup[bit // 8] &= ~(1 << (bit & 7))
    _set_bits(backup, 560, 15, 5)
    # Keep both copies structurally valid and both walks successful. The
    # second copy resolves the same live file ID one fragment later.
    for bit in range(8, 23):
        backup[bit // 8] &= ~(1 << (bit & 7))
    _set_bits(backup, 8, 15, 592 - 8)
    _set_bits(backup, 576, 15, 4)
    _set_bits(backup, 591, 1, 1)
    backup[0] = _zone_check(backup)
    object_conflict[1024:2048] = backup
    (output / "e-object-conflict.jfd").write_bytes(protected(
        bytes(object_conflict), tracks))
    invalid_backup = bytearray(e)
    invalid_backup[1024] ^= 1
    (output / "e-one-copy.jfd").write_bytes(protected(
        bytes(invalid_backup), tracks))
    (output / "e-zone-check-backup.jfd").write_bytes(protected(
        bytes(invalid_backup), tracks))
    backup_only = bytearray(e)
    backup_only[0] ^= 1
    (output / "e-backup-only.jfd").write_bytes(protected(
        bytes(backup_only), tracks))
    (output / "e-zone-check-primary.jfd").write_bytes(protected(
        bytes(backup_only), tracks))
    # QA remediation: two map copies can resolve the same object step starts
    # with different physical lengths. The complete ordered extents disagree.
    def extent_map(first_bits: int, gap_bits: int, last_bits: int) -> bytearray:
        block = bytearray(1024)
        rec = memoryview(block)[4:64]
        rec[:6] = bytes((10, 5, 2, 2, 13, 7))
        rec[9] = 1
        rec[10:12] = (1312).to_bytes(2, "little")
        rec[12:16] = (0x203).to_bytes(4, "little")
        rec[16:20] = ADF_SIZE.to_bytes(4, "little")
        block[3] = 0xff
        pieces = ((0, 32, 2), (32, 124, 5), (156, first_bits, 3),
                  (156 + first_bits, gap_bits, 6), (239, last_bits, 3))
        for relative, width, fragment_id in pieces:
            start = 512 + relative
            _set_bits(block, start, 13, fragment_id)
            _set_bits(block, start + width - 1, 1, 1)
        free_at = 239 + last_bits
        _set_bits(block, 512 + free_at, 13, 0)
        _set_bits(block, 6911, 1, 1)
        _set_bits(block, 8, 13, 512 + free_at - 8)
        block[0] = _zone_check(block)
        return block

    agree = bytearray(ADF_SIZE)
    map_a = extent_map(46, 37, 19)
    map_b = extent_map(44, 39, 21)
    agree[:1024] = map_a
    agree[1024:2048] = map_b
    agree_root = _new_directory()
    _new_entry(agree_root, 0, b"F", 6035, 3 << 8)
    _finish_new_directory(agree_root, 1, 0x203)
    agree[2048:4096] = agree_root
    complete = {side: full.copy() for side in range(160)}
    complete[5].remove(0)
    (output / "e-agree-length-conflict.jfd").write_bytes(protected(bytes(agree), complete))
    agree[1024:2048] = map_a
    (output / "e-agree-length-control.jfd").write_bytes(protected(bytes(agree), complete))

    # Both copies parse, but one cannot walk the child directory. Exercise
    # each copy as the primary to cover both failure orders.
    bad_walk = bytearray(e)
    bad_block = bytearray(bad_walk[1024:2048])
    for bit in range(544, 559):
        bad_block[bit // 8] &= ~(1 << (bit & 7))
    _set_bits(bad_block, 544, 15, 6)
    bad_block[0] = _zone_check(bad_block)
    bad_walk[1024:2048] = bad_block
    (output / "e-second-walk-fails.jfd").write_bytes(protected(bytes(bad_walk), tracks))
    bad_walk[:1024], bad_walk[1024:2048] = bad_walk[1024:2048], bad_walk[:1024]
    (output / "e-first-walk-fails.jfd").write_bytes(protected(bytes(bad_walk), tracks))

    # An ordinary 190-file E disc, each file 4 KiB, exercises bounded
    # resolution work near the physical capacity of an 800 KiB disc.
    many = bytearray(ADF_SIZE)
    rec = memoryview(many)[4:64]
    rec[:6] = bytes((10, 5, 2, 2, 15, 7))
    rec[9] = 1
    rec[10:12] = (0x520).to_bytes(2, "little")
    rec[12:16] = (0x203).to_bytes(4, "little")
    rec[16:20] = ADF_SIZE.to_bytes(4, "little")
    many[3] = 0xff
    bit = 512
    def many_frag(fragment_id: int, width: int) -> int:
        nonlocal bit
        _set_bits(many, bit, 15, fragment_id)
        _set_bits(many, bit + width - 1, 1, 1)
        physical = (bit - 512) * 128
        bit += width
        return physical
    many_frag(2, 32)
    dirs = [many_frag(3 + k, 16) for k in range(3)]
    files = [many_frag(6 + k, 32) for k in range(190)]
    _set_bits(many, bit, 15, 0)
    _set_bits(many, 6911, 1, 1)
    _set_bits(many, 8, 15, bit - 8)
    many_root = _new_directory()
    for k, physical in enumerate(dirs):
        _new_entry(many_root, k, f"Dir{k}".encode(), 2048, (3 + k) << 8, True)
        sub = _new_directory(f"Dir{k}".encode())
        indexes = list(range(k * 64, min(190, (k + 1) * 64)))
        for j, index in enumerate(indexes):
            _new_entry(sub, j, f"F{index:03d}".encode(), 4096,
                       (6 + index) << 8)
        _finish_new_directory(sub, len(indexes), 0x203)
        many[physical:physical + 2048] = sub
    _finish_new_directory(many_root, 3, 0x203)
    many[2048:4096] = many_root
    many[0] = _zone_check(many[:1024])
    many[1024:2048] = many[:1024]
    last_sector = (files[-1] + 4095) // 1024
    many_tracks = {side: full.copy() for side in range(160)}
    many_tracks[last_sector // 5].remove(last_sector % 5)
    (output / "e-many-190.jfd").write_bytes(protected(bytes(many), many_tracks))

    # A dense directory tree makes the object limit reachable even on 800 KiB.
    def object_limit_fixture(total: int) -> bytes:
        disc = build_d_discovery([], b"ObjLimit")
        _store_u24(disc, 0, 3000)
        _store_u24(disc, 256, 200)
        disc[510] = 3
        disc[255] = _old_map_checksum(disc[:255])
        disc[511] = _old_map_checksum(disc[256:511])
        remaining = total - 2  # Root and one omitted payload object.
        next_address = 8192
        root_dir = _new_directory()
        _new_entry(root_dir, 0, b"Payload", 1100, (5120 + 256) // 256)
        root_count = 1
        while remaining:
            mid_address = next_address; next_address += 2048; remaining -= 1
            mid = _new_directory(f"M{root_count:02d}".encode())
            mid_count = 0
            while remaining and mid_count < 77:
                leaf_address = next_address; next_address += 2048; remaining -= 1
                leaf = _new_directory(f"L{mid_count:02d}".encode())
                leaf_count = min(77, remaining)
                remaining -= leaf_count
                for index in range(leaf_count):
                    _new_entry(leaf, index, f"Z{index:02d}".encode(), 0, 0)
                _finish_new_directory(leaf, leaf_count, mid_address // 256)
                disc[leaf_address:leaf_address + 2048] = leaf
                _new_entry(mid, mid_count, f"L{mid_count:02d}".encode(),
                           2048, leaf_address // 256, True)
                mid_count += 1
            _finish_new_directory(mid, mid_count, 4)
            disc[mid_address:mid_address + 2048] = mid
            _new_entry(root_dir, root_count, f"M{root_count:02d}".encode(),
                       2048, mid_address // 256, True)
            root_count += 1
        _finish_new_directory(root_dir, root_count, 0)
        disc[1024:3072] = root_dir
        assert next_address < 3000 * 256
        return protected(bytes(disc), {1: [0, 2, 3, 4],
                                       **{side: full.copy() for side in range(160) if side != 1}})
    for count in (8192, 8193):
        (output / f"d-objects-{count}.jfd").write_bytes(object_limit_fixture(count))

    # Captured child plus omitted descendant, old-map sector zero claim, and
    # track-zero file/free omissions are separate proof boundaries.
    child = bytearray(d)
    child_root = _new_directory()
    _new_entry(child_root, 0, b"Child", 2048, 5120 // 256, True)
    _finish_new_directory(child_root, 1, 0)
    child[1024:3072] = child_root
    child_dir = _new_directory(b"Child")
    _new_entry(child_dir, 0, b"Desc", 1000, 8192 // 256)
    _finish_new_directory(child_dir, 1, 4)
    child[5120:7168] = child_dir
    child_tracks = {side: full.copy() for side in range(160)}
    child_tracks[1].remove(3)
    (output / "d-valid-child-desc-omitted.jfd").write_bytes(protected(bytes(child), child_tracks))
    child[5120 + 2047] ^= 1
    (output / "d-bad-child-desc-omitted.jfd").write_bytes(protected(bytes(child), child_tracks))
    in_map = bytearray(d)
    in_map_root = _new_directory()
    _new_entry(in_map_root, 0, b"MapTail", 256, 512 // 256)
    _new_entry(in_map_root, 1, b"Payload", 1100, (5120 + 256) // 256)
    _finish_new_directory(in_map_root, 2, 0)
    in_map[1024:3072] = in_map_root
    (output / "d-object-in-map-sector.jfd").write_bytes(protected(bytes(in_map), tracks))
    track_zero = bytearray(d)
    _store_u24(track_zero, 0, 16)
    _store_u24(track_zero, 256, 3184)
    track_zero[255] = _old_map_checksum(track_zero[:255])
    track_zero[511] = _old_map_checksum(track_zero[256:511])
    track_root = _new_directory()
    _new_entry(track_root, 0, b"Tiny", 1, 3072 // 256)
    _finish_new_directory(track_root, 1, 0)
    track_zero[1024:3072] = track_root
    (output / "d-track0-file-free.jfd").write_bytes(protected(
        bytes(track_zero), {0: [0, 1, 2]}))
    (output / "all-proof-fixtures.txt").write_text("".join(
        path.name + "\n" for path in sorted(output.glob("*.jfd"))))


def pdd5_descriptor_offset(image: bytearray, track_side: int,
                           sector_id: int) -> int:
    track_offset, sector_offset = struct.unpack_from("<II", image, 24)
    relative = struct.unpack_from("<I", image, track_offset + track_side * 4)[0]
    cursor = sector_offset + relative
    while struct.unpack_from("<I", image, cursor)[0] != 0xFFFFFFFF:
        header = struct.unpack_from("<I", image, cursor)[0]
        if ((header >> 8) & 0xFF) == sector_id:
            return cursor
        cursor += 8
    raise ValueError("P-DD5 descriptor is absent")


def _zone_check(block: bytearray) -> int:
    sums = [0, 0, 0, 0]
    rover = 1020
    while rover > 0:
        sums[0] += block[rover] + (sums[3] >> 8); sums[3] &= 0xff
        sums[1] += block[rover + 1] + (sums[0] >> 8); sums[0] &= 0xff
        sums[2] += block[rover + 2] + (sums[1] >> 8); sums[1] &= 0xff
        sums[3] += block[rover + 3] + (sums[2] >> 8); sums[2] &= 0xff
        rover -= 4
    sums[0] += sums[3] >> 8
    sums[1] += block[1] + (sums[0] >> 8)
    sums[2] += block[2] + (sums[1] >> 8)
    sums[3] += block[3] + (sums[2] >> 8)
    return (sums[0] ^ sums[1] ^ sums[2] ^ sums[3]) & 0xff


def _filecore_boot_checksum(block: bytes) -> int:
    """FileCore's end-around-carry sum of the first 511 boot-block bytes."""
    checksum = sum(block[:511])
    while checksum > 0xff:
        checksum = (checksum & 0xff) + (checksum >> 8)
    return checksum


def _set_f_boot_checksum(image: bytearray) -> None:
    image[0xdff] = _filecore_boot_checksum(image[0xc00:0xe00])


def _finish_f_directory(directory: bytearray, entry_count: int) -> None:
    """Set the new-directory check byte over its used header and tail."""
    used = 5 + 26 * entry_count
    checksum = 0
    for offset in range(0, used & ~3, 4):
        checksum = struct.unpack_from("<I", directory, offset)[0] ^ _ror32(checksum, 13)
    for offset in range(used & ~3, used):
        checksum = directory[offset] ^ _ror32(checksum, 13)
    for offset in range(2008, 2044, 4):
        checksum = struct.unpack_from("<I", directory, offset)[0] ^ _ror32(checksum, 13)
    directory[2047] = (checksum ^ (checksum >> 8) ^
                       (checksum >> 16) ^ (checksum >> 24)) & 0xff


def build_f_discovery() -> bytearray:
    image = bytearray(1638400)
    image[0xc00:0xc04] = (0x20000000).to_bytes(4, "little")
    record = bytearray(60)
    record[:6] = bytes((10, 10, 2, 4, 15, 6))
    record[9] = 4
    record[10:12] = (1600).to_bytes(2, "little")
    record[12:16] = (0x209).to_bytes(4, "little")
    record[16:20] = len(image).to_bytes(4, "little")
    record[22:32] = b"ArcFTest\r\r"
    image[0xdc0:0xdc0 + 60] = record
    blocks = [bytearray(1024) for _ in range(4)]
    zone_bits = 8192 - 1600
    last_end = 32 + (len(image) // 64 + 480 - 3 * zone_bits)
    for zone, block in enumerate(blocks):
        start = 512 if zone == 0 else 32
        end = last_end if zone == 3 else 32 + zone_bits
        if zone == 0:
            block[4:64] = record
            _set_bits(block, start, 15, 2)
            _set_bits(block, start + 63, 1, 1)
            start += 64
        elif zone == 2:
            _set_bits(block, start, 15, 2)
            _set_bits(block, start + 191, 1, 1)
            start += 192
        else:
            _set_bits(block, start, 15, 3)
            _set_bits(block, start + 15, 1, 1)
            start += 16
            if zone == 3:
                _set_bits(block, start, 15, 4)
                _set_bits(block, start + 15, 1, 1)
                start += 16
        _set_bits(block, 8, 15, start - 8)
        _set_bits(block, 23, 1, 1)
        _set_bits(block, end - 1, 1, 1)
        if zone == 3:
            # FileCore closes the beyond-disc region with defect fragment 1.
            _set_bits(block, end, 15, 1)
            _set_bits(block, 31 + zone_bits, 1, 1)
        block[3] = 0xff if zone == 0 else 0
        block[0] = _zone_check(block)
    map_address = ((8192 - 1600 - 480) + (8192 - 1600)) * 64
    map_bytes = b"".join(blocks)
    image[map_address:map_address + len(map_bytes)] = map_bytes
    image[map_address + len(map_bytes):map_address + 2 * len(map_bytes)] = map_bytes
    root = _new_directory()
    _new_entry(root, 0, b"!Demo", 2048, 3 << 8, True)
    _finish_f_directory(root, 1)
    image[map_address + 8 * 1024:map_address + 10 * 1024] = root
    child = _new_directory(b"!Demo")
    run_text = b"*Echo ArcDroid synthetic application\r"
    _new_entry(child, 0, b"!Run", len(run_text), 4 << 8, load=0xffffeb00)
    _finish_f_directory(child, 1)
    zone1 = (8192 - 1600 - 480) * 64
    zone3 = (8192 - 1600 - 480 + 2 * (8192 - 1600)) * 64
    image[zone1:zone1 + 1024] = child[:1024]
    image[zone3:zone3 + 1024] = child[1024:]
    image[zone3 + 1024:zone3 + 1024 + len(run_text)] = run_text
    _set_f_boot_checksum(image)
    return image


def build_sparse_f_jfd(image: bytes, flags: int = 0,
                       skipped: frozenset[int] = frozenset(),
                       extended: bool = True,
                       first_track_times: dict[int, int] | None = None,
                       force_first_ids: frozenset[int] = frozenset()) -> bytearray:
    tracks = bytearray()
    sectors = bytearray()
    data = bytearray()
    for track_side in range(160):
        entries = bytearray()
        for sector_id in range(10):
            if track_side * 10 + sector_id in skipped:
                continue
            payload = image[(track_side * 10 + sector_id) * 1024:
                            (track_side * 10 + sector_id + 1) * 1024]
            if not any(payload) and not (track_side == 0 and
                                         sector_id in force_first_ids):
                continue
            time = first_track_times[sector_id] if track_side == 0 and \
                first_track_times is not None else 0
            entries += struct.pack("<II", (time << 24) | (4 << 16) |
                                   (sector_id << 8) | 3,
                                   len(data))
            data += payload
        if entries:
            tracks += struct.pack("<I", len(sectors))
            sectors += entries + struct.pack("<I", 0xffffffff)
        else:
            tracks += struct.pack("<I", 0xffffffff)
    track_offset = 316 if extended else 304
    sector_offset = track_offset + len(tracks)
    data_offset = sector_offset + len(sectors)
    output = bytearray(data_offset + len(data))
    output[:4] = b"JFDI"
    struct.pack_into("<I", output, 8, len(output))
    struct.pack_into("<III", output, 24, track_offset, sector_offset, data_offset)
    output[48:68] = b"ArcDroid F fixture".ljust(20, b"\0")
    if extended:
        struct.pack_into("<I", output, 304, flags)
    output[track_offset:sector_offset] = tracks
    output[sector_offset:data_offset] = sectors
    output[data_offset:] = data
    return output


def build_old_map_media(size: int, old_size: int, seed: int) -> bytearray:
    image = bytearray(((index * 29) ^ seed) & 0xFF for index in range(size))
    image[:512] = bytes(512)
    _store_u24(image, 0, 7)
    _store_u24(image, 252, old_size)
    _store_u24(image, 256, old_size - 7)
    image[509] = 3
    image[510] = 3
    image[255] = _old_map_checksum(image[:255])
    image[511] = _old_map_checksum(image[256:511])
    return image


def build_d_guest_applications(
        applications: list[tuple[bytes, bytes | None, bytes | None]],
        disc_name: bytes) -> bytearray:
    """Build project-authored D-format applications with Obey !Boot/!Run files."""
    image = bytearray(ADF_SIZE)
    total_sectors = ADF_SIZE // 256
    _store_u24(image, 252, total_sectors)
    _set_old_disc_name(image, disc_name)
    image[509] = 0
    root = _new_directory()
    ordered = sorted(applications, key=lambda application:
                     _adfs_name_key(application[0]))
    data_offset = 3072 + len(ordered) * 2048
    for index, (name, boot, run) in enumerate(ordered):
        if not 1 <= len(name) <= 10:
            raise ValueError("guest application leaf must be 1..10 bytes")
        child_offset = 3072 + index * 2048
        _new_entry(root, index, name, 2048, child_offset // 256, True)
        child = _new_directory(name)
        child_entry = 0
        files = sorted(((filename, payload) for filename, payload in
                        ((b"!Boot", boot), (b"!Run", run))
                        if payload is not None),
                       key=lambda item: _adfs_name_key(item[0]))
        for filename, payload in files:
            assert payload is not None
            data_offset = (data_offset + 1023) & ~1023
            if data_offset + len(payload) > len(image):
                raise ValueError("guest fixture data exceeds the image")
            _new_entry(child, child_entry, filename, len(payload),
                       data_offset // 256, load=0xFFFFEB00)
            child_entry += 1
            image[data_offset:data_offset + len(payload)] = payload
            data_offset += len(payload)
        _finish_new_directory(child, child_entry, 4)
        image[child_offset:child_offset + 2048] = child
    _finish_new_directory(root, len(ordered), 4)
    image[1024:3072] = root
    data_offset = (data_offset + 1023) & ~1023
    free_start = data_offset // 256
    if free_start >= total_sectors:
        raise ValueError("guest fixture leaves no valid free extent")
    _store_u24(image, 0, free_start)
    _store_u24(image, 256, total_sectors - free_start)
    image[510] = 3
    image[255] = _old_map_checksum(image[:255])
    image[511] = _old_map_checksum(image[256:511])
    return image


def build_checkpoint_guest_disc() -> bytearray:
    """Disc used by Fable to check sibling boot order, errors, writes and aliases."""
    apps = [
        (b"!Alias", b"Set ArcDroid$Trace <ArcDroid$Trace>A\r"
                     b"Set ArcDroid$Count <ArcDroid$Count>1\r"
                     b"Set Alias$Dir Echo ARCDROID-DIR-ALIAS-CAPTURED\r"
                     b"Set Alias$Filer_Run Echo ARCDROID-RUN-ALIAS-CAPTURED\r", None),
        (b"!Fault", b"Set ArcDroid$Trace <ArcDroid$Trace>E\r"
                     b"Set ArcDroid$Count <ArcDroid$Count>1\r"
                     b"Error ArcDroid synthetic sibling error\r", None),
        (b"!Prep", b"Set ArcDroid$Trace <ArcDroid$Trace>P\r"
                    b"Set ArcDroid$Count <ArcDroid$Count>1\r"
                    b"Set ArcDroid$Ready Yes\r", None),
        (b"!System", b"Set ArcDroid$Trace S\rSet ArcDroid$Count 1\r", None),
        (b"!Fonts", b"Set ArcDroid$Trace <ArcDroid$Trace>F\r"
                     b"Set ArcDroid$Count <ArcDroid$Count>1\r", None),
        (b"!Scrap", b"Set ArcDroid$Trace <ArcDroid$Trace>W\r"
                     b"Set ArcDroid$Count <ArcDroid$Count>1\r"
                     b"Echo ArcDroid-write-attempt { > ADFS::0.$.ArcWrite }\r", None),
        (b"!Qaventra", b"Set ArcDroid$Trace <ArcDroid$Trace>T\r"
                    b"Set ArcDroid$Count <ArcDroid$Count>1\r",
         b"Echo ARCDROID CHECKPOINT TRACE <ArcDroid$Trace> COUNT <ArcDroid$Count>\r"
         b"If \"<ArcDroid$Ready>\" <> \"Yes\" Then Error ArcDroid prerequisite missing\r"
         b"If \"<ArcDroid$Trace>\" <> \"SFWAEPT\" Then Error ArcDroid boot order wrong\r"
         b"If \"<ArcDroid$Count>\" <> \"1111111\" Then Error ArcDroid boot count wrong\r"
         b"Echo ARCDROID CHECKPOINT TARGET REACHED <ArcDroid$Trace> <ArcDroid$Count>\r"),
    ]
    return build_d_guest_applications(apps, b"ArcCheckA")


def build_nested_checkpoint_guest_disc(parent_name: bytes) -> bytearray:
    """Real-RISC OS checkpoint: local resource/boot order and parent isolation."""
    apps = [
        (b"!System", b"Set ArcDroid$NestedTrace S\r"
                     b"Set ArcDroid$OtherParent \"\"\r", None),
        (b"!Prep", b"Set ArcDroid$NestedTrace <ArcDroid$NestedTrace>P\r", None),
        (b"!Target", b"Set ArcDroid$NestedTrace <ArcDroid$NestedTrace>T\r",
         b"If \"<ArcDroid$NestedTrace>\" <> \"SPT\" Then Error Nested order wrong\r"
         b"If \"<ArcDroid$OtherParent>\" <> \"\" Then Error Other parent booted\r"
         b"Echo ARCDROID NESTED TARGET REACHED <ArcDroid$NestedTrace>\r"
         b"Dir\rURD\r"),
        (b"!Other", b"Set ArcDroid$OtherParent Wrong\r", None),
    ]
    image = build_d_guest_applications(apps, b"ArcNest")
    sorted_apps = sorted(apps, key=lambda item: _adfs_name_key(item[0]))
    free_start = int.from_bytes(image[0:3], "little") * 256
    first_parent = free_start
    second_parent = free_start + 2048
    root = _new_directory()
    _new_entry(root, 0, parent_name, 2048, first_parent // 256, True)
    _new_entry(root, 1, b"Other", 2048, second_parent // 256, True)
    _finish_new_directory(root, 2, 4)
    image[1024:3072] = root
    parents = [_new_directory(parent_name), _new_directory(b"Other")]
    counts = [0, 0]
    for index, (name, boot, run) in enumerate(sorted_apps):
        group = 1 if name == b"!Other" else 0
        child_offset = 3072 + index * 2048
        _new_entry(parents[group], counts[group], name, 2048,
                   child_offset // 256, True)
        counts[group] += 1
        child = bytearray(image[child_offset:child_offset + 2048])
        _finish_new_directory(child, int(boot is not None) + int(run is not None),
                              (second_parent if group else first_parent) // 256)
        image[child_offset:child_offset + 2048] = child
    for group in range(2):
        _finish_new_directory(parents[group], counts[group], 4)
    image[first_parent:first_parent + 2048] = parents[0]
    image[second_parent:second_parent + 2048] = parents[1]
    new_free = second_parent + 2048
    _store_u24(image, 0, new_free // 256)
    _store_u24(image, 256, ADF_SIZE // 256 - new_free // 256)
    image[255] = _old_map_checksum(image[:255])
    image[511] = _old_map_checksum(image[256:511])
    return image


def build_checkpoint_name_discs() -> dict[str, bytearray]:
    """One valid, lone-application disc for each addressable unusual leaf."""
    cases = (
        ("checkpoint-a-top-bit.adf", b"!Z\xf6rvex", b"ArcTopBit",
         b"Echo ARCDROID TOP-BIT NAME REACHED\r"),
        ("checkpoint-a-hard-space.adf", b"!Nim\xa0Vox", b"ArcHardSp",
         b"Echo ARCDROID HARD-SPACE NAME REACHED\r"),
        ("checkpoint-a-greater-than.adf", b"!Vor>nex", b"ArcGreater",
         b"Echo ARCDROID GREATER-THAN NAME REACHED\r"),
    )
    return {filename: build_d_guest_applications([(name, None, run)], disc_name)
            for filename, name, disc_name, run in cases}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--checkpoint-guest-disc", type=Path)
    parser.add_argument("--checkpoint-name-discs", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "arcdroid-test.rom").write_bytes(build_rom())
    (args.output / "arcdroid-power-on-early-swi.rom").write_bytes(
        build_rom(power_on_probe="early-swi"))
    (args.output / "arcdroid-power-on-early-xswi.rom").write_bytes(
        build_rom(power_on_probe="early-xswi"))
    (args.output / "arcdroid-power-on-late-kart.rom").write_bytes(
        build_rom(power_on_probe="late-kart"))
    (args.output / "arcdroid-power-on-cmos-guest.rom").write_bytes(
        build_rom(cmos_exerciser=True, power_on_probe="cmos-boot"))
    for pattern in ("prompt", "cli-then-prompt"):
        (args.output / f"arcdroid-power-on-boot-{pattern}.rom").write_bytes(
            build_rom(boot_watch_pattern=pattern, boot_watch_kart=True))
    for pattern in ("prompt", "prompt-write", "cli", "cli-then-prompt", "desktop", "cli-confirm", "horizon-edge", "two-phase"):
        (args.output / f"arcdroid-boot-{pattern}.rom").write_bytes(
            build_rom(boot_watch_pattern=pattern))
    (args.output / "arcdroid-cmos-exerciser.rom").write_bytes(
        build_rom(cmos_exerciser=True))
    (args.output / "arcdroid-chooser-test.rom").write_bytes(build_chooser_rom())
    (args.output / "arcdroid-chooser-d-test.rom").write_bytes(
        build_chooser_rom(helper_visual_oracle=True))
    unsupported_launch_rom = build_chooser_rom()
    unsupported_launch_rom[-1] = 0x42
    (args.output / "arcdroid-unsupported-launch-test.rom").write_bytes(
        unsupported_launch_rom)
    (args.output / "arcdroid-test.adf").write_bytes(build_adf())
    build_adffs_fixtures(args.output / "adffs")
    (args.output / "arcdroid-demo.adl").write_bytes(build_demo_adl())
    (args.output / "arcdroid-application-l.adl").write_bytes(build_application_l())
    (args.output / "arcdroid-chooser-l.adl").write_bytes(build_chooser_application_l())
    (args.output / "arcdroid-g1-chooser-l.adl").write_bytes(build_g1_chooser_l())
    (args.output / "arcdroid-nested-chooser-l.adl").write_bytes(
        build_nested_chooser_l(b"Folder"))
    (args.output / "arcdroid-extended-chooser-d.adf").write_bytes(
        build_nested_chooser_d(b"Arc\xa0Games"))
    launch = args.output / "launch-family"
    launch.mkdir(exist_ok=True)
    launch_images = {
        "single-l.adl": (build_l_discovery([(b"!Demo", True)], b"Demo"), "L"),
        "single-d-hugo.adf": (build_d_discovery([(b"!Demo", True)], b"Demo"), "D"),
        "single-d-3180.adf": (build_d_discovery(
            [(b"!Demo", True)], b"Demo", declared_sectors=3180), "D"),
        "single-d-nick.adf": (build_d_discovery([(b"!Demo", True)], b"Demo", signature=b"Nick"), "D"),
        "single-e-hugo.adf": (build_e_discovery([(b"!Demo", True)], b"Demo"), "E"),
        "single-e-nick.adf": (build_e_discovery([(b"!Demo", True)], b"Demo", signature=b"Nick"), "E"),
        "late-target-e.adf": (build_e_discovery(
            [(b"!Demo", True)], b"Demo", late_target=True), "E"),
        "stale-d.adf": (build_d_discovery(
            [(b"!Demo", True)], b"Demo", stale_names=True), "D"),
        "stale-e.adf": (build_e_discovery(
            [(b"!Demo", True)], b"Demo", stale_names=True), "E"),
        "short-name-attr-l.adl": (build_l_discovery(
            [(b"!A", True)], b"A", root_attribute_high=0), "L"),
        "top-bit-d.adf": (build_d_discovery([(b"!Z\xf6rvex", True)], b"Zorvex"), "D"),
        "hard-space-e.adf": (build_e_discovery([(b"!Nim\xa0Vox", True)], b"NimVox"), "E"),
        "full-ten-d.adf": (build_d_discovery([(b"!ABCDEFGHI", True)]), "D"),
        "attr-88-d.adf": (build_d_discovery(
            [(b"!Zorvex", True)], b"Zorvex", root_attribute_high=0), "D"),
        "irrelevant-extent-e.adf": (build_e_discovery(
            [(b"!Zorvex", True)], b"Zorvex", irrelevant_malformed=True), "E"),
        "multiple-exact-d.adf": (build_d_discovery([(b"!Alpha", True), (b"!Beta", True)], b"Beta"), "D"),
        "multiple-trailing-d.adf": (build_d_discovery([(b"!Alpha", True), (b"!Beta", True)], b"Beta2"), "D"),
        "ambiguous-l.adl": (build_l_discovery(
            [(b"!Docs", False), (b"!Foo", True), (b"!Bar", True)], b"Other"), "L"),
        "chooser-resources-d.adf": (build_d_discovery(
            [(b"!System", True), (b"!Fonts", True), (b"!Scrap", True),
             (b"!Virelai", True)], b"Other"), "D"),
        "chooser-resource-pair-d.adf": (build_d_discovery(
            [(b"!System", True), (b"!Fonts", True), (b"!Scrap", True),
             (b"!Nulvane", True), (b"!Qaventra", True)], b"Other"), "D"),
        "chooser-incomplete-d.adf": (build_d_discovery(
            [(b"!Nulvane", False), (b"!Qaventra", True)], b"Other",
            incomplete=0), "D"),
        "no-run-d.adf": (build_d_discovery([(b"!Docs", False), (b"!Data", False)]), "D"),
        "incomplete-l.adl": (build_l_discovery([(b"!Broken", True)], incomplete=0), "L"),
        "incomplete-d.adf": (build_d_discovery([(b"!Broken", True)], incomplete=0), "D"),
        "incomplete-e.adf": (build_e_discovery([(b"!Broken", True)], unresolved=0), "E"),
        "label-with-unknown-d.adf": (build_d_discovery(
            [(b"!Zorvex", True), (b"!Nulvane", False)], b"Zorvex", incomplete=1), "D"),
        "label-is-unknown-d.adf": (build_d_discovery(
            [(b"!Zorvex", True), (b"!Nulvane", False)], b"Nulvane", incomplete=1), "D"),
        "duplicate-root-d.adf": (build_d_discovery(
            [(b"!Qarvox", True), (b"!qARVOX", True)], b"Qarvox"), "D"),
        "duplicate-root-file-d.adf": (build_d_discovery(
            [(b"!Qarvox", True)], b"Qarvox", duplicate_root_file=0), "D"),
        "duplicate-run-d.adf": (build_d_discovery(
            [(b"!Zorvex", True)], b"Zorvex", duplicate_run=0), "D"),
        "duplicate-boot-e.adf": (build_e_discovery(
            [(b"!Zorvex", True)], b"Zorvex", boot_children={0},
            duplicate_boot=0), "E"),
        "has-boot-d.adf": (build_d_discovery(
            [(b"!Zorvex", True)], b"Zorvex", boot_children={0}), "D"),
        "self-boot-unknown-d.adf": (build_d_discovery(
            [(b"!Broken", False)], b"Broken", boot=2, incomplete=0), "D"),
        "self-boot-file-d.adf": (build_d_discovery(
            [(b"!Demo", True)], b"Demo", boot=3, root_boot="file"), "D"),
        "self-boot-dir-d.adf": (build_d_discovery(
            [(b"!Demo", True)], b"Demo", boot=3, root_boot="directory"), "D"),
        "nested-literal-d.adf": (build_nested_d_discovery([
            (b"Folder", [(b"!Nulvane", True, True),
                         (b"!System", False, True),
                         (b"!Qaventra", True, False)]),
            (b"Extras", [(b"!Zorvex", True, True)])], b"Other"), "D"),
        "nested-extended-literal-d.adf": (build_nested_d_discovery([
            (b"Arc\xa0Games", [(b"!Demo", True, True)])], b"Demo"), "D"),
        "nested-single-e.adf": (build_nested_e_discovery(b"Folder"), "E"),
        "nested-mixed-root-d.adf": (build_nested_with_root_d(b"!Root", "application"), "D"),
        "nested-resource-root-d.adf": (build_nested_with_root_d(b"!System", "resource"), "D"),
        "nested-unknown-root-d.adf": (build_nested_with_root_d(b"!Broken", "unknown"), "D"),
        "nested-depth-two-d.adf": (build_depth_two_only_d(), "D"),
        "nested-disc-tie-d.adf": (build_nested_d_discovery([
            (b"Folder", [(b"!Demo", True, False)]),
            (b"Other", [(b"!Demo", True, False)])], b"Demo"), "D"),
        "nested-unsafe-leaf-d.adf": (build_nested_d_discovery([
            (b"Folder", [(b"!Bad.Name", True, False)])]), "D"),
        "nested-collision-d.adf": (build_nested_d_discovery([
            (b"Arc Games", [(b"!Demo", True, True)]),
            (b"Arcade", [(b"!Other", True, False)])], b"Demo"), "D"),
        "nested-file-collision-d.adf": (build_nested_d_discovery([
            (b"Arc", [(b"!Demo", True, True)])], b"Demo",
            root_files=(b"arc",)), "D"),
        "nested-empty-prefix-d.adf": (build_nested_d_discovery([
            (b" Bad", [(b"!Demo", True, False)])], b"Demo"), "D"),
        "nested-pipe-d.adf": (build_nested_d_discovery([
            (b"Arc|Games", [(b"!Demo", True, False)])], b"Demo"), "D"),
        "nested-quote-d.adf": (build_nested_d_discovery([
            (b'Arc"Games', [(b"!Demo", True, False)])], b"Demo"), "D"),
        "nested-extended-boundary-d.adf": (build_nested_d_discovery([
            (b"Arc\x80 Bad", [(b"!Demo", True, False)])], b"Demo"), "D"),
        "nested-extended-first-d.adf": (build_nested_d_discovery([
            (b"\x80Safe", [(b"!Demo", True, False)])], b"Demo"), "D"),
        "nested-one-byte-d.adf": (build_nested_d_discovery([
            (b"A", [(b"!Demo", True, False)])], b"Demo"), "D"),
        "nested-case-file-d.adf": (build_nested_d_discovery([
            (b"Arc", [(b"!Demo", True, False)])], b"Demo",
            root_files=(b"aRC",)), "D"),
        "nested-literal-duplicate-d.adf": (build_nested_d_discovery([
            (b"Folder", [(b"!Demo", True, True)]),
            (b"folder", [(b"!Other", True, True)])], b"Demo"), "D"),
        "nested-parent-file-d.adf": (build_nested_d_discovery([
            (b"Folder", [(b"!Demo", True, True)])], b"Demo",
            duplicate_parent_file=0), "D"),
        "nested-unreadable-d.adf": (build_nested_d_discovery([
            (b"Folder", [(b"!Demo", True, False)]),
            (b"Broken", [(b"!Other", True, False)])], b"Demo",
            unreadable_parent=1), "D"),
        "nested-no-target-unreadable-d.adf": (build_nested_d_discovery([
            (b"Broken", [(b"!Other", True, False)])],
            unreadable_parent=0), "D"),
        "nested-unreadable-child-d.adf": (build_nested_d_discovery([
            (b"Folder", [(b"!Demo", True, False)])], b"Demo",
            unreadable_child=0), "D"),
        "nested-child-collision-d.adf": (build_nested_d_discovery([
            (b"Folder", [(b"!Demo", True, True)])], b"Demo",
            duplicate_child_file=0), "D"),
        "nested-77-d.adf": (build_nested_d_discovery([
            (b"Folder", [(f"!A{i:02d}".encode(), True, False)
                         for i in range(77)])]), "D"),
        "nested-78-d.adf": (build_nested_d_discovery([
            (b"Folder", [(f"!A{i:02d}".encode(), True, False)
                         for i in range(77)]),
            (b"More", [(b"!Last", True, False)])]), "D"),
        "nested-77-parents-d.adf": (build_nested_d_discovery([
            (f"P{i:02d}".encode(), []) for i in range(77)]), "D"),
        "unsafe-disc-name-l.adl": (build_l_discovery(
            [(b"!Foo", True), (b"!Bar", True)], b"Bad\nName"), "L"),
        "more-eight-d.adf": (build_d_discovery(
            [(f"!App{i}".encode(), i == 9) for i in range(10)]), "D"),
        "fragmented-e.adf": (build_e_discovery([(b"!Demo", True)], b"Demo", fragmented=0), "E"),
        "unresolved-e.adf": (build_e_discovery([(b"!Demo", True)], b"Demo", unresolved=0), "E"),
        "explicit-d.adf": (build_d_discovery(
            [(b"!Docs", False), (b"!Demo", True), (b"!Other", True)], b"Other", boot=3), "D"),
        "sibling-pipe-l.adl": (build_l_discovery(
            [(b"!Bad|Name", False), (b"!Qaventra", True)], b"Qaventra",
            boot_children={0}), "L"),
        "duplicate-siblings-l.adl": (build_l_discovery(
            [(b"!Twin", False), (b"!tWIN", False), (b"!Qaventra", True)],
            b"Qaventra", boot_children={0, 1}), "L"),
    }
    for boot in range(4):
        launch_images[f"boot-{boot}-l.adl"] = (
            build_l_discovery([(b"!Demo", True)], b"Demo", boot), "L")
        if boot:
            launch_images[f"boot-file-{boot}-l.adl"] = (
                build_l_discovery([(b"!Demo", True)], b"Demo", boot,
                                  root_boot="file"), "L")
            launch_images[f"boot-dir-{boot}-l.adl"] = (
                build_l_discovery([(b"!Demo", True)], b"Demo", boot,
                                  root_boot="directory"), "L")
        for shape, candidates, incomplete in (
                ("zero", [], None),
                ("one", [(b"!Demo", True)], None),
                ("multiple", [(b"!Demo", True), (b"!Other", True)], None),
                ("unknown", [(b"!Broken", True)], 0)):
            for boot_object in ("absent", "file", "directory"):
                launch_images[f"boot-matrix-{boot}-{boot_object}-{shape}.adf"] = (
                    build_d_discovery(candidates, b"Fixture", boot,
                                      incomplete=incomplete,
                                      root_boot=None if boot_object == "absent"
                                                else boot_object), "D")
    duplicate_boot_root = build_d_discovery([(b"!Demo", True)], b"Demo",
                                            boot=3, root_boot="file")
    duplicate_root = bytearray(duplicate_boot_root[1024:3072])
    _new_entry(duplicate_root, 2, b"!Boot", 1, 700416 // 256)
    duplicate_boot_root[1024:3072] = duplicate_root
    launch_images["boot-duplicate-root-d.adf"] = (duplicate_boot_root, "D")
    unreadable_boot_root = build_d_discovery([(b"!Demo", True)], b"Demo",
                                             boot=3, root_boot="file")
    unreadable_boot_root[1025:1029] = b"Bad!"
    launch_images["boot-unreadable-root-d.adf"] = (unreadable_boot_root, "D")
    illegal = (b"!Bad.Name", b"!Bad:Name", b"!Bad<Name", b"!Bad>Name",
               b"!Bad|Name", b"!Bad$Name", b"!Bad&Name", b"!Bad@Name",
               b"!Bad^Name", b"!Bad%Name", b"!Bad*Name", b"!Bad#Name",
               b"!Bad\"Name", b"!Bad Name", b"!Bad\\Name", b"!Bad\x01", b"!Bad\x7f",
               b"!BadNameX\x80")
    launch_images["illegal-l.adl"] = (
        build_l_discovery([(name, True) for name in illegal]), "L")
    for name, (payload, _geometry) in launch_images.items():
        (launch / name).write_bytes(payload)
    (launch / "boot-known-one-launchable.adf").write_bytes(
        build_d_discovery([(b"!Demo", True), (b"!Bad.Name", True)],
                          b"Fixture", 3, root_boot="file"))
    one = launch_images["boot-matrix-3-file-one.adf"][0]
    several = launch_images["boot-matrix-3-file-multiple.adf"][0]
    none = launch_images["boot-matrix-3-file-zero.adf"][0]
    structural = {
        "one-to-several": (bytearray(several), one[1024:3072], several[1024:3072]),
        "one-to-none": (bytearray(one), one[1024:3072], none[1024:3072]),
        "none-to-one": (bytearray(one), none[1024:3072], one[1024:3072]),
    }
    for shape, (source, initial_root, final_root) in structural.items():
        source[1024:3072] = initial_root
        (launch / f"boot-struct-{shape}.adf").write_bytes(source)
        (args.output / f"arcdroid-boot-struct-{shape}.rom").write_bytes(
            build_rom(boot_watch_pattern="prompt-structure",
                      write_payload=final_root))
    renamed = build_d_discovery([(b"!Changed", True)], b"Fixture", 3,
                                root_boot="file")
    (launch / "boot-struct-one-to-renamed.adf").write_bytes(one)
    (args.output / "arcdroid-boot-struct-one-to-renamed.rom").write_bytes(
        build_rom(boot_watch_pattern="prompt-structure",
                  boot_write_sectors=(1, 2, 4, 5),
                  write_payload=renamed[1024:3072] + renamed[4096:6144]))
    sibling_source = build_d_discovery(
        [(b"!Demo", True), (b"!Other", False)], b"Fixture", 3,
        root_boot="file")
    (launch / "boot-struct-sibling.adf").write_bytes(sibling_source)
    (args.output / "arcdroid-boot-struct-sibling.rom").write_bytes(
        build_rom(boot_watch_pattern="prompt-structure",
                  boot_write_sectors=(6, 7),
                  write_payload=several[6144:8192]))
    jfd_struct = _build_jfd_disk(
        (launch / "boot-struct-one-to-several.adf").read_bytes(), "D",
        b"ArcDroid structural write fixture", extended=True)
    struct.pack_into("<I", jfd_struct, 304, 2)  # writable native store
    (launch / "boot-struct-one-to-several.jfd").write_bytes(jfd_struct)
    hd = args.output / "hd-family"
    hd.mkdir(exist_ok=True)
    f_image = build_f_discovery()
    (hd / "valid-f.adf").write_bytes(f_image)
    (hd / "valid-f.jfd").write_bytes(build_sparse_f_jfd(f_image))
    map_address = ((8192 - 1600 - 480) + (8192 - 1600)) * 64
    identification = args.output / "jfd-identification"
    build_file_data_fixtures(args.output / "jfd-file-data")
    identification.mkdir(exist_ok=True)
    first_ids = frozenset((0, 3, 4, 5, 6, 7, 8, 9))
    first_times = {0: 0, 3: 60, 4: 80, 5: 100, 6: 120,
                   7: 140, 8: 160, 9: 180}
    (identification / "sparse-f-timed.jfd").write_bytes(build_sparse_f_jfd(
        f_image, skipped=frozenset((1, 2)), first_track_times=first_times,
        force_first_ids=first_ids))
    (identification / "sparse-f-untimed.jfd").write_bytes(build_sparse_f_jfd(
        f_image, skipped=frozenset((1, 2)),
        first_track_times={sector_id: 0xFF for sector_id in first_ids},
        force_first_ids=first_ids))
    (identification / "sparse-f-mixed-basis.jfd").write_bytes(
        build_sparse_f_jfd(f_image, skipped=frozenset((1, 2, 4)),
                           first_track_times=first_times,
                           force_first_ids=first_ids))
    last_end = 32 + (len(f_image) // 64 + 480 - 3 * (8192 - 1600))
    zone_end = 32 + 8192 - 1600
    for stem, bits, free_link in (
        ("zero-tail", (), False),
        ("tail-id-only", (last_end,), False),
        ("tail-stop-only", (zone_end - 1,), False),
        ("tail-other-id", (last_end + 1, zone_end - 1), False),
        ("tail-two-fragments", (last_end, last_end + 31,
                                last_end + 32, zone_end - 1), False),
        ("tail-early-stop", (last_end, last_end + 31), False),
        ("tail-free-link", (last_end, zone_end - 1), True),
        ("tail-stray-bit", (last_end + 40,), False),
    ):
        variant = bytearray(f_image)
        for copy_at in (map_address, map_address + 4096):
            zone_at = copy_at + 3 * 1024
            block = bytearray(variant[zone_at:zone_at + 1024])
            for bit in range(last_end, zone_end):
                block[bit // 8] &= ~(1 << (bit & 7))
            for bit in bits:
                _set_bits(block, bit, 1, 1)
            if free_link:
                for bit in range(8, 23):
                    block[bit // 8] &= ~(1 << (bit & 7))
                _set_bits(block, 8, 15, last_end - 8)
            block[0] = _zone_check(block)
            variant[zone_at:zone_at + 1024] = block
        (identification / f"sparse-f-{stem}.jfd").write_bytes(
            build_sparse_f_jfd(variant, skipped=frozenset((1, 2)),
                               first_track_times=first_times,
                               force_first_ids=first_ids))
    f_one_zone = bytearray(1638400)
    one_record = bytearray(60)
    one_record[:6] = bytes((10, 10, 2, 4, 15, 10))
    one_record[9] = 1
    one_record[10:12] = (1600).to_bytes(2, "little")
    one_record[16:20] = len(f_one_zone).to_bytes(4, "little")
    f_one_zone[0xdc0:0xdc0 + 60] = one_record
    one_map = bytearray(1024)
    one_map[4:64] = one_record
    _set_bits(one_map, 8, 15, 504)
    _set_bits(one_map, 23, 1, 1)
    _set_bits(one_map, 2111, 1, 1)
    one_map[3] = 0xff
    one_map[0] = _zone_check(one_map)
    f_one_zone[:1024] = one_map
    (identification / "sparse-f-one-zone.jfd").write_bytes(build_sparse_f_jfd(
        f_one_zone, skipped=frozenset((1, 2)), first_track_times=first_times,
        force_first_ids=first_ids))
    one_stray = bytearray(f_one_zone)
    one_stray_map = bytearray(one_map)
    _set_bits(one_stray_map, 2112 + 40, 1, 1)
    one_stray_map[0] = _zone_check(one_stray_map)
    one_stray[:1024] = one_stray_map
    (identification / "sparse-f-one-zone-stray-tail.jfd").write_bytes(
        build_sparse_f_jfd(one_stray, skipped=frozenset((1, 2)),
                           first_track_times=first_times,
                           force_first_ids=first_ids))
    one_prefix_map = bytearray(one_map)
    for bit in range(8, 23):
        one_prefix_map[bit // 8] &= ~(1 << (bit & 7))
    _set_bits(one_prefix_map, 8, 15, 568)
    _set_bits(one_prefix_map, 512, 15, 2)
    _set_bits(one_prefix_map, 575, 1, 1)
    one_prefix_map[0] = _zone_check(one_prefix_map)
    f_one_overlap = bytearray(f_one_zone)
    f_one_overlap[:1024] = one_prefix_map
    f_one_overlap[1024:2048] = one_prefix_map
    (identification / "sparse-f-map-backup.jfd").write_bytes(
        build_sparse_f_jfd(f_one_overlap, skipped=frozenset((1, 2)),
                           first_track_times=first_times,
                           force_first_ids=first_ids))
    first_without_zero = frozenset((1, 3, 4, 5, 6, 7, 8, 9))
    (identification / "sparse-f-map-primary.jfd").write_bytes(
        build_sparse_f_jfd(f_one_overlap, skipped=frozenset((0, 2)),
                           first_track_times={**first_times, 1: 20},
                           force_first_ids=first_without_zero))
    f_backup = bytearray(f_image)
    f_backup[map_address + 0] ^= 1
    (identification / "sparse-f-backup.jfd").write_bytes(build_sparse_f_jfd(
        f_backup, skipped=frozenset((1, 2)), first_track_times=first_times,
        force_first_ids=first_ids))
    f_copy_disagree = bytearray(f_image)
    backup_zone0 = bytearray(f_copy_disagree[map_address + 4096:
                                             map_address + 5120])
    _set_bits(backup_zone0, 512, 15, 3)
    backup_zone0[0] = _zone_check(backup_zone0)
    f_copy_disagree[map_address + 4096:map_address + 5120] = backup_zone0
    (identification / "sparse-f-copy-disagree.jfd").write_bytes(
        build_sparse_f_jfd(f_copy_disagree, skipped=frozenset((1, 2)),
                           first_track_times=first_times,
                           force_first_ids=first_ids))
    f_copy_free = bytearray(f_image)
    backup_zone0 = bytearray(f_copy_free[map_address + 4096:
                                          map_address + 5120])
    for bit in (*range(8, 23), *range(512, 527), 575):
        backup_zone0[bit // 8] &= ~(1 << (bit & 7))
    _set_bits(backup_zone0, 8, 15, 504)
    backup_zone0[0] = _zone_check(backup_zone0)
    f_copy_free[map_address + 4096:map_address + 5120] = backup_zone0
    (identification / "sparse-f-copy-free-prefix.jfd").write_bytes(
        build_sparse_f_jfd(f_copy_free, skipped=frozenset((1, 2)),
                           first_track_times=first_times,
                           force_first_ids=first_ids))
    f_partial_system = bytearray(f_image)
    for copy_at in (map_address, map_address + 4096):
        block = bytearray(f_partial_system[copy_at:copy_at + 1024])
        _set_bits(block, 535, 1, 1)
        _set_bits(block, 536, 15, 3)
        block[0] = _zone_check(block)
        f_partial_system[copy_at:copy_at + 1024] = block
    (identification / "sparse-f-partial-system.jfd").write_bytes(
        build_sparse_f_jfd(f_partial_system, skipped=frozenset((1, 2)),
                           first_track_times=first_times,
                           force_first_ids=first_ids))
    f_root_boundary = bytearray(f_image)
    f_root_boundary[0xdc0 + 12:0xdc0 + 16] = (0x20e).to_bytes(4, "little")
    for copy_at in (map_address, map_address + 4096):
        block = bytearray(f_root_boundary[copy_at:copy_at + 1024])
        block[4 + 12:4 + 16] = (0x20e).to_bytes(4, "little")
        _set_bits(block, 543, 1, 1)
        _set_bits(block, 544, 15, 2)
        block[0] = _zone_check(block)
        f_root_boundary[copy_at:copy_at + 1024] = block
    _set_f_boot_checksum(f_root_boundary)
    (identification / "sparse-f-root-boundary.jfd").write_bytes(
        build_sparse_f_jfd(f_root_boundary, skipped=frozenset((1, 2)),
                           first_track_times=first_times,
                           force_first_ids=first_ids))
    f_root_second = bytearray(f_image)
    # The shared offset skips fragment 2's zone-2 run. Root sector one is
    # zone 0 byte 0; sector two is the omitted prefix at byte 0x400.
    f_root_second[0xdc0 + 12:0xdc0 + 16] = (0x20d).to_bytes(4, "little")
    for copy_at in (map_address, map_address + 4096):
        block = bytearray(f_root_second[copy_at:copy_at + 1024])
        block[4 + 12:4 + 16] = (0x20d).to_bytes(4, "little")
        block[0] = _zone_check(block)
        f_root_second[copy_at:copy_at + 1024] = block
    _set_f_boot_checksum(f_root_second)
    (identification / "sparse-f-root-second.jfd").write_bytes(
        build_sparse_f_jfd(f_root_second, skipped=frozenset((1, 2)),
                           first_track_times=first_times,
                           force_first_ids=first_ids))
    f_root_unresolved = bytearray(f_image)
    f_root_unresolved[0xdc0 + 12:0xdc0 + 16] = (0x500).to_bytes(4, "little")
    for copy_at in (map_address, map_address + 4096):
        block = bytearray(f_root_unresolved[copy_at:copy_at + 1024])
        block[4 + 12:4 + 16] = (0x500).to_bytes(4, "little")
        block[0] = _zone_check(block)
        f_root_unresolved[copy_at:copy_at + 1024] = block
    _set_f_boot_checksum(f_root_unresolved)
    (identification / "sparse-f-root-unresolved.jfd").write_bytes(
        build_sparse_f_jfd(f_root_unresolved, skipped=frozenset((1, 2)),
                           first_track_times=first_times,
                           force_first_ids=first_ids))
    (identification / "sparse-f-boot-omitted.jfd").write_bytes(
        build_sparse_f_jfd(f_image, skipped=frozenset((1, 2, 3)),
                           first_track_times=first_times,
                           force_first_ids=first_ids))
    f_mixed = bytearray(f_image)
    f_mixed[map_address] ^= 1
    f_mixed[map_address + 4096 + 1024] ^= 1
    (identification / "sparse-f-mixed.jfd").write_bytes(build_sparse_f_jfd(
        f_mixed, skipped=frozenset((1, 2)), first_track_times=first_times,
        force_first_ids=first_ids))
    f_allocated = bytearray(f_image)
    for copy_at in (map_address, map_address + 4096):
        block = bytearray(f_allocated[copy_at:copy_at + 1024])
        _set_bits(block, 512, 15, 3)  # Allocated, but not system fragment.
        block[0] = _zone_check(block)
        f_allocated[copy_at:copy_at + 1024] = block
    (identification / "sparse-f-allocated.jfd").write_bytes(build_sparse_f_jfd(
        f_allocated, skipped=frozenset((1, 2)), first_track_times=first_times,
        force_first_ids=first_ids))
    f_conflict = bytearray(f_image)
    backup_zone0 = bytearray(f_conflict[map_address + 4096:
                                     map_address + 5120])
    backup_zone0[10] = 1  # Record byte 6: addressing flags differ.
    backup_zone0[0] = _zone_check(backup_zone0)
    f_conflict[map_address + 4096:map_address + 5120] = backup_zone0
    (identification / "sparse-f-conflict.jfd").write_bytes(build_sparse_f_jfd(
        f_conflict, skipped=frozenset((1, 2)), first_track_times=first_times,
        force_first_ids=first_ids))
    f_boot_conflict = bytearray(f_image)
    for copy_at in (map_address, map_address + 4096):
        block = bytearray(f_boot_conflict[copy_at:copy_at + 1024])
        block[10] = 1  # Both copies agree; the boot record still says zero.
        block[0] = _zone_check(block)
        f_boot_conflict[copy_at:copy_at + 1024] = block
    (identification / "sparse-f-boot-conflict.jfd").write_bytes(
        build_sparse_f_jfd(f_boot_conflict, skipped=frozenset((1, 2)),
                           first_track_times=first_times,
                           force_first_ids=first_ids))
    f_bad_cross = bytearray(f_image)
    for copy_at in (map_address, map_address + 4096):
        block = bytearray(f_bad_cross[copy_at:copy_at + 1024])
        block[3] = 0
        block[0] = _zone_check(block)
        f_bad_cross[copy_at:copy_at + 1024] = block
    (identification / "sparse-f-bad-cross.jfd").write_bytes(build_sparse_f_jfd(
        f_bad_cross, skipped=frozenset((1, 2)), first_track_times=first_times,
        force_first_ids=first_ids))
    f_past_end = bytearray(f_image)
    last_map_end = 32 + (len(f_image) // 64 + 480 -
                         3 * (8192 - 1600))
    for copy_at in (map_address, map_address + 4096):
        zone_at = copy_at + 3 * 1024
        block = bytearray(f_past_end[zone_at:zone_at + 1024])
        _set_bits(block, last_map_end + 9, 1, 1)
        block[0] = _zone_check(block)
        f_past_end[zone_at:zone_at + 1024] = block
    (identification / "sparse-f-past-end.jfd").write_bytes(build_sparse_f_jfd(
        f_past_end, skipped=frozenset((1, 2)), first_track_times=first_times,
        force_first_ids=first_ids))
    map_address = ((8192 - 1600 - 480) + (8192 - 1600)) * 64
    last_end = 32 + (len(f_image) // 64 + 480 - 3 * (8192 - 1600))
    blank_boot = bytearray(f_image)
    blank_boot[0xdc0 + 20:0xdc0 + 32] = bytes(12)
    _set_f_boot_checksum(blank_boot)
    (hd / "blank-boot-name-f.adf").write_bytes(blank_boot)
    stray_tail = bytearray(f_image)
    missing_terminator = bytearray(f_image)
    for copy in (0, 4096):
        block_at = map_address + copy + 3 * 1024
        block = bytearray(stray_tail[block_at:block_at + 1024])
        _set_bits(block, last_end + 9, 1, 1)
        _set_bits(block, 32 + 8192 - 1600 - 1, 1, 1)
        block[0] = _zone_check(block)
        stray_tail[block_at:block_at + 1024] = block
        block = bytearray(missing_terminator[block_at:block_at + 1024])
        block[(last_end - 1) // 8] &= ~(1 << ((last_end - 1) & 7))
        _set_bits(block, last_end + 9, 1, 1)
        block[0] = _zone_check(block)
        missing_terminator[block_at:block_at + 1024] = block
    (hd / "stray-tail-bits-f.adf").write_bytes(stray_tail)
    (hd / "unterminated-last-f.adf").write_bytes(missing_terminator)
    small_spare = bytearray(f_image)
    small_spare[0xdc0 + 10:0xdc0 + 12] = (31).to_bytes(2, "little")
    _set_f_boot_checksum(small_spare)
    (hd / "small-zone-spare-f.adf").write_bytes(small_spare)
    (hd / "riscpc-f.jfd").write_bytes(build_sparse_f_jfd(
        f_image, (0x10 << 8) | (0x18 << 16)))
    (hd / "legacy-f.jfd").write_bytes(build_sparse_f_jfd(
        f_image, extended=False))
    (hd / "zero-cpu-f.jfd").write_bytes(build_sparse_f_jfd(
        f_image, 0x18 << 16))
    (hd / "zero-os-f.jfd").write_bytes(build_sparse_f_jfd(
        f_image, 0x10 << 8))
    (hd / "write-protected-f.jfd").write_bytes(build_sparse_f_jfd(
        f_image, 1))
    for tag, flags in (("arm250", 1 << 9), ("arm3", 1 << 8),
                       ("riscos2", 1 << 16), ("riscos31", 1 << 17)):
        (hd / f"{tag}-f.jfd").write_bytes(build_sparse_f_jfd(
            f_image, flags | (0x18 << 16) | (0x10 << 8)))
    corrupted = bytearray(f_image)
    corrupted[map_address] ^= 1
    (hd / "backup-f.adf").write_bytes(corrupted)
    (hd / "backup-f.jfd").write_bytes(build_sparse_f_jfd(corrupted))
    (hd / "unreadable-primary-f.jfd").write_bytes(build_sparse_f_jfd(
        f_image, skipped=frozenset({map_address // 1024 + 1})))
    corrupted[map_address + 4096] ^= 1
    (hd / "corrupt-both-f.adf").write_bytes(corrupted)
    invalid_zones = bytearray(f_image)
    invalid_zones[0xdc0 + 9] = 9
    _set_f_boot_checksum(invalid_zones)
    (hd / "invalid-zones-f.adf").write_bytes(invalid_zones)
    bad_free = bytearray(f_image)
    bad_free[map_address + 1024 + 1] = 0xff
    bad_free[map_address + 1024 + 2] = 0x7f
    bad_free[map_address + 1024] = _zone_check(bytearray(
        bad_free[map_address + 1024:map_address + 2048]))
    bad_free[map_address + 4096:map_address + 8192] = \
        bad_free[map_address:map_address + 4096]
    (hd / "invalid-free-f.adf").write_bytes(bad_free)
    invalid_id = bytearray(f_image)
    for record_at in (0xdc0, map_address + 4, map_address + 4096 + 4):
        struct.pack_into("<I", invalid_id, record_at + 12, 0x7fff00)
    _set_f_boot_checksum(invalid_id)
    for copy_at in (map_address, map_address + 4096):
        invalid_id[copy_at] = _zone_check(bytearray(
            invalid_id[copy_at:copy_at + 1024]))
    (hd / "invalid-id-f.adf").write_bytes(invalid_id)
    bad_cross = bytearray(f_image)
    for copy_at in (map_address, map_address + 4096):
        bad_cross[copy_at + 3] = 0
        bad_cross[copy_at] = _zone_check(bytearray(
            bad_cross[copy_at:copy_at + 1024]))
    (hd / "invalid-cross-f.adf").write_bytes(bad_cross)
    (hd / "sparse-map-holes-f.jfd").write_bytes(build_sparse_f_jfd(
        f_image, skipped=frozenset({map_address // 1024 + 1,
                                    map_address // 1024 + 5})))
    (hd / "filler-f.adf").write_bytes(bytes(1638400))
    (hd / "short-f.adf").write_bytes(f_image[:-1])
    (hd / "long-f.adf").write_bytes(f_image + b"\0")
    old_over = build_d_discovery([(b"!Demo", True)], b"Demo")
    _store_u24(old_over, 252, 3263)
    old_over[255] = _old_map_checksum(old_over[:255])
    (hd / "old-overdeclared-d.adf").write_bytes(old_over)
    (hd / "old-overdeclared-d.jfd").write_bytes(
        _build_jfd_disk(old_over, "D"))
    old_hole = _build_jfd_disk(old_over, "D")
    old_sector_table = struct.unpack_from("<I", old_hole, 28)[0]
    struct.pack_into("<I", old_hole, old_sector_table + 8 + 4, 0xffffffff)
    (hd / "old-overdeclared-hole-d.jfd").write_bytes(old_hole)
    dd_image = build_adf(0xA5, b"ARCDROID MEDIA DISC ONE v1\0")
    (hd / "dd-sibling.adf").write_bytes(dd_image)
    (hd / "f-boot.m3u").write_text("valid-f.adf\ndd-sibling.adf\n")
    (hd / "f-sibling.m3u").write_text("dd-sibling.adf\nvalid-f.adf\n")
    with zipfile.ZipFile(hd / "f-boot.zip", "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("Disc1.adf", f_image)
        archive.writestr("Disc2.adf", dd_image)
    with zipfile.ZipFile(hd / "f-sibling.zip", "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("Disc1.adf", dd_image)
        archive.writestr("Disc2.adf", f_image)
    f_folder = hd / "Fset.adf"
    f_folder.mkdir(exist_ok=True)
    (f_folder / "Fset.adf").write_bytes(f_image)
    (f_folder / "Fset 2.adf").write_bytes(dd_image)
    dd_folder = hd / "DDset.adf"
    dd_folder.mkdir(exist_ok=True)
    (dd_folder / "DDset.adf").write_bytes(dd_image)
    (dd_folder / "DDset 2.adf").write_bytes(f_image)
    for base in ("single-l.adl", "single-d-hugo.adf", "single-e-hugo.adf"):
        payload, geometry = launch_images[base]
        stem = base.rsplit(".", 1)[0]
        (launch / f"{stem}.jfd").write_bytes(
            build_jfd_family([], b"ArcDroid application fixture",
                             disk_payload=payload, disk_geometry=geometry))
        (launch / f"{stem}-extended.jfd").write_bytes(
            build_extended_jfd(payload, b"ArcDroid application fixture",
                               disk_geometry=geometry))
    stale_payload, stale_geometry = launch_images["stale-d.adf"]
    (launch / "stale-d.jfd").write_bytes(
        build_jfd_family([], b"ArcDroid stale-name fixture",
                         disk_payload=stale_payload,
                         disk_geometry=stale_geometry))
    for base in ("short-name-attr-l.adl", "top-bit-d.adf", "hard-space-e.adf",
                 "full-ten-d.adf", "attr-88-d.adf", "irrelevant-extent-e.adf",
                 "incomplete-l.adl", "incomplete-d.adf", "incomplete-e.adf",
                 "label-with-unknown-d.adf", "label-is-unknown-d.adf",
                 "duplicate-root-d.adf", "duplicate-root-file-d.adf",
                 "duplicate-run-d.adf", "duplicate-boot-e.adf",
                 "has-boot-d.adf", "self-boot-unknown-d.adf", "illegal-l.adl",
                 "nested-literal-d.adf", "nested-extended-literal-d.adf"):
        payload, geometry = launch_images[base]
        stem = base.rsplit(".", 1)[0]
        (launch / f"{stem}.jfd").write_bytes(
            build_jfd_family([], b"ArcDroid launch robustness fixture",
                             disk_payload=payload, disk_geometry=geometry))
    checkpoint_guest = build_checkpoint_guest_disc()
    checkpoint_names = build_checkpoint_name_discs()
    (launch / "checkpoint-a-guest.adf").write_bytes(checkpoint_guest)
    (launch / "checkpoint-nested-literal.adf").write_bytes(
        build_nested_checkpoint_guest_disc(b"Folder"))
    (launch / "checkpoint-nested-extended.adf").write_bytes(
        build_nested_checkpoint_guest_disc(b"Arc\xa0Games"))
    for filename, image in checkpoint_names.items():
        (launch / filename).write_bytes(image)
    if args.checkpoint_guest_disc:
        args.checkpoint_guest_disc.parent.mkdir(parents=True, exist_ok=True)
        args.checkpoint_guest_disc.write_bytes(checkpoint_guest)
    if args.checkpoint_name_discs:
        args.checkpoint_name_discs.mkdir(parents=True, exist_ok=True)
        for filename, image in checkpoint_names.items():
            (args.checkpoint_name_discs / filename).write_bytes(image)
    media_rom = build_rom(media_exerciser=True)
    (args.output / "arcdroid-short-tail-wd.rom").write_bytes(
        build_rom(media_exerciser=True, jfd_save_probe=True,
                  short_tail_probe=True))
    jfd_save_rom = build_rom(media_exerciser=True, jfd_save_probe=True)
    newio_jfd_save_rom = build_rom(media_exerciser=True, jfd_save_probe=True,
                                   newio_save_probe=True)
    disc1 = build_adf(0xA5, b"ARCDROID MEDIA DISC ONE v1\0")
    disc2 = build_adf(0x3C, b"ARCDROID MEDIA DISC TWO v1\0")
    (args.output / "arcdroid-media-test.rom").write_bytes(media_rom)
    (args.output / "arcdroid-jfd-save-test.rom").write_bytes(jfd_save_rom)
    (args.output / "arcdroid-jfd-newio-save-test.rom").write_bytes(newio_jfd_save_rom)
    (args.output / "arcdroid-jfd-create-test.rom").write_bytes(
        build_rom(media_exerciser=True, jfd_save_probe=True,
                  write_sector=3, creation_probe=True))
    (args.output / "arcdroid-jfd-newio-create-test.rom").write_bytes(
        build_rom(media_exerciser=True, jfd_save_probe=True,
                  newio_save_probe=True, creation_probe=True))
    (args.output / "arcdroid-jfd-merge-test.rom").write_bytes(
        build_rom(media_exerciser=True, jfd_save_probe=True,
                  write_sector=3, creation_probe=True, virtual_merge_probe=True,
                  ))
    (args.output / "arcdroid-jfd-newio-merge-test.rom").write_bytes(
        build_rom(media_exerciser=True, jfd_save_probe=True,
                  newio_save_probe=True, creation_probe=True,
                  virtual_merge_probe=True))
    # Fresh process proof reads committed data without a prior write.
    (args.output / "arcdroid-jfd-merge-readback-test.rom").write_bytes(
        build_rom(media_exerciser=True, jfd_save_probe=True,
                  write_sector=3, creation_probe=True, virtual_merge_probe=True))
    (args.output / "arcdroid-jfd-newio-merge-readback-test.rom").write_bytes(
        build_rom(media_exerciser=True, jfd_save_probe=True,
                  newio_save_probe=True, creation_probe=True,
                  virtual_merge_probe=True))
    # Reads a different omitted slot, so a valid saved ID 3 can coexist with
    # an RNF guest probe when a damaged slot suppresses the virtual latch.
    (args.output / "arcdroid-jfd-merge-missing-test.rom").write_bytes(
        build_rom(media_exerciser=True, jfd_save_probe=True,
                  write_sector=3, creation_probe=True,
                  virtual_merge_probe=True, merge_read_sector=1))
    (args.output / "arcdroid-jfd-newio-merge-missing-test.rom").write_bytes(
        build_rom(media_exerciser=True, jfd_save_probe=True,
                  newio_save_probe=True, creation_probe=True,
                  virtual_merge_probe=True, merge_read_sector=1))
    (args.output / "arcdroid-media-disc1.adf").write_bytes(disc1)
    (args.output / "arcdroid-media-disc2.adf").write_bytes(disc2)
    (args.output / "arcdroid-media.m3u").write_text(
        "#EXTM3U\narcdroid-media-disc1.adf\narcdroid-media-disc2.adf\n",
        encoding="utf-8", newline="\n")
    folder_set = args.output / "Qaventra.adf"
    folder_set.mkdir(exist_ok=True)
    (folder_set / "Qaventra.adf").write_bytes(disc1)
    (folder_set / "Qaventra 2.adf").write_bytes(disc2)
    folder_adl = build_old_map_media(640 * 1024, 2560, 0x53)
    (folder_set / "Qaventra 3.adl").write_bytes(folder_adl)
    (folder_set / "Qaventra 4.jfd").write_bytes(
        build_jfd_family([(0, 0, 3, 1, 0, bytes(128))],
                         b"ArcDroid structurally invalid folder JFD"))
    (folder_set / "Qaventra 5.adl").write_bytes(folder_adl[:-256])
    (folder_set / "Qaventra 9.adf").write_bytes(disc1)
    (folder_set / "Qaventra 10.JFD").write_bytes(
        build_jfd(bytes(disc1[:256]), b"ArcDroid folder JFD"))
    (folder_set / ".Qaventra 1.adf").write_bytes(disc2)
    (folder_set / "Qaventra.m3u").write_text(
        "#EXTM3U\nQaventra.adf\nQaventra 2.adf\n",
        encoding="utf-8", newline="\n")
    nested_folder = folder_set / "Nested.adf"
    nested_folder.mkdir(exist_ok=True)
    (nested_folder / "Nested.adf").write_bytes(disc2)
    launch_folder_set = args.output / "Virelai.adf"
    launch_folder_set.mkdir(exist_ok=True)
    (launch_folder_set / "Virelai.adf").write_bytes(
        build_d_discovery([(b"!Virelai", True)], b"Virelai"))
    (launch_folder_set / "Virelai 2.adf").write_bytes(
        build_d_discovery([(b"!Nulvane", True)], b"Nulvane"))
    jfd = build_jfd(bytes(disc1[:256]))
    (args.output / "arcdroid-test.jfd").write_bytes(jfd)
    writes = args.output / "jfd-writes"
    writes.mkdir(exist_ok=True)
    writable = build_extended_jfd(bytes(disc1), b"ArcDroid JFD save probe",
                                   disk_geometry="D")
    struct.pack_into("<I", writable, 304, 2)
    (writes / "rw-d.jfd").write_bytes(writable)
    newio_writable = build_extended_jfd(bytes(disc1),
                                        b"ArcDroid JFD NewIO save probe",
                                        disk_geometry="D")
    struct.pack_into("<I", newio_writable, 304, 2)
    (writes / "rw-newio-d.jfd").write_bytes(newio_writable)
    # The map and directory are captured; ID 3 is omitted on track-side 0.
    # Track-side 1 is full, providing the P-DD5 profile vote.
    # The legacy creation ROM reads its omitted target before writing. Give
    # it a valid base map that already allocates that target; the free-slot
    # probe/veto sequence has its own C-11c fixture and ROM below.
    create_base = build_d_discovery([], b"CreateProbe")
    create = build_pdd5_jfd(bytes(create_base),
        {0: [0, 2, 4], 1: list(range(5))},
        title=b"ArcDroid omitted-sector save probe")
    (writes / "rw-create-d.jfd").write_bytes(create)
    create_wp = bytearray(create)
    struct.pack_into("<I", create_wp, 304, 3)
    (writes / "wp-create-d.jfd").write_bytes(create_wp)
    # The omitted merge target (logical sector 3) is already allocated by
    # this old map; no prior write is needed to make its zero fill readable.
    allocated_base = build_d_discovery([], b"AllocatedMerge")
    merge_base = bytearray(allocated_base)
    _store_u24(merge_base, 0, 4)  # ID 1 is free; omitted ID 3 is allocated
    _store_u24(merge_base, 256, 4)
    merge_base[510] = 3
    merge_base[255] = _old_map_checksum(merge_base[:255])
    merge_base[511] = _old_map_checksum(merge_base[256:511])
    merge = build_pdd5_jfd(bytes(merge_base),
        {0: [0, 2, 4], 1: list(range(5))},
        title=b"ArcDroid allocated-sector merge probe")
    (writes / "rw-merge-d.jfd").write_bytes(merge)
    merge_wp = bytearray(merge)
    struct.pack_into("<I", merge_wp, 304, 3)
    (writes / "wp-merge-d.jfd").write_bytes(merge_wp)
    allocation = args.output / "jfd-allocation"
    allocation.mkdir(exist_ok=True)
    full = list(range(5))
    (allocation / "old-allocated.jfd").write_bytes(build_pdd5_jfd(
        bytes(allocated_base), {0: [0, 2, 4], 1: full}))
    old_no_map = bytearray(allocated_base)
    old_no_map[255] ^= 1
    (allocation / "old-no-map.jfd").write_bytes(build_pdd5_jfd(
        bytes(old_no_map), {0: [0, 2, 4], 1: full}))
    old_free = build_d_discovery([], b"FreeProbe")
    _store_u24(old_free, 0, 12)  # logical sector 3 starts at byte 3072
    _store_u24(old_free, 256, 3188)
    old_free[510] = 3
    old_free[255] = _old_map_checksum(old_free[:255])
    old_free[511] = _old_map_checksum(old_free[256:511])
    (allocation / "old-free.jfd").write_bytes(build_pdd5_jfd(
        bytes(old_free), {0: [0, 2, 4], 1: full}))
    (allocation / "old-dual-free.jfd").write_bytes(build_pdd5_jfd(
        bytes(old_free), {0: [0, 2], 1: full}))
    (identification / "sparse-d-timed.jfd").write_bytes(build_pdd5_jfd(
        bytes(old_free), {0: [0, 1, 2], 1: full},
        {0: {0: 0, 1: 40, 2: 80}}))
    (writes / "rw-identification-d.jfd").write_bytes(build_pdd5_jfd(
        bytes(old_free), {0: [0, 1, 2, 4], 1: full},
        title=b"ArcDroid identification save probe"))
    partial_first = bytearray(old_free)
    _store_u24(partial_first, 256, 2)  # First 512 bytes of ID 3 only.
    partial_first[511] = _old_map_checksum(partial_first[256:511])
    (identification / "sparse-d-partial-first.jfd").write_bytes(
        build_pdd5_jfd(bytes(partial_first), {0: [0, 1, 2, 4], 1: full}))
    (identification / "sparse-d-untimed.jfd").write_bytes(build_pdd5_jfd(
        bytes(old_free), {0: [0, 1, 2], 1: full}))
    (identification / "sparse-d-unplaceable.jfd").write_bytes(build_pdd5_jfd(
        bytes(old_free), {0: [0, 1, 2], 1: full},
        {0: {0: 0, 1: 70, 2: 140}}))
    old_tail = bytearray(old_free)
    _store_u24(old_tail, 252, 16)
    _store_u24(old_tail, 256, 4)
    old_tail[255] = _old_map_checksum(old_tail[:255])
    old_tail[511] = _old_map_checksum(old_tail[256:511])
    (identification / "sparse-d-outside.jfd").write_bytes(build_pdd5_jfd(
        bytes(old_tail), {0: [0, 1, 2], 1: full},
        {0: {0: 0, 1: 40, 2: 80}}))
    (identification / "sparse-d-coincident.jfd").write_bytes(build_pdd5_jfd(
        bytes(old_free), {0: [0, 1, 2], 1: full},
        {0: {0: 0, 1: 200, 2: 80}}))
    for stem, extents in (
        ("interior", ((12, 3188),)),
        ("crossing", ((12, 3189),)),
        ("exterior", ((12, 3188), (3201, 1))),
        ("bad-exterior", ((12, 3188), (3201, 2))),
    ):
        clamped = bytearray(old_free)
        _store_u24(clamped, 252, 3202)
        for index, (first, length) in enumerate(extents):
            _store_u24(clamped, index * 3, first)
            _store_u24(clamped, 256 + index * 3, length)
        clamped[510] = len(extents) * 3
        clamped[255] = _old_map_checksum(clamped[:255])
        clamped[511] = _old_map_checksum(clamped[256:511])
        (identification / f"sparse-d-clamped-{stem}.jfd").write_bytes(
            build_pdd5_jfd(bytes(clamped), {0: [0, 1, 2], 1: full},
                            {0: {0: 0, 1: 40, 2: 80}}))
    (identification / "complete-d.jfd").write_bytes(build_pdd5_jfd(
        bytes(old_free), {0: full, 1: full}))
    l_image = build_old_map_media(655360, 2560, 0x42)
    (identification / "legacy-l.jfd").write_bytes(build_extended_jfd(
        bytes(l_image), disk_geometry="L"))
    partial_free = bytearray(old_free)
    _store_u24(partial_free, 0, 13)  # only 768 bytes of omitted ID 3 are free
    _store_u24(partial_free, 256, 3187)
    partial_free[255] = _old_map_checksum(partial_free[:255])
    partial_free[511] = _old_map_checksum(partial_free[256:511])
    (allocation / "old-partial-slot.jfd").write_bytes(build_pdd5_jfd(
        bytes(partial_free), {0: [0, 2, 4], 1: full}))
    claim_map = bytearray(old_free[:1024])
    _store_u24(claim_map, 0, 16)
    _store_u24(claim_map, 256, 3184)
    claim_map[255] = _old_map_checksum(claim_map[:255])
    claim_map[511] = _old_map_checksum(claim_map[256:511])
    for machine, newio in (("", False), ("newio-", True)):
        for mode in ("veto", "claim", "read", "probe-write"):
            name = f"arcdroid-jfd-{machine}allocation-{mode}-test.rom"
            (args.output / name).write_bytes(build_rom(
                media_exerciser=True, jfd_save_probe=True,
                newio_save_probe=newio, write_payload=bytes(claim_map),
                allocation_mode=mode))
    new_base = build_e_discovery([], b"NewMapProbe")
    new_base[1024:2048] = new_base[:1024]
    (identification / "sparse-e-free.jfd").write_bytes(build_pdd5_jfd(
        bytes(new_base), {0: [0, 1, 2, 3], 1: full},
        {0: {0: 0, 1: 40, 2: 80, 3: 120}}))
    e_spare = bytearray(new_base)
    for copy_at in (0, 1024):
        block = bytearray(e_spare[copy_at:copy_at + 1024])
        _set_bits(block, 32 + 8192 - 0x0520, 1, 1)
        block[0] = _zone_check(block)
        e_spare[copy_at:copy_at + 1024] = block
    (identification / "sparse-e-spare-bit.jfd").write_bytes(build_pdd5_jfd(
        bytes(e_spare), {0: [0, 1, 2, 3], 1: full},
        {0: {0: 0, 1: 40, 2: 80, 3: 120}}))
    (identification / "sparse-e-allocated.jfd").write_bytes(build_pdd5_jfd(
        bytes(new_base), {0: [0, 1, 2, 4], 1: full},
        {0: {0: 0, 1: 40, 2: 80, 4: 160}}))
    e_copy_disagree = bytearray(new_base)
    backup = bytearray(e_copy_disagree[1024:2048])
    # ID 4 starts at byte 0x1000: the backup allocates its map bits.
    for bit in range(8, 23):
        backup[bit // 8] &= ~(1 << (bit & 7))
    _set_bits(backup, 8, 15, 560 - 8)
    _set_bits(backup, 544, 15, 3)
    _set_bits(backup, 559, 1, 1)
    backup[0] = _zone_check(backup)
    e_copy_disagree[1024:2048] = backup
    (identification / "sparse-e-copy-allocated.jfd").write_bytes(
        build_pdd5_jfd(bytes(e_copy_disagree), {0: [0, 1, 2, 3], 1: full},
                       {0: {0: 0, 1: 40, 2: 80, 3: 120}}))
    e_backup = bytearray(new_base)
    e_backup[0] ^= 1
    (identification / "sparse-e-backup.jfd").write_bytes(build_pdd5_jfd(
        bytes(e_backup), {0: [0, 1, 2, 3], 1: full},
        {0: {0: 0, 1: 40, 2: 80, 3: 120}}))
    e_conflict = bytearray(new_base)
    e_conflict[1024 + 10] = 1  # Record byte 6: addressing flags differ.
    e_conflict[1024] = _zone_check(bytearray(e_conflict[1024:2048]))
    (identification / "sparse-e-conflict.jfd").write_bytes(build_pdd5_jfd(
        bytes(e_conflict), {0: [0, 1, 2, 3], 1: full},
        {0: {0: 0, 1: 40, 2: 80, 3: 120}}))
    e_bad_link = bytearray(new_base)
    for copy_at in (0, 1024):
        block = bytearray(e_bad_link[copy_at:copy_at + 1024])
        _set_bits(block, 8, 15, 504)
        block[0] = _zone_check(block)
        e_bad_link[copy_at:copy_at + 1024] = block
    (identification / "sparse-e-bad-link.jfd").write_bytes(build_pdd5_jfd(
        bytes(e_bad_link), {0: [0, 1, 2, 3], 1: full},
        {0: {0: 0, 1: 40, 2: 80, 3: 120}}))
    (allocation / "new-allocated.jfd").write_bytes(build_pdd5_jfd(
        bytes(new_base), {0: [0, 1, 2, 4], 1: full}))
    (allocation / "new-free.jfd").write_bytes(build_pdd5_jfd(
        bytes(new_base), {0: full, 1: [1, 2, 3, 4], 2: full}))
    tail_defect = bytearray(new_base)
    tail_defect[14:16] = (0x0500).to_bytes(2, "little")
    _set_bits(tail_defect, 6912, 15, 1)  # 32 map bits beyond 800 KiB: ID 1
    _set_bits(tail_defect, 6943, 1, 1)
    sums = [0, 0, 0, 0]
    tail_defect[0] = 0
    for rover in range(1020, 0, -4):
        sums[0] += tail_defect[rover] + (sums[3] >> 8); sums[3] &= 0xFF
        sums[1] += tail_defect[rover + 1] + (sums[0] >> 8); sums[0] &= 0xFF
        sums[2] += tail_defect[rover + 2] + (sums[1] >> 8); sums[1] &= 0xFF
        sums[3] += tail_defect[rover + 3] + (sums[2] >> 8); sums[2] &= 0xFF
    sums[0] += sums[3] >> 8
    sums[1] += tail_defect[1] + (sums[0] >> 8)
    sums[2] += tail_defect[2] + (sums[1] >> 8)
    sums[3] += tail_defect[3] + (sums[2] >> 8)
    tail_defect[0] = (sums[0] ^ sums[1] ^ sums[2] ^ sums[3]) & 0xFF
    tail_defect[1024:2048] = tail_defect[:1024]
    (allocation / "new-tail-defect.jfd").write_bytes(build_pdd5_jfd(
        bytes(tail_defect), {0: full, 1: [1, 2, 3, 4], 2: full}))
    bad_primary = bytearray(new_base)
    bad_primary[0] ^= 1
    (allocation / "new-bad-primary.jfd").write_bytes(build_pdd5_jfd(
        bytes(bad_primary), {0: full, 1: [1, 2, 3, 4], 2: full}))
    claimed_e_map = build_e_discovery([(b"!Object", False)], b"NewMapProbe")[:1024]
    assert claimed_e_map[4:64] == new_base[4:64]
    disagree = bytearray(new_base)
    disagree[1024:2048] = claimed_e_map
    (allocation / "new-disagree.jfd").write_bytes(build_pdd5_jfd(
        bytes(disagree), {0: full, 1: [1, 2, 3, 4], 2: full}))
    bad_primary_allocated = bytearray(bad_primary)
    bad_primary_allocated[1024:2048] = claimed_e_map
    (allocation / "new-bad-primary-allocated.jfd").write_bytes(build_pdd5_jfd(
        bytes(bad_primary_allocated), {0: full, 1: [1, 2, 3, 4], 2: full}))
    bad_both = bytearray(bad_primary)
    bad_both[1024] ^= 1
    (allocation / "new-both-bad.jfd").write_bytes(build_pdd5_jfd(
        bytes(bad_both), {0: full, 1: [1, 2, 3, 4], 2: full}))
    newio_protected = bytearray(newio_writable)
    struct.pack_into("<I", newio_protected, 304, 3)
    (writes / "wp-newio-d.jfd").write_bytes(newio_protected)
    adffs_writable = build_pdd5_jfd(bytes(disc1),
        {0: list(range(5))}, title=b"ArcDroid ADFFS native-save fixture")
    struct.pack_into("<HHI", adffs_writable, 12, 1, 1, 123)
    (writes / "adffs-rw.jfd").write_bytes(adffs_writable)
    protected = bytearray(writable)
    struct.pack_into("<I", protected, 304, 3)
    (writes / "wp-d.jfd").write_bytes(protected)
    folder = writes / "SaveSet.jfd"
    folder.mkdir(exist_ok=True)
    (folder / "SaveSet.jfd").write_bytes(writable)
    (folder / "SaveSet 2.jfd").write_bytes(protected)
    neither = bytearray(writable)
    struct.pack_into("<I", neither, 304, 0)
    (writes / "neither-d.jfd").write_bytes(neither)
    (writes / "rw-d-gzip.jfd").write_bytes(gzip_member(writable))
    (writes / "legacy-d.jfd").write_bytes(
        _build_jfd_disk(bytes(disc1), "D", b"ArcDroid JFD legacy probe"))
    (writes / "mixed.adf").write_bytes(disc1)
    (writes / "mixed.m3u").write_text(
        "mixed.adf\nrw-d.jfd\nwp-d.jfd\n",
        encoding="utf-8", newline="\n")
    (writes / "rw-d.zip").write_bytes(_zip_bytes([("RW.jfd", bytes(writable), True)]))
    (writes / "missing-one-d.jfd").write_bytes(build_jfd_family(
        [(0xFF, 0, 2, 0, 0, bytes(1024))],
        b"ArcDroid JFD missing read sector", extended=True))
    creation = args.output / "jfd-creation"
    creation.mkdir(exist_ok=True)
    full = list(range(5))
    launch_created = build_d_discovery([(b"!NewApp", True)], b"CreatedApp")
    (creation / "launch-full.jfd").write_bytes(build_pdd5_jfd(
        bytes(launch_created), {0: full, 1: full}))
    (creation / "launch-partial.jfd").write_bytes(build_pdd5_jfd(
        bytes(launch_created), {0: [0, 2, 3, 4], 1: full}))
    launch_hidden = build_d_discovery(
        [(b"!Visible", True), (b"FolderA", False),
         (b"FolderB", False), (b"!Hidden", True)], b"HiddenSide")
    hidden_image = build_pdd5_jfd(bytes(launch_hidden),
                                  {0: full, 1: full, 2: full + [9]})
    at = pdd5_descriptor_offset(hidden_image, 2, 9)
    header = struct.unpack_from("<I", hidden_image, at)[0]
    struct.pack_into("<I", hidden_image, at,
                     (header & ~(0xF << 16)) | (8 << 16))
    (creation / "launch-nonmap-quarantine.jfd").write_bytes(hidden_image)
    (creation / "partial-untimed.jfd").write_bytes(build_pdd5_jfd(
        bytes(disc1), {0: full, 1: [0, 2, 4], 159: [0, 2, 4],
                       160: [0], 165: [0]}, track_count=166))
    (creation / "partial-edge.jfd").write_bytes(build_pdd5_jfd(
        bytes(disc1), {0: full, 1: [1, 2, 3]}))
    (creation / "partial-timed.jfd").write_bytes(build_pdd5_jfd(
        bytes(disc1), {0: full, 1: [0, 2, 4]},
        {1: {0: 0, 2: 80, 4: 160}}))
    (creation / "boundary.jfd").write_bytes(build_pdd5_jfd(
        bytes(disc1), {0: full, 1: [0]}, {1: {0: 190}}))
    (creation / "unschedulable.jfd").write_bytes(build_pdd5_jfd(
        bytes(disc1), {0: full, 1: [0, 2, 4]},
        {1: {0: 0, 2: 70, 4: 140}}))
    (creation / "timed-rounded.jfd").write_bytes(build_pdd5_jfd(
        bytes(disc1), {0: full, 1: [0, 1, 2, 3]},
        {1: {0: 0, 1: 37, 2: 73, 3: 110}}))
    short_full = {track: full for track in range(100)}
    (creation / "short-full.jfd").write_bytes(build_pdd5_jfd(
        bytes(disc1), short_full, track_count=100))
    short_partial = dict(short_full)
    short_partial[1] = [0, 2, 4]
    (creation / "short-partial.jfd").write_bytes(build_pdd5_jfd(
        bytes(disc1), short_partial, track_count=100))
    (creation / "nonmatching.jfd").write_bytes(build_pdd5_jfd(
        bytes(disc1), {0: [0, 2, 4]}))
    for stem, free_start, free_length in (
        ("irregular-free", 20, 3180),
        ("irregular-allocated", 20, 1000)):
        mapped = build_d_discovery([], b"GateMap")
        _store_u24(mapped, 0, free_start)
        _store_u24(mapped, 256, free_length)
        mapped[510] = 3
        mapped[255] = _old_map_checksum(mapped[:255])
        mapped[511] = _old_map_checksum(mapped[256:511])
        (creation / f"{stem}.jfd").write_bytes(build_pdd5_jfd(
            bytes(mapped), {0: full, 1: full, 159: [9]}))
    # All five IDs are present on side 159, but ID 4 cannot accept a write.
    # The base ADFS map decides whether that missing writable slot is safe.
    for defect in ("id-only", "bad-crc"):
        for extent, free_length in (("free", 3180), ("allocated", 1000)):
            mapped = build_d_discovery([], b"GateWritableID")
            _store_u24(mapped, 0, 20)
            _store_u24(mapped, 256, free_length)
            mapped[510] = 3
            mapped[255] = _old_map_checksum(mapped[:255])
            mapped[511] = _old_map_checksum(mapped[256:511])
            image = build_pdd5_jfd(bytes(mapped),
                                   {0: full, 1: full, 159: full})
            descriptor = pdd5_descriptor_offset(image, 159, 4)
            if defect == "id-only":
                struct.pack_into("<I", image, descriptor + 4, 0xFFFFFFFF)
            else:
                header = struct.unpack_from("<I", image, descriptor)[0]
                struct.pack_into("<I", image, descriptor, header | (2 << 4))
            (creation / f"irregular-{defect}-{extent}.jfd").write_bytes(image)
    mapped = build_d_discovery([], b"GateSeq")
    _store_u24(mapped, 0, 20)
    _store_u24(mapped, 256, 3180)
    mapped[510] = 3
    mapped[255] = _old_map_checksum(mapped[:255])
    mapped[511] = _old_map_checksum(mapped[256:511])
    new_mapped = build_e_discovery([], b"GateNew")
    (creation / "irregular-newmap-free.jfd").write_bytes(build_pdd5_jfd(
        bytes(new_mapped), {0: full, 1: full, 159: [9]}))
    backup_only = bytearray(new_mapped)
    backup_only[1024:2048] = backup_only[:1024]
    backup_only[0] ^= 1
    (creation / "irregular-newmap-backup-free.jfd").write_bytes(
        build_pdd5_jfd(bytes(backup_only),
                        {0: full, 1: full, 159: [9]}))
    (creation / "irregular-free-sequence.jfd").write_bytes(build_pdd5_jfd(
        bytes(mapped), {0: [0, 2, 4], 1: full, 159: [9]}))
    (creation / "half-vote.jfd").write_bytes(build_pdd5_jfd(
        bytes(disc1), {0: full, 1: full + [9]},
        {1: {0: 0, 1: 32, 2: 64, 3: 96, 4: 128, 9: 160}}))
    # A valid old map allocates every quarantined side. All 160 sides have
    # captured data, so 80 compatible sides are exactly half the vote.
    tie_map = build_d_discovery([], b"GateTie")
    _store_u24(tie_map, 0, 20)
    _store_u24(tie_map, 256, 1000)
    tie_map[510] = 3
    tie_map[255] = _old_map_checksum(tie_map[:255])
    tie_map[511] = _old_map_checksum(tie_map[256:511])
    tie = build_pdd5_jfd(bytes(tie_map), {side: full for side in range(160)})
    for side in range(80, 160):
        at = pdd5_descriptor_offset(tie, side, 4)
        header = struct.unpack_from("<I", tie, at)[0]
        struct.pack_into("<I", tie, at, (header & ~(0xF << 16)) | (8 << 16))
    (creation / "quarantine-majority-tie.jfd").write_bytes(tie)
    (creation / "irregular-all-ids.jfd").write_bytes(build_pdd5_jfd(
        bytes(disc1), {0: full, 1: full + [9], 2: full},
        {1: {0: 0, 1: 32, 2: 64, 3: 96, 4: 128, 9: 160}}))
    # Two independent launch identities exercise record reuse after a guest
    # sector write. The rename ROM carries only our generated root sector;
    # its write changes !Alpha to !Alphb without touching the child !Run.
    for stem, name in (("launch-unchanged", b"Unchanged"),
                       ("launch-renamed", b"Renamed")):
        launch_payload = build_d_discovery(
            [(b"!Alpha", True), (b"!Beta", True)], name)
        launch_jfd = build_extended_jfd(
            bytes(launch_payload), b"ArcDroid JFD launch write probe",
            disk_geometry="D")
        struct.pack_into("<I", launch_jfd, 304, 2)
        (writes / f"{stem}-d.jfd").write_bytes(launch_jfd)
        if stem == "launch-renamed":
            changed_root_sector = bytearray(launch_payload[1024:2048])
            changed_root_sector[5 + 5] = ord("b")
    (args.output / "arcdroid-jfd-directory-benign.rom").write_bytes(
        build_rom(media_exerciser=True, write_sector=3))
    (args.output / "arcdroid-jfd-directory-rename.rom").write_bytes(
        build_rom(media_exerciser=True,
                  write_payload=bytes(changed_root_sector)))
    build_zip_content_fixtures(args.output / "zip-content", bytes(disc1),
                               bytes(disc2), bytes(jfd),
                               (args.output / "adffs/eligible.jfd").read_bytes(),
                               (args.output / "adffs/too-large.jfd").read_bytes(),
                               (args.output / "adffs/decoded-too-large.jfd").read_bytes())
    (args.output / "arcdroid-test-gzip.jfd").write_bytes(gzip_member(jfd))
    extended_jfd = build_extended_jfd(bytes(disc1[:256]))
    two_member = gzip_member(extended_jfd[:316]) + gzip_member(extended_jfd[316:])
    body_midpoint = 316 + (len(extended_jfd) - 316) // 2
    three_member = (gzip_member(extended_jfd[:316]) +
                    gzip_member(extended_jfd[316:body_midpoint]) +
                    gzip_member(extended_jfd[body_midpoint:]))
    (args.output / "arcdroid-test-two-member.jfd").write_bytes(two_member)
    (args.output / "arcdroid-test-three-member.jfd").write_bytes(three_member)
    family = args.output / "jfd-family"
    family.mkdir(exist_ok=True)
    for size_code in range(4):
        payload = bytes(((i * 17 + size_code) & 0xFF) for i in range(128 << size_code))
        (family / f"size-{size_code}.jfd").write_bytes(
            build_jfd_family([(0, 0, 2, size_code, 0, payload)]))
    (family / "single-density.jfd").write_bytes(
        build_jfd_family([(0, 0, 1, 0, 0, bytes(range(128)))]))
    (family / "timed-duplicates.jfd").write_bytes(build_jfd_family([
        (10, 0, 2, 7, 0, bytes([0x11]) * 256),
        (20, 0, 2, 7, 0, bytes([0x22]) * 256)]))
    (family / "wrap-equal-times.jfd").write_bytes(build_jfd_family([
        (199, 0, 2, 1, 0, bytes([0x33]) * 128),
        (0, 0, 2, 2, 0, bytes([0x44]) * 128),
        (0, 0, 2, 3, 0, bytes([0x55]) * 128)]))
    (family / "untimed.jfd").write_bytes(build_jfd_family([
        (0xFF, 0, 2, 1, 0, bytes([0x66]) * 128),
        (0xFF, 0, 2, 2, 0, bytes([0x77]) * 256)]))
    (family / "write-two.jfd").write_bytes(build_jfd_family([
        (0xFF, 0, 2, 3, 0, bytes(128)),
        (0xFF, 0, 2, 4, 0, bytes(128))]))
    (family / "write-slow.jfd").write_bytes(build_jfd_family([
        (10, 0, 2, 3, 8, bytes(128))]))
    (family / "write-duplicate.jfd").write_bytes(build_jfd_family([
        (10, 0, 2, 3, 0, bytes([0x11]) * 128),
        (20, 0, 2, 3, 0, bytes([0x22]) * 128)]))
    shared = build_jfd_family([
        (10, 0, 2, 3, 0, bytes([0x11]) * 128),
        (20, 0, 2, 4, 0, bytes([0x22]) * 128)])
    struct.pack_into("<I", shared, 324, 0)
    (family / "write-shared.jfd").write_bytes(shared)
    (family / "quad-data.jfd").write_bytes(build_jfd_family([
        (0xff, 0, 4, 3, 0, bytes([0x5a]) * 1024)]))
    (family / "quad-timed.jfd").write_bytes(build_jfd_family([
        (17, 0, 4, 3, 0, bytes([0x5a]) * 1024)]))
    (family / "quad-id-only.jfd").write_bytes(build_jfd_family([
        (0, 0, 4, 3, 0, None)]))
    (family / "quad-id-crc.jfd").write_bytes(build_jfd_family([
        (0, 0, 4, 3, 1, bytes([0x5a]) * 1024)]))
    (family / "quad-data-crc.jfd").write_bytes(build_jfd_family([
        (0, 0, 4, 3, 2, bytes([0x5a]) * 1024)]))
    (family / "mixed-density.jfd").write_bytes(build_jfd_family([
        (0xff, 0, 2, 1, 0, bytes([0x33]) * 128),
        (0xff, 0, 4, 3, 0, bytes([0x5a]) * 1024)]))
    for name, crc, payload in (
        ("id-crc", 1, bytes([0x88]) * 128),
        ("data-crc", 2, bytes([0x99]) * 128),
        ("both-crc", 3, bytes([0xAA]) * 128),
        ("id-only", 0, None),
        ("data-crc-id-only", 2, None)):
        (family / f"{name}.jfd").write_bytes(
            build_jfd_family([(0, 0, 2, 4, crc, payload)]))
    (family / "blank-track.jfd").write_bytes(build_jfd_family([]))
    rejects = {
        "reject-discop3.jfd": [(0, 1, 2, 1, 0, bytes(128))],
        "reject-density8.jfd": [(0, 0, 8, 1, 0, bytes(128))],
        "reject-mixed-time.jfd": [(0, 0, 2, 1, 0, bytes(128)),
                                  (0xFF, 0, 2, 2, 0, bytes(128))],
        "reject-untimed-duplicate.jfd": [(0xFF, 0, 2, 1, 0, bytes(128)),
                                         (0xFF, 0, 2, 1, 0, bytes(128))],
        "reject-long-schedule.jfd": [(0xFF, 0, 1, i, 0, bytes(1024)) for i in range(3)],
    }
    for name, descriptors in rejects.items():
        (family / name).write_bytes(build_jfd_family(descriptors))
    # Project-authored two-sided captures: a late native-model fault hides
    # the entire first side while the second remains readable.
    late = build_jfd_family([
        (0, 0, 2, 1, 0, bytes([0x41]) * 128),
        (0, 0, 2, 2, 0, bytes([0x42]) * 2048)],
        second_descriptors=((0, 0, 2, 3, 0, bytes([0x43]) * 128),))
    (family / "quarantine-late.jfd").write_bytes(late)
    (family / "quarantine-late.gz").write_bytes(gzip_member(late))
    (family / "quarantine-first.jfd").write_bytes(build_jfd_family([
        (0, 0, 8, 1, 0, bytes(128)),
        (0, 1, 2, 2, 0, bytes(2048))],
        second_descriptors=((0, 0, 2, 3, 0, bytes(128)),)))
    (family / "quarantine-same-side-invalid.jfd").write_bytes(build_jfd_family([
        (0, 0, 2, 1, 0, bytes(2048)),
        (0, 0, 3, 2, 0, bytes(128))]))
    (family / "quarantine-quad.jfd").write_bytes(build_jfd_family(
        [(0, 0, 4, 1, 0, bytes(2048))],
        second_descriptors=((0, 0, 2, 2, 0, bytes(128)),)))
    (family / "quarantine-quad-usable.jfd").write_bytes(build_jfd_family(
        [(0, 0, 4, 1, 0, bytes(2048))],
        second_descriptors=((0, 0, 4, 2, 0, bytes(1024)),)))
    guest = build_pdd5_jfd(bytes([0x5A]) * 819200,
                           {158: [3, 4], 159: [3]})
    guest_header_at = pdd5_descriptor_offset(guest, 158, 4)
    guest_header = struct.unpack_from("<I", guest, guest_header_at)[0]
    struct.pack_into("<I", guest, guest_header_at,
                     (guest_header & ~(0xF << 16)) | (8 << 16))
    (family / "quarantine-guest.jfd").write_bytes(guest)
    with zipfile.ZipFile(family / "quarantine-guest.zip", "w",
                         zipfile.ZIP_STORED) as archive:
        archive.writestr("Disc1.jfd", guest)
        archive.writestr("Disc2.jfd", build_pdd5_jfd(
            bytes([0x5A]) * 819200, {159: [3]}))
    (args.output / "arcdroid-jfd-quarantine.rom").write_bytes(
        build_rom(quarantine_probe=True))
    large = build_jfd_family([(0, 0, 2, 1, 0, bytes(128 << 15))],
        second_descriptors=((0, 0, 2, 2, 0, bytes(1024)),))
    large_second = 312 + 8 + 4
    struct.pack_into("<I", large, large_second + 4, 4096)
    (family / "quarantine-large-alias.jfd").write_bytes(large)
    for name, change in (("invalid-density", 3 << 16),
                         ("invalid-time", 201 << 24),
                         ("invalid-time-254", 254 << 24)):
        damaged = bytearray(late)
        at = 312 + 2 * 8 + 4
        header = struct.unpack_from("<I", damaged, at)[0]
        mask = 0xF << 16 if name == "invalid-density" else 0xFF << 24
        struct.pack_into("<I", damaged, at, (header & ~mask) | change)
        (family / f"quarantine-then-{name}.jfd").write_bytes(damaged)
    damaged = bytearray(late)
    struct.pack_into("<I", damaged, 312 + 2 * 8 + 4 + 4, len(damaged))
    (family / "quarantine-then-bad-payload.jfd").write_bytes(damaged)
    damaged = bytearray(large)
    struct.pack_into("<I", damaged, 312 + 4, 1025)
    (family / "quarantine-large-out-of-bounds.jfd").write_bytes(damaged)
    near = [(0xff, 0, 1, 1, 0, bytes(1024)),
            (0xff, 0, 1, 2, 0, bytes(1024)),
            (0xff, 0, 2, 3, 0, bytes(1024)),
            (0xff, 0, 4, 4, 0, bytes(1024))]
    (family / "schedule-below.jfd").write_bytes(build_jfd_family(near))
    over = [(0xff, 0, 2, 1, 0, bytes(128)),
            (0xff, 0, 1, 2, 0, bytes(256)),
            (0xff, 0, 1, 3, 0, bytes(256)),
            (0xff, 0, 1, 4, 0, bytes(1024)),
            (0xff, 0, 1, 5, 0, bytes(1024))]
    (family / "schedule-over.jfd").write_bytes(build_jfd_family(over))
    delta = build_jfd_family([(0, 0, 2, 1, 0, bytes(128))])
    struct.pack_into("<I", delta, 36, 4)
    (family / "reject-delta.jfd").write_bytes(delta)
    # The Founder's private image is never used: short fixtures are truncations
    # of this project-authored descriptor-bearing E-format image.
    short_e = bytearray(ADF_SIZE)
    record = short_e[4:64]
    record[0:6] = bytes((10, 5, 2, 2, 15, 7))
    record[6] = 1; record[9] = 1
    record[10:12] = (0x0520).to_bytes(2, "little")
    record[12:16] = (0x00000203).to_bytes(4, "little")
    record[16:20] = ADF_SIZE.to_bytes(4, "little")
    record[20:22] = (0x1234).to_bytes(2, "little")
    record[22:32] = b"ArcShortE1"
    short_e[4:64] = record
    short_e[3] = 0xFF
    # Same FileCore zone-check schedule as the independent C fixture.
    sums = [0, 0, 0, 0]
    rover = 1020
    while rover > 0:
        sums[0] += short_e[rover] + (sums[3] >> 8); sums[3] &= 0xFF
        sums[1] += short_e[rover + 1] + (sums[0] >> 8); sums[0] &= 0xFF
        sums[2] += short_e[rover + 2] + (sums[1] >> 8); sums[1] &= 0xFF
        sums[3] += short_e[rover + 3] + (sums[2] >> 8); sums[2] &= 0xFF
        rover -= 4
    sums[0] += sums[3] >> 8
    sums[1] += short_e[1] + (sums[0] >> 8)
    sums[2] += short_e[2] + (sums[1] >> 8)
    sums[3] += short_e[3] + (sums[2] >> 8)
    short_e[0] = (sums[0] ^ sums[1] ^ sums[2] ^ sums[3]) & 0xFF
    for missing in (1024, 10240, 10241, 20480, 20481, 21504, 513):
        (args.output / f"short-e-missing-{missing}.adf").write_bytes(short_e[:-missing])
    short_family = args.output / "short-family"
    short_family.mkdir(exist_ok=True)
    short_d = build_old_map_media(800 * 1024, 3200, 0xD0)
    _store_u24(short_d, 0, 12)  # Map, boot block and root are allocated.
    _store_u24(short_d, 256, 3188)
    short_d[255] = _old_map_checksum(short_d[:255])
    short_d[511] = _old_map_checksum(short_d[256:511])
    geometries = (
        ("320k", build_old_map_media(320 * 1024, 1280, 0x31), 4096, 256, "adf"),
        ("640k-l", build_old_map_media(640 * 1024, 2560, 0x64), 8192, 256, "adl"),
        ("800k-d", short_d, 20480, 1024, "adf"),
        ("800k-e", short_e, 20480, 1024, "adf"),
    )
    for name, full, cap, sector_size, extension in geometries:
        (short_family / f"{name}-full.{extension}").write_bytes(full)
        cases = {
            "one-sector": sector_size,
            "cap": cap,
            "cap-plus-one": cap + 1,
            "cap-plus-sector": cap + sector_size,
            "non-aligned": sector_size + 1,
        }
        for case, missing in cases.items():
            (short_family / f"{name}-{case}.{extension}").write_bytes(full[:-missing])
        bad_descriptor = bytearray(full[:-sector_size])
        if name == "800k-e":
            bad_descriptor[8] = 12
            bad_descriptor[0] = 0
            sums = [0, 0, 0, 0]
            rover = 1020
            while rover > 0:
                sums[0] += bad_descriptor[rover] + (sums[3] >> 8); sums[3] &= 0xFF
                sums[1] += bad_descriptor[rover + 1] + (sums[0] >> 8); sums[0] &= 0xFF
                sums[2] += bad_descriptor[rover + 2] + (sums[1] >> 8); sums[1] &= 0xFF
                sums[3] += bad_descriptor[rover + 3] + (sums[2] >> 8); sums[2] &= 0xFF
                rover -= 4
            sums[0] += sums[3] >> 8
            sums[1] += bad_descriptor[1] + (sums[0] >> 8)
            sums[2] += bad_descriptor[2] + (sums[1] >> 8)
            sums[3] += bad_descriptor[3] + (sums[2] >> 8)
            bad_descriptor[0] = (sums[0] ^ sums[1] ^ sums[2] ^ sums[3]) & 0xFF
        else:
            _store_u24(bad_descriptor, 252, 11)
            bad_descriptor[255] = _old_map_checksum(bad_descriptor[:255])
        (short_family / f"{name}-bad-descriptor.{extension}").write_bytes(bad_descriptor)
        bad_checksum = bytearray(full[:-sector_size])
        bad_checksum[255] ^= 1
        (short_family / f"{name}-bad-checksum.{extension}").write_bytes(bad_checksum)

    # Two independently valid E map copies, with a free tail beyond D-064.
    free_e = build_e_discovery([], b"FreePrefix")
    free_e[1024:2048] = free_e[:1024]
    (short_family / "800k-e-free-map.adf").write_bytes(free_e[:-21504])
    cyclic_e = bytearray(free_e)
    for copy in (0, 1024):
        block = bytearray(cyclic_e[copy:copy + 1024])
        _set_bits(block, 544, 15, 1)  # next free link falls inside this fragment
        block[0] = _zone_check(block)
        cyclic_e[copy:copy + 1024] = block
    (short_family / "800k-e-cyclic-free.adf").write_bytes(cyclic_e[:-21504])
    unterminated_e = bytearray(free_e)
    for copy in (0, 1024):
        block = bytearray(unterminated_e[copy:copy + 1024])
        block[6911 // 8] &= ~(1 << (6911 & 7))
        block[0] = _zone_check(block)
        unterminated_e[copy:copy + 1024] = block
    (short_family / "800k-e-unterminated.adf").write_bytes(unterminated_e[:-21504])
    allocated_e = bytearray(free_e)
    for copy in (0, 1024):
        block = bytearray(allocated_e[copy:copy + 1024])
        for bit in range(8, 23):
            block[bit // 8] &= ~(1 << (bit & 7))
        block[0] = _zone_check(block)
        allocated_e[copy:copy + 1024] = block
    (short_family / "800k-e-allocated-tail.adf").write_bytes(allocated_e[:-21504])
    conflict_e = bytearray(free_e)
    block = bytearray(conflict_e[1024:2048])
    block[26] ^= 1  # Disc-record name: two valid, conflicting copies.
    block[0] = _zone_check(block)
    conflict_e[1024:2048] = block
    (short_family / "800k-e-conflicting-maps.adf").write_bytes(conflict_e[:-21504])
    allocation_conflict_e = build_e_discovery([(b"Folder", False)], b"FreePrefix")
    allocation_conflict_e[1024:2048] = allocation_conflict_e[:1024]
    block = bytearray(allocation_conflict_e[1024:2048])
    _set_bits(block, 546, 1, 1)  # Allocated child fragment 3 becomes 7; root and free tail agree.
    block[0] = _zone_check(block)
    allocation_conflict_e[1024:2048] = block
    (short_family / "800k-e-conflicting-allocation.adf").write_bytes(
        allocation_conflict_e[:-21504])
    directory_tail = build_d_guest_applications(
        [(b"!Demo", None, b"Synthetic run payload\r")], b"DiscPrefix")
    tail_first = ADF_SIZE - 2048
    directory_tail[tail_first:] = directory_tail[3072:5120]
    _store_u24(directory_tail, 1024 + 5 + 22, tail_first // 256)
    directory_tail[3072:5120] = bytes(2048)
    free_first = int.from_bytes(directory_tail[:3], "little")
    _store_u24(directory_tail, 256, tail_first // 256 - free_first)
    directory_tail[255] = _old_map_checksum(directory_tail[:255])
    directory_tail[511] = _old_map_checksum(directory_tail[256:511])
    (short_family / "discovery-directory-full.adf").write_bytes(directory_tail)
    (short_family / "discovery-directory-tail.adf").write_bytes(directory_tail[:-1024])
    payload_tail = build_d_guest_applications(
        [(b"!Demo", None, b"Synthetic run payload\r")], b"PayloadTail")
    run = b"Synthetic run payload\r"
    tail_start = ADF_SIZE - 1024
    payload_tail[tail_start:tail_start + len(run)] = run
    _store_u24(payload_tail, 3072 + 5 + 22, tail_start // 256)
    free_first = int.from_bytes(payload_tail[:3], "little")
    _store_u24(payload_tail, 256, tail_start // 256 - free_first)
    payload_tail[255] = _old_map_checksum(payload_tail[:255])
    payload_tail[511] = _old_map_checksum(payload_tail[256:511])
    (short_family / "discovery-payload-tail.adf").write_bytes(payload_tail[:-1024])


if __name__ == "__main__":
    main()
