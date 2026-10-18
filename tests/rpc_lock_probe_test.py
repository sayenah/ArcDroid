#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Drive real libretro entry points; independent executable proves exclusion."""
from pathlib import Path
import os
import re
import time
import subprocess
import sys
import tempfile

loader, core, process, *mutants = sys.argv[1:]
KEY = '0123456789abcdef0123456789abcdef01234567'


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


class Session:
    def __init__(self, binary, plan):
        self.child = subprocess.Popen([loader, binary, str(plan)], stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if self.child.stdout.readline().strip() != 'loaded':
            self.close()
            raise RuntimeError('load failed')

    def command(self, command, expected='frame'):
        self.child.stdin.write(command + '\n')
        self.child.stdin.flush()
        require(self.child.stdout.readline().strip() == expected, 'loader handshake: ' + command)

    def close(self):
        if self.child.poll() is None:
            self.child.kill()
        self.child.wait(timeout=5)
        error = self.child.stderr.read()
        self.child.stdin.close()
        self.child.stdout.close()
        self.child.stderr.close()
        require(not error, error)


def write_plan(path, text):
    # Exact UTF-8/LF fixture bytes, without Windows text-mode CRLF expansion.
    path.write_bytes(text.encode('utf-8'))


def plan_file(base, name, root, legs):
    plan, output = base / (name + '.arcprobe'), base / (name + '.log')
    write_plan(plan, 'arc-lock-probe-v1\nroot=' + str(root) + '\noutput=' + str(output) +
               ''.join('\nleg=' + leg for leg in legs) + '\n')
    return plan, output


# Log read_text() and subprocess text=True intentionally normalize CRLF to LF;
# their protocol comparisons below use '\n' on every platform. Plan input uses
# bytes so newline-specific parser tests do not inherit that normalization.
def process_attempt(root):
    return subprocess.run([process, 'try', str(root)], capture_output=True, text=True, timeout=5)


def attempt(root, expected):
    reply = process_attempt(root)
    require(reply.returncode == (10 if expected == 'contended' else 0) and
            reply.stdout.strip() == expected,
            'independent try expected ' + expected + ': ' + reply.stdout + reply.stderr)


def post_death(root, windows=(os.name == 'nt'), try_once=process_attempt,
               monotonic=time.monotonic_ns, pause=time.sleep, label='native'):
    start = monotonic()
    attempts = 0
    while True:
        reply = try_once(root)
        attempts += 1
        elapsed = monotonic() - start
        require(not windows or elapsed < 10_000_000_000,
                f'post-death acquisition deadline attempts={attempts} elapsed-ns={elapsed}')
        if reply.returncode == 0 and reply.stdout.strip() == 'held' and not reply.stderr:
            break
        require(windows and reply.returncode == 10 and reply.stdout.strip() == 'contended'
                and not reply.stderr and elapsed < 10_000_000_000,
                f'post-death acquisition failed attempts={attempts} elapsed-ns={elapsed}: ' + reply.stdout + reply.stderr)
        pause(0.001)
    require(windows or attempts == 1, 'POSIX first-attempt requirement')
    print(f'post-death source={label} policy={"Windows" if windows else "POSIX"} attempts={attempts} elapsed-ns={elapsed}', flush=True)
    return attempts, elapsed


def child_outcome(text, role):
    # Match the child's own line; a later contended result cannot rescue it.
    pattern = r'result leg=fork-contend role=' + re.escape(role) + r' pid=([0-9]+) outcome=contended attempts=1'
    matches = [re.fullmatch(pattern, line) for line in text.splitlines()]
    return [int(match[1]) for match in matches if match]


def check_fork(text):
    first, proof = child_outcome(text, 'child'), child_outcome(text, 'retained-proof')
    require(len(first) == 1, 'fork child own-line outcome')
    require(len(proof) == 1 and first[0] != proof[0], 'fork retained-proof outcome')
    for role in ('child', 'retained-proof'):
        require('pipe leg=fork-contend role=' + role + ' cloexec=1' in text, 'pipe close-on-exec')


def two_process(base, binary, name):
    root = base / name
    root.mkdir()
    plan, output = plan_file(base, name, root, ['hold'])
    session = Session(binary, plan)
    try:
        session.command('f')
        require('result leg=hold outcome=held attempts=1\n' in output.read_text(), 'hold outcome')
        attempt(root, 'contended')
        # No graceful cleanup. Reap, then first POSIX attempt / bounded Windows retry.
        session.child.kill()
        session.child.wait(timeout=5)
        post_death(root)
        require('done leg=hold' not in output.read_text(), 'death unexpectedly ran cleanup')
    finally:
        session.close()


# Exercise the Windows delay/deadline policy on every host; these are authored
# subprocess outcomes, separate from the real OS release test below.
def synthetic_release(outcomes, ticks, windows):
    replies = iter(outcomes)
    clock = iter(ticks)
    calls = []

    def try_once(root):
        calls.append(root)
        result = next(replies)
        return subprocess.CompletedProcess([], 0 if result == 'held' else 10, result + '\n', '')

    result = post_death('synthetic-release', windows, try_once, lambda: next(clock), lambda _: None, 'synthetic')
    return result, len(calls)


require(synthetic_release(['contended', 'held'], [0, 100, 200], True) == ((2, 200), 2), 'Windows delayed release')
for outcomes, ticks, windows, reason in (
    (['contended'], [0, 10_000_000_001], True, 'post-death acquisition deadline'),
    (['contended'], [0, 100], False, 'post-death acquisition failed'),
):
    try:
        synthetic_release(outcomes, ticks, windows)
    except RuntimeError as error:
        require(reason in str(error), 'wrong release-policy failure')
    else:
        raise RuntimeError('release-policy refusal missing')


with tempfile.TemporaryDirectory(prefix='arc-p10b-core-') as directory:
    base = Path(directory).resolve()
    info_root = base / 'info-only'
    info_root.mkdir()
    info_plan, info_output = plan_file(base, 'info-only', info_root, ['info'])
    info_session = Session(core, info_plan)
    try:
        info_session.command('f')
        for name in ('root', 'sentinel'):
            require('identity name=' + name + ' device=' in info_output.read_text(), 'standalone info ' + name)
        require(info_output.read_text().endswith('done leg=info\n'), 'standalone info done')
        attempt(info_root, 'held')
        info_session.command('q', 'quit')
        require(info_session.child.wait(timeout=5) == 0, 'info quit')
    finally:
        info_session.close()
    root = base / 'save'
    root.mkdir()
    legs = ['try', 'same-process', 'fork-contend', 'info', 'hold']
    plan, output = plan_file(base, 'all', root, legs)
    session = Session(core, plan)
    try:
        for _ in legs:
            session.command('f')
        text = output.read_text()
        for leg in legs[:-1]:
            require(text.count('done leg=' + leg + '\n') == 1, 'done ' + leg)
        require('result leg=try outcome=held attempts=1\n' in text, 'try')
        require('result leg=same-process role=second outcome=contended attempts=1\n' in text, 'same-process')
        for leg in ('same-process', 'fork-contend'):
            require('verify leg=' + leg + ' role=parent outcome=held\n' in text, 'parent identity ' + leg)
        if os.name != 'nt':
            check_fork(text)
            require('diagnostic-flock result=0 errno=0' in text, 'diagnostic flock')
            require('uid=' in text and 'kernel=' in text, 'identity info')
        else:
            require('result leg=fork-contend outcome=unavailable platform=Windows' in text, 'fork unavailable')
        for name in ('root', 'sentinel'):
            require('identity name=' + name + ' device=' in text, 'stat ' + name)
        if sys.platform.startswith('linux'):
            require('mountinfo ' in text, 'mountinfo')
        attempt(root, 'contended')
        session.command('r', 'reset')
        attempt(root, 'contended')
        # Repeated held/identity-lost results produce no duplicate lines.
        for _ in range(3):
            session.command('f')
        require(output.read_text() == text, 'unchanged verify logged repeatedly')
        session.command('u', 'unloaded')
        require(output.read_text().endswith('done leg=hold reason=unload\n'), 'unload completion')
        attempt(root, 'held')
        session.command('l', 'loaded')
        for _ in legs:
            session.command('f')
        session.command('d', 'deinit')
        require(output.read_text().endswith('done leg=hold reason=deinit\n'), 'deinit completion')
        require(session.child.poll() is None, 'deinit must be tested before process exit')
        attempt(root, 'held')
        session.command('q', 'quit')
        require(session.child.wait(timeout=5) == 0, 'deinit exit')
    finally:
        session.close()

    if os.name != 'nt':
        # Successful rename-over and unlink are performed by this independent
        # partner, never by the core. Verify loss is sticky, with no reacquire.
        for operation in ('replace', 'unlink'):
            plan, output = plan_file(base, operation, root, ['hold'])
            session = Session(core, plan)
            try:
                session.command('f')
                sentinel = root / 'arcdroid' / 'locks-riscpc' / ('c-' + KEY + '.lock')
                if operation == 'replace':
                    replacement = root / 'replacement'
                    replacement.touch()
                    replacement.replace(sentinel)
                else:
                    sentinel.unlink()
                session.command('f')
                require('verify leg=hold frame=2 outcome=identity-lost\n' in output.read_text(), operation)
                text = output.read_text()
                session.command('f')
                require(text == output.read_text(), 'sticky loss')
                attempt(root, 'held')
                session.command('q', 'quit')
                require(session.child.wait(timeout=5) == 0, 'quit')
            finally:
                session.close()
        alias = base / 'root-alias'
        alias.symlink_to(root, target_is_directory=True)
        plan, output = plan_file(base, 'alias', alias, ['hold'])
        session = Session(core, plan)
        try:
            session.command('f')
            attempt(root, 'contended')
            session.command('q', 'quit')
            require(session.child.wait(timeout=5) == 0, 'alias quit')
        finally:
            session.close()

    # In-app mutation partners share the app UID/mount view; no shell access is
    # needed. The parent verifies on the following two frames without reacquire.
    for operation in ('replace', 'delete'):
        mutation_root = base / ('in-app-' + operation)
        mutation_root.mkdir()
        plan, output = plan_file(base, 'in-app-' + operation, mutation_root, [operation])
        session = Session(core, plan)
        try:
            session.command('f')
            text = output.read_text()
            require('result leg=' + operation + ' role=parent outcome=held attempts=1' in text, 'mutation holder')
            operation_lines = [line for line in text.splitlines() if line.startswith('operation leg=' + operation + ' ')]
            require(len(operation_lines) == 1, 'mutation operation result')
            if os.name == 'nt':
                match = re.fullmatch(r'operation leg=' + operation + r' role=in-process result=-1 error=([1-9][0-9]*)', operation_lines[0])
                require(match is not None, 'Windows mutation must record actual refusal')
                expected, held = 'held', 1
            else:
                require(re.fullmatch(r'operation leg=' + operation + r' role=child pid=[0-9]+ result=0 errno=0', operation_lines[0]), 'fork mutation success')
                require('pipe leg=' + operation + ' role=child cloexec=1' in text, 'mutation pipe close-on-exec')
                expected, held = 'identity-lost', 0
            require('done leg=' + operation not in text, 'mutation must wait for next verify frame')
            session.command('f')
            require('verify leg=' + operation + ' phase=next frame=2 outcome=' + expected + ' held=' + str(held) + '\n' in output.read_text(), 'next mutation verify')
            named = mutation_root / 'arcdroid' / 'locks-riscpc' / ('c-' + KEY + '.lock')
            if os.name != 'nt' and operation == 'delete':
                require(not named.exists(), 'delete silently reacquired')
            else:
                require(named.stat().st_size == 0, 'replacement must be empty')
            attempt(mutation_root, 'contended' if os.name == 'nt' else 'held')
            session.command('f')
            text = output.read_text()
            require('verify leg=' + operation + ' phase=sticky frame=3 outcome=' + expected + ' held=' + str(held) + '\n' in text, 'sticky mutation verify')
            if os.name == 'nt':
                require('result leg=' + operation + ' role=retained-proof outcome=contended attempts=1\n' in text, 'Windows mutation retained lock')
            require(text.endswith('done leg=' + operation + '\n'), 'mutation done')
            session.command('f')
            require(output.read_text() == text, 'mutation ran again after completion')
            attempt(mutation_root, 'held')
            session.command('q', 'quit')
            require(session.child.wait(timeout=5) == 0, 'mutation quit')
        finally:
            session.close()

    # Refused plans perform no output write and create no missing root/ancestor.
    invalid = [('', 'leg=try'), ('relative', 'leg=try'),
               (str(base / 'missing' / 'save'), 'leg=try'),
               (str(root / '..' / 'escape'), 'leg=try'),
               (str(root), 'leg=unknown'), (str(root), 'leg=hold\nleg=try')]
    for index, (save, entries) in enumerate(invalid):
        plan, output = plan_file(base, 'bad' + str(index), root, ['try'])
        write_plan(plan, 'arc-lock-probe-v1\nroot=' + save + '\noutput=' + str(output) + '\n' + entries + '\n')
        reply = subprocess.run([loader, core, str(plan)], capture_output=True, text=True, timeout=5)
        require(reply.returncode == 3 and not output.exists() and not reply.stderr, 'invalid plan accepted')
    require(not (base / 'missing').exists() and not (base / 'escape').exists(), 'escaped creation')
    # The try leg observes contention from an independently held real lock.
    holder_plan, holder_output = plan_file(base, 'holder', root, ['hold'])
    holder = Session(core, holder_plan)
    try:
        holder.command('f')
        contender_plan, contender_output = plan_file(base, 'contender', root, ['try'])
        contender = Session(core, contender_plan)
        try:
            contender.command('f')
            require('result leg=try outcome=contended attempts=1\n' in contender_output.read_text(), 'core try contention')
            require(contender_output.read_text().endswith('done leg=try\n'), 'contended done')
            contender.command('q', 'quit')
            require(contender.child.wait(timeout=5) == 0, 'contender quit')
        finally:
            contender.close()
        attempt(root, 'contended')
        holder.command('q', 'quit')
        require(holder.child.wait(timeout=5) == 0, 'holder quit')
    finally:
        holder.close()

    plan, output = plan_file(base, 'parse', root, ['try'])
    valid = plan.read_bytes()
    require(b'\r' not in valid and valid.endswith(b'\n'), 'LF plan fixture bytes')
    malformed = [valid + b'unknown=value\n', valid + b'root=\n', valid + b'output=\n',
                 valid + b'\0', valid.replace(b'v1', b'v2'), b'x' * 16385,
                 valid + b'leg=try\n' * 16, valid.replace(b'\n', b'\r\r\n')]
    for data in malformed:
        plan.write_bytes(data)
        reply = subprocess.run([loader, core, str(plan)], capture_output=True, text=True, timeout=5)
        require(reply.returncode == 3 and not output.exists() and not reply.stderr, 'malformed plan accepted')
    for label, data in (('LF', valid), ('CRLF', valid.replace(b'\n', b'\r\n'))):
        plan.write_bytes(data)
        session = Session(core, plan)
        try:
            session.command('f')
            require('result leg=try outcome=held attempts=1\n' in output.read_text(), label)
            session.command('q', 'quit')
            require(session.child.wait(timeout=5) == 0, label + ' quit')
        finally:
            session.close()

    # Namespace redirects must never create/lock files in the outside fixture.
    if os.name != 'nt':
        outside, redirected = base / 'outside', base / 'redirected'
        outside.mkdir()
        redirected.mkdir()
        (redirected / 'arcdroid').symlink_to(outside, target_is_directory=True)
        plan, output = plan_file(base, 'redirect', redirected, ['try'])
        session = Session(core, plan)
        try:
            session.command('f')
            require('outcome=root-refused attempts=0' in output.read_text() and not list(outside.iterdir()), 'redirect escaped')
            session.command('q', 'quit')
            require(session.child.wait(timeout=5) == 0, 'redirect quit')
        finally:
            session.close()
        protected = outside / 'protected'
        protected.write_bytes(b'preserve')
        plan, output = plan_file(base, 'redirect-output', root, ['try'])
        output.symlink_to(protected)
        reply = subprocess.run([loader, core, str(plan)], capture_output=True, text=True, timeout=5)
        require(reply.returncode == 3 and protected.read_bytes() == b'preserve', 'output symlink clobbered')
        output.unlink()
        os.link(protected, output)
        reply = subprocess.run([loader, core, str(plan)], capture_output=True, text=True, timeout=5)
        require(reply.returncode == 3 and protected.read_bytes() == b'preserve', 'output hard link clobbered')
    # QA F-3 reproductions: resolve both existing files and not-yet-created
    # output parents. Reservation must survive case and directory aliases.
    def refuse_output(candidate):
        plan = base / 'reserved.arcprobe'
        write_plan(plan, 'arc-lock-probe-v1\nroot=' + str(root) + '\noutput=' + str(candidate) + '\nleg=try\n')
        existed = candidate.exists()
        before = candidate.read_bytes() if existed and candidate.is_file() else None
        reply = subprocess.run([loader, core, str(plan)], capture_output=True, text=True, timeout=5)
        require(reply.returncode == 3 and not reply.stderr, 'reserved output accepted: ' + str(candidate))
        if before is not None:
            require(candidate.read_bytes() == before, 'reserved file clobbered')
        elif not existed:
            require(not candidate.exists(), 'reserved output created')

    locks = root / 'arcdroid' / 'locks-riscpc'
    refuse_output(locks)
    case_locks = root / 'ARCDROID' / 'locks-riscpc'
    if case_locks.exists() and case_locks.samefile(locks):
        refuse_output(case_locks / ('c-' + KEY + '.lock'))
        refuse_output(case_locks / 'new-output.log')
        refuse_output(root / 'P10B-FLOCK.PROBE')
        print('case-variant output refusal: passed')
    else:
        print('case-variant output refusal: unqualified on case-sensitive host')
    if os.name != 'nt':
        alias = base / 'lock-directory-alias'
        alias.symlink_to(locks, target_is_directory=True)
        refuse_output(alias / ('c-' + KEY + '.lock'))
        refuse_output(alias / 'new-output.log')
        root_alias = base / 'root-output-alias'
        root_alias.symlink_to(root, target_is_directory=True)
        refuse_output(root_alias / 'p10b-flock.probe')
        # Missing diagnostic: resolve its parent and reserve its future leaf.
        empty = base / 'future-diagnostic'
        empty.mkdir()
        empty_alias = base / 'future-diagnostic-alias'
        empty_alias.symlink_to(empty, target_is_directory=True)
        plan = base / 'future-diagnostic.arcprobe'
        candidate = empty_alias / 'P10B-FLOCK.PROBE'
        write_plan(plan, 'arc-lock-probe-v1\nroot=' + str(empty) + '\noutput=' + str(candidate) + '\nleg=try\n')
        reply = subprocess.run([loader, core, str(plan)], capture_output=True, text=True, timeout=5)
        require(reply.returncode == 3 and not candidate.exists(), 'future diagnostic alias accepted')
    sentinel = root / 'arcdroid' / 'locks-riscpc' / ('c-' + KEY + '.lock')
    require(sentinel.stat().st_size == 0, 'sentinel carried data')
    require({p.relative_to(root).as_posix() for p in root.rglob('*')} == {
        'arcdroid', 'arcdroid/locks-riscpc', 'arcdroid/locks-riscpc/c-' + KEY + '.lock',
        *({'p10b-flock.probe'} if os.name != 'nt' else set())}, 'unexpected root writes')
    two_process(base, core, 'death')
    try:
        two_process(base, mutants[0], 'mutant')
    except RuntimeError as error:
        require('independent try expected contended' in str(error), 'mutant failed for unrelated reason: ' + str(error))
    else:
        raise RuntimeError('skip-native-lock mutant survived independent process test')
    if os.name != 'nt':
        plan, output = plan_file(base, 'child-unlock', root, ['fork-contend'])
        session = Session(mutants[1], plan)
        try:
            session.command('f')
            text = output.read_text()
            require(len(child_outcome(text, 'child')) == 1, 'mutant must first contend normally')
            try:
                check_fork(text)
            except RuntimeError as error:
                require(str(error) == 'fork retained-proof outcome', 'unrelated child-unlock failure')
            else:
                raise RuntimeError('QA inherited-unlock mutant survived')
            session.command('q', 'quit')
            require(session.child.wait(timeout=5) == 0, 'child-unlock quit')
        finally:
            session.close()
        require(not child_outcome('result leg=fork-contend role=child pid=1 outcome=held attempts=1\n'
                                  'later outcome=contended attempts=1\n', 'child'), 'unanchored child parser')
print('P10b core: all legs, lifecycle, identity, confinement, process death; skip-lock mutant killed; ' +
      ('inherited-unlock mutant killed' if os.name != 'nt' else 'inherited-unlock mutant unavailable on Windows'))
