#!/usr/bin/env python3
"""Fresh-process guest partial merge of a base-allocated omitted sector.

Each step starts a new harness process and libretro core. The ROM poisons RAM,
reads 1024 zero bytes from an omission allocated in the base map, changes its
first 128 bytes, and writes the result. A different free omission remains RNF.
The harness also checks the saved bytes, damaged-slot reload and C-4 failure.
"""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    harness, core, fixtures = map(Path, sys.argv[1:4])
    writes = fixtures / "jfd-writes"
    rw = writes / "rw-merge-d.jfd"
    wp = writes / "wp-merge-d.jfd"
    nonmatching = fixtures / "jfd-creation/nonmatching.jfd"
    unsupported = fixtures / "jfd-creation/irregular-newmap-free-sequence.jfd"
    sources = (rw, wp, nonmatching, unsupported)
    before = tuple(map(digest, sources))
    with tempfile.TemporaryDirectory(prefix="arc-jfd-merge-") as temp:
        for machine, rom_name in (
            ("A3000", "arcdroid-jfd-merge-test.rom"),
            ("A3020", "arcdroid-jfd-newio-merge-test.rom"),
        ):
            root = Path(temp) / machine
            root.mkdir()
            rom = fixtures / rom_name
            readback_rom = fixtures / rom_name.replace(
                "-merge-test", "-merge-readback-test")
            missing_rom = fixtures / rom_name.replace(
                "-merge-test", "-merge-missing-test")
            subprocess.run((str(harness), str(core), str(missing_rom), str(rw),
                            str(root), "--jfd-merge-step", machine,
                            "0000006a", "pre-read"), check=True)
            for image, outcome, slot in (
                (rw, "006a0000", "off-to-on"),
                (wp, "0000006a", "no-slot"),
                (nonmatching, "0000006a", "no-slot"),
                (unsupported, "0000006a", "no-slot"),
            ):
                subprocess.run((str(harness), str(core), str(rom), str(image),
                                str(root), "--jfd-merge-step", machine,
                                outcome, slot), check=True)
            subprocess.run((str(harness), str(core), str(readback_rom), str(rw),
                            str(root), "--jfd-merge-step", machine,
                            "00001500", "slot"), check=True)
            subprocess.run((str(harness), str(core), str(missing_rom), str(rw),
                            str(root), "--jfd-merge-step", machine,
                            "0000006a", "pre-read-saved"), check=True)
            subprocess.run((str(harness), str(core), str(readback_rom), str(rw),
                            str(root), "--jfd-merge-step", machine,
                            "00001500", "advance"), check=True)
            slot_root = root / "save/arcdroid/jfd"
            stem = hashlib.sha1(rw.read_bytes()).hexdigest()
            slots = [slot_root / f"{stem}.{suffix}" for suffix in "ab"]
            assert all(slot.exists() for slot in slots)
            generations = [struct.unpack_from("<Q", slot.read_bytes(), 68)[0]
                           for slot in slots]
            assert generations[0] != generations[1]
            newest = slots[generations.index(max(generations))]
            bad = bytearray(newest.read_bytes())
            bad[0] ^= 1
            newest.write_bytes(bad)
            subprocess.run((str(harness), str(core), str(missing_rom), str(rw),
                            str(root), "--jfd-merge-step", machine,
                            "0000006a", "damaged"), check=True)
            subprocess.run((str(harness), str(core), str(readback_rom), str(rw),
                            str(root), "--jfd-merge-step", machine,
                            "00001500", "damaged"), check=True)

            failure_root = Path(temp) / (machine + "-failure")
            failure_root.mkdir()
            subprocess.run((str(harness), str(core), str(rom), str(rw),
                            str(failure_root), "--jfd-merge-step", machine,
                            "0000006a" if machine == "A3020" else "00006a00",
                            "failed"), check=True)

    assert tuple(map(digest, sources)) == before, "source JFD mutated"
    print("jfd_merge_process_test: both controllers, reload, WP, damaged store, commit failure passed")


if __name__ == "__main__":
    main()
