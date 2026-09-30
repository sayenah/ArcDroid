#!/usr/bin/env python3
"""Exercise terminal started scopes and product recovery with invented media."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, fixtures, output = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    batch = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    content = fixtures / "hd-family" / "valid-f.adf"

    def boot(name: str, pattern: str, *options: str, frames: int = 100,
             root: Path | None = None) -> str:
        directory = root or batch / name
        selected = (fixtures / "hd-family" / "write-protected-f.jfd"
                    if pattern == "refused" else content)
        command = [str(harness), str(core),
                   str(fixtures / f"arcdroid-outcome-{pattern}.rom"),
                   str(selected), str(directory), "--real-boot", "--machine",
                   "A3020", "--boot-hard-disc-auto", "--boot-frames",
                   str(frames), *options]
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == 0, (name, result.stdout[-3000:],
                                        result.stderr[-3000:])
        return (directory / "evidence" / "real-boot.log").read_text()

    positive_root = batch / "product"
    positive = boot("product", "copy-clean", "--boot-seed-started",
                    "product", "--boot-arm-hd-copy", root=positive_root)
    assert "Launch outcome: armed target=ADFS::4.$.ArcDroid.!Demo.!Run" in positive
    assert "Launch outcome: ended rc=none refused=0 hd-refused=0" in positive
    assert "Hard disc scope: state=started copy=yes product=yes" in positive
    assert "recovery=product" in positive
    assert "reason=product" in positive
    helper = re.search(r"helper=(\S+\.product\.adl)", positive)
    assert helper, positive[-3000:]
    data = Path(helper.group(1)).read_bytes()
    # Old-map L: root is physical sector 2; ArcLaunch starts at physical 16.
    launch_entry = 2 * 256 + 31
    launch_size = int.from_bytes(data[launch_entry + 18:launch_entry + 22],
                                 "little")
    launch = data[16 * 256:16 * 256 + launch_size]
    assert launch == (b"| ArcDroid desktop boot file\r"
                      b"%BASIC -quit ADFS::1.$.ArcMark\r")
    assert b"ArcInstall" not in data and b"ArcRAM" not in data

    abex = boot("abex", "copy-abex", "--boot-seed-started", "product",
                "--boot-arm-hd-copy")
    assert "Launch outcome: ended rc=5" in abex
    assert "recovery=product" not in abex

    refused = boot("hd-refused", "copy-delayed", "--boot-seed-started",
                   "product", "--boot-arm-hd-copy", "--boot-hd-fail-at",
                   "10", frames=1000)
    assert "store-write-failed=io" in refused
    assert "Launch outcome: ended rc=none refused=0 hd-refused=1" in refused
    assert "recovery=product" not in refused

    for variant in ("extra", "copy"):
        root = batch / variant
        boot(variant, "exact", "--boot-seed-started", variant, frames=30,
             root=root)
        terminal = boot(variant, "exact", frames=30, root=root)
        assert "Hard disc scope: state=started copy=yes" in terminal
        assert terminal.count("installation on the hard disc is started") == 1
        assert "reason=single application" in terminal
        assert "reason=hd-product" not in terminal
        assert "recovery=product" not in terminal
        if variant == "extra":
            assert "other=1" in terminal
        else:
            assert "product=no applications=1 other=0" in terminal

    failed_root = batch / "install-write-failed"
    failed = boot("install-write-failed", "refused",
                  "--boot-hd-fail-after-install", frames=300,
                  root=failed_root)
    assert failed.count("recovery=install") == 1
    assert "store-write-failed=io" in failed
    assert "recovery=product" not in failed
    reloaded = boot("install-write-failed", "exact", frames=30,
                    root=failed_root)
    assert "Hard disc scope: state=completed" not in reloaded
    assert "reason=hd-product" not in reloaded
    assert "reason=single application" in reloaded


if __name__ == "__main__":
    main()
