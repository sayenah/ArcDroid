#!/usr/bin/env python3
"""Independent argv fixtures and Windows CRT encoder checks. GPL-2.0-only."""
import itertools
import subprocess
import sys
sys.dont_write_bytecode=True
from rpc_ninja_commands import split_command,rebuild_commands,same_path

# Shape and escaped version flag from native CI's compile command. Expected
# argv is authored here, not supplied by the parser or a core measurement.
command=r'ccache D:\a\_temp\msys64\ucrt64\bin\cc.exe -DARCDROID_VERSION=\"0.1.0\" -o CMakeFiles/test.obj -c D:/a/ArcDroid/engine/rpc/rpc_session.c'
expected=['ccache',r'D:\a\_temp\msys64\ucrt64\bin\cc.exe','-DARCDROID_VERSION="0.1.0"',
          '-o','CMakeFiles/test.obj','-c','D:/a/ArcDroid/engine/rpc/rpc_session.c']
assert split_command(command,True)==expected
assert split_command('cc -DNAME=\\"value\\" -c /tmp/a.c')==['cc','-DNAME="value"','-c','/tmp/a.c']
assert split_command('"C:\\Program Files\\gcc.exe" "C:\\src dir\\a.c"',True)==[r'C:\Program Files\gcc.exe',r'C:\src dir\a.c']
assert split_command('"a""b"',True)==['a"b']
values=['','plain','two words','a\tb',r'C:\src\file.c','C:\\folder with spaces\\',
        '"quoted"',r'back\"quote',r'back\\"quote',"apostrophe's"]
for args in itertools.product(values,repeat=2):
    assert split_command(subprocess.list2cmdline(args),True)==list(args),args
# Native Windows Ninja uses slash-separated source paths even when pathlib
# supplies backslashes. Include spaces, drive/case changes, relative sources,
# object spelling differences and CMake's shell wrappers.
build=r'D:\a\Arc Droid\build'
compile_line=r'ccache "D:\tool chain\gcc.exe" -DNAME=\"value\" -o CMakeFiles\rpc_orchestration_test.dir\libretro\rpc_preparation.c.obj -c "D:/a/Arc Droid/libretro/rpc_preparation.c"'
link_line=r'cmd.exe /C "cd . && "D:\tool chain\gcc.exe" CMakeFiles/rpc_orchestration_test.dir/libretro/rpc_preparation.c.obj -o rpc_orchestration_test.exe -Wl,--out-implib,librpc_orchestration_test.dll.a && cd ."'
for source in (r'd:\A\ARC DROID\libretro\rpc_preparation.c',r'..\libretro\rpc_preparation.c'):
    cc,ld=rebuild_commands([compile_line,link_line],source,'rpc_orchestration_test',build,True)
    assert cc[0]=='ccache' and cc[1]==r'D:\tool chain\gcc.exe'
    assert '-DNAME="value"' in cc and ld[0]==r'D:\tool chain\gcc.exe'
    assert same_path(cc[cc.index('-o')+1],ld[1],build,True)
    assert 'cmd.exe' not in ld and '&&' not in ld and ld[ld.index('-o')+1]=='rpc_orchestration_test.exe'
for command in ('cc -o CMakeFiles/rpc_orchestration_test.dir/libretro/rpc_preparation.c.o -c /src/libretro/rpc_preparation.c',):
    for wrapper in ('cc {obj} -o rpc_orchestration_test', ': && cc {obj} -o rpc_orchestration_test && :'):
        obj='CMakeFiles/rpc_orchestration_test.dir/libretro/rpc_preparation.c.o'
        cc,ld=rebuild_commands([command,wrapper.format(obj=obj)],'/src/libretro/rpc_preparation.c','rpc_orchestration_test','/build')
        assert ld==['cc',obj,'-o','rpc_orchestration_test']
print('RPC Ninja command parsing: native CI argv, 100 CRT encoder cases and Windows/POSIX rebuild selection passed')
