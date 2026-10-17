#!/usr/bin/env python3
"""Plan-core expectations, repeatability, quotas/observers and reach. GPL-2.0-only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from rpc_lockstep_compare import load_schema, stdout_projection as projection
from rpc_lockstep_expect import FEATURES, supports, structural_zeros
from rpc_lockstep_rom import build, write_item

def require(ok, message):
    if not ok: raise RuntimeError(message)


def execute(runner, core, fixtures, plan, save, words, profile=None, extra=()):
    args = [runner, '--core', core, '--plan', str(plan), '--system', str(fixtures),
            '--save', str(save), '--frames', '2', '--ram-offset', '262144', '--ram-words', str(words), *extra]
    if profile: args.extend(('--profile', str(profile), '--profile-sample-stride', '0'))
    return subprocess.run(args, capture_output=True, text=True, timeout=60)


def ram(text):
    rows = [line[4:] for line in text.splitlines() if line.startswith('ram=')]
    require(bool(rows), 'missing RAM signature')
    return [int(x, 16) for x in rows[-1].split(',')]


def expectation(item, result):
    require((result.returncode != 0) == item['fatal'], (item['name'], 'unexpected exit', result.returncode, result.stderr[-600:]))
    if item['fatal']:
        # A-32: committed RAM awaits the emitter; diagnostic registers remain checked.
        require('fault=1' in result.stdout, (item['name'], 'missing terminal fault'))
        registers = next(line[5:].split(' events=')[0] for line in reversed(result.stdout.splitlines()) if line.startswith('regs='))
        values = [int(x,16) for x in registers.split(',')]
        for index, expected in item['terminal_registers'].items():
            require(values[int(index)]==expected,(item['name'],'fatal register',index,values[int(index)],expected))
        return
    actual = ram(result.stdout)
    require(actual == item['expected'], (item['name'], 'RAM expectation mismatch',
            [(i, hex(x), hex(y)) for i, (x, y) in enumerate(zip(actual, item['expected'])) if x != y][:8]))
    for feature, offset in item['markers'].items():
        require(actual[offset] > 0, (item['name'], feature, 'realised floor'))
    if item.get('terminal_cpsr') is not None:
        registers = next(line[5:].split(' events=')[0] for line in reversed(result.stdout.splitlines()) if line.startswith('regs='))
        require(int(registers.split(',')[16],16) & 255 == item['terminal_cpsr'], (item['name'],'masked reg[16] refresh'))
    if 'budget' in item:
        attempts = [int(x) for x in re.findall(r' cpu=(\d+)', result.stdout)]
        require(attempts and max(attempts) <= item['budget'], (item['name'], 'instruction budget'))


def suffix(quota, mode):
    text = f'observation_quota={quota}\n'
    if mode == 'unmatched': text += 'observation_profile=unmatched\n'
    if mode == 'trace': text += 'observation_trace=numeric.jsonl\n'
    if mode == 'active': text += 'observer_cells=255\nobserver_forms=0xffffffff\nobserver_launch=Run Unmatched\nobserver_boot=launch\n'
    return text


def main():
    p = argparse.ArgumentParser()
    p.add_argument('runner'); p.add_argument('core'); p.add_argument('fixtures', type=Path)
    p.add_argument('--model', type=int); p.add_argument('--profile', action='store_true')
    p.add_argument('--quick', action='store_true'); p.add_argument('--generator-mutants', action='store_true')
    p.add_argument('--only', default=''); args = p.parse_args()
    manifest = json.loads((args.fixtures/'manifest.json').read_text())
    schema, _ = load_schema()
    items = [x for x in manifest['items'] if (args.model is None or x['model'] == args.model) and args.only in x['name']]
    require(bool(items), 'empty corpus selection')
    coverage = {m:dict(features={}, exits={}, classes={}, reads=0, writes=0, mmio_reads=0, mmio_writes=0) for m in range(4)}
    findings = []
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2a-corpus-') as temp:
        save = Path(temp); plan = save/'test.arcplan'
        for item in items:
            source = (args.fixtures/(item['name']+'.arcplan')).read_text()
            # Paths in generated plans are relative to --system, independent of build location.
            plan.write_bytes(source.encode('utf-8'))
            profile = save/'profile.json' if args.profile else None
            result = execute(args.runner,args.core,args.fixtures,plan,save,len(item['expected']),profile)
            expectation(item,result)
            previous = execute(args.runner,args.core,args.fixtures,plan,save,len(item['expected']))
            require(previous.returncode == result.returncode and previous.stdout == result.stdout, (item['name'],'AC-7 repeated stdout disagreement'))
            expected = projection(result.stdout,'cross-quota',schema)
            for quota in ((200,) if args.quick else (1, 7, 200)):
                off_stdout = None
                for mode in (('off',) if args.quick else ('off','unmatched','trace','active')):
                    plan.write_bytes((source+suffix(quota,mode)).encode('utf-8'))
                    current = execute(args.runner,args.core,args.fixtures,plan,save,len(item['expected']))
                    expectation(item,current)
                    if item['name'].startswith('fuzz-') and item['model'] in (0,2) and quota==200 and mode=='unmatched':
                        require(re.search(r'integer_attempts=[1-9]',current.stderr),
                                (item['name'],'integral-base integer batching not reached'))
                    if projection(current.stdout,'cross-quota',schema) != expected:
                        finding = (item['name'],quota,mode,'AC-7 oracle projection disagreement')
                        findings.append(finding)
                        print('ORACLE FINDING',json.dumps(finding),flush=True)
                    if mode == 'off':
                        off_stdout = current.stdout
                    elif projection(current.stdout,'guest-output',schema) != projection(off_stdout,'guest-output',schema):
                        finding = (item['name'],quota,mode,'AC-7 same-quota guest-output disagreement')
                        findings.append(finding)
                        print('ORACLE FINDING',json.dumps(finding),flush=True)
                    (save/'numeric.jsonl').unlink(missing_ok=True)
            model = item['model']; count = coverage[model]
            for feature, offset in item['markers'].items(): count['features'][feature] = count['features'].get(feature,0)+ram(result.stdout)[offset]
            if args.profile:
                record = json.loads(profile.read_text())['records'][-1]
                if item.get('walk_mmio'):
                    require(record['mmio_reads'] == record['exits']['E7b'] and record['mmio_reads']>0,
                            (item['name'],'one physical MMIO descriptor read per prefetch abort'))
                for group in ('exits','classes'):
                    for key, value in record[group].items(): count[group][key] = count[group].get(key,0)+value
                for key in ('reads','writes','mmio_reads','mmio_writes'): count[key] += record[key]
            print('PASS', item['name'], 'sha256='+hashlib.sha256(result.stdout.encode()).hexdigest(), flush=True)
        for model in sorted({x['model'] for x in items}):
            count = coverage[model]
            if not args.only:
                for feature in FEATURES:
                    if supports(feature,model): require(count['features'].get(feature,0)>0,(model,feature,'feature floor'))
                if args.profile:
                    for exit in ('E3','E4','E6','E7a','E7b','E10','E11'): require(count['exits'].get(exit,0)>0,(model,exit,'exit floor'))
                    require(count['writes']>0 and count['mmio_writes']>0 and count['mmio_reads']>0,(model,'profile access floor'))
            count['structural_zeros'] = structural_zeros(model)
            count['exit_structural_zeros'] = {'E2':'no candidate guards', 'semantic-E9':'no translated-code dependency mechanism; profile E9 is a recent-store proxy'}
            print('COVERAGE',model,json.dumps(count,sort_keys=True),flush=True)
        # Oracle-only lifecycle controls retained by A-20; lockstep lifecycle is M2c.
        if not args.only:
            item = next(x for x in items if not x['fatal']); plan.write_bytes((args.fixtures/(item['name']+'.arcplan')).read_bytes())
            reset = execute(args.runner,args.core,args.fixtures,plan,save,len(item['expected']),extra=['--reset-at','0'])
            expectation(item,reset)
            failed = execute(args.runner,args.core,args.fixtures,plan,save,len(item['expected']),extra=['--fail-alloc','1'])
            require(failed.returncode != 0,'allocation-fault request accepted')
        if args.generator_mutants:
            model = 2
            for feature in FEATURES:
                if not supports(feature,model): continue
                words, expected, markers = build(model,1,omit=feature)
                name = 'omit-'+feature; write_item(save,name,model,words)
                (save/'authored.seed').write_bytes(bytes(256))
                mutant = dict(name=name,expected=expected,markers=markers,fatal=False)
                result = execute(args.runner,args.core,save,save/(name+'.arcplan'),save,len(expected))
                require(result.returncode == 0, (feature,'mutant must execute'))
                try: expectation(mutant,result)
                except RuntimeError: print('killed generator', feature, flush=True)
                else: raise RuntimeError('generator mutant survived: '+feature)
                (save/(name+'.rom')).unlink(); (save/(name+'.arcplan')).unlink()
    require(not findings, ('AC-7 oracle findings require Fable ruling', findings))


if __name__ == '__main__': main()
