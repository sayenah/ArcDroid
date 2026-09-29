"""End-to-end remembered prompt checks using executed synthetic SWIs."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
from power_on_guest_assert import assert_guest_shift_each_construction


def main() -> None:
    harness, core, rom, alternate_rom, content, output = sys.argv[1:]
    Path(output).mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    power_on = os.environ.get("ARCDROID_TEST_POWER_ON_SHIFT") == "1"
    command = [harness, core, rom, content, str(root), "--real-boot",
               "--boot-frames", "360" if power_on else "180"]

    def run(extra=(), firmware=None):
        selected = command.copy()
        if firmware:
            selected[2] = firmware
        result = subprocess.run(selected + list(extra), capture_output=True,
                                text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        log = (root / "evidence" / "real-boot.log").read_text()
        lines = [line for line in log.splitlines()
                 if line.startswith("Launch:")]
        if power_on:
            assert_guest_shift_each_construction(log, len(lines))
        return log, lines

    log, lines = run()
    assert len(lines) == 2 and "boot-watch=armed" in lines[0], lines
    assert "recovery=helper" in lines[1] and "Boot outcome: prompt" in log
    records = list((root / "save" / "arcdroid" / "launch").glob("*.boot"))
    assert len(records) == 1, records
    record = records[0]
    original = record.read_bytes()
    assert len(original) == 160 and original[:8] == b"ARCBOOT1"
    assert int.from_bytes(original[8:12], "little") == 1
    assert int.from_bytes(original[12:16], "little") == 2

    log, lines = run()
    assert len(lines) == 1 and "remembered boot observation; disc !Boot skipped" in lines[0], lines
    assert "boot-watch=disabled" in lines[0] and "Boot outcome:" not in log
    assert "helper=" in lines[0] and ".adl" in lines[0]

    revision_one = bytearray(original)
    revision_one[12:16] = (1).to_bytes(4, "little")
    record.write_bytes(revision_one)
    log, lines = run()
    assert len(lines) == 2 and "boot-watch=armed" in lines[0], lines
    assert "Boot outcome: prompt" in log
    assert int.from_bytes(record.read_bytes()[12:16], "little") == 2

    log, lines = run(("--boot-reset-once",))
    assert len(lines) == 2 and all("remembered boot observation" in line for line in lines), lines
    assert "Boot outcome:" not in log

    failed = subprocess.run(command + ["--boot-fail-construct"],
                            capture_output=True, text=True, check=False)
    assert failed.returncode == 2 and "Synthetic construction failure." in (
        failed.stdout + failed.stderr)
    assert not record.exists()
    record.write_bytes(original)

    # Change the root !Boot data byte in the writable shadow. Its directory
    # entries and application list stay identical, while the effective media
    # stamp must change.
    shadows = list((root / "save" / "arcdroid" / "media").glob("*/disc.adl"))
    assert len(shadows) == 1
    shadow = bytearray(shadows[0].read_bytes())
    logical_sector = 700
    track = logical_sector // 16
    physical_sector = ((track % 80) * 2 + track // 80) * 16 + logical_sector % 16
    boot_offset = physical_sector * 256
    assert shadow[boot_offset] == 13
    shadow[boot_offset] = 10
    shadows[0].write_bytes(shadow)
    log, lines = run()
    assert len(lines) == 2 and "boot-watch=armed" in lines[0], lines
    shadow[boot_offset] = 13
    shadows[0].write_bytes(shadow)

    cmos_files = list((root / "save" / "arcdroid" / "cmos").glob("*.bin"))
    assert len(cmos_files) == 1
    cmos = bytearray(cmos_files[0].read_bytes())
    cmos[216] ^= 1
    cmos_files[0].write_bytes(cmos)
    record.write_bytes(original)
    log, lines = run()
    assert len(lines) == 2 and "boot-watch=armed" in lines[0], lines
    cmos[216] ^= 1
    cmos_files[0].write_bytes(cmos)

    for extra, firmware in (
        ((), alternate_rom),
        (("--machine", "A3010"), None),
        (("--boot-disk-writes", "off"), None),
        (("--boot-jfd-boot", "native"), None),
        (("--install-saves", "persistent"), None),
    ):
        record.write_bytes(original)
        log, lines = run(extra, firmware)
        assert len(lines) == 2 and "boot-watch=armed" in lines[0], (extra, firmware, lines)
        assert "Boot outcome: prompt" in log

    # Every stamp and the compatibility revision must cause a new observation.
    for offset in (12, 16, 56, 96, 136, 140, 144, 148, 152, 156):
        changed = bytearray(original)
        changed[offset] ^= 1
        record.write_bytes(changed)
        log, lines = run()
        assert len(lines) == 2 and "boot-watch=armed" in lines[0], (offset, lines)
        assert "Boot outcome: prompt" in log, offset
    for malformed in (b"", original[:-1], original + b"x", b"malformed"):
        record.write_bytes(malformed)
        log, lines = run()
        assert len(lines) == 2 and "boot-watch=armed" in lines[0], lines
        assert "Boot outcome: prompt" in log

    record.write_bytes(b"malformed")
    log, lines = run(("--boot-fail-record-remove",))
    assert len(lines) == 2 and "boot-watch=armed" in lines[0], lines
    assert "Boot outcome: prompt" in log

    for option in ("--boot-launch-off", "--boot-launch-app1"):
        record.write_bytes(b"malformed")
        log, lines = run((option,))
        assert len(lines) == 1 and "boot-watch=disabled" in lines[0], lines
        assert record.read_bytes() == b"malformed" and "Boot outcome:" not in log

    record.unlink()
    log, lines = run(("--boot-fail-record-write",))
    assert len(lines) == 2 and "recovery=helper" in lines[1], lines
    assert not record.exists() and "Boot outcome: prompt" in log

    ask_root = Path(tempfile.mkdtemp(prefix="ask-single-", dir=output))
    ask_command = command.copy()
    ask_command[4] = str(ask_root)
    for attempt in (1, 2):
        result = subprocess.run(ask_command + ["--boot-chooser-ask"],
                                capture_output=True, text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        ask_log = (ask_root / "evidence" / "real-boot.log").read_text()
        ask_lines = [line for line in ask_log.splitlines()
                     if line.startswith("Launch:")]
        if power_on:
            assert_guest_shift_each_construction(ask_log, 2 if attempt == 1 else 1)
        if attempt == 1:
            assert len(ask_lines) == 2 and "recovery=helper" in ask_lines[1], ask_lines
        else:
            assert len(ask_lines) == 1 and "remembered boot observation" in ask_lines[0], ask_lines
            assert "helper=" in ask_lines[0] and ".adl" in ask_lines[0]
            assert "Boot outcome:" not in ask_log
        assert "recovery=chooser" not in ask_log, ask_log
    print("boot-observation-harness: passed")


if __name__ == "__main__":
    main()
