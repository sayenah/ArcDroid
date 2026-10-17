#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the actual code-section allow-list boundary, including second TU."""
import sys
from pathlib import Path
from unittest.mock import patch
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import rpc_measure_compare as c
objects={Path('engine/rpc/rpc_mem_timing.c.o'):Path('timing'),Path('engine/rpc/rpc_session.c.o'):Path('session')}
right={k:Path('new-'+str(v)) for k,v in objects.items()}
def check(changed,allowed):
    def code(p,*args):return {'.text':b'changed' if str(p).removeprefix('new-') in changed and str(p).startswith('new-') else b'original'}
    with patch.object(c,'code',code):
        c.compare_objects(objects,right,'unused','self-test',allow_changed=allowed)
check(set(),None)
check({'timing'},'rpc_mem_timing')
for changed,allowed in [({'timing'},None),({'timing','session'},'rpc_mem_timing'),({'session'},'rpc_mem_timing')]:
    try:check(changed,allowed)
    except RuntimeError:pass
    else:raise RuntimeError('changed object escaped explicit allow-list')
print('allow-list: exactly one named object permitted; second changed object refused')
