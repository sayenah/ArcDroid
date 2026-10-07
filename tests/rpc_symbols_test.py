#!/usr/bin/env python3
"""Reject real malformed objects with the backend symbol guard. GPL-2.0-only."""
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

sys.dont_write_bytecode = True
root = Path(sys.argv[1])
compiler = sys.argv[2]
nm = sys.argv[3]
spec = importlib.util.spec_from_file_location("symbol_guard", root / "tools/check-backend-symbols.py")
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)
policy = json.loads((root / "engine/backend-symbols.json").read_text())


def check(source, expected=None, flags=()):
    with tempfile.TemporaryDirectory(prefix="arcdroid-symbol-test-") as temp:
        directory = Path(temp)
        c = directory / "injected.c"
        c.write_text(source)
        subprocess.check_call([compiler, "-fno-common", *flags, "-c", str(c),
                               "-o", str(directory / "injected.o")])
        try:
            guard.verify([], [directory], policy, nm)
        except ValueError as error:
            assert expected is not None and expected in str(error), str(error)
            return
        assert expected is None, f"invalid object was accepted: {expected}"


check("int arc_rpc_data = 7; int arc_rpc_fn(void) { return arc_rpc_data; }")
check("int collision_data = 1;", "unprefixed RPCEmu definition")
check("int collision_fn(void) { return 1; }", "unprefixed RPCEmu definition")
check("extern void foreign(void); void arc_rpc_fn(void) { foreign(); }",
      "unexpected rpc reference")
check("extern void exit(int); void arc_rpc_fn(void) { exit(1); }",
      "unexpected rpc reference")
check("extern void abort(void); void arc_rpc_fn(void) { abort(); }",
      "unexpected rpc reference")
check("int arc_rpc_common;", "common symbol", ("-fcommon",))
check("__attribute__((weak)) int arc_rpc_weak = 1;", "weak external symbol")
check("__attribute__((weak)) void arc_rpc_fn(void) {}", "weak external symbol")
with tempfile.TemporaryDirectory(prefix="arcdroid-symbol-collision-") as temp:
    directory = Path(temp)
    for name in ("first", "second"):
        source = directory / (name + ".c")
        source.write_text("int arc_rpc_collision = 1;")
        subprocess.check_call([compiler, "-c", str(source), "-o", str(directory / (name + ".o"))])
    try:
        guard.verify([], [directory], policy, nm)
    except ValueError as error:
        assert "duplicate/colliding" in str(error)
    else:
        raise AssertionError("colliding definitions accepted")
print("rpc-symbol-guard: data/function/undefined/termination/common/weak/collision injections rejected")


def check_nm(output, engine, expected=None, sanitized=False, form="elf"):
    """Exercise GCC ELF output even when the host compiler emits Mach-O/COFF."""
    magic = {"elf": b"\x7fELF", "macho": b"\xcf\xfa\xed\xfe", "coff": b"\x64\x86\0\0"}
    with tempfile.TemporaryDirectory(prefix="arcdroid-symbol-nm-") as temp:
        directory = Path(temp)
        (directory / "injected.o").write_bytes(magic[form])
        with patch.object(guard.subprocess, "check_output", return_value=output), \
                contextlib.redirect_stdout(io.StringIO()):
            try:
                if engine == "outside":
                    guard.verify_outside([directory], policy, nm, sanitized)
                else:
                    guard.verify([directory] if engine == "arculator" else [],
                                 [directory] if engine == "rpc" else [], policy, nm, sanitized)
            except ValueError as error:
                assert expected is not None and expected in str(error), str(error)
                return
            assert expected is None, f"invalid nm output was accepted: {output}"


for engine in ("arculator", "rpc", "outside"):
    check_nm("_GLOBAL_OFFSET_TABLE_ U\n", engine)
    check_nm("_GLOBAL_OFFSET_TABLE_ U\n", engine, sanitized=True)
    check_nm("__odr_asan.debugon B 0 1\n", engine, sanitized=True)
    # GCC runtime families include versioned and abort-on-error entry points
    # beyond the exact Clang service names in the existing policy.
    check_nm("__asan_report_load4_noabort U\n__asan_version_mismatch_check_v8 U\n"
             "__ubsan_handle_type_mismatch_v1_abort U\n", engine, sanitized=True)

for engine in ("arculator", "rpc"):
    definition = "unexpected Arculator definition" if engine == "arculator" else "unprefixed RPCEmu definition"
    reference = f"unexpected {engine} reference"
    for output in ("__odr_asan.debugon B 0 1\n", "__asan_report_load4_noabort U\n",
                   "__ubsan_handle_type_mismatch_v1_abort U\n"):
        check_nm(output, engine, definition if " B " in output else reference)
    # Format, exact family spelling, and toolchain role are all necessary.
    for form in ("macho", "coff"):
        decoration = "_" if form == "macho" else ""
        check_nm(decoration + "_GLOBAL_OFFSET_TABLE_ U\n", engine, reference, form=form)
        check_nm(decoration + "__odr_asan.debugon B 0 1\n", engine, definition,
                 sanitized=True, form=form)
    for name in ("_GLOBAL_OFFSET_TABLE_", "__asan_engine", "__ubsan_engine",
                 "__odr_asan_debugon", "__odr_asan."):
        check_nm(f"{name} B 0 1\n", engine, definition, sanitized=True)
    for name in ("_GLOBAL_OFFSET_TABLE_engine", "__asanengine", "__ubsanengine",
                 "__asan_", "__ubsan_", "__odr_asan.debugon"):
        check_nm(f"{name} U\n", engine, reference, sanitized=True)
    for name in ("__odr_asan.debugon", "__asan_engine", "__ubsan_engine"):
        check_nm(f"{name} C 0 1\n", engine, "common symbol", sanitized=True)
        check_nm(f"{name} V 0 1\n", engine, "weak external symbol", sanitized=True)
    check_nm("__asan_engine T 0 4\n", engine, definition, sanitized=True)
    check_nm("collision_data B 0 1\n__odr_asan.collision_data B 0 1\n", engine,
             definition, sanitized=True)
check_nm("arc_rpc_foreign U\n", "arculator", "unexpected arculator reference", sanitized=True)
check_nm("debugon U\n", "rpc", "unexpected rpc reference", sanitized=True)
check_nm("arc_rpc_foreign U\n", "outside", "outside engine references RPCEmu", sanitized=True)
print("rpc-symbol-guard: ELF linker/GCC sanitizer roles accepted; mode/format/look-alike/common/weak/cross-engine violations rejected")

exports = (root / "libretro/arcdroid.exports").read_text().replace("_retro_", "retro_")
helper = ["sh", str(root / "tools/check-export-names.sh")]
subprocess.run(helper, input=exports, text=True, check=True)
for invalid in (exports.replace("retro_api_version", "retro_wrong_name"),
                exports + "arc_rpc_data\n", exports + "retro_run\n"):
    result = subprocess.run(helper, input=invalid, text=True, capture_output=True)
    assert result.returncode != 0, "non-exact exports were accepted"
print("rpc-symbol-guard: renamed, added data/weak and duplicate exports rejected")
