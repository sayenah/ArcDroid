#!/usr/bin/env python3
"""Verify the ordinary leg's options at the frontend/core callback boundary.
SPDX-License-Identifier: GPL-2.0-only
"""
import argparse
from pathlib import Path
import re
import sys

EXPECTED = dict.fromkeys(('arcdroid_machine', 'arcdroid_cpu', 'arcdroid_os'), 'Auto')
QUERY = re.compile(r'^\[(?:DEBUG|ERROR)\] \[Environ\] GET_VARIABLE: (arcdroid_\w+)(?:\s|$)')
READ = re.compile(r'^\[DEBUG\] \[Environ\] GET_VARIABLE: (arcdroid_\w+) = "([^"]*)"$')


def verify(lines):
    counts = dict.fromkeys(EXPECTED, 0)
    for line in lines:
        query = QUERY.match(line)
        if query and query[1] in EXPECTED:
            match = READ.fullmatch(line)
            if not match:
                raise ValueError('Failed or malformed option callback: ' + line)
            key, value = match.groups()
            if value != EXPECTED[key]:
                raise ValueError(f'Core received {key}={value!r}; expected {EXPECTED[key]!r}')
            counts[key] += 1
    missing = [key for key, count in counts.items() if not count]
    if missing:
        raise ValueError('No frontend GET_VARIABLE evidence for ' + ', '.join(missing))
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    try:
        counts = verify(args.log.read_text(encoding='utf-8-sig').splitlines())
        print('Core options received: ' + '; '.join(
            f'{key}={EXPECTED[key]} reads={count}' for key, count in counts.items()) + ' PASS')
    except (OSError, ValueError) as error:
        print(f'Ordinary options refused: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
