#!/usr/bin/env python3
"""12a must remain dormant until atomic store migration in 12b. GPL-2.0-only."""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve()


def check(sources):
    errors = []
    for name, text in sources.items():
        if name in ('libretro/arc_lock.c', 'libretro/arc_lock.h'):
            continue
        if re.search(r'\barc_lock_\w+|\barc_lock\.h\b|\bArcLock\w*\b', text):
            errors.append(f'{name}: dormant lock referenced by shipping source')
        if name not in ('libretro/arc_io.c', 'libretro/arc_io.h') and re.search(r'\barc_io_mkdir\s*\(', text):
            errors.append(f'{name}: lock-only mkdir migrated to another shipping caller')
    return errors


sources = {str(p.relative_to(root)): p.read_text() for folder in ('core', 'engine', 'platform', 'libretro')
           for p in (root / folder).rglob('*') if p.suffix in ('.c', '.h')}
errors = check(sources)
if errors:
    raise SystemExit('\n'.join(errors))
for name in ('libretro/rpc_preparation.c', 'libretro/rpc_hd_store.c', 'libretro/rpc_cmos_store.c',
             'libretro/rpc_launch_record_store.c', 'libretro/arcdroid_libretro.c'):
    for injected in ('\n#include "arc_lock.h"\n', '\n#include "../libretro/arc_lock.h"\n',
                     '\nvoid injected(void) { arc_lock_acquire(0); }\n',
                     '\nvoid injected(void) { arc_io_mkdir("/save"); }\n'):
        if not check({**sources, name: sources.get(name, '') + injected}):
            raise SystemExit(f'dormancy mutant survived: {name}')
print('rpc-lock-dormancy: no shipping caller or store enforcement; 20 migration mutants refused')
