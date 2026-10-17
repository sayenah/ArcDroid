#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
binary=$1
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
readelf_bin=${LLVM_READELF:-llvm-readelf}
exports=$($readelf_bin --dyn-syms --wide "$binary" |
  awk '($5 == "GLOBAL" || $5 == "WEAK") && $6 == "DEFAULT" && $7 != "UND" {
    sub(/@.*/, "", $8); print $8
  }')
printf '%s\n' "$exports" | sh "$script_dir/check-export-names.sh"
$readelf_bin -dW "$binary" | awk '/NEEDED/ {gsub(/[][]/, "", $5); print $5}' | while IFS= read -r lib; do
  case "$lib" in libc.so|libm.so|libdl.so|libz.so) ;; *) printf 'unexpected Android dependency: %s\n' "$lib" >&2; exit 1 ;; esac
done
$readelf_bin -lW "$binary" | awk '/ LOAD / {if ($NF != "0x4000") exit 1}'
if $readelf_bin -Ws "$binary" | grep -E '[[:space:]]_?getenv(@[^[:space:]]*)?$' >/dev/null; then
  printf '%s\n' 'Android guard: unexpected getenv import in shipping core' >&2
  exit 1
fi
printf 'Android guard: exact 25 accepted dynamic exports; allowed NEEDED entries; LOAD alignment 0x4000; no getenv import\n'
