#!/usr/bin/env python3
"""12a must remain dormant until atomic store migration in 12b. GPL-2.0-only."""
from pathlib import Path
import re
import sys
import tempfile

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


def collect(directory):
    return {p.relative_to(directory).as_posix(): p.read_text() for folder in ('core', 'engine', 'platform', 'libretro')
            for p in (directory / folder).rglob('*') if p.suffix in ('.c', '.h')}


# Exercise the collector itself, including recursive paths and headers. Merely
# passing synthetic names to check() would miss a narrowed shipping-tree scan.
with tempfile.TemporaryDirectory(prefix='arc-lock-dormancy-') as directory:
    fixture = Path(directory)
    for name in ('core/probe.c', 'engine/nested/probe.h', 'platform/probe.c', 'libretro/probe.h'):
        path = fixture / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('ArcLockResult injected;\n')
        found = collect(fixture)
        if name not in found or not check({name: found[name]}):
            raise SystemExit(f'dormancy collector mutant survived: {name}')

sources = collect(root)
rejected = 0
for windows_paths in (False, True):
    for crlf in (False, True):
        variant = {(name.replace('/', '\\') if windows_paths else name):
                   (text.replace('\n', '\r\n') if crlf else text)
                   for name, text in sources.items()}
        errors = check(variant)
        if errors:
            raise SystemExit('\n'.join(errors))
        for name in ('libretro/rpc_preparation.c', 'libretro/rpc_hd_store.c', 'libretro/rpc_cmos_store.c',
                     'libretro/rpc_launch_record_store.c', 'libretro/arcdroid_libretro.c',
                     'libretro/arc_io.c', 'libretro/arc_io.h', 'core/probe.c',
                     'engine/nested/probe.h', 'platform/probe.c'):
            path = name.replace('/', '\\') if windows_paths else name
            for injected in ('\n#include "arc_lock.h"\n', '\n#include "../libretro/arc_lock.h"\n',
                             '\nvoid injected(void) { arc_lock_acquire(0); }\n',
                             '\nvoid injected(void) { arc_io_mkdir("/save"); }\n',
                             '\n#include "ARC_LOCK.H"\n',
                             '\n#include "..\\libretro\\Arc_Lock.h"\n',
                             '\nArcLockResult type_only;\n',
                             '\n/* arc_lock_held(0) */\n',
                             '\nconst char *commentary = "ArcLockResult";\n'):
                # arc_io owns the VFS mkdir adapter, but never a lock caller.
                if name in ('libretro/arc_io.c', 'libretro/arc_io.h') and 'arc_io_mkdir' in injected:
                    continue
                if crlf:
                    injected = injected.replace('\n', '\r\n')
                # Append to the real source so a truncated scan cannot pass by
                # recognizing only a short, isolated injection.
                if not check({path: variant.get(path, '') + injected}):
                    raise SystemExit(f'dormancy mutant survived: {path}')
                rejected += 1
print('rpc-lock-dormancy: no shipping caller or store enforcement; '
      f'{rejected} migration, type, arc_io, source-tree and documentary negative checks '
      'in POSIX/Windows path forms and LF/CRLF text')
