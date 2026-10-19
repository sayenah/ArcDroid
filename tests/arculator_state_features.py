#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""AC-4/AC-6 public libretro API, notices and native mutation checks."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile
from arculator_state_matrix import run
p=argparse.ArgumentParser()
for name in ('harness','core','fixtures','output'):p.add_argument(name,type=Path)
a=p.parse_args()
for name in vars(a):setattr(a,name,getattr(a,name).resolve())
a.output.mkdir(parents=True,exist_ok=True)
for mode,media_class,machines,media in [
    ('compat','adf',('A3000',),'arcdroid-test.adf'),
    ('write-path','adf-shadow',('A3000',),'arcdroid-test.adf'),
    ('write-path','adf-shadow',('A3020',),'hd-family/valid-f.adf'),
    ('replacement','adf-shadow',('A3020',),'arcdroid-test.adf'),
    ('secondary','adf-shadow',('A3000',),'arcdroid-test.adf'),
    ('secondary','jfd-shadow',('A3000',),'adffs/writable.jfd'),
    ('secondary','hd',('A3020',),'hd-family/valid-f.adf'),
    ('preempt','adf-shadow',('A3000',),'arcdroid-test.adf'),
    ('publish-failure','adf',('A3000',),'arcdroid-test.adf'),
    ('features','adf',('A3000','A3010','A3020','A4000'),'arcdroid-test.adf'),
    ('features','jfd-shadow',('A3000',),'adffs/writable.jfd'),
    ('boundary','adf',('A3000',),'arcdroid-test.adf'),
    ('refusals','adf',('A3000',),'arcdroid-test.adf'),
    ('store','adf',('A3000',),'arcdroid-test.adf'),
    ('store','adf-shadow',('A3000',),'arcdroid-test.adf'),
    ('store','adf-original',('A3000',),'arcdroid-test.adf'),
    ('store','jfd-swap',('A3000',),'adffs/Writable-folder.jfd/Writable-folder.jfd'),
    ('store','jfd-shadow',('A3000',),'adffs/writable.jfd'),
    ('store','hd',('A3020',),'hd-family/valid-f.adf')]:
    for machine in machines:
        with tempfile.TemporaryDirectory(dir=a.output) as directory:
            root=Path(directory);content=a.fixtures/media
            if media_class=='adf-original':
                content=root/'authored.adf';shutil.copyfile(a.fixtures/media,content)
            env=dict(os.environ,ARCDROID_SS1_CLASS=media_class)
            if mode=='secondary':
                # Copied core modules intentionally have identical exported C
                # globals. Keep memory/UB checks; disable only the C++ ODR-name
                # diagnostic for this actual two-module lifecycle probe.
                env['ASAN_OPTIONS']=env.get('ASAN_OPTIONS','')+':detect_odr_violation=0'
            print(run([a.harness,a.core,a.fixtures/'arcdroid-test.rom',content,root,
                       '--ss1-'+mode,machine],env),flush=True)
