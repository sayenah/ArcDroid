#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Replay all 25 frozen RPC golden clients with a matching build's binaries."""
import argparse
import json
from pathlib import Path
import subprocess
import time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('recipes',type=Path,help='candidate CTest recipes')
    p.add_argument('binaries',type=Path,help='matching runner/core/client build')
    a=p.parse_args();recipes=a.recipes.resolve();binaries=a.binaries.resolve()
    tests=json.loads(subprocess.check_output(['ctest','--test-dir',str(recipes),'--show-only=json-v1'],text=True))['tests']
    names={'rpc-cpu-trace','rpc-cmos-trace','rpc-plan-devices',
           'rpc-factory-capture-2','rpc-factory-capture-3'}
    for model in range(4):
        names.update(f'{prefix}-{model}' for prefix in ('rpc-video-trace','rpc-video-paths',
                     'rpc-d2-guest','rpc-plan-matrix','rpc-plan-av'))
    assert len(names)==25
    selected={t['name']:t for t in tests if t['name'] in names}
    assert selected.keys()==names,'missing golden recipe'
    for name in sorted(names):
        command=[]
        for word in selected[name]['command']:
            if word.startswith(str(recipes)+'/'):
                # Plans are generated authored inputs, not ABI clients. Keep
                # the candidate recipe's inputs; pair every executable/core.
                relative=Path(word).relative_to(recipes)
                if relative.parts[0] not in ('generated','tests'):
                    word=str(binaries/relative)
            command.append(word)
        before=time.monotonic()
        r=subprocess.run(command,cwd=recipes,capture_output=True,text=True,timeout=600)
        if r.returncode:raise RuntimeError((name,r.returncode,r.stdout[-2000:],r.stderr[-4000:]))
        print(f'{name}: frozen golden passed ({time.monotonic()-before:.2f}s)',flush=True)
    print('25/25 frozen goldens; no fixture regenerated; matching executable/core pairs')

if __name__=='__main__':main()
