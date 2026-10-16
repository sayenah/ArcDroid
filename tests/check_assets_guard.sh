#!/bin/sh
# ArcDroid asset-guard regression. GPL-2.0-only.
set -eu
unset GIT_INDEX_FILE

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
git rm --cached -q payload
rm payload

printf 'synthetic archive name only\n' > payload.zip
git add payload.zip
if output=$("$guard" 2>&1); then
  printf 'asset guard accepted a tracked zip by extension\n' >&2
  exit 1
fi
case "$output" in
  *"forbidden tracked binary: payload.zip"*) ;;
  *) printf 'asset guard failed without identifying payload.zip: %s\n' "$output" >&2; exit 1 ;;
esac
printf 'asset guard self-test: extensionless binary and zip extension rejected\n'
git rm --cached -q payload.zip
rm payload.zip
mkdir -p engine/arculator/roms tests/fixtures/generated
for asset in engine/arculator/roms/arcrom_ext tests/fixtures/generated/unlisted.bin; do
  printf '\000ArcDroid synthetic binary\001' > "$asset"
  git add "$asset"
  if "$guard" >/dev/null 2>&1; then
    printf 'asset guard accepted a retired or unlisted generated binary\n' >&2
    exit 1
  fi
  git rm --cached -q "$asset"
  rm "$asset"
done
printf 'asset guard self-test: retired support ROM and tracked generated binary rejected\n'
