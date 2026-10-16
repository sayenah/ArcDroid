#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Guest map probe/veto, fresh-process claim and zero read on both FDCs."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, fixtures = map(Path, sys.argv[1:4])
    image = fixtures / "jfd-allocation/old-free.jfd"
    source_hash = hashlib.sha256(image.read_bytes()).digest()
    with tempfile.TemporaryDirectory(prefix="arc-jfd-allocation-") as temp:
        for machine, stem in (("A3000", ""), ("A3020", "newio-")):
            data = Path(temp) / machine
            data.mkdir()
            for mode, outcome in (("veto", "00001500"),
                                  ("claim", "006a0000"),
                                  ("read", "00001500")):
                rom = fixtures / f"arcdroid-jfd-{stem}allocation-{mode}-test.rom"
                subprocess.run((str(harness), str(core), str(rom), str(image),
                                str(data), "--jfd-allocation-step", machine,
                                outcome, mode), check=True)
            set_data = Path(temp) / (machine + "-set")
            set_data.mkdir()
            shutil.copyfile(image, set_data / "first.jfd")
            shutil.copyfile(fixtures / "jfd-allocation/old-dual-free.jfd",
                            set_data / "second.jfd")
            playlist = set_data / "two.m3u"
            playlist.write_text("first.jfd\nsecond.jfd\n", encoding="utf-8")
            rom = fixtures / f"arcdroid-jfd-{stem}allocation-veto-test.rom"
            subprocess.run((str(harness), str(core), str(rom), str(playlist),
                            str(set_data), "--jfd-allocation-set", machine),
                           check=True)
    assert hashlib.sha256(image.read_bytes()).digest() == source_hash
    print("jfd_allocation_process_test: both controllers, veto, fresh claim/read passed")


if __name__ == "__main__":
    main()
