#!/usr/bin/env python3
"""Configured compiler resolution, including native Windows argv. GPL-2.0-only."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('lock_guard',root/'tools/check-rpc-lock-object.py')
guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
spec=importlib.util.spec_from_file_location('lock_mutants',root/'tests/rpc_lock_object_test.py')
mutants=importlib.util.module_from_spec(spec);spec.loader.exec_module(mutants)

with tempfile.TemporaryDirectory(prefix='rpc-lock-compiler-') as temporary:
    build=Path(temporary)
    record=build/'CMakeFiles/4.0.1/CMakeCCompiler.cmake';record.parent.mkdir(parents=True)
    (build/'CMakeFiles/3.0.0').mkdir()
    (build/'CMakeFiles/3.0.0/CMakeCCompiler.cmake').write_text('set(CMAKE_C_COMPILER "/unrelated/cc")\n')
    def configure(compiler,launcher=''):
        # The generated record, not a stale cache value or old version, wins.
        record.write_text('set(CMAKE_C_COMPILER "'+compiler+'")\n')
        (build/'CMakeCache.txt').write_text(
            'CMAKE_CACHE_MAJOR_VERSION:INTERNAL=4\nCMAKE_CACHE_MINOR_VERSION:INTERNAL=0\n'
            'CMAKE_CACHE_PATCH_VERSION:INTERNAL=1\nCMAKE_C_COMPILER:FILEPATH=/unrelated/cc\n'
            'CMAKE_C_COMPILER_LAUNCHER:STRING='+launcher+'\n')
    compiler='D:/tool chain/ucrt64/bin/cc.exe';configure(compiler)
    command=r'cc.exe -DNAME=\"value\" -o CMakeFiles\lock.obj -c "D:/src dir/libretro/arc_lock.c"'
    expected=[compiler,'-DNAME="value"','-o',r'CMakeFiles\lock.obj','-c','D:/src dir/libretro/arc_lock.c']
    assert guard.compile_arguments({'command':command},build,True)==expected
    command=r'"D:\tool chain\ucrt64\bin\cc.exe" '+command[len('cc.exe '):]
    assert guard.compile_arguments({'command':command},build,True)==expected
    assert guard.compile_arguments({'arguments':expected},build,True)==expected
    configure(compiler,'ccache;--compiler-check=content')
    assert guard.compile_arguments({'arguments':['ccache','--compiler-check=content',*expected]},build,True)==expected
    assert guard.compile_arguments({'arguments':['CCACHE.EXE','--compiler-check=content','CC',*expected[1:]]},build,True)==expected
    for args in (['gcc.exe','-c','lock.c'],['ccache','--wrong',*expected]):
        try:guard.compile_arguments({'arguments':args},build,True)
        except ValueError:pass
        else:raise AssertionError('unrelated compiler/launcher accepted')
    configure('cc.exe')
    try:guard.compile_arguments({'command':'cc.exe -c lock.c'},build,True)
    except ValueError:pass
    else:raise AssertionError('relative compiler accepted')
    # Actually execute with no compiler on PATH. The selected absolute program
    # must run; merely returning a plausible basename would not pass this case.
    configure(Path(sys.executable).as_posix())
    args=guard.compile_arguments({'arguments':[Path(sys.executable).name,'-c','print("recorded compiler")']},build)
    env=os.environ.copy();env['PATH']=''
    assert subprocess.check_output(args,env=env,text=True).strip()=='recorded compiler'
    # Output/dependency flags may be separate or joined. Preserve definitions,
    # forced includes and target flags while removing every build output.
    semantic=[compiler,'-DNAME="value"','-include','keep.h','--target=x86_64-w64-mingw32','-c']
    outputs=['-MD','-MMD','-M','-MM','-MP','-MG',
             '-MF','shared.d','-MT','shared.obj','-MQ','quoted target',
             '-MJ','shared.json','-o','shared.obj',
             '-MFjoined.d','-MTjoined.obj','-MQjoined target','-MJjoined.json','-ojoined.obj']
    entry={'directory':'D:/src dir/build','file':'D:/src dir/libretro/arc_lock.c'}
    for source in (entry['file'],r'd:\SRC DIR\libretro\ARC_LOCK.c',r'..\libretro\arc_lock.c'):
        command=mutants.mutant_arguments([*semantic,*outputs,source],entry,
                                        Path('temporary source.c'),Path('temporary object.o'),True)
        assert command==[*semantic,'-I'+(root/'libretro').as_posix(),'-trigraphs',
                         '-o','temporary object.o','temporary source.c']
    for args in ([compiler,'-MF'],[compiler,'-o']):
        try:guard.without_compile_outputs(args)
        except ValueError:pass
        else:raise AssertionError('missing output operand accepted')
    for sources in ([],[entry['file'],entry['file']]):
        try:mutants.mutant_arguments([*semantic,*sources],entry,Path('new.c'),Path('new.o'),True)
        except ValueError:pass
        else:raise AssertionError('missing/duplicate original source accepted')
    # Run the harness through a failing compiler, so its control still fails
    # and CI receives the actual argv, status and both diagnostic streams.
    failure=build/'failing compiler.py'
    failure.write_text('import sys\nprint("compiler stdout")\nprint("compiler stderr",file=sys.stderr)\nsys.exit(7)\n')
    (build/'compile_commands.json').write_text(json.dumps([{
        'directory':str(build),'file':str(root/'libretro/arc_lock.c'),
        'arguments':[sys.executable,str(failure),'-c',str(root/'libretro/arc_lock.c'),'-o','shared.o']}]))
    failed=subprocess.run([sys.executable,'-B',str(root/'tests/rpc_lock_object_test.py'),str(build),'windows','unused-nm'],
                          capture_output=True,text=True)
    assert failed.returncode!=0
    diagnostic=failed.stderr
    for expected in ('lock mutant control: compiler failed','return code: 7',
                     'stdout:\ncompiler stdout','stderr:\ncompiler stderr','AssertionError'):
        assert expected in diagnostic,(expected,diagnostic)
    command=json.loads(next(line.removeprefix('command argv: ') for line in diagnostic.splitlines()
                            if line.startswith('command argv: ')))
    assert command[:2]==[Path(sys.executable).as_posix(),str(failure)]
    assert command[-3]=='-o'
    assert Path(command[-2]).is_absolute() and Path(command[-2]).parent!=build
    assert not (build/'shared.o').exists()
    record.unlink()
    try:guard.compile_arguments({'command':'cc -c lock.c'},build)
    except FileNotFoundError:pass
    else:raise AssertionError('missing configuration fell back to PATH')
print('rpc-lock compiler: compiler resolution, Windows source aliases, isolated outputs and failed-control diagnostics passed')
