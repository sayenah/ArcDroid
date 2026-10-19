# SPDX-License-Identifier: GPL-2.0-only
"""Build the shared identity rule in an isolated authored Git repository."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

cmake, compiler, make = sys.argv[1:]
root = Path(__file__).resolve().parents[1]


def run(*args, cwd):
    return subprocess.check_output(args, cwd=cwd, text=True, stderr=subprocess.STDOUT,
                                   timeout=60).strip()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


with tempfile.TemporaryDirectory(prefix='arc-build-identity-') as directory:
    repo = Path(directory) / 'source'
    repo.mkdir()
    for relative in ('cmake/ArcBuildIdentity.cmake', 'cmake/update-build-identity.cmake',
                     'libretro/arc_build_identity.h.in'):
        dest = repo / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / relative, dest)
    (repo / 'CMakeLists.txt').write_text('''# SPDX-License-Identifier: GPL-2.0-only
cmake_minimum_required(VERSION 3.20)
project(IdentityProbe C)
file(APPEND "${CMAKE_BINARY_DIR}/configure-count" "configured\\n")
include(cmake/ArcBuildIdentity.cmake)
arc_build_identity_setup()
add_executable(probe probe.c)
add_dependencies(probe arc_build_identity)
target_include_directories(probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
''')
    # Compile the actual frontend logger with a tiny log sink. This checks the
    # emitted G-3 line, not just the generated macro's spelling.
    frontend = (root / 'libretro/arcdroid_libretro.c').read_text()
    logger = re.search(r'(?ms)^static void arc_build_identity\(void\)\n\{.*?^\}', frontend)
    require(logger is not None, 'missing frontend identity logger')
    (repo / 'probe.c').write_text('''/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include "arc_build_identity.h"
#define RETRO_LOG_INFO 1
#define ARCDROID_VERSION "authored"
#define ARC_BUILD_TARGET "probe"
#define ARC_BUILD_CONFIG "test"
#define ARC_BUILD_SUBSTITUTIONS ""
static uint64_t arc_init_count = 1;
static void log_line(int level, const char *format, ...) {
    (void)level; va_list args; va_start(args, format); vprintf(format, args); va_end(args);
}
static void (*arc_log)(int, const char *, ...) = log_line;
''' + frontend[frontend.index('__attribute__((used)) static const char arc_rpc_build_descriptor'):logger.start()] + logger.group(0) + '\nint main(void) { arc_build_identity(); }\n')
    (repo / 'tracked').write_text('one\n')
    # Ignore build products just as the project does, including untracked files.
    (repo / '.gitignore').write_text('ignored\n')
    run('git', 'init', '-q', cwd=repo)
    run('git', 'config', 'user.name', 'ArcDroid authored test', cwd=repo)
    run('git', 'config', 'user.email', 'test@example.invalid', cwd=repo)
    run('git', 'add', '.', cwd=repo)
    run('git', 'commit', '-qm', 'authored initial source', cwd=repo)
    build = Path(directory) / 'build'
    generator = 'Ninja' if 'ninja' in Path(make).name.lower() else 'Unix Makefiles'
    run(cmake, '-S', str(repo), '-B', str(build), '-G', generator,
        '-DCMAKE_C_COMPILER=' + compiler, '-DCMAKE_MAKE_PROGRAM=' + make,
        '-DARCDROID_SOURCE_REVISION=stale-override', cwd=repo)
    cache = (build / 'CMakeCache.txt').read_bytes()
    binary = build / ('probe.exe' if os.name == 'nt' else 'probe')
    header = build / 'generated/arc_build_identity.h'

    def check(expected):
        run(cmake, '--build', str(build), '--target', 'probe', cwd=repo)
        line = run(str(binary), cwd=repo)
        require(line == 'ArcDroid build: version=authored commit=' + expected +
                ' target=probe config=test gate=closed substitutions=none catalogue=production init=1', line)
        require((build / 'configure-count').read_text() == 'configured\n', 'build reconfigured')
        require((build / 'CMakeCache.txt').read_bytes() == cache, 'configure cache changed')
        return header.stat().st_mtime_ns, binary.stat().st_mtime_ns

    first = run('git', 'rev-parse', '--short=12', 'HEAD', cwd=repo)
    unchanged = check(first)
    require(check(first) == unchanged, 'clean no-op build rewrote identity or binary')
    (repo / 'tracked').write_text('two\n')
    dirty_unchanged = check(first + '-dirty')
    run('git', 'add', 'tracked', cwd=repo)
    require(check(first + '-dirty') == dirty_unchanged,
            'staging an already-dirty tree rewrote identity or binary')
    run('git', 'commit', '-qm', 'authored incremental change', cwd=repo)
    second = run('git', 'rev-parse', '--short=12', 'HEAD', cwd=repo)
    require(second != first, 'test commits are identical')
    check(second)  # No configure command after either commit or checkout.
    run('git', 'checkout', '-q', '--detach', first, cwd=repo)
    check(first)
    (repo / 'untracked').write_text('authored\n')
    check(first + '-dirty')
    (repo / 'untracked').unlink()
    (repo / 'ignored').write_text('ignored build data\n')
    check(first)
    # Archives must not accidentally discover an enclosing checkout.
    (repo / '.git').rename(Path(directory) / 'git-metadata')
    check('stale-override')
    run(cmake, '-DARC_IDENTITY_SOURCE=' + str(repo), '-DARC_IDENTITY_REVISION=',
        '-DARC_IDENTITY_TEMPLATE=' + str(repo / 'libretro/arc_build_identity.h.in'),
        '-DARC_IDENTITY_OUTPUT=' + str(header), '-P',
        str(repo / 'cmake/update-build-identity.cmake'), cwd=repo)
    require('"unknown"' in header.read_text(), 'archive without revision')
print('build identity: commit/checkout without configure, dirty/clean, no-op and archive passed')
