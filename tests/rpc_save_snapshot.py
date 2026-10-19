"""Save-tree comparisons without reading native lock sentinel ranges.
SPDX-License-Identifier: GPL-2.0-only
"""


def save_snapshot(save):
    files = {}
    for path in save.rglob('*'):
        if not path.is_file():
            continue
        relative = path.relative_to(save)
        name = relative.as_posix()
        if relative.parent.as_posix() == 'arcdroid/locks-riscpc' and path.suffix == '.lock':
            # Presence is represented by the key. stat is valid even while a
            # Windows owner holds the sentinel's byte range; read_bytes is not.
            size = path.stat().st_size
            assert size == 0, f'nonempty lock sentinel: {name}'
            files[name] = ('lock-sentinel', size)
        else:
            files[name] = path.read_bytes()
    return files
