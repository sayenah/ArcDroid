# SPDX-License-Identifier: GPL-2.0-only
"""Audit identity consumers against CMake's evaluated target properties."""
from pathlib import Path
import re
import sys


def consumers(manifest):
    include = re.compile(r'^\s*#\s*include\s*[<"]arc_build_identity\.h[>"]', re.M)
    found = []
    failures = []
    for record in manifest.read_text().split('---end---\n'):
        if not record:
            continue
        sources, dependencies = record.split('---dependencies---\n')
        target, directory, *paths = sources.splitlines()
        for source in paths:
            path = Path(source)
            # Ignore header listings, object-library expansions and custom rules.
            if path.suffix.lower() not in ('.c', '.cc', '.cpp', '.cxx', '.m', '.mm', '.s'):
                continue
            if not path.is_absolute():
                path = Path(directory) / path
            # Generated sources need not exist before the build. Existing source
            # consumers, including evaluated expressions, are checked now.
            if path.is_file() and include.search(path.read_text()):
                if 'arc_build_identity' not in dependencies.splitlines():
                    failures.append(f'{target}: {source} lacks arc_build_identity dependency')
                found.append(target)
                break
    if not found:
        raise RuntimeError('no compiled build-identity consumers found')
    if failures:
        raise RuntimeError('\n'.join(failures))
    return sorted(found)


if __name__ == '__main__':
    print('build identity dependencies: ' + ', '.join(consumers(Path(sys.argv[1]))))
