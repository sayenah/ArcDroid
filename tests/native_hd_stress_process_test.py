"""Host process proof for the authored native IDE compaction stress ROM."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


def main() -> None:
    harness, core, ordinary_rom, stress_rom, media = map(Path, sys.argv[1:6])
    with tempfile.TemporaryDirectory(prefix="arc-native-stress-") as root_name:
        root = Path(root_name)
        data = root / "session"

        def command(rom: Path, phase: str) -> tuple[str, ...]:
            return (str(harness), str(core), str(rom), str(media),
                    str(data), "--native-hd-guest", phase)

        def run(rom: Path, phase: str) -> tuple[str, str]:
            result = subprocess.run(command(rom, phase), capture_output=True,
                                    text=True, timeout=180)
            output = result.stdout + result.stderr
            assert result.returncode == 0, output
            assert "[ERROR]" not in output, output
            signature = re.search(r"signature=([0-9a-f]{16})", output)
            pixel = re.search(r"pixel=([0-9a-f]{8})", output)
            assert signature and pixel, output
            return signature.group(1), pixel.group(1)

        first_signature, first_pixel = run(ordinary_rom, "first")
        assert first_pixel != "00009400"
        store = next((data / "save" / "arcdroid" / "hd").iterdir())
        journal0 = store / "journal.g0"
        assert (store / "ready.a").is_file() and journal0.is_file()

        def kill_after(rom: Path, phase: str, needle: str,
                       ready: callable) -> str:
            log = root / f"{phase}.log"
            with log.open("w+") as output:
                process = subprocess.Popen(command(rom, phase), stdin=subprocess.PIPE,
                                           stdout=output, stderr=subprocess.STDOUT)
                deadline = time.monotonic() + 180
                try:
                    while time.monotonic() < deadline:
                        output.flush()
                        observed = log.read_text()
                        if needle in observed and ready(observed):
                            assert process.poll() is None, observed
                            process.kill()
                            process.wait(timeout=10)
                            assert "[ERROR]" not in observed, observed
                            return observed
                        assert process.poll() is None, observed
                        time.sleep(0.1)
                    raise AssertionError(f"timed out waiting for {needle}: {log.read_text()}")
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=10)

        stress_log = kill_after(
            stress_rom, "stress-pause", "journal-commit lba=4048 count=256",
            lambda observed: observed.count("journal-commit") == 9 and
            "native guest pause pixel=00009400" in observed and
            journal0.stat().st_size > 1024 * 1024)
        assert stress_log.count("journal-commit") == 9
        assert journal0.stat().st_size > 1024 * 1024
        before_compaction = journal0.read_bytes()
        assert (store / "ready.b").exists() is False

        compact_log = kill_after(
            ordinary_rom, "compact-pause",
            "store-compaction=manifest-renamed generation=1",
            lambda observed: "store-compaction=committed" in observed and
            "native guest pause pixel=00009400" in observed and
            (store / "ready.b").is_file())
        assert "store-compaction=committed" in compact_log
        assert (store / "ready.b").is_file()
        assert before_compaction != (store / "journal.g1").read_bytes()

        final_signature, final_pixel = run(ordinary_rom, "reload")
        assert final_signature != first_signature
        assert final_pixel == "00009400"
        assert (store / "ready.b").is_file()
    print("native-hd-stress-process: nine bounded commands, both kills, READY readback passed")


if __name__ == "__main__":
    main()
