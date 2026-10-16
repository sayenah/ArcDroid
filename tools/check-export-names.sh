#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Compare the complete defined export set, including data and weak symbols.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
expected=$(sed 's/^_//' "$script_dir/../libretro/arcdroid.exports" | LC_ALL=C sort)
actual=$(sed '/^[[:space:]]*$/d' | LC_ALL=C sort)
if [ "$actual" != "$expected" ]; then
  printf '%s\n' 'export guard: defined exports differ from the 25 accepted names' >&2
  printf 'expected:\n%s\nactual:\n%s\n' "$expected" "$actual" >&2
  exit 1
fi
