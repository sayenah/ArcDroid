#!/usr/bin/env python3
"""AC-1: shipping input sources cannot retain the removed selection path."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
assert not (root / "core/input_profiles.c").exists()
for name in ("core/input_mapping.c", "core/arcdroid_core.h", "libretro/arcdroid_libretro.c"):
    source = (root / name).read_text()
    for retired in (
        "input_profile", "InputProfile", "MAPPING_SOURCE_PROFILE",
        "MAPPING_SOURCE_PRESET", "fde0f540e0d5732646c1eebb54629a162f40db91",
        "elite",
    ):
        assert retired not in source, (name, retired)
print("shipping input sources: retired selection absent")
