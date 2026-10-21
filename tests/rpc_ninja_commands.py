"""Test rebuild command parsing. GPL-2.0-only; no shell execution.

Windows uses the MS C-runtime rules documented by subprocess.list2cmdline:
backslashes are literal except immediately before a double quote. POSIX
shlex with escapes disabled loses the quotes in Ninja's -DNAME=\"value\".
"""
import ntpath
from pathlib import Path
import shlex

def compile_entry(rows, target, source_name):
    """Select one compile DB row regardless of Windows path separators."""
    matches = [row for row in rows
               if f'{target}.dir/' in row['command'].replace('\\', '/')
               and row['file'].replace('\\', '/').rsplit('/', 1)[-1] == source_name]
    if len(matches) != 1:
        raise RuntimeError(f'Expected one {target}/{source_name} compile entry, got {len(matches)}')
    return matches[0]

def same_path(a, b, build, windows=False):
    if windows:
        def canonical(p):
            return ntpath.normcase(ntpath.normpath(ntpath.join(str(build),str(p))))
    else:
        def canonical(p):return (Path(build)/p).resolve()
    return canonical(a)==canonical(b)

def rebuild_commands(commands, source, target, build, windows=False):
    # Split CMake's cd/link/post-build wrappers before decoding argv, as the
    # QA rebuild scripts do. Compare parsed paths, not raw command spelling.
    rows=[split_command(segment.strip(),windows) for command in commands
          for segment in command.split('&&') if ' -o ' in segment]
    compile=next((r for r in rows if '-c' in r and '-o' in r and
        same_path(r[r.index('-c')+1],source,build,windows) and
        f'{target}.dir/' in r[r.index('-o')+1].replace('\\','/')),None)
    if compile is None:raise RuntimeError(f'No {target} compile command for {source}')
    obj=compile[compile.index('-o')+1]
    link=next((r for r in rows if '-c' not in r and '-o' in r and
        any(same_path(x,obj,build,windows) for x in r if x.lower().endswith(('.o','.obj')))),None)
    if link is None:raise RuntimeError(f'No {target} link command for {obj}')
    return compile,link

def split_command(command, windows=False):
    if not windows:
        return shlex.split(command)
    result=[];i=0
    while i<len(command):
        while i<len(command) and command[i] in ' \t':i+=1
        if i==len(command):break
        word=[];quoted=False
        while i<len(command):
            if command[i] in ' \t' and not quoted:break
            if command[i]=='\\':
                start=i
                while i<len(command) and command[i]=='\\':i+=1
                count=i-start
                if i<len(command) and command[i]=='"':
                    word.extend('\\'*(count//2))
                    if count%2:word.append('"');i+=1;continue
                else:
                    word.extend('\\'*count);continue
            if command[i]=='"':
                if quoted and i+1<len(command) and command[i+1]=='"':
                    word.append('"');i+=2;continue
                quoted=not quoted;i+=1
            else:word.append(command[i]);i+=1
        result.append(''.join(word))
    return result
