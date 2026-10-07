#!/usr/bin/env python3
"""B-1 real-object negative checks; no test is weakened. GPL-2.0-only."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
root,compiler,nm=sys.argv[1:4]
root=Path(root)
spec=importlib.util.spec_from_file_location('guard',root/'tools/check-backend-symbols.py')
guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
policy=json.loads((root/'engine/backend-symbols.json').read_text())
assert policy['outside_test_hooks']==[], 'No direct outside-engine test references are needed'
with tempfile.TemporaryDirectory(prefix='arc-boundary-') as temp:
    temp=Path(temp);(temp/'libretro').mkdir()
    for name in ['arc_rpc_audio_peek','arc_rpc_audio_commit','arc_rpc_session_generation','arc_rpc_sound_fault_silence','arc_rpc_unknown','arc_rpc_test_unlisted']:
        source=temp/'libretro'/'injected.c'
        source.write_text(f'extern void {name}(void); void frontend(void) {{ {name}(); }}')
        subprocess.run([compiler,'-fno-common','-c',str(source),'-o',str(temp/'libretro'/'injected.o')],check=True)
        try:guard.verify_outside([temp],policy,nm)
        except ValueError as e:assert 'outside engine references RPCEmu' in str(e),str(e)
        else:raise AssertionError(f'private frontend reference accepted: {name}')
    source.write_text('extern void arc_engine_audio_commit(void); void frontend(void) {arc_engine_audio_commit();}')
    subprocess.run([compiler,'-fno-common','-c',str(source),'-o',str(temp/'libretro'/'injected.o')],check=True)
    guard.verify_outside([temp],policy,nm)
for source in (root/'libretro').rglob('*'):
    if source.suffix in ['.c','.h']:assert '#include "rpc/' not in source.read_text(),source
print('B-1: frontend private references and unlisted hooks rejected; generic boundary admitted')
