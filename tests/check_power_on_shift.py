#!/usr/bin/env python3
"""Guest KART evidence from project-authored firmware under the test DLL."""
import os
from pathlib import Path
import re
import subprocess
import sys


def run(harness: str, core: str, fixtures: Path, root: Path,
        rom: str, frames: int, reset: bool, reason: str,
        extra: tuple[str, ...] = (), content: str | None = None,
        kart: bool = True) -> str:
    data = root / rom.replace(".rom", "")
    data.mkdir(parents=True, exist_ok=True)
    command = [harness, core, str(fixtures / rom),
               content or str(fixtures / "arcdroid-test.adf"), str(data),
               "--real-boot", "--boot-expect-disabled", "--boot-frames", str(frames)]
    if reset:
        command.append("--boot-reset-once")
    command.extend(extra)
    env = dict(os.environ, ARCDROID_TEST_POWER_ON_SHIFT="1")
    result = subprocess.run(command, env=env, capture_output=True, text=True,
                            timeout=240, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    log = (data / "evidence" / "real-boot.log").read_text(errors="replace")
    constructions = log.count("Power-on: boot-flag=clear shift=held")
    expected = 2 if reset else 1
    assert constructions == expected, (rom, constructions)
    releases = re.findall(r"Power-on: shift released at=(\d+) reason=([^\s]+)", log)
    assert len(releases) == expected, (rom, releases)
    for at, why in releases:
        assert why == reason, (rom, releases)
        assert (int(at) < 1_000_000) if reason == "boot-action" else (
            1_000_000 <= int(at) < 1_002_000), (rom, releases)
    down = "KART device->guest values=C4,CC"
    up = "KART device->guest values=D4,DC"
    if kart:
        assert log.count(down) == log.count(up) == expected, (rom, log)
        if rom.endswith("late-kart.rom"):
            assert log.index("Power-on: shift released") < log.index(down) < log.index(up)
    assert log.count("FDC command=80 type=8") >= expected, rom
    if rom.endswith("early-swi.rom"):
        # The authored guest resets through its vector immediately after
        # consuming key-up. Its last operation before the branch is a
        # WD1770 force-interrupt marker; the adapter does not re-arm.
        assert log.count("FDC command=D0 type=D") == 1, log
        assert log.index(up) < log.index("FDC command=D0 type=D"), log
    print(f"{rom}: {expected} constructions, " +
          ("guest KART Shift down/up" if kart else "ADFFS route") + f", {reason}")
    return log


def main() -> None:
    harness, core, fixtures, root, *adffs = sys.argv[1:]
    fixtures, root = Path(fixtures), Path(root)
    run(harness, core, fixtures, root, "arcdroid-test.rom", 230, True, "timer")
    run(harness, core, fixtures, root, "arcdroid-power-on-early-swi.rom",
        130, False, "boot-action")
    run(harness, core, fixtures, root, "arcdroid-power-on-early-xswi.rom",
        130, False, "boot-action")
    run(harness, core, fixtures, root, "arcdroid-power-on-late-kart.rom",
        500, False, "timer")
    for order in ("early", "late"):
        run(harness, core, fixtures, root, "arcdroid-test.rom",
            130, False, "timer", ("--boot-user-shift", order))
    run(harness, core, fixtures, root, "arcdroid-test.rom", 130,
        False, "timer", ("--boot-incomplete-fields",))
    if adffs:
        assert len(adffs) == 2
        log = run(harness, core, fixtures, root, "arcdroid-test.rom",
                  130, False, "timer", ("--boot-package", adffs[1]), adffs[0],
                  kart=False)
        assert "JFD boot: path=adffs" in log, log


if __name__ == "__main__":
    main()
