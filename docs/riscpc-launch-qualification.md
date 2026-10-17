<!-- SPDX-License-Identifier: GPL-2.0-only -->

# R3c-3b qualification commands

These are candidate qualification commands, not evidence of promotion. Launch
and prompt cells remain disabled in the shipping table. Rebase on R3c-2 run H
before qualifying its disc/error profiles. Every save below is a copy; never run
against the source pair stores. Keep all outputs under `.evidence/`.

Set `ROM_DIR`, `BOOT_ARCHIVES`, and `PAIR_STORES` to the private locations in
Fable's work order. `ROM_DIR` contains `ROM350`, `ROM360`, `ROM370`, `ROM371`;
`PAIR_STORES` contains the seven original `q-<version>-<cpu>` save directories.
Set `QUAL_PAIR_STORES` to a separate root of disposable copied `q-*` directories
whose guest drive configuration Fable has admitted for this qualification.
Every matrix run copies those again; neither root is a run destination.
Use a new `RUN_ID` for each matrix. Use copied, admitted guest configurations
that expose the attached drive 1. The supplied one-drive configurations report
Bad drive for that drive and cannot establish delivery. In the two local private
pairs, ordinary guest `Configure Floppies 2` on disposable copies, followed by a
cold load, enabled the frozen gate/controller route. No source CMOS defaults,
gate bytes or attachment policy were changed. Fable must adjudicate the
production configuration policy before promotion.

## Seven pairs and four subjects

```sh
export RUN_ID=run-01
while read -r os cpu layout; do
  for subject in returning desktop blocked prompt; do
    for cells in 0 3; do
      python3 -B tests/rpc_launch_qualification.py \
        --runner build/host/rpc_runner \
        --core build/host/arcdroid_rpcplan_libretro.dylib \
        --firmware "$ROM_DIR/ROM$os" --archives "$BOOT_ARCHIVES" \
        --cpu "$cpu" --layout "$layout" --case "$subject" --cells "$cells" \
        --copy-save "$QUAL_PAIR_STORES/q-$os-$cpu" --fresh-hd \
        --content-id 0123456789abcdef0123456789abcdef01234567 \
        --session 1 --frames 14000 --dump-frame 13900 \
        --work ".evidence/r3c3b-$RUN_ID/$os-$cpu-$subject-$cells"
    done
  done
done <<'PAIRS'
350 arm610 ro350
360 arm710 ro360
360 arm7500 ro360
370 sa110 ro370
371 arm710 ro370
371 sa110 ro370
371 arm7500 ro370
PAIRS
```

First run a diagnostic copy with `--trace`. Expect role-1 events 1,2,3 before
the exact StartTask; a returning command has 4,5,8; a live desktop handoff has
4,6,8; a blocked command has no invented return. A pending qualified ReadLine
offers manual interaction. Ordinary input and authorised in-set swaps retain
the token. Compare cells 0/3 for identical command effects and guest execution;
their observation/notice state can differ. Trace files are private.

The local configured 3.50/ARM610 and 3.71/SA-110 copies execute the actual
controller. Returning commands distinguish return from a live desktop child;
blocked commands expire unknown. A prompt accepts manual input while retaining
the attempt. The returning subject also maintains an independent numeric guest
variable, `ArcAuth$Runs`, for duplicate checks: invoke the unchanged gate again
and inspect it before/after. A second gate CALL returns the duplicate summary;
it must not produce another role-1 consumption or command execution. Runner exit
zero or a role-0 marker alone does not establish delivery.

## Reset and media cases

`rpc_runner --preparation-reset-at N` exercises the preparation transaction
without changing the R3d-owned `retro_reset` dispatch. For the precise synchronous
cuts, use the existing test advance API through the runner:

```sh
# Choose a frame before consumption from a diagnostic run on this pair.
# The instruction-boundary search is bounded to 5 virtual seconds and fails
# if it misses the requested phase. It is not a cost measurement.
python3 -B tests/rpc_launch_qualification.py \
  --runner build/host/rpc_runner --core build/host/arcdroid_rpcplan_libretro.dylib \
  --firmware "$ROM_DIR/ROM$os" --archives "$BOOT_ARCHIVES" \
  --cpu "$cpu" --layout "$layout" --case blocked --cells 3 --session 1 \
  --copy-save "$QUALIFIED_PAIR_SAVE" --content-id 0123456789abcdef0123456789abcdef01234567 \
  --reset-stage consumed --reset-stage-frame "$BEFORE_CONSUMPTION_FRAME" \
  --work ".evidence/r3c3b-$RUN_ID/reset-consumed" --frames 6000
```

Repeat with a fresh destination for `--reset-stage prepared --reset-stage-frame
0`, and `--reset-stage dispatch` at the same pre-consumption frame. Reset an
actually executing blocked subject with `--reset-frame N` after its dispatch.
Use the desktop subject and `--reset-stage handoff --reset-stage-frame N` after
handoff. The two local pairs passed those cuts, including the narrower
consumed/dispatch boundaries. A no-op must be attached before resumed guest
execution; no fresh automated token is issued. The production-source fault and
stale-status harness remains:

```sh
ctest --test-dir build/host --output-on-failure -R '^rpc-launch-preparation$'
```

For an in-flight authorised swap, pass `--media "$TWO_DISC_PLAYLIST"` and
`--disk-control "$SWAP_SCRIPT"` with a script of the runner's existing format:
`N eject`, `N+1 select 1`, `N+2 insert`. Use actual decimal frame numbers in
the script. No helper replacement or second token is permitted. A failed
rollback must stop the session. R3d's promoted frontend route still needs its
integration repeat after that slice merges.

## 084 install and cold product relaunch

`INSTALL_084` is the work order's `.work/s62/ac5/i7-084` directory. The
installer's existing input script is reused only for the baseline manual
installation route. Remove its manual disk-control script to exercise automatic
exchange, including the installer's request for disc 2. Preparation must validate
the resulting product independently of the installer returning.

```sh
python3 - "$QUAL_PAIR_STORES/q-371-sa110" ".evidence/r3c3b-$RUN_ID/084-auto" <<'PY'
from pathlib import Path
import shutil, sys
root=Path(sys.argv[2]);root.mkdir(parents=True,exist_ok=True)
shutil.copytree(sys.argv[1],root/'save')
old=root/'save/arcdroid/hd-riscpc/0123456789abcdef0123456789abcdef01234567'
if old.exists():old.rename(root/'retained-hd')
PY
python3 - "$INSTALL_084/plan.arcplan" ".evidence/r3c3b-$RUN_ID/084-auto/install.arcplan" <<'PY'
from pathlib import Path
import sys
rows=Path(sys.argv[1]).read_text().splitlines()
drop={'disk_control','observer_cells','observer_forms','observer_disc_devices'}
rows=[r for r in rows if r.partition('=')[0].strip() not in drop]
rows+=['observer_cells=255','observer_forms=14','observer_disc_devices=3']
Path(sys.argv[2]).write_text('\n'.join(rows)+'\n')
PY
build/host/rpc_runner --core build/host/arcdroid_rpcplan_libretro.dylib \
  --plan ".evidence/r3c3b-$RUN_ID/084-auto/install.arcplan" \
  --system "$QUAL_SYSTEM" --save ".evidence/r3c3b-$RUN_ID/084-auto/save" \
  --frames 14400 --disk-writes shadow \
  --dump-frame 14000 ".evidence/r3c3b-$RUN_ID/084-auto/install.ppm"
```

This command preserves the baseline manual installation start. Separately
exercise the actual controller with the explicit application choice below; the
JFD parser uses the decoded sector view and retains structural ambiguity
handling. Selecting an application is not proof that installation completed.
Use run H's qualified disc/error settings after rebasing; the overrides above
are test candidates.

```sh
python3 -B tests/rpc_launch_qualification.py \
  --runner build/host/rpc_runner --core build/host/arcdroid_rpcplan_libretro.dylib \
  --firmware "$ROM_DIR/ROM371" --archives "$BOOT_ARCHIVES" \
  --cpu sa110 --layout ro370 --media "$MEDIA_084" \
  --action application --index 0 --cells 255 --session 84 \
  --content-id 0123456789abcdef0123456789abcdef01234567 \
  --copy-save "$QUALIFIED_PAIR_SAVE" \
  --work ".evidence/r3c3b-$RUN_ID/084-controller" --frames 14000 --dump-frame 13900
```

The explicit index is the user's selected candidate for this leg, not a
production title rule. Combine a separately reviewed installation input script
with the controller leg when appropriate; do not reuse screen coordinates
against a different startup state without inspecting them.

Once installation has produced an actual application, set `PRODUCT_TARGET` to
its explicit `ADFS::4.$.<parent>.!Application` path (or a root application).
The product request below is checked against current directories, allocation,
`!Run`, and a bracketing store identity before token issuance. The current plan parser does not certify immutable
ordered media, so boot-record publication remains unavailable on this route.
A missing/edited
product refuses; an installer return alone never admits it.

```sh
python3 -B tests/rpc_launch_qualification.py \
  --runner build/host/rpc_runner --core build/host/arcdroid_rpcplan_libretro.dylib \
  --firmware "$ROM_DIR/ROM371" --archives "$BOOT_ARCHIVES" \
  --cpu sa110 --layout ro370 --media "$MEDIA_084" \
  --action product --target "$PRODUCT_TARGET" --cells 3 --session 2 \
  --content-id 0123456789abcdef0123456789abcdef01234567 \
  --copy-save ".evidence/r3c3b-$RUN_ID/084-auto/save" \
  --work ".evidence/r3c3b-$RUN_ID/084-cold" --frames 9000 --dump-frame 8500
```

## Native Windows and Odin3

On native Windows, configure with the repository's UCRT64 environment, then:

```powershell
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows
ctest --test-dir build/windows --output-on-failure -R '^(rpc-launch-.*|rpc-observers|rpc-session-orchestration)$'
build/windows/rpc_runner.exe --core build/windows/arcdroid_rpcplan_libretro.dll --plan "$Plan" --system "$System" --save "$Save" --frames 9000 --timing "$Timing"
& "$RetroArch" --verbose --config "$Config" --log-file "$Log" -L build/windows/arcdroid_rpcplan_libretro.dll "$Plan"
```

Generate each `.arcplan` with native Windows paths, using the same seven rows
and copied-save recipe above; substitute `.exe` and `.dll`. In RetroArch repeat
input, in-flight swap, Restart, unload/reload and contained-fault cases. Its
config names the copied system/save directories. Production routing and ID
allocation repeat after R3d merges; the current plan route injects the ID.

Odin3 builds use the installed NDK and the non-installed plan core:

```sh
cmake -S . -B build/android-launch -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release -DARCDROID_ANDROID_TEST_CORE=ON
cmake --build build/android-launch
adb -s "$ODIN_SERIAL" shell mkdir -p /data/local/tmp/arcdroid-r3c3b
adb -s "$ODIN_SERIAL" push build/android-launch/rpc_runner /data/local/tmp/arcdroid-r3c3b/
adb -s "$ODIN_SERIAL" push build/android-launch/arcdroid_rpcplan_libretro_android.so /data/local/tmp/arcdroid-r3c3b/
adb -s "$ODIN_SERIAL" push "$ODIN_PAIR_DIRECTORY/." /data/local/tmp/arcdroid-r3c3b/
adb -s "$ODIN_SERIAL" shell chmod 755 /data/local/tmp/arcdroid-r3c3b/rpc_runner
adb -s "$ODIN_SERIAL" shell '/data/local/tmp/arcdroid-r3c3b/rpc_runner --core /data/local/tmp/arcdroid-r3c3b/arcdroid_rpcplan_libretro_android.so --plan /data/local/tmp/arcdroid-r3c3b/case.arcplan --system /data/local/tmp/arcdroid-r3c3b/system --save /data/local/tmp/arcdroid-r3c3b/save --frames 9000 --timing /data/local/tmp/arcdroid-r3c3b/timing.csv'
adb -s "$ODIN_SERIAL" pull /data/local/tmp/arcdroid-r3c3b/timing.csv .evidence/
```

`ODIN_PAIR_DIRECTORY` is a fresh private copy with device-absolute plan paths.
For the native RetroArch leg, install the same plan core and `.info` through
Fable's normal test-core deployment, open `case.arcplan`, and repeat the visible
matrix under normal performance/fan settings. No device result is claimed here.

Cost qualification uses tracing off and identical helper execution with cells
0 versus 3. Determine phase windows from a separate diagnostic copy, then use
those windows in repeated paired timing runs: dispatch/Desktop p50/p95 at most
10% over cells-off; blocked/expired at most 3%; prompt at most 100% only while
the bounded read watch is live, then 3%. Never count offline preparation frames
as launch cost. For no-launch hot cost use D-180's pinned pre-observer core and
workload, `--warmup-seconds 120 --measure-seconds 600`, with the 3% ceiling.
Build success or a Mac timing run cannot establish these Odin3 ceilings.
