#!/usr/bin/env python3
"""D-193 exact-once sentinel I/O exception and confinement. GPL-2.0-only."""
from pathlib import Path
import re
import sys

MODULE = 'libretro/arc_lock.c'
POSIX_OPEN = '    int fd = open(path, O_RDWR|O_CREAT|O_CLOEXEC, 0600);'
WINDOWS_OPEN = '    HANDLE file = CreateFileW(wide, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);'
# Member callbacks are VFS operations; bare host service calls are not.
BOUNDARY = r'(?<![\w>.])'
GENERAL = re.compile(BOUNDARY + r'(?:fopen\w*|open(?:64|at|at64)?|creat(?:64)?|fdopen|_wfopen\w*|gz\w*|CreateFile\w*|NtCreateFile|_open\w*|_wopen\w*|_sopen\w*|_wsopen\w*|freopen\w*|tmpfile\w*|mkstemp\w*|dumpregs)\s*\(')
CONFINED = re.compile(BOUNDARY + r'(?:fcntl\w*|flock|LockFileEx|UnlockFileEx|stat(?:64)?|fstat(?:64)?|close|CloseHandle)\s*\(')
DATA = re.compile(BOUNDARY + r'(?:read|write|_read|_write|pread\w*|pwrite\w*|readv|writev|ftruncate\w*|truncate\w*|WriteFile\w*|ReadFile\w*|SetEndOfFile|fopen\w*|freopen\w*|fdopen|fclose\w*|fread\w*|fwrite\w*|fseek\w*|ftell\w*|rewind|fflush\w*|fprintf\w*|vfprintf\w*|fscanf\w*|vfscanf\w*|fgets\w*|fputs\w*|fgetc\w*|fputc\w*|getc|putc|getw|putw|ungetc|setbuf|setvbuf|fileno|printf|vprintf|puts|getchar|putchar|scanf|vscanf|getline|getdelim|dprintf|vdprintf|clearerr|feof|ferror|fgetpos|fsetpos|remove|unlink\w*|rename\w*|DeleteFile\w*|MoveFile\w*|mmap|MapViewOfFile)\s*\(')
CLASSIC = re.compile(r'\b(?:F_SETLK|F_SETLKW|F_OFD_SETLKW)\b')
ARC_COMMENT = ' * dumpregs(), and its obsolete declaration was removed from this header. */'


def code_only(source):
    # Keep line numbers and call tokens, discard strings/comments that merely
    # name an operation (e.g. install_store's diagnostic "close(read)").
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*',
                  lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]),
                  source, flags=re.S)


def violations(sources):
    errors = []
    for name, source in sources.items():
        lines = source.splitlines()
        code_lines = code_only(source).splitlines()
        if name == MODULE:
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
            if name == MODULE and (DATA.search(code) or CLASSIC.search(code)):
                errors.append(f'{name}:{number}: sentinel data/size/namespace or classic/blocking lock call')
    return errors


def require(condition, message):
    if not condition:
        raise ValueError(message)


def self_test():
    original = Path(MODULE).read_text()
    require(not violations({MODULE: original}), 'actual lock module refused')
    rejected = 0

    def reject(sources, label):
        nonlocal rejected
        require(bool(violations(sources)), f'guard mutant survived: {label}')
        rejected += 1

    for line in (POSIX_OPEN, WINDOWS_OPEN):
        reject({'libretro/elsewhere.c': line}, 'allowed line in another file')
        reject({MODULE: original + '\n' + line}, 'duplicate allowed line')
        reject({MODULE: original.replace(line, '')}, 'missing allowed line')
    reject({MODULE: original.replace('FILE_SHARE_READ|FILE_SHARE_WRITE,', 'FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,')}, 'delete sharing')
    for call in ('fcntl(0,0,0)', 'flock(0,0)', 'LockFileEx(0,0,0,0,0,0)',
                 'UnlockFileEx(0,0,0,0,0)', 'stat(0,0)', 'fstat(0,0)', 'close(0)', 'CloseHandle(0)'):
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
    require(not violations({'libretro/adapter.c': 'vfs->stat(p, n); vfs->close(h); backend.read(h);'}), 'VFS callbacks refused')
    print(f'shipping lock I/O self-test: {rejected} negative inputs rejected; exact-once module and VFS callbacks admitted')


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
