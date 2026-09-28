"""Executed-SWI confirmation-frame cancellation and horizon timing."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, rom, content, output, case = sys.argv[1:]
    Path(output).mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    frames = "2100" if case == "horizon" else "180"
    result = subprocess.run((harness, core, rom, content, str(root),
                             "--real-boot", "--boot-frames", frames),
                            capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    match = re.search(r"boot observer: state=(\d+) armed_us=(\d+) "
                      r"read_us=(\d+) event_us=(\d+) pending=(\d+)",
                      result.stdout)
    assert match, result.stdout
    state, armed, read, event, pending = map(int, match.groups())
    log = (root / "evidence" / "real-boot.log").read_text()
    assert "Boot outcome:" not in log
    if case == "cancel":
        assert state == 1 and armed == 0 and pending == 0
        assert 2_000_000 <= event - read < 2_020_000, (read, event)
        assert "boot-watch=armed" in log
    else:
        assert state == 2 and armed == 0 and pending == 0
        assert 38_000_000 <= read < 38_100_000, read
        assert read + 2_000_000 >= 40_000_000
        assert "boot-watch=expired" in log
    print(f"boot-watch-timing: {case} passed")


if __name__ == "__main__":
    main()
