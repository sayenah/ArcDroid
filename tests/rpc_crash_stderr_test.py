#!/usr/bin/env python3
"""Windows executable startup logs real faults and never handles them."""
# GPL-2.0-only
from pathlib import Path
import re
import subprocess
import sys

exe = Path(sys.argv[1]).resolve()
for mode in ("handled", "crash"):
    result = subprocess.run([str(exe), mode], capture_output=True, text=True, timeout=30)
    code = result.returncode & 0xffffffff
    assert code == (0 if mode == "handled" else 0xc0000005), (mode, code, result.stderr)
    assert "RPC first-chance exception code=0x00000000c0000005" in result.stderr, result.stderr
    assert re.search(re.escape(exe.name) + r"\+0x[0-9a-f]+ base=0x[0-9a-f]+", result.stderr), result.stderr
    assert "RPC stack frame=" in result.stderr, result.stderr
    if mode == "handled":
        assert "code=0x00000000c000001d" in result.stderr, result.stderr
        assert "address=0x00000000deadcafe" in result.stderr, result.stderr
    else:
        # Unwind reports loaded RVAs. addr2line needs the preferred PE image
        # base plus RVA, independently of where ASLR loaded this process.
        headers = subprocess.check_output([sys.argv[2], "-p", str(exe)], text=True)
        image_base = int(re.search(r"ImageBase\s+([0-9a-fA-F]+)", headers)[1], 16)
        rva = int(re.search(re.escape(exe.name) + r"\+0x([0-9a-f]+)", result.stderr)[1], 16)
        source = subprocess.check_output([sys.argv[3], "-f", "-e", str(exe), hex(image_base + rva)], text=True)
        assert "arc_test_crash_fault" in source and "rpc_crash_probe.c:" in source, source
        print("Fault RVA maps to matching executable source:", source.strip())
    print(f"Windows RPC stderr locator {mode}: exit=0x{code:08x}")
    print(result.stderr, end="")
