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
import struct

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

    def subs_imm(self, rd: int, rn: int, value: int) -> None:
        self.emit(0xE2500000 | (rn << 16) | (rd << 12) | self._imm(value))

    def eor(self, rd: int, rn: int, rm: int) -> None:
        self.emit(0xE0200000 | (rn << 16) | (rd << 12) | rm)

    def orr_lsl(self, rd: int, rn: int, rm: int, shift: int) -> None:
        self.emit(0xE1800000 | (rn << 16) | (rd << 12) | (shift << 7) | rm)

    def add_lsl(self, rd: int, rn: int, rm: int, shift: int) -> None:
        self.emit(0xE0800000 | (rn << 16) | (rd << 12) | (shift << 7) | rm)

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


def build_rom(media_exerciser: bool = False) -> bytearray:
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
    if media_exerciser:
        a.ldr_const(3, 250000)
        a.label("fdc_write_wait")
        a.ldr(4, 0, byte=True)
        a.tst_imm(4, 1)
        a.branch("fdc_write_done", "eq")
        a.tst_imm(4, 2)
        a.branch("fdc_write_tick", "eq")
        a.str(7, 0, 12, byte=True)
        a.subs_imm(5, 5, 1)
        a.branch("fdc_write_done", "eq")
        a.label("fdc_write_tick")
        a.subs_imm(3, 3, 1)
        a.branch("fdc_write_wait", "ne")
        a.mov(4, 0xD0)  # bounded cancellation if a reader never completes
        a.str(4, 0, byte=True)
        a.label("fdc_write_done")
    else:
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

    # Expand the guest checksum to all bytes and paint 640x256 at 8 bpp. The
    # alternating complement makes the payload-derived signature visibly
    # distinctive in native-frontend screenshots instead of a uniform colour.
    a.orr_lsl(6, 6, 6, 8)
    a.orr_lsl(6, 6, 6, 16)
    a.ldr_const(7, 0x00FFFFFF)
    a.mov(1, 0)
    a.ldr_const(5, 40960)
    a.label("fill_video")
    a.str_post(6, 1)
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


def build_jfd_family(descriptors: list[tuple[int, int, int, int, int, bytes | None]],
                     title: bytes = b"ArcDroid JFD family",
                     disk_payload: bytes | None = None,
                     disk_geometry: str | None = None) -> bytearray:
    """Build one track-side from (time, B, density, id, C, payload)."""
    if disk_payload is not None and disk_geometry is not None:
        return _build_jfd_disk(disk_payload, disk_geometry, title, False)
    track_offset, sector_offset = 304, 312
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


def _build_absolute_boot() -> bytes:
    """Build a tiny ARM2 Absolute program that paints a stable VDU pattern."""
    vdu = bytearray((22, 12, 17, 128 + 1, 12, 23, 1, 0, 0, 0, 0, 0, 0, 0, 0))
    for row in range(4, 28):
        vdu.extend((31, 8, row, 17, 2 + (row % 6)))
        vdu.extend((b"##  " if row & 1 else b"  ##") * 16)
    title = b"ARCDROID - PROJECT AUTHORED BOOT DISC"
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


def _set_old_disc_name(image: bytearray, name: bytes) -> None:
    padded = name[:10].ljust(10, b"\r")
    image[247:252] = padded[0::2]
    image[502:507] = padded[1::2]


def build_l_discovery(candidates: list[tuple[bytes, bool]],
                      disc_name: bytes = b"Fixture", boot: int = 0,
                      incomplete: int | None = None) -> bytearray:
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
        if incomplete != index:
            child = _old_directory(name)
            if application:
                _old_entry(child, 0, b"!Run", 1, 400 + index)
                _l_write(image, 400 + index, b"\r")
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


def build_d_discovery(candidates: list[tuple[bytes, bool]],
                      disc_name: bytes = b"Fixture", boot: int = 0,
                      signature: bytes = b"Hugo",
                      incomplete: int | None = None,
                      declared_sectors: int = ADF_SIZE // 256,
                      stale_names: bool = False) -> bytearray:
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
        if stale_names:
            _stale_new_name(root, index, name)
        if incomplete != index:
            child = _new_directory(name, signature)
            if application:
                run_offset = 65536 + index * 1024
                _new_entry(child, 0, b"!Run", 1, run_offset // 256)
                if stale_names:
                    _stale_new_name(child, 0, b"!Run")
                image[run_offset] = 13
            image[child_offset:child_offset + 2048] = child
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
                      late_target: bool = False) -> bytearray:
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
        if stale_names:
            _stale_new_name(root, index, name)
        child_offset, run_offset = allocations[index]
        child = _new_directory(name, signature)
        if application:
            run_id = child_id + 1
            run_length = 3072 if fragmented == index else 1
            _new_entry(child, 0, b"!Run", run_length, run_id << 8)
            if stale_names:
                _stale_new_name(child, 0, b"!Run")
            image[run_offset] = 13
        image[child_offset:child_offset + 2048] = child
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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "arcdroid-test.rom").write_bytes(build_rom())
    (args.output / "arcdroid-test.adf").write_bytes(build_adf())
    (args.output / "arcdroid-demo.adl").write_bytes(build_demo_adl())
    (args.output / "arcdroid-application-l.adl").write_bytes(build_application_l())
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
        "multiple-exact-d.adf": (build_d_discovery([(b"!Alpha", True), (b"!Beta", True)], b"Beta"), "D"),
        "multiple-trailing-d.adf": (build_d_discovery([(b"!Alpha", True), (b"!Beta", True)], b"Beta2"), "D"),
        "ambiguous-l.adl": (build_l_discovery(
            [(b"!Docs", False), (b"!Foo", True), (b"!Bar", True)], b"Other"), "L"),
        "no-run-d.adf": (build_d_discovery([(b"!Docs", False), (b"!Data", False)]), "D"),
        "incomplete-l.adl": (build_l_discovery([(b"!Broken", True)], incomplete=0), "L"),
        "unsafe-disc-name-l.adl": (build_l_discovery(
            [(b"!Foo", True), (b"!Bar", True)], b"Bad\nName"), "L"),
        "more-eight-d.adf": (build_d_discovery(
            [(f"!App{i}".encode(), i == 9) for i in range(10)]), "D"),
        "fragmented-e.adf": (build_e_discovery([(b"!Demo", True)], b"Demo", fragmented=0), "E"),
        "unresolved-e.adf": (build_e_discovery([(b"!Demo", True)], b"Demo", unresolved=0), "E"),
        "explicit-d.adf": (build_d_discovery(
            [(b"!Docs", False), (b"!Demo", True), (b"!Other", True)], b"Other", boot=3), "D"),
    }
    for boot in range(4):
        launch_images[f"boot-{boot}-l.adl"] = (
            build_l_discovery([(b"!Demo", True)], b"Demo", boot), "L")
    illegal = (b"!Bad.Name", b"!Bad:Name", b"!Bad<Name", b"!Bad>Name",
               b"!Bad|Name", b"!Bad$Name", b"!Bad&Name", b"!Bad@Name",
               b"!Bad^Name", b"!Bad%Name", b"!Bad*Name", b"!Bad#Name",
               b"!Bad\"Name", b"!Bad Name", b"!Bad\x01", b"!Bad\x7f",
               b"!BadNameX\x80")
    launch_images["illegal-l.adl"] = (
        build_l_discovery([(name, True) for name in illegal]), "L")
    for name, (payload, _geometry) in launch_images.items():
        (launch / name).write_bytes(payload)
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
    media_rom = build_rom(media_exerciser=True)
    disc1 = build_adf(0xA5, b"ARCDROID MEDIA DISC ONE v1\0")
    disc2 = build_adf(0x3C, b"ARCDROID MEDIA DISC TWO v1\0")
    (args.output / "arcdroid-media-test.rom").write_bytes(media_rom)
    (args.output / "arcdroid-media-disc1.adf").write_bytes(disc1)
    (args.output / "arcdroid-media-disc2.adf").write_bytes(disc2)
    (args.output / "arcdroid-media.m3u").write_text(
        "#EXTM3U\narcdroid-media-disc1.adf\narcdroid-media-disc2.adf\n",
        encoding="utf-8", newline="\n")
    jfd = build_jfd(bytes(disc1[:256]))
    (args.output / "arcdroid-test.jfd").write_bytes(jfd)
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
        "reject-density4.jfd": [(0, 0, 4, 1, 0, bytes(128))],
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
    for missing in (1024, 10240, 10241, 11264, 513):
        (args.output / f"short-e-missing-{missing}.adf").write_bytes(short_e[:-missing])
    short_family = args.output / "short-family"
    short_family.mkdir(exist_ok=True)
    geometries = (
        ("320k", build_old_map_media(320 * 1024, 1280, 0x31), 4096, 256, "adf"),
        ("640k-l", build_old_map_media(640 * 1024, 2560, 0x64), 8192, 256, "adl"),
        ("800k-d", build_old_map_media(800 * 1024, 3200, 0xD0), 10240, 1024, "adf"),
        ("800k-e", short_e, 10240, 1024, "adf"),
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
