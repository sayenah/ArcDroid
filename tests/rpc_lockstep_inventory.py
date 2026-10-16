#!/usr/bin/env python3
"""Explicit static-root, pointee and live-local inventory check. GPL-2.0-only."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def require(ok, message):
    if not ok: raise RuntimeError(message)


def data_symbols(nm, obj, apple=False):
    # Mach-O nm's S includes text literals; use sections to select real data/bss.
    args = [nm, '-m' if apple else '-P', str(obj)]
    text = subprocess.check_output(args, text=True)
    result = []
    for line in text.splitlines():
        if apple:
            match = re.match(r'^[0-9a-f]+ \(__DATA[^,]*,[^)]+\).*? (\S+)$',line)
            if not match: continue
            name = match[1]
            if name.startswith('_'): name = name[1:]
        else:
            parts = line.split()
            if len(parts)<2 or parts[1] not in ('b','B','d','D','c','C','g','G','s','S'): continue
            name = parts[0]
        result.append(name)
    return result


def check_symbols(schema, source, symbols):
    explicit = schema['object_symbols'].get(source,{})
    roots = {r['symbol']:r for r in schema['inventory'] if r['category']=='static-root' and r['source']==source}
    for name in symbols:
        require(name in explicit or name in roots, (source,'unclassified data/bss symbol',name))
        entry = explicit.get(name,roots.get(name))
        require(entry.get('classification') in ('contents-compared','immutable-input identity','output-compared','excluded with ruling'), (source,name,'missing class'))


def check_sources(schema, overrides=None):
    overrides = overrides or {}
    for name, expected in schema['source_sha256'].items():
        actual = overrides.get(name,(ROOT/name).read_bytes())
        require(hashlib.sha256(actual).hexdigest()==expected,(name,'inventory source changed: re-ground fields/roots before accepting it'))


def main():
    p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('--nm',default='nm');p.add_argument('--apple',action='store_true')
    a=p.parse_args();s=json.loads((ROOT/'tests/rpc_lockstep_schema.json').read_text());check_sources(s)
    classes={'contents-compared','immutable-input identity','output-compared','excluded with ruling'}
    for entry in s['inventory']:
        require(entry['classification'] in classes and entry.get('citation') and entry.get('ruling'),'incomplete inventory entry')
    tested=0
    for source in s['source_sha256']:
        obj=a.build/'CMakeFiles/arcdroid_rpcplan_libretro_rpc.dir'/(source+('.obj' if sys.platform=='win32' else '.o'))
        if not obj.is_file() and source in s.get('optional_profile_sources',[]):continue
        require(obj.is_file(),('missing inventory object',source))
        symbols=data_symbols(a.nm,obj,a.apple);check_symbols(s,source,symbols);tested+=len(symbols)
    # Check both a visible new nm root and a static merged away by a compiler.
    source='engine/rpc/rpc_session.c'
    for mutation in ('symbol','merged-static'):
        try:
            if mutation=='symbol':check_symbols(s,source,['lockstep_unclassified_static'])
            else:check_sources(s,{source:(ROOT/source).read_bytes()+b'\nstatic volatile unsigned lockstep_unclassified_static;\n'})
        except RuntimeError:print('killed inventory',mutation)
        else:raise RuntimeError('unclassified-static mutant survived')
    for category in ('static-root','reachable-storage','live-control-local'):
        print(category,sum(r['category']==category for r in s['inventory']))
    pending=[r['symbol'] for r in s['inventory'] if r.get('pending_ruling')]
    print('nm mapped symbols:',tested,'; pending Fable representation rulings:',','.join(pending) or 'None')
    # This check establishes inventory completeness, not acceptance of a pending exclusion.


if __name__=='__main__':main()
