# R1 non-shipping plan core and runner

The shipping core stays dormant. `arcdroid_libretro_test` retains the existing
fault-harness contract. **`arcdroid_rpcplan_libretro`** is the separately named
plan-enabled test core, built from the same dispatcher, backend and libretro
AV/input sources. Only this variant advertises `arcplan`. Neither it nor the
runner is installed. Copy its matching `.info` alongside the frontend's other
core metadata when sideloading; select this core explicitly in RetroArch.

## Plan version 1

UTF-8's ASCII subset, at most 16 KiB, `key=value` per line; spaces/tabs around
values are stripped, LF/CRLF accepted, blank lines and `#` comment lines ignored.
Values are unquoted; internal spaces are literal. Duplicates, unknown keys,
NUL/non-ASCII, invalid numbers, inconsistent CMOS modes and malformed scripts
are errors. Required keys:

```ini
version=1
model=sa110
firmware=firmware.rom
cmos=seed
seed=seed.bin
rtc_epoch=0
```

Models: `arm610`, `arm710`, `sa110`, `arm7500`, with default rates
20/35/**100**/35 MIPS. Optional `rate_mips=1..100` is test-only. AC-8 explicitly
declares 100 and never substitutes a lower rate. The 1..100 bound preserves
the existing instruction-work limit. `rtc_epoch` is an unsigned Unix epoch in
1970 through 2399 (`0..13569465599`); UTC calendar fields come from virtual
elapsed time, never frontend pacing or a host clock.

ROM files are exactly 2, 4 or 8 MiB. Seed files are exactly 256 bytes, copied
without patching. Replace `cmos=seed` with `cmos=factory` and omit `seed` for
factory capture: zero volatile CMOS, internally owned Delete for 12 virtual
seconds, capture at exactly 30 seconds; zero guest writes faults the session.
The core never saves this result. The runner's explicit `--capture-seed FILE`
exports only a completed capture's raw 256 bytes and reports its write mask.
Reset cancels capture and preserves volatile CMOS; a new load reads its plan
again. No ROM search, fingerprint, seed repair or firmware-specific heuristic.

`firmware`, `seed` and optional `input` paths are relative to the frontend's
system directory, or absolute POSIX/Windows drive/UNC paths. Relative `..`
components, drive-relative paths and URI schemes are rejected. Paths may have
spaces, but cannot contain newlines. Files use libretro VFS; permission and
Android storage access must be provided by the frontend. The runner supplies
a read-only native VFS and uses `--system DIR` for the relative base.

Optional `input=events.input` refers to at most 256 KiB/4096 events:

```text
# zero-based retro_run number, numeric libretro key, down level
0 key 97 1
0 mouse 12 -7 1
3 key 97 0
4 mouse 0 0 0
```

Events must be in nondecreasing frame order. Keys are `0..511`, levels 0/1;
mouse relative deltas are -32767..32767 and button masks 0..7. Equal-frame
events retain file order. Script levels use the shared input service's OSK
source, independently of physical
keyboard/pad/mouse levels; buttons remain held until changed. Motion accumulates
through the normal bounded FIFO/mouse path. Script events occur after frontend
polling and before engine entry, at the corresponding virtual boundary. Reset
restarts the script from frame zero after releasing pending input.

## Builds

macOS (Apple Clang; also usable for Linux with its native compiler):

```sh
cmake -S . -B build/r1 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/r1 -j 2
ctest --test-dir build/r1 -R '^(rpc-.*|core-services|left-stick-input-trace)$' --output-on-failure
```

Artifacts: `build/r1/rpc_runner`, `arcdroid_rpcplan_libretro.dylib` on macOS
(`.so` on Linux), and `arcdroid_rpcplan_libretro.info`.

Native Windows in a MinGW/UCRT64 shell:

```sh
cmake -S . -B build/windows-r1 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows-r1 -j 2
ctest --test-dir build/windows-r1 -R '^rpc-.*' --output-on-failure
```

macOS-to-MinGW cross build (build only; execution needs Windows):

```sh
cmake -S . -B build/windows-r1 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres
cmake --build build/windows-r1 -j 2
sh tools/check-windows.sh build/windows-r1/arcdroid_libretro.dll
```

Artifacts: `rpc_runner.exe`, `arcdroid_rpcplan_libretro.dll`,
`arcdroid_rpcplan_libretro.info` under that build directory.

Android arm64, **NDK r29 / 29.0.14206865**, API 21 (set `ANDROID_NDK` to the
installed NDK; same Release configuration as CI and `check-android.sh`):

```sh
cmake -S . -B build/android-r1 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DARCDROID_ANDROID_TEST_CORE=ON
cmake --build build/android-r1 -j 2
LLVM_READELF="$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-readelf" \
  sh tools/check-android.sh build/android-r1/arcdroid_libretro_android.so
```

For Linux build hosts, replace `darwin-x86_64` with `linux-x86_64`. Actual NDK
Release compile commands contain `-O3 -DNDEBUG`, PIC for the cores, the RPC
namespace include, `-fno-common -fno-strict-aliasing`, and the NDK's ordinary
security/section flags. Shipping and Android test cores link with 16 KiB LOAD
alignment; the shipping dependency/export guards remain unchanged.

**Odin3 artifacts:** `build/android-r1/rpc_runner`,
`arcdroid_rpcplan_libretro_android.so`,
`arcdroid_rpcplan_libretro.info`. Sideload the latter two for RetroArch;
the executable loads this same core from an explicit path. Neither is a release
artifact. The old `arcdroid_libretro_test_android.so` is only the unchanged
fault-harness variant, and does not accept plans.

RetroArch removes the platform suffix when looking up metadata: the Android
core above uses **`arcdroid_rpcplan_libretro.info`** in the configured Core Info
directory. This follows [`core_info_get_file_id` in RetroArch 1.22.2](https://github.com/libretro/RetroArch/blob/v1.22.2/core_info.c#L1445-L1474).
Copy the authored ROM/seed/script inputs to the declared system directory and
load the `.arcplan` as content with this core selected.

## Authored replay and factory capture

```sh
build/r1/rpc_runner --core build/r1/arcdroid_rpcplan_libretro.dylib \
  --plan build/r1/generated/rpc-plans/matrix-2.arcplan \
  --system build/r1/generated/rpc-plans --frames 2 --pacing flat \
  --ram-offset 262144 --ram-words 700 > flat.trace
build/r1/rpc_runner --core build/r1/arcdroid_rpcplan_libretro.dylib \
  --plan build/r1/generated/rpc-plans/matrix-2.arcplan \
  --system build/r1/generated/rpc-plans --frames 2 --pacing burst \
  --ram-offset 262144 --ram-words 700 > burst.trace
cmp flat.trace burst.trace
```

Use `.dll`/`.so` and the Windows executable where appropriate. The committed
matrix has its exact record count in `rpc_plan_matrix_rom.py`; requesting extra
words above the records reads zeroed RAM. Plans and authored ROM/seed files are
generated under the build tree only. To generate a standalone external kit:

```sh
python3 -B tests/rpc_plan_fixtures.py build/rpc-kit
python3 -B tools/rpc_ac8_workload.py build/rpc-kit
```

For a privately supplied factory plan, without logging its file contents:

```sh
rpc_runner --core CORE --plan FACTORY.arcplan --system SYSTEM_DIR \
  --frames 1900 --capture-seed CAPTURED_SEED --pacing realtime > factory.trace
```

`--reset-at N` exercises frontend reset; `--cmos` prints raw bytes for authored
tests only. `--fail-alloc 1..5` and `--fault-at N` are explicit runner injection
controls for containment qualification. `--reject` checks that a shipping core
rejects plan content and advertises neither the extension nor admission hooks.

## Canonical evidence and timing

Stdout contains per-frame virtual ns, instruction/idle counts, registers,
device event counts, IRQ/FIQ state, actual raster FNV-1a64 (little-endian
XRGB words), DMA/flyback/current progression, and **actual libretro signed
stereo PCM** FNV-1a64 (little-endian pairs), counts and queue accounting.
Missing video callbacks' dupe frames retain the preceding hash. Additional
first-raster and first-480-pair hashes qualify the combined D1/D2 plan.
`--ram-offset N --ram-words N` adds guest-authored observations. Time summaries
go to stderr so pacing-independent stdout can be compared byte-for-byte.
Canonical stdout uses LF on Windows too; the runner selects binary stdout
before writing traces, following [the CRT translation-mode contract](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/setmode).

`--pacing flat` imposes no delay; `burst` inserts a 3 ms frontend pause after
every third frame starting at zero; `realtime` sleeps only the remaining
reported nominal frame period. None feeds host time into guest execution.

`--timing frames.csv` records every frame's dispatcher/backend execution ns,
nominal `T`, and whether measured. The test dispatcher measures engine entry
through exit; frontend input, script preparation, raster/PCM hashing, AV/audio/
video callbacks, output and pacing are outside it. Backend log callback time
is also excluded. Nearest-rank p50/p95/p99/p99.9 are reported against `T`;
`budget=pass` requires p95 ≤ 0.80T and p99.9 ≤ T. A budget failure is reported,
and does not stop measurement or lower a rate. Host smoke is not AC-8 approval.

## AC-8 predeclared workload and Odin3 command

`rpc_ac8_workload.py` generates `ac8-authored.rom`, `authored.seed` and
`ac8.arcplan`. It explicitly selects SA-110 **100 MIPS**, MMU client-domain
sections for ROM/MMIO/RAM and a mixed page/section data loop. Page sub-APs are
3/2/1/0; section alias is user-read-only. A primed privileged access followed by
denied STRT enters Data Abort and recovers once. The hot loop has ordinary and
user-transfer loads/stores plus ALU/branch operations; denied-store count,
iteration count and palette-update count are guest-observable.

The raster is exactly 24 MHz / 800 clocks / 500 lines = 60 Hz, 640×480,
8 bpp, DRAM DMA. Polling VIDCUR changes palette entry 1 before each following
active line; the initial colour is programmed before the first line. The
authored smoke expects **481, 961, 1441** palette writes after its first three
fields: one initial pointer observation plus one per each of 480 active lines.
The palette varies modulo 256; blanking still advances normal guest scanout.
Stereo DMA is continuously rearmed from alternating 4 KiB banks, containing
signed -1234/+1234 pairs at 44.1 kHz and resampled to actual 48 kHz frontend PCM.
No firmware, game recipe or rate substitution enters this workload.

```sh
python3 -B tools/rpc_ac8_workload.py build/ac8-kit
adb shell mkdir -p /data/local/tmp/arcdroid-r1
adb push build/android-r1/rpc_runner \
  build/android-r1/arcdroid_rpcplan_libretro_android.so \
  build/ac8-kit/. /data/local/tmp/arcdroid-r1/
adb shell chmod 755 /data/local/tmp/arcdroid-r1/rpc_runner
adb shell 'cd /data/local/tmp/arcdroid-r1 && ./rpc_runner \
  --core ./arcdroid_rpcplan_libretro_android.so --plan ac8.arcplan --system . \
  --warmup-seconds 120 --measure-seconds 600 --pacing realtime \
  --timing ac8-frames.csv > ac8.trace 2> ac8-summary.log'
adb pull /data/local/tmp/arcdroid-r1/ac8-frames.csv build/ac8-kit/
adb pull /data/local/tmp/arcdroid-r1/ac8-summary.log build/ac8-kit/
```

Warm-up and measurement use frontend monotonic **wall-clock** windows: 120
seconds then 600 seconds. A frame beginning before warm-up ends is excluded;
measurement stops after the first whole frame reaching the 720-second window.
Overload reduces the number of frames measured without skipping guest work or
lowering its rate. Fixed-frame replays remain pacing-independent. Fable records device
settings, temperatures and frequencies at start/end and the exact artifact
hashes. A failure returns to Fable. Private 3.71 desktop timing is diagnostic
only, not a substitute for this workload.

Frame inspection: repeat `--dump-frame N FILE.ppm` to capture up to 64
predeclared, zero-based `retro_run` frames as binary P6 RGB. For example,
`--dump-frame 0 first.ppm --dump-frame 900 desktop.ppm`. The runner retains
cached presentations for duplicate callbacks. Copying and file output occur
in the frontend video callback, outside the measured engine interval; timing
CSV and canonical stdout remain available. A missing frame or failed write
returns an error. Private smoke images belong in external scratch storage.

## R2c authored native RetroArch hard-disc leg (Windows CI / Android)

Build `rpc-hd-retroarch-fixtures` and the **plan** core. The generated
`generated/rpc-hd-retroarch/` contains `hd.rom`, `hd.arcplan`, `floppy.adf`,
`disk.script`, and an authored CMOS record under `save/`. These are entirely
project-authored GPL-2.0-only test assets, not RISC OS firmware. Use a fresh
save directory; copy the fixture's `save/arcdroid` into it **once**, before the
first launch. Never re-copy the seed between sessions. Set RetroArch's system
directory to the fixture directory and use that same save directory and plan
for all three launches. Select the separately named plan core and its matching
`.info`, not the shipping core. Relative paths in the plan need no platform
edits. On Android, put fixtures/saves in frontend-accessible emulated storage
and install the arm64 plan core through RetroArch's core installation UI.

1. Launch `hd.arcplan`. Cyan means generation 1's 2 MiB write and completion
   marker were acknowledged. Force-stop **the process you launched**, without
   unloading (Android App info → Force stop, or Fable's owned-process tooling).
2. Launch again. Blue means every generation-1 word verified and the different
   generation-2 pattern and marker were acknowledged. Quit cleanly through
   RetroArch (the network `QUIT` pair is used on Windows CI).
3. Launch again. Green means generation 2 verified. Quit cleanly. Red is FAIL;
   black is still running. Do not accept blue on the third launch: a stale
   generation-1 store would take that branch again.

Each launch executes the plan's timed floppy eject/insert at frontend frames
4/6 through the registered disk-control callbacks. The ROM compares all 512
IDENTIFY bytes before/after the operation, independently checks 256 cylinders,
16 heads, 63 sectors and 258,048-sector capacity, then reads every payload word.
The fixed hard-disc identity is `cccccccccccccccccccccccccccccccccccccccc`.
The data begins at LBA 8192; the completion sector is LBA 4096. A virtual timer
wait allows 120 maintenance ticks before writing the marker. After each
session the canonical store should have exactly one `manifest.?` and one
`journal.?`, with every declared chunk present and no temporaries. Manifest
generation (little-endian uint64 at offset 88) must advance from blank 0 to
at least 1 after session 1, advance again in session 2, and remain unchanged in
session 3. This proves publication **and completed RETIRE/GC**, rather than
only an acknowledgement in the journal. Capture screenshots, logs and the
store listing; no damage, refusal or saving-failure notice is acceptable.

Windows CI packages these fixtures with the existing plan DLL/`.info`; the
added leg of `tools/test-retroarch-windows.ps1` checks nine screenshot samples,
manifest/header/chunk checksums and bindings, canonical references, generation
progression and logs. Its evidence and measured added seconds are uploaded in
`arcdroid-windows-retroarch-evidence`. The host `rpc-hd-retroarch-guest` CTest
runs the same ROM/plan through the runner, kills session 1, and also proves a
stale reload and a wrong pattern cannot satisfy the green-screen oracle.
