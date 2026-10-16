#!/usr/bin/env python3
"""12a behavioral predicates and native syscall contracts. GPL-2.0-only.

Mutants compile isolated copies. No production source, test or golden is edited.
The real blocking-lock mutant must trip the per-attempt watchdog (exit 75).
"""
from pathlib import Path
import errno
import subprocess
import sys
import tempfile
import time

compiler, root = sys.argv[1:3]
root = Path(root).resolve()
original = (root / 'libretro/arc_lock.c').read_text()
probe_original = (root / 'tests/rpc_lock_process.c').read_text()
io_original = (root / 'libretro/arc_io.c').read_text()
unit = (root / 'tests/rpc_lock_test.c').read_text()
native = (root / 'tests/rpc_lock_native_test.c').read_text()


def require(ok, message):
    if not ok:
        raise ValueError(message)


# label, test (unit or native branch), old, new. Every predicate changes an
# observable contract: refused inputs, outcomes, lifetime or syscall arguments.
cases = []


def add(label, old, new, test='unit'):
    cases.append((label, test, old, new))


add('ofd-eacces-is-contention', 'error == EAGAIN)', '(error == EAGAIN || error == EACCES))')
unsupported_codes = ('ENOSYS', 'ENOLCK', 'EOPNOTSUPP', 'ENOTSUP')
if errno.EOPNOTSUPP == errno.ENOTSUP:
    unsupported_codes = ('ENOSYS', 'ENOLCK')
    print('equivalent: individual ENOTSUP/EOPNOTSUPP removals (same errno); mutate their union instead', flush=True)
    add('unsupported-alias-union', 'error == EOPNOTSUPP || error == ENOTSUP', 'false')
for code in unsupported_codes:
    add('unsupported-' + code, 'error == ' + code, 'false')
add('ofd-einval', 'error == EINVAL)', 'false)')
add('eintr', 'error == EINTR', 'false')
add('create-race', '(opening && error == EEXIST)', 'false')
add('windows-contention', '!opening && error == 33', 'false')
add('windows-transient-open', 'opening && error == 32', 'false')
add('windows-invalid-function', 'error == 1 ||', 'false ||')
add('windows-not-supported', 'error == 50', 'false')
add('ofd-contention', 'platform == ARC_LOCK_OFD && error == EAGAIN', 'false')
add('flock-contention', 'platform == ARC_LOCK_FLOCK && error == EWOULDBLOCK', 'false')
add('open-vs-lock-error', 'return opening ? ARC_LOCK_CREATE_ERROR : ARC_LOCK_ERROR;', 'return ARC_LOCK_CONTENDED;')
add('null-initialization', 'if (!lock) return ARC_LOCK_ROOT_REFUSED;', '')
add('windows-open-lock-domain', 'if (platform == ARC_LOCK_WINDOWS) {', 'if (false) {')
add('no-root-validation', 'if (!lock->cursor || !key)', 'if (!key)')
add('no-scheme-check', '!n || strstr(save, "://")', '!n')
add('posix-relative', "return save[0] == '/' ? 1 : 0;", 'return 1;')
add('drive-relative', "save[1] == ':' && separator(lock, save[2])", "save[1] == ':'")
add('unc-device-namespace', " || save[2] == '?' || save[2] == '.'", '')
add('unc-server', '!separator(lock, save[0])', 'false')
add('unc-share', 'if (at == n || ++at == n || separator(lock, save[at]))', 'if (false)')
add('hex-length', 'strlen(key) != n', 'false')
add('hex-validation', "if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))", 'if (false)')
add('hex-case', "if (c >= 'A' && c <= 'F')", 'if (false)')
add('model-limit', 'model < 4 &&', 'true &&')
add('model-index', 'hex, model + 1, procedure, timing', 'hex, model, procedure, timing')
add('content-key', '"c-%s.lock"', '"d-%s.lock"')
add('defaults-key', '"d-%s-m%u-p%u-t%u.lock"', '"d-%s-m%u-t%u-p%u.lock"')
add('wrong-directory', '"%.*s%sarcdroid/locks-riscpc/%s"', '"%.*s%sarcdroid/locks-other/%s"')
add('trailing-separator', 'while (n > lock->cursor && separator(lock, save[n - 1])) --n;', '')
add('path-truncation', 'length < 0 || (size_t)length >= sizeof(lock->path)', 'length < 0')
for field in ('open_handle', 'try_lock', 'identity', 'unlock', 'close_handle'):
    add('backend-missing-' + field, '!lock->backend->' + field + ' ||', 'false ||')
add('backend-domain', '(unsigned)lock->backend->platform > ARC_LOCK_WINDOWS', 'false')
add('namespace-query', 'lock && lock->valid && !lock->parents_done;', 'false;')
add('held-query', 'lock && lock->valid && lock->held && !lock->lost;', 'lock && lock->valid;')
add('release-unlock', 'if (lock->held) lock->backend->unlock(lock->opaque, lock->handle);', '')
add('release-close', 'if (lock->opened) lock->backend->close_handle(lock->opaque, lock->handle);', '')
add('release-double-close', 'lock->held = lock->opened = lock->lost = false;', 'lock->held = lock->lost = false;')
add('verify-unheld', 'if (!lock->held) return ARC_LOCK_ERROR;', '')
add('sticky-identity-loss', 'lock->lost = true;', 'lock->lost = false;')
add('mkdir-exists-rejected', 'result == ARC_IO_MKDIR_EXISTS &&', 'false &&')
add('mkdir-no-directory-check', 'arc_io_metadata(lock->path, NULL) == ARC_IO_METADATA_DIRECTORY', 'true')
add('mkdir-failure-ignored', 'if (!ok) return ARC_LOCK_CREATE_ERROR;', '')
add('open-error-ignored', 'if (error) return mapped_error(lock->backend->platform, true, error);', '')
add('transient-reopens', 'if (result != ARC_LOCK_CONTENDED && result != ARC_LOCK_TRANSIENT)', 'if (true)')
add('failed-lock-leaked', 'if (result != ARC_LOCK_CONTENDED && result != ARC_LOCK_TRANSIENT) arc_lock_release(lock);', '')
add('lock-failure-held', 'return result;\n    }\n    lock->held = true;', 'return ARC_LOCK_HELD;\n    }\n    lock->held = true;')
add('no-post-acquire-identity', 'if (!lock->backend->identity(lock->opaque, lock->handle, lock->path)) {\n        arc_lock_release(lock);\n        return ARC_LOCK_TRANSIENT;', 'if (false) {\n        arc_lock_release(lock);\n        return ARC_LOCK_TRANSIENT;')
add('no-held-identity', 'if (!lock->backend->identity(lock->opaque, lock->handle, lock->path)) {\n        arc_lock_release(lock);\n        lock->lost = true;', 'if (false) {\n        arc_lock_release(lock);\n        lock->lost = true;')
add('vfs-type', 'arc_io_metadata(lock->path, &size) != ARC_IO_METADATA_FILE', 'false')
add('vfs-size', '|| size != 0', '')
add('ofd-classic', 'F_OFD_SETLK, &lock', 'F_SETLK, &lock', 'native0')
add('ofd-blocking-contract', 'F_OFD_SETLK, &lock', 'F_OFD_SETLKW, &lock', 'native0')
add('ofd-shared', 'lock.l_type = F_WRLCK;', 'lock.l_type = F_RDLCK;', 'native0')
add('ofd-whole-file', 'lock.l_type = F_WRLCK;', 'lock.l_type = F_WRLCK; lock.l_len = 1;', 'native0')
add('ofd-pid', 'lock.l_type = F_WRLCK;', 'lock.l_type = F_WRLCK; lock.l_pid = 1;', 'native0')
add('ofd-offset', 'lock.l_type = F_WRLCK;', 'lock.l_type = F_WRLCK; lock.l_start = 1;', 'native0')
add('no-cloexec', 'O_RDWR|O_CREAT|O_CLOEXEC', 'O_RDWR|O_CREAT', 'native0')
add('wrong-create-mode', 'O_CLOEXEC, 0600', 'O_CLOEXEC, 0666', 'native0')
for predicate in ('fstat((int)handle, &descriptor) == 0', 'stat(path, &named) == 0',
                  'descriptor.st_nlink >= 1', 'named.st_nlink >= 1',
                  'descriptor.st_dev == named.st_dev', 'descriptor.st_ino == named.st_ino'):
    add('identity-' + str(len(cases)), predicate, 'true', 'native0')
add('flock-shared', 'LOCK_EX|LOCK_NB', 'LOCK_SH|LOCK_NB', 'native1')
add('flock-blocking-contract', 'LOCK_EX|LOCK_NB', 'LOCK_EX', 'native1')
add('windows-inheritable', 'FILE_SHARE_READ|FILE_SHARE_WRITE, NULL,', 'FILE_SHARE_READ|FILE_SHARE_WRITE, (void *)1,', 'native2')
add('windows-share-delete', 'FILE_SHARE_READ|FILE_SHARE_WRITE,', 'FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,', 'native2')
add('windows-blocking', 'LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY', 'LOCKFILE_EXCLUSIVE_LOCK', 'native2')
add('windows-shared', 'LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY', 'LOCKFILE_FAIL_IMMEDIATELY', 'native2')
add('windows-overlapped', 'OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,', 'OPEN_ALWAYS, FILE_FLAG_OVERLAPPED,', 'native2')
add('windows-truncate', 'NULL, OPEN_ALWAYS,', 'NULL, 2,', 'native2')
add('windows-utf8', 'CP_UTF8, MB_ERR_INVALID_CHARS', 'CP_UTF8, 0', 'native2')
add('windows-open-failure', 'if (file == INVALID_HANDLE_VALUE) return GetLastError();', '', 'native2')
add('windows-conversion-failure', 'if (!MultiByteToWideChar', 'if (0 && !MultiByteToWideChar', 'native2')
# Parenthesization for this targeted conditional replacement.
cases[-1] = (*cases[-1][:2], 'if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 4096))',
             'if (0 && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 4096))')

with tempfile.TemporaryDirectory(prefix='arcdroid-lock-mutants-') as temp:
    temp = Path(temp)
    source, io, test, binary = [temp / n for n in ('arc_lock.c', 'arc_io.c', 'test.c', 'test')]
    includes = [f'-I{root / p}' for p in ('libretro', 'libretro/include', 'tests', 'tests/lock-shims')]

    def build_run(text, kind, io_text=io_original, expect_fail=True):
        source.write_text(text); io.write_text(io_text)
        args = [compiler, '-std=c17', '-O0', '-UNDEBUG', *includes]
        if kind.startswith('native'):
            test.write_text(native.replace('#include "../libretro/arc_lock.c"', f'#include "{source}"'))
            args += [f'-DARC_LOCK_NATIVE_KIND={kind[-1]}', str(test), str(io)]
        elif kind == 'process':
            args += [str(root / 'tests/rpc_lock_process.c'), str(source), str(io)]
        else:
            test.write_text(unit); args += [str(test), str(source), str(io)]
        compiled = subprocess.run([*args, '-o', str(binary)], capture_output=True, text=True, timeout=60)
        require(compiled.returncode == 0, compiled.stderr)
        command = [str(binary)]
        if kind == 'process':
            command += ['self-test', str(temp / 'save')]
        run = subprocess.run(command, capture_output=True, text=True, timeout=15)
        require((run.returncode != 0) == expect_fail, f'{kind}: return={run.returncode}\n{run.stdout}\n{run.stderr}')
        return run

    for kind in ('unit', 'native0', 'native1', 'native2', 'process'):
        build_run(original, kind, expect_fail=False)
    for label, kind, old, new in cases:
        require(old in original, f'{label}: missing anchor')
        text = original.replace(old, new)
        run = build_run(text, kind)
        print(f'killed {label}: {kind} return={run.returncode}', flush=True)
    for label, old, new in (
        ('mkdir-exists', 'result == -2 ? ARC_IO_MKDIR_EXISTS', 'result == -2 ? ARC_IO_MKDIR_ERROR'),
        ('mkdir-created', 'result == 0 ? ARC_IO_MKDIR_CREATED', 'result == 0 ? ARC_IO_MKDIR_ERROR')):
        require(old in io_original, label + ': missing anchor')
        build_run(original, 'unit', io_original.replace(old, new))
        print('killed ' + label, flush=True)
    # A blocked OS call must fail by the test's per-attempt bound, never by
    # CTest's timeout. On macOS this mutates flock; Linux mutates OFD.
    blocking = original.replace('LOCK_EX|LOCK_NB', 'LOCK_EX') if sys.platform == 'darwin' else original.replace('F_OFD_SETLK, &lock', 'F_OFD_SETLKW, &lock')
    start = time.monotonic(); run = build_run(blocking, 'process')
    require(run.returncode == 75 and time.monotonic() - start < 8, f'blocking lock did not trip per-attempt watchdog: {run.returncode}')
    print('killed real blocking OS call: per-attempt watchdog return=75', flush=True)
    # Deletion at release is prohibited AND must change the process oracle.
    text = original.replace('lock->held = lock->opened = lock->lost = false;',
        'remove(lock->path); lock->held = lock->opened = lock->lost = false;')
    build_run(text, 'process'); print('killed sentinel deleted at release', flush=True)
    # Prove Linux/Android cannot silently compile a fallback when the SDK lacks
    # OFD. The shim already included fcntl.h before this undef, so re-inclusion
    # in the production unit cannot restore the constant.
    test.write_text(native.replace('#include "../libretro/arc_lock.c"',
        f'#undef F_OFD_SETLK\n#include "{source}"'))
    source.write_text(original)
    result = subprocess.run([compiler, '-std=c17', '-DARC_LOCK_NATIVE_KIND=0', *includes,
        '-c', str(test), '-o', str(temp / 'missing-ofd.o')], capture_output=True, text=True, timeout=60)
    require(result.returncode != 0 and 'RiscPC ownership requires F_OFD_SETLK' in result.stderr,
            'missing OFD SDK did not trigger the required compile refusal')
    print('missing OFD declaration: compile refused by D-192 #error', flush=True)
    print(f'rpc-lock-mutants: {len(cases) + 4} behavioral mutants killed (plus guard negative inputs)', flush=True)

    # The reporting oracle must kill QA's unconditional-pass mutant and every
    # other optional leg's false qualification without changing native locks.
    reports = [('qa-unconditional-identity-pass', probe_original.replace('if (qualified) printf', 'printf')),
               ('reused-probe-directory', probe_original.replace('if (!result) { printf("probe-root=',
                   'if (!result || result == -2) { printf("probe-root='))]
    for leg in ('symlink-alias', 'supplied-alias', 'read-only-open', 'mode-restoration', 'fixture-cleanup'):
        anchor = next(line for line in probe_original.splitlines() if leg + ': unqualified' in line)
        reports.append((leg + '-false-pass', probe_original.replace(anchor, f'        puts("{leg}: passed");\n' + anchor, 1)))
    anchor = next(line for line in probe_original.splitlines() if 'read-only-open: not qualified' in line)
    reports.append(('ignored-mode-false-pass', probe_original.replace(anchor, '            puts("read-only-open: passed");\n' + anchor, 1)))
    for label, mutated in reports:
        test.write_text(mutated)
        result = subprocess.run([sys.executable, '-B', str(root / 'tests/rpc_lock_probe_capabilities.py'),
            compiler, str(root), '--source', str(test)], capture_output=True, text=True, timeout=60)
        require(result.returncode != 0 and 'AssertionError' in result.stderr,
                f'probe-report mutant survived or failed before its assertion: {label}\n{result.stdout}\n{result.stderr}')
        print('killed probe report mutant: ' + label, flush=True)

    for script, label, old, new, arguments in (
        ('tests/rpc_lock_source_guard.py', 'qa-type-only', r'|\bArcLock\w*\b', '', [str(root)]),
        ('tests/rpc_lock_source_guard.py', 'qa-arc-io-exemption',
         "if name in ('libretro/arc_lock.c', 'libretro/arc_lock.h'):",
         "if name in ('libretro/arc_lock.c', 'libretro/arc_lock.h', 'libretro/arc_io.c'):", [str(root)]),
        ('tests/rpc_lock_source_guard.py', 'qa-narrowed-scan',
         "for folder in ('core', 'engine', 'platform', 'libretro')", "for folder in ('libretro',)", [str(root)]),
        ('tools/check-shipping-lock-io.py', 'qa-comments-strings', 'GENERAL.search(line)', 'GENERAL.search(code)', ['--self-test']),
        ('tools/check-shipping-lock-io.py', 'module-allowlist',
         "errors.extend(f'{name}: {error}' for error in module_calls(source))", 'pass', ['--self-test']),
        ('tools/check-shipping-lock-io.py', 'native-census', 'if counts != Counter(NATIVE_CALLS):', 'if False:', ['--self-test']),
        ('tools/check-shipping-lock-io.py', 'ofd-fields',
         "errors.append('OFD lock fields must retain the whole-file zero-initialized contract')", 'pass', ['--self-test']),
        ('tools/check-shipping-lock-io.py', 'lstat-confinement', 'CONFINED.search(code)',
         "CONFINED.search(code) and 'lstat' not in code", ['--self-test']),
    ):
        text = (root / script).read_text()
        require(text.count(old) == 1, label + ': guard mutation anchor')
        mutated = temp / 'guard.py'; mutated.write_text(text.replace(old, new))
        result = subprocess.run([sys.executable, '-B', str(mutated), *arguments], cwd=root,
                                capture_output=True, text=True, timeout=60)
        require(result.returncode != 0 and ('mutant survived' in result.stdout + result.stderr),
                f'guard mutant not killed by its self-test: {label}\n{result.stdout}\n{result.stderr}')
        print('killed guard mutant: ' + label, flush=True)

    # Existing-directory observation must precede confinement, while creation
    # outside --save stays refused. Run in the isolated directory, not the repo.
    runner = (root / 'tools/rpc_runner_vfs.h').read_text()
    runner_test = (root / 'tests/rpc_vfs_test.c').read_text()
    header = temp / 'rpc_runner_vfs.h'
    test.write_text(runner_test.replace('#include "../tools/rpc_runner_vfs.h"', f'#include "{header}"'))
    for label, old, new in (
        ('runner-existing-directory', 'if(vfs_stat(p,NULL)&RETRO_VFS_STAT_IS_DIRECTORY)return -2;', ''),
        ('runner-mkdir-confinement', 'if(!writable(p))return -1;', ''),
    ):
        require(runner.count(old) == 1, label + ': runner mutation anchor')
        header.write_text(runner.replace(old, new))
        result = subprocess.run([compiler, '-std=c17', '-UNDEBUG', *includes, str(test), '-o', str(binary)],
                                capture_output=True, text=True, timeout=60)
        require(result.returncode == 0, result.stderr)
        result = subprocess.run([str(binary), str(temp / 'runner-save')], cwd=temp,
                                capture_output=True, text=True, timeout=15)
        require(result.returncode != 0, label + ': runner mutant survived')
        print('killed runner mutant: ' + label, flush=True)
