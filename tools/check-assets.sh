#!/bin/sh
set -eu
failed=0
git ls-files | while IFS= read -r path; do
  lower=$(printf '%s' "$path" | tr '[:upper:]' '[:lower:]')
  case "$lower" in
    *.rom|*.bin|*.hdf|*.adf|*.apd|*.jfd|*.hfe|*.wav)
      case "$path" in
        engine/arculator/roms/arcrom_ext|engine/arculator/cmos/arthur/cmos.bin|engine/arculator/cmos/riscos2/cmos.bin|engine/arculator/cmos/riscos3_old/cmos.bin|engine/arculator/cmos/riscos3_new/cmos.bin|tests/fixtures/generated/*) ;;
        *) printf 'forbidden tracked binary: %s\n' "$path" >&2; failed=1 ;;
      esac
      ;;
  esac
  [ "$failed" -eq 0 ] || exit 1
done
printf 'asset guard: tracked binary allowlist satisfied\n'
