#!/bin/sh
set -eu

sources='engine/arcdroid_engine.c
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
core
libretro/arcdroid_libretro.c'

if rg -n '\bfopen[[:space:]]*\(|\bdumpregs[[:space:]]*\(' $sources; then
  printf '%s\n' 'shipping I/O guard: forbidden CWD/debug file path found' >&2
  exit 1
fi
printf '%s\n' 'shipping I/O guard: no fopen or register-dump path outside the VFS bridge/harness'
