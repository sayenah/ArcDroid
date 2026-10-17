#!/usr/bin/env python3
"""Within-job generation determinism; no committed golden hashes. GPL-2.0-only."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
with tempfile.TemporaryDirectory(prefix='arcdroid-m2a-determinism-') as temp:
    expected=None
    for seed in ('1','918273'):
        out=Path(temp)/seed
        subprocess.run([sys.executable,'-B',str(Path(__file__).with_name('rpc_lockstep_fuzz.py')),str(out)],
                       env=dict(os.environ,PYTHONHASHSEED=seed),check=True,timeout=90)
        hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.iterdir())}
        if expected is not None and hashes!=expected:raise RuntimeError('generation depends on PYTHONHASHSEED')
        expected=hashes
        # Retain only hashes before generating the next independent directory.
        for p in out.iterdir():p.unlink()
        out.rmdir()
    print('identical generation under PYTHONHASHSEED=1/918273; files=',len(expected))
