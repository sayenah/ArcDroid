# SPDX-License-Identifier: GPL-2.0-only
"""Two-process synthetic native IDE save/readback, with an Off control."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, rom, media, dd_media = map(Path, sys.argv[1:6])
    with tempfile.TemporaryDirectory(prefix="arc-native-hd-") as root_name:
        root = Path(root_name)
        def check(content: Path, phase: str, data: Path) -> str:
            result = subprocess.run((str(harness), str(core), str(rom),
                                     str(content), str(data),
                                     "--native-hd-guest", phase),
                                    capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr
            assert f"native hard disc guest {phase}" in result.stdout
            return result.stdout + result.stderr

        def run(content: Path, phase: str, data: Path,
                expected_pixel: str | None = None) -> str:
            output = check(content, phase, data)
            if phase == "first":
                assert output.index("store-publication=committed") < output.index(
                    "journal-commit lba=15456 count=1"), output
            match = re.search(r"signature=([0-9a-f]{16})", output)
            assert match, output
            pixel = re.search(r"pixel=([0-9a-f]{8})", output)
            assert pixel, output
            if phase in ("reload", "read-only"):
                assert pixel.group(1) == (expected_pixel or "00009400"), output
            return match.group(1)

        data = root / "session"
        first_signature = run(media, "first", data)
        stores = list((data / "save" / "arcdroid" / "hd").iterdir())
        assert len(stores) == 1
        store = stores[0]
        assert len(store.name) == 40 and (store / "ready.a").is_file()
        journal = store / "journal.g0"
        assert journal.is_file() and journal.stat().st_size > 124
        committed = journal.read_bytes()
        assert committed[204] == 0x5a
        reload_signature = run(media, "reload", data)
        assert first_signature != reload_signature, (first_signature, reload_signature)
        assert journal.read_bytes() == committed
        # A torn final record cannot become a partial guest command. Opening
        # read-only must leave even the incomplete bytes untouched.
        journal.write_bytes(committed + b"\x01\x02\x03")
        before_read_only = {p.relative_to(store): p.read_bytes()
                            for p in store.iterdir() if p.is_file()}
        assert run(media, "read-only", data) == reload_signature
        assert before_read_only == {p.relative_to(store): p.read_bytes()
                                    for p in store.iterdir() if p.is_file()}
        other = root / "other.adf"
        content = bytearray(media.read_bytes())
        content[-1024] ^= 0x40
        other.write_bytes(content)
        run(other, "first", data)
        assert len(list((data / "save" / "arcdroid" / "hd").iterdir())) == 2
        off = root / "off"
        off_signature = run(media, "off", off)
        assert not (off / "save" / "arcdroid" / "hd").exists()
        failed = root / "create-fail"
        assert "unavailable:directory-create-failed" in check(
            media, "create-fail", failed)
        assert not (failed / "save" / "arcdroid" / "hd" / store.name /
                    "ready.a").exists()
        probe = root / "probe-fail"
        assert "unavailable:write-probe-failed" in check(
            media, "probe-fail", probe)
        assert not list((probe / "save" / "arcdroid" / "hd").glob("*/ready.*"))
        formatter = root / "formatter-fail"
        assert "store-create=formatter-hash-mismatch" in check(
            media, "formatter-fail", formatter)
        assert not list((formatter / "save" / "arcdroid" / "hd").glob("*/ready.*"))
        check(media, "notice-rearm", root / "notice-rearm")
        check(media, "compact-neutral", root / "compact-neutral")
        create_recover = root / "create-recover"
        create_recover_signature = run(media, "create-recover", create_recover)
        assert run(media, "reload", create_recover) != create_recover_signature
        off_to_auto = root / "off-to-auto"
        off_to_auto_signature = run(media, "off-to-auto", off_to_auto)
        assert run(media, "reload", off_to_auto) != off_to_auto_signature
        auto_to_off = root / "auto-to-off"
        assert run(media, "auto-to-off", auto_to_off) == off_signature
        auto_off_store = next((auto_to_off / "save" / "arcdroid" / "hd").iterdir())
        assert (auto_off_store / "ready.a").is_file()
        assert (auto_off_store / "journal.g0").stat().st_size == 124
        append_failure = root / "append-fail"
        append_failure_signature = run(media, "append-fail", append_failure)
        assert append_failure_signature == off_signature
        assert (append_failure / "save" / "arcdroid" / "hd" /
                store.name / "ready.a").exists()
        failed_store = append_failure / "save" / "arcdroid" / "hd" / store.name
        failed_bytes = {p.name: p.read_bytes() for p in failed_store.iterdir()
                        if p.is_file()}
        run(media, "read-only", append_failure, "003fe9e9")
        assert failed_bytes == {p.name: p.read_bytes() for p in failed_store.iterdir()
                                if p.is_file()}
        recovered_signature = run(media, "recover", append_failure)
        assert run(media, "reload", append_failure) != recovered_signature
        cap = root / "journal-cap"
        assert run(media, "journal-cap", cap) == off_signature
        cap_recovered_signature = run(media, "recover", cap)
        assert run(media, "reload", cap) != cap_recovered_signature
        damaged = root / "damaged"
        run(media, "first", damaged)
        damaged_store = next((damaged / "save" / "arcdroid" / "hd").iterdir())
        manifest = damaged_store / "ready.a"
        broken = bytearray(manifest.read_bytes())
        broken[20] ^= 0x80
        manifest.write_bytes(broken)
        preserved = {p.name: p.read_bytes() for p in damaged_store.iterdir()
                     if p.is_file()}
        run(media, "damaged", damaged)
        assert preserved == {p.name: p.read_bytes() for p in damaged_store.iterdir()
                             if p.is_file()}
        for machine in ("a3010", "a4000"):
            absent_data = root / machine
            run(media, machine, absent_data)
            assert not (absent_data / "save" / "arcdroid" / "hd").exists()
        dd_data = root / "dd-a3020"
        run(dd_media, "dd-a3020", dd_data)
        assert len(list((dd_data / "save" / "arcdroid" / "hd").iterdir())) == 1
        ejected = root / "ejected"
        ejected_signature = run(media, "eject-reset", ejected)
        assert run(media, "reload", ejected) != ejected_signature
        playlist = root / "two-discs.m3u"
        playlist.write_text(f"{media.resolve()}\n{other.resolve()}\n")
        switched = root / "switched"
        switched_signature = run(playlist, "disk-index-reset", switched)
        assert run(playlist, "reload", switched) != switched_signature
        assert len(list((switched / "save" / "arcdroid" / "hd").iterdir())) == 1
    print("native-hd-guest-process: persistence, failures, resets, isolation and absent buses passed")


if __name__ == "__main__":
    main()
