#!/usr/bin/env python3
"""Generate ArcDroid-owned deterministic test firmware/media (GPL-2.0-only)."""
from pathlib import Path
import argparse

ROM_SIZE = 512 * 1024
ADF_SIZE = 800 * 1024


def pattern(size: int, seed: int) -> bytearray:
    result = bytearray(size)
    value = seed
    for index in range(size):
        value = (value * 1664525 + 1013904223) & 0xFFFFFFFF
        result[index] = (value >> 24) ^ (index & 0xFF)
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    rom = pattern(ROM_SIZE, 0xA2C0D001)
    rom_header = b"ARCDROID ARMV2 TEST ROM v1\0\0\0\0"
    rom[: len(rom_header)] = rom_header
    # ARM2 reset vector: branch to itself. The full machine-programmed validation
    # ROM remains an explicit Slice 1 completion requirement.
    rom[32:36] = bytes((0xFE, 0xFF, 0xFF, 0xEA))
    (args.output / "arcdroid-test.rom").write_bytes(rom)

    adf = pattern(ADF_SIZE, 0xADF80001)
    adf_header = b"ARCDROID ADF TEST PAYLOAD v1\0\0\0"
    adf[: len(adf_header)] = adf_header
    (args.output / "arcdroid-test.adf").write_bytes(adf)


if __name__ == "__main__":
    main()
