#!/usr/bin/env python3
"""Population/receipt faults and fresh process termination. GPL-2.0-only.

Full keeps all 2,692 population and 144 append/checkpoint cuts, and adds an
exhaustive two-rotation matrix. Sanitizer uses the fixed points documented in
rpc_boot_faults_profile.json: each store-state/substep/file-kind/callback/
before-or-after class, plus both sides of the first and last callback in each
population phase. Formatting, writing and validation reuse the same store
helpers; their identical callback classes are covered once. Rotation reuses
append cuts from checkpoint; sealed recovery reuses cleanup cuts from chunk
recovery. Each shared class is already exercised in that earlier scenario.
Trace totals, exact selected labels, every observed class and population phase
endpoints are checked; drift fails instead of silently changing the subset.

Both profiles also probe each recovery cleanup class from two killed candidates
(chunk.tmp before rename, and a sealed candidate before root publication).
Recovery's repeated absent-chunk inventory stats are sampled in both profiles;
the original full matrices and their oracles remain exhaustive and unchanged.
"""
from pathlib import Path
import argparse
import json
import re
import shutil
import subprocess
import tempfile
# The authored fixture module is the only input; private inventories are absent.
from rpc_boot_packages_test import inputs, run, compare, require

parser=argparse.ArgumentParser()
parser.add_argument('binary')
parser.add_argument('--profile',choices=('full','sanitizer'),default='full')
args=parser.parse_args()
profiles=json.loads(Path(__file__).with_name('rpc_boot_faults_profile.json').read_text())


def cuts_for(name, result):
    rows=[(int(n),key) for n,key in re.findall(r'^boundary=(\d+) (.+)$',result.stdout,re.M)]
    count=int(re.findall(r'^points=(\d+)$',result.stdout,re.M)[-1])
    fixed=profiles[name]
    require(count==fixed['total'] and [n for n,_ in rows]==list(range(1,count+1)),
            f'{name}: injection trace/count changed; review the fixed profile')
    selected={int(n):key for n,key in fixed['points'].items()}
    observed=dict(rows)
    require(all(observed.get(n)==key for n,key in selected.items()),
            f'{name}: documented point changed class')
    covered=set(selected.values())
    for other in fixed['coverage_with']:
        covered.update(profiles[other]['points'].values())
    if name=='population':
        # These phases call the same store helpers; keep separate phase entry
        # and exit cuts as well as every distinct store callback class.
        identity=lambda key: '/'.join(key.split('/')[1:])
        covered={identity(key) for key in covered}
        require(covered=={identity(key) for key in observed.values()},
                f'{name}: sanitizer profile omits a boundary class')
        for phase in {key.split('/')[0] for key in observed.values()}:
            phase_cuts=[n for n,key in rows if key.split('/')[0]==phase]
            require(set(phase_cuts[:2]+phase_cuts[-2:])<=set(selected),
                    f'{name}: sanitizer profile omits phase endpoints for {phase}')
    else:
        require(set(observed.values())<=covered,
                f'{name}: sanitizer profile omits a boundary class')
    cuts=sorted(selected) if args.profile=='sanitizer' or name.startswith('recovery-') else range(1,count+1)
    print(f'rpc-boot profile={args.profile} scenario={name} cuts={len(cuts)}/{count} '
          f'classes={len(set(observed.values()))} points='+','.join(map(str,cuts)),flush=True)
    return cuts


def inject(system, save, mode, cut):
    result=subprocess.run([args.binary,str(system),str(save),'1',mode,str(cut),'0'],
                          capture_output=True,text=True,timeout=180)
    require(result.returncode in (0,2,3,-9,77),
            f'{mode} invalid result at {cut}: {result.stderr}')
    return result


with tempfile.TemporaryDirectory(prefix='arcdroid-boot-faults-') as temp:
    root=Path(temp);system=inputs(root/'fixture');baseline=root/'baseline'
    _,result=run(system,baseline,mode='trace')
    population_cuts=cuts_for('population',result)
    for mode in ('fault','kill'):
        for cut in population_cuts:
            save=root/'live';save.mkdir()
            inject(system,save,mode,cut)
            state,_=run(system,save,mode='verify')
            if state['published']:
                require(state['receipt']==1,'published root lacks receipt');compare(save)
            else:
                require(state['receipt']==0,'unpublished receipt')
            # Interruption restarts from scratch; a complete root becomes a no-op.
            run(system,save);compare(save)
            shutil.rmtree(save)
        print(f'rpc-boot-{mode}: {len(population_cuts)} population, readback, seal/receipt, publish and retirement points pass',flush=True)
    # Preserve the original append matrix; supplement it with two real complete
    # rotations, including replacement of retained roots/chunks and cleanup.
    for scenario in ('checkpoint','rotation'):
        save=root/scenario;shutil.copytree(baseline,save)
        _,result=run(system,save,mode=scenario+'-trace');compare(save)
        checkpoint_cuts=cuts_for(scenario,result)
        for mode in (scenario+'-fault',scenario+'-kill'):
            for cut in checkpoint_cuts:
                save=root/'live';shutil.copytree(baseline,save)
                inject(system,save,mode,cut)
                run(system,save,mode='verify');compare(save)
                run(root/'missing-system',save,mode='reopen');compare(save)
                shutil.rmtree(save)
            print(f'rpc-boot-{mode}: {len(checkpoint_cuts)} guest-write, rotation, publication, retirement and cleanup points pass',flush=True)
    for scenario,seed_cut in (('recovery-chunk',33),('recovery-seal',2667)):
        seed=root/scenario;seed.mkdir()
        result=inject(system,seed,'kill',seed_cut)
        require(result.returncode in (-9,77),f'{scenario}: seed did not terminate')
        traced=root/(scenario+'-trace');shutil.copytree(seed,traced)
        state,result=run(system,traced,mode='recovery-trace')
        require(state=={'published':0,'receipt':0},f'{scenario}: candidate published')
        recovery_cuts=cuts_for(scenario,result)
        for mode in ('recovery-fault','recovery-kill'):
            for cut in recovery_cuts:
                save=root/'live';shutil.copytree(seed,save)
                inject(system,save,mode,cut)
                state,_=run(system,save,mode='verify')
                require(state=={'published':0,'receipt':0},f'{scenario}: partial root')
                run(system,save);compare(save)
                shutil.rmtree(save)
            print(f'rpc-boot-{scenario}-{mode}: {len(recovery_cuts)} cleanup points pass',flush=True)
