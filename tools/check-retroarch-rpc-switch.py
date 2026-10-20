#!/usr/bin/env python3
"""Classify only the native frontend's exact LOAD_CONTENT rejection.
SPDX-License-Identifier: GPL-2.0-only
"""
import argparse
import ntpath
from pathlib import Path
import re
import sys

CORE_LOAD = re.compile(r'\[INFO\] \[Core\] Loading dynamic libretro core from: "([^"]+)"\.')


def classify(lines, after_lines, command, core, working_directory=None):
    if not command.startswith('LOAD_CONTENT ') or not 0 <= after_lines <= len(lines):
        raise ValueError('Invalid switch command or log boundary')
    # RA may log the relative --libretro spelling verbatim. Both child
    # processes inherit the script's working directory; compare full paths.
    working_directory = str(Path.cwd()) if working_directory is None else working_directory
    def identity(path):
        return ntpath.normcase(ntpath.normpath(ntpath.join(working_directory, path)))
    expected = identity(core)
    initial = [m[1] for line in lines[:after_lines] if (m := CORE_LOAD.fullmatch(line))]
    if not initial or identity(initial[0]) != expected:
        raise ValueError('Ordinary switch started with the wrong core')
    tail = lines[after_lines:]
    loads = [m[1] for line in tail if (m := CORE_LOAD.fullmatch(line))]
    if any(identity(path) != expected for path in loads):
        raise ValueError('LOAD_CONTENT selected the wrong core')
    if any('Failed to load content' in line for line in tail):
        raise ValueError('LOAD_CONTENT failed to load content')
    rejection = f'[WARN] Unrecognized command "{command}" received.'
    if rejection in tail:
        if loads or any('Firmware: ArcDroid test ROM' in line for line in tail):
            raise ValueError('Command rejection conflicts with a content load')
        return 'unsupported'
    # Missing, partial, stale, foreign-command or core-output rejections never
    # permit an exception. The caller must complete the strict switch checks.
    return 'strict'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--after-lines', type=int, required=True)
    parser.add_argument('--command', required=True)
    parser.add_argument('--core', required=True)
    args = parser.parse_args()
    try:
        lines = args.log.read_text(encoding='utf-8-sig').splitlines()
        print(classify(lines, args.after_lines, args.command, args.core))
    except (OSError, ValueError) as error:
        print(f'Ordinary switch refused: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
