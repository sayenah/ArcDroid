#!/usr/bin/env python3
"""Fresh-process guest map/directory/omitted-data JFD save proof.

Each step starts a new harness process and libretro core. The synthetic ROM
uses steady palette outcomes; the second step reads the created sector and
exits before issuing any write, so a stale screen cannot pass as persistence.
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
    rw = writes / "rw-create-d.jfd"
    wp = writes / "wp-create-d.jfd"
    nonmatching = fixtures / "jfd-creation/nonmatching.jfd"
    unsupported = fixtures / "jfd-creation/irregular-newmap-free-sequence.jfd"
    sources = (rw, wp, nonmatching, unsupported)
    before = tuple(map(digest, sources))
    with tempfile.TemporaryDirectory(prefix="arc-jfd-create-") as temp:
        for machine, rom_name in (
            ("A3000", "arcdroid-jfd-create-test.rom"),
            ("A3020", "arcdroid-jfd-newio-create-test.rom"),
        ):
            root = Path(temp) / machine
            root.mkdir()
            rom = fixtures / rom_name
            for image, outcome, slot in (
                (rw, "006a0000", "slot"),
                (rw, "00001500", "slot"),
                (wp, "00006a00", "no-slot"),
                (nonmatching, "00006a00", "no-slot"),
                (unsupported, "00006a00", "no-slot"),
            ):
                subprocess.run((str(harness), str(core), str(rom), str(image),
                                str(root), "--jfd-create-step", machine,
                                outcome, slot), check=True)
    assert tuple(map(digest, sources)) == before, "source JFD mutated"
    print("jfd_creation_process_test: both controllers, fresh reload and WP passed")


if __name__ == "__main__":
    main()
