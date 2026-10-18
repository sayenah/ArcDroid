#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Drive real libretro entry points; independent executable proves exclusion."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

loader, core, process, mutant = sys.argv[1:]
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


def plan_file(base, name, root, legs):
    plan, output = base / (name + '.arcprobe'), base / (name + '.log')
    plan.write_text('arc-lock-probe-v1\nroot=' + str(root) + '\noutput=' + str(output) +
                    ''.join('\nleg=' + leg for leg in legs) + '\n')
    return plan, output


def attempt(root, expected):
    reply = subprocess.run([process, 'try', str(root)], capture_output=True, text=True, timeout=5)
    require(reply.returncode == (10 if expected == 'contended' else 0) and
            reply.stdout.strip() == expected,
            'independent try expected ' + expected + ': ' + reply.stdout + reply.stderr)


def two_process(base, binary, name):
    root = base / name
    root.mkdir()
    plan, output = plan_file(base, name, root, ['hold'])
    session = Session(binary, plan)
    try:
        session.command('f')
        require('result leg=hold outcome=held attempts=1\n' in output.read_text(), 'hold outcome')
        attempt(root, 'contended')
        # No graceful cleanup. Kill and reap, then exactly one fresh attempt.
        session.child.kill()
        session.child.wait(timeout=5)
        attempt(root, 'held')
        require('done leg=hold' not in output.read_text(), 'death unexpectedly ran cleanup')
    finally:
        session.close()


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
            require('verify leg=' + leg + ' role=parent outcome=held\n' in text, 'parent survives ' + leg)
        if os.name != 'nt':
            require('result leg=fork-contend role=child pid=' in text and
                    'outcome=contended attempts=1\n' in text.split('role=child pid=')[1], 'fork child')
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

    # Refused plans perform no output write and create no missing root/ancestor.
    invalid = [('', 'leg=try'), ('relative', 'leg=try'),
               (str(base / 'missing' / 'save'), 'leg=try'),
               (str(root / '..' / 'escape'), 'leg=try'),
               (str(root), 'leg=unknown'), (str(root), 'leg=hold\nleg=try')]
    for index, (save, entries) in enumerate(invalid):
        plan, output = plan_file(base, 'bad' + str(index), root, ['try'])
        plan.write_text('arc-lock-probe-v1\nroot=' + save + '\noutput=' + str(output) + '\n' + entries + '\n')
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
    malformed = [valid + b'unknown=value\n', valid + b'root=\n', valid + b'output=\n',
                 valid + b'\0', valid.replace(b'v1', b'v2'), b'x' * 16385,
                 valid + b'leg=try\n' * 16]
    for data in malformed:
        plan.write_bytes(data)
        reply = subprocess.run([loader, core, str(plan)], capture_output=True, text=True, timeout=5)
        require(reply.returncode == 3 and not output.exists() and not reply.stderr, 'malformed plan accepted')
    plan.write_bytes(valid.replace(b'\n', b'\r\n'))
    session = Session(core, plan)
    try:
        session.command('f')
        require('result leg=try outcome=held attempts=1\n' in output.read_text(), 'CRLF')
        session.command('q', 'quit')
        require(session.child.wait(timeout=5) == 0, 'CRLF quit')
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
        protected.write_text('preserve')
        plan, output = plan_file(base, 'redirect-output', root, ['try'])
        output.symlink_to(protected)
        reply = subprocess.run([loader, core, str(plan)], capture_output=True, text=True, timeout=5)
        require(reply.returncode == 3 and protected.read_text() == 'preserve', 'output symlink clobbered')
        output.unlink()
        os.link(protected, output)
        reply = subprocess.run([loader, core, str(plan)], capture_output=True, text=True, timeout=5)
        require(reply.returncode == 3 and protected.read_text() == 'preserve', 'output hard link clobbered')
    sentinel = root / 'arcdroid' / 'locks-riscpc' / ('c-' + KEY + '.lock')
    require(sentinel.stat().st_size == 0, 'sentinel carried data')
    require({p.relative_to(root).as_posix() for p in root.rglob('*')} == {
        'arcdroid', 'arcdroid/locks-riscpc', 'arcdroid/locks-riscpc/c-' + KEY + '.lock',
        *({'p10b-flock.probe'} if os.name != 'nt' else set())}, 'unexpected root writes')
    two_process(base, core, 'death')
    try:
        two_process(base, mutant, 'mutant')
    except RuntimeError as error:
        require('independent try expected contended' in str(error), 'mutant failed for unrelated reason: ' + str(error))
    else:
        raise RuntimeError('skip-native-lock mutant survived independent process test')
print('P10b core: all legs, lifecycle, identity loss, confinement, process death/first attempt; skip-lock mutant killed')
