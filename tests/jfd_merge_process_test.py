#!/usr/bin/env python3
"""Fresh-process guest read/partial-merge/write proof for a formatted slot.

Each step starts a new harness process and libretro core. The synthetic ROM
uses steady palette outcomes. The first save commits the map sector to arm
virtual reads, reads 1024 zero bytes, then writes 128 changed bytes and the
zero tail. A fresh process reads the committed sector before any write and
sees an unwritten omission return RNF again. The harness also checks
each saved byte through the core's test-only effective-view hook, then proves
damaged-load and mid-session failure latch transitions on both controllers.
"""
import hashlib
from pathlib import Path
import shutil
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
    irregular_free = fixtures / "jfd-creation/irregular-free-sequence.jfd"
    sources = (rw, wp, nonmatching, irregular_free)
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
                (wp, "00006a00", "no-slot"),
                (nonmatching, "00006a00", "no-slot"),
                (irregular_free, "00006a00", "no-slot"),
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

            set_root = Path(temp) / (machine + "-set")
            set_root.mkdir()
            shutil.copyfile(rw, set_root / "first.jfd")
            shutil.copyfile(writes / "rw-create-d.jfd", set_root / "second.jfd")
            playlist = set_root / "two.m3u"
            playlist.write_text("first.jfd\nsecond.jfd\n", encoding="utf-8")
            subprocess.run((str(harness), str(core), str(readback_rom),
                            str(playlist), str(set_root),
                            "--jfd-activation-set", machine), check=True)
    assert tuple(map(digest, sources)) == before, "source JFD mutated"
    print("jfd_merge_process_test: both controllers, reload, WP, damaged latch, commit failure passed")


if __name__ == "__main__":
    main()
