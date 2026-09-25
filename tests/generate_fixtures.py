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

# Keep the chooser application ROM at exactly 50 Hz while retaining the same
# 640-pixel guest display: 1000 pixel clocks per line * 320 lines at 16 MHz.
CHOOSER_VIDC_MODE = (
    VIDC_MODE[0], 0x807CC000, *VIDC_MODE[2:8], 0xA04FC000, *VIDC_MODE[9:]
)


def build_rom(media_exerciser: bool = False,
              jfd_save_probe: bool = False, write_sector: int = 1,
              write_payload: bytes | None = None) -> bytearray:
    if not 0 <= write_sector <= 4 or (write_payload is not None and
                                      len(write_payload) != 1024):
        raise ValueError("the synthetic D-format writer needs one full sector")
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
    if jfd_save_probe:
        a.mov(1, 1)
    a.str(1, 0, 8, byte=True)
    a.mov(1, 0x80)
    a.str(1, 0, byte=True)
    if jfd_save_probe:
        a.ldr_const(5, 1024)
    else:
        a.mov(5, 0x100)
    a.mov(6, 0)
    if jfd_save_probe:
        a.ldr_const(3, 250000)
    a.label("fdc_wait")
    a.ldr(7, 0, byte=True)
    a.tst_imm(7, 2)
    if jfd_save_probe:
        a.branch("fdc_read_byte", "ne")
        a.tst_imm(7, 1)
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
        # Classify the full sector *before* writing. The two constants are
        # the rolling hashes of the generated pristine sector and 1024 bytes
        # of 0x5A; an unknown read or controller error is never
        # allowed to masquerade as a successful save.
        # WD1770 leaves bit 7 (ready) set on a successful completed command.
        a.cmp_imm(4, 0x80)
        a.branch("save_unknown", "ne")
        a.ldr_const(7, 0xEEFAB800)
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

    # Write sector one through the same controller. The source ADF is immutable;
    # the harness verifies that only the content-addressed shadow changes.
    a.mov(1, write_sector)
    a.str(1, 0, 8, byte=True)
    a.mov(1, 0xA0)
    a.str(1, 0, byte=True)
    a.ldr_const(5, 1024)
    if write_payload is None:
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
        if write_payload is not None:
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
        if write_payload is not None:
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
    a.branch("idle")
    a.label("mode_fast")
    a.ldr_const(0, 0xA04DC000)  # 312 total lines
    a.mov(9, 0)
    a.str(0, 1)
    a.branch("idle")

    code = a.finish()
    rom[0x20:0x20 + len(code)] = code
    marker = (b"ARCDROID ARMV2 MEDIA EXERCISER ROM v1\0" if media_exerciser
              else b"ARCDROID ARMV2 TEST ROM v2\0")
    rom[0x1F00:0x1F00 + len(marker)] = marker
    if write_payload is not None:
        rom[0x2000:0x2400] = write_payload
    return rom


def _chooser_signature_application(title: bytes, colour: int) -> bytes:
    """One-sector RISC OS Absolute app also executable by the chooser ROM."""
    vdu = bytes((22, 12, 17, colour, 12, 31, 8, 15)) + title
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


def build_chooser_rom() -> bytearray:
    """Run the executable named by ArcDroid's drive-1 launch helper.

    The helper's ArcLaunch file is fixed at logical sector 1280, which is
    physical track 0, side 1, sector 0 on an L-format disc. The two invented
    fixture applications keep their Absolute executables in physical sectors
    7 and 8. This ROM reads the helper target, loads the matching executable at
    its declared &8000 address, and runs it. A tiny OS_WriteC handler folds the
    application's VDU stream into a full-width framebuffer signature row.
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
    a.cmp_imm(5, 204)
    a.branch("chooser_helper_not_target", "ne")
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
    a.mov(6, 7)
    a.branch("chooser_load_app")
    a.label("chooser_app_two")
    a.mov(6, 8)

    a.label("chooser_load_app")
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
                     extended: bool = False) -> bytearray:
    """Build one track-side from (time, B, density, id, C, payload)."""
    if disk_payload is not None and disk_geometry is not None:
        return _build_jfd_disk(disk_payload, disk_geometry, title, False)
    track_offset = 316 if extended else 304
    sector_offset = track_offset + 8
    data_offset = sector_offset + len(descriptors) * 8 + 4
    payloads = bytearray()
    table = bytearray()
    for time, block, density, sector_id, crc, payload in descriptors:
        size = len(payload) if payload is not None else 128
        size_code = {128: 0, 256: 1, 512: 2, 1024: 3}[size]
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
    struct.pack_into("<II", image, track_offset, 0, 0xFFFFFFFF)
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
                      root_attribute_high: int | None = None) -> bytearray:
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
                      duplicate_root_file: int | None = None) -> bytearray:
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
    image[1024:3072] = root
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
                      irrelevant_malformed: bool = False) -> bytearray:
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
                       extended: bool = True) -> bytearray:
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
            if not any(payload):
                continue
            entries += struct.pack("<II", (4 << 16) | (sector_id << 8) | 3,
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
    (args.output / "arcdroid-chooser-test.rom").write_bytes(build_chooser_rom())
    unsupported_launch_rom = build_chooser_rom()
    unsupported_launch_rom[-1] = 0x42
    (args.output / "arcdroid-unsupported-launch-test.rom").write_bytes(
        unsupported_launch_rom)
    (args.output / "arcdroid-test.adf").write_bytes(build_adf())
    build_adffs_fixtures(args.output / "adffs")
    (args.output / "arcdroid-demo.adl").write_bytes(build_demo_adl())
    (args.output / "arcdroid-application-l.adl").write_bytes(build_application_l())
    (args.output / "arcdroid-chooser-l.adl").write_bytes(build_chooser_application_l())
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
    illegal = (b"!Bad.Name", b"!Bad:Name", b"!Bad<Name", b"!Bad>Name",
               b"!Bad|Name", b"!Bad$Name", b"!Bad&Name", b"!Bad@Name",
               b"!Bad^Name", b"!Bad%Name", b"!Bad*Name", b"!Bad#Name",
               b"!Bad\"Name", b"!Bad Name", b"!Bad\\Name", b"!Bad\x01", b"!Bad\x7f",
               b"!BadNameX\x80")
    launch_images["illegal-l.adl"] = (
        build_l_discovery([(name, True) for name in illegal]), "L")
    for name, (payload, _geometry) in launch_images.items():
        (launch / name).write_bytes(payload)
    hd = args.output / "hd-family"
    hd.mkdir(exist_ok=True)
    f_image = build_f_discovery()
    (hd / "valid-f.adf").write_bytes(f_image)
    (hd / "valid-f.jfd").write_bytes(build_sparse_f_jfd(f_image))
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
                 "has-boot-d.adf", "self-boot-unknown-d.adf", "illegal-l.adl"):
        payload, geometry = launch_images[base]
        stem = base.rsplit(".", 1)[0]
        (launch / f"{stem}.jfd").write_bytes(
            build_jfd_family([], b"ArcDroid launch robustness fixture",
                             disk_payload=payload, disk_geometry=geometry))
    checkpoint_guest = build_checkpoint_guest_disc()
    checkpoint_names = build_checkpoint_name_discs()
    (launch / "checkpoint-a-guest.adf").write_bytes(checkpoint_guest)
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
    jfd_save_rom = build_rom(media_exerciser=True, jfd_save_probe=True)
    disc1 = build_adf(0xA5, b"ARCDROID MEDIA DISC ONE v1\0")
    disc2 = build_adf(0x3C, b"ARCDROID MEDIA DISC TWO v1\0")
    (args.output / "arcdroid-media-test.rom").write_bytes(media_rom)
    (args.output / "arcdroid-jfd-save-test.rom").write_bytes(jfd_save_rom)
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
        build_jfd_family([(0, 0, 8, 1, 0, bytes(128))],
                         b"ArcDroid rejected folder JFD"))
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
    adffs_writable = build_jfd_family([
        (0xFF, 0, 2, 0, 0, bytes(1024)),
        (0xFF, 0, 2, 1, 0, bytes(1024)),
    ], b"ArcDroid ADFFS native-save fixture", extended=True)
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
    geometries = (
        ("320k", build_old_map_media(320 * 1024, 1280, 0x31), 4096, 256, "adf"),
        ("640k-l", build_old_map_media(640 * 1024, 2560, 0x64), 8192, 256, "adl"),
        ("800k-d", build_old_map_media(800 * 1024, 3200, 0xD0), 20480, 1024, "adf"),
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


if __name__ == "__main__":
    main()
