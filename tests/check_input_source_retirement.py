#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""AC-1: shipping input sources cannot retain the removed selection path."""
import hashlib
from pathlib import Path
import re
import sys

# Case-folded identifier digest from the original retired-name assertion.
RETIRED_NAME_SHA256 = "1a80b0691b696fe18dbc9e94722bc7bb2325d76078bda9fdd0c1760bd4cd9864"


def contains_retired_name(source):
    return any(hashlib.sha256(identifier.casefold().encode()).hexdigest() == RETIRED_NAME_SHA256
               for identifier in re.findall(r"[A-Za-z_][A-Za-z_0-9]*", source))


def main(root):
    if (root / "core/input_profiles.c").exists():
        print("retired input profile source exists", file=sys.stderr)
        sys.exit(1)
    for name in ("core/input_mapping.c", "core/arcdroid_core.h", "libretro/arcdroid_libretro.c"):
        source = (root / name).read_text()
        for retired in (
            "input_profile", "InputProfile", "MAPPING_SOURCE_PROFILE",
            "MAPPING_SOURCE_PRESET",
        ):
            if retired in source:
                print(f"{name}: retired token {retired}", file=sys.stderr)
                sys.exit(1)
        if contains_retired_name(source):
            print(f"{name}: retired selection name", file=sys.stderr)
            sys.exit(1)
    print("shipping input sources: retired selection absent")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
