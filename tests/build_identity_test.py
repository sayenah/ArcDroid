# SPDX-License-Identifier: GPL-2.0-only
"""Build the shared identity rule in an isolated authored Git repository."""
from pathlib import Path
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

cmake, compiler, make = sys.argv[1:]
root = Path(__file__).resolve().parents[1]


def run(*args, cwd, env=None):
    return subprocess.check_output(args, cwd=cwd, env=env, text=True, stderr=subprocess.STDOUT,
                                   timeout=60).strip()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def write_lf(path, text):
    path.write_bytes(text.encode('utf-8'))


with tempfile.TemporaryDirectory(prefix='arc-build-identity-') as directory:
    repo = Path(directory) / 'source'
    repo.mkdir()
    for relative in ('cmake/ArcBuildIdentity.cmake', 'cmake/update-build-identity.cmake',
                     'libretro/arc_build_identity.h.in', 'tools/stage-rpc-artifacts.py',
                     'tools/check-rpc-artifacts.py', 'COPYING', 'THIRD_PARTY_NOTICES.md'):
        dest = repo / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / relative, dest)
    # Exercise a transitive local import as well as the classifier itself.
    classifier=repo/'tools/check-rpc-artifacts.py'
    write_lf(classifier,classifier.read_text()+'\nimport stage_import_probe\n')
    write_lf(repo/'tools/stage_import_probe.py','# GPL-2.0-only; authored transitive import fixture.\n')
    write_lf(repo / 'CMakeLists.txt', '''# SPDX-License-Identifier: GPL-2.0-only
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
    write_lf(repo / 'probe.c', '''/* SPDX-License-Identifier: GPL-2.0-only */
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
    write_lf(repo / 'tracked','one\n')
    # Ignore build products just as the project does, including untracked files.
    write_lf(repo / '.gitignore',(root/'.gitignore').read_text()+'\nignored\n')
    raw_crlf=b'/* GPL-2.0-only; authored CRLF fixture. */\r\n'
    (repo / 'raw-crlf.c').write_bytes(raw_crlf)
    run('git', 'init', '-q', cwd=repo)
    run('git', 'config', 'user.name', 'ArcDroid authored test', cwd=repo)
    run('git', 'config', 'user.email', 'test@example.invalid', cwd=repo)
    run('git', 'config', 'core.autocrlf', 'false', cwd=repo)
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
    write_lf(repo / 'tracked','two\n')
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

    def stage_core(source, executable, expected, env=None, failure=None):
        payload=source/'build/stage-input';payload.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(executable,payload/executable.name)
        manifest=source/'build/manifest.json'
        manifest.write_text(json.dumps({'targets':[{'target':'probe','file':executable.name,
                                                   'gate_open':0,'authored':0}]}))
        output=source/'build/public-identity'
        stage_env=dict(os.environ if env is None else env)
        for key in ('PYTHONDONTWRITEBYTECODE','PYTHONPYCACHEPREFIX'):stage_env.pop(key,None)
        result=subprocess.run([sys.executable,str(source/'tools/stage-rpc-artifacts.py'),
            '--input',str(payload),'--output',str(output),'--manifest',str(manifest)],
            cwd=source,env=stage_env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
        if failure:
            require(result.returncode!=0 and failure in result.stdout,result.stdout)
        else:
            require(result.returncode==0,result.stdout)
            record=json.loads((output/'SOURCE.json').read_text())
            require(record=={'revision':expected,'url':'https://github.com/sayenah/ArcDroid/tree/'+expected,
                             'archive':'https://github.com/sayenah/ArcDroid/archive/'+expected+'.zip'},record)
            require(json.loads((output/'CLASSIFIED.json').read_text())['source']==record,'classified source differs')

    def stage_windows_order(source,executable,env):
        stage_env=dict(env)
        for key in ('PYTHONDONTWRITEBYTECODE','PYTHONPYCACHEPREFIX'):stage_env.pop(key,None)
        require(not run('git','status','--porcelain',cwd=source,env=stage_env),'clone is not clean before staging')
        incoming=source/'build/windows';package=incoming/'retroarch-package';package.mkdir(parents=True)
        name='rpc-stage-probe.exe'
        shutil.copyfile(executable,incoming/name);shutil.copyfile(executable,package/name)
        write_lf(package/'authored.adf','authored package media\n')
        manifest=incoming/'rpc-artifact-targets.json'
        manifest.write_text(json.dumps({'targets':[{'target':'probe','file':name,'gate_open':0,'authored':0}]}))
        for payload,output,extra in ((incoming,'public-diagnostics',['--diagnostics']),
                                     (package,'public-retroarch-input',[])):
            command=[sys.executable,str(source/'tools/stage-rpc-artifacts.py'),'--input',str(payload),
                     '--output',str(source/'build'/output),'--manifest',str(manifest),*extra]
            result=run(*command,cwd=source,env=stage_env)
            require((source/'build'/output/name).is_file(),'staging omitted the diagnostic probe')
            require(not list(source.rglob('__pycache__')),'staging created bytecode despite ignored caches')
            require(not run('git','status','--porcelain',cwd=source,env=stage_env),'staging dirtied the clone')
            print(f'Windows-order {output}: {result}; no bytecode; git status --porcelain empty')
        require((source/'build/public-retroarch-input/authored.adf').is_file(),'package media omitted')
        # Ignore only caches, at every depth; an ordinary tools source remains
        # visible; tracked edits and untracked source must still refuse.
        for relative in ('__pycache__/probe.pyc','tools/__pycache__/probe.pyc',
                         'tests/nested/__pycache__/probe.pyc'):
            cache=source/relative;cache.parent.mkdir(parents=True,exist_ok=True);cache.write_bytes(b'authored cache')
            require(run('git','check-ignore',relative,cwd=source,env=stage_env)==relative,'cache not ignored: '+relative)
        require(not run('git','status','--porcelain',cwd=source,env=stage_env),'ignored cache dirtied clone')
        for relative in ('tracked','tools/x.py'):
            path=source/relative;original=path.read_bytes() if path.exists() else None
            write_lf(path,'# GPL-2.0-only; genuine source edit\n')
            for extra in ([],['--diagnostics']):
                refused=subprocess.run([sys.executable,str(source/'tools/stage-rpc-artifacts.py'),
                    '--input',str(incoming),'--output',str(source/'build/public-refused'),
                    '--manifest',str(manifest),*extra],cwd=source,env=stage_env,text=True,
                    stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
                require(refused.returncode!=0 and 'requires an exact clean source checkout' in refused.stdout,refused.stdout)
                require(not (source/'build/public-refused').exists(),'dirty refusal wrote a stage')
            if original is None:path.unlink()
            else:path.write_bytes(original)
            print('Windows-order source dirt refused: '+relative+' (package and diagnostics)')
        require(not run('git','status','--porcelain',cwd=source,env=stage_env),'refusal probe left source dirt')

    # GitHub checks out a synthetic two-parent PR merge, not the branch head.
    run('git','branch','event-topic',second,cwd=repo)
    run('git','checkout','-qb','event-base',first,cwd=repo)
    write_lf(repo/'base-only','authored base change\n')
    run('git','add','base-only',cwd=repo)
    run('git','commit','-qm','authored base change',cwd=repo)
    run('git','merge','--no-ff','-qm','authored PR merge','event-topic',cwd=repo)
    merged=run('git','rev-parse','HEAD',cwd=repo)
    require(len(run('git','rev-list','--parents','-n','1','HEAD',cwd=repo).split())==3,'not a merge commit')
    run('git','update-ref','refs/pull/566/merge',merged,cwd=repo)
    event_path=Path(directory)/'event.json'
    event_path.write_text(json.dumps({'pull_request':{'head':{'sha':run('git','rev-parse',second,cwd=repo)}}}))
    for event,ref in [('pull_request','refs/pull/566/merge'),('push','event-topic'),
                      ('schedule','event-base'),('workflow_dispatch','event-base')]:
        run('git','checkout','-q','--detach',ref,cwd=repo)
        revision=run('git','rev-parse','HEAD',cwd=repo);check(revision[:12])
        event_env=os.environ.copy();event_env.update(GITHUB_EVENT_NAME=event,GITHUB_SHA=revision,
            GITHUB_EVENT_PATH=str(event_path),GITHUB_HEAD_REF='event-topic')
        stage_core(repo,binary,revision,event_env)
    # A clean but unrelated source cannot bless a binary built at the merge.
    run('git','checkout','-q','--detach',first,cwd=repo)
    stage_core(repo,binary,first,failure='binary does not identify the exact clean corresponding source')
    check(first)
    write_lf(repo/'tracked','genuinely changed\n')
    stage_core(repo,binary,first,failure='requires an exact clean source checkout')
    check(first+'-dirty')
    write_lf(repo/'tracked','one\n')
    stage_core(repo,binary,first,failure='binary does not identify the exact clean corresponding source')
    check(first)
    (repo/'untracked').write_text('genuine untracked source\n')
    stage_core(repo,binary,first,failure='requires an exact clean source checkout')
    (repo/'untracked').unlink()

    # Use the actual policy in both Windows jobs, including checkout's Git and
    # MSYS2 Git. It takes effect before checkout and never rewrites a dirty tree.
    workflow=(root/'.github/workflows/ci.yml').read_text()
    policy={'GIT_CONFIG_COUNT':'1','GIT_CONFIG_KEY_0':'core.autocrlf','GIT_CONFIG_VALUE_0':'false'}
    for job in ('windows','windows-retroarch'):
        section=re.search(r'(?ms)^  '+job+r':\n(.*?)(?=^  [\w-]+:|\Z)',workflow)
        require(section is not None,'missing Windows job')
        # The MSYS2 shell does not inherit the runner's Git/tool PATH. Both
        # jobs build exact identities and inspect PE objects before consuming
        # them, so each must provision its own build and guard tools.
        installs=re.findall(r'^          install: (.+)$',section[1],re.M)
        require(len(installs)==1,'missing unambiguous MSYS2 package list: '+job)
        required={'git',*('mingw-w64-ucrt-x86_64-'+tool for tool in
                         ('binutils','gcc','cmake','ninja','python'))}
        missing=required-set(installs[0].split())
        require(not missing,job+' lacks build/guard packages: '+', '.join(sorted(missing)))
        for key,value in policy.items():
            require(re.search(r'^      '+key+r": ['\"]?"+value+r"['\"]?\s*$",section[1],re.M),
                    'missing pre-checkout Windows Git policy: '+key)
        if job=='windows':
            require('CCACHE_DIR: ${{ github.workspace }}/build/.ccache' in section[1],
                    'Windows compiler cache is outside the ignored build directory')
            require('path: ${{ env.CCACHE_DIR }}' in section[1],'cache action does not use CCACHE_DIR')
    # A root cache also dirties the embedded identity. Keep genuine untracked
    # files detectable; relocate CI cache data rather than ignoring source dirt.
    cache_file=repo/'.ccache/result';cache_file.parent.mkdir();cache_file.write_text('authored cache\n')
    check(first+'-dirty')
    stage_core(repo,binary,first,failure='requires an exact clean source checkout')
    cache_file.unlink();cache_file.parent.rmdir()
    cache_file=repo/'build/.ccache/result';cache_file.parent.mkdir(parents=True);cache_file.write_text('authored cache\n')
    check(first)
    stage_core(repo,binary,run('git','rev-parse','HEAD',cwd=repo))
    plain_env={key:value for key,value in os.environ.items()
               if not re.fullmatch(r'GIT_CONFIG_(COUNT|KEY_\d+|VALUE_\d+)',key)}
    ci_env={**plain_env,**policy}
    converted=Path(directory)/'crlf-checkout'
    run('git','clone','-q','--no-local','--config','core.autocrlf=true',str(repo),str(converted),cwd=repo,env=plain_env)
    require((converted/'tracked').read_bytes()==b'one\r\n','CRLF checkout was not simulated')
    require(run('git','status','--porcelain',cwd=converted,env=ci_env),'converted checkout falsely appeared clean')
    stage_core(converted,binary,first,ci_env,failure='requires an exact clean source checkout')
    preserved=Path(directory)/'ci-checkout'
    run('git','clone','-q','--no-local','--config','core.autocrlf=true',str(repo),str(preserved),cwd=repo,env=ci_env)
    require((preserved/'tracked').read_bytes()==b'one\n','CI converted committed LF bytes')
    require((preserved/'raw-crlf.c').read_bytes()==raw_crlf,'CI changed a committed raw CRLF blob')
    require(not run('git','status','--porcelain',cwd=preserved,env=ci_env),'CI checkout is falsely dirty')
    clone_build=preserved/'build/probe'
    run(cmake,'-S',str(preserved),'-B',str(clone_build),'-G',generator,
        '-DCMAKE_C_COMPILER='+compiler,'-DCMAKE_MAKE_PROGRAM='+make,cwd=preserved,env=ci_env)
    run(cmake,'--build',str(clone_build),'--target','probe',cwd=preserved,env=ci_env)
    clone_binary=clone_build/binary.name
    require('commit='+first+' target=' in run(str(clone_binary),cwd=preserved,env=ci_env),
            'CI checkout embedded a dirty or unrelated identity')
    stage_windows_order(preserved,clone_binary,ci_env)
    stage_core(preserved,clone_binary,run('git','rev-parse','HEAD',cwd=preserved,env=ci_env),ci_env)
    (preserved/'tracked').write_bytes(b'genuine CRLF content edit\r\n')
    stage_core(preserved,clone_binary,first,ci_env,failure='requires an exact clean source checkout')

    # Archives must not accidentally discover an enclosing checkout.
    (repo / '.git').rename(Path(directory) / 'git-metadata')
    check('stale-override')
    run(cmake, '-DARC_IDENTITY_SOURCE=' + str(repo), '-DARC_IDENTITY_REVISION=',
        '-DARC_IDENTITY_TEMPLATE=' + str(repo / 'libretro/arc_build_identity.h.in'),
        '-DARC_IDENTITY_OUTPUT=' + str(header), '-P',
        str(repo / 'cmake/update-build-identity.cmake'), cwd=repo)
    require('"unknown"' in header.read_text(), 'archive without revision')
print('build identity: incremental, merge-ref/push/nightly staging, CRLF checkout policy, dirty/unrelated refusal and archive passed')
