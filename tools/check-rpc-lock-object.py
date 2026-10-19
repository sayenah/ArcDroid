#!/usr/bin/env python3
"""Real compiler/preprocessor and object proof for sentinel calls. GPL-2.0-only."""
import argparse
from collections import Counter
import importlib.util
import json
from pathlib import Path
import re
import shlex
import subprocess

def expanded_calls(body):
    spec=importlib.util.spec_from_file_location('sentinel_source_guard',Path(__file__).with_name('check-shipping-lock-io.py'))
    guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
    code=guard.code_only(body);calls=Counter()
    callbacks={expression.split('(',1)[0] for expression in guard.ADAPTER_CALLS}
    services=guard.PURE_CALLS|guard.HELPERS|callbacks|{
        '__error','__errno_location','__errno','_errno',
        '__builtin_object_size','__builtin___snprintf_chk','__builtin___memset_chk','__builtin___memcpy_chk',
        'open','open64','close','fstat','fstat64','stat','stat64','fcntl','fcntl64','flock',
        'MultiByteToWideChar','GetLastError','CreateFileW','LockFileEx','UnlockFileEx','CloseHandle'}
    for match in guard.CALL.finditer(code):
        name=re.sub(r'\s+','',match[0][:-1])
        if name in {'if','for','while','switch','sizeof','return'}:continue
        at=match.end();depth=1
        while at<len(code) and depth:
            depth+=(code[at]=='(')-(code[at]==')');at+=1
        if code[at:].lstrip().startswith('{'):continue
        if name not in services:raise ValueError('unreviewed real preprocessor call: '+name)
        calls[name]+=1
    return calls

def check(build,platform,nm):
    commands=json.loads((Path(build)/'compile_commands.json').read_text())
    seen=0
    for entry in commands:
        if not entry['file'].replace('\\','/').endswith('/libretro/arc_lock.c'):continue
        args=entry.get('arguments') or shlex.split(entry['command'])
        output=entry.get('output') or args[args.index('-o')+1]
        obj=Path(entry['directory'])/output
        if not obj.exists():continue
        pp=[];skip=False
        for arg in args:
            if skip:skip=False;continue
            if arg=='-o':skip=True;continue
            if arg=='-c':continue
            pp.append(arg)
        expanded=subprocess.check_output([*pp,'-E'],cwd=entry['directory'],text=True)
        origin='';body=[]
        for line in expanded.splitlines():
            marker=re.match(r'#\s+\d+\s+"([^"]+)"',line)
            if marker:origin=re.sub('/+','/',marker[1].replace('\\','/'));continue
            if origin.endswith('/libretro/arc_lock.c'):body.append(line)
        source=re.sub(r'\s+','', '\n'.join(body))
        calls=expanded_calls('\n'.join(body))
        symbols=subprocess.check_output([nm,'-u',str(obj)],text=True)
        imports=set()
        for line in symbols.splitlines():
            name=line.split()[-1] if line.split() else ''
            if name.startswith('__imp_'):name=name[6:]
            if name.startswith('_') and platform=='apple':name=name[1:]
            imports.add(name)
        common={'arc_io_metadata','arc_io_mkdir','memset','bzero','strlen','strrchr','strstr','snprintf',
                '__memset_chk','memcpy','memcmp','strcmp','strncmp','_GLOBAL_OFFSET_TABLE_',
                '__asan_register_image_globals','__asan_unregister_image_globals','__asan_shadow_memory_dynamic_address',
                '__stack_chk_fail','__stack_chk_guard','__error','__errno','__errno_location','__strrchr_chk','__vsnprintf_chk','__snprintf_chk',
                '__mingw_snprintf','___chkstk_ms','__asan_init','__asan_version_mismatch_check_v8',
                '__asan_register_globals','__asan_unregister_globals','__asan_register_elf_globals','__asan_unregister_elf_globals',
                '__asan_option_detect_stack_use_after_return'}
        native={'open','open64','close','stat','stat64','fstat','fstat64','fcntl','fcntl64'} if platform=='ofd' else {'open','close','stat','fstat','flock'} if platform=='apple' else {'CreateFileW','CloseHandle','GetLastError','LockFileEx','UnlockFileEx','MultiByteToWideChar'}
        actual=Counter()
        for name in native:
            actual[name[:-2] if name.endswith('64') else name]+=calls[name]
        expected=Counter({'open':1,'close':1,'stat':1,'fstat':1,'fcntl':2} if platform=='ofd' else
                         {'open':1,'close':1,'stat':1,'fstat':1,'flock':2} if platform=='apple' else
                         {'CreateFileW':1,'CloseHandle':1,'GetLastError':3,'LockFileEx':1,'UnlockFileEx':1,'MultiByteToWideChar':1})
        if +actual!=expected:raise ValueError('real preprocessor native call census differs')
        # Instrumentation imports never confer file access.
        unexpected={name for name in imports-common-native if not name.startswith(('__asan_report_','__asan_stack_malloc_','__asan_set_shadow_','__asan_version_mismatch_check_apple_clang_','__ubsan_handle_','__asan_mem'))}
        if unexpected:raise ValueError('unreviewed compiled sentinel imports: '+', '.join(sorted(unexpected)))
        if platform=='ofd':
            for required in ('fcntl','F_WRLCK','F_UNLCK'):
                if required=='fcntl' and not re.search(r'\b_?fcntl(?:64)?\b',symbols):raise ValueError('OFD object lacks fcntl import')
            # Pin the expanded command from the real system header, not a shim.
            # Probe real platform headers independently of source/header redefinitions.
            probe=[args[0]]
            take=False
            for arg in args[1:]:
                if take:probe.append(arg);take=False;continue
                if arg in ('--sysroot','-isysroot','-target','--target'):probe.append(arg);take=True
                elif arg.startswith(('--sysroot=','--target=','-march=','-mabi=','-mmacosx-version-min=')):probe.append(arg)
            macros=subprocess.check_output([*probe,'-E','-dM','-x','c','-'],input='#define _GNU_SOURCE 1\n#include <fcntl.h>\n',text=True)
            source_macros=subprocess.check_output([*pp,'-E','-dM'],cwd=entry['directory'],text=True)
            constant=re.search(r'^#define F_OFD_SETLK\s+(\d+)\s*$',macros,re.M)
            if not constant:raise ValueError('real headers do not define numeric F_OFD_SETLK')
            command=constant[1]
            def numeric(name):
                m=re.search(r'^#define '+name+r'\s+(\d+)\s*$',macros,re.M)
                if not m:raise ValueError('missing real lock constant '+name)
                return m[1]
            if 'lock.l_type='+numeric('F_WRLCK')+';' not in source or 'lock.l_type='+numeric('F_UNLCK')+';' not in source:raise ValueError('wrong compiled lock request type')
            if len(re.findall(r'fcntl(?:64)?\(\(int\)handle,'+command+r',&lock\)',source))!=2:raise ValueError('expanded OFD call count/command differs')
            if '__APPLE__' in source_macros or 'flock(' in source:raise ValueError('OFD platform redirected to Apple')
        elif platform=='apple':
            if source.count('flock((int)handle,')!=2 or not re.search(r'\b_flock\b',symbols) or re.search(r'\b_fcntl\b',symbols):raise ValueError('Apple compiled lock backend differs')
        else:
            if source.count('LockFileEx(')!=1 or source.count('UnlockFileEx(')!=1 or 'LockFileEx' not in symbols:raise ValueError('Windows compiled lock backend differs')
        forbidden=r'\b(?:read|write|pread|pwrite|ftruncate|ReadFile|WriteFile|mmap)\s*\('
        if re.search(forbidden,'\n'.join(body)):raise ValueError('expanded sentinel body contains data I/O')
        seen+=1
    if not seen:raise ValueError('no compiled lock object inspected')
    print(f'RPC lock: {seen} real preprocessed bodies and {platform} objects verified')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--build',required=True);p.add_argument('--platform',choices=['apple','ofd','windows'],required=True);p.add_argument('--nm',default='nm');a=p.parse_args();check(a.build,a.platform,a.nm)
