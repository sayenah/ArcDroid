#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Guest and store proof for a writable, G5-admitted omitted DD sector."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, fixtures = map(Path, sys.argv[1:4])
    image = fixtures / "jfd-writes/rw-identification-d.jfd"
    source_hash = hashlib.sha256(image.read_bytes()).digest()
    with tempfile.TemporaryDirectory(prefix="arc-jfd-identification-") as temp:
        for machine, stem in (("A3000", ""), ("A3020", "newio-")):
            root = Path(temp) / machine
            root.mkdir()

            def step(data: Path, rom: str, mode: str, outcome: str) -> None:
                subprocess.run((str(harness), str(core),
                                str(fixtures / f"arcdroid-jfd-{stem}{rom}-test.rom"),
                                str(image), str(data), "--jfd-identification-step",
                                machine, outcome, mode), check=True)

            step(root, "allocation-probe-write", "probe-write", "00001500")
            step(root, "allocation-veto", "veto", "00001500")
            step(root, "allocation-claim", "claim", "006a0000")
            step(root, "create", "write", "006a0000")
            step(root, "create", "readback", "00001500")
            step(root, "create", "off", "00000000")

            damaged_root = Path(temp) / (machine + "-damaged")
            shutil.copytree(root, damaged_root)
            slots = list((damaged_root / "save/arcdroid/jfd").glob("*.?"))
            assert slots
            for slot in slots:
                payload = bytearray(slot.read_bytes())
                payload[0] ^= 1
                slot.write_bytes(payload)
            step(damaged_root, "create", "damaged", "00000000")

            failed_root = Path(temp) / (machine + "-failed")
            failed_root.mkdir()
            step(failed_root, "allocation-claim", "claim", "006a0000")
            step(failed_root, "create", "failed", "00006a00")
    assert hashlib.sha256(image.read_bytes()).digest() == source_hash
    print("jfd_identification_process_test: both controllers and store paths passed")


if __name__ == "__main__":
    main()
