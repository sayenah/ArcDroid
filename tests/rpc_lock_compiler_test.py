#!/usr/bin/env python3
"""Configured compiler resolution, including native Windows argv. GPL-2.0-only."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('lock_guard',root/'tools/check-rpc-lock-object.py')
guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)

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
    record.unlink()
    try:guard.compile_arguments({'command':'cc -c lock.c'},build)
    except FileNotFoundError:pass
    else:raise AssertionError('missing configuration fell back to PATH')
print('rpc-lock compiler: Windows paths/quotes/argv, recorded launcher/version, empty PATH and fail-closed metadata passed')
