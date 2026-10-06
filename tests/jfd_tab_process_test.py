"""Synthetic write-tab matrices through both controllers and fresh processes."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile


def main():
    harness, core, fixtures = map(Path, sys.argv[1:4])
    mode = sys.argv[4]
    tab = fixtures / "jfd-tab"
    sources = list(tab.glob("*.jfd"))
    before = {p: hashlib.sha256(p.read_bytes()).digest() for p in sources}

    def run(rom, image, root, args):
        if args[0] == "--jfd-tab-route" and len(args) == 4:
            logs = []
            for writes in ("off", "shadow", "original"):
                logs.append(run(rom, image, root, [*args, writes]))
            assert len(set(logs)) == 1, "route or notice changed with Disk Writes"
            return logs[0]
        result = subprocess.run([str(harness), str(core), str(fixtures / rom),
                                 str(image), str(root), *args],
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout + result.stderr

    with tempfile.TemporaryDirectory(prefix="arc-tab-") as temp:
        root = Path(temp)
        if mode == "guest":
            for machine, infix in (("A3000", ""), ("A3020", "newio-")):
                for kind, rom_kind in (("captured", "save"), ("created", "create")):
                    rom = f"arcdroid-jfd-{infix}{rom_kind}-test.rom"
                    for header in ("0", "1", "2", "3", "legacy"):
                        image = tab / f"{kind}-{header}.jfd"
                        for policy in ("as-imaged", "off"):
                            for writes in ("off", "shadow", "original"):
                                case = root / f"{machine}-{kind}-{header}-{policy}-{writes}"
                                enabled = writes != "off" and (policy == "off" or header == "2")
                                outcome = "006a0000" if enabled else "00006a00"
                                args = ["--jfd-tab-step", machine, policy, writes, outcome, "basic"]
                                log = run(rom, image, case, args)
                                assert f"reason={'disk-writes-off' if writes == 'off' and (policy == 'off' or header == '2') else 'rw-flag' if header == '2' else 'tab-off' if policy == 'off' else 'legacy-header' if header == 'legacy' else 'wp-flag' if header in ('1','3') else 'no-flag'}" in log
                                if policy == "off":
                                    assert "tab=off" in log and f"header={'yes' if header == '2' else 'no'} effective=yes" in log
                                slots = list((case / "save/arcdroid/jfd").glob("*.[ab]"))
                                assert bool(slots) == enabled
                                if enabled:
                                    args[4] = "00001500"
                                    run(rom, image, case, args)
                for kind, rom_kind in (("captured", "save"), ("created", "create")):
                    rom = f"arcdroid-jfd-{infix}{rom_kind}-test.rom"
                    for header in ("0", "3", "legacy"):
                        for failure in ("write-fault", "readback-fault", "rename-fault"):
                            run(rom, tab / f"{kind}-{header}.jfd",
                                root / f"{machine}-{kind}-{header}-{failure}",
                                ["--jfd-tab-step", machine, "off", "shadow", "0000006a", failure])
                for raw in ("arcdroid-test.adf", "arcdroid-demo.adl"):
                    for policy in ("as-imaged", "off"):
                        for writes in ("off", "shadow", "original"):
                            run("arcdroid-test.rom", fixtures / raw,
                                root / f"{machine}-{raw}-{policy}-{writes}",
                                ["--jfd-tab-step", machine, policy, writes, "0", "raw"])
                for header in ("0", "1", "3", "legacy"):
                    case = root / f"{machine}-probe-{header}"
                    log = run(f"arcdroid-jfd-{infix}allocation-veto-test.rom",
                              tab / f"probe-{header}.jfd", case,
                              ["--jfd-tab-step", machine, "off", "shadow", "00001500", "probe-veto"])
                    assert "reason=probed-unformatted" in log
                    assert not list((case / "save/arcdroid/jfd").glob("*.[ab]"))
                image = tab / "captured-3.jfd"
                rom = f"arcdroid-jfd-{infix}save-test.rom"
                run(rom, image, root / f"{machine}-sampling",
                    ["--jfd-tab-step", machine, "as-imaged", "shadow", "00006a00", "sampling"])
                run(rom, image, root / f"{machine}-hide",
                    ["--jfd-tab-step", machine, "off", "shadow", "006a0000", "hide-restore"])
        elif mode == "routing":
            rom = "arcdroid-test.rom"
            package = str(fixtures / "adffs/synthetic.zip")
            for header in ("0", "1", "2", "3", "legacy"):
                for variant in ("route", "route-os", "route-layout"):
                    image = tab / f"{variant}-{header}.jfd"
                    for policy in ("as-imaged", "off"):
                        for boot in ("auto", "native", "adffs"):
                            case = root / f"{variant}-{header}-{policy}-{boot}"
                            log = run(rom, image, case,
                                      ["--jfd-tab-route", boot, policy, package])
                            native_writable = boot == "auto" and (variant == "route" or (variant == "route-os" and header == "legacy")) and (policy == "off" or header == "2")
                            expected = "reason=option" if boot == "native" else "reason=native-writable" if native_writable else "path=adffs"
                            assert expected in log, f"{variant} {header} {policy} {boot}\n{log}"
                            assert f"tab-adffs-notices={int(policy == 'off' and expected == 'path=adffs')}" in log, log
            for header in ("0", "1", "2", "3", "legacy"):
                image = tab / f"route-{header}.jfd"
                case = root / f"saved-{header}"
                run("arcdroid-jfd-save-test.rom", image, case,
                    ["--jfd-tab-step", "A3000", "off", "shadow", "006a0000", "basic"])
                slots = {p: p.read_bytes() for p in (case / "save/arcdroid/jfd").glob("*.[ab]")}
                assert slots
                for damaged in (False, True):
                    if damaged:
                        for p in slots:
                            p.write_bytes(b"damaged")
                    current = {p: p.read_bytes() for p in slots}
                    for policy in ("as-imaged", "off"):
                        for boot in ("auto", "native", "adffs"):
                            log = run(rom, image, case, ["--jfd-tab-route", boot, policy, package])
                            authorized = policy == "off" or header == "2"
                            expected = "reason=option" if boot == "native" else "reason=native-writable" if boot == "auto" and authorized else "reason=native-saves" if authorized and not damaged else "path=adffs"
                            assert expected in log, log
                            assert f"tab-hidden-notices={int(policy == 'as-imaged' and header != '2')}" in log, log
                            assert f"tab-adffs-notices={int(policy == 'off' and expected == 'path=adffs')}" in log, log
                            assert all(p.read_bytes() == data for p, data in current.items())
                    for p, data in slots.items():
                        p.write_bytes(data)
            for image, reason in ((tab / "mixed.m3u", "multi-image"),
                                  (fixtures / "adffs/count-two.jfd", "multi-disc")):
                for policy in ("as-imaged", "off"):
                    for boot in ("auto", "native", "adffs"):
                        log = run(rom, image, root / f"{reason}-{policy}-{boot}",
                                  ["--jfd-tab-route", boot, policy, package])
                        assert f"reason={reason}" in log and "tab-adffs-notices=0" in log
        else:
            assert mode == "set"
            run("arcdroid-test.rom", tab / "mixed.m3u", root / "set",
                ["--jfd-tab-set", str(tab / "set-legacy.jfd"), str(tab / "request-3.jfd")])
    assert all(hashlib.sha256(p.read_bytes()).digest() == before[p] for p in sources)
    print(f"jfd-tab-{mode}: passed; all source hashes unchanged")


if __name__ == "__main__":
    main()
