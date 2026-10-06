#!/usr/bin/env python3
"""Check private engine objects in Mach-O, PE/COFF and ELF. GPL-2.0-only."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def object_format(path):
    magic = path.read_bytes()[:4]
    if magic == b"\x7fELF":
        return "elf"
    if magic in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe",
                 b"\xfe\xed\xfa\xcf", b"\xfe\xed\xfa\xce"):
        return "macho"
    if magic[:2] in (b"\x64\x86", b"\x4c\x01", b"\x64\xaa"):
        return "coff"
    raise ValueError(f"unsupported object format: {path}")


def read_symbols(path, nm, sanitized=False):
    form = object_format(path)
    output = subprocess.check_output([nm, "-g", "-P", str(path)], text=True)
    # Darwin's POSIX nm does not encode weak attributes in its type letter.
    # Native nm and llvm-nm both expose those attributes in Mach-O mode.
    if form == "macho":
        attributes = subprocess.check_output([nm, "-g", "-m", str(path)], text=True)
        require(not re.search(r"\bweak\b", attributes), f"weak external symbol: {path}")
    symbols = []
    for line in output.splitlines():
        parts = line.split()
        require(len(parts) >= 2 and len(parts[1]) == 1,
                f"unrecognised nm output: {path}: {line}")
        name, kind = parts[:2]
        if form == "macho" or (form == "coff" and path.read_bytes()[:2] == b"\x4c\x01"):
            require(name.startswith("_"), f"missing ABI decoration: {path}: {name}")
            name = name[1:]
        # Compiler bookkeeping is not an engine variable. Apple ASan emits
        # this one shared registration flag as a common even with -fno-common.
        if sanitized and name == "___asan_globals_registered" and kind == "C":
            continue
        require(kind not in "Cc", f"common symbol: {path}: {name}")
        require(kind not in "WwVv", f"weak external symbol: {path}: {name}")
        require(kind in "TtDdBbSsRrAU", f"unexpected symbol type {kind}: {path}: {name}")
        # MinGW's COMDAT import-pointer helpers describe the underlying
        # reference. Validate that referent with the same engine policy.
        if form == "coff" and name.startswith(".refptr.") and kind == "R":
            symbols.append((name[len(".refptr."):], True))
        else:
            symbols.append((name, kind == "U"))
    return symbols


def objects(directory):
    paths = sorted(path for path in directory.rglob("*")
                   if path.suffix in (".o", ".obj") and path.is_file())
    require(paths, f"no engine objects: {directory}")
    return paths


def verify(arculator, rpc, policy, nm, sanitized=False):
    definitions = {}
    groups = []
    instrumentation = set(policy["compiler_services"])
    if sanitized:
        instrumentation |= set(policy["sanitizer_services"])
    for engine, directories in (("arculator", arculator), ("rpc", rpc)):
        if not directories:
            continue
        paths = [path for directory in directories for path in objects(directory)]
        current = []
        for path in paths:
            for name, undefined in read_symbols(path, nm, sanitized):
                current.append((path, name, undefined))
                if undefined:
                    continue
                require(name not in definitions, f"duplicate/colliding definition: {name}")
                definitions[name] = path
                if engine == "rpc":
                    require(name.startswith("arc_rpc_"), f"unprefixed RPCEmu definition: {path}: {name}")
                else:
                    require(name in policy["arculator_definitions"],
                            f"unexpected Arculator definition: {path}: {name}")
        groups.append((engine, paths, current))
    for engine, paths, current in groups:
        services = set(policy[engine + "_services"]) | instrumentation
        own = {name for _, name, undefined in current if not undefined}
        for path, name, undefined in current:
            if not undefined:
                continue
            require(name in own or name in services or
                    (engine == "rpc" and name.startswith("arc_rpc_")),
                    f"unexpected {engine} reference: {path}: {name}")
            if engine == "arculator":
                require(not name.startswith("arc_rpc_"),
                        f"Arculator references RPCEmu: {path}: {name}")
            if engine == "rpc":
                require(name not in {"exit", "abort", "_exit", "assert", "__assert_fail",
                                     "__assert_rtn", "_assert", "_wassert"},
                        f"process termination reference: {path}: {name}")
        print(f"backend symbols: {engine}: {len(paths)} objects, defined + undefined passed")
    return len(definitions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arculator", type=Path, action="append", default=[])
    parser.add_argument("--rpc", type=Path, action="append", default=[])
    parser.add_argument("--nm", default="nm")
    parser.add_argument("--sanitized", action="store_true",
                        help="recognise compiler-owned ASan registration bookkeeping")
    parser.add_argument("--policy", type=Path,
                        default=Path(__file__).resolve().parents[1] / "engine/backend-symbols.json")
    args = parser.parse_args()
    try:
        require(args.arculator or args.rpc, "no backend selected")
        policy = json.loads(args.policy.read_text())
        require(policy["schema"] == 1, "unsupported symbol policy")
        verify(args.arculator, args.rpc, policy, args.nm, args.sanitized)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(f"backend symbol guard: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
