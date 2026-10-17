# SPDX-License-Identifier: GPL-2.0-only
"""A validated JFD-store change invalidates a remembered prompt."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, prompt_rom, writer_rom, content, output = sys.argv[1:]
    Path(output).mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="run-", dir=output))

    def run(rom):
        command = [harness, core, rom, content, str(root), "--real-boot",
                   "--boot-frames", "180", "--boot-jfd-boot", "native"]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        log = (root / "evidence" / "real-boot.log").read_text()
        return log, [line for line in log.splitlines() if line.startswith("Launch:")]

    log, lines = run(prompt_rom)
    assert len(lines) == 2 and "recovery=helper" in lines[1], lines
    records = list((root / "save" / "arcdroid" / "launch").glob("*.boot"))
    assert len(records) == 1
    original = records[0].read_bytes()
    log, lines = run(writer_rom)
    assert "JFD write: image=1 generation=1" in log
    assert len(lines) == 2 and "recovery=chooser" in lines[1], lines
    assert not records[0].exists()  # The failed boot changed its own media.
    records[0].write_bytes(original)
    log, lines = run(prompt_rom)
    assert len(lines) == 2 and "boot-watch=armed" in lines[0], lines
    assert "recovery=chooser" in lines[1] and "Boot outcome: prompt" in log
    print("boot-observation-jfd-harness: store invalidation passed")


if __name__ == "__main__":
    main()
