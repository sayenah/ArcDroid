#!/usr/bin/env python3
"""F4 raw RetroArch 1.22.2 log oracle, shared by Windows and Odin3.

SPDX-License-Identifier: GPL-2.0-only

Use an unmodified --verbose --log-file capture, English frontend language,
and repeat --sentinel for every authored path marker. No lines are discarded.
Android's file logger omits severity tags (verbosity.c:221-237); the same
classifier accepts its unprefixed environment lines and catalogue ID lines.
Unknown sentinel-bearing lines fail closed, including multiline continuations.
"""
import argparse
from pathlib import Path
import re
import sys

UPSTREAM = 'https://github.com/libretro/RetroArch/blob/v1.22.2/'
# runloop.c:831-867 supplies the callback tags; verbosity.c:204-333 writes
# them as the first tag, not as a nested [INFO] [libretro INFO] pair.
# runloop.c:1611-1651 echoes SET_MESSAGE always and EXT unless OSD-only.
CORE_CALLBACK = re.compile(r'^\[libretro (DEBUG|INFO|WARN|ERROR)\] ?(.*)$')
SEVERITY = re.compile(r'^\[(?:DEBUG|INFO|WARN|ERROR)\] ')
MESSAGE = re.compile(r'^\[Environ\] (SET_MESSAGE(?:_EXT)?): (.*)$')
ANDROID_CORE = re.compile(r'^(?:N-(?:[0-9]+|[AB])(?: [^:]*)?: |RPC privacy probe: )')

# Each is an entire frontend-authored line form, never a prefix exemption.
# English strings: intl/msg_hash_us.h MSG_LOADING_CONTENT_FILE and
# MSG_REDIRECTING_{SAVEFILE,SAVESTATE,CHEATFILE}_TO.
OWNED = (
    ('directory-query', r'\[Environ\] GET_(?:SYSTEM|SAVE)_DIRECTORY: "[^"]*"\.', 'runloop.c#L1825-L1839'),
    ('save-redirection', r'\[Override\] Redirecting (?:save file|save state|cheat file) to "[^"]*"\.', 'runloop.c#L7709-L7742'),
    ('content-loading', r'\[Content\] Loading content file: "[^"]*"\.', 'tasks/task_content.c#L626-L627'),
    ('content-selection', r'\[Core\] Using content: "[^"]*"\.', 'tasks/task_content.c#L1254-L1256'),
    ('runtime-log', r'\[Runtime\] Saving runtime log file: "[^"]*"\.', 'runtime_file.c#L1070'),
)
OWNED_PATTERNS = [(name, re.compile(pattern), source) for name, pattern, source in OWNED]


def classify(line):
    """Return form, payload, source; core-output recognition takes precedence."""
    callback = CORE_CALLBACK.fullmatch(line)
    if callback:
        return 'core-log-' + callback[1], callback[2], 'runloop.c#L831-L867'
    body = SEVERITY.sub('', line, count=1)
    message = MESSAGE.fullmatch(body)
    if message:
        return message[1], message[2], 'runloop.c#L1611-L1651'
    if ANDROID_CORE.match(body):
        return 'core-log-android', body, 'verbosity.c#L221-L237'
    for name, pattern, source in OWNED_PATTERNS:
        if pattern.fullmatch(body):
            return 'retroarch-' + name, body, source
    return 'unknown', body, ''


def check(lines, sentinels, notice, message_path, emit=print):
    errors = []
    live = False
    delivered = False
    echo = False
    seen = set()
    core_count = 0
    for number, line in enumerate(lines, 1):
        line = line.rstrip('\r\n')
        form, payload, source = classify(line)
        core = form.startswith('core-log-') or form in ('SET_MESSAGE', 'SET_MESSAGE_EXT')
        if core:
            core_count += 1
            # The actual catalogue text, not a frontend pathname or arbitrary
            # substring in a diagnostic, proves notice liveness.
            live |= payload in (notice, 'N-7: ' + notice)
            delivered |= payload == 'RPC privacy probe: ' + (
                'SET_MESSAGE_EXT' if message_path == 'extended' else 'SET_MESSAGE') + ' accepted'
            echo |= form == 'SET_MESSAGE' and payload == notice
        hits = [s for s in sentinels if s.casefold() in line.casefold()]
        seen.update(hits)
        if hits:
            emit(f'line {number} form={form} source={UPSTREAM + source if source else "unclassified"}: {line}')
            if core or not form.startswith('retroarch-'):
                errors.append(f'line {number}: sentinel in {form}')
    for sentinel in sentinels:
        if sentinel not in seen:
            errors.append(f'sentinel not observed in raw frontend log: {sentinel}')
    if not live:
        errors.append('authored catalogue notice absent from core-output forms')
    if not delivered:
        errors.append(f'{message_path} delivery acceptance absent from core-output forms')
    if message_path == 'legacy' and not echo:
        errors.append('legacy catalogue SET_MESSAGE echo absent')
    for error in errors:
        emit('FAIL: ' + error)
    if not errors:
        emit(f'PASS: {message_path}; {core_count} core-output lines; authored notice live; all sentinel lines classified')
    return not errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--sentinel', action='append', required=True)
    parser.add_argument('--notice', default='Checking the RiscPC hard disc')
    parser.add_argument('--message-path', choices=('extended', 'legacy'), required=True)
    args = parser.parse_args()
    if not args.notice or any(not s for s in args.sentinel):
        parser.error('notice and sentinels must be nonempty')
    with args.log.open(encoding='utf-8-sig', errors='strict') as log:
        return 0 if check(log, args.sentinel, args.notice, args.message_path) else 1


if __name__ == '__main__':
    sys.exit(main())
