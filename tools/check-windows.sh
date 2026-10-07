#!/bin/sh
set -eu
binary=$1
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
objdump_bin=x86_64-w64-mingw32-objdump
command -v "$objdump_bin" >/dev/null 2>&1 || objdump_bin=objdump
exports=$($objdump_bin -p "$binary" | awk '
  /\[Ordinal\/Name Pointer\] Table/ {in_exports = 1; next}
  in_exports && /^[[:space:]]*\[[[:space:]]*[0-9]+\][[:space:]]/ {
    print $NF; next
  }
  in_exports && /^The / {exit}
')
printf '%s\n' "$exports" | sh "$script_dir/check-export-names.sh"
$objdump_bin -p "$binary" | awk '/DLL Name:/ {print $3}' | while IFS= read -r dll; do
  case "$dll" in KERNEL32.dll|api-ms-win-crt-*) ;; *) printf 'unexpected DLL import: %s\n' "$dll" >&2; exit 1 ;; esac
done
if $objdump_bin -p "$binary" | grep -E '(^|[[:space:]])_?getenv([[:space:]]|$)' >/dev/null; then
  printf '%s\n' 'Windows guard: unexpected getenv import in shipping core' >&2
  exit 1
fi
printf 'Windows guard: exact 25 accepted PE exports; imports limited to KERNEL32 and UCRT API sets; no getenv import\n'
