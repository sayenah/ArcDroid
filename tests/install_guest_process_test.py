#!/usr/bin/env python3
"""Two-process synthetic install, publication, save and readback test."""
from pathlib import Path
import io
import subprocess
import sys
import tempfile
import zipfile


def assert_no_package_bytes(data: Path, canaries: tuple[bytes, ...]) -> None:
    store_root = data / "save" / "arcdroid" / "install"
    if not store_root.exists():
        return
    for output in store_root.rglob("*"):
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
        for interrupted in ("interrupt-marker", "interrupt-directory",
                            "reset-directory", "interrupt-doorbell",
                            "reset-doorbell", None):
            data = Path(root) / (interrupted or "ordinary")
            for phase in ((interrupted, "first", "reload") if interrupted
                          else ("first", "reload")):
                subprocess.run((str(harness), str(core), str(rom),
                                str(playlist), str(data), "--install-guest",
                                str(package), phase), check=True)
                assert_no_package_bytes(data, canaries)


if __name__ == "__main__":
    main()
