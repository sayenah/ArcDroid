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


def read_symbols(path, nm, sanitized=False, errors=None, keep_sanitizer_references=False):
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
            if (kind == "U" and not keep_sanitizer_references and
                    re.fullmatch(r"__(?:asan|ubsan)_[A-Za-z0-9_]+", name)):
                continue
        # Compiler bookkeeping is not an engine variable. Apple ASan emits
        # this one shared registration flag as a common even with -fno-common.
        if form == "macho" and sanitized and name == "___asan_globals_registered" and kind == "C":
            continue
        # Clang's private ODR indicators let two copied libretro DSOs retain
        # ASan's check without conflating their intentionally separate globals.
        # Admit data bookkeeping only when this SAME object defines its
        # referent; that definition still goes through the full engine policy.
        indicator = re.fullmatch(r"__odr_asan_gen_([A-Za-z_][A-Za-z0-9_]*)", name)
        if sanitized and form in {"macho", "elf"} and indicator and kind in ("BS" if form=="macho" else "B"):
            raw = ("_" if form=="macho" else "") + indicator[1]
            violation(re.search(r"^"+re.escape(raw)+r" [DdBbSsRr]\b", output, re.M) is not None,
                      f"ODR indicator without local data referent: {path}: {name}", errors)
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


def verify(arculator, rpc, policy, nm, sanitized=False, errors=None, allocation_census=False, shipping=False):
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
                if engine=="rpc":
                    violation(not name.startswith("arc_sha256_") or path.name in ("rpc_lockstep.c.o","rpc_lockstep.c.obj"),
                              f"SHA-256 reference outside lockstep emitter: {path}: {name}",errors)
                    if shipping:
                        violation(not name.startswith(("arc_rpc_lockstep_","arc_rpc_test_lockstep_","arc_sha256_")),
                                  f"shipping lockstep symbol: {path}: {name}",errors)
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


# Census-only external services, reviewed independently of the backend policy:
# that policy deliberately permits raw allocators in ordinary cores. Resolve
# internal references against actual linked definitions, never a name prefix.
# New external APIs require review here, including allocating libc/Win32 APIs.
CENSUS_SERVICES = {
    # Non-owning libc/string/math services and the existing stdio diagnostics.
    "acos", "asin", "atan", "atexit", "bzero", "ceil", "cos", "exp", "floor",
    "fmod", "fprintf", "getenv", "hypotl", "isalnum", "isalpha", "isdigit",
    "isspace", "localtime", "log", "log10", "longjmp", "memchr", "memcmp",
    "memcpy", "memmove", "memset", "memset_pattern16", "printf", "puts", "qsort",
    "setjmp", "sin", "snprintf", "sprintf", "sqrt", "sscanf", "strcat", "strchr",
    "strcmp", "strcpy", "strlen", "strncmp", "strrchr", "strstr", "strtol",
    "strtoul", "strtoull", "tan", "time", "tolower", "vsnprintf",
    # glibc bits/mathcalls.h: scalar results only, no heap ownership.
    "fabs",  # Absolute value of a double; no allocation.
    "trunc",  # Round a double toward zero; no allocation.
    "truncf",  # Round a float toward zero; no allocation.
    "truncl",  # Round a long double toward zero; no allocation.
    # GCC libgcc soft-fp binary128 (TFmode), used by aarch64 Linux long double.
    "__addtf3",  # Add binary128 values; no allocation.
    "__subtf3",  # Subtract binary128 values; no allocation.
    "__multf3",  # Multiply binary128 values; no allocation.
    "__divtf3",  # Divide binary128 values; no allocation.
    "__negtf2",  # Negate a binary128 value; no allocation.
    "__eqtf2",  # Binary128 equality comparison; no allocation.
    "__netf2",  # Binary128 inequality comparison; no allocation.
    "__getf2",  # Binary128 greater-or-equal comparison; no allocation.
    "__gttf2",  # Binary128 greater-than comparison; no allocation.
    "__letf2",  # Binary128 less-or-equal comparison; no allocation.
    "__lttf2",  # Binary128 less-than comparison; no allocation.
    "__unordtf2",  # Binary128 unordered comparison; no allocation.
    "__fixtfsi",  # Binary128 to signed SI integer; no allocation.
    "__fixtfdi",  # Binary128 to signed DI integer; no allocation.
    "__fixtfti",  # Binary128 to signed TI integer; no allocation.
    "__fixunstfsi",  # Binary128 to unsigned SI integer; no allocation.
    "__fixunstfdi",  # Binary128 to unsigned DI integer; no allocation.
    "__fixunstfti",  # Binary128 to unsigned TI integer; no allocation.
    "__floatsitf",  # Signed SI integer to binary128; no allocation.
    "__floatditf",  # Signed DI integer to binary128; no allocation.
    "__floattitf",  # Signed TI integer to binary128; no allocation.
    "__floatunsitf",  # Unsigned SI integer to binary128; no allocation.
    "__floatunditf",  # Unsigned DI integer to binary128; no allocation.
    "__floatuntitf",  # Unsigned TI integer to binary128; no allocation.
    "__extendsftf2",  # Extend binary32 to binary128; no allocation.
    "__extenddftf2",  # Extend binary64 to binary128; no allocation.
    "__trunctfsf2",  # Narrow binary128 to binary32; no allocation.
    "__trunctfdf2",  # Narrow binary128 to binary64; no allocation.
    # Darwin/glibc runtime, locale and process/profile clocks.
    "_DefaultRuneLocale", "__assert_rtn", "__assert_fail", "__error",
    "__errno_location", "__maskrune", "__stderrp", "__tolower", "stderr",
    "__ctype_b_loc", "__ctype_tolower_loc", "__isoc99_sscanf", "__isoc23_sscanf",
    "__isoc23_strtol", "__isoc23_strtoul", "__isoc23_strtoull",
    "clock_gettime", "sysctlbyname",
    "getauxval",  # Linux/Android hash HWCAP query; no allocation.
    # Existing dormant native lock module: handles/fds, never heap ownership.
    "close", "flock", "fstat", "open", "stat", "fcntl",
    # MinGW runtime and existing test-only exception locator. The locator's
    # handler registration is paired with RemoveVectoredExceptionHandler.
    "_assert", "_wassert", "_errno", "__acrt_iob_func", "__iob_func",
    "_localtime64", "_time64", "GetLocalTime", "AddVectoredExceptionHandler",
    "RemoveVectoredExceptionHandler", "CloseHandle", "CreateFileW",
    "FlushFileBuffers", "GetCurrentProcess", "GetCurrentThreadId", "GetLastError",
    "GetModuleFileNameA", "GetStdHandle", "LockFileEx", "MultiByteToWideChar",
    "QueryPerformanceCounter", "QueryPerformanceFrequency", "ReadProcessMemory",
    "RtlLookupFunctionEntry", "RtlVirtualUnwind", "UnlockFileEx", "VirtualQuery",
    "WriteFile",
}
CENSUS_ALLOCATORS = {"malloc", "calloc", "realloc", "free"}


def verify_census(directories, nm, sanitized, errors, policy):
    """Allow only reviewed external services in every census-linked object."""
    services = CENSUS_SERVICES | set(policy["compiler_services"])
    if sanitized:
        # GCC libsanitizer asan_interface_internal.h / ubsan_handlers.h:
        # stack markers and failure diagnostics, never application allocators.
        runtime = set(policy["sanitizer_services"]) | {
            "__asan_poison_stack_memory",  # Mark existing stack shadow; no allocation.
            "__asan_unpoison_stack_memory",  # Clear existing stack shadow; no allocation.
            "__asan_alloca_poison",  # Mark existing VLA/alloca redzones; no allocation.
            "__asan_allocas_unpoison",  # Clear existing VLA/alloca shadow; no allocation.
            # Each UBSan diagnostic and its _abort variant returns no owned allocation.
            "__ubsan_handle_invalid_builtin",  # Argument diagnostic; no owned allocation.
            "__ubsan_handle_nonnull_return_v1",  # Result diagnostic; no owned allocation.
            "__ubsan_handle_vla_bound_not_positive",  # Bound diagnostic; no owned allocation.
        }
        # GCC's recovery/abort variants of the same finite runtime services.
        services |= runtime | {name + "_abort" for name in runtime
                               if name.startswith("__ubsan_handle_")}
        services |= {name + "_noabort" for name in runtime
                     if name.startswith("__asan_report_")}
        services |= {"__asan_stack_free_8", "__asan_stack_free_10"}
    # Preserve GCC runtime imports here: the ordinary backend reader accepts
    # runtime families, but this external-service allowlist must check each name.
    rows = [(path, read_symbols(path, nm, sanitized, errors, keep_sanitizer_references=True))
            for directory in directories for path in objects(directory)]
    own = {name for _, symbols in rows for name, undefined in symbols if not undefined}
    for path, symbols in rows:
        bridge = path.name.split('.', 1)[0] == "arc_alloc_census"
        for name, undefined in symbols:
            if not undefined:
                continue
            referent = name.removeprefix("__imp_")
            fortified = re.fullmatch(r"__([A-Za-z][A-Za-z0-9_]*)_chk", referent)
            fortify_ok = (object_format(path) in {"elf", "macho"} and
                          fortified is not None and fortified[1] in CENSUS_SERVICES)
            violation(name in own or referent in services or fortify_ok or
                      (bridge and referent in CENSUS_ALLOCATORS),
                      f"allocation census bypass: unreviewed external reference: {path}: {name}", errors)
    if not errors:
        print("allocation census: all linked objects import only reviewed services; raw allocators confined to bridge")


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
    parser.add_argument("--shipping", type=int, choices=(0,1),default=0)
    parser.add_argument("--policy", type=Path,
                        default=Path(__file__).resolve().parents[1] / "engine/backend-symbols.json")
    args = parser.parse_args()
    try:
        require(args.arculator or args.rpc or args.outside, "no objects selected")
        policy = json.loads(args.policy.read_text())
        require(policy["schema"] == 1, "unsupported symbol policy")
        errors = []
        verify(args.arculator, args.rpc, policy, args.nm, args.sanitized, errors, args.allocation_census,args.shipping)
        if args.outside:
            verify_outside(args.outside, policy, args.nm, args.sanitized, errors)
        if args.allocation_census:
            verify_census(args.arculator + args.rpc + args.outside, args.nm, args.sanitized, errors, policy)
        finish(errors)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(f"backend symbol guard: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
