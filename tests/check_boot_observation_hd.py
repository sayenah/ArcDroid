"""Actual boot-observation lookup/publication with a native IDE store."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, rom, content = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="arc-boot-hd-") as name:
        root = Path(name)

        def run(data: Path, mode: str | None = None) -> tuple[str, list[Path]]:
            command = [harness, core, rom, content, str(data), "--real-boot",
                       "--boot-frames", "180", "--machine", "A3020",
                       "--boot-cpu", "ARM250", "--boot-hard-disc-auto"]
            if mode:
                command += ["--boot-hd-write", mode]
            result = subprocess.run(command, capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr
            log = (data / "evidence" / "real-boot.log").read_text()
            assert "Hard disc: policy=auto attach=yes mode=writable" in log
            records = list((data / "save" / "arcdroid" / "launch").glob("*.boot"))
            return log, records

        baseline = root / "baseline"
        first, records = run(baseline)
        assert ("Boot outcome: prompt" in first and
                "recovery=helper" in first and len(records) == 1)
        second, records = run(baseline)
        assert "remembered boot observation" in second and len(records) == 1

        for mode in ("identical", "restore", "failed", "state-change",
                     "compact-neutral"):
            data = root / mode
            first, records = run(data, mode)
            assert "Boot outcome: prompt" in first and "recovery=helper" in first
            if mode == "failed":
                assert len(records) == 1, mode
            else:
                assert not records, mode
                relearned, records = run(data)
                assert "Boot outcome: prompt" in relearned and len(records) == 1, mode
            hit, records = run(data)
            assert "remembered boot observation" in hit and len(records) == 1, mode
    print("boot-observation-hd: write suppression, state change, replay and relearn passed")


if __name__ == "__main__":
    main()
