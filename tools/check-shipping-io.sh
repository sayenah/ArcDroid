#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

command -v grep >/dev/null 2>&1 || {
  printf '%s\n' 'shipping I/O guard: required search tool grep is unavailable' >&2
  exit 2
}

sources='core/adffs.c
core/install_disc.c
core/rpc_boot_filecore.c
core/rpc_launch_helper.c
core/rpc_launch_controller.c
core/rpc_launch_plan.c
core/rpc_launch_attempt.c
core/rpc_boot_record.c
core/hash.c
core/hash_accel.inc
core/hash_accel.h
core/hash_crc_slices.h
core/machine.c
core/rpc_selection.c
core/rpc_catalogue_route.c
core/rpc_selection.h
libretro/rpc_inventory.c
libretro/rpc_route.c
libretro/rpc_route.h
libretro/rpc_staging.h
libretro/rpc_inventory.h
core/input.c
core/input_mapping.c
core/osk.c
core/firmware_table.c
core/launch.c
core/launch_discovery.c
core/launch_disc.c
core/launch_chooser.c
core/hard_disc_scope.c
core/hd_program.c
core/folder_set.c
core/playlist.c
core/jfd.c
core/persistence.c
core/cmos_defaults.c
engine/arcdroid_engine.c
engine/arculator_backend.c
engine/rpc/rpc_observer.c
engine/rpc/rpc_observers.c
engine/rpc/rpc_inspector.c
engine/rpc/rpc_profile.c
engine/rpc/rpc_crosscheck.c
engine/rpc/rpc_lockstep.c
engine/rpc/rpc_session.c
engine/rpc/rpc_audio.c
engine/rpc/rpc_input.c
engine/rpc/rpc_timing.c
engine/rpc/rpc_mem_timing.c
engine/rpc/rpc_floppy.c
engine/rpc/rpc_scheduler.c
engine/rpc/rpc_rtc.c
engine/rpcemu/src/arm.c
engine/rpcemu/src/arm_common.c
engine/rpcemu/src/cp15.c
engine/rpcemu/src/mem.c
engine/rpcemu/src/cmos.c
engine/rpcemu/src/fdc.c
engine/rpcemu/src/ide.c
engine/rpcemu/src/iomd.c
engine/rpcemu/src/keyboard.c
engine/rpcemu/src/vidc20.c
engine/rpcemu/src/superio.c
engine/rpcemu/src/i8042.c
engine/arc_boot_watch.c
engine/arc_launch_outcome.c
engine/arc_error_box.c
engine/arc_disc_request.c
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
libretro/arc_lock.c
libretro/arc_session.c
libretro/install_store.c
libretro/rpc_hd_store.c
libretro/rpc_preparation.c
libretro/rpc_preparation_host.c
libretro/rpc_launch_record_store.c
libretro/rpc_preparation.h
libretro/rpc_cmos_store.c
libretro/rpc_boot.c
libretro/rpc_boot_zip.c
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
    /^[[:space:]]*set\(ARCDROID_(LIBRETRO|ARCULATOR|RPC)_SOURCES[[:space:]]*$/ { scan = 1; next }
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

pattern='(^|[^[:alnum:]_>.])(fopen[a-z0-9_]*|open(at|64|at64)?|creat(64)?|fdopen|_wfopen[a-z0-9_]*|gz[a-z]*|CreateFile[A-Za-z0-9]*|NtCreateFile|_open[a-z0-9_]*|_wopen[a-z0-9_]*|_sopen[a-z0-9_]*|_wsopen[a-z0-9_]*|freopen[a-z0-9_]*|tmpfile[a-z0-9_]*|mkstemp[a-z0-9_]*)[[:space:]]*\(|(^|[^[:alnum:]_])dumpregs[[:space:]]*\('

# Every compiled RPC source is scanned, including the session and device hooks.
rpc_sources='engine/arc_error_box.c engine/arc_disc_request.c engine/arc_launch_outcome.c engine/arc_boot_watch.c
engine/rpc/rpc_observer.c
engine/rpc/rpc_observers.c
engine/rpc/rpc_inspector.c
engine/rpc/rpc_profile.c
engine/rpc/rpc_crosscheck.c
engine/rpc/rpc_lockstep.c
engine/rpc/rpc_session.c engine/rpc/rpc_audio.c engine/rpc/rpc_input.c engine/rpc/rpc_timing.c engine/rpc/rpc_mem_timing.c
engine/rpc/rpc_floppy.c engine/rpc/rpc_scheduler.c engine/rpc/rpc_rtc.c
engine/rpcemu/src/arm.c engine/rpcemu/src/arm_common.c engine/rpcemu/src/cp15.c
engine/rpcemu/src/mem.c engine/rpcemu/src/cmos.c engine/rpcemu/src/fdc.c engine/rpcemu/src/ide.c
engine/rpcemu/src/iomd.c engine/rpcemu/src/keyboard.c engine/rpcemu/src/vidc20.c
engine/rpcemu/src/superio.c engine/rpcemu/src/i8042.c'
rpc_cmake_sources=$(grep -oE 'engine/(rpc|rpcemu)/[[:alnum:]_./-]+\.c' CMakeLists.txt | sort -u)
for source in $rpc_cmake_sources; do
  if ! printf '%s\n' "$rpc_sources" | tr ' ' '\n' | grep -Fx "$source" >/dev/null; then
    printf 'shipping I/O guard: compiled RPC source is not scanned: %s\n' "$source" >&2
    exit 1
  fi
done
# Backend member callbacks are the authorized frontend boundary, not host libc calls.
rpc_pattern='(^|[^[:alnum:]_>.])(fopen[0-9]*|freopen|fclose|fread|fwrite|fseek[a-z0-9]*|ftell[a-z0-9]*|open|close|read|write|socket|connect|send[a-z]*|recv[a-z]*|pthread_[a-z_]*|CreateThread|sleep|usleep|nanosleep|Sleep|time|clock|clock_gettime|gettimeofday|localtime|gmtime|getenv|exit|abort|assert)[[:space:]]*\('
if grep -En "$rpc_pattern" $rpc_sources; then
  printf '%s\n' 'shipping I/O guard: forbidden RPCEmu host service' >&2
  exit 1
fi
scan_sources=${ARCDROID_SHIPPING_IO_SOURCES:-$sources}
# D-193: path-keyed, exact-once opens; lock/identity confinement and no data
# access on sentinel descriptors. The helper scans the same complete allowlist.
python3 -B tools/check-shipping-lock-io.py $scan_sources

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

  zlib_build_root=${ARCDROID_SHIPPING_IO_BUILD_DIR:-build}
  zlib_search_path="$zlib_build_root/**/CMakeFiles/arcdroid_zlib.dir/**/*.{o,obj}"
  zlib_objects=$(find "$zlib_build_root" -type f -path '*arcdroid_zlib.dir*' \( -name '*.o' -o -name '*.obj' \) 2>/dev/null || true)
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
    python3 -B tools/check-shipping-lock-io.py --self-test
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
    for call in 'read(0,0,0)' 'write(0,0,0)' 'clock_gettime(0,0)' 'abort()' 'time(0)' 'getenv(0)'; do
      printf 'void injected(void) { (void)%s; }\n' "$call" > "$guard_tmp/rpc.c"
      if ! grep -Eq "$rpc_pattern" "$guard_tmp/rpc.c"; then
        printf 'shipping I/O guard self-test: direct RPC %s was not rejected\n' "$call" >&2
        exit 1
      fi
    done
    printf '%s\n' 'void adapter(void) { ide.backend.read(0,0,0,0); ide.backend.write(0,0,0,0); }' > "$guard_tmp/rpc.c"
    if grep -Eq "$rpc_pattern" "$guard_tmp/rpc.c"; then
      printf '%s\n' 'shipping I/O guard self-test: authorized backend callbacks were rejected' >&2
      exit 1
    fi
    printf '%s\n' 'shipping I/O guard self-test: file, gzip, platform-open and dump injections rejected'
    ;;
  *)
    printf 'shipping I/O guard: unknown argument: %s\n' "$1" >&2
    exit 2
    ;;
esac
printf '%s\n' 'shipping I/O guard: no native file path outside the VFS bridge and the exact D-193 sentinel exception'
