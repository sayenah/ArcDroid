#!/usr/bin/env python3
"""Compiler-level adversaries missed by a raw-source call census. GPL-2.0-only."""
import importlib.util,json,shlex,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('lock_guard',root/'tools/check-rpc-lock-object.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
build=Path(sys.argv[1]);platform=sys.argv[2];nm=sys.argv[3]
entries=json.loads((build/'compile_commands.json').read_text())
entry=next(e for e in entries if e['file'].replace('\\','/').endswith('/libretro/arc_lock.c'))
args=entry.get('arguments') or shlex.split(entry['command']);source=Path(entry['file']).read_text()
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
with tempfile.TemporaryDirectory(prefix='rpc-lock-object-') as temporary:
    work=Path(temporary);(work/'libretro').mkdir();mutant=work/'libretro/arc_lock.c';header=work/'poison.h';obj=work/'lock.o'
    for label,payload in [('control',''),*variants.items()]:
        header.write_text(payload)
        # Include after system declarations to avoid macro damage in system headers.
        # Data-I/O macros must precede native functions, not mapped_error.
        anchor='#if defined(_WIN32)\nstatic unsigned native_open'
        text=source.replace(anchor,'#include "'+header.as_posix()+'"\n'+anchor,1)
        mutant.write_text(text)
        changed=[];skip=False
        for arg in args:
            if skip:skip=False;continue
            if arg=='-o':skip=True;continue
            if arg==entry['file']:continue
            changed.append(arg)
        changed += ['-I'+(root/'libretro').as_posix(),'-trigraphs','-o',obj.as_posix(),mutant.as_posix()]
        compiled=subprocess.run(changed,cwd=entry['directory'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        if label=='control':assert compiled.returncode==0
        if compiled.returncode:continue # A compiler rejection is also fail-closed.
        (work/'compile_commands.json').write_text(json.dumps([{'directory':entry['directory'],'file':str(mutant),'arguments':changed,'output':str(obj)}]))
        try:guard.check(work,platform,nm)
        except (ValueError,subprocess.CalledProcessError):
            if label=='control':raise
        else:
            if label=='control':continue
            raise AssertionError('compiled lock mutant survived: '+label)
print(f'rpc-lock-object: {len(variants)} header, trigraph and joined-call adversaries rejected')
