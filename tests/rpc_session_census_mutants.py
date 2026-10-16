#!/usr/bin/env python3
"""One real lost allocation per session must fail the census. GPL-2.0-only."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from rpc_ninja_commands import rebuild_commands, same_path
from rpc_session_hygiene_test import run

build, exe, core, fixtures = (Path(p).resolve() for p in sys.argv[1:5])
profile = sys.argv[5]
root = Path(__file__).resolve().parents[1]
cache = (build / 'CMakeCache.txt').read_text()
ninja = next(x.split('=', 1)[1] for x in cache.splitlines()
             if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands = [x['command'] for x in json.loads(subprocess.check_output(
    [ninja, '-t', 'compdb', '-x'], cwd=build, text=True))]
# Prove the same executable/route passes before accepting a mutant failure.
run(exe, core, fixtures, 'leak-check', profile)
with tempfile.TemporaryDirectory(prefix='arc-census-mutants-') as directory:
    temp = Path(directory)
    source = root / 'libretro/rpc_preparation.c'
    original = source.read_text()
    anchor = 'bool arc_riscpc_preparation_free(ArcRpcPreparation *p,char *e,size_t n)\n{\n    if(!p)return true;'
    if original.count(anchor) != 1:
        raise RuntimeError('preparation teardown anchor changed')
    cc, link = rebuild_commands(commands, source, 'arcdroid_session_census_libretro', build, os.name == 'nt')
    bypass_dir = temp / 'bypass'
    bypass_dir.mkdir()
    bypass = bypass_dir / 'bypass.c'
    bypass.write_text('#undef malloc\nvoid *arc_census_bypass(void) { return malloc(1); }\n')
    compile_cmd = list(cc)
    compile_cmd[compile_cmd.index('-o') + 1] = str(bypass_dir / 'bypass.o')
    compile_cmd[compile_cmd.index('-c') + 1] = str(bypass)
    if '-MF' in compile_cmd:
        compile_cmd[compile_cmd.index('-MF') + 1] = str(bypass_dir / 'bypass.d')
    subprocess.run(compile_cmd, cwd=build, check=True, capture_output=True, timeout=90)
    nm = next(x.split('=', 1)[1] for x in cache.splitlines() if x.startswith('CMAKE_NM:FILEPATH='))
    guard = subprocess.run([sys.executable, str(root / 'tools/check-backend-symbols.py'),
        '--allocation-census=1', '--outside', str(bypass_dir), '--nm', nm,
        *(['--sanitized'] if profile == 'sanitizer' else [])], capture_output=True, text=True, timeout=30)
    if not guard.returncode or 'allocation census bypass:' not in guard.stderr:
        raise RuntimeError('uninstrumented object survived coverage guard: ' + guard.stdout + guard.stderr)
    print('census coverage guard rejected raw allocator bypass', flush=True)
    for allocator in ('malloc(1)', 'calloc(1, 1)', 'realloc(NULL, 1)'):
        # Volatile publication makes the lost allocation observable to the
        # compiler. None of the existing hand-maintained counters can see it.
        mutated = temp / source.name
        mutated.write_text(original.replace(anchor, anchor +
            '\n   static void *volatile planted_leak;\n   planted_leak = ' + allocator + ';\n   (void)planted_leak;'))
        obj = temp / 'mutant.o'
        library = temp / core.name
        compile_cmd = list(cc)
        previous = compile_cmd[compile_cmd.index('-o') + 1]
        compile_cmd[compile_cmd.index('-o') + 1] = str(obj)
        compile_cmd[compile_cmd.index('-c') + 1] = str(mutated)
        if '-MF' in compile_cmd:
            compile_cmd[compile_cmd.index('-MF') + 1] = str(temp / 'mutant.d')
        link_cmd = [str(obj) if same_path(x, previous, build, os.name == 'nt') else x for x in link]
        link_cmd[link_cmd.index('-o') + 1] = str(library)
        link_cmd = ['-Wl,--out-implib,' + str(temp / 'mutant.dll.a')
                    if x.startswith('-Wl,--out-implib,') else x for x in link_cmd]
        subprocess.run(compile_cmd, cwd=build, check=True, capture_output=True, timeout=90)
        subprocess.run(link_cmd, cwd=build, check=True, capture_output=True, timeout=90)
        try:
            run(exe, library, fixtures, 'leak-check', profile)
        except RuntimeError as error:
            output = str(error)
            match = re.search(r'switch-cycles=(\d+) heap-live-min=(\d+) max=(\d+) delta=(\d+) handles=0 rpc-allocations=0 frontend-allocations=0', output)
            blocks = re.search(r'allocation-blocks-min=(\d+) max=(\d+)', output)
            if (not match or not blocks or int(match[4]) != int(match[1]) or
                    int(blocks[2]) - int(blocks[1]) != int(match[1])):
                raise RuntimeError('mutant failed before proving live-allocation growth: ' + output) from error
            print(f'census killed {allocator}: cycles={match[1]} delta={match[4]} bytes; inherited counters zero', flush=True)
        else:
            raise RuntimeError('allocation leak survived: ' + allocator)
