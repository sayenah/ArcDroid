#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Projection arithmetic vectors and compiled fatal/overflow sensitivity."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
compiler=sys.argv[1];source=ROOT/'tests/rpc_lockstep_arithmetic.c'
header=(ROOT/'engine/rpc/rpc_lockstep_arithmetic.h').read_text(encoding='utf-8')
recipes=[('fatal-on-overflow','fail("lockstep projection overflow")','fatal("lockstep projection overflow")'),
 ('add-one-tick','*out=a+b;','*out=a+b+1;'),
 ('tick-overflow-unchecked','floor>UINT64_MAX/d','false')]
with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-arithmetic-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
    root=Path(tmp);copy=root/source.name;copy.write_text(source.read_text(encoding='utf-8'), encoding='utf-8')
    for label,text in [('control',header),*[(label,header.replace(old,new)) for label,old,new in recipes]]:
        (root/'rpc_lockstep_arithmetic.h').write_text(text, encoding='utf-8')
        binary=root/label
        result=subprocess.run([compiler,'-std=c17','-O2',str(copy),'-o',str(binary)],capture_output=True,text=True,timeout=30, encoding='utf-8')
        if result.returncode:raise RuntimeError((label,'compile failure, not a kill',result.stderr))
        result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10, encoding='utf-8')
        if label=='control':
            if result.returncode:raise RuntimeError(('control',result.stderr))
            print(result.stdout.strip())
        elif result.returncode!=1 or 'projection vector' not in result.stderr:raise RuntimeError((label,'survived/infrastructure',result.returncode,result.stderr))
        else:print('killed real arithmetic',label)
