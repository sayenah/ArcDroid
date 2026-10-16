#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check license statements on every tracked ArcDroid-authored file."""
import argparse
from pathlib import Path
import re
import subprocess
import sys

VENDORED = ("engine/arculator/", "engine/rpcemu/", "third_party/")
EXCEPTIONS = {"COPYING", "libretro/include/libretro.h", "engine/rpc/rpc_audio.c"}
OWNED_JFD = {"engine/arculator/src/disc_jfd.c", "engine/arculator/src/disc_jfd.h"}
OWNED_METADATA = {"engine/arculator/upstream-blobs.json", "engine/rpcemu/UPSTREAM.md",
                  "engine/rpcemu/manifest.json"}


def statement(data):
    return bool(re.search(rb"GPL-2\.0-only|license\s*=\s*\"GPLv2\"", data[:1024]))


def verify(root, include_untracked=False):
    command = ["git", "ls-files", "-z", "--cached"]
    if include_untracked:
        command += ["--others", "--exclude-standard"]
    names = set(filter(None, subprocess.check_output(command, cwd=root).decode().split("\0")))
    missing = []
    for name in sorted(names):
        if name in EXCEPTIONS or (name.startswith(VENDORED) and name not in OWNED_JFD | OWNED_METADATA):
            continue
        path = root / name
        if not path.is_file():
            continue
        sidecar = name + ".license"
        if statement(path.read_bytes()) or (sidecar in names and statement((root / sidecar).read_bytes())):
            continue
        missing.append(name)
    return missing


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--include-untracked", action="store_true")
    args = parser.parse_args()
    missing = verify(args.root, args.include_untracked)
    for name in missing:
        print(f"missing ArcDroid license statement: {name}", file=sys.stderr)
    if missing:
        return 1
    print("license guard: every tracked ArcDroid-authored file has a license statement")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
