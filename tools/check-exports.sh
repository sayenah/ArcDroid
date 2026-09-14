#!/bin/sh
set -eu
binary=$1
expected=25
case "$(uname -s)" in
  Darwin) symbols=$(nm -gU "$binary" | awk '$2 == "T" {sub(/^_/, "", $3); if ($3 ~ /^retro_/) print $3}') ;;
  *) symbols=$(nm -D --defined-only "$binary" | awk '$2 == "T" && $3 ~ /^retro_/ {print $3}') ;;
esac
count=$(printf '%s\n' "$symbols" | sed '/^$/d' | wc -l | tr -d ' ')
[ "$count" = "$expected" ]
case "$(uname -s)" in
  Darwin) all=$(nm -gU "$binary" | awk '$2 == "T" {print $3}') ;;
  *) all=$(nm -D --defined-only "$binary" | awk '$2 == "T" {print $3}') ;;
esac
[ "$(printf '%s\n' "$all" | sed '/^$/d' | wc -l | tr -d ' ')" = "$expected" ]
printf 'export guard: exactly %s retro_* symbols\n' "$expected"
