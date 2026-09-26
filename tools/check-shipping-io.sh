#!/bin/sh
set -eu

command -v grep >/dev/null 2>&1 || {
  printf '%s\n' 'shipping I/O guard: required search tool grep is unavailable' >&2
  exit 2
}

sources='core/adffs.c
core/install_disc.c
core/install_validate.c
core/hash.c
core/machine.c
core/input.c
core/input_mapping.c
core/input_profiles.c
core/osk.c
core/firmware_table.c
core/launch.c
core/launch_discovery.c
core/launch_disc.c
core/launch_chooser.c
core/folder_set.c
core/playlist.c
core/jfd.c
core/persistence.c
core/cmos_defaults.c
engine/arcdroid_engine.c
engine/arc_install_doorbell.c
engine/arcdroid_stubs.c
engine/arculator/src/82c711.c
engine/arculator/src/82c711_fdc.c
engine/arculator/src/arm.c
engine/arculator/src/cmos.c
engine/arculator/src/cp15.c
engine/arculator/src/disc.c
engine/arculator/src/disc_adf.c
engine/arculator/src/disc_jfd.c
engine/arculator/src/ds2401.c
engine/arculator/src/fpa.c
engine/arculator/src/ioc.c
engine/arculator/src/ioeb.c
engine/arculator/src/ide.c
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
libretro/install_store.c
libretro/jfd_store.c
libretro/arcdroid_libretro.c
core/*.h
engine/*.h
engine/compat/*.h
engine/arculator/src/*.h
platform/*.h
libretro/*.h'

if [ -z "${ARCDROID_SHIPPING_IO_SOURCES:-}" ]; then
  cmake_sources=$(awk '
    /^[[:space:]]*add_library\(arcdroid_core[[:space:]]+STATIC[[:space:]]*$/ { scan = 1; next }
    /^[[:space:]]*set\(ARCDROID_LIBRETRO_SOURCES[[:space:]]*$/ { scan = 1; next }
    scan && /^[[:space:]]*\)[[:space:]]*$/ { scan = 0; next }
    scan && $1 ~ /^(core|libretro|platform|engine)\/.*\.c$/ { print $1 }
  ' CMakeLists.txt | sort -u)
  if [ -z "$cmake_sources" ]; then
    printf '%s\n' 'shipping I/O guard: no shipping C sources parsed from CMakeLists.txt' >&2
    exit 1
  fi
  for source in $cmake_sources; do
    if ! printf '%s\n' "$sources" | grep -Fx "$source" >/dev/null; then
      printf 'shipping I/O guard: CMake shipping source is not scanned: %s\n' "$source" >&2
      exit 1
    fi
  done
fi

pattern='(^|[^[:alnum:]_>.])(fopen|open|_wfopen|gz[a-z]*|CreateFile[A-Za-z]*)[[:space:]]*\(|(^|[^[:alnum:]_])dumpregs[[:space:]]*\('
scan_sources=${ARCDROID_SHIPPING_IO_SOURCES:-$sources}
if all_matches=$(grep -En "$pattern" $scan_sources); then
  matches=$(printf '%s\n' "$all_matches" | grep -Ev '^engine/arculator/src/arc\.h:[0-9]+:[[:space:]]*\*[[:space:]]*dumpregs\(\), and its obsolete declaration was removed from this header\. \*/$' || true)
  if [ -n "$matches" ]; then
    printf '%s\n' "$matches" >&2
    printf '%s\n' 'shipping I/O guard: forbidden CWD/debug file path found' >&2
    exit 1
  fi
else
  status=$?
  if [ "$status" -ne 1 ]; then
    printf 'shipping I/O guard: grep failed with status %s\n' "$status" >&2
    exit "$status"
  fi
fi

# zlib is compiled with Z_SOLO, which removes its gz/file API declarations and
# code-generator branches. Preprocess the exact vendored allowlist under that
# definition, then scan what the compiler actually sees. The only raw-source
# exceptions are zutil.h's two inactive F_OPEN macro bodies.
if [ -z "${ARCDROID_SHIPPING_IO_SOURCES:-}" ]; then
  zlib_files='third_party/zlib/adler32.c third_party/zlib/crc32.c third_party/zlib/crc32.h
third_party/zlib/inffast.c third_party/zlib/inffast.h third_party/zlib/inffixed.h
third_party/zlib/inflate.c third_party/zlib/inflate.h third_party/zlib/inftrees.c
third_party/zlib/inftrees.h third_party/zlib/zconf.h third_party/zlib/zlib.h
third_party/zlib/zutil.c third_party/zlib/zutil.h'
  raw_zlib_matches=$(grep -HEn '(^|[^[:alnum:]_])(fopen|_wfopen)[[:space:]]*\(' third_party/zlib/zutil.h || true)
  unexpected_zlib=$(printf '%s\n' "$raw_zlib_matches" | grep -Ev \
    '^third_party/zlib/zutil\.h:124:[[:space:]]+fopen\(\(name\), \(mode\), "mbc=60", "ctx=stm", "rfm=fix", "mrs=512"\)$|^third_party/zlib/zutil\.h:192:#  define F_OPEN\(name, mode\) fopen\(\(name\), \(mode\)\)$' || true)
  if [ -n "$unexpected_zlib" ]; then
    printf '%s\n' "$unexpected_zlib" >&2
    printf '%s\n' 'shipping I/O guard: unexpected zlib raw file-I/O reference' >&2
    exit 1
  fi
  preprocess_tmp=$(mktemp "${TMPDIR:-/tmp}/arcdroid-zlib-preprocessed.XXXXXX")
  for source in $zlib_files; do
    "${CC:-cc}" -DZ_SOLO -Ithird_party/zlib -E -P "$source" >> "$preprocess_tmp"
  done
  if grep -En "$pattern" "$preprocess_tmp" >/dev/null; then
    grep -En "$pattern" "$preprocess_tmp" >&2
    rm -f "$preprocess_tmp"
    printf '%s\n' 'shipping I/O guard: compiled zlib subset exposes file I/O' >&2
    exit 1
  fi
  rm -f "$preprocess_tmp"

  zlib_search_path='build/**/CMakeFiles/arcdroid_zlib.dir/**/*.{o,obj}'
  zlib_objects=$(find build -type f -path '*arcdroid_zlib.dir*' \( -name '*.o' -o -name '*.obj' \) 2>/dev/null || true)
  if [ -z "$zlib_objects" ]; then
    printf 'shipping I/O guard: no compiled zlib objects found; searched %s\n' "$zlib_search_path" >&2
    exit 1
  fi
  nm_bin=${LLVM_NM:-${NM:-nm}}
  if zlib_imports=$($nm_bin -u $zlib_objects 2>/dev/null | grep -E '(^|[[:space:]])_?(fopen|open|_wfopen|gz[a-z]*|CreateFile[A-Za-z]*)$'); then
    printf '%s\n' "$zlib_imports" >&2
    printf '%s\n' 'shipping I/O guard: compiled zlib object references file I/O' >&2
    exit 1
  fi
fi

case "${1:-}" in
  '') ;;
  --scan-only) ;;
  --self-test)
    guard_tmp=$(mktemp -d "${TMPDIR:-/tmp}/arcdroid-shipping-io.XXXXXX")
    trap 'rm -rf "$guard_tmp"' EXIT HUP INT TERM
    for call in 'fopen("x","r")' 'open("x",0)' '_wfopen(0,0)' 'gzopen("x","r")' 'CreateFileW(0,0,0,0,0,0,0)'; do
      printf 'void injected(void) { (void)%s; }\n' "$call" > "$guard_tmp/injected.c"
      if ARCDROID_SHIPPING_IO_SOURCES="$guard_tmp/injected.c" sh "$0" --scan-only >/dev/null 2>&1; then
        printf 'shipping I/O guard self-test: injected %s was not rejected\n' "$call" >&2
        exit 1
      fi
    done
    printf '%s\n' 'void injected_header(void) { dumpregs(); }' > "$guard_tmp/injected.h"
    if ARCDROID_SHIPPING_IO_SOURCES="$guard_tmp/injected.h" sh "$0" --scan-only >/dev/null 2>&1; then
      printf '%s\n' 'shipping I/O guard self-test: injected header dump was not rejected' >&2
      exit 1
    fi
    printf '%s\n' 'shipping I/O guard self-test: file, gzip, platform-open and dump injections rejected'
    ;;
  *)
    printf 'shipping I/O guard: unknown argument: %s\n' "$1" >&2
    exit 2
    ;;
esac
printf '%s\n' 'shipping I/O guard: no file/gzip/platform-open or register-dump path outside the VFS bridge/harness'
