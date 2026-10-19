#!/usr/bin/env python3
"""Compiler-level adversaries missed by a raw-source call census. GPL-2.0-only."""
import importlib.util,json,os,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('lock_guard',root/'tools/check-rpc-lock-object.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)

def mutant_arguments(args,entry,mutant,obj,windows=None):
    windows=os.name=='nt' if windows is None else windows
    changed=guard.without_compile_outputs(args)
    # Windows compilation databases and argv can spell the same path with
    # different separators, case, or relative components.
    from rpc_ninja_commands import same_path
    sources=[at for at,arg in enumerate(changed[1:],1)
             if not arg.startswith('-') and same_path(arg,entry['file'],entry['directory'],windows)]
    if len(sources)!=1:raise ValueError('expected one original lock source in compiler argv')
    del changed[sources[0]]
    return [*changed,'-I'+(root/'libretro').as_posix(),'-trigraphs','-o',obj.as_posix(),mutant.as_posix()]

def compile_mutant(command,directory,label):
    compiled=subprocess.run(command,cwd=directory,capture_output=True,text=True,errors='replace')
    if compiled.returncode:
        print(f'lock mutant {label}: compiler failed\ncommand argv: {json.dumps(command)}\n'
              f'working directory: {directory}\nreturn code: {compiled.returncode}\n'
              f'stdout:\n{compiled.stdout}\nstderr:\n{compiled.stderr}',file=sys.stderr,flush=True)
    return compiled

def main():
    run(Path(sys.argv[1]),sys.argv[2],sys.argv[3])

def run(build,platform,nm):
    entries=json.loads((build/'compile_commands.json').read_text())
    entry=next(e for e in entries if e['file'].replace('\\','/').endswith('/libretro/arc_lock.c'))
    args=guard.compile_arguments(entry,build);source=Path(entry['file']).read_text()
    # Each replacement is injected through a header; real preprocessing determines calls.
    variants={
     'header-data-io':'#define GetLastError() (WriteFile(0,0,0,0,0),0)\n' if platform=='windows' else '#define close(x) (write(x,"x",1),0)\n',
     'joined-data-io':'#define GetLastError() (Write\\\nFile(0,0,0,0,0),0)\n' if platform=='windows' else '#define close(x) (wr\\\nite(x,"x",1),0)\n',
     'space-joined-data-io':'#define GetLastError() (Write\\ \nFile(0,0,0,0,0),0)\n' if platform=='windows' else '#define close(x) (wr\\ \nite(x,"x",1),0)\n',
     'trigraph-data-io':'#define GetLastError() (Write??/\nFile(0,0,0,0,0),0)\n' if platform=='windows' else '#define close(x) (wr??/\nite(x,"x",1),0)\n',
    }
    if platform=='ofd':
        variants['header-apple']='#include <sys/file.h>\n#define __APPLE__ 1\n'
        variants['classic-lock']='#undef F_OFD_SETLK\n#define F_OFD_SETLK F_SETLK\n'
    elif platform=='apple':
        variants['blocking-flock']='#undef LOCK_NB\n#define LOCK_NB 0\n'
    else:
        variants['blocking-lockfile']='#undef LOCKFILE_FAIL_IMMEDIATELY\n#define LOCKFILE_FAIL_IMMEDIATELY 0\n'
    with tempfile.TemporaryDirectory(prefix='rpc-lock-object-') as temporary:
        work=Path(temporary);(work/'libretro').mkdir();mutant=work/'libretro/arc_lock.c';header=work/'poison.h';obj=work/'lock.o'
        for label,payload in [('control',''),*variants.items()]:
            header.write_text(payload)
            # Include after system declarations to avoid macro damage in system headers.
            # Data-I/O macros must precede native functions, not mapped_error.
            anchor='#if defined(_WIN32)\nstatic unsigned native_open'
            text=source.replace(anchor,'#include "'+header.as_posix()+'"\n'+anchor,1)
            mutant.write_text(text)
            changed=mutant_arguments(args,entry,mutant,obj)
            compiled=compile_mutant(changed,entry['directory'],label)
            if label=='control':assert compiled.returncode==0
            if compiled.returncode:continue # A compiler rejection is also fail-closed.
            (work/'compile_commands.json').write_text(json.dumps([{'directory':entry['directory'],'file':str(mutant),'arguments':changed,'output':str(obj)}]))
            try:guard.check(work,platform,nm,toolchain=build)
            except (ValueError,subprocess.CalledProcessError):
                if label=='control':raise
            else:
                if label=='control':continue
                raise AssertionError('compiled lock mutant survived: '+label)
    print(f'rpc-lock-object: {len(variants)} header, trigraph and joined-call adversaries rejected')

if __name__=='__main__':main()
