#!/usr/bin/env python3
"""Generate the predeclared AC-8 authored workload, never firmware. GPL-2.0-only.

SA-110, 100 MIPS; MMU client domain 0; identity ROM/MMIO/RAM sections plus
page/section data aliases. Permission checking includes a forbidden STRT
with a recovering architectural abort and allowed LDRT/STRT in the hot loop.
24 MHz / 800 pixels / 500 lines = 60 Hz; 640x480 8 bpp; a guest VIDCUR
poll updates palette entry 1 for the following active line (initial colour
is programmed before scanout). Stereo signed DMA continuously rearms 4 KiB
banks at 44.1 kHz; the actual 48 kHz frontend stream is hashed by the runner.
No rate, private game recipe, firmware heuristic or host device write.
"""
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from rpc_video_rom import Guest


def workload():
    g = Guest()
    g.w = [0xeafffffe] * 64
    g.w[0] = 0xea00003e  # reset -> 0x100
    g.w[4] = 0xea00001a  # data abort -> 0x80
    # No stack/host involvement: count the denied user store, skip it.
    g.w[32:40] = [0xe59fb010, 0xe59bc000, 0xe28cc001, 0xe58bc000,
                  0xe25ef004, 0xe1a00000, 0x10020000, 0xeafffffe]
    g.literal(0, 0x70); g.emit(0xee010f10)
    g.literal(0, 0xd3); g.emit(0xe121f000)  # SVC32
    g.fill(0x10004000, 4096, 0)
    # Identity sections: ROM, IOMD, VIDC, RAM. Client AP3.
    for index, physical in [(0, 0), (0x32, 0x03200000), (0x34, 0x03400000), (0x100, 0x10000000)]:
        g.write(0x10004000+index*4, physical | 0xc02)
    # Page alias 8 MiB -> RAM 0x10030000; APs 3/2/1/0.
    g.write(0x10004020, 0x10008001)
    g.write(0x10008000, 0x10030000 | (3 << 4) | (2 << 6) | (1 << 8) | 2)
    g.write(0x10004024, 0x10000802)  # section alias 9 MiB, user read only
    g.fill(0x10080000, 76800, 0x01010101)
    g.fill(0x10010000, 1024, 0x04d2fb2e)  # L=-1234/R=1234
    g.write(0x10020000, 0)
    g.literal(0, 0x10004000); g.emit(0xee020f10)
    g.literal(0, 1); g.emit(0xee030f10)
    g.literal(0, 0x71); g.emit(0xee010f10)  # MMU+32-bit+late abort
    # Cached privileged read followed by user-transfer store must be denied.
    g.literal(2, 0x00900300); g.emit(0xe5921000, 0xe4a21000)
    for reg, value in [(0x1d8, 0x10080000), (0x1dc, 0x10080000),
                       (0x1d4, 0x100caff0), (0x1e0, 0x70)]:
        g.write(0x03200000+reg, value)
    for v in [0x10000001, 0x00000001, 0x83000000, 0x84000280,
              0x93000002, 0x940001e2, 0x80000318, 0x900001f2,
              0xd0000000, 0xe0000060]:
        g.v(v)
    for address, value in [(0x03400000, 0xb0000002), (0x03400000, 0xb1000002),
                           (0x03200190, 0x80), (0x03200180, 0x10010000),
                           (0x03200184, 0x10010ff0), (0x03200188, 0x10010000),
                           (0x0320018c, 0x10010ff0), (0x03200190, 0x20)]:
        g.write(address, value)
    for reg, value in [(2, 0x00800000), (3, 0x00800400), (4, 0x00900300),
                       (8, 0x03200000), (9, 0x03400000), (10, 0x10020000),
                       (5, 0), (6, 0), (7, 0), (11, 0x10010000), (12, 0x10010ff0)]:
        g.literal(reg, value)
    loop = len(g.w)
    # ALU, permission checked page/section loads, user and ordinary stores.
    g.emit(0xe2855001, 0xe4b30000, 0xe5941000, 0xe4a25000, 0xe5921000,
           0xe0211005, 0xe5821004, 0xe58a5004)
    # One guest palette change for each observed active DMA line transition.
    g.emit(0xe59801d0, 0xe1500006)
    branch = len(g.w); g.emit(0)
    g.emit(0xe1a06000, 0xe2877001)
    g.literal(0, 0x10000001); g.emit(0xe5890000, 0xe20700ff, 0xe5890000, 0xe58a7008)
    g.w[branch] = 0x0a000000 | ((len(g.w)-branch-2) & 0xffffff)
    # Refill the consumed bank as soon as SD0ST raises its IRQ bit. Current
    # bank bit names the other bank; no writes to a running DMA pointer.
    g.emit(0xe5980194, 0xe3100002)
    skip = len(g.w); g.emit(0)
    g.emit(0xe3100001, 0x1588b180, 0x1588c184, 0x0588b188, 0x0588c18c)
    g.w[skip] = 0x0a000000 | ((len(g.w)-skip-2) & 0xffffff)
    g.emit(0xea000000 | ((loop-len(g.w)-2) & 0xffffff))
    return g.w


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    words = workload()
    rom = bytearray(struct.pack("<I", 0xeafffffe) * (2097152//4))
    rom[:len(words)*4] = struct.pack("<"+"I"*len(words), *words)
    (directory / "ac8-authored.rom").write_bytes(rom)
    (directory / "authored.seed").write_bytes(bytes(i ^ 0xa5 for i in range(256)))
    (directory / "ac8.arcplan").write_text(
        "version=1\nmodel=sa110\nrate_mips=100\nfirmware=ac8-authored.rom\n"
        "cmos=seed\nseed=authored.seed\nrtc_epoch=0\n")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: rpc_ac8_workload.py OUTPUT_DIRECTORY")
    generate(Path(sys.argv[1]))
