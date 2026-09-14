#!/bin/sh
set -eu

command -v grep >/dev/null 2>&1 || {
  printf '%s\n' 'shipping I/O guard: required search tool grep is unavailable' >&2
  exit 2
}

sources='core/hash.c
core/input.c
core/firmware_table.c
core/launch.c
core/persistence.c
core/cmos_defaults.c
engine/arcdroid_engine.c
engine/arcdroid_stubs.c
engine/arculator/src/arm.c
engine/arculator/src/cmos.c
engine/arculator/src/cp15.c
engine/arculator/src/disc.c
engine/arculator/src/disc_adf.c
engine/arculator/src/ds2401.c
engine/arculator/src/fpa.c
engine/arculator/src/ioc.c
engine/arculator/src/ioeb.c
engine/arculator/src/joystick.c
engine/arculator/src/keyboard.c
engine/arculator/src/lc.c
engine/arculator/src/mem.c
engine/arculator/src/memc.c
engine/arculator/src/printer.c
engine/arculator/src/sound.c
engine/arculator/src/timer.c
engine/arculator/src/vidc.c
engine/arculator/src/wd1770.c
platform/arcdroid_input.c
platform/arcdroid_joystick.c
platform/arcdroid_sound.c
platform/arcdroid_video.c
libretro/arc_io.c
libretro/arcdroid_libretro.c'

scan_sources=${ARCDROID_SHIPPING_IO_SOURCES:-$sources}
if matches=$(grep -En '(^|[^[:alnum:]_])(fopen|dumpregs)[[:space:]]*\(' $scan_sources); then
  printf '%s\n' "$matches" >&2
  printf '%s\n' 'shipping I/O guard: forbidden CWD/debug file path found' >&2
  exit 1
else
  status=$?
  if [ "$status" -ne 1 ]; then
    printf 'shipping I/O guard: grep failed with status %s\n' "$status" >&2
    exit "$status"
  fi
fi

case "${1:-}" in
  '') ;;
  --scan-only) ;;
  --self-test)
    guard_tmp=$(mktemp -d "${TMPDIR:-/tmp}/arcdroid-shipping-io.XXXXXX")
    trap 'rm -rf "$guard_tmp"' EXIT HUP INT TERM
    printf '%s\n' '#include <stdio.h>' 'void injected(void) { (void)fopen("injected", "wb"); }' > "$guard_tmp/injected.c"
    if ARCDROID_SHIPPING_IO_SOURCES="$guard_tmp/injected.c" sh "$0" --scan-only >/dev/null 2>&1; then
      printf '%s\n' 'shipping I/O guard self-test: injected fopen was not rejected' >&2
      exit 1
    fi
    printf '%s\n' 'shipping I/O guard self-test: injected fopen rejected'
    ;;
  *)
    printf 'shipping I/O guard: unknown argument: %s\n' "$1" >&2
    exit 2
    ;;
esac
printf '%s\n' 'shipping I/O guard: no fopen or register-dump path outside the VFS bridge/harness'
