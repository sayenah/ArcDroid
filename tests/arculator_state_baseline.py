#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""AC-6 ordinary retro_run against a base test core with the read-only CPU oracle.

The disposable base build adds only arc_engine_test_cpu_snapshot to arm.c and
its symbol allowlist. No state codec, device behavior or execution path changes.
"""
import argparse
import os
from pathlib import Path
import shutil
import tempfile
from arculator_state_matrix import run

p=argparse.ArgumentParser()
for name in ('harness','core','baseline','fixtures','output'):p.add_argument(name,type=Path)
a=p.parse_args()
for name in vars(a):setattr(a,name,getattr(a,name).resolve())
a.output.mkdir(parents=True,exist_ok=True)
cases={
 'adf':'arcdroid-test.adf', 'adf-shadow':'arcdroid-test.adf',
 'adf-original':'arcdroid-test.adf', 'adl':'arcdroid-application-l.adl',
 'adl-shadow':'arcdroid-application-l.adl',
 'jfd':'arcdroid-test.jfd', 'jfd-shadow':'adffs/writable.jfd',
 'jfd-gzip':'adffs/writable-gzip.jfd', 'zip':'zip-content/stored.zip',
 'folder':'adffs/Writable-folder.jfd/Writable-folder.jfd',
 'playlist':'arcdroid-media.m3u', 'adffs':'adffs/eligible.jfd',
 'hd-media':'hd-family/valid-f.adf', 'hd-media-shadow':'hd-family/valid-f.adf', 'hd':'hd-family/valid-f.adf',
 'hd-jfd':'adffs/eligible-hd-rw.jfd'}
count=0
for machine in ('A3000','A3010','A3020','A4000'):
    for media_class,media in cases.items():
        if (media_class.startswith('hd') and machine=='A3000') or (
                media_class in ('hd','hd-jfd') and machine!='A3020'):continue
        with tempfile.TemporaryDirectory(dir=a.output) as directory:
            root=Path(directory);traces=[]
            for core,label in ((a.baseline,'main'),(a.core,'ss1')):
                data=root/label;data.mkdir();content=a.fixtures/media
                if media_class=='adffs':
                    target=data/'system/arcdroid/adffs';target.mkdir(parents=True)
                    shutil.copyfile(a.fixtures/'adffs/synthetic.zip',target/'synthetic.zip')
                if media_class=='adf-original':
                    content=data/'authored.adf';shutil.copyfile(a.fixtures/media,content)
                command=[a.harness,core,a.fixtures/'arcdroid-test.rom',content,data,'--ss1-output',machine]
                env=dict(os.environ,ARCDROID_SS1_CLASS=media_class)
                run(command,env);trace=(data/'trace.bin').read_bytes()
                # Original changes its source under ordinary guest execution;
                # compare a cold reopen too, still without any state API call.
                if media_class=='adf-original':
                    run(command,env);trace+=(data/'trace.bin').read_bytes()
                traces.append(trace)
            assert traces[0]==traces[1],f'{machine}/{media_class}: no-save video/PCM/CPU output changed'
            count+=1;print(f'No-save baseline identical: {machine}/{media_class}',flush=True)
print(f'No-save baseline: {count} profile/media pairs plus Original cold reopens passed')
