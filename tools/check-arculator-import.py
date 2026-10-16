#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check the pinned Arculator import and dated modification notices offline."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

PIN = "7d3d2679589bba759197e3a9288a12749c797381"
REPLACEMENTS = {"src/disc_jfd.c", "src/disc_jfd.h"}
METADATA = {"upstream-blobs.json"}
NOTICE = re.compile(rb"ArcDroid\s+(?:modifications?\s+)?\(\d{4}-\d{2}-\d{2}[^)]*\)")


def blob(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def pristine(data, digest):
    lf = data.replace(b"\r\n", b"\n")
    return digest in {blob(data), blob(lf), blob(lf.replace(b"\n", b"\r\n"))}


def verify(root):
    directory = root / "engine/arculator"
    manifest = json.loads((directory / "upstream-blobs.json").read_text())
    if (manifest["schema"] != 1 or manifest["commit"] != PIN
            or set(manifest["authored_replacements"]) != REPLACEMENTS
            or f"Commit: `{PIN}`" not in (root / "engine/UPSTREAM.md").read_text()):
        raise ValueError("pinned upstream provenance mismatch")
    files = manifest["files"]
    actual = set()
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--", "engine/arculator/"], cwd=root)
    for name in filter(None, tracked.decode().split("\0")):
        path = root / name
        if path.is_symlink():
            raise ValueError("symlink in upstream tree")
        actual.add(path.relative_to(directory).as_posix())
    if actual != set(files) | REPLACEMENTS | METADATA:
        raise ValueError("Arculator tree differs from import allowlist")
    changed = []
    for name, digest in files.items():
        if not re.fullmatch(r"[a-f0-9]{40}", digest):
            raise ValueError(f"invalid upstream blob ID: {name}")
        data = (directory / name).read_bytes()
        unchanged = blob(data) == digest if name.endswith(".bin") else pristine(data, digest)
        if not unchanged:
            comments = re.findall(rb"/\*.*?\*/|//[^\r\n]*", data, re.S)
            if not name.endswith((".c", ".h")) or not any(NOTICE.search(c) for c in comments):
                raise ValueError(f"missing dated ArcDroid modification notice: {name}")
            changed.append(name)
    for name in REPLACEMENTS:
        data = (directory / name).read_bytes()[:1024]
        if b"ArcDroid" not in data or b"GPL-2.0-only" not in data:
            raise ValueError(f"missing authored replacement notice: {name}")
    return len(files), changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        count, changed = verify(args.root)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Arculator import guard: {error}", file=sys.stderr)
        return 1
    print(f"Arculator import guard: {count} pinned blobs; {len(changed)} dated modifications; "
          f"{len(REPLACEMENTS)} authored replacements passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
