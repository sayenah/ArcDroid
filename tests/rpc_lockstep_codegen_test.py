#!/usr/bin/env python3
"""Build-class rejection and stale C table controls. GPL-2.0-only."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
compiler,generated=sys.argv[1],Path(sys.argv[2])
with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-codegen-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
    root=Path(tmp);source=root/'guard.c';source.write_text('#include "rpc_namespace.h"\nint lockstep_compile_control;\n', encoding='utf-8')
    base=['-DARCDROID_TEST_FAULTS=1','-DARCDROID_RPC_LOCKSTEP=1']
    cases=[('positive',base,True),('measurement',base+['-DARCDROID_RPC_MEASUREMENT=1'],False),
        ('no-hot-hooks',base[1:],False),('profile',base+['-DARCDROID_RPC_PROFILE=1'],False),
        ('crosscheck',base+['-DARCDROID_RPC_CROSSCHECK=1'],False)]
    for label,flags,expected in cases:
        result=subprocess.run([compiler,'-std=c17','-fsyntax-only','-I',str(ROOT/'engine/rpc'),*flags,str(source)],capture_output=True,text=True, encoding='utf-8')
        if (result.returncode==0)!=expected or (not expected and 'ARCDROID_RPC_LOCKSTEP' not in result.stderr):
            raise RuntimeError((label,result.returncode,result.stderr))
    tables=root/'tables';shutil.copytree(generated,tables)
    command=[sys.executable,'-B',str(ROOT/'tools/rpc_lockstep_codegen.py'),str(tables),'--check']
    subprocess.run(command,check=True,capture_output=True)
    # The independently reviewed O2 condition must pin the executable field
    # rule too, rather than merely duplicating documentary policy text.
    for mode in ('cross-quota','strict','guest-output','trace-toggle'):
        schema=json.loads((ROOT/'tests/rpc_lockstep_schema.json').read_text(encoding='utf-8'))
        field=next(f for f in schema['fields'] if f['name']=='memory.active_kind')
        field['mode_conditions'][mode]=False
        path=root/'unreviewed-condition.json';path.write_text(json.dumps(schema), encoding='utf-8')
        rejected=subprocess.run([sys.executable,'-B',str(ROOT/'tools/rpc_lockstep_codegen.py'),
            str(root/'unreviewed-tables'),'--schema',str(path)],capture_output=True,text=True, encoding='utf-8')
        if rejected.returncode==0 or 'unreviewed cross-quota condition' not in rejected.stderr:
            raise RuntimeError('O2 field condition escaped the reviewed-policy pin: '+mode)
    schema=json.loads((ROOT/'tests/rpc_lockstep_schema.json').read_text(encoding='utf-8'));schema['fields'][0]['citation']+=' metadata change'
    path=root/'schema.json';path.write_text(json.dumps(schema), encoding='utf-8')
    stale=subprocess.run(command+['--schema',str(path)],capture_output=True,text=True, encoding='utf-8')
    if stale.returncode==0 or 'regenerate C tables' not in stale.stderr:raise RuntimeError('stale schema without regeneration accepted')
    header=tables/'rpc_lockstep_generated.h';header.write_text(header.read_text(encoding='utf-8')+'\n/* stale table control */\n', encoding='utf-8')
    drift=subprocess.run(command,capture_output=True,text=True, encoding='utf-8')
    if drift.returncode==0 or 'generated C table drift' not in drift.stderr:raise RuntimeError('generated header drift accepted')
print('build class: positive + four rejected combinations; stale schema and generated C header refused')
