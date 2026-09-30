#!/usr/bin/env python3
"""Exercise the raw-ADF refusal callback with an authored guest ROM."""
from pathlib import Path
import subprocess
import sys


def main() -> None:
    harness, core, fixtures, output = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    command = [str(harness), str(core),
               str(fixtures / "arcdroid-boot-prompt-write.rom"),
               str(fixtures / "arcdroid-test.adf"), str(output),
               "--real-boot", "--boot-disk-writes", "off",
               "--boot-frames", "150"]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    log = (output / "evidence" / "real-boot.log").read_text()
    assert "FDC command=A0 type=A drive=0" in log
    assert "ADF write refused: image=1 track=0 side=0 sector=4" in log


if __name__ == "__main__":
    main()
