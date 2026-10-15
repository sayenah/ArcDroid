#!/usr/bin/env python3
"""12a must remain dormant until atomic store migration in 12b. GPL-2.0-only."""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve()


def check(sources):
    errors = []
    for name, text in sources.items():
        # Path string separators are native on Windows; exemptions are repo names.
        name = name.replace('\\', '/')
        if name in ('libretro/arc_lock.c', 'libretro/arc_lock.h'):
            continue
        if re.search(r'\barc_lock_\w+|\b(?i:arc_lock\.h)\b|\bArcLock\w*\b', text):
            errors.append(f'{name}: dormant lock referenced by shipping source')
        if name not in ('libretro/arc_io.c', 'libretro/arc_io.h') and re.search(r'\barc_io_mkdir\s*\(', text):
            errors.append(f'{name}: lock-only mkdir migrated to another shipping caller')
    return errors


sources = {p.relative_to(root).as_posix(): p.read_text() for folder in ('core', 'engine', 'platform', 'libretro')
           for p in (root / folder).rglob('*') if p.suffix in ('.c', '.h')}
for windows_paths in (False, True):
    for crlf in (False, True):
        variant = {(name.replace('/', '\\') if windows_paths else name):
                   (text.replace('\n', '\r\n') if crlf else text)
                   for name, text in sources.items()}
        errors = check(variant)
        if errors:
            raise SystemExit('\n'.join(errors))
        for name in ('libretro/rpc_preparation.c', 'libretro/rpc_hd_store.c', 'libretro/rpc_cmos_store.c',
                     'libretro/rpc_launch_record_store.c', 'libretro/arcdroid_libretro.c'):
            path = name.replace('/', '\\') if windows_paths else name
            for injected in ('\n#include "arc_lock.h"\n', '\n#include "../libretro/arc_lock.h"\n',
                             '\nvoid injected(void) { arc_lock_acquire(0); }\n',
                             '\nvoid injected(void) { arc_io_mkdir("/save"); }\n',
                             '\n#include "ARC_LOCK.H"\n',
                             '\n#include "..\\libretro\\Arc_Lock.h"\n'):
                if crlf:
                    injected = injected.replace('\n', '\r\n')
                if not check({**variant, path: variant.get(path, '') + injected}):
                    raise SystemExit(f'dormancy mutant survived: {path}')
print('rpc-lock-dormancy: no shipping caller or store enforcement; 20 migration mutants refused '
      'in POSIX/Windows path forms and LF/CRLF text; 10 header-case mutants also refused')
