#!/bin/sh
set -eu
binary=$1
expected=25
case "$(uname -s)" in
  Darwin)
    symbols=$(nm -gU "$binary" | awk '$2 == "T" {sub(/^_/, "", $3); if ($3 ~ /^retro_/) print $3}')
    undefined=$(nm -u "$binary")
    ;;
  *)
    symbols=$(nm -D --defined-only "$binary" | awk '$2 == "T" && $3 ~ /^retro_/ {print $3}')
    undefined=$(nm -D --undefined-only "$binary")
    ;;
esac
count=$(printf '%s\n' "$symbols" | sed '/^$/d' | wc -l | tr -d ' ')
[ "$count" = "$expected" ]
case "$(uname -s)" in
  Darwin) all=$(nm -gU "$binary" | awk '$2 == "T" {print $3}') ;;
  *) all=$(nm -D --defined-only "$binary" | awk '$2 == "T" {print $3}') ;;
esac
[ "$(printf '%s\n' "$all" | sed '/^$/d' | wc -l | tr -d ' ')" = "$expected" ]
if printf '%s\n' "$undefined" | grep -E '(^|[[:space:]])_?getenv([[:space:]]|$)' >/dev/null; then
  printf '%s\n' 'shipping binary guard: unexpected getenv import' >&2
  exit 1
fi
printf 'export guard: exactly %s retro_* symbols; no getenv import\n' "$expected"
