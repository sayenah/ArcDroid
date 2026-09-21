#!/bin/sh
set -eu

allowed_binary()
{
  case "$1" in
    engine/arculator/roms/arcrom_ext|engine/arculator/cmos/arthur/cmos.bin|engine/arculator/cmos/riscos2/cmos.bin|engine/arculator/cmos/riscos3_old/cmos.bin|engine/arculator/cmos/riscos3_new/cmos.bin|tests/fixtures/generated/*) return 0 ;;
    *) return 1 ;;
  esac
}

git ls-files | while IFS= read -r path; do
  lower=$(printf '%s' "$path" | tr '[:upper:]' '[:lower:]')
  case "$lower" in
    *.rom|*.bin|*.hdf|*.adf|*.apd|*.jfd|*.hfe|*.wav|*.zip)
      if ! allowed_binary "$path"; then
        printf 'forbidden tracked binary: %s\n' "$path" >&2
        exit 1
      fi
      ;;
  esac

  # Git's own binary heuristic catches extensionless and misleadingly named
  # assets. `git diff --numstat` reports binary files with two '-' fields.
  classification=$(git diff --no-index --numstat -- /dev/null "$path" 2>/dev/null || :)
  case "$classification" in
    "-	-	"*)
      if ! allowed_binary "$path"; then
        printf 'forbidden tracked binary: %s\n' "$path" >&2
        exit 1
      fi
      ;;
  esac
done
printf 'asset guard: tracked binary allowlist satisfied\n'
