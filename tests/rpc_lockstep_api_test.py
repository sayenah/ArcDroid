#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Other test cores refuse the stream through the same exported API."""
from pathlib import Path
import subprocess
import sys
import tempfile
loader,runner,shipping,*cores=sys.argv[1:]
# The native loader links the configured sanitizer runtime. Loading instrumented
# DLLs directly into the uninstrumented Python interpreter is not valid.
subprocess.run([loader,'--shipping',shipping],check=True,timeout=20)
with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-api-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
    for core in cores:
        subprocess.run([loader,'--refusal',core],check=True,timeout=20)
        args=[runner,'--core',core,'--plan',str(Path(tmp)/'unused.arcplan'),'--system',tmp,'--save',tmp,'--lockstep-stream',str(Path(tmp)/'unused.stream')]
        result=subprocess.run(args,capture_output=True,text=True,timeout=20)
        if result.returncode!=2 or 'Core refuses lockstep stream' not in result.stderr:raise RuntimeError((core,result.returncode,result.stderr))
print('shipping: no lockstep API; other test cores: no emitter/injection, bit absent, exported stubs refuse, runner exits 2 with named refusal')
