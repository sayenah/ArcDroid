"""Remembered prompt precedence on a synthetic multi-application disc."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, rom, content, output = sys.argv[1:]
    Path(output).mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    command = [harness, core, rom, content, str(root), "--real-boot",
               "--boot-frames", "180"]

    def run(*extra):
        result = subprocess.run(command + list(extra), capture_output=True,
                                text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        log = (root / "evidence" / "real-boot.log").read_text()
        lines = [line for line in log.splitlines() if line.startswith("Launch:")]
        return log, lines

    log, lines = run("--boot-chooser-pick")
    assert len(lines) == 3 and "recovery=chooser" in lines[1], lines
    assert "recovery=helper" in lines[2]
    records = list((root / "save" / "arcdroid" / "launch").glob("*.boot"))
    choices = list((root / "save" / "arcdroid" / "launch").glob("*.choice"))
    assert len(records) == len(choices) == 1

    choices[0].unlink()
    log, lines = run("--boot-chooser-pick")
    assert len(lines) == 2 and "remembered boot observation" in lines[0], lines
    assert "chooser chose target" in lines[1] and "Boot outcome:" not in log

    log, lines = run()
    assert len(lines) == 1 and "remembered boot observation" in lines[0], lines
    assert "decision=!Demo" in lines[0] and "boot-watch=disabled" in lines[0]
    assert "Boot outcome:" not in log

    log, lines = run("--boot-chooser-ask", "--boot-chooser-pick")
    assert len(lines) == 2 and "remembered boot observation" in lines[0], lines
    assert "chooser chose target" in lines[1] and "Boot outcome:" not in log

    log, lines = run("--boot-chooser-ask", "--boot-chooser-continue")
    assert len(lines) == 2 and "chooser chose desktop" in lines[1], lines
    log, lines = run()
    assert len(lines) == 1 and "remembered boot observation" in lines[0], lines
    assert "decision=none" in lines[0] and "helper=none" in lines[0]
    assert "boot-watch=disabled" in lines[0] and "Boot outcome:" not in log

    choices[0].unlink()
    run("--boot-chooser-ask", "--boot-chooser-pick")
    helpers = list((root / "save" / "arcdroid" / "launch").glob("*.adl"))
    assert len(helpers) == 1
    helpers[0].unlink()
    helpers[0].mkdir()
    log, lines = run()
    assert len(lines) == 1 and "remembered boot observation" in lines[0], lines
    assert "boot-watch=disabled" in lines[0] and "Boot outcome:" not in log
    assert log.count("Remembered launch helper publication failed.") == 1, log
    assert not records[0].exists()
    print("boot-observation-chooser-harness: passed")


if __name__ == "__main__":
    main()
