#!/usr/bin/env python3
"""Check all source/configured core metadata for RetroArch 1.22.2. GPL-2.0-only.

command.c:command_get_status copies system_id without a NULL check.
core_info.c:core_info_parse_config_file leaves missing/empty strings NULL.
menu/menu_displaylist.c:menu_displaylist_parse_core_info guards the other five
identity fields below, but require complete identity in every ArcDroid .info.
Sources: https://github.com/libretro/RetroArch/tree/v1.22.2
"""
from pathlib import Path
import re
import sys

REQUIRED = ("systemid", "display_name", "corename", "display_version",
            "systemname", "manufacturer")
ASSIGNMENT = re.compile(r'\s*([a-zA-Z0-9_]+)\s*=\s*"((?:[^"\\]|\\.)*)"\s*(?:#.*)?')


def verify(text, name):
    values = {}
    for number, line in enumerate(text.splitlines(), 1):
        match = ASSIGNMENT.fullmatch(line)
        if match and match[1] in REQUIRED:
            key, value = match.groups()
            if key in values:
                raise ValueError(f"{name}:{number}: duplicate {key}")
            values[key] = value
    for key in REQUIRED:
        if not values.get(key, "").strip():
            raise ValueError(f"{name}: missing, empty or malformed {key}")


def main():
    root, build = (Path(p).resolve() for p in sys.argv[1:3])
    files = [Path(p).resolve() for p in sys.argv[3:]]
    if not files or len(files) % 2:
        raise ValueError("expected configured source/output .info pairs")
    sources = set(root.glob("*.info")) | set((root / "tests").rglob("*.info"))
    outputs = set(build.glob("*.info"))
    registered_sources, registered_outputs = set(files[::2]), set(files[1::2])
    if sources - registered_sources:
        # Test-only source metadata also gets checked in shipping-only builds.
        for path in sorted(sources - registered_sources):
            verify(path.read_text(encoding="utf-8"), path)
    if outputs != registered_outputs:
        raise ValueError(f"unregistered/missing configured .info: {outputs ^ registered_outputs}")
    for source, output in zip(files[::2], files[1::2]):
        text = source.read_text(encoding="utf-8")
        verify(text, source)
        verify(output.read_text(encoding="utf-8"), output)
        if output.read_bytes() != source.read_bytes():
            raise ValueError(f"{output}: differs from {source}")
    # Every required identity field is independently removed and emptied;
    # a comment or the internal C struct spelling cannot supply systemid.
    valid = '\n'.join(f'{key} = "value"' for key in REQUIRED)
    verify(valid.replace('\n', '\r\n'), "CRLF metadata")
    for key in REQUIRED:
        for replacement in ("", f'{key} = ""', f'# {key} = "value"'):
            candidate = valid.replace(f'{key} = "value"', replacement)
            try:
                verify(candidate, "negative fixture")
            except ValueError as error:
                if key not in str(error):
                    raise
            else:
                raise ValueError(f"guard accepted invalid {key}")
    try:
        verify(valid.replace('systemid =', 'system_id ='), "internal field spelling")
    except ValueError:
        pass
    else:
        raise ValueError("guard accepted system_id instead of systemid")
    print(f"rpc-core-info: {len(sources)} sources, {len(outputs)} configured files; "
          "19 missing/empty/commented/wrong-key cases refused; CRLF accepted")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print(f"rpc-core-info: {error}", file=sys.stderr)
        sys.exit(1)
