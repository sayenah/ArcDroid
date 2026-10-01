#!/usr/bin/env python3
"""AC-1: shipping input sources cannot retain the removed selection path."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
if (root / "core/input_profiles.c").exists():
    print("retired input profile source exists", file=sys.stderr)
    sys.exit(1)
for name in ("core/input_mapping.c", "core/arcdroid_core.h", "libretro/arcdroid_libretro.c"):
    source = (root / name).read_text()
    for retired in (
        "input_profile", "InputProfile", "MAPPING_SOURCE_PROFILE",
        "MAPPING_SOURCE_PRESET", "fde0f540e0d5732646c1eebb54629a162f40db91",
    ):
        if retired in source:
            print(f"{name}: retired token {retired}", file=sys.stderr)
            sys.exit(1)
    if "elite" in source.casefold():
        print(f"{name}: retired selection name", file=sys.stderr)
        sys.exit(1)
print("shipping input sources: retired selection absent")
