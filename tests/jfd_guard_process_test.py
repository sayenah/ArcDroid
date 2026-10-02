#!/usr/bin/env python3
"""D-120 guest writes, refusal recovery, merge, restart and violating v2 stores."""
import hashlib
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import tempfile
import zlib


def checksum(block: bytes) -> int:
    value = 255
    for byte in reversed(block[:255]):
        if value > 255:
            value = (value + 1) & 255
        value += byte
    return value & 255


def records(snapshot: bytes) -> list[tuple[int, int, int, bytes]]:
    assert snapshot[:4] == b"AJWO" and struct.unpack_from("<I", snapshot, 4)[0] == 2
    at = 80
    result = []
    for _ in range(struct.unpack_from("<I", snapshot, 76)[0]):
        track, kind, ordinal, _, _, size = struct.unpack_from("<HBHBBH", snapshot, at)
        result.append((track, kind, ordinal, snapshot[at + 9:at + 9 + size]))
        at += 9 + size
    assert at == len(snapshot) - 4
    return result


def main() -> None:
    harness, core, fixtures = map(Path, sys.argv[1:4])
    image = fixtures / "jfd-allocation/guard-crossing.jfd"
    merge = fixtures / "jfd-allocation/guard-merge.jfd"
    sources = {p: hashlib.sha256(p.read_bytes()).digest() for p in (image, merge)}
    with tempfile.TemporaryDirectory(prefix="arc-jfd-guard-") as temp:
        for machine, prefix in (("A3000", ""), ("A3020", "newio-")):
            def run(rom: Path, content: Path, data: Path, flag: str, outcome: str, mode: str) -> None:
                data.mkdir(exist_ok=True)
                subprocess.run((str(harness), str(core), str(rom), str(content),
                                str(data), flag, machine, outcome, mode), check=True)

            for mode, reason in (("u", "guard-unavailable"), ("tail", "guard-tail"),
                                 ("invalid", "guard-invalid-map"), ("size", "guard-size"),
                                 ("data", "guard-unavailable")):
                data = Path(temp) / f"{machine}-{mode}"
                run(fixtures / f"arcdroid-jfd-{prefix}guard-{mode}.rom", image, data,
                    "--jfd-guard-step", "00001500", reason)
                slots = list((data / "save/arcdroid/jfd").glob("*.[ab]"))
                assert len(slots) == 1
                saved = slots[0].read_bytes()
                # The allocating map/data-first U write published nothing;
                # only the subsequent unrelated captured ID 2 was committed.
                assert struct.unpack_from("<Q", saved, 68)[0] == 1
                assert [(t, k, o) for t, k, o, _ in records(saved)] == [(0, 0, 2)]

            route = fixtures / "jfd-allocation/guard-route.jfd"
            for mode in ("route-enabled", "route-off", "route-damaged"):
                data = Path(temp) / (machine + "-" + mode)
                package_dir = data / "system/arcdroid/adffs"
                package_dir.mkdir(parents=True)
                shutil.copyfile(fixtures / "adffs/synthetic.zip", package_dir / "synthetic.zip")
                if mode == "route-damaged":
                    store_dir = data / "save/arcdroid/jfd"
                    store_dir.mkdir(parents=True)
                    stem = hashlib.sha1(route.read_bytes()).hexdigest()
                    (store_dir / (stem + ".a")).write_bytes(b"invented invalid store")
                run(fixtures / f"arcdroid-jfd-{prefix}save-test.rom", route, data,
                    "--jfd-guard-step", "00000000", mode)

            save_data = Path(temp) / (machine + "-save")
            rom = fixtures / f"arcdroid-jfd-{prefix}save-test.rom"
            for outcome, mode in (("006a0000", "save"), ("00001500", "saved")):
                run(rom, image, save_data, "--jfd-guard-step", outcome, mode)

            merge_data = Path(temp) / (machine + "-merge")
            rom = fixtures / f"arcdroid-jfd-{prefix}merge-test.rom"
            for outcome in ("006a0000", "00001500"):
                run(rom, merge, merge_data, "--jfd-merge-step", outcome, "slot")
            slots = list((merge_data / "save/arcdroid/jfd").glob("*.[ab]"))
            assert len(slots) == 1
            original = slots[0].read_bytes()
            assert records(original)[0][3] == bytes((0x5A,)) * 128 + bytes(896)
            decoded = merge.read_bytes()
            payload_at = struct.unpack_from("<I", decoded, 32)[0]
            # Read the map descriptor payload via table offsets, rather than
            # assuming the generated descriptor table's serialized position.
            table = struct.unpack_from("<I", decoded, 28)[0]
            map_offset = struct.unpack_from("<I", decoded, table + 4)[0]
            base_map = bytearray(decoded[payload_at + map_offset:payload_at + map_offset + 1024])
            base_map[:3] = (20).to_bytes(3, "little")
            base_map[256:259] = (3160).to_bytes(3, "little")
            base_map[3:6] = (3200).to_bytes(3, "little")
            base_map[259:262] = (63).to_bytes(3, "little")
            base_map[510] = 6
            base_map[255] = checksum(base_map[:256])
            base_map[511] = checksum(base_map[256:512])
            snapshot = bytearray(original[:80])
            struct.pack_into("<I", snapshot, 76, 3)
            snapshot += struct.pack("<HBHBBH", 0, 0, 0, 0, 2, 1024) + base_map
            snapshot += struct.pack("<HBHBBH", 0, 0, 1, 2, 2, 1024) + bytes((0x71,)) * 1024
            snapshot += original[80:-4]
            snapshot += struct.pack("<I", zlib.crc32(snapshot))
            slots[0].write_bytes(snapshot)
            candidate = slots[0].with_suffix(".tmp")
            candidate.write_bytes(b"invented stale candidate")
            before = {p: p.read_bytes() for p in slots + [candidate]}
            readback = fixtures / f"arcdroid-jfd-{prefix}merge-readback-test.rom"
            run(readback, merge, merge_data, "--jfd-guard-step", "00001500", "violated")
            run(readback, merge, merge_data, "--jfd-guard-step", "0000006a", "off")
            assert all(p.read_bytes() == value for p, value in before.items())
    assert all(hashlib.sha256(p.read_bytes()).digest() == value for p, value in sources.items())
    print("jfd-guard-process: both FDCs, refusal/continued use, persistence, merges and guard-violated/Off passed")


if __name__ == "__main__":
    main()
