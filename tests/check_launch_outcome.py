#!/usr/bin/env python3
"""Run authored ROM SWI sequences through the libretro harness."""
import pathlib
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, fixtures, output = map(pathlib.Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    batch = pathlib.Path(tempfile.mkdtemp(prefix="run-", dir=output))
    content = fixtures / "hd-family" / "valid-f.adf"
    cases = {
        "exact": ("Launch outcome: armed target=ADFS::0.$.!Demo",
                  "Launch outcome: ended rc=none refused=0"),
        "other": (),
        "no-poll": ("Launch outcome: armed target=ADFS::0.$.!Demo",),
        "close-init": ("Launch outcome: armed target=ADFS::0.$.!Demo",
                       "Launch outcome: cancelled=initialise"),
        "refused": ("JFD write refused: image=1 track=0 side=0 sector=0",
                    "Launch outcome: ended rc=none refused=1",
                    "recovery=install"),
        "refused-fail": ("Launch outcome: ended rc=none refused=1",
                         "reconstruction-failed=Synthetic construction failure.",
                         "Hard-disc recovery could not restart"),
        "refused-collision": ("Launch outcome: ended rc=none refused=1",
                              "Hard disc scope: state=unowned"),
        "refused-second": ("Launch outcome: ended rc=none refused=1",
                            "recovery=install"),
        "delayed-input": ("Launch outcome: cancelled=input",),
        "delayed-reset": ("Launch outcome: cancelled=reset",),
    }
    for pattern, witnesses in cases.items():
        run_dir = batch / pattern
        refused = pattern.startswith("refused")
        selected_content = (fixtures / "hd-family" / "write-protected-f.jfd"
                            if refused else content)
        rom_pattern = "refused" if refused else pattern
        command = [str(harness), str(core),
                   str(fixtures / f"arcdroid-outcome-{rom_pattern}.rom"),
                   str(selected_content), str(run_dir), "--real-boot", "--machine",
                   "A3020", "--boot-hard-disc-auto", "--boot-frames",
                   "200" if refused else "100"]
        if pattern == "refused-fail":
            command += ["--boot-fail-hd-recover"]
        if pattern == "refused-collision":
            command += ["--boot-hd-preflight-collision"]
        if pattern == "refused-second":
            command += ["--boot-rearm-after-install"]
        if pattern == "delayed-input":
            command += ["--boot-cancel-input"]
        if pattern == "delayed-reset":
            command += ["--boot-reset-once", "--boot-reset-at", "10"]
        run = subprocess.run(command, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
        assert run.returncode == 0, (pattern, run.stdout[-3000:])
        log = (run_dir / "evidence" / "real-boot.log").read_text()
        assert "Hard disc scope: state=absent" in log, pattern
        assert "Hard disc: policy=auto attach=yes mode=writable" in log, pattern
        for witness in witnesses:
            assert witness in log, (pattern, witness)
        if pattern == "other":
            assert "Launch outcome:" not in log, pattern
        if pattern == "no-poll":
            assert "Launch outcome: ended" not in log, pattern
        if pattern == "exact":
            assert "recovery=install" not in log, pattern
        if pattern == "close-init":
            assert "Launch outcome: ended" not in log, pattern
        if pattern == "refused":
            assert ".install.adl" in log, pattern
        if pattern == "refused-fail":
            assert "recovery=install reconstruction-failed" in log
            assert "phase=recovery" not in log
        if pattern == "refused-collision":
            assert "recovery=install" not in log
        if pattern == "refused-second":
            assert log.count("Launch outcome: ended rc=none refused=1") == 2
            assert log.count("recovery=install") == 1
            assert log.count("disc_load 1 ") == 2
        if pattern in ("delayed-input", "delayed-reset"):
            assert "Launch outcome: ended" not in log


if __name__ == "__main__":
    main()
