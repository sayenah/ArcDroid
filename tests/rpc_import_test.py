#!/usr/bin/env python3
"""Adversarial provenance/include checks on temporary copies. GPL-2.0-only."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
from unittest.mock import patch

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

# Simulate a Windows core.autocrlf text checkout, including pristine headers,
# upstream's CRLF readme and the owned sound adaptation notice. Byte-accurate
# tarball/pristine git audits remain a separate unchanged path in the guard.
with tempfile.TemporaryDirectory(prefix="arc-import-crlf-") as temp:
    candidate = Path(temp)
    shutil.copytree(root / "engine/rpcemu", candidate / "engine/rpcemu")
    shutil.copytree(root / "engine/rpc", candidate / "engine/rpc")
    for path in candidate.rglob("*"):
        if path.is_file():
            data = path.read_bytes().replace(b"\r\n", b"\n")
            path.write_bytes(data.replace(b"\n", b"\r\n"))
    guard.verify(candidate)
print("rpc-import-guard: complete CRLF checkout accepted; pristine bytes audit unchanged")

# GCC/MinGW prerequisites are make syntax, not shell/string escapes. Exercise
# the same compiler-closure validator on every host, with synthetic Windows
# roots so the native Windows spelling does not depend on the test machine.
depfile_cases = [
    ("Windows drive/backslashes", r"D:\a\ArcDroid\ArcDroid",
     "rpc: " + r"D:\a\ArcDroid\ArcDroid\engine\rpcemu\src\arm.c",
     "rpc: " + r"D:\a\ArcDroid\ArcDroid\engine\rpcemu\src\disc.h"),
    ("Windows escaped space", r"D:\a\Arc Droid",
     "rpc: " + r"D:\a\Arc\ Droid\engine\rpcemu\src\arm.c",
     "rpc: " + r"D:\a\Arc\ Droid\engine\rpcemu\src\disc.h"),
    ("Windows LF continuation", r"D:\a\ArcDroid",
     "rpc: " + r"D:\a\ArcDroid\engine\rpcemu\src\arm.c" + " \\\n "
     + r"D:\a\ArcDroid\engine\rpc\rpc_memory.h",
     "rpc: " + r"D:\a\ArcDroid\engine\rpcemu\src\arm.c" + " \\\n "
     + r"D:\a\ArcDroid\engine\rpcemu\src\disc.h"),
    ("Windows CRLF continuation", r"D:\a\ArcDroid",
     "rpc: " + r"D:\a\ArcDroid\engine\rpcemu\src\arm.c" + " \\\r\n\t"
     + r"D:\a\ArcDroid\engine\rpc\rpc_memory.h",
     "rpc: " + r"D:\a\ArcDroid\engine\rpcemu\src\arm.c" + " \\\r\n\t"
     + r"D:\a\ArcDroid\engine\rpcemu\src\disc.h"),
    ("Windows dollars", r"D:\a\Arc$Droid",
     "rpc: " + r"D:\a\Arc$$Droid\engine\rpcemu\src\arm.c",
     "rpc: " + r"D:\a\Arc$$Droid\engine\rpcemu\src\disc.h"),
    ("Windows separator before escaped space", r"D:\a\ ArcDroid",
     "rpc: " + r"D:\a\\\ ArcDroid\engine\rpcemu\src\arm.c",
     "rpc: " + r"D:\a\\\ ArcDroid\engine\rpcemu\src\disc.h"),
    ("Windows mixed separators/case/dot segments", r"D:\a\ArcDroid",
     "rpc: d:/A/ARCDROID/engine/rpcemu/src/../src/./arm.c D:/a/ArcDroid/engine/rpcemu/COPYING",
     "rpc: d:/A/ARCDROID/engine/rpcemu/src/../../arculator/src/arm.h"),
    ("Windows relative prerequisites", r"D:\a\ArcDroid",
     "rpc: " + r"engine\rpcemu\src\arm.c engine\rpc\rpc_memory.h",
     "rpc: " + r"engine\rpcemu\src\disc.h"),
    ("Windows sibling checkout", r"D:\a\ArcDroid",
     "rpc: D:/a/ArcDroid/engine/rpcemu/src/arm.c",
     "rpc: D:/a/ArcDroid-other/engine/rpcemu/src/arm.c"),
    ("Windows different drive", r"D:\a\ArcDroid",
     "rpc: D:/a/ArcDroid/engine/rpcemu/src/arm.c",
     "rpc: E:/a/ArcDroid/engine/rpcemu/src/arm.c"),
    ("Windows escaping root", r"D:\a\ArcDroid",
     "rpc: D:/a/ArcDroid/engine/rpcemu/src/arm.c",
     "rpc: D:/a/ArcDroid/../elsewhere/engine/rpcemu/src/arm.c"),
    ("Windows UNC", r"\\server\share\ArcDroid",
     "rpc: " + r"\\server\share\ArcDroid\engine\rpcemu\src\arm.c",
     "rpc: " + r"\\server\other\ArcDroid\engine\rpcemu\src\arm.c"),
    ("POSIX absolute/relative/continuation", "/tmp/ArcDroid",
     "rpc: /tmp/ArcDroid/engine/rpcemu/src/../src/arm.c \\\n engine/rpc/rpc_memory.h",
     "rpc: /tmp/ArcDroid/engine/rpcemu/src/arm.c \\\n engine/rpcemu/src/disc.h"),
    ("POSIX space/dollar/hash", "/tmp/Arc $Droid#",
     r"rpc: /tmp/Arc\ $$Droid\#/engine/rpcemu/src/arm.c",
     r"rpc: /tmp/Arc\ $$Droid\#/engine/rpcemu/src/disc.h"),
    ("POSIX literal backslash", r"/tmp/Arc\Droid",
     r"rpc: /tmp/Arc\Droid/engine/rpcemu/src/arm.c",
     r"rpc: /tmp/Arc\Droid/engine/rpcemu/src/disc.h"),
]
for label, checkout, accepted, denied in depfile_cases:
    guard.verify_compiler_dependencies(checkout, accepted + "\n")
    try:
        guard.verify_compiler_dependencies(checkout, denied + "\n")
    except ValueError as error:
        assert "compiler dependency outside allowlist" in str(error), (label, str(error))
    else:
        raise AssertionError(f"{label}: excluded compiler dependency accepted")

# An even backslash count ends the token; an odd count quotes whitespace.
assert list(guard.dependency_tokens(r"one\\ two\\\ three $$four \#five")) == [
    "one\\", "two\\ three", "$four", "#five"]
assert list(guard.dependency_tokens("one\\\ttwo")) == ["one\ttwo"]
print(f"rpc-import-guard: {len(depfile_cases)} depfile cases accepted and excluded paths rejected")

# Prove verify() uses the validator for all compiled sources, including an
# excluded dependency visible only in the compiler's output.
def compiler_output(command, **kwargs):
    source = Path(command[-1]).as_posix().replace("$", "$$").replace(" ", r"\ ")
    return "rpc: " + source + "\n"


with patch.object(guard.subprocess, "check_output", side_effect=compiler_output) as compiler:
    guard.verify(root, compiler="synthetic-gcc")
    assert compiler.call_count == len(guard.NAMES)
with patch.object(guard.subprocess, "check_output", return_value="rpc: engine/rpcemu/src/disc.h\n"):
    try:
        guard.verify(root, compiler="synthetic-gcc")
    except ValueError as error:
        assert "compiler dependency outside allowlist" in str(error), str(error)
    else:
        raise AssertionError("compiler-only excluded dependency accepted")
print("rpc-import-guard: compiler closure integration accepts all sources and rejects excluded header")
