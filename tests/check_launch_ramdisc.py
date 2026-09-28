"""Synthetic helper publication and option lifecycle checks. GPL-2.0-only."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, rom, selfboot, chooser, initial_chooser, output = sys.argv[1:]
    Path(output).mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="ramdisc-", dir=output))
    request = b"%BASIC -quit ADFS::1.$.ArcRAM\r"
    source = Path(selfboot).read_bytes()

    def run(content: str, args=(), fresh=False, data_dir=None):
        data = (data_dir if data_dir is not None else
                Path(tempfile.mkdtemp(prefix="ramdisc-", dir=output))
                if fresh else root)
        command = [harness, core, rom, content, str(data), "--real-boot",
                   "--boot-frames", "180", *args]
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        if "--boot-cmos-ramdisc" in args:
            assert "reconstructed guest RAM-disc CMOS byte preserved" in result.stdout
        log = (data / "evidence" / "real-boot.log").read_text()
        helpers = list((data / "save" / "arcdroid" / "launch").glob("*.adl"))
        assert len(helpers) == 1, helpers
        return data, log, helpers[0], helpers[0].read_bytes()

    _, log, helper, auto = run(selfboot)
    assert auto.count(request) == 1 and b"XOS_ChangeDynamicArea" in auto
    assert log.count("RAM disc: request=32K if absent") == 1
    assert "recovery=helper" in log
    boot_records = list(helper.parent.glob("*.boot"))
    assert len(boot_records) == 1 and boot_records[0].read_bytes()[:8] == b"ARCBOOT1"
    revision_one = boot_records[0].read_bytes()

    _, log, _, cached = run(selfboot)
    assert cached == auto and "remembered boot observation" in log
    assert log.count("RAM disc: request=32K if absent") == 1
    assert boot_records[0].read_bytes() == revision_one

    _, log, _, disabled = run(selfboot, ("--boot-ramdisc", "Disabled"))
    assert request not in disabled and b"XOS_ChangeDynamicArea" not in disabled
    assert log.count("RAM disc: disabled") == 1
    assert "remembered boot observation" in log and "Boot outcome:" not in log
    assert boot_records[0].read_bytes() == revision_one
    assert disabled != auto
    assert hashlib.sha256(disabled).hexdigest() == (
        "c3c34b4be32a5aa8528b1387a3ef1d70287d2aaede52290cc6ddcb4d0596a2c1"
    )

    _, log, _, renewed = run(selfboot)
    assert renewed == auto and "remembered boot observation" in log
    assert boot_records[0].read_bytes() == revision_one

    _, log, _, toggled = run(selfboot, ("--boot-ramdisc-toggle-reset",))
    assert toggled == disabled and "RAM disc: disabled" in log
    _, log, _, toggled = run(selfboot, ("--boot-ramdisc", "Disabled",
                                      "--boot-ramdisc-toggle-reset"))
    assert toggled == auto and "RAM disc: request=32K if absent" in log

    _, log, _, explicit = run(selfboot, ("--boot-launch-app1",), fresh=True)
    assert explicit.count(request) == 1 and "RAM disc: request=32K if absent" in log

    single = str(Path(initial_chooser).with_name("boot-matrix-0-file-one.adf"))
    _, log, _, automatic = run(single, fresh=True)
    assert automatic.count(request) == 1 and "reason=single application" in log

    auto_data, _, _, _ = run(selfboot, ("--boot-launch-app1",), fresh=True)
    off_data, _, _, _ = run(selfboot, ("--boot-launch-app1",
                                       "--boot-ramdisc", "Disabled"), fresh=True)
    def cmos(data):
        records = list((data / "save" / "arcdroid" / "cmos").glob("*.bin"))
        assert len(records) == 1, records
        return records[0].read_bytes()
    assert cmos(auto_data) == cmos(off_data)

    changed_data, changed_log, _, changed_helper = run(
        selfboot, ("--boot-cmos-ramdisc",), fresh=True)
    assert "recovery=helper" in changed_log and changed_helper == auto
    assert cmos(changed_data) != cmos(auto_data)

    _, log, _, picked = run(chooser, ("--boot-chooser-pick",), fresh=True)
    assert picked.count(request) == 1 and "RAM disc: request=32K if absent" in log
    assert "recovery=chooser" in log

    chosen_data, log, _, picked = run(initial_chooser,
                                      ("--boot-chooser-pick",), fresh=True)
    assert picked.count(request) == 1 and "RAM disc: request=32K if absent" in log
    assert "chooser chose target" in log
    _, log, _, remembered = run(initial_chooser, data_dir=chosen_data)
    assert remembered == picked and "remembered" in log

    # An unavailable publication still follows the established refusal path.
    blocked = Path(tempfile.mkdtemp(prefix="ramdisc-blocked-", dir=output))
    parent = blocked / "save" / "arcdroid"
    parent.mkdir(parents=True)
    (parent / "launch").write_bytes(b"blocked")
    command = [harness, core, rom, selfboot, str(blocked), "--real-boot",
               "--boot-frames", "180"]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    failure_log = (blocked / "evidence" / "real-boot.log").read_text()
    assert "recovery=failed" in failure_log and "RAM disc: request" not in failure_log
    assert Path(selfboot).read_bytes() == source
    print("launch-ramdisc-harness: passed")


if __name__ == "__main__":
    main()
