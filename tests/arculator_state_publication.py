#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""AC-5: pending effects, actual SIGKILL at native publication boundaries."""
import argparse
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile
import zlib
from arculator_state_matrix import run, store_bytes

p=argparse.ArgumentParser()
for name in ('harness','core','fixtures','output'):p.add_argument(name,type=Path)
a=p.parse_args()
for name in vars(a):setattr(a,name,getattr(a,name).resolve())
a.output.mkdir(parents=True,exist_ok=True)
cases=[('hd','A3020','hd-family/valid-f.adf',(1,2,3)),
       ('jfd-shadow','A3000','adffs/writable.jfd',(4,5)),
       ('adf-shadow','A3000','arcdroid-test.adf',(6,7))]
for media_class,machine,media,phases in cases:
    with tempfile.TemporaryDirectory(dir=a.output) as directory:
        root=Path(directory);env=dict(os.environ,ARCDROID_SS1_CLASS=media_class)
        cmd=[a.harness,a.core,a.fixtures/'arcdroid-test.rom',a.fixtures/media,root]
        run(cmd+['--ss1-produce',machine],env)
        def reset():
            shutil.rmtree(root/'save');shutil.copytree(root/'checkpoint-save',root/'save')
        def status():
            text=run(cmd+['--ss1-check-store',machine],env)
            found=re.search(r'SS1 store-status (\d+) (\d+) (\d+) (\d+)',text)
            assert found,text
            return tuple(map(int,found.groups()))
        reset();old=status()
        run(cmd+['--ss1-publish',machine],env);new=status()
        assert old!=new
        for phase in phases:
            reset()
            result=subprocess.run(list(map(str,cmd+['--ss1-publish',machine])),
                env=dict(env,ARCDROID_SS1_KILL_PHASE=str(phase)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=90)
            assert result.returncode==-signal.SIGKILL,(phase,result.returncode,result.stdout)
            recovered=status()
            if media_class=='hd' and recovered[1]==1:
                # Two guest commands form an ABA sequence. Killing after the
                # first AICM permits precisely that complete first command.
                bases=list((root/'save').rglob('disc.g0.hdf'));assert len(bases)==1
                image=bytearray(bases[0].read_bytes());image[15456*512]^=0x40
                assert recovered==(old[0],1,old[2]^0x40,zlib.crc32(image))
            else:assert recovered in (old,new),(media_class,phase,old,new,recovered)
            stable=store_bytes(root/'save');assert status()==recovered
            assert store_bytes(root/'save')==stable
            print(f'SIGKILL publication recovered: {media_class} phase={phase} frontier={recovered[:2]}',flush=True)
print('SS1 publication: 7 actual kills, valid cold reopen, whole-command data and idempotent recovery passed')
