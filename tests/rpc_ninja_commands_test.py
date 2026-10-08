#!/usr/bin/env python3
"""Independent argv fixtures and Windows CRT encoder checks. GPL-2.0-only."""
import itertools
import subprocess
import sys
sys.dont_write_bytecode=True
from rpc_ninja_commands import split_command

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
print('RPC Ninja command parsing: native CI argv and 100 CRT encoder cases passed')
