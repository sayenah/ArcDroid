#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Reject private-location shapes without printing their values.

Scan tracked working-tree bytes, including binary strings; ignore deleted files.
The opt-in untracked mode checks a pending implementation before it is staged.
Allowlist entries bind public syntax to paths, classes and review reasons.
This is a shape guard, not a substitute for reviewing public text for privacy.
"""
import argparse
from fnmatch import fnmatchcase
import json
from pathlib import Path
import re
import subprocess
import sys

OCTET = r"(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)"
PATH_END = r"""(?=[/\s.`'")\],:;]|$)"""
HOST_LABEL = r"[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?"
URL = re.compile(
    r"(?<![\w+.-])https?://" + HOST_LABEL + r"(?:\." + HOST_LABEL + r")+(?::[0-9]+)?"
    r"""(?=$|[/?#\s<>"'\x60(),;|])(?:[/?#][^\s<>"'\x60,;|]*)?""", re.I)
PATTERNS = {
    "private-ipv4": re.compile(r"(?<![\w.])(?:10\." + OCTET + r"|172\.(?:1[6-9]|2\d|3[01])|192\.168)\."
                               + OCTET + r"\." + OCTET + r"(?!\w|\.\d)"),
    "mac-user-path": re.compile(r"/Users/[\w. -]+" + PATH_END),
    "windows-user-path": re.compile(r"[a-z]:[\\/]+Users(?:[\\/]+|" + PATH_END + r")", re.I),
    "android-volume": re.compile(r"/(?:storage|mnt/media_rw)/[a-f0-9]{4}-[a-f0-9]{4}" + PATH_END, re.I),
}


def tracked(root, include_untracked=False):
    command = ["git", "ls-files", "-z", "--cached"]
    if include_untracked:
        command += ["--others", "--exclude-standard"]
    return sorted(set(filter(None, subprocess.check_output(command, cwd=root).decode().split("\0"))))


def url_spans(text):
    for url in URL.finditer(text):
        end = url.end()
        if url.start() and text[url.start() - 1] == "(":
            # Close a citation without treating parentheses within its URL as delimiters.
            depth = 0
            for offset, char in enumerate(url.group()):
                if char == "(":
                    depth += 1
                elif char == ")":
                    if not depth:
                        end = url.start() + offset
                        break
                    depth -= 1
        yield url.start(), end


def scan(name, text, allowlist):
    failures = []
    urls = list(url_spans(text))
    for kind, pattern in PATTERNS.items():
        for match in pattern.finditer(text):
            if kind == "mac-user-path" and any(start <= match.start() < end for start, end in urls):
                continue
            if any(entry["class"] == kind
                   and any(fnmatchcase(name, path) for path in entry["paths"])
                   and any(context.span("token") == match.span()
                           for context in re.finditer(entry["context"], text))
                   for entry in allowlist):
                continue
            failures.append((name, text.count("\n", 0, match.start()) + 1, kind))
    return failures


def verify(root, include_untracked=False):
    entries = json.loads((root / "tools/public-text-allowlist.json").read_text())["entries"]
    for entry in entries:
        if (set(entry) != {"paths", "class", "context", "reason"} or not entry["reason"].strip()
                or not entry["paths"] or entry["class"] not in PATTERNS
                or "token" not in re.compile(entry["context"]).groupindex):
            raise ValueError("invalid public-text allowlist entry")
    failures = []
    for name in tracked(root, include_untracked):
        path = root / name
        if path.is_file():
            failures.extend(scan(name, path.read_bytes().decode("utf-8", "replace"), entries))
            failures.extend(scan(name, name, []))
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--include-untracked", action="store_true")
    args = parser.parse_args()
    try:
        failures = verify(args.root, args.include_untracked)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError):
        print("public-text guard: scan or allowlist error", file=sys.stderr)
        return 1
    for name, line, kind in failures:
        # Never echo the matched token, source line or absolute checkout path.
        display = name
        for pattern in PATTERNS.values():
            display = pattern.sub("<redacted>", display)
        print(f"{display}:{line}: {kind}", file=sys.stderr)
    if failures:
        return 1
    print("public-text guard: tracked text passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
