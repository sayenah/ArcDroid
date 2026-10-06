#!/usr/bin/env python3
"""Verify the D-137 upstream tree, modification notices and include closure.

The optional tarball audit also compares every pristine git blob with its
archive member. No extraction or network access is needed. GPL-2.0-only.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile


ARCHIVE_SHA256 = "4e4624641cd1a83af275bd6da8765d4a0e5e0c83f008f059014cc24c5c33d59e"
PRISTINE = "fe96e411a747021c7d110b4302d819fe4bca179c"
NAMES = ("arm", "arm_common", "cp15", "mem", "iomd", "vidc20", "keyboard",
         "cmos", "superio", "i8042", "fdc")
ALLOWLIST = {"COPYING", "readme.txt", "src/rpcemu.h", "src/sound.h",
             "src/ide.h", "src/podules.h"} | {
    f"src/{name}.{extension}" for name in NAMES for extension in ("c", "h")}
SYSTEM_HEADERS = {"assert.h", "errno.h", "limits.h", "stdarg.h", "stddef.h",
                  "stdint.h", "stdio.h", "stdlib.h", "string.h", "time.h", "stdbool.h"}
PROJECT_HEADERS = {"engine/rpc/rpc_video.h", "engine/rpc/rpc_floppy.h", "engine/rpc/rpc_memory.h", "engine/rpc/rpc_rtc.h", "engine/rpc/rpc_namespace.h", "engine/rpc/rpc_timing.h", "engine/rpc/rpc_scheduler.h"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def without_comments(text):
    # Preserve strings and newlines so commented directives cannot satisfy
    # closure and line numbers still locate a rejected directive.
    pattern = r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*'
    return re.sub(pattern, lambda match: match[0] if match[0].startswith('"')
                  else "\n" * match[0].count("\n"), text, flags=re.S)


def verify(root, archive_path=None, compiler=None):
    root = root.resolve()
    directory = root / "engine/rpcemu"
    manifest = json.loads((directory / "manifest.json").read_text())
    require(manifest["schema"] == 1 and manifest["version"] == "0.9.5"
            and manifest["changeset"] == "1369:f005be758787"
            and manifest["pristine_commit"] == PRISTINE
            and manifest["tarball_sha256"] == ARCHIVE_SHA256,
            "unexpected release/provenance")
    records = manifest["files"]
    paths = [record["path"] for record in records]
    require(len(paths) == len(set(paths)) and set(paths) == ALLOWLIST,
            "manifest differs from D-137 allowlist")
    require(manifest["metadata_files"] == ["UPSTREAM.md", "manifest.json"],
            "unexpected upstream metadata")
    actual = set()
    for path in directory.rglob("*"):
        require(not path.is_symlink(), f"upstream symlink: {path}")
        if path.is_file():
            actual.add(path.relative_to(directory).as_posix())
    require(actual == ALLOWLIST | set(manifest["metadata_files"]),
            "upstream tree differs from manifest")
    patches = manifest["patches"]
    require(len({patch["id"] for patch in patches}) == len(patches),
            "duplicate patch ID")
    require(all(set(patch["files"]) <= ALLOWLIST for patch in patches),
            "patch targets outside allowlist")
    for record in records:
        relative = record["path"]
        require(record["original_path"] == relative, "unexpected original path")
        data = (directory / relative).read_bytes()
        changed = hashlib.sha256(data).hexdigest() != record["sha256"]
        lineage = [patch["id"] for patch in patches if relative in patch["files"]]
        require(record["patches"] == lineage, f"patch lineage mismatch: {relative}")
        require(changed == bool(lineage), f"unrecorded or empty patch: {relative}")
        if changed:
            require(b"ArcDroid modification (" in data[:2048],
                    f"missing modification notice: {relative}")
        if relative.endswith((".c", ".h")):
            header = record["licence_header"]
            require(header and "GNU General Public License" in header
                    and "version 2" in header and "any later version" in header
                    and header.encode() in data[:4096],
                    f"GPL notice missing or altered: {relative}")
    if archive_path:
        require(hashlib.sha256(archive_path.read_bytes()).hexdigest() == ARCHIVE_SHA256,
                "tarball SHA-256 mismatch")
        with tarfile.open(archive_path, "r:gz") as archive:
            for record in records:
                member = archive.getmember("rpcemu-0.9.5/" + record["original_path"])
                require(member.isfile(), "upstream source is not a regular file")
                original = archive.extractfile(member).read()
                require(hashlib.sha256(original).hexdigest() == record["sha256"],
                        f"original hash mismatch: {record['path']}")
                pristine = subprocess.check_output(
                    ["git", "show", f"{PRISTINE}:engine/rpcemu/{record['path']}"], cwd=root)
                require(original == pristine, f"pristine blob mismatch: {record['path']}")
    # Walk imported/project headers too; this checks indirect dependencies.
    checked = set()
    pending = [directory / relative for relative in paths if relative.endswith(".c")]
    while pending:
        path = pending.pop()
        if path in checked:
            continue
        checked.add(path)
        text = without_comments(path.read_text())
        for number, line in enumerate(text.splitlines(), 1):
            if not re.match(r"\s*#\s*include\b", line):
                continue
            match = re.fullmatch(r'\s*#\s*include\s*(?:"([^"\n]+)"|<([^>\n]+)>)\s*', line)
            require(match, f"nonliteral include: {path}:{number}")
            quoted, system = match.groups()
            if system:
                require(system in SYSTEM_HEADERS, f"unknown system include: {path}:{number}")
                continue
            require(not PurePosixPath(quoted).is_absolute() and ".." not in PurePosixPath(quoted).parts,
                    f"escaping include: {path}:{number}")
            choices = [path.parent / quoted, directory / "src" / quoted,
                       root / "engine/rpc" / quoted]
            resolved = next((candidate for candidate in choices if candidate.is_file()), None)
            require(resolved is not None, f"unresolved include: {path}:{number}: {quoted}")
            require(resolved.resolve() == resolved.absolute(),
                    f"symlinked dependency: {path}:{number}: {quoted}")
            relative = resolved.relative_to(root).as_posix()
            require(relative in PROJECT_HEADERS or relative in {
                "engine/rpcemu/" + allowed for allowed in ALLOWLIST},
                f"excluded dependency: {path}:{number}: {relative}")
            pending.append(resolved)
    if compiler:
        # GCC, MinGW and Clang dependency output uses -MM consistently.
        for name in NAMES:
            source = directory / f"src/{name}.c"
            output = subprocess.check_output(
                [compiler, "-MM", "-MT", "rpc", "-include",
                 str(root / "engine/rpc/rpc_namespace.h"), "-I", str(directory / "src"),
                 "-I", str(root / "engine/rpc"), str(source)], cwd=root, text=True)
            require(output.startswith("rpc:"), f"missing compiler dependency output: {source}")
            # Compare escaped paths using the compiler's Makefile spelling;
            # spaces in a checkout path do not split one dependency into two.
            dependencies = output[4:].replace("\\\n", " ")
            tokens = re.findall(r"(?:\\.|[^\s])+", dependencies)
            for token in tokens:
                token = re.sub(r"\\(.)", r"\1", token)
                dependency = Path(token)
                if not dependency.is_absolute():
                    dependency = root / dependency
                relative = dependency.relative_to(root).as_posix()
                require(relative in PROJECT_HEADERS or relative in {
                    "engine/rpcemu/" + allowed for allowed in ALLOWLIST},
                    f"compiler dependency outside allowlist: {relative}")
    return len(records), len(checked)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--tarball", type=Path)
    parser.add_argument("--compiler")
    args = parser.parse_args()
    try:
        files, closure = verify(args.root.resolve(), args.tarball, args.compiler)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError, tarfile.TarError) as error:
        print(f"RPCEmu import guard: {error}", file=sys.stderr)
        return 1
    print(f"RPCEmu import guard: {files} manifest files; notices and {closure}-file include closure passed"
          + ("; pristine tarball comparison passed" if args.tarball else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
