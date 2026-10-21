<!-- SPDX-License-Identifier: GPL-2.0-only -->

# R1 non-shipping plan core and runner

The shipping core stays dormant. `arcdroid_libretro_test` retains the existing
fault-harness contract. **`arcdroid_rpcplan_libretro`** is the separately named
plan-enabled test core, built from the same dispatcher, backend and libretro
AV/input sources. Only this variant advertises `arcplan`. Neither it nor the
runner is installed. Copy its matching `.info` alongside the frontend's other
core metadata when sideloading; select this core explicitly in RetroArch.

RetroArch 1.22.2's network `GET_STATUS` can crash for any loaded core without
its matching `.info`, or with an omitted/empty `systemid`: its
[`command_get_status`](https://github.com/libretro/RetroArch/blob/v1.22.2/command.c#L972-L1007)
copies `core_info->system_id` without checking for NULL. Install this plan
core's matching `.info` in the configured Core Info directory before using
network commands. It declares `systemid = "riscpc"`; `rpc-core-info` checks
source and configured metadata. This applies to frontend status polling,
including the Windows leg's command-port readiness check.

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

## R2c run E: hash identity and hard-disc work on the Odin3

Build the Android test core, runner and standalone hash test (NDK arm64 API 21
baseline; optional instructions are selected at runtime):

```sh
cmake --build build/r2c-b/android --target arcdroid_rpcplan_libretro rpc_runner hash_accel_test -j1
python3 -B tests/rpc_hd_retroarch_rom.py build/r2c-e/odin-fixture
sed 's/hard_disc=on/hard_disc=off/' build/r2c-e/odin-fixture/hd.arcplan > build/r2c-e/odin-fixture/hd-off.arcplan
adb shell mkdir -p /data/local/tmp/arcdroid-r2c-e
adb push build/r2c-b/android/rpc_runner build/r2c-b/android/hash_accel_test \
  build/r2c-b/android/arcdroid_rpcplan_libretro_android.so \
  build/r2c-e/odin-fixture /data/local/tmp/arcdroid-r2c-e/
adb shell 'cd /data/local/tmp/arcdroid-r2c-e && chmod 755 rpc_runner hash_accel_test && ./hash_accel_test --require-arm'
adb shell 'cd /data/local/tmp/arcdroid-r2c-e && ./hash_accel_test --bench'
adb shell 'cd /data/local/tmp/arcdroid-r2c-e && mkdir save-on && cp -R odin-fixture/save/arcdroid save-on/ && ./rpc_runner --core ./arcdroid_rpcplan_libretro_android.so --plan odin-fixture/hd.arcplan --system odin-fixture --save save-on --frames 1500 --pacing flat --timing on.csv --hd-cost hd.csv --ram-offset 131072 --ram-words 1 > on.trace 2> on.log'
adb shell 'cd /data/local/tmp/arcdroid-r2c-e && mkdir save-off && cp -R odin-fixture/save/arcdroid save-off/ && ./rpc_runner --core ./arcdroid_rpcplan_libretro_android.so --plan odin-fixture/hd-off.arcplan --system odin-fixture --save save-off --frames 1500 --pacing flat --timing off.csv --hd-cost hd-off.csv > off.trace 2> off.log'
adb pull /data/local/tmp/arcdroid-r2c-e/hd.csv build/r2c-e/odin-hd.csv
adb pull /data/local/tmp/arcdroid-r2c-e/hd-off.csv build/r2c-e/odin-hd-off.csv
python3 -B tools/check-rpc-hd-cost.py build/r2c-e/odin-hd.csv --frames 1500
python3 -B tools/check-rpc-hd-cost.py build/r2c-e/odin-hd-off.csv --frames 1500
```

Use new save-directory names for a repeat; never replace the seed in an existing
store. `--require-arm` fails unless **both** SHA2 and CRC32 were detected through
Android's `getauxval`; it then runs the vectors and differential tests, including
forced portable modes. `--bench` compares all available modes without changing
capability detection. Windows' `hash_accel_test.exe` runs the same tests and
executes SHA-NI only when CPUID reports it. `--require-x86` prints CPUID SHA
advertisement and the selected automatic SHA path, and fails when advertised
SHA-NI is not selected (CI runs this explicitly).

`--hd-cost` is opt-in and requires the plan test core. Its separate CSV records
every requested frame, including warm-up: backend read verification, complete
write acknowledgement (hashing, VFS, index/cache), maintenance tick time,
maintenance entry/exit phases, the executed `substep` and its attempted
`namespace_operations`, read/write command counts, generation and
checkpoint count, plus `open_ns` for store open/import/blank creation reached
during the first-launch CMOS capture transition. Load-time work outside
`retro_run` is excluded. `total_ns` sums all four timed intervals inside
`retro_run`. Clocks and reporting never control guest execution. CSV writes,
framebuffer/PCM hashing and frontend pacing are outside these intervals. It
keeps the existing trace and `--timing` format unchanged.

The checker requires run N’s substep/namespace columns and refuses incomplete
reports or namespace counts outside 0/1. Under D-161 it fails any frame with
`2 * total_ns > T_ns` when its maintenance tick made one namespace call,
or `5 * total_ns > T_ns` otherwise. Write acknowledgement independently keeps
`5 * acknowledgement_ns <= T_ns` even in a namespace tick. Inspect phase/substep
outliers and `over_D161`; averages and percentiles cannot qualify the run.
AC-9’s whole-frame no-over-T gate still stands. Inspect `on.trace`'s last RAM word: it
must be `ram=00000001` (fresh session, cyan), and the final HD row must show at least two completed
checkpoints and an advanced generation. Collect `on/off.csv`, traces/logs and
the canonical store, and compare with the prior on/off device measurement.
For Fable's private 031 installer replay, use the identical existing runner
arguments and starting store, adding `--hd-cost installer-hd.csv` and
`--timing installer-on.csv`; check that report the same way. Its emulation cost
is separate from the hard-disc bound (D-152). Native RetroArch termination,
media/persistence legs and the Windows CI leg must still be repeated on the
final core.

Run K (D-156): D-152 measurements use saves on `/storage/emulated/0` (FUSE)
and `rpc_runner --vfs-flush retroarch --hd-cost <CSV>`: the VFS flush checks
`fflush` only, matching RetroArch 1.22.2 `retro_vfs_file_flush_impl`.
`--vfs-flush fsync` is the default and adds `fsync` (`_commit` on Windows);
retain this mode for termination and durability experiments. Neither mode
changes the core's acknowledgement checks or promises directory/power-loss
durability. Always record the save path and flush mode alongside timing.
The two modes are checked by `rpc-vfs-conformance`; the real kill sweeps keep
the default sync mode. Reject other `--vfs-flush` values.

## 6b-P0i profiling (test-only)

`ARCDROID_RPC_PROFILE` defaults OFF. ON instruments only the non-installed
plan core and runner; the shipping core remains uninstrumented. Schema 1,
counter definitions, sampling limits and measured Mac overhead are in
[the instrumentation design](../docs/riscpc-translator.md#6b-p0i-instrumentation-design-s64).
Use a copy of the same initial save store for each ON/OFF leg. Keep private
plans, ROMs, saves and logs local. Profiling never controls guest progression.

Mac build and run, from the clone root (replace the three private paths):

```sh
cmake -S . -B build/profile -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DARCDROID_RPC_PROFILE=ON
cmake --build build/profile --target rpc_runner arcdroid_rpcplan_libretro -j 4
P0I_PLAN='/absolute/private/private.arcplan'
P0I_SYSTEM='/absolute/private/system'
P0I_SAVE='/absolute/private/copied-save'
mkdir -p .evidence/p0i-mac
build/profile/rpc_runner --core "$PWD/build/profile/arcdroid_rpcplan_libretro.dylib" \
  --plan "$P0I_PLAN" --system "$P0I_SYSTEM" --save "$P0I_SAVE" \
  --frames 18001 --pacing flat --profile .evidence/p0i-mac/profile.json \
  --profile-interval 60 --profile-sample-stride 1024 \
  --timing .evidence/p0i-mac/frames.csv --preparation-timing .evidence/p0i-mac/preparation.csv \
  > .evidence/p0i-mac/guest.txt 2> .evidence/p0i-mac/runner.log
```

Odin3 build and run, after Fable finishes other serial device work and confirms
the device is available. This kit does not replace an installed RetroArch core.
The device plan, system and copied save directory must already exist; private
paths below are placeholders. Path variables must contain no single quotes
because they are quoted inside the remote shell command.

```sh
P0I_NDK=/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865
cmake -S . -B build/android-on -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$P0I_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DARCDROID_ANDROID_TEST_CORE=ON -DARCDROID_RPC_PROFILE=ON
cmake --build build/android-on --target rpc_runner arcdroid_rpcplan_libretro -j 4
P0I_ADB=/opt/homebrew/share/android-commandlinetools/platform-tools/adb
"$P0I_ADB" shell mkdir -p /data/local/tmp/arcdroid-p0i
"$P0I_ADB" push build/android-on/rpc_runner /data/local/tmp/arcdroid-p0i/rpc_runner
"$P0I_ADB" push build/android-on/arcdroid_rpcplan_libretro_android.so /data/local/tmp/arcdroid-p0i/plan.so
"$P0I_ADB" shell chmod 755 /data/local/tmp/arcdroid-p0i/rpc_runner
P0I_DEVICE_PLAN=/storage/emulated/0/RetroArch/private.arcplan
P0I_DEVICE_SYSTEM=/storage/emulated/0/BIOS
P0I_DEVICE_SAVE=/storage/emulated/0/arcdroid-p0i-save
"$P0I_ADB" shell "/data/local/tmp/arcdroid-p0i/rpc_runner --core /data/local/tmp/arcdroid-p0i/plan.so --plan '$P0I_DEVICE_PLAN' --system '$P0I_DEVICE_SYSTEM' --save '$P0I_DEVICE_SAVE' --frames 18001 --pacing flat --profile /data/local/tmp/arcdroid-p0i/profile.json --profile-interval 60 --profile-sample-stride 1024 --timing /data/local/tmp/arcdroid-p0i/frames.csv --preparation-timing /data/local/tmp/arcdroid-p0i/preparation.csv > /data/local/tmp/arcdroid-p0i/guest.txt 2> /data/local/tmp/arcdroid-p0i/runner.log"
# Check the preceding exit status before accepting results.
mkdir -p .evidence/p0i-odin
"$P0I_ADB" pull /data/local/tmp/arcdroid-p0i/profile.json .evidence/p0i-odin/
"$P0I_ADB" pull /data/local/tmp/arcdroid-p0i/frames.csv .evidence/p0i-odin/
"$P0I_ADB" pull /data/local/tmp/arcdroid-p0i/preparation.csv .evidence/p0i-odin/
"$P0I_ADB" pull /data/local/tmp/arcdroid-p0i/runner.log .evidence/p0i-odin/
```

Repeat with strides 1021 and 0, restoring the initial save store each time.
Build OFF into a separate directory with `-DARCDROID_RPC_PROFILE=OFF`; omit
all `--profile*` options for that leg. Alternate both orders and record machine,
load, settings, thermal state, toolchain and binary hashes. Compare guest
projections and persisted bytes. Use OFF frame timings for actual headroom;
compare cold and sustained windows by guest second in the profile and CSV.
Preparation timing is separate from requested-session profiling.

## 6b-1a0 endpoint and measurement cores

D-195's amended capability matrix and exact endpoint equations are in
`docs/riscpc-translator.md`, "Slice 1a-0 measurement design". All workloads
below are authored locally. Keep private plans, stores and results in private
local evidence directories; do not commit or upload them.

Host verification (use two jobs on a shared machine):

```sh
cmake -S . -B /private/tmp/arcdroid-1a0-host -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DARCDROID_RPC_PROFILE=ON -DARCDROID_RPC_CROSSCHECK=ON
cmake --build /private/tmp/arcdroid-1a0-host -j 2
ctest --test-dir /private/tmp/arcdroid-1a0-host --output-on-failure -j 2
cmake -S . -B /private/tmp/arcdroid-1a0-sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DARCDROID_SANITIZERS=ON \
  -DARCDROID_RPC_PROFILE=ON -DARCDROID_RPC_CROSSCHECK=ON
cmake --build /private/tmp/arcdroid-1a0-sanitize -j 2
ctest --test-dir /private/tmp/arcdroid-1a0-sanitize --output-on-failure -R '^rpc-' -j 2
```

Linux sanitizer CI enables cross-check and runs its success/injected-mismatch
self-test. Local sanitizer runtime settings should match the host's supported
runtime; a Mac run without LeakSanitizer is not leak evidence.

`rpc-memory-timing-0..3` each run every declared interval in intrusive and
endpoint modes, compare complete records and emit the measured-interval
integer/rational/S1 totals on stderr. `rpc-endpoint-directed`,
`rpc-endpoint-cpu`, `rpc-counter-profile-directed`, `rpc-endpoint-plan-prefix`,
`rpc-measurement-mutants` and `rpc-crosscheck-selftest` are the new directed
checks. Existing forced-slow memory-service, fetch-memo, hot-video and long
request-level checks retain their capabilities. `rpc-measurement-core` checks
stdout/count parity, frame timing and refusals on hot plus four synthetics.

The deciding core is **arcdroid_rpcmeasure_libretro** (`.dylib`, `.so`, `.dll`
according to platform), not the instrumented plan core. It is not installed.
It supports plans, production observers, frame `core_ns` and event diagnostics.
Intrusive/endpoint measurement, observer tracing, quota clamps, fault injection
and exercise/ranking getters refuse. The runner reports the facility and exits
nonzero. For example, append `--exercise`, `--ranking`,
`--intrusive-instructions 1:2:0`, `--endpoint-instructions 1:2:0`,
`--fail-alloc 1` or `--fault-at 0`; plan `observation_quota` or trace keys also
refuse. Ordinary plan/calibration cores still support their existing requests.

For authored endpoint experiments on an ordinary plan core, use
`--endpoint-instructions FIRST:LAST:DRAIN` (DRAIN is 0/1). A functional prefix
is captured with `--prefix-instructions LAST`; `--single-step-prefix` uses the
existing quota-one test API for the unarmed reference. Prefix aggregates are
stderr-only, and default goldens remain unchanged. Run these diagnostics on
authored inputs; they are absent from the deciding core.

Code and layout comparison, with base/candidate built using the same compiler,
optimisation, architecture, visibility and source-revision define:

```sh
python3 -B tools/rpc_measure_compare.py --base "$BASE_BUILD" \
  --candidate "$CANDIDATE_BUILD" --readobj "$LLVM_READOBJ"
```

The script checks every shipping object's executable sections (Mach-O, ELF or
COFF), then the full code/relocations/constants of `arm.c`, `cp15.c`, `mem.c`
and `rpc_mem_timing.c` between measurement and shipping. This includes all
three interpreter loops and the named timing/translation/fetch functions and
inline callers. Every shared timing/cache/packet member offset, size and
alignment is checked by compiling a constant probe with those targets' flags.
No code, relocation, constant or address masking is used. The separate
R-e0-1 check (`--test-loops`) compares the endpoint-containing `arcdroid_libretro_test_rpc`
with the base: all three preprocessed loops and reachable local/inline helpers
match; exercise-begin is in the common wrapper before dispatch. Emitted
instruction counts do not grow and RPC relocation references match. Build the
ordinary test core in both trees, and supply `--test-loops --objdump LLVM_OBJDUMP` as well
as `--readobj LLVM_READOBJ` if they are not on PATH. Its CTest self-test is
`rpc-test-loop-guard`. Keep source identity pinned identically for archive
builds (`-DARCDROID_SOURCE_REVISION=<base revision>`).

AC-9 observer caveat (Run C's authorized F-4 fallback): the four compared
CPU/timing/translation objects match shipping; observer objects are outside
that identity claim. `rpc_observer.c` adds API functions without changing the
production source (host QA found existing functions instruction-identical).
In `rpc_observers.c`, `state.overridden` shifts offsets
for inserted-media, callback, candidate, retry and suppression state. Observer
events/frame/status functions addressing them use those shifted offsets.
`error_event` and `disc_event` load the mutable `watch_budget` instead of
shipping's constant 1,000,000,000 attempts for watch/renew calls. This is the
watch-budget API, not the launch horizon. The horizon API separately makes
`state.launch.profile` redirectable for launch/frame handling; rebase saves
and restores test override policy per reset. Full observer-object identity
would require redesigning the current API/state boundary, removing retained
policy capabilities, or changing shipping state/reset code. Run C keeps these
APIs and shipping identity and uses the authorized documentation fallback.
Record this per-event cost caveat with
AC-9 results; no such work is added per instruction.

For AC-9 on **any** pair, including qualified 031 firmware, write a copy of the
plan with `observer_cells=off` for the unobserved leg. Confirm the distinct
`RPC observers: TEST OFF; all cells disabled (observer_cells=off)` log. Use an
absent `observer_cells` key for the production leg. Numeric zero still means
no candidate override and is not an off switch. Omit observation trace and
`observation_profile=unmatched` in both cost legs; those are separate diagnostic
subscriptions. Both plan and measurement cores support the off setting,
including attachment/reset. `rpc-observers-off-plan` and
`rpc-observers-off-measure` exercise all cells off on a qualified 3.71/SA110
binding via the existing trusted table seam, with absent/zero controls.

Windows cross-build on the development host:

```sh
cmake -S . -B /private/tmp/arcdroid-1a0-mingw -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64-x86_64.cmake \
  -DARCDROID_RPC_PROFILE=ON -DARCDROID_RPC_CROSSCHECK=ON
cmake --build /private/tmp/arcdroid-1a0-mingw -j 2
```

Native Windows runs the same CTest suite and comparison script with
`llvm-readobj` available. The Windows test-export guard includes the
measurement DLL. Fable's private native Windows runs use the measurement DLL,
`rpc_runner.exe` and the same authored/private inputs as Android; cross-building
alone is not native Windows execution evidence.

Android shell kit (NDK path is local configuration):

```sh
ARCDROID_NDK="${ANDROID_NDK_HOME:?set the NDK path}"
cmake -S . -B /private/tmp/arcdroid-1a0-android -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_TOOLCHAIN_FILE="$ARCDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DARCDROID_ANDROID_TEST_CORE=ON -DARCDROID_RPC_PROFILE=ON
cmake --build /private/tmp/arcdroid-1a0-android -j 2
python3 -B tools/rpc_hot_workload.py /private/tmp/arcdroid-1a0-workloads/hot
python3 -B tools/rpc_measure_workloads.py --out /private/tmp/arcdroid-1a0-workloads
adb shell mkdir -p /data/local/tmp/arcdroid-1a0
adb push /private/tmp/arcdroid-1a0-android/rpc_runner /data/local/tmp/arcdroid-1a0/
adb push /private/tmp/arcdroid-1a0-android/rpc_timing_test /data/local/tmp/arcdroid-1a0/
adb push /private/tmp/arcdroid-1a0-android/rpc_endpoint_test /data/local/tmp/arcdroid-1a0/
adb push /private/tmp/arcdroid-1a0-android/rpc_endpoint_cpu_test /data/local/tmp/arcdroid-1a0/
adb push /private/tmp/arcdroid-1a0-android/arcdroid_libretro_test_android.so /data/local/tmp/arcdroid-1a0/
adb push /private/tmp/arcdroid-1a0-android/arcdroid_rpcplan_libretro_android.so /data/local/tmp/arcdroid-1a0/
adb push /private/tmp/arcdroid-1a0-android/arcdroid_rpcmeasure_libretro_android.so /data/local/tmp/arcdroid-1a0/
adb push /private/tmp/arcdroid-1a0-workloads /data/local/tmp/arcdroid-1a0/workloads
adb shell '/data/local/tmp/arcdroid-1a0/rpc_endpoint_test && /data/local/tmp/arcdroid-1a0/rpc_endpoint_cpu_test'
adb shell '/data/local/tmp/arcdroid-1a0/rpc_timing_test /data/local/tmp/arcdroid-1a0/arcdroid_libretro_test_android.so'
```

The filenames above are from the configured Android build. The device must
be available and unused before Fable starts these serial legs. Record only aggregate public
results in tracked reports. Use the normal Android shipping guard too.

The four fixed synthetic recipes are `warm` (16 load/store pairs on a warmed
line), `stream-sa110` and `stream-arm710` (16384-word ring, buffered stores), and
`ldm-stm8` (eight-register load/store). `--kind NAME` generates one; the default
`--kind all` generates all. Committed plan templates live in
`tests/fixtures/measure/`. Models/rates are SA-110/100 except ARM710/35 for its
streaming recipe. Video DMA and input are absent, like the existing hot recipe.
Each uses realtime **60 s warm + 300 s measured**, production observer setup,
no watch and trace off. For example (Fable's device leg):

```sh
adb shell '/data/local/tmp/arcdroid-1a0/rpc_runner --core /data/local/tmp/arcdroid-1a0/arcdroid_rpcmeasure_libretro_android.so --plan /data/local/tmp/arcdroid-1a0/workloads/warm.arcplan --system /data/local/tmp/arcdroid-1a0/workloads --frames 1000000 --pacing realtime --warmup-seconds 60 --measure-seconds 300 --timing /data/local/tmp/arcdroid-1a0/warm-A.csv > /data/local/tmp/arcdroid-1a0/warm-A-guest.txt 2> /data/local/tmp/arcdroid-1a0/warm-A.log'
```

For AC-5(c), compare the plan and measurement cores' complete stdout and guest
instruction counts on hot and the private 031 recipe, and verify timing CSV
`core_ns`. For private 031, keep S61's exact input schedule and fresh store copy,
8000 frames, decision window 6400–7400 and report window 6000–8000. Substitute
only private environment-provided plan/system/save paths in local commands.
Never copy those values into this runbook or public evidence.

AC-8 uses measurement cores only: A/A under two tags on each platform/workload,
orders A B B A A B and B A A B B A, 120 s idle between runs, closed RetroArch,
screen on, temperature/frequencies recorded. Per run p50/p95; per order medians
of three A and three B runs. Bands are max absolute per-order A/A deltas, each
floored at 0.5%. Apply the work order's ten-block/8%/three-consecutive clock-step
rule and its single replacement, repeated-flag and candidate-throttling rules.
Later slices use the frozen gain/regression/retest/inconclusive procedure, with
Windows no-regression on hot and 031. Do not silently exclude tail regressions.

AC-9 uses the same measurement core with observers off versus production cells:
≤3% at both p50/p95 on hot and 031. Held-watch, SWI-dense and idle desktop are
reported upper bounds. Existing `tools/rpc_observer_cost.py` generates authored
hot/held/SWI inputs; retain the work order's two orders, windows and clock-step
policy for the deciding evidence. No device leg is run concurrently with another.

AC-10 uses the **profile** plan core, never the measurement core. Run private
031 with S61 inputs and the authored hot workload on ARM710 (change only that
plan's model/rate to `arm710`/35). Request `--profile PROFILE.json
--profile-interval 1 --profile-sample-stride 0`; repeat sampled profiling at a
recorded stride if cost attribution is needed. Schema 2 carries lookup
outcomes/ways/probes, exclusive service classes, TLB walks/hit opportunities,
fill/shortcut counts, AL share and integer/rational counts. New ranking counters
reset with `ranking_epoch`; their denominator is `ranking_attempts` (AL share uses `ranking_fetched`).
AL and both denominators reset at the same binding. Legacy
P0i report fields retain cumulative semantics. Derive a precise aggregate:

```sh
python3 -B tools/rpc_rank_report.py --profile "$PRIVATE_PROFILE" \
  --first-frame 6400 --last-frame 7400 > "$PRIVATE_RANK_REPORT"
```

The report contains integer counts and explicit numerator/denominator pairs,
including probes per hit. It does not choose an optimisation: Fable records
L1/S1 order from both required workloads before authorising follow-on slices.
AC-5(c), AC-8, AC-9 and AC-10 remain Fable's private legs.

Concrete private-leg command forms (set each variable locally; never paste its
value into public logs). For AC-5(c), restore the same initial save-store bytes
into `PRIVATE_SAVE_COPY` before **each** invocation, then compare the complete
stdout files with `cmp` on the host or `fc /b` on native Windows:

```sh
"$RUNNER" --core "$PLAN_CORE" --plan "$PRIVATE_PLAN" --system "$PRIVATE_SYSTEM" \
  --save "$PRIVATE_SAVE_COPY" --frames 8000 --pacing realtime \
  --timing "$PRIVATE_PLAN_TIMING" > "$PRIVATE_PLAN_TRACE" 2> "$PRIVATE_PLAN_LOG"
# Restore the identical initial save-store copy here.
"$RUNNER" --core "$MEASUREMENT_CORE" --plan "$PRIVATE_PLAN" --system "$PRIVATE_SYSTEM" \
  --save "$PRIVATE_SAVE_COPY" --frames 8000 --pacing realtime \
  --timing "$PRIVATE_MEASURE_TIMING" > "$PRIVATE_MEASURE_TRACE" 2> "$PRIVATE_MEASURE_LOG"
cmp "$PRIVATE_PLAN_TRACE" "$PRIVATE_MEASURE_TRACE"
```

On Android, use these same runner arguments inside `adb shell` with device-side
variables/paths supplied locally. Repeat hot with the generated hot plan and
its fixed 60/300 second warm/measure window. AC-8 repeats the measurement leg
under A and B result names in both frozen six-run orders; restore initial
stores each time and leave 120 s idle between runs. A and B use identical core
bytes for the initial A/A band. Preserve per-run CSVs before applying the
work order's clock-step and p50/p95 rules.

For AC-9, supply locally prepared off/production plans that differ only in
observer configuration, and repeat both orders with the same measurement
core and input/store recipe. One leg's command form is:

```sh
"$RUNNER" --core "$MEASUREMENT_CORE" --plan "$PRIVATE_OBSERVER_PLAN" \
  --system "$PRIVATE_SYSTEM" --save "$PRIVATE_SAVE_COPY" --frames 8000 \
  --pacing realtime --timing "$PRIVATE_OBSERVER_TIMING" \
  > "$PRIVATE_OBSERVER_TRACE" 2> "$PRIVATE_OBSERVER_LOG"
```

For AC-10, use the profile core and interval-one capture, restoring the same
initial private store first. For the second required workload substitute the
authored hot plan configured for ARM710/35; preserve its 60/300 second window.

```sh
"$RUNNER" --core "$PROFILE_CORE" --plan "$PRIVATE_PLAN" --system "$PRIVATE_SYSTEM" \
  --save "$PRIVATE_SAVE_COPY" --frames 8000 --pacing realtime \
  --profile "$PRIVATE_PROFILE" --profile-interval 1 --profile-sample-stride 0 \
  > "$PRIVATE_PROFILE_TRACE" 2> "$PRIVATE_PROFILE_LOG"
python3 -B tools/rpc_rank_report.py --profile "$PRIVATE_PROFILE" \
  --first-frame 6400 --last-frame 7400 > "$PRIVATE_RANK_REPORT"
```

For the authored ARM710 ranking leg, copy the generated hot plan locally and
change `model` to `arm710` and `rate_mips` to `35`, then run:

```sh
"$RUNNER" --core "$PROFILE_CORE" --plan "$ARM710_HOT_PLAN" --system "$HOT_SYSTEM" \
  --save "$HOT_SAVE_COPY" --frames 1000000 --pacing realtime \
  --warmup-seconds 60 --measure-seconds 300 --profile "$HOT_PROFILE" \
  --profile-interval 1 --profile-sample-stride 0 \
  > "$HOT_PROFILE_TRACE" 2> "$HOT_PROFILE_LOG"
```

Select exact recorded frame bounds within that workload's measured window for
`rpc_rank_report.py`. The fixed 6400–7400 decision window belongs to private
031; the hot wall-clock window remains 60/300 seconds.

### L1 measurement variants (D-200, frozen selection table)

Configure a separate Release tree with `-DARCDROID_RPC_L1_VARIANTS=ON`.
The default measurement core retains the shipping gates: ARM610 hint+mirror,
ARM710/ARM7500 linear, SA-110 way0-hint (selection run 3, S68).
These separately named measurement targets retain shipping-shaped flags and
carry no counters, hint poison or cross-check. Their gate macro survives the
measurement core's TEST_FAULTS undefinition:

```sh
cmake -S . -B /private/tmp/arcdroid-l1-measure-host -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DARCDROID_RPC_L1_VARIANTS=ON
cmake --build /private/tmp/arcdroid-l1-measure-host -j3 --target rpc_runner \
  arcdroid_rpcmeasure_libretro \
  arcdroid_rpcmeasure_l1_sa_linear_libretro \
  arcdroid_rpcmeasure_l1_sa_hint_libretro \
  arcdroid_rpcmeasure_l1_sa_mirror_libretro \
  arcdroid_rpcmeasure_l1_sa_way0_hint_libretro \
  arcdroid_rpcmeasure_l1_sa_way0_mirror_libretro \
  arcdroid_rpcmeasure_l1_off_libretro
```

For native Windows use the same configure/build arguments from its MinGW shell
and an external build path. For a MinGW cross-build on the Mac add
`-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64-x86_64.cmake` to configure.
Android arm64 measurement cores and runner (NDK r29 / API 21):

```sh
L1_NDK="${ANDROID_NDK_HOME:?set the NDK path}"
cmake -S . -B /private/tmp/arcdroid-l1-measure-android -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DARCDROID_ANDROID_TEST_CORE=ON \
  -DCMAKE_TOOLCHAIN_FILE="$L1_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DARCDROID_RPC_L1_VARIANTS=ON
cmake --build /private/tmp/arcdroid-l1-measure-android -j3 --target rpc_runner \
  arcdroid_rpcmeasure_libretro \
  arcdroid_rpcmeasure_l1_sa_linear_libretro \
  arcdroid_rpcmeasure_l1_sa_hint_libretro \
  arcdroid_rpcmeasure_l1_sa_mirror_libretro \
  arcdroid_rpcmeasure_l1_sa_way0_hint_libretro \
  arcdroid_rpcmeasure_l1_sa_way0_mirror_libretro \
  arcdroid_rpcmeasure_l1_off_libretro
```

`sa_linear`, `sa_hint`, `sa_mirror`, `sa_way0_hint` and `sa_way0_mirror` set both SA caches to 0/1/2/3/4 while keeping
the other frozen gates. `off` forces every cache linear for CG-10. Files use
those target names plus the platform library suffix (Android additionally
`_android.so`). Run each with the runner from this revision and the unchanged
frozen recipe. Selection on 031, warm and ldm-stm8, and fresh confirmation ABBA,
belong to Fable; selection evidence is never reused for confirmation. Default
measurement/shipping identity excludes these deliberate variant cores.

Counter ABI revision 3 uses versioned snapshot symbols plus revision/size
arguments. A runner/core pair must come from one revision; compare the base
with its own runner and the candidate with its own runner. JSON remains schema
2 with scalar names appended and histogram offsets changed in the binary ABI.

For local correctness, `ARCDROID_RPC_L1_TEST_GATE=0|1|2|3|4` changes test cores only;
`ARCDROID_RPC_L1_POISON=ON` poisons test hints only. Default measurement and
shipping cores ignore both options. `rpc-l1-corpus-<variant>` constructs isolated test
cores for default/linear/hint/mirror/way0-hint/way0-mirror and hint-poison variants, with the retained reference enabled,
and runs 25 immutable goldens, all calibration intervals and observation plans.
`tests/rpc_l1_corpus_test.py BUILD --variants 2 poison` selects a smaller rerun.
CTest registers all nine variants independently so correctness comparisons run
in parallel, with unique temporary objects, libraries, depfiles and save roots.
Every variant retains the `slow` label (full PR coverage on `host`, full nightly
coverage, excluded on push). `rpc-l1-corpus-count` pins the complete partition;
`tests/rpc_l1_corpus_count_test.py BUILD --results BUILD/Testing/Temporary/LastTest.log`
also checks the completed comparisons and counts from a full run.
The PR `host` job partitions its ordinary CTest selection across four runners,
with two or three corpus variants per runner. Run
`tools/check-host-pr-shards.py --build BUILD --self-test` to check exact-once
placement and workflow failure propagation. The
existing emitter, profiling and ordinary-route legs run once; push and nightly
retain their unpartitioned selections. The final check remains named `host`. The sanitizer PR selection uses three
runners with the same exact-once guard (`--suite sanitizers`); its final check
remains named `host-sanitizers`. Native profiling/emitter steps run on PR shard 0 and the dependent route/baseline
steps on shard 3; sanitizer integration steps run on shard 2. Both suites use a
single unpartitioned runner for push and nightly.


The primary sanitizer suite uses its actual Debug cores. The secondary full
L1 gate corpus also exercises sanitized RPC objects and the unchanged firmware hash helper at -O2, retaining every
preprocessor, ABI and sanitizer flag and all assertions. This supplies a second
optimization level while avoiding five repetitions of the 485-million-attempt
Debug SA calibration. These temporary correctness cores are never measurement
cores and never decide benefit.

## M2b lockstep core and v1 driver

The opt-in `ARCDROID_RPC_LOCKSTEP=ON` builds the non-installed host core
`arcdroid_rpclockstep_libretro`; Android never builds it. It has capability bit
32. All other test cores export explicit refusing sink/status/End stubs, with
bit 32 absent. Shipping exports no lockstep API. R-M2b-S1's measurement exception
is confined to the enumerated session stubs; the named hot objects stay equal.

```sh
cmake -S . -B /private/tmp/arcdroid-m2b-host -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DARCDROID_RPC_LOCKSTEP=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /private/tmp/arcdroid-m2b-host -j3
ctest --test-dir /private/tmp/arcdroid-m2b-host -R '^rpc-lockstep-' --output-on-failure -j3
python3 -B tools/rpc_lockstep_run.py \
  --baseline-runner /private/tmp/arcdroid-m2b-host/rpc_runner \
  --baseline-core /private/tmp/arcdroid-m2b-host/arcdroid_rpclockstep_libretro.dylib \
  --candidate-runner /private/tmp/arcdroid-m2b-host/rpc_runner \
  --candidate-core /private/tmp/arcdroid-m2b-host/arcdroid_rpclockstep_libretro.dylib \
  --plan /private/tmp/arcdroid-m2b-host/generated/rpc-lockstep/ac-h2-0.arcplan \
  --system /private/tmp/arcdroid-m2b-host/generated/rpc-lockstep \
  --model 0 --mode strict --density every-instruction --frames 1
```

Supply schedule/configuration SHA-256 inputs with `--schedule-hash` and
`--configuration-hash`; these must identify the complete agreed input set.
The default plan hash is SHA-256 of the plan bytes. For two revisions, supply
two explicit runner/core pairs. Cross-quota and observer modes require the
reviewed v1 projections; a discovered oracle disagreement is reported under
AC-8, never silently suppressed. `--hash-only` limits reports to hashes.

The runner also accepts `--lockstep-stream`, `--lockstep-role`,
`--lockstep-density`, `--lockstep-mode`, `--lockstep-p1`, `--lockstep-detail A:B`,
`--lockstep-stop-after K`, `--lockstep-key-context GENERATION:EPOCH`, and the
explicit plan/schedule/configuration hashes. A:B and K are emitter instruction
ordinals; stop-after ends after the containing frame. Context zero is a
wildcard. The driver carries the original generation and epoch into a rerun,
retains its compact prefix and requests detail only at the mismatching attempt.
Each peer has a separate mutable save directory. The header model defaults to
the plan model; `--model` explicitly overrides it.
The driver uses POSIX FIFOs and bounded Windows temporary files, closes its
readers and terminates only its own child handles on failure. A detail rerun
must reproduce the original compact groups and matching prefix before its leaf
finding is accepted. Changed or earlier divergence is `non-deterministic`.

Full audit checks all fixed 64 MiB RAM plus up to 2 MiB VRAM at P3 against an
independent log-applied shadow. It cannot establish a write-and-restore between
P3s. R-M2b-S2 budget-fatal probes are **injected**, only in the lockstep class;
bad-mode is guest-reachable, and clock-time overflow remains unexercised.
All stream scratch is deleted after a run. Private-title streams contain data
values: keep them in private scratch, delete them, and never upload them.
Generated tables and authored fixture bytes belong in build trees, never git.
Delete `/private/tmp/arcdroid-m2b-*` intermediate trees when finished.
