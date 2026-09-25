#!/usr/bin/env python3
"""Fresh-process guest read/partial-merge/write proof for a formatted slot.

Each step starts a new harness process and libretro core. The synthetic ROM
uses steady palette outcomes. The first step reads 1024 zero bytes before
writing only 128 changed bytes with the zero tail; the second step reads the
created sector and exits before issuing any write. The harness also checks
each saved byte through the core's test-only effective-view hook.
"""
import hashlib
from pathlib import Path
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
            for image, outcome, slot in (
                (rw, "006a0000", "off-to-on"),
                (rw, "00001500", "slot"),
                (wp, "00006a00", "no-slot"),
                (nonmatching, "00006a00", "no-slot"),
                (irregular_free, "00006a00", "no-slot"),
            ):
                subprocess.run((str(harness), str(core), str(rom), str(image),
                                str(root), "--jfd-merge-step", machine,
                                outcome, slot), check=True)
    assert tuple(map(digest, sources)) == before, "source JFD mutated"
    print("jfd_merge_process_test: both controllers, fresh reload and WP passed")


if __name__ == "__main__":
    main()
