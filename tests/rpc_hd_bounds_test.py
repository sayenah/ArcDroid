#!/usr/bin/env python3
"""Retained-data oracle in new executable processes after both streams. GPL-2.0-only."""
from pathlib import Path
import subprocess,sys,tempfile
binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='arc-rpc-hd-bounds-') as tmp:
    paths=[Path(tmp)/name for name in ('sequential.snapshot','scatter.snapshot')]
    subprocess.run([str(binary),'bounds',*[str(p) for p in paths]],check=True,timeout=3000)
    for path in paths:subprocess.run([str(binary),'reopen',str(path)],check=True,timeout=120)
