#!/usr/bin/env python3
"""Exercise optional probe fixtures with real locks, never shipping hooks. GPL-2.0-only."""
from pathlib import Path
import errno
import os
import re
import subprocess
import sys
import tempfile

compiler, root = sys.argv[1:3]
root = Path(root).resolve()
source_path = Path(sys.argv[sys.argv.index('--source') + 1]) if '--source' in sys.argv else root / 'tests/rpc_lock_process.c'
source = source_path.read_text()
# Replace fixture operations only. arc_lock.c is compiled unchanged, so every
# acquisition, contention, identity check and process-death release uses the OS.
hooks = {
    'symlink(".", symlink_path)': 'fixture_symlink(".", symlink_path)',
    'chmod(first.path, 0400)': 'fixture_chmod(first.path, 0400)',
    'chmod(first.path, 0600)': 'fixture_chmod(first.path, 0600)',
    'rename(replacement_path, first.path)': 'fixture_rename(replacement_path, first.path)',
    'unlink(first.path)': 'fixture_unlink(first.path)',
    'unlink(path)': 'fixture_unlink(path)',
    'open(first.path, O_RDWR|O_CLOEXEC)': 'fixture_open(first.path, O_RDWR|O_CLOEXEC)',
    'stat(alias, &alias_stat)': 'fixture_stat(alias, &alias_stat)',
    'int result = vfs_mkdir(save);': 'int result = fixture_mkdir(save);',
}
for old, new in hooks.items():
    assert source.count(old) == 1, old
    source = source.replace(old, new)
declarations = '''
#include <sys/types.h>
struct stat;
static int fixture_symlink(const char *, const char *);
static int fixture_chmod(const char *, mode_t);
static int fixture_rename(const char *, const char *);
static int fixture_unlink(const char *);
static int fixture_open(const char *, int);
static int fixture_stat(const char *, struct stat *);
static int fixture_mkdir(const char *);
'''
definitions = '''
static int fixture_error(const char *key)
{
    const char *value = getenv(key);
    if (!value) return 0;
    errno = atoi(value); return -1;
}
static int fixture_symlink(const char *from, const char *to)
{ return fixture_error("PROBE_SYMLINK") ? -1 : symlink(from, to); }
static int fixture_chmod(const char *path, mode_t mode)
{
    if (getenv("PROBE_IGNORE_CHMOD")) return 0;
    return fixture_error(mode == 0400 ? "PROBE_CHMOD" : "PROBE_RESTORE") ? -1 : chmod(path, mode);
}
static int fixture_rename(const char *from, const char *to)
{
    if (getenv("PROBE_FALSE_RENAME_SUCCESS")) return 0;
    if (fixture_error("PROBE_RENAME")) {
        int error = errno;
        if (getenv("PROBE_REMOVE_ON_REFUSAL")) assert(unlink(to) == 0);
        errno = error; return -1;
    }
    return rename(from, to);
}
static int fixture_unlink(const char *path)
{ return fixture_error("PROBE_UNLINK") ? -1 : unlink(path); }
static int fixture_open(const char *path, int flags)
{ return fixture_error("PROBE_OPEN") ? -1 : open(path, flags); }
static int fixture_stat(const char *path, struct stat *s)
{ return fixture_error("PROBE_ALIAS_STAT") ? -1 : stat(path, s); }
static int fixture_mkdir(const char *path)
{
    static int calls;
    if (getenv("PROBE_MKDIR_COLLISION") && !calls++) {
        assert(mkdir(path, 0700) == 0);
        printf("occupied-probe-root=%s\\n", path); return -2;
    }
    return vfs_mkdir(path);
}
'''
with tempfile.TemporaryDirectory(prefix='arcdroid-lock-capabilities-') as directory:
    temp = Path(directory)
    probe, binary = temp / 'probe.c', temp / 'probe'
    probe.write_text(declarations + source + definitions)
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
    subprocess.run([compiler, '-std=c17', '-D_GNU_SOURCE=1', '-UNDEBUG', *flags,
                    '-I' + str(root / 'libretro'), '-I' + str(root / 'libretro/include'),
                    str(probe), str(root / 'libretro/arc_lock.c'), str(root / 'libretro/arc_io.c'),
                    '-o', str(binary)], check=True, capture_output=True, text=True)
    count = 0

    used_roots = set()
    def run(label, settings=None, expected=(), fail=False, alias='same', stale=None):
        global count
        count += 1
        save = temp / str(count)
        save.mkdir()
        if stale:
            sentinel = save / 'arcdroid/locks-riscpc/c-0123456789abcdef0123456789abcdef01234567.lock'
            sentinel.parent.mkdir(parents=True)
            sentinel.write_bytes(b'x' if stale == 'nonzero' else b'')
            if stale == 'readonly':
                sentinel.chmod(0o400)
        alternate = str(save / '.')
        if alias == 'distinct':
            other = temp / (str(count) + '-other')
            other.mkdir()
            alternate = str(other)
        env = os.environ.copy()
        env.update({key: str(value) for key, value in (settings or {}).items()})
        result = subprocess.run([str(binary), 'self-test', str(save), alternate],
                                capture_output=True, text=True, timeout=15, env=env)
        assert (result.returncode != 0) == fail, (label, result.stdout, result.stderr)
        if not fail:
            for text in (*expected, 'rpc-lock-process: passed'):
                assert text in result.stdout, (label, text, result.stdout)
            # Qualify each optional leg separately: a mandatory dot-root pass
            # on a symlink-unavailable line does not qualify the symlink leg.
            for leg in re.findall(r'^([\w-]+): (?:unqualified|not qualified)\b', result.stdout, re.M):
                assert not re.search(r'^' + re.escape(leg) + r': passed\b', result.stdout, re.M), (label, leg, result.stdout)
            actual_root = Path(re.search(r'^probe-root=(.+)$', result.stdout, re.M)[1])
            assert actual_root.parent == save and actual_root not in used_roots
            used_roots.add(actual_root)
            occupied = re.search(r'^occupied-probe-root=(.+)$', result.stdout, re.M)
            if occupied:
                assert actual_root != Path(occupied[1]), (label, result.stdout)
            if stale:
                assert sentinel.read_bytes() == (b'x' if stale == 'nonzero' else b'')
                if stale == 'readonly':
                    assert sentinel.stat().st_mode & 0o777 == 0o400
                again = subprocess.run([str(binary), 'self-test', str(save)], capture_output=True, text=True, timeout=15, env=env)
                assert again.returncode == 0, (label, again.stdout, again.stderr)
                next_root = Path(re.search(r'^probe-root=(.+)$', again.stdout, re.M)[1])
                assert next_root != actual_root and next_root.parent == save
        print('probe-capability:', label, 'passed', flush=True)

    run('available fixtures', expected=('supplied-alias: passed', 'symlink-alias: passed',
                                       'rename-over-identity: passed', 'delete-identity: passed'))
    run('stale nonzero sentinel and repeated root', stale='nonzero')
    run('stale readonly sentinel and repeated root', stale='readonly')
    run('occupied PID-counter directory', {'PROBE_MKDIR_COLLISION': 1})
    for code in sorted({errno.ENOSYS, errno.ENOTSUP, errno.EOPNOTSUPP, errno.EPERM, errno.EACCES}):
        run('optional errno ' + str(code),
            {key: code for key in ('PROBE_SYMLINK', 'PROBE_CHMOD', 'PROBE_RENAME', 'PROBE_UNLINK', 'PROBE_ALIAS_STAT')},
            ('symlink-alias: unqualified', 'dot-root alias passed', 'read-only-open: unqualified',
             'rename-over-identity: unqualified', 'delete-identity: unqualified',
             'ownership held and second acquisition contended', 'fixture-cleanup: unqualified',
             'supplied-alias: unqualified'))
    run('distinct alias namespace', alias='distinct', expected=('supplied-alias: unqualified (distinct root',))
    run('missing alias namespace', {'PROBE_ALIAS_STAT': errno.ENOENT}, expected=('supplied-alias: unqualified',))
    run('unsupported permission-check open', {'PROBE_OPEN': errno.ENOSYS},
        expected=('read-only-open: unqualified (permission-check open',))
    run('ignored chmod', {'PROBE_IGNORE_CHMOD': 1},
        expected=('read-only-open: not qualified (filesystem/UID/ACL permits write-open)',))
    run('unsupported mode restoration', {'PROBE_RESTORE': errno.ENOSYS}, expected=('mode-restoration: unqualified',))
    for key in ('PROBE_SYMLINK', 'PROBE_CHMOD', 'PROBE_RENAME', 'PROBE_UNLINK',
                'PROBE_ALIAS_STAT', 'PROBE_OPEN', 'PROBE_RESTORE'):
        run('unexpected EIO ' + key, {key: errno.EIO}, fail=True)
    run('rename success requires actual identity loss', {'PROBE_FALSE_RENAME_SUCCESS': 1}, fail=True)
    run('rename refusal requires ownership still held',
        {'PROBE_RENAME': errno.ENOSYS, 'PROBE_REMOVE_ON_REFUSAL': 1}, fail=True)
    print(f'rpc-lock-probe-capabilities: {count} scenarios passed')
