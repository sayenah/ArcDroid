#!/usr/bin/env python3
"""Kill fresh store processes before/after every VFS boundary. GPL-2.0-only."""
from pathlib import Path
import subprocess,sys,tempfile,shutil,re
binary=Path(sys.argv[1]).resolve()
def run(root,mode,kind,cut=0):
    return subprocess.run([str(binary),str(root),mode,kind,str(cut)],capture_output=True,text=True,timeout=60)
with tempfile.TemporaryDirectory(prefix='arc-rpc-hd-process-') as temp:
    temp=Path(temp);baseline=temp/'baseline';baseline.mkdir()
    r=run(baseline,'prepare','checkpoint');assert r.returncode==0,r.stdout+r.stderr
    for kind in (tuple(sys.argv[2:]) or ('checkpoint','population','creation','interleaved')):
        live=temp/'live'
        def fresh():
            if kind=='creation':live.mkdir()
            else:shutil.copytree(baseline,live)
        fresh()
        r=run(live,'write',kind);assert r.returncode==0,r.stdout+r.stderr
        count=int(r.stdout.split('boundaries=')[-1].splitlines()[0]);shutil.rmtree(live)
        print(f'{kind}: checking {count} termination boundaries',flush=True)
        for cut in range(1,count+1):
            fresh()
            r=run(live,'write',kind,cut)
            assert r.returncode in (-9,77), (kind,cut,r.returncode,r.stdout+r.stderr)
            masks=re.findall(r'^ack_mask=(\d+)$',r.stdout,re.M)
            r=run(live,'verify',kind,int(masks[-1]) if masks else 0);assert r.returncode==0,(kind,cut,r.stdout+r.stderr)
            shutil.rmtree(live)
            if cut%100==0:print(f'{kind}: recovered {cut}/{count}',flush=True)
        print(f'{kind}: {count} real termination boundaries recovered with readdir disabled',flush=True)
