#!/usr/bin/env python3
"""PE test DLLs must export every dynamically required harness seam. GPL-2.0-only."""
from pathlib import Path
import re
import subprocess
import sys
sys.dont_write_bytecode=True
objdump,root,*libraries=sys.argv[1:]
root=Path(root)
required=set()
for name in ('rpc_timing_test.c','rpc_cmos_plan_test.c'):
    required.update(re.findall(r'LOAD\([^,]+,\s*"([^"]+)"\)',(root/'tests'/name).read_text()))
required.update({"arc_engine_test_floppy_mount","arc_engine_test_floppy_eject"})
for library in libraries:
    output=subprocess.check_output([objdump,'-p',library],text=True)
    table=output.split('[Ordinal/Name Pointer] Table',1)[1].split('\nThe ',1)[0]
    exported=set(re.findall(r'^\s*\[\s*\d+\].*?\s+(\w+)\s*$',table,re.M))
    assert required<=exported,(library,sorted(required-exported))
    print(f'{Path(library).name}: all {len(required)} timing/CMOS/floppy harness symbols exported')
