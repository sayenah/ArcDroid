# SPDX-License-Identifier: GPL-2.0-only
"""Executed-ROM cold observation and CMOS-dependent two-phase boot checks."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, prompt_rom, two_phase_rom, content, output = sys.argv[1:]
    Path(output).mkdir(parents=True, exist_ok=True)

    def run(root: Path, rom: str) -> tuple[str, list[str]]:
        result = subprocess.run(
            [harness, core, rom, content, str(root), "--real-boot",
             "--boot-frames", "180"],
            capture_output=True, text=True, check=False,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        log = (root / "evidence" / "real-boot.log").read_text()
        return log, [line for line in log.splitlines()
                     if line.startswith("Launch:")]

    cold = Path(tempfile.mkdtemp(prefix="cold-", dir=output))
    first_log, first_lines = run(cold, prompt_rom)
    assert len(first_lines) == 2 and "boot-watch=armed" in first_lines[0]
    assert "recovery=helper" in first_lines[1]
    assert "Boot outcome: prompt" in first_log
    assert len(list((cold / "save" / "arcdroid" / "launch").glob("*.boot"))) == 1
    second_log, second_lines = run(cold, prompt_rom)
    assert len(second_lines) == 1 and "remembered boot observation" in second_lines[0]
    assert "boot-watch=disabled" in second_lines[0]
    assert "Boot outcome:" not in second_log

    phased = Path(tempfile.mkdtemp(prefix="phased-", dir=output))
    first_log, first_lines = run(phased, two_phase_rom)
    assert len(first_lines) == 2 and "boot-watch=armed" in first_lines[0]
    assert "recovery=helper" in first_lines[1]
    assert "Boot outcome: prompt" in first_log
    records = list((phased / "save" / "arcdroid" / "launch").glob("*.boot"))
    assert len(records) == 1
    cmos = list((phased / "save" / "arcdroid" / "cmos").glob("*.bin"))
    assert len(cmos) == 1 and cmos[0].read_bytes()[0x58] == 0x77

    second_log, second_lines = run(phased, two_phase_rom)
    assert second_lines and "boot-watch=armed" in second_lines[0], second_lines
    assert "remembered boot observation" not in second_log
    assert any("boot-watch=disabled" in line for line in second_lines), second_lines
    assert all("recovery=helper" not in line for line in second_lines)
    assert "Boot outcome:" not in second_log
    assert "disc_load 1 " not in second_log
    assert not records[0].exists()
    print("boot-observation-two-phase-harness: passed")


if __name__ == "__main__":
    main()
