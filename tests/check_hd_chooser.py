#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check installed versus floppy decisions with invented FileCore media."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main() -> None:
    harness, core, fixtures, output = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    batch = Path(tempfile.mkdtemp(prefix="run-", dir=output))

    def boot(root: Path, content: Path, *options: str,
             rom: str = "arcdroid-outcome-exact.rom", frames: int = 160) -> str:
        command = [str(harness), str(core), str(fixtures / rom), str(content),
                   str(root), "--real-boot", "--machine", "A3020",
                   "--boot-hard-disc-auto", "--boot-frames", str(frames),
                   *options]
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == 0, (command, result.stdout[-3000:],
                                        result.stderr[-3000:])
        return (root / "evidence" / "real-boot.log").read_text()

    def helper(log: str) -> bytes:
        launch = [line for line in log.splitlines() if line.startswith("Launch:")]
        assert launch, log[-3000:]
        match = re.search(r"helper=(\S+\.adl)", launch[-1])
        assert match, launch[-1]
        return Path(match.group(1)).read_bytes()

    content = fixtures / "hd-family" / "valid-f.adf"
    root = batch / "flat"
    boot(root, content, "--boot-seed-product", frames=30)
    floppy = boot(root, content, "--boot-chooser-ask",
                  "--boot-chooser-pick-row", "0")
    assert "reason=chooser chose target" in floppy
    assert "reason=hd-product" not in floppy
    assert b"%Filer_Run ADFS::0.$.!Demo" in helper(floppy)
    choice = next((root / "save" / "arcdroid" / "launch").glob("*.choice"))
    assert choice.read_bytes().startswith(b"v1\napp\n")

    legacy = boot(root, content)
    assert "reason=chooser remembered target" in legacy
    assert "reason=hd-product" not in legacy
    assert b"%Filer_Run ADFS::0.$.!Demo" in helper(legacy)

    choice.unlink()
    installed = boot(root, content, "--boot-chooser-ask",
                     "--boot-chooser-pick-row", "1")
    assert "reason=hd-product" in installed
    assert b"%Filer_Run ADFS::4.$.ArcDroid.!Result" in helper(installed)
    assert choice.read_bytes().startswith(b"v4\ninstalled\n")

    remembered_installed = boot(root, content)
    assert "reason=hd-product" in remembered_installed
    assert b"%Filer_Run ADFS::4.$.ArcDroid.!Result" in helper(remembered_installed)

    stale = choice.read_bytes().splitlines()
    assert stale[-1] == b"1"
    stale[-1] = b"2"
    choice.write_bytes(b"\n".join(stale) + b"\n")
    fallback = boot(root, content, "--boot-chooser-ask",
                    "--boot-chooser-pick-row", "0")
    assert "reason=chooser asked" in fallback
    assert "reason=chooser chose target" in fallback
    assert "reason=hd-product" not in fallback
    assert b"%Filer_Run ADFS::0.$.!Demo" in helper(fallback)

    explicit = boot(root, content, "--boot-launch-app1")
    assert "reason=hd-product" not in explicit
    binding = next((root / "save" / "arcdroid" / "launch").glob("*.bind"))
    original = binding.read_bytes()
    assert original.startswith(b"v2\n")
    binding.write_bytes(original.replace(b"2144656d6f", b"2144656d61", 1))
    assert binding.read_bytes() != original
    choice.unlink()
    auto = boot(root, content)
    assert "reason=hd-product" in auto
    assert b"%Filer_Run ADFS::4.$.ArcDroid.!Result" in helper(auto)

    nested_root = batch / "nested"
    nested = fixtures / "hd-family" / "nested-d.adf"
    boot(nested_root, nested, "--boot-seed-product", frames=30)
    nested_pick = boot(nested_root, nested, "--boot-chooser-ask",
                       "--boot-chooser-pick-row", "0")
    assert "reason=chooser chose target" in nested_pick
    assert b"%Filer_Run ADFS::0.$.Folder.!Demo" in helper(nested_pick)
    nested_choice = next((nested_root / "save" / "arcdroid" / "launch")
                         .glob("*.choice"))
    assert nested_choice.read_bytes().startswith(b"v3\napp\n")
    nested_legacy = boot(nested_root, nested)
    assert "reason=chooser remembered target" in nested_legacy
    assert "reason=hd-product" not in nested_legacy
    assert b"%Filer_Run ADFS::0.$.Folder.!Demo" in helper(nested_legacy)

    ambiguous_root = batch / "ambiguous"
    ambiguous = fixtures / "hd-family" / "ambiguous-d.adf"
    boot(ambiguous_root, ambiguous, "--boot-seed-started", "product",
         "--boot-chooser-pick-row", "1")
    picked = boot(ambiguous_root, ambiguous, "--boot-chooser-ask",
                  "--boot-chooser-pick")
    assert picked.count("Hard disc scope: state=started") == 1
    assert picked.count("installation on the hard disc is started") == 1
    assert "reason=chooser chose target" in picked
    assert b"%Filer_Run ADFS::0.$.!Other" in helper(picked)

    match_root = batch / "name-match"
    name_match = fixtures / "hd-family" / "name-match-d.adf"
    boot(match_root, name_match, "--boot-seed-product", frames=30)
    matched = boot(match_root, name_match, frames=30)
    assert "reason=hd-product" in matched
    assert b"%Filer_Run ADFS::4.$.ArcDroid.!Result" in helper(matched)

    multi = boot(batch / "multi", fixtures / "hd-family" / "f-boot.m3u",
                 frames=30)
    assert "Hard disc scope:" not in multi
    adffs = boot(batch / "adffs", fixtures / "adffs" / "eligible.jfd",
                 "--boot-package", str(fixtures / "adffs" / "synthetic.zip"),
                 "--boot-jfd-boot", "adffs", frames=30)
    assert "JFD boot: path=adffs" in adffs
    assert "Hard disc scope:" not in adffs

    boot_root = batch / "boot-record"
    boot_content = fixtures / "hd-family" / "boot-d.adf"
    boot(boot_root, boot_content, "--boot-seed-product",
         rom="arcdroid-boot-prompt.rom", frames=30)
    boot(boot_root, boot_content, "--boot-seed-record",
         rom="arcdroid-boot-prompt.rom", frames=30)
    assert list((boot_root / "save" / "arcdroid" / "launch").glob("*.boot"))
    hit = boot(boot_root, boot_content, "--boot-expect-record-hit",
               rom="arcdroid-boot-prompt.rom", frames=30)
    assert "reason=hd-product" in hit
    assert b"%Filer_Run ADFS::4.$.ArcDroid.!Result" in helper(hit)


if __name__ == "__main__":
    main()
