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
p.add_argument('--only-mode',choices=('video','rewind-buffer'))
a=p.parse_args()
for name in ('harness','core','fixtures','output'):setattr(a,name,getattr(a,name).resolve())
a.output.mkdir(parents=True,exist_ok=True)
for mode,media_class,machines,media in [
    ('video','adf',('A3000','A3010','A3020','A4000'),'arcdroid-test.adf'),
    ('rewind-buffer','adf',('A3000','A3010','A3020','A4000'),'arcdroid-test.adf'),
    ('rewind-buffer','adf-shadow',('A3000','A3010','A3020','A4000'),'arcdroid-test.adf'),
    ('rewind-buffer','adl-shadow',('A3000','A3010','A3020','A4000'),'arcdroid-application-l.adl'),
    ('rewind-buffer','hd-media-shadow',('A3010','A3020','A4000'),'hd-family/valid-f.adf'),
    ('rewind-buffer','jfd-shadow',('A3000','A3010','A3020','A4000'),'adffs/writable.jfd'),
    ('created-cold','jfd-shadow',('A3000','A3010','A3020','A4000'),'jfd-writes/rw-create-d.jfd'),
    ('created-cold-refusal','jfd-shadow',('A3000','A3010','A3020','A4000'),'jfd-writes/rw-create-d.jfd'),
    ('large-derived','adf',('A3000','A3010','A3020','A4000'),'arcdroid-test.adf'),
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
    if a.only_mode and mode!=a.only_mode:continue
    if not a.only_mode and mode in ('video','rewind-buffer'):continue
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
            command=[a.harness,a.core,a.fixtures/'arcdroid-test.rom',content,root,'--ss1-'+mode,machine]
            print(run(command,env,timeout=600 if mode=='video' else 90),flush=True)
            if mode=='write-path':
                print('New process reusing the same save folder:',flush=True)
                print(run(command,env,timeout=90),flush=True)
