#!/usr/bin/env python3
"""Adversarial provenance/include checks on temporary copies. GPL-2.0-only."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
root = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("rpc_guard", root / "tools/check-rpcemu-import.py")
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


def rejected(change, expected):
    with tempfile.TemporaryDirectory(prefix="arcdroid-rpc-import-") as temp:
        candidate = Path(temp)
        shutil.copytree(root / "engine/rpcemu", candidate / "engine/rpcemu")
        shutil.copytree(root / "engine/rpc", candidate / "engine/rpc")
        change(candidate)
        try:
            guard.verify(candidate)
        except ValueError as error:
            assert expected in str(error), str(error)
            return
        raise AssertionError("invalid import was accepted")


def mutate(path, change):
    path.write_text(change(path.read_text()))


def manifest(candidate, change):
    path = candidate / "engine/rpcemu/manifest.json"
    value = json.loads(path.read_text())
    change(value)
    path.write_text(json.dumps(value))


guard.verify(root)
rejected(lambda c: (c / "engine/rpcemu/cmos.ram").write_bytes(b"invented"), "tree differs")
rejected(lambda c: (c / "engine/rpcemu/src/arm.h").unlink(), "tree differs")
rejected(lambda c: mutate(c / "engine/rpcemu/src/arm.h", lambda s: s + "\n"), "unrecorded")
rejected(lambda c: mutate(c / "engine/rpcemu/src/fdc.c",
                         lambda s: s.replace("ArcDroid modification (", "unattributed edit (")),
         "modification notice")
rejected(lambda c: mutate(c / "engine/rpcemu/src/fdc.c",
                         lambda s: s.replace("GNU General Public License", "missing original notice")),
         "GPL notice")
rejected(lambda c: mutate(c / "engine/rpcemu/src/fdc.c",
                         lambda s: s + '\n#include "disc.h"\n'), "unresolved include")
rejected(lambda c: mutate(c / "engine/rpcemu/src/fdc.c",
                         lambda s: s + '\n#include "../private.h"\n'), "escaping include")
rejected(lambda c: mutate(c / "engine/rpcemu/src/fdc.c",
                         lambda s: s + '\n#include UNKNOWN_HEADER\n'), "nonliteral include")
rejected(lambda c: mutate(c / "engine/rpc/rpc_floppy.h",
                         lambda s: s + '\n#include "unknown.h"\n'), "unresolved include")
rejected(lambda c: manifest(c, lambda m: m["files"].append(m["files"][0])), "allowlist")
rejected(lambda c: manifest(c, lambda m: m["files"][0]["patches"].append("unknown")), "lineage")
rejected(lambda c: manifest(c, lambda m: m.update(tarball_sha256="0" * 64)), "provenance")
def symlinked(c):
    header = c / "engine/rpc/rpc_floppy.h"
    target = c / "elsewhere.h"
    header.rename(target)
    header.symlink_to(target)

# Windows normally denies unprivileged symlink creation. All other import
# mutations run there; exercise this extra boundary when the host permits it.
if sys.platform != "win32":
    rejected(symlinked, "symlinked dependency")
print("rpc-import-guard: 12 malformed trees/records rejected; symlink boundary checked where supported")
