#!/usr/bin/env python3
"""Reject real malformed objects with the backend symbol guard. GPL-2.0-only."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

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

exports = (root / "libretro/arcdroid.exports").read_text().replace("_retro_", "retro_")
helper = ["sh", str(root / "tools/check-export-names.sh")]
subprocess.run(helper, input=exports, text=True, check=True)
for invalid in (exports.replace("retro_api_version", "retro_wrong_name"),
                exports + "arc_rpc_data\n", exports + "retro_run\n"):
    result = subprocess.run(helper, input=invalid, text=True, capture_output=True)
    assert result.returncode != 0, "non-exact exports were accepted"
print("rpc-symbol-guard: renamed, added data/weak and duplicate exports rejected")
