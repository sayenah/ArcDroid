# Unit P platform evidence runbook (run 12)

This disposable branch is never merged. Keep every runtime input/output private.
Run 12 does **not** qualify the final >=600 s AC-4 replay or AC-8 performance.
110's raster failure awaits the Founder; do not run acceptance legs until the
rendering decision is reconciled, the final core is frozen and AC-7 supplies
qualified model-wide rates. No rate may be lowered to make performance pass.

## 1. Freeze and inspect the build

Builds are independent of production CMake. From the repository root:

```sh
cmake -S probe/riscpc -B probe/riscpc/run/host -DCMAKE_BUILD_TYPE=Release
cmake --build probe/riscpc/run/host -j2
ctest --test-dir probe/riscpc/run/host --output-on-failure
cmake -S probe/riscpc -B probe/riscpc/run/windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchain-mingw-w64-x86_64.cmake"
cmake --build probe/riscpc/run/windows -j2
cmake -S probe/riscpc -B probe/riscpc/run/android -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21
cmake --build probe/riscpc/run/android -j2
LLVM_READELF=/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-readelf sh tools/check-android.sh probe/riscpc/run/android/arcdroid_rpcprobe_libretro.so
```

Release flags follow ArcDroid's CMake/NDK release configuration (`-O3 -DNDEBUG`,
no strict aliasing, hidden core symbols, version script, 16 KB ELF LOAD alignment).
MinGW statically links libgcc for the core; Windows uses the system UCRT.
Do not strip/change optimisation between evidence and installation.
Record SHA-256 of source files, core/runner/mechanism binaries, build logs,
compiler/NDK versions and full command lines in an explicit private inventory.
Only copy generated binaries/source/tooling to the platform evidence directory.
Original private ROM/game/archive bytes never enter this repository, including
its ignored run directory. Session-derived IDE baselines are permitted there.

## 2. Authored cross-platform mechanism traces (no private inputs)

Run in a writable private directory. The harness creates/removes an authored
CMOS file; no firmware, IDE baseline or game is needed. Capture exit status
and **all** stdout/stderr. Never filter/sort individual state lines.

macOS (use absolute binary path if the current directory differs):

```sh
./probe_mechanisms > macos.trace 2> macos.stderr
```

Windows PowerShell, binaries copied from the same frozen build:

```powershell
& .\probe_mechanisms.exe 1> windows.trace 2> windows.stderr
$LASTEXITCODE | Set-Content windows.exit
& .\probe_controller.exe 1> controller.trace 2> controller.stderr
$LASTEXITCODE | Set-Content controller.exit
& .\probe_lifecycle.exe 1> lifecycle.trace 2> lifecycle.stderr
$LASTEXITCODE | Set-Content lifecycle.exit
```

PowerShell 7.4+ preserves native redirected bytes; older shells can use
`cmd /c "probe_mechanisms.exe >windows.trace 2>windows.stderr"` to avoid UTF-16
redirection. Preserve original raw bytes; comparison normalizes CRLF only.
The lifecycle executable needs the frozen core DLL in the same directory.

Odin3, commands for the orchestrator (replace the build directory only):

```sh
ADB=/opt/homebrew/share/android-commandlinetools/platform-tools/adb
"$ADB" shell mkdir -p /data/local/tmp/rpcprobe-p12
"$ADB" push probe/riscpc/run/android/probe_mechanisms /data/local/tmp/rpcprobe-p12/
"$ADB" push probe/riscpc/run/android/probe_runner /data/local/tmp/rpcprobe-p12/
"$ADB" push probe/riscpc/run/android/arcdroid_rpcprobe_libretro.so /data/local/tmp/rpcprobe-p12/
"$ADB" shell 'chmod 700 /data/local/tmp/rpcprobe-p12/probe_mechanisms /data/local/tmp/rpcprobe-p12/probe_runner'
"$ADB" shell 'cd /data/local/tmp/rpcprobe-p12 && ./probe_mechanisms >arm64.trace 2>arm64.stderr; rc=$?; echo "$rc" >arm64.exit; exit "$rc"'
"$ADB" pull /data/local/tmp/rpcprobe-p12/arm64.trace probe/riscpc/run/arm64.trace
"$ADB" pull /data/local/tmp/rpcprobe-p12/arm64.stderr probe/riscpc/run/arm64.stderr
"$ADB" pull /data/local/tmp/rpcprobe-p12/arm64.exit probe/riscpc/run/arm64.exit
python3 probe/riscpc/tools/compare_traces.py probe/riscpc/run/macos.trace probe/riscpc/run/arm64.trace --output probe/riscpc/run/arm64-comparison.json
```

Require exit 0 and the complete final `checks=... failures=0` line, all four
models and identical state traces. Use the same comparator for Windows.
Missing platform execution is MISSING; a build is not cross-platform trace proof.
For a no-ROM load diagnostic on Odin3, create a fresh writable output directory
and run `probe_runner CORE 2 unthrottled rpcprobe_authored=enabled
rpcprobe_model=SA110 rpcprobe_rate=20 rpcprobe_output=OUTPUT`. Pull its raw CSV,
`csv-cost.csv`, `events.csv`, core footer and stderr. This is not RetroArch-load
or sustained-performance evidence.

## 3. RetroArch layout and initial state

Use normal Load Core then Load Content. Windows: install the frozen DLL.
Odin3: copy the frozen `.so` to RetroArch's downloads, use Install or Restore a
Core, then Load Core. Do not replace/stop another session's running instance.
Record actual loaded build/custody and the RetroArch version/video/audio driver.
Use the device's configured system directory (commonly `/storage/emulated/0/BIOS`).

Layout for each private workload (`trial-a` ... `trial-d`):

```text
<system>/arcdroid/firmware/ROM371
<system>/arcdroid/rpcprobe/trial-a/script.txt
<system>/arcdroid/rpcprobe/trial-a/cmos.bin
<system>/arcdroid/rpcprobe/trial-a/cmos.sha1
<system>/arcdroid/rpcprobe/trial-a/baseline.hdf
<system>/arcdroid/rpcprobe/trial-a/baseline.sha1
```

Content `.jfd` or `.m3u` stays in the player's external private content directory;
playlist paths resolve beside the playlist, in disc order. Use the private report
for the approved four-trial mapping/baseline/CMOS/script inventory (both 084
payloads need separate recorded runs). Never rename a title in public source.
SHA-1 sidecars contain exactly the baseline/CMOS digest; freeze SHA-256 in custody
too. Every load creates an exclusive `run-NNNNNN` subdirectory and a fresh scratch
IDE clone. No state file support is claimed: a warm state is a reproducible
scripted guest scene from immutable cold inputs, not a save-state shortcut.

Core options, set through RetroArch's core-options UI:

- `rpcprobe_firmware`: verified `ROM371` (other pairs only for smoke checks).
- `rpcprobe_model`: the qualified model from the private report.
- `rpcprobe_rate`: the **frozen model-wide empirical rate**. Run 12 does not
  supply a qualified rate when progress/timer/cadence evidence is missing.
- `rpcprobe_workload`: the corresponding `trial-*` directory.
- `rpcprobe_ide=enabled`; `rpcprobe_boot=standard` (single floppy drive 0).
- `rpcprobe_measurement=enabled` for every acceptance leg.
- `rpcprobe_diagnostics=disabled`, `rpcprobe_register_trace=disabled`,
  `rpcprobe_activity=disabled` for performance. Diagnostic collectors are excluded.
- `rpcprobe_expected_seconds`: the predeclared **total** guest duration. Choose
  2100/2400/3000/3600 when setup precedes the measurement origin; it only declares
  footer duration and does not change guest event order or stop the machine.

Scripts timestamp normal input in microseconds from guest origin 0, stable
file order. Checkpoints and key down/up, deltas/buttons and swaps use the real
input devices; held inputs clear at `end`/reset/unload. Replay disables physical
input. Use the already qualified private scripts as cold prefixes, then author
and independently inspect representative **active** segments for screening and
sustained runs. Short run-11 scripts ending at150–400 s are not 30-minute gameplay
scripts. Do not relabel idle/menu/paused tails as active gameplay.

## 4. Declare measurements before launching

Write an immutable JSON declaration; hash it before launch and retain a copy of
that hash in the run dispatch ledger. The manifest refers to that file/hash.
It contains:

```json
{
  "kind": "sustained",
  "measurement_origin_ns": 300000000000,
  "requested_ns": 2100000000000,
  "script_sha256": "REPLACE_WITH_ACTUAL_DIGEST",
  "predeclared_exclusions": [],
  "phases": [{"start_ns": 900000000000, "end_ns": 2100000000001, "phase": "active-scene"}]
}
```

The numbers are an example: setup at0–300 s, 10-minute warm-up at300–900,
20 minutes measured at900–2100. Choose actual script phases and total duration
before running. Labels must represent matched activities in both five-minute
windows. A single label cannot conceal level/scene/load changes. Declare any
pause/loading exclusions in advance, with reasons; do not trim bad frames later.
Independent gameplay inspection remains required even with exact script delivery.

Screen at model-relevant rates for300 measured guest seconds after the same
recorded warm scene, forward then reverse order. Record cold inputs, exact warm
checkpoint/state evidence and the rate order. At the frozen rate, run three
independent sustained legs per workload/platform (initial passing run plus two
repeats), each with600 s warm-up +1200 measured. Stop early on decisive failure
and preserve incomplete/failure evidence. Do not report completion after an early
exit. Exit/Close Content normally **after** the declared guest duration so that
the core writes its final footer. A process kill/crash cannot create completion.
Record real wall interval independently; slow motion/fast forward/frame skipping,
rewind/run-ahead and suppressed frames invalidate the leg.

## 5. Collect raw evidence and output verification

Per core session collect `frames.csv`, `csv-cost.csv`, `events.csv`,
`media-custody.csv`, `run-footer.json`, final CMOS and scratch IDE hash. Raw exec
cost excludes the primary CSV append; `csv-cost.csv` supplies its measured tail
cost and the evaluator adds it. Tail records are deferred to the next core call,
which includes that record's write in its execution cost. Unload flushes the
last tail before the footer; a truncated/missing cost row is MISSING. Whole-call
on/off pairs also measure the clock/return boundary and overall observer cost.
Do not omit that overhead qualification.

Also collect RetroArch verbose log/statistics, presentation counters for the
exact measurement window, legitimate repeated-frame classification, AV-info
mode changes, independent output observations, hardware settings, temperatures
and frequencies throughout warm-up/measured time. Odin3: retain sampled thermal
zone names/values, CPU frequency paths/values and actual fan/performance mode;
Windows: retain the corresponding monitor's raw samples and power settings.
Record unavailable sensors as missing. Never use invented numeric zeros.

Ordinary RetroArch verbose logs/FPS averages may **not** expose missed
presentation counts or distinguish legitimate repeats. If absent, keep frontend
telemetry MISSING: do not infer zero misses from an empty ERROR search or good
average FPS. The evaluator accepts an actual statistics export with raw capture
hash/method/interval, not counts typed into the manifest. Fable must independently
check the export against its captured statistics/log. No unverified generic log
regex/parser is represented as RetroArch telemetry support.

Independent audio observation JSON records exact interval, observer, output
device, method, core-caused underrun/overrun counts and audible discontinuities.
Listen/verify captured output on the Odin3/Windows output device over the active
segment; generated PCM/hash counts alone cannot prove perceptual quality.
Retain any loopback recording and device/frontend audio statistics used to
attribute a discontinuity. Missing attribution or listening remains MISSING.

Each telemetry raw JSON has `start_ns`, `end_ns`, and `method`. Frontend additionally
has `source="RetroArch statistics export"`, `expected_presentations`,
`missed_presentations`, `legitimate_repeated_frames`, `capture_file`,
`capture_sha256`, `repeat_classification_method`. Audio additionally has
`observer`, `output_device`, `core_caused_underruns`, `core_caused_overruns`,
`audible_discontinuities`. Platform has `device`, `temperatures`, `frequencies`,
`performance_settings`. Gameplay has `observer`, `checkpoints`,
`substantial_active_gameplay`. Missing records are omitted, never synthesized.

## 6. Recompute and retain custody

Manifest sections `declaration`, `footer`, `csv_cost`, `phases`, `frontend`,
`independent_audio`, `platform`, `gameplay`, `overhead`, `custody` each reference
`{"raw_file":"relative-file","sha256":"actual SHA-256"}`. Paths are relative
 to the manifest. The overhead record points at a complete `overhead.json` beside
its paired raw subdirectories. Custody raw JSON records `core_sha256`, `core_options` and `input_sha256`;
platform telemetry also records `host_system`/`host_machine`. The evaluator
compares this binding to the overhead collector instead of accepting a manifest
boolean. Final overhead must match the actual binary,
platform and workload, not only this Mac's authored dry run.

```sh
python3 probe/riscpc/tools/label_phases.py --csv RUN/frames.csv --declaration RUN/declaration.json --script RUN/script.txt --events RUN/events.csv --output RUN/phases.csv
python3 probe/riscpc/tools/run_summary.py RUN/frames.csv --output RUN/summary.json
python3 probe/riscpc/tools/evaluate_ac8.py RUN/frames.csv --manifest RUN/manifest.json --json RUN/criteria.json
python3 probe/riscpc/tools/evaluate_suite.py PRIVATE/suite.json --output PRIVATE/suite-results.json
python3 probe/riscpc/tools/verify_custody.py PRIVATE/inventory.json
```

Exit0=all relevant evidence PASS; exit1=FAIL; exit2=MISSING. Summary COMPLETE
means footer/row integrity only, never visual/audio/AC8 approval. Nearest-rank
percentiles, threshold inclusivity and drift/phase comparisons are fixed in
source. AC8 p95<=.80T, p99<=.90T, p99.9<=T, fewer than3 consecutive misses,
frontend miss fraction<.001, every matched-phase mean cost within10%, drift<=.001,
zero core/independent audio errors are printed separately.

Full overhead diagnostic (3 alternating on/off pairs, identical guest AV):

```sh
python3 probe/riscpc/tools/measure_overhead.py --runner RUN/probe_runner --core RUN/CORE --seconds 5 --output PRIVATE/overhead
python3 probe/riscpc/tools/measure_overhead.py --output PRIVATE/overhead --recompute
```

For private scenes pass `--options PRIVATE/options.json` and predeclare
`--start-seconds`/`--end-seconds` for the matched active overhead window (list of identical core
options pointing at immutable external inputs). This collector needs Python on
its execution host; on Odin3 use the shipped C runner with the six generated
commands/identical sidecars and record the same manifest/exit/PID/timing/AV-ledger
fields privately. Do not label Mac-collected timings as Odin3 overhead. The
explicit5%T diagnostic bound is additional to AC8 thresholds, not a relaxation.
Host variance crossing the bound yields MISSING. It must not be clamped to zero.
The collector freezes runner/core copies in `overhead/binary` before launching.
For device evidence pulled to another host, put an `input-locations.json` beside
`overhead.json` mapping each external recorded input path to its existing local
private path. Only locations change: every original digest is rechecked. Never
copy original firmware/content into the repository or synthesize missing inputs.

`evaluate_suite.py` requires four private trial labels on Windows/Odin3 and all
12 authored workload coverage labels shown in its source, three sustained legs
each plus alternating rate screening. A missing executable workload/phase recipe
remains a coverage gap. The current libretro authored ROM is the framebuffer/
sound DMA workload; the complete mechanism harness checks other mechanisms but
is **not** itself a sustained RetroArch workload recipe for every label. Those
coverage recipes and final extended private gameplay scripts remain explicit
qualification prerequisites, not fabricated completed runs.

Suite JSON contains `frozen_model_rates`, `calibration_qualified`,
`screening_rates` per model, and `runs`: `platform` (windows/odin3), `workload`,
`model`, `rate`, `kind` (screening/sustained), `repetition`, `csv`, `manifest`;
screenings also have `order` and `warm_state_sha256`. Duplicate CSVs/identities
are refused. No cached PASS result is accepted without raw recomputation.
AC9 inventory JSON lists `artifacts` with `file`, `sha256`, `role`; preserve source,
binaries, configuration, predeclarations, scripts, cold/warm hashes, every raw
measurement, failed attempts, exact durations and verification logs. Fable
recomputes and inspects representative gameplay independently. The tool does
not confer QA, release, merge or Founder approval.
