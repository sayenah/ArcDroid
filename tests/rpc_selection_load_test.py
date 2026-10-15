#!/usr/bin/env python3
"""Author ZIP wrappers, then exercise ordinary C load preparation. GPL-2.0-only."""
import pathlib
import shutil
import subprocess
import sys
import zipfile
from generate_fixtures import build_extended_jfd
import struct

exe, core, rom, target = sys.argv[1:5]
out = pathlib.Path(target)
if out.exists():
    shutil.rmtree(out)
out.mkdir(parents=True)
def disc(cpu, os, number, count):
    b = build_extended_jfd(bytes(256), b"Authored selection disc")
    struct.pack_into("<I", b, 12, (count << 16) | number)
    struct.pack_into("<I", b, 304, (cpu << 8) | (os << 16))
    return b
for name, entries in [("set.zip", [("1.jfd", disc(20,12,1,2)), ("2.jfd",disc(20,12,2,2))]),
                      ("conflict.zip", [("1.jfd",disc(4,4,1,2)), ("2.jfd",disc(16,8,2,2))])]:
    with zipfile.ZipFile(out / name, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for member, data in entries:
            z.writestr(member,data)
subprocess.run([exe,core,rom,str(out),*sys.argv[5:]],check=True)
