#!/bin/sh
set -eu
binary=$1
count=$(x86_64-w64-mingw32-nm -g --defined-only "$binary" | awk '$2 == "T" && $3 ~ /^retro_/ {print $3}' | wc -l | tr -d ' ')
[ "$count" = 25 ]
x86_64-w64-mingw32-objdump -p "$binary" | awk '/DLL Name:/ {print $3}' | while IFS= read -r dll; do
  case "$dll" in KERNEL32.dll|api-ms-win-crt-*) ;; *) printf 'unexpected DLL import: %s\n' "$dll" >&2; exit 1 ;; esac
done
printf 'Windows guard: 25 exports; imports limited to KERNEL32 and UCRT API sets\n'
