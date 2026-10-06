#!/bin/sh
set -eu
binary=$1
expected=25
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$(uname -s)" in
  Darwin)
    symbols=$(nm -gU -j "$binary" | sed 's/^_//')
    undefined=$(nm -u "$binary")
    ;;
  *)
    symbols=$(nm -D --defined-only --format=posix "$binary" | awk '{print $1}')
    undefined=$(nm -D --undefined-only "$binary")
    ;;
esac
printf '%s\n' "$symbols" | sh "$script_dir/check-export-names.sh"
if printf '%s\n' "$undefined" | grep -E '(^|[[:space:]])_?getenv([[:space:]]|$)' >/dev/null; then
  printf '%s\n' 'shipping binary guard: unexpected getenv import' >&2
  exit 1
fi
printf 'export guard: exact %s accepted symbols; no getenv import\n' "$expected"
