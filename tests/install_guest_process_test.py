#!/usr/bin/env python3
"""Two-process synthetic install, publication, save and readback test."""
from pathlib import Path
import io
import subprocess
import sys
import tempfile
import zipfile


def assert_no_package_bytes(data: Path, canaries: tuple[bytes, ...],
                            logs: bytes) -> None:
    # The copied package is the only canary-bearing input. Scan every other
    # file, including CMOS, staged media, stores, evidence and logs.
    for canary in canaries:
        if canary in logs:
            raise AssertionError("package/support bytes reached frontend logs")
    for output in data.rglob("*"):
        if output == data / "system" / "arcdroid" / "adffs" / "synthetic.zip":
            continue
        if output.is_file():
            content = output.read_bytes()
            for canary in canaries:
                if canary in content:
                    raise AssertionError(f"package/support bytes reached {output}")


def main() -> None:
    harness, core, rom, playlist, package = map(Path, sys.argv[1:6])
    package_canaries = (
        b"Synthetic 1.0\0",
        b"ArcDroid synthetic ADFFS SetVars fixture\n",
        b"Set ArcFixture Builtin\r",
    )
    with zipfile.ZipFile(package) as archive:
        assert package_canaries[0] in archive.read("!ADFFS/ADFFS")
        assert package_canaries[1] in archive.read("!ADFFS/SetVars")
        with zipfile.ZipFile(io.BytesIO(archive.read("!ADFFS/obey"))) as obey:
            assert package_canaries[2] in obey.read("F0012301")
    canaries = (*package_canaries, b"ArcDroid install doorbell\r")
    with tempfile.TemporaryDirectory(prefix="arc-install-guest-") as root:
        def run(data: Path, phase: str) -> None:
            result = subprocess.run((str(harness), str(core), str(rom),
                                     str(playlist), str(data), "--install-guest",
                                     str(package), phase), capture_output=True)
            if result.returncode:
                sys.stderr.buffer.write(result.stdout + result.stderr)
                raise subprocess.CalledProcessError(result.returncode,
                                                    result.args)
            assert_no_package_bytes(data, canaries,
                                    result.stdout + result.stderr)

        for interrupted in ("interrupt-marker", "interrupt-directory",
                            "reset-directory", "interrupt-doorbell",
                            "reset-doorbell", None):
            data = Path(root) / (interrupted or "ordinary")
            for phase in ((interrupted, "first", "reload") if interrupted
                          else ("first", "reload")):
                run(data, phase)
                if phase == "interrupt-marker":
                    run(data, "route-unpublished-probe")
        ready = Path(root) / "ordinary"
        for phase in ("route-off-ready", "route-machine-ready",
                      "route-unwritable-ready", "route-temporary-ready",
                      "route-failure-io-ready", "route-failure-cap-ready"):
            run(ready, phase)
        run(ready, "route-grow-journal-ready")
        assert any(path.stat().st_size > 1024 * 1024 for path in
                   (ready / "save" / "arcdroid" / "install").rglob("journal.g*"))
        run(ready, "compaction-reload")
        for phase in ("route-new-probe", "route-machine-new",
                      "route-nonnative-new", "route-no-package",
                      "route-untested-package", "route-script-mismatch",
                      "route-no-311"):
            run(Path(root) / phase, phase)


if __name__ == "__main__":
    main()
