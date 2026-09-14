#!/usr/bin/env python3
"""Generate ArcDroid's deterministic ARMv2 validation ROM and 800 KiB ADF.

The tiny assembler emits ARM2 words directly because current GCC and LLVM no
longer target ARMv2. The generated firmware configures MEMC/VIDC, reads sector
zero through WD1772, seeds the picture from that data, enables sound DMA, and
alternates two programmed field rates. No third-party firmware is used.

Copyright 2026 ArcDroid contributors. GPL-2.0-only.
"""
from pathlib import Path
import argparse
import struct

ROM_SIZE = 512 * 1024
ADF_SIZE = 800 * 1024


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

    def subs_imm(self, rd: int, rn: int, value: int) -> None:
        self.emit(0xE2500000 | (rn << 16) | (rd << 12) | self._imm(value))

    def eor(self, rd: int, rn: int, rm: int) -> None:
        self.emit(0xE0200000 | (rn << 16) | (rd << 12) | rm)

    def orr_lsl(self, rd: int, rn: int, rm: int, shift: int) -> None:
        self.emit(0xE1800000 | (rn << 16) | (rd << 12) | (shift << 7) | rm)

    def tst_imm(self, rn: int, value: int) -> None:
        self.emit(0xE3100000 | (rn << 16) | self._imm(value))

    def ldr_const(self, rd: int, value: int) -> None:
        index = len(self.words)
        self.emit(rd << 12)
        self.literals.append((index, value & 0xFFFFFFFF))

    def ldr(self, rd: int, rn: int, offset: int = 0, byte: bool = False) -> None:
        self.emit(0xE5900000 | (0x00400000 if byte else 0) |
                  (rn << 16) | (rd << 12) | offset)

    def str(self, rd: int, rn: int, offset: int = 0, byte: bool = False) -> None:
        self.emit(0xE5800000 | (0x00400000 if byte else 0) |
                  (rn << 16) | (rd << 12) | offset)

    def str_post(self, rd: int, rn: int, offset: int = 4) -> None:
        self.emit(0xE4800000 | (rn << 16) | (rd << 12) | offset)

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
        if len(self.words) * 4 >= 4096:
            raise ValueError("validation program exceeds its reserved page")
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


def build_rom() -> bytearray:
    rom = bytearray(b"\xFF" * ROM_SIZE)
    rom[0:4] = struct.pack("<I", 0xE59FF000)  # ldr pc, [pc]
    rom[8:12] = struct.pack("<I", 0x03800020)
    a = Arm2(0x03800020)

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

    # Select drive 0, side 0, motor on, double density; read track 0 sector 0.
    for address, value in ((0x03350040, 0x9E), (0x03350018, 0x00)):
        a.ldr_const(0, address)
        a.mov(1, value)
        a.str(1, 0, byte=True)
    a.ldr_const(0, 0x03310000)
    a.mov(1, 0)
    a.str(1, 0, 4, byte=True)
    a.str(1, 0, 8, byte=True)
    a.mov(1, 0x80)
    a.str(1, 0, byte=True)
    a.mov(5, 0x100)
    a.mov(6, 0)
    a.label("fdc_wait")
    a.ldr(7, 0, byte=True)
    a.tst_imm(7, 2)
    a.branch("fdc_wait", "eq")
    a.ldr(7, 0, 12, byte=True)
    a.eor(6, 6, 7)
    a.subs_imm(5, 5, 1)
    a.branch("fdc_wait", "ne")
    a.label("fdc_read_finish")
    a.ldr(4, 0, byte=True)
    a.tst_imm(4, 1)
    a.branch("fdc_read_finish", "ne")

    # Write sector one through the same controller. The source ADF is immutable;
    # the harness verifies that only the content-addressed shadow changes.
    a.mov(1, 1)
    a.str(1, 0, 8, byte=True)
    a.mov(1, 0xA0)
    a.str(1, 0, byte=True)
    a.ldr_const(5, 1024)
    a.mov(7, 0x5A)
    a.label("fdc_write_wait")
    a.ldr(4, 0, byte=True)
    a.tst_imm(4, 2)
    a.branch("fdc_write_wait", "eq")
    a.str(7, 0, 12, byte=True)
    a.subs_imm(5, 5, 1)
    a.branch("fdc_write_wait", "ne")
    a.label("fdc_write_finish")
    a.ldr(4, 0, byte=True)
    a.tst_imm(4, 1)
    a.branch("fdc_write_finish", "ne")

    # Expand the guest checksum to all bytes and paint 640x256 at 8 bpp.
    a.orr_lsl(6, 6, 6, 8)
    a.orr_lsl(6, 6, 6, 16)
    a.mov(1, 0)
    a.ldr_const(5, 40960)
    a.label("fill_video")
    a.str_post(6, 1)
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
    # to the normal mode. The frontend must receive a warning, never terminate.
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
    a.ldr_const(10, 250000)
    a.label("idle")
    a.ldr(0, 11, 0x20, byte=True)
    a.tst_imm(0, 0x80)
    a.branch("input_event", "ne")
    a.subs_imm(10, 10, 1)
    a.branch("idle", "ne")
    a.ldr_const(10, 250000)
    a.tst_imm(9, 1)
    a.branch("mode_fast", "ne")
    a.ldr_const(0, 0xA063C000)  # 400 total lines
    a.mov(9, 1)
    a.str(0, 1)
    a.branch("idle")

    a.label("input_event")
    a.ldr(0, 11, 4, byte=True)
    a.eor(6, 6, 0)
    a.mov(4, 0x3F)
    a.str(4, 11, 4, byte=True)
    a.label("input_second")
    a.ldr(0, 11, 0x20, byte=True)
    a.tst_imm(0, 0x80)
    a.branch("input_second", "eq")
    a.ldr(0, 11, 4, byte=True)
    a.eor(6, 6, 0)
    a.mov(4, 0x33)
    a.str(4, 11, 4, byte=True)
    a.str(6, 8)
    a.branch("idle")
    a.label("mode_fast")
    a.ldr_const(0, 0xA04DC000)  # 312 total lines
    a.mov(9, 0)
    a.str(0, 1)
    a.branch("idle")

    code = a.finish()
    rom[0x20:0x20 + len(code)] = code
    marker = b"ARCDROID ARMV2 TEST ROM v2\0"
    rom[0x1F00:0x1F00 + len(marker)] = marker
    return rom


def build_adf() -> bytearray:
    adf = bytearray(ADF_SIZE)
    marker = b"ARCDROID WD1772 PAYLOAD v2\0"
    adf[:len(marker)] = marker
    for index in range(len(marker), ADF_SIZE):
        adf[index] = ((index * 73) ^ (index >> 3) ^ 0xA5) & 0xFF
    return adf


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "arcdroid-test.rom").write_bytes(build_rom())
    (args.output / "arcdroid-test.adf").write_bytes(build_adf())


if __name__ == "__main__":
    main()
