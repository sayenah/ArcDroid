"""ADFFS and install routes never arm the boot observer."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, rom, content, package, output, route = sys.argv[1:]
    Path(output).mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    command = [harness, core, rom, content, str(root), "--real-boot",
               "--boot-frames", "180", "--boot-package", package,
               "--boot-expect-disabled"]
    command += (["--boot-jfd-boot", "adffs"] if route == "adffs" else
                ["--install-saves", "persistent"])
    def run():
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "boot observer disabled for all frames" in result.stdout
        log = (root / "evidence" / "real-boot.log").read_text()
        assert ("JFD boot: path=adffs script=" if route == "adffs" else
                "JFD boot: path=adffs-install") in log
        assert "Boot outcome:" not in log
        assert all("boot-watch=disabled" in line for line in log.splitlines()
                   if line.startswith("Launch:"))

    run()
    launch = root / "save" / "arcdroid" / "launch"
    assert not list(launch.glob("*.boot"))
    cmos = list((root / "save" / "arcdroid" / "cmos").glob("*.bin"))
    assert len(cmos) == 1
    content_id = cmos[0].name.split(".")[-2]
    launch.mkdir(exist_ok=True)
    record = launch / f"{content_id}.boot"
    record.write_bytes(b"malformed")
    run()
    assert record.read_bytes() == b"malformed"
    print(f"boot-watch-route: {route} disabled")


if __name__ == "__main__":
    main()
