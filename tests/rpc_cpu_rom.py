#!/usr/bin/env python3
"""Emit project-authored ARM instructions, never a firmware image. GPL-2.0-only.

The independently stated outcomes are in rpc-cpu-expectations.md. ARM opcodes
are explicit here so the fixture is reviewable without an assembler dependency.
"""
from pathlib import Path
import sys

PROGRAMS = {
    # MOV r0, #0x10000000; MOV r1, #42; STR; LDR; ADD; branch to self.
    "ram": [0xe3a00201, 0xe3a0102a, 0xe5801000, 0xe5902000,
            0xe2822001, 0xeafffffe],
    "data_abort": [0xe5901000],        # LDR r1, [r0]
    "ldrt": [0xe4b01000],              # LDRT r1, [r0], #0
    "strt": [0xe4a01000],              # STRT r1, [r0], #0
    "cp15_id": [0xee103f10],           # MRC p15, 0, r3, c0, c0, 0
    "halfword": [0xe1d030b0],          # LDRH r3, [r0] (ARMv4 only)
    "undefined_cp15": [0xee194f10],    # MRC p15, 0, r4, c9, c0, 0
    "smc_first": [0xe3a03007],         # MOV r3, #7
    "smc_second": [0xe3a03009],        # MOV r3, #9
    "nop": [0xe1a00000],
}

def main():
    output = ["/* Generated from project-authored instructions. GPL-2.0-only. */",
              "#include <stdint.h>"]
    for name, words in PROGRAMS.items():
        output.append("static const uint32_t rpc_rom_" + name + "[] = {" +
                      ", ".join(f"0x{x:08x}u" for x in words) + "};")
    Path(sys.argv[1]).write_text("\n".join(output) + "\n")

if __name__ == "__main__":
    main()
