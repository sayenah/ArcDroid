#!/usr/bin/env python3
"""Population/receipt faults and fresh process termination. Authored GPL-2.0-only."""
from pathlib import Path
import json
import re
import shutil
import subprocess
import sys
import tempfile
# The authored fixture module is the only input; private inventories are absent.
from rpc_boot_packages_test import inputs, run, compare, require, IDENTITY

with tempfile.TemporaryDirectory(prefix='arcdroid-boot-faults-') as temp:
    root=Path(temp);system=inputs(root/'fixture');baseline=root/'baseline'
    _,result=run(system,baseline)
    count=int(re.findall(r'^points=(\d+)$',result.stdout,re.M)[-1])
    for mode in ('fault','kill'):
        for cut in range(1,count+1):
            save=root/'live';save.mkdir()
            result=subprocess.run([sys.argv[1],str(system),str(save),'1',mode,str(cut),'0'],capture_output=True,text=True,timeout=180)
            require(result.returncode in (0,2,3,-9,77),f'{mode} invalid result at {cut}: {result.stderr}')
            state,_=run(system,save,mode='verify')
            if state['published']:
                require(state['receipt']==1,'published root lacks receipt');compare(save)
            else:
                require(state['receipt']==0,'unpublished receipt')
            # Interruption restarts from scratch; a complete root becomes a no-op.
            run(system,save);compare(save)
            shutil.rmtree(save)
        print(f'rpc-boot-{mode}: {count} before/after population, seal, receipt, publish and retirement points pass',flush=True)
    # Version-2 receipt participates in exact parent-manifest bindings and is
    # preserved across guest-write checkpoint, both published slots and recovery.
    save=root/'checkpoint';shutil.copytree(baseline,save)
    _,result=run(system,save,mode='checkpoint')
    compare(save)
    checkpoint_points=int(re.findall(r'^points=(\d+)$',result.stdout,re.M)[-1])
    for mode in ('checkpoint-fault','checkpoint-kill'):
        for cut in range(1,checkpoint_points+1):
            save=root/'live';shutil.copytree(baseline,save)
            result=subprocess.run([sys.argv[1],str(system),str(save),'1',mode,str(cut),'0'],capture_output=True,text=True,timeout=180)
            require(result.returncode in (0,2,3,-9,77),f'{mode} invalid result at {cut}: {result.stderr}')
            run(system,save,mode='verify');compare(save)
            run(root/'missing-system',save,mode='reopen');compare(save)
            shutil.rmtree(save)
        print(f'rpc-boot-{mode}: {checkpoint_points} guest-write, rotation, publication, retirement and cleanup points pass',flush=True)
