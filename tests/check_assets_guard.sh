#!/bin/sh
# ArcDroid asset-guard regression. GPL-2.0-only.
set -eu

guard=$1
guard_tmp=$(mktemp -d "${TMPDIR:-/tmp}/arcdroid-asset-guard.XXXXXX")
trap 'rm -rf "$guard_tmp"' EXIT HUP INT TERM

cd "$guard_tmp"
git init -q
printf 'ordinary source text\n' > source.txt
git add source.txt
"$guard" >/dev/null

printf '\000ArcDroid synthetic binary\001' > payload
git add payload
if output=$("$guard" 2>&1); then
  printf 'asset guard accepted an extensionless tracked binary\n' >&2
  exit 1
fi
case "$output" in
  *"forbidden tracked binary: payload"*) ;;
  *) printf 'asset guard failed without identifying payload: %s\n' "$output" >&2; exit 1 ;;
esac
printf 'asset guard self-test: extensionless binary rejected\n'
