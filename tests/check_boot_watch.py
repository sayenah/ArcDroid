# SPDX-License-Identifier: GPL-2.0-only
"""Executed-SWI boot recovery smoke checks against the libretro harness."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, rom, content, output, expected = sys.argv[1:]
    Path(output).mkdir(parents=True, exist_ok=True)
    data_root = tempfile.mkdtemp(prefix="run-", dir=output)
    switches = {
        "input-cancel": ["--boot-cancel-input"],
        "shift-cancel": ["--boot-cancel-shift"],
        "reset": ["--boot-reset-once"],
        "chooser-pick": ["--boot-chooser-pick"],
        "chooser-continue": ["--boot-chooser-continue"],
        "publish-fail-chooser": ["--boot-chooser-pick"],
        "off": ["--boot-launch-off"],
        "explicit": ["--boot-launch-app1"],
        "chooser-reset": ["--boot-chooser-reset", "--boot-cmos-sentinel", "--boot-frames", "180"],
        "chooser-unload": ["--boot-chooser-unload", "--boot-cmos-sentinel", "--boot-frames", "180"],
        "chooser-reset-fail": ["--boot-chooser-reset", "--boot-cmos-sentinel", "--boot-cmos-write-fail", "--boot-frames", "180"],
        "chooser-unload-fail": ["--boot-chooser-unload", "--boot-cmos-sentinel", "--boot-cmos-write-fail", "--boot-frames", "180"],
    }.get(expected, [])
    if expected in ("publish-fail", "publish-fail-chooser"):
        launch_parent = Path(data_root) / "save" / "arcdroid"
        launch_parent.mkdir(parents=True)
        (launch_parent / "launch").write_bytes(b"blocked")
    sentinel_cases = {"helper", "write", "chooser", "reset", "chooser-pick", "chooser-continue"}
    if expected in sentinel_cases:
        switches = switches + ["--boot-cmos-sentinel"]
    command = [harness, core, rom, content, data_root, "--real-boot"]
    result = subprocess.run(
        command + switches,
        capture_output=True, text=True, check=False,
    )
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    if expected in ("helper", "write", "reset", "chooser-pick"):
        assert "boot helper CMOS fields and checksum verified" in result.stdout, result.stdout
    log = (Path(data_root) / "evidence" / "real-boot.log").read_text()
    if expected in sentinel_cases:
        records = list((Path(data_root) / "save" / "arcdroid" / "cmos").glob("*.bin"))
        assert len(records) == 1, records
        record = records[0].read_bytes()
        assert len(record) == 256 and record[:8] == b"ARCCMOS2"
        assert record[216] == 0x5A and record[75] & 7 == 0 and record[199] & 7 == 1
    if expected in ("helper", "write", "reset"):
        helpers = list((Path(data_root) / "save" / "arcdroid" / "launch").glob("*.adl"))
        assert len(helpers) == 1, helpers
        helper = helpers[0].read_bytes()
        assert len(helper) == 655360 and hashlib.sha256(helper).hexdigest() == (
            "79d97918e7dd77d991eb420f12095164873760e31d07b6fb486ea5bef27756b8"
        )
    if expected == "write":
        staged = list((Path(data_root) / "save" / "arcdroid" / "media").glob("*/disc.adl"))
        assert len(staged) == 1, staged
        assert staged[0].read_bytes()[1024:1280] == b"\x5a" * 256
        assert Path(content).read_bytes()[1024:1280] != b"\x5a" * 256
        assert not list((Path(data_root) / "save" / "arcdroid" / "launch").glob("*.boot"))
    lines = [line for line in log.splitlines() if line.startswith("Launch:")]
    assert lines and "phase=initial" in lines[0], lines
    if expected not in ("off", "explicit"):
        assert "discovery-reason=disc boots itself" in lines[0], lines[0]
    if expected in ("helper", "write"):
        assert len(lines) == 2, lines
        assert "phase=recovery" in lines[1] and "recovery=helper" in lines[1], lines
        assert "Boot outcome: prompt" in log
        assert "disc_load 1" in log, log
    elif expected in ("chooser", "known-one-launchable"):
        assert len(lines) == 2 and "recovery=chooser" in lines[1], lines
        assert "Boot outcome: prompt" in log
        if expected == "known-one-launchable":
            assert "candidates=[1:!Demo:app,2:!Bad.Name:app]" in lines[1]
            assert "helper=none" in lines[1]
    elif expected == "none":
        assert len(lines) == 2 and "recovery=none" in lines[1], lines
        assert log.count("Boot outcome: prompt") == 1
        assert "Disc startup stopped. No application could be selected automatically." in log
        assert not list((Path(data_root) / "save" / "arcdroid" / "launch").glob("*.boot"))
    elif expected in ("publish-fail", "publish-fail-chooser"):
        assert len(lines) == (3 if expected == "publish-fail-chooser" else 2), lines
        assert "recovery=failed" in lines[-1], lines
        assert log.count("disc_load 0 ") == 1 and "disc_load 1 " not in log
        assert log.count("Boot outcome: prompt") == 1
    elif expected in ("off", "explicit"):
        assert len(lines) == 1 and "boot-watch=disabled" in lines[0], lines
        assert "Boot outcome: prompt" not in log
        assert not list((Path(data_root) / "save" / "arcdroid" / "launch").glob("*.boot"))
    elif expected == "reset":
        assert len(lines) == 4, lines
        assert sum("recovery=helper" in line for line in lines) == 2, lines
        assert log.count("Boot outcome: prompt") == 2, log
    elif expected in ("chooser-pick", "chooser-continue"):
        assert len(lines) == 3 and "recovery=chooser" in lines[1], lines
        terminal = "recovery=helper" if expected == "chooser-pick" else "recovery=none"
        assert terminal in lines[2], lines
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        replay = (Path(data_root) / "evidence" / "real-boot.log").read_text()
        replay_lines = [line for line in replay.splitlines() if line.startswith("Launch:")]
        assert len(replay_lines) == 2 and terminal in replay_lines[1], replay_lines
        assert "Boot outcome: prompt" in replay
        hit = subprocess.run(command, capture_output=True, text=True, check=False)
        assert hit.returncode == 0, hit.stdout + hit.stderr
        hit_log = (Path(data_root) / "evidence" / "real-boot.log").read_text()
        hit_lines = [line for line in hit_log.splitlines() if line.startswith("Launch:")]
        assert len(hit_lines) == 1 and "remembered boot observation" in hit_lines[0], hit_lines
        assert "boot-watch=disabled" in hit_lines[0] and "Boot outcome:" not in hit_log
        assert ("helper=" in hit_lines[0] and ".adl" in hit_lines[0]) == (expected == "chooser-pick")
        if expected == "chooser-pick":
            asked = subprocess.run(command + ["--boot-chooser-ask", "--boot-chooser-pick"],
                                   capture_output=True, text=True, check=False)
            assert asked.returncode == 0, asked.stdout + asked.stderr
            ask_log = (Path(data_root) / "evidence" / "real-boot.log").read_text()
            assert "remembered boot observation" in ask_log and "chooser chose target" in ask_log
            assert "Boot outcome:" not in ask_log
            choices = list((Path(data_root) / "save" / "arcdroid" / "launch").glob("*.choice"))
            assert len(choices) == 1, choices
            choice = choices[0].read_bytes()
            assert b"2144656d6f" in choice
            for stale in (choice.replace(b"2144656d6f", b"214e6f7065"), b"malformed\n"):
                choices[0].write_bytes(stale)
                ignored = subprocess.run(command, capture_output=True, text=True, check=False)
                assert ignored.returncode == 0, ignored.stdout + ignored.stderr
                ignored_log = (Path(data_root) / "evidence" / "real-boot.log").read_text()
                assert "remembered boot observation" in ignored_log
                assert "chooser chose target" not in ignored_log and "Boot outcome:" not in ignored_log
    elif expected in ("chooser-reset", "chooser-unload", "chooser-reset-fail", "chooser-unload-fail"):
        assert "recovery=chooser" in log and "Boot outcome: prompt" in log
        assert "boot chooser CMOS seed preserved on" in result.stdout
        if expected.endswith("-fail"):
            assert "after failed write" in result.stdout
        if "reset" in expected:
            assert len(lines) == 3 and "phase=initial" in lines[2], lines
        else:
            assert len(lines) == 2, lines
    else:
        assert all("phase=initial" in line for line in lines), lines
        assert "Boot outcome: prompt" not in log, lines
        terminal = "cancelled" if expected in ("input-cancel", "shift-cancel") else expected
        assert any(f"boot-watch={terminal}" in line for line in lines[1:]), lines
    print(f"boot-watch-harness: {expected} passed")


if __name__ == "__main__":
    main()
