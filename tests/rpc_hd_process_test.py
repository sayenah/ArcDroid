#!/usr/bin/env python3
"""Kill fresh store processes before/after every VFS boundary. GPL-2.0-only."""
from pathlib import Path
import argparse,json,subprocess,sys,tempfile,shutil,re
from concurrent.futures import ThreadPoolExecutor
from rpc_fault_profile import checked
parser=argparse.ArgumentParser()
parser.add_argument('binary',type=Path)
parser.add_argument('kinds',nargs='*')
parser.add_argument('--profile',choices=('full','sanitizer'),default='full')
args=parser.parse_args();binary=args.binary.resolve()
profiles=json.loads(Path(__file__).with_name('rpc_hd_process_profile.json').read_text())
def run(root,mode,kind,cut=0):
    return subprocess.run([str(binary),str(root),mode,kind,str(cut)],capture_output=True,text=True,timeout=60)
with tempfile.TemporaryDirectory(prefix='arc-rpc-hd-process-') as temp:
    temp=Path(temp);baseline=temp/'baseline';baseline.mkdir()
    r=run(baseline,'prepare','checkpoint');assert r.returncode==0,r.stdout+r.stderr
    for kind in (tuple(args.kinds) or ('checkpoint','population','creation','interleaved')):
        live=temp/'live'
        def fresh():
            if kind=='creation':live.mkdir()
            else:shutil.copytree(baseline,live)
        fresh()
        r=run(live,'trace',kind);assert r.returncode==0,r.stdout+r.stderr
        count=int(r.stdout.split('boundaries=')[-1].splitlines()[0]);shutil.rmtree(live)
        rows=[(int(n),key) for n,key in re.findall(r'^boundary=(\d+) (.+)$',r.stdout,re.M)]
        normalized,to_raw,prefix=checked(rows,profiles[kind]);observed=dict(normalized)
        fixed=profiles[kind];selected={int(n):key for n,key in fixed['points'].items()}
        assert len(rows)==count,(kind,'trace drift')
        assert all(observed.get(n)==key for n,key in selected.items()),(kind,'profile drift')
        assert set(selected.values())==set(observed.values()),(kind,'missing callback class')
        cuts=sorted(to_raw[n] for n in selected) if args.profile=='sanitizer' else list(range(1,count+1))
        if args.profile=='sanitizer':
            first={}
            for n,key in prefix:first.setdefault(key,n)
            cuts=sorted(cuts+list(first.values()))
        print(f'{kind}: profile={args.profile} checking {len(cuts)}/{count} termination boundaries',flush=True)
        def exercise(cut):
            live=temp/f'{kind}-{cut}'
            if kind=='creation':live.mkdir()
            else:shutil.copytree(baseline,live)
            r=run(live,'write',kind,cut)
            assert r.returncode in (-9,77), (kind,cut,r.returncode,r.stdout+r.stderr)
            masks=re.findall(r'^ack_mask=(\d+)$',r.stdout,re.M)
            r=run(live,'verify',kind,int(masks[-1]) if masks else 0);assert r.returncode==0,(kind,cut,r.stdout+r.stderr)
            shutil.rmtree(live)
        if args.profile=='sanitizer':
            with ThreadPoolExecutor(max_workers=2) as workers:list(workers.map(exercise,cuts))
        else:
            for cut in cuts:exercise(cut)
        print(f'{kind}: {len(cuts)} real termination boundaries recovered with readdir disabled',flush=True)
