# SPDX-License-Identifier: GPL-2.0-only
"""Fresh recovery discovery after guest FDC writes, raw and JFD."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, rom, content, output, expected = sys.argv[1:]
    Path(output).mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    command = [harness, core, rom, content, str(root), "--real-boot",
               "--boot-frames", "180"]
    if content.endswith(".jfd"):
        command += ["--boot-jfd-boot", "native"]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    log = (root / "evidence" / "real-boot.log").read_text()
    lines = [line for line in log.splitlines() if line.startswith("Launch:")]
    assert len(lines) == 2 and "boot-watch=armed" in lines[0], lines
    assert "Boot outcome: prompt" in log
    assert f"recovery={expected}" in lines[1], lines
    assert "FDC command=A0" in log
    if expected == "chooser":
        assert "1:!Demo:app" in lines[0]
        assert "candidates=[1:!Demo:app,2:!Other:app]" in lines[1]
    elif expected == "none":
        assert "candidates=[1:!Demo:app]" in lines[0]
        assert "candidates=[]" in lines[1]
    elif "renamed" in content:
        assert "candidates=[1:!Demo:app]" in lines[0]
        assert "candidates=[1:!Changed:app]" in lines[1]
        assert "decision=!Changed" in lines[1]
    else:
        assert "candidates=[]" in lines[0]
        assert "candidates=[1:!Demo:app]" in lines[1]
    if content.endswith(".jfd"):
        assert "JFD write: image=1 generation=1" in log
        assert list((root / "save" / "arcdroid" / "jfd").glob("*.a")) or list(
            (root / "save" / "arcdroid" / "jfd").glob("*.b"))
    else:
        staged = list((root / "save" / "arcdroid" / "media").glob("*/disc.adf"))
        assert len(staged) == 1 and staged[0].read_bytes() != Path(content).read_bytes()
    print(f"boot-watch-structure: {expected} passed")


if __name__ == "__main__":
    main()
