#!/usr/bin/env python3
"""D-193 exact-once sentinel I/O exception and confinement. GPL-2.0-only."""
from pathlib import Path
from collections import Counter
import re
import sys

MODULE = 'libretro/arc_lock.c'
POSIX_OPEN = '    int fd = open(path, O_RDWR|O_CREAT|O_CLOEXEC, 0600);'
WINDOWS_OPEN = '    HANDLE file = CreateFileW(wide, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);'
# Member callbacks are VFS operations; bare host service calls are not.
BOUNDARY = r'(?<![\w>.])'
GENERAL = re.compile(BOUNDARY + r'(?:fopen\w*|open(?:64|at|at64)?|creat(?:64)?|fdopen|_wfopen\w*|gz\w*|CreateFile\w*|NtCreateFile|_open\w*|_wopen\w*|_sopen\w*|_wsopen\w*|freopen\w*|tmpfile\w*|mkstemp\w*|dumpregs)\s*\(')
CONFINED = re.compile(BOUNDARY + r'(?:fcntl\w*|flock|LockFileEx|UnlockFileEx|(?:lstat|fstatat|_stat|_wstat)\w*|stat(?:64)?|fstat(?:64)?|close|CloseHandle)\s*\(')
ARC_COMMENT = ' * dumpregs(), and its obsolete declaration was removed from this header. */'

# Only these native expressions may occur, in these preprocessor branches and
# with these occurrence counts. Pinning arguments also pins flags and ranges.
WIN = ('defined(_WIN32)',)
POSIX = ('else:defined(_WIN32)',)
MAC = (*POSIX, 'defined(__APPLE__)')
OFD = (*POSIX, 'else:defined(__APPLE__)')
NATIVE_CALLS = {
    (WIN, 'MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,4096)'): 1,
    (WIN, 'GetLastError()'): 3,
    (WIN, 'CreateFileW(wide,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL)'): 1,
    (WIN, 'LockFileEx((HANDLE)handle,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&overlap)'): 1,
    (WIN, 'UnlockFileEx((HANDLE)handle,0,1,0,&overlap)'): 1,
    (WIN, 'CloseHandle((HANDLE)handle)'): 1,
    (POSIX, 'open(path,O_RDWR|O_CREAT|O_CLOEXEC,0600)'): 1,
    (MAC, 'flock((int)handle,LOCK_EX|LOCK_NB)'): 1,
    (MAC, 'flock((int)handle,LOCK_UN)'): 1,
    (OFD, 'fcntl((int)handle,F_OFD_SETLK,&lock)'): 2,
    (POSIX, 'fstat((int)handle,&descriptor)'): 1,
    (POSIX, 'stat(path,&named)'): 1,
    (POSIX, 'close((int)handle)'): 1,
}
PURE_CALLS = {'strlen', 'strstr', 'strrchr', 'snprintf', 'memset'}
HELPERS = {'mapped_error', 'separator', 'root_cursor', 'hex_key', 'initialize',
           'arc_lock_content', 'arc_lock_defaults', 'arc_lock_next_namespace',
           'arc_lock_held', 'arc_lock_release', 'arc_lock_verify', 'arc_lock_acquire'}
ADAPTER_CALLS = {
    'arc_io_mkdir(lock->path)',
    'arc_io_metadata(lock->path,NULL)', 'arc_io_metadata(lock->path,&size)',
    'lock->backend->open_handle(lock->opaque,lock->path,&lock->handle)',
    'lock->backend->try_lock(lock->opaque,lock->handle)',
    'lock->backend->identity(lock->opaque,lock->handle,lock->path)',
    'lock->backend->unlock(lock->opaque,lock->handle)',
    'lock->backend->close_handle(lock->opaque,lock->handle)',
}
CALL = re.compile(r'\b\w+(?:(?:\s*->\s*|\s*\.\s*)\w+)*\s*\(')
# Pin the whole directive sequence, including includes and branch conditions.
# Otherwise a macro can hide a call from the census or change a pinned flag.
MODULE_DIRECTIVES = (
    '#ifndef _GNU_SOURCE', '#define _GNU_SOURCE 1', '#endif',
    '#include "arc_lock.h"', '#include "arc_io.h"',
    '#include <errno.h>', '#include <stdio.h>', '#include <string.h>',
    '#if defined(_WIN32)', '#include <windows.h>', '#else',
    '#include <fcntl.h>', '#include <sys/stat.h>', '#include <unistd.h>',
    '#if defined(__APPLE__)', '#include <sys/file.h>',
    '#elif !defined(F_OFD_SETLK)',
    '#error "RiscPC ownership requires F_OFD_SETLK (D-192); no classic-lock fallback"',
    '#endif', '#endif',
    '#if defined(_WIN32)', '#define NATIVE_PLATFORM ARC_LOCK_WINDOWS', '#else',
    '#if defined(__APPLE__)', '#else', '#endif',
    '#if defined(__APPLE__)', '#else', '#endif',
    '#if defined(__APPLE__)', '#define NATIVE_PLATFORM ARC_LOCK_FLOCK', '#else',
    '#define NATIVE_PLATFORM ARC_LOCK_OFD', '#endif', '#endif',
)


def module_calls(source):
    """Small lexical call census, not a C preprocessor or a denylist of APIs."""
    # Recognize C's alternative #/backslash spellings, then splice continued
    # lines before interpreting comments and directives.
    source = source.replace('??=', '#').replace('??/', '\\')
    source = re.sub(r'\\\r?\n', '', source)
    code = code_only(source)
    directives = tuple(line.strip() for line, tokens in
                       zip(source.splitlines(), code.splitlines())
                       if re.match(r'\s*(?:#|%:)', tokens))
    if directives != MODULE_DIRECTIVES:
        return ['module preprocessor directives changed']
    contexts, stack, offset = {}, [], 0
    for line in code.splitlines(keepends=True):
        directive = re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', line)
        if directive:
            kind, condition = directive.groups()
            condition = re.sub(r'\s+', '', condition)
            if kind in ('if', 'ifdef', 'ifndef'):
                stack.append(condition if kind == 'if' else kind + ':' + condition)
            elif kind in ('else', 'elif'):
                stack[-1] = 'else:' + stack[-1] if kind == 'else' else 'elif:' + condition
            else:
                stack.pop()
        contexts[offset] = tuple(stack)
        offset += len(line)
    # Only the pinned directives remain; their branch context above is part of
    # the native-call key. Preserve offsets while excluding their tokens.
    code = re.sub(r'(?m)^\s*#[^\n]*', lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), code)
    counts, errors = Counter(), []
    for match in CALL.finditer(code):
        name = re.sub(r'\s+', '', match[0][:-1])
        if name in {'if', 'for', 'while', 'switch', 'sizeof', 'return'}:
            continue
        end, depth = match.end(), 1
        while end < len(code) and depth:
            depth += (code[end] == '(') - (code[end] == ')')
            end += 1
        # Function definitions declare helpers; their bodies are still scanned.
        if code[end:].lstrip().startswith('{'):
            continue
        expression = re.sub(r'\s+', '', code[match.start():end])
        start = code.rfind('\n', 0, match.start()) + 1
        key = (contexts[start], expression)
        if key in NATIVE_CALLS:
            counts[key] += 1
        elif name not in PURE_CALLS | HELPERS and expression not in ADAPTER_CALLS:
            errors.append('call outside module allowlist: ' + expression)
    if counts != Counter(NATIVE_CALLS):
        errors.append('native call census/arguments/platform changed')
    ofd_fields = Counter(re.sub(r'\s+', '', assignment) for assignment in
                         re.findall(r'\block\.l_\w+\s*=\s*[^;]*;', code))
    if ofd_fields != Counter({'lock.l_type=F_WRLCK;': 1, 'lock.l_type=F_UNLCK;': 1,
                              'lock.l_whence=SEEK_SET;': 2}) or len(re.findall(r'\bstruct\s+flock\s+lock\s*=\s*\{0\}', code)) != 2:
        errors.append('OFD lock fields must retain the whole-file zero-initialized contract')
    without_casts = re.sub(r'\(\s*(?:void|size_t|int|unsigned|intptr_t|HANDLE)\s*\)', '', code)
    if re.search(r'\)\s*\(', without_casts):
        errors.append('indirect call outside module allowlist')
    return errors


def code_only(source):
    # Keep line numbers and call tokens, discard strings/comments that merely
    # name an operation (e.g. install_store's diagnostic "close(read)").
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*',
                  lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]),
                  source, flags=re.S)


def violations(sources):
    errors = []
    for name, source in sources.items():
        name = name.replace('\\', '/')
        lines = source.splitlines()
        code_lines = code_only(source).splitlines()
        if name == MODULE:
            errors.extend(f'{name}: {error}' for error in module_calls(source))
            for allowed in (POSIX_OPEN, WINDOWS_OPEN):
                if lines.count(allowed) != 1:
                    errors.append(f'{name}: pinned open must occur exactly once: {allowed.strip()}')
        for number, (line, code) in enumerate(zip(lines, code_lines), 1):
            allowed = name == MODULE and line in (POSIX_OPEN, WINDOWS_OPEN)
            comment = name == 'engine/arculator/src/arc.h' and line == ARC_COMMENT
            if GENERAL.search(line) and not allowed and not comment:
                errors.append(f'{name}:{number}: forbidden native open/dump: {line.strip()}')
            if name != MODULE and CONFINED.search(code):
                errors.append(f'{name}:{number}: native lock/identity/close outside {MODULE}')
    return errors


def require(condition, message):
    if not condition:
        raise ValueError(message)


def self_test():
    original = Path(MODULE).read_text()
    def variants(sources):
        for windows_paths in (False, True):
            for crlf in (False, True):
                yield {(name.replace('/', '\\') if windows_paths else name):
                       (text.replace('\n', '\r\n') if crlf else text)
                       for name, text in sources.items()}

    for sources in variants({MODULE: original, 'engine/arculator/src/arc.h': ARC_COMMENT}):
        require(not violations(sources), 'actual lock module/header exemption refused')
    rejected = 0

    def reject(sources, label):
        nonlocal rejected
        for variant in variants(sources):
            require(bool(violations(variant)), f'guard mutant survived: {label}')
        rejected += 1

    for line in (POSIX_OPEN, WINDOWS_OPEN):
        reject({'libretro/elsewhere.c': line}, 'allowed line in another file')
        reject({MODULE: original + '\n' + line}, 'duplicate allowed line')
        reject({MODULE: original.replace(line, '')}, 'missing allowed line')
    reject({MODULE: original.replace('FILE_SHARE_READ|FILE_SHARE_WRITE,', 'FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,')}, 'delete sharing')
    for call in ('fcntl(0,0,0)', 'flock(0,0)', 'LockFileEx(0,0,0,0,0,0)',
                 'UnlockFileEx(0,0,0,0,0)', 'stat(0,0)', 'fstat(0,0)', 'close(0)', 'CloseHandle(0)',
                 'lstat(0,0)', 'fstatat(0,0,0,0)', '_stat(0,0)', '_wstat(0,0)'):
        reject({'libretro/elsewhere.c': f'void f(void) {{ {call}; }}'}, call)
    for name in ('openat', 'creat', 'fdopen', 'CreateFile2', 'NtCreateFile', '_open', '_wopen',
                 '_sopen', '_sopen_s', '_wsopen', 'open64', 'fopen64', 'fopen_s', '_wfopen_s',
                 'freopen', 'tmpfile', 'mkstemp'):
        # _wsopen belongs to the same Windows open family.
        reject({'libretro/elsewhere.c': f'void f(void) {{ {name}(0); }}'}, name)
    for name in ('read', 'write', 'pread', 'pwrite', 'ftruncate', 'WriteFile', 'ReadFile',
                 'SetEndOfFile', 'fopen', 'fclose', 'fread', 'fwrite', 'fprintf', 'fscanf',
                 'fgets', 'fputs', 'fgetc', 'fputc', 'fflush', 'fseek', 'ftell', 'rewind',
                 'setvbuf', 'remove', 'unlink', 'rename', 'DeleteFileW', 'mmap',
                 'printf', 'puts', 'getchar', 'putchar', 'scanf', 'getline', 'getdelim',
                 'dprintf', 'fgetpos', 'fsetpos', '_read', '_write'):
        reject({MODULE: original + f'\nvoid injected(void) {{ {name}(0); }}'}, name)
    for name in ('F_SETLK', 'F_SETLKW', 'F_OFD_SETLKW'):
        reject({MODULE: original + f'\nvoid injected(void) {{ fcntl(0,{name},0); }}'}, name)
    for call in ('lockf(fd,F_TLOCK,0)', 'fallocate(fd,0,0,1)', 'posix_fallocate(fd,0,1)',
                 'SetFileInformationByHandle(h,0,0,0)', 'ioctl(fd,0)', 'dup(fd)',
                 'lseek(fd,0,0)', 'sendfile(fd,fd,0,1)', 'fcntl(fd,F_SETFD,0)',
                 'arc_io_remove(lock->path)', 'arc_io_write_all(lock->path,0,0)',
                 'lstat(path,0)', 'unexpected_future_io(fd)', '(*callback)(fd)'):
        reject({MODULE: original + f'\nvoid injected(void) {{ {call}; }}'}, call)
    for call in ('lockf(fd,F_TLOCK,0)', 'unlink(lock->path)',
                 'write((int)lock->handle,"x",1)'):
        for definition, use in ((f'#define QA_IO {call}', ''),
                                (f'#define QA_IO {call}', 'QA_IO;'),
                                (f'#define QA_IO() {call}', 'QA_IO();')):
            reject({MODULE: original + f'\n{definition}\nvoid injected(void) {{ {use} }}'},
                   'macro body: ' + definition + ' ' + use)
    for flag, value in (('F_OFD_SETLK', 'F_SETLKW'),
                        ('LOCKFILE_FAIL_IMMEDIATELY', '0'), ('LOCK_NB', '0'),
                        ('O_CLOEXEC', '0'), ('FILE_SHARE_READ', '0'),
                        ('FILE_SHARE_WRITE', '(0x2|0x4)'), ('FILE_SHARE_DELETE', '0')):
        for directive in (f'#undef {flag}', f'#define {flag} {value}',
                          f'#undef {flag}\n#define {flag} {value}'):
            reject({MODULE: original + '\n' + directive + '\n'}, directive)
    for directive in ('#define QA_FLAG LOCK_NB', '#define QA_ALIAS unlink',
                      '#include "other.h"', '#pragma include_alias("arc_lock.h", "other.h")',
                      '#if 0\n#endif', '#endif',
                      '#de\\\nfine QA_IO unlink(lock->path)',
                      '%:define QA_IO unlink(lock->path)',
                      '??=define LOCK_NB 0', '#de??/\nfine LOCK_NB 0',
                      '/* comment */ #define QA_IO unlink(lock->path)'):
        reject({MODULE: original + '\n' + directive + '\n'}, directive)
    for old, new in (('#define _GNU_SOURCE 1', '#define _GNU_SOURCE 0'),
                     ('#define NATIVE_PLATFORM ARC_LOCK_OFD',
                      '#define NATIVE_PLATFORM ARC_LOCK_FLOCK'),
                     ('#include "arc_io.h"', '#include "other.h"')):
        require(old in original, 'directive mutation anchor missing')
        reject({MODULE: original.replace(old, new)}, new)
    for old, new in (
        ('LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY', 'LOCKFILE_EXCLUSIVE_LOCK'),
        ('LOCK_EX|LOCK_NB', 'LOCK_EX'),
        ('lock.l_type = F_WRLCK;', 'lock.l_type = F_RDLCK;'),
        ('lock.l_type = F_WRLCK;', 'lock.l_type = F_WRLCK; lock.l_pid = 1;'),
        ('lock.l_type = F_WRLCK;', 'lock.l_type = F_WRLCK; lock.l_len = 1;'),
        ('struct flock lock = {0};', 'struct flock lock;'),
        ('#if defined(__APPLE__)\n    if (flock', '#if !defined(__APPLE__)\n    if (flock'),
        ('    if (flock((int)handle, LOCK_EX|LOCK_NB) == 0) return 0;',
         '    (void)flock((int)handle, LOCK_EX|LOCK_NB);\n    if (flock((int)handle, LOCK_EX|LOCK_NB) == 0) return 0;'),
    ):
        require(old in original, 'native guard mutation anchor missing')
        reject({MODULE: original.replace(old, new)}, old)
    # Raw open/dump references are forbidden even in documentary text. Other
    # comment/string mentions do not become executable identity/data calls.
    for text in ('/* open(0,0); */', '// fopen(0,0)', 'const char *s="CreateFileW(0)";',
                 'const char *s="dumpregs()";'):
        reject({'libretro/elsewhere.c': text}, 'raw reference: ' + text)
        reject({MODULE: original + '\n' + text}, 'module raw reference: ' + text)
    for sources in variants({MODULE: original + '\n/* write(fd,0,0); lstat(0,0); */\n',
                             'libretro/adapter.c': '/* close(0); lstat(0,0); */ const char *s="stat(0,0) close(read)";'}):
        require(not violations(sources), 'non-executable identity/data text refused')
    for sources in variants({MODULE: original + '\n/*\n#define QA_IO unlink(0)\n*/\n'
                             '// #undef LOCK_NB\nconst char *s = "#define LOCK_NB 0";\n'}):
        require(not violations(sources), 'non-executable directive text refused')
    for sources in variants({'libretro/adapter.c': 'vfs->stat(p, n); vfs->close(h); backend.read(h);'}):
        require(not violations(sources), 'VFS callbacks refused')
    print(f'shipping lock I/O self-test: {rejected} negative inputs rejected; '
          'exact-once module and VFS callbacks admitted in POSIX/Windows path forms and LF/CRLF text')


def main():
    if sys.argv[1:] == ['--self-test']:
        self_test()
    else:
        errors = violations({name: Path(name).read_text() for name in sys.argv[1:]})
        require(not errors, '\n'.join(errors))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as error:
        print(f'shipping I/O guard: {error}', file=sys.stderr)
        sys.exit(1)
