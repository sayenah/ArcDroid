#!/usr/bin/env python3
"""Fresh, licensed CI stage; only a successful classification is publishable.
SPDX-License-Identifier: GPL-2.0-only
"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--input',required=True);p.add_argument('--output',required=True);p.add_argument('--manifest',required=True);p.add_argument('--diagnostics',action='store_true');a=p.parse_args()
source=Path(__file__).resolve().parent.parent
incoming=Path(a.input);output=Path(a.output)
if not output.resolve().is_relative_to((source/'build').resolve()) or not output.name.startswith('public-') or output.resolve()==incoming.resolve():
    raise ValueError('stage output must be a dedicated build/public- directory')
# These output directories contain only this command's staged copies.
if output.exists():shutil.rmtree(output)
output.parent.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='rpc-stage-',dir=output.parent) as temporary:
    stage=Path(temporary)/'public';stage.mkdir()
    if a.diagnostics:
        for pattern in ('rpc*.exe','hash_accel_test.exe','arcdroid*libretro*.dll','rpc-repeat.log','Testing/Temporary/LastTest*.log'):
            for candidate in sorted(incoming.glob(pattern)):
                if candidate.name.startswith('arcdroid_lockprobe'):continue
                shutil.copy2(candidate,stage/candidate.name)
    else:
        shutil.copytree(incoming,stage,dirs_exist_ok=True)
    for name in ('COPYING','THIRD_PARTY_NOTICES.md'):shutil.copy2(source/name,stage/name)
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()
    (stage/'SOURCE.json').write_text(json.dumps({'revision':revision,'url':'https://github.com/sayenah/ArcDroid/tree/'+revision,'archive':'https://github.com/sayenah/ArcDroid/archive/'+revision+'.zip'})+'\n')
    spec=importlib.util.spec_from_file_location('classifier',source/'tools/check-rpc-artifacts.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);module.classify(stage,a.manifest)
    shutil.move(str(stage),output)
