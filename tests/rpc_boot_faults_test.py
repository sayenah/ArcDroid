#!/usr/bin/env python3
"""Population/receipt faults and fresh process termination. GPL-2.0-only.

Full keeps every population and append/checkpoint cut, the exhaustive
two-rotation matrix, and the original recovery cuts in both modes. Sanitizer
uses the fixed selection rule and ordinals in rpc_boot_faults_profile.json:
each store-state/substep/file-kind/callback/before-or-after class once across
scenarios, with callback failure before execution and termination after it.
Boot phases reuse store helpers; every phase still has selected cuts. Trace
totals, pinned labels and complete class coverage are checked so drift fails.
Both profiles probe cleanup from killed chunk and sealed candidates. Sanitizer
runs two isolated cases concurrently; full remains serial.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse
import json
import re
import shutil
import subprocess
import tempfile
# The authored fixture module is the only input; private inventories are absent.
from rpc_boot_packages_test import inputs, run, compare, require
from rpc_fault_profile import checked

parser=argparse.ArgumentParser()
parser.add_argument('binary')
parser.add_argument('--profile',choices=('full','sanitizer'),default='full')
args=parser.parse_args()
profiles=json.loads(Path(__file__).with_name('rpc_boot_faults_profile.json').read_text())


def cuts_for(name, result):
    rows=[(int(n),key) for n,key in re.findall(r'^boundary=(\d+) (.+)$',result.stdout,re.M)]
    count=int(re.findall(r'^points=(\d+)$',result.stdout,re.M)[-1])
    fixed=profiles[name]
    require(count==len(rows),f'{name}: raw trace/count changed')
    rows,to_raw,prefix=checked(rows,fixed)
    count=len(rows)
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
    if args.profile=='sanitizer':
        selected={int(n):key for n,key in fixed['sanitizer_points'].items()}
        require(all(observed.get(n)==key for n,key in selected.items()),
                f'{name}: sanitizer point changed class')
        identity=lambda key: '/'.join(key.split('/')[1:])
        covered={identity(key) for key in selected.values()}
        for earlier in ('population','checkpoint','rotation','recovery-chunk','recovery-seal'):
            if earlier==name:
                break
            covered.update(identity(key) for key in profiles[earlier]['sanitizer_points'].values())
        require({identity(key) for key in observed.values()}<=covered,
                f'{name}: sanitizer profile omits a boundary class')
        if name=='population':
            require({key.split('/')[0] for key in selected.values()}==
                    {key.split('/')[0] for key in observed.values()},
                    f'{name}: sanitizer profile omits a population phase')
        cuts=sorted(selected)
        modes={mode:[n for n in cuts if selected[n].endswith('/'+side)]
               for mode,side in (('fault','before'),('kill','after'))}
        require(sum(map(len,modes.values()))==len(cuts),
                f'{name}: invalid sanitizer boundary side')
    else:
        cuts=sorted(selected) if name.startswith('recovery-') else range(1,count+1)
        modes={'fault':cuts,'kill':cuts}
    modes={mode:[to_raw[n] for n in ordinals] for mode,ordinals in modes.items()}
    # Prefix stat counts depend on native path depth. Preserve injection of
    # both sides while pinning the remaining operation graph exactly.
    for mode,side in (('fault','before'),('kill','after')):
        candidates=[(n,key) for n,key in prefix if key.endswith('/'+side)]
        if args.profile=='sanitizer':
            seen=set();candidates=[(n,key) for n,key in candidates if not (key in seen or seen.add(key))]
        modes[mode].extend(n for n,_ in candidates)
        modes[mode].sort()
    print(f'rpc-boot profile={args.profile} scenario={name} cuts={len(cuts)}/{count} '
          f'classes={len(set(observed.values()))} points='+','.join(map(str,cuts)),flush=True)
    return modes


def inject(system, save, mode, cut):
    result=subprocess.run([args.binary,str(system),str(save),'1',mode,str(cut),'0'],
                          capture_output=True,text=True,timeout=180)
    require(result.returncode in (0,2,3,-9,77),
            f'{mode} invalid result at {cut}: {result.stderr}')
    return result


def exercise(cuts, action):
    if args.profile=='sanitizer':
        # Fixed work, bounded concurrency; each case owns a separate save tree.
        with ThreadPoolExecutor(max_workers=2) as workers:
            list(workers.map(action,cuts))
    else:
        for cut in cuts:
            action(cut)


def live_path(root, mode, cut):
    return root/(f'live-{mode}-{cut}' if args.profile=='sanitizer' else 'live')


with tempfile.TemporaryDirectory(prefix='arcdroid-boot-faults-') as temp:
    root=Path(temp);system=inputs(root/'fixture');baseline=root/'baseline'
    _,result=run(system,baseline,mode='trace')
    population_cuts=cuts_for('population',result)
    for mode in ('fault','kill'):
        def population_case(cut):
            save=live_path(root,mode,cut);save.mkdir()
            inject(system,save,mode,cut)
            state,_=run(system,save,mode='verify')
            if state['published']:
                require(state['receipt']==1,'published root lacks receipt');compare(save)
            else:
                require(state['receipt']==0,'unpublished receipt')
            # Interruption restarts from scratch; a complete root becomes a no-op.
            run(system,save);compare(save)
            shutil.rmtree(save)
        exercise(population_cuts[mode],population_case)
        print(f'rpc-boot-{mode}: {len(population_cuts[mode])} population, readback, seal/receipt, publish and retirement points pass',flush=True)
    # Preserve the original append matrix; supplement it with two real complete
    # rotations, including replacement of retained roots/chunks and cleanup.
    for scenario in ('checkpoint','rotation'):
        save=root/scenario;shutil.copytree(baseline,save)
        _,result=run(system,save,mode=scenario+'-trace');compare(save)
        checkpoint_cuts=cuts_for(scenario,result)
        for mode in (scenario+'-fault',scenario+'-kill'):
            def checkpoint_case(cut):
                save=live_path(root,mode,cut);shutil.copytree(baseline,save)
                inject(system,save,mode,cut)
                run(system,save,mode='verify');compare(save)
                run(root/'missing-system',save,mode='reopen');compare(save)
                shutil.rmtree(save)
            exercise(checkpoint_cuts[mode.rsplit('-',1)[-1]],checkpoint_case)
            print(f'rpc-boot-{mode}: {len(checkpoint_cuts[mode.rsplit('-',1)[-1]])} guest-write, rotation, publication, retirement and cleanup points pass',flush=True)
    for scenario in ('recovery-chunk','recovery-seal'):
        seed_cut=profiles[scenario]['seed_cut']
        seed=root/scenario;seed.mkdir()
        result=inject(system,seed,'kill',seed_cut)
        require(result.returncode in (-9,77),f'{scenario}: seed did not terminate')
        traced=root/(scenario+'-trace');shutil.copytree(seed,traced)
        state,result=run(system,traced,mode='recovery-trace')
        require(state=={'published':0,'receipt':0},f'{scenario}: candidate published')
        recovery_cuts=cuts_for(scenario,result)
        for mode in ('recovery-fault','recovery-kill'):
            def recovery_case(cut):
                save=live_path(root,mode,cut);shutil.copytree(seed,save)
                inject(system,save,mode,cut)
                state,_=run(system,save,mode='verify')
                require(state=={'published':0,'receipt':0},f'{scenario}: partial root')
                run(system,save);compare(save)
                shutil.rmtree(save)
            exercise(recovery_cuts[mode.rsplit('-',1)[-1]],recovery_case)
            print(f'rpc-boot-{scenario}-{mode}: {len(recovery_cuts[mode.rsplit('-',1)[-1]])} cleanup points pass',flush=True)
