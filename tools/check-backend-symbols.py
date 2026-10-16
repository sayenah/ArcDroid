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


def violation(condition, message, errors):
    if not condition:
        errors.append(message)


def finish(errors):
    if errors:
        raise ValueError("\n".join(errors))


def read_symbols(path, nm, sanitized=False, errors=None):
    local = errors is None
    if local:
        errors = []
    form = object_format(path)
    output = subprocess.check_output([nm, "-g", "-P", str(path)], text=True)
    # Darwin's POSIX nm does not encode weak attributes in its type letter.
    # Native nm and llvm-nm both expose those attributes in Mach-O mode.
    if form == "macho":
        attributes = subprocess.check_output([nm, "-g", "-m", str(path)], text=True)
        for line in attributes.splitlines():
            if re.search(r"\bweak\b", line):
                errors.append(f"weak external symbol: {path}: {line.strip()}")
    symbols = []
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 2 or len(parts[1]) != 1:
            errors.append(f"unrecognised nm output: {path}: {line}")
            continue
        name, kind = parts[:2]
        if form == "macho" or (form == "coff" and path.read_bytes()[:2] == b"\x4c\x01"):
            violation(name.startswith("_"), f"missing ABI decoration: {path}: {name}", errors)
            name = name[1:]
        # Linker-owned ELF GOT anchor: PIC objects reference it, never define it.
        if form == "elf" and name == "_GLOBAL_OFFSET_TABLE_" and kind == "U":
            continue
        # GCC's ELF sanitizer families have distinct roles: __odr_asan.* is
        # zero-initialised global bookkeeping; __asan_* / __ubsan_* are runtime
        # references. Do not exempt engine definitions, commons or weaks just
        # because their names resemble runtime instrumentation.
        if form == "elf" and sanitized:
            if kind == "B" and re.fullmatch(r"__odr_asan\.[A-Za-z_][A-Za-z0-9_.$]*", name):
                continue
            if kind == "U" and re.fullmatch(r"__(?:asan|ubsan)_[A-Za-z0-9_]+", name):
                continue
        # Compiler bookkeeping is not an engine variable. Apple ASan emits
        # this one shared registration flag as a common even with -fno-common.
        if form == "macho" and sanitized and name == "___asan_globals_registered" and kind == "C":
            continue
        if kind in "Cc":
            errors.append(f"common symbol: {path}: {name}")
            continue
        if kind in "WwVv":
            errors.append(f"weak external symbol: {path}: {name}")
            continue
        if kind not in "TtDdBbSsRrAU":
            errors.append(f"unexpected symbol type {kind}: {path}: {name}")
            continue
        # MinGW's COMDAT import-pointer helpers describe the underlying
        # reference. Validate that referent with the same engine policy.
        if form == "coff" and name.startswith(".refptr.") and kind == "R":
            symbols.append((name[len(".refptr."):], True))
        else:
            symbols.append((name, kind == "U"))
    if local:
        finish(errors)
    return symbols


def objects(directory):
    paths = sorted(path for path in directory.rglob("*")
                   if path.suffix in (".o", ".obj") and path.is_file())
    require(paths, f"no engine objects: {directory}")
    return paths


def verify(arculator, rpc, policy, nm, sanitized=False, errors=None, allocation_census=False):
    local = errors is None
    if local:
        errors = []
    definitions = {}
    groups = []
    instrumentation = set(policy["compiler_services"])
    if allocation_census:
        instrumentation |= {"arc_alloc_census_" + name for name in
                            ("malloc", "calloc", "realloc", "free")}
    if sanitized:
        instrumentation |= set(policy["sanitizer_services"])
    for engine, directories in (("arculator", arculator), ("rpc", rpc)):
        if not directories:
            continue
        paths = [path for directory in directories for path in objects(directory)]
        current = []
        for path in paths:
            try:
                symbols = read_symbols(path, nm, sanitized, errors)
            except (ValueError, OSError, subprocess.CalledProcessError) as error:
                errors.append(str(error))
                continue
            form = object_format(path)
            for name, undefined in symbols:
                current.append((path, name, undefined, form))
                if undefined:
                    continue
                violation(name not in definitions, f"duplicate/colliding definition: {name}", errors)
                definitions[name] = path
                if engine == "rpc":
                    violation(name.startswith("arc_rpc_"), f"unprefixed RPCEmu definition: {path}: {name}", errors)
                else:
                    violation(name in policy["arculator_definitions"],
                              f"unexpected Arculator definition: {path}: {name}", errors)
        groups.append((engine, paths, current))
    for engine, paths, current in groups:
        services = set(policy[engine + "_services"]) | instrumentation
        own = {name for _, name, undefined, _ in current if not undefined}
        for path, name, undefined, form in current:
            if not undefined:
                continue
            # glibc/Darwin fortified libc wrappers have the same service role.
            # Only undefined references qualify; definitions remain private.
            fortified = re.fullmatch(r"__([A-Za-z][A-Za-z0-9_]*)_chk", name)
            libc = set(policy[engine + "_services"])
            fortify_ok = (form in {"elf", "macho"} and fortified is not None
                          and fortified[1] in libc and not fortified[1].startswith("arc_"))
            violation(name in own or name in services or fortify_ok or
                      (engine == "rpc" and name.startswith("arc_rpc_")),
                      f"unexpected {engine} reference: {path}: {name}", errors)
            if engine == "arculator":
                violation(not name.startswith("arc_rpc_"),
                          f"Arculator references RPCEmu: {path}: {name}", errors)
            if engine == "rpc":
                violation(name not in {"exit", "abort", "_exit", "assert", "__assert_fail",
                                       "__assert_rtn", "_assert", "_wassert"},
                          f"process termination reference: {path}: {name}", errors)
    if local:
        finish(errors)
    if not errors:
        for engine, paths, _ in groups:
            print(f"backend symbols: {engine}: {len(paths)} objects, defined + undefined passed")
    return len(definitions)


def verify_outside(directories, policy, nm, sanitized=False, errors=None):
    """The dispatcher/engine owns private references; frontend/core do not."""
    local = errors is None
    if local:
        errors = []
    allowed = set(policy["outside_test_hooks"])
    count = 0
    for directory in directories:
        for path in objects(directory):
            # CMake preserves the source directory beneath the object target.
            relative = path.relative_to(directory)
            shared = path.name.split('.', 1)[0] in {name for name in (
                'arc_error_box', 'arc_disc_request', 'arc_launch_outcome', 'arc_boot_watch')}
            if relative.parts[0] == "engine" and not shared:
                continue
            try:
                symbols = read_symbols(path, nm, sanitized, errors)
            except (ValueError, OSError, subprocess.CalledProcessError) as error:
                errors.append(str(error))
                continue
            for name, undefined in symbols:
                if shared and undefined:
                    violation(name not in {"exit", "abort", "_exit", "assert", "__assert_fail",
                                           "__assert_rtn", "_assert", "_wassert"},
                              f"process termination reference: {path}: {name}", errors)
                if undefined and name.startswith("arc_rpc_"):
                    violation(name in allowed,
                              f"outside engine references RPCEmu: {path}: {name}", errors)
            count += 1
    if local:
        finish(errors)
    if not errors:
        print(f"backend symbols: outside engine: {count} objects passed")


def verify_census(directories, nm, sanitized, errors):
    """Fail closed if any linked object bypasses the exact allocation census."""
    allocators = {"malloc", "calloc", "realloc", "free", "strdup", "strndup",
                  "aligned_alloc", "posix_memalign", "_aligned_malloc", "_aligned_free"}
    for directory in directories:
        for path in objects(directory):
            if path.name.split('.', 1)[0] == "arc_alloc_census":
                continue  # The single bridge to the real allocator.
            for name, undefined in read_symbols(path, nm, sanitized, errors):
                referent = name.removeprefix("__imp_")
                violation(not undefined or referent not in allocators,
                          f"allocation census bypass: {path}: {name}", errors)
    if not errors:
        print("allocation census: all linked objects use the instrumented allocator")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arculator", type=Path, action="append", default=[])
    parser.add_argument("--rpc", type=Path, action="append", default=[])
    parser.add_argument("--outside", type=Path, action="append", default=[])
    parser.add_argument("--nm", default="nm")
    parser.add_argument("--sanitized", action="store_true",
                        help="recognise compiler-owned ASan/UBSan instrumentation")
    parser.add_argument("--allocation-census", type=int, choices=(0, 1), default=0,
                        help="recognise forced allocator instrumentation in the dedicated test core")
    parser.add_argument("--policy", type=Path,
                        default=Path(__file__).resolve().parents[1] / "engine/backend-symbols.json")
    args = parser.parse_args()
    try:
        require(args.arculator or args.rpc or args.outside, "no objects selected")
        policy = json.loads(args.policy.read_text())
        require(policy["schema"] == 1, "unsupported symbol policy")
        errors = []
        verify(args.arculator, args.rpc, policy, args.nm, args.sanitized, errors, args.allocation_census)
        if args.outside:
            verify_outside(args.outside, policy, args.nm, args.sanitized, errors)
        if args.allocation_census:
            verify_census(args.arculator + args.rpc + args.outside, args.nm, args.sanitized, errors)
        finish(errors)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(f"backend symbol guard: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
