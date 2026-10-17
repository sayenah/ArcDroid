#!/usr/bin/env python3
"""Classify live object symbols; export reviewable inventory. GPL-2.0-only."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
CLASSES = {'contents-compared','immutable-input identity','output-compared','excluded with ruling'}


def require(ok, message):
    if not ok: raise RuntimeError(message)


def ignored_symbol(name):
    # Object-format section markers and assembler local labels, never a C-name wildcard.
    return bool(re.fullmatch(r'\.(?:bss|data|rdata|sbss|sdata)(?:[.$].*)?',name) or
                re.fullmatch(r'\.(?:L|l)\S*',name))


def parse_symbols(text, apple=False):
    result=[]
    for line in text.splitlines():
        if apple:
            match=re.match(r'^[0-9a-fA-F]+ (?:\(__DATA[^,]*,[^)]+\)|\(common\)).*? (\S+)$',line)
            if not match: continue
            name=match[1]
            # Mach-O assembler labels lack the C ABI underscore. Do not hide
            # an actual C variable _ltmp3 when normalising that underscore.
            if re.fullmatch(r'[lL]tmp\d+',name):continue
            if name.startswith('_'): name=name[1:]
        else:
            parts=line.split()
            if len(parts)<2 or parts[1] not in ('b','B','d','D','c','C','g','G','s','S'): continue
            name=parts[0]
        if not ignored_symbol(name): result.append(name)
    return result


def data_symbols(nm, obj, apple=False):
    return parse_symbols(subprocess.check_output([nm,'-m' if apple else '-P',str(obj)],text=True, encoding='utf-8'),apple)


def symbol_entry(schema,source,name):
    roots={r['symbol']:r for r in schema['inventory'] if r['category']=='static-root' and r['source']==source}
    explicit=schema['object_symbols'].get(source,{})
    # Only declared locals lose a GCC ordinal. Identity includes their owning function.
    for local in schema['local_static_symbols'].get(source,[]):
        function,variable=local['function'],local['name']
        if name in (variable,function+'.'+variable) or re.fullmatch(re.escape(variable)+r'\.\d+',name):
            return function+'::'+variable,roots[variable]
    return name,explicit.get(name,roots.get(name))


def check_symbols(schema,source,symbols):
    rows=[];identities=set();classified={};indicators=[]
    for name in symbols:
        if ignored_symbol(name): continue
        # GCC and Clang ELF ASan ODR indicators inherit a classified global's
        # identity only within this object. They cannot approve absent storage.
        if name.startswith(('__odr_asan.','__odr_asan_gen_')):
            indicators.append(name)
            continue
        identity,entry=symbol_entry(schema,source,name)
        require(entry is not None,(source,'unclassified data/bss symbol',name))
        require(identity not in identities,(source,'ambiguous duplicate symbol identity',identity,name))
        identities.add(identity)
        require(entry.get('classification') in CLASSES,(source,name,'missing class'))
        rows.append(dict(source=source,symbol=name,identity=identity,
                         classification=entry['classification'],
                         roots=entry.get('roots',[entry.get('root')]),citation=entry['citation']))
        classified[name]=rows[-1]
    for name in indicators:
        prefix='__odr_asan.' if name.startswith('__odr_asan.') else '__odr_asan_gen_'
        target=name.removeprefix(prefix)
        require(target in classified,(source,'ASan indicator lacks classified same-object global',name,target))
        rows.append(dict(classified[target],symbol=name,alias_of=target))
    return rows


def self_test(schema):
    for apple,text,expected in (
        (False,'.bss b 0 0\n.data d 0 0\n.rdata$zzz d 0 0\n.L42 b 0 0\nshortened.0 b 0 8\n',['shortened.0']),
        (False,'shortened.31 b 0 8\nunknown.7 b 0 8\n',['shortened.31','unknown.7']),
        (True,'00000000 (__DATA,__bss) non-external _shortened.0\n00000000 (__DATA,__bss) non-external ltmp3\n00000000 (__DATA,__bss) external _ltmp3\n',['shortened.0','ltmp3'])):
        require(parse_symbols(text,apple)==expected,'format symbol parsing')
    source='engine/rpc/rpc_observers.c'
    for name in ('shortened','shortened.0','shortened.31','arc_rpc_observers_test_horizon.shortened'):
        row=check_symbols(schema,source,[name])[0]
        require(row['identity']=='arc_rpc_observers_test_horizon::shortened','function-local identity')
    for name in ('lockstep_unclassified_static','unknown.7','shortened_extra.0','ltmp3'):
        try: check_symbols(schema,source,[name])
        except RuntimeError: print('rejected inventory',name)
        else: raise RuntimeError(('unclassified-symbol control survived',name))
    try:check_symbols(schema,source,['shortened.0','shortened.1'])
    except RuntimeError:print('rejected ambiguous function-local suffix identities')
    else:raise RuntimeError('colliding local-static identities silently classified')
    # Sanitizer-shaped nm tables: ELF/COFF have no ABI underscore; Mach-O
    # removes exactly one. Marker-before-global order must also work.
    source='engine/rpc/rpc_session.c';global_name='arc_rpc_bus_ns'
    for label,apple,table in (
        ('GCC ELF',False,'__odr_asan.arc_rpc_bus_ns B 0 1\narc_rpc_bus_ns B 8 8\n'),
        ('Clang ELF',False,'__odr_asan_gen_arc_rpc_bus_ns B 0 1\narc_rpc_bus_ns B 8 8\n'),
        ('Apple ABI GCC',True,'00000000 (__DATA,__bss) external ___odr_asan.arc_rpc_bus_ns\n'
            '00000008 (__DATA,__bss) external _arc_rpc_bus_ns\n'),
        ('Apple ABI Clang',True,'00000000 (__DATA,__bss) external ___odr_asan_gen_arc_rpc_bus_ns\n'
            '00000008 (__DATA,__bss) external _arc_rpc_bus_ns\n')):
        symbols=parse_symbols(table,apple);rows=check_symbols(schema,source,symbols)
        require(len(rows)==2 and rows[1]['alias_of']==global_name and
                rows[1]['identity']==rows[0]['identity'] and
                rows[1]['classification']==rows[0]['classification'],'ASan alias identity/class')
        for bad in ([symbols[0]], # Known in schema, absent from this object.
                    [symbols[0],'lockstep_unclassified_static'],
                    ['__odr_asan.lockstep_unclassified_static','lockstep_unclassified_static'],
                    ['__odr_asan_gen_lockstep_unclassified_static','lockstep_unclassified_static'],
                    [symbols[0],global_name+'x'],
                    ['__odr_asan.__odr_asan.'+global_name,symbols[0],global_name]):
            try:check_symbols(schema,source,bad)
            except RuntimeError:pass
            else:raise RuntimeError(('ASan same-object control survived',label,bad))
        print('ASan same-object identity and rejection controls:',label)
    # Apple Clang also emits descriptors and an object-wide registry flag.
    # These are not ODR aliases; keep them unclassified pending a ruling.
    for name in ('___asan_globals_registered','__asan_binder_'+global_name,
                 '__asan_global_'+global_name):
        try:check_symbols(schema,source,[global_name,name])
        except RuntimeError:pass
        else:raise RuntimeError(('unreviewed sanitizer metadata accepted',name))
    print('unreviewed Apple/Clang ASan metadata remains rejected')


def object_control(schema,build,compiler,nm,apple):
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2a-inventory-',dir=build.parent) as temp:
        source=Path(temp)/'control.c';obj=Path(temp)/'control.o'
        source.write_text('/* SPDX-License-Identifier: GPL-2.0-only */\n'
            'volatile unsigned lockstep_unclassified_static;\n'
            'volatile unsigned ltmp3;\n'
            'unsigned arc_rpc_observers_test_horizon(unsigned x) {\n'
            'static volatile unsigned shortened; shortened += x; return shortened; }\n', encoding='utf-8')
        subprocess.run([compiler,'-O2','-c',str(source),'-o',str(obj)],check=True,capture_output=True)
        symbols=data_symbols(nm,obj,apple)
        unknown={'lockstep_unclassified_static','ltmp3'}
        require(unknown<=set(symbols),'compiled unknown-symbol probe missing')
        for name in sorted(unknown):
            try:check_symbols(schema,'engine/rpc/rpc_observers.c',[name])
            except RuntimeError as error:require(name in str(error),'wrong unknown-symbol rejection')
            else:raise RuntimeError('compiled unclassified-symbol control survived')
        known=[name for name in symbols if name not in unknown]
        require(known and all(row['identity']=='arc_rpc_observers_test_horizon::shortened'
            for row in check_symbols(schema,'engine/rpc/rpc_observers.c',known)), 'compiled local-static identity')
        print('compiled unknown data/bss symbol rejected; function-local identity verified')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('build',type=Path);p.add_argument('--nm',default='nm');p.add_argument('--apple',action='store_true')
    p.add_argument('--write-inventory',type=Path,help='export current classified object symbols; never auto-approve unknown symbols')
    p.add_argument('--probe-compiler',help='compile real unknown-symbol/local-static controls outside the clone')
    a=p.parse_args();schema=json.loads((ROOT/'tests/rpc_lockstep_schema.json').read_text(encoding='utf-8'))
    policy=json.loads((ROOT/'tests/rpc_lockstep_reviewed_policy.json').read_text(encoding='utf-8'))
    inventory={k:hashlib.sha256(json.dumps(schema[k],sort_keys=True,separators=(',',':')).encode('utf-8')).hexdigest()
        for k in ('inventory','object_symbols','local_static_symbols')}
    require(inventory==schema['reviewed_inventory']==policy['reviewed_inventory'],
            'inventory metadata changed without independent review')
    for entry in schema['inventory']:
        require(entry['classification'] in CLASSES and entry.get('citation') and entry.get('ruling'),'incomplete inventory entry')
    self_test(schema)
    if a.probe_compiler:object_control(schema,a.build,a.probe_compiler,a.nm,a.apple)
    base=a.build/('CMakeFiles/arcdroid_rpclockstep_libretro_rpc.dir' if
        (a.build/'CMakeFiles/arcdroid_rpclockstep_libretro_rpc.dir').is_dir() else
        'CMakeFiles/arcdroid_rpcplan_libretro_rpc.dir')
    objects=sorted([*base.rglob('*.c.o'),*base.rglob('*.c.obj')])
    require(objects,('no built inventory objects',base))
    rows=[];sources=set()
    for obj in objects:
        source=obj.relative_to(base).as_posix().removesuffix('.obj').removesuffix('.o')
        require(source.startswith(('engine/rpc/','engine/rpcemu/')),('unexpected object source',source))
        sources.add(source)
        for row in check_symbols(schema,source,data_symbols(a.nm,obj,a.apple)):
            rows.append(dict(object=obj.relative_to(a.build).as_posix(),**row))
    require({'engine/rpc/rpc_session.c','engine/rpcemu/src/arm.c','engine/rpcemu/src/mem.c'}<=sources,'incomplete plan-core object build')
    if a.write_inventory:
        a.write_inventory.write_bytes((json.dumps(dict(license='GPL-2.0-only',objects=len(objects),symbols=rows),indent=2,sort_keys=True)+'\n').encode('utf-8'))
    for category in ('static-root','reachable-storage','live-control-local'):
        print(category,sum(r['category']==category for r in schema['inventory']))
    print('built objects:',len(objects),'; nm mapped symbols:',len(rows),'; source hashes: none')


if __name__=='__main__': main()
