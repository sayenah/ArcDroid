# Disposable RiscPC trial

This directory is the unit P feasibility probe on `probe/riscpc-trial`.
It is never merged into ArcDroid. The work order and thresholds are in
`docs/handoff.md` and `docs/riscpc.md`. No feasibility pass is implied by a
successful build or a completed headless run.

The verified RPCEmu 0.9.5 tarball and pristine unpacked tree precede the
probe patches. Modified upstream files retain licence notices and marked
`ARCDROID-PROBE` comment blocks. Probe sources are GPL-2.0-only. JFD and
hash sources are compiled by reference; the marked JFD presentation copy
preserves ArcDroid's controller-facing IDs.

Host build and authored mechanism checks:

```sh
cmake -S probe/riscpc -B probe/riscpc/run/host -DCMAKE_BUILD_TYPE=Release
cmake --build probe/riscpc/run/host -j 2
ctest --test-dir probe/riscpc/run/host --output-on-failure
```

`expectations.md` states authored expectations. Set `PROBE_SANITIZERS=ON`
in a separate host build for ASan/UBSan. The headless runner loads the
libretro shared library, supplies option values and accepts an emulated
duration up to 3600 seconds. Pacing is `unthrottled`, `realtime` or
`staggered`; only the frontend uses host time. Script timestamps are
microseconds from machine reset, ordered stably and delivered at the next
core boundary of at most one virtual microsecond. Scripts use normal
set-2 scan codes (extended codes are 256 plus the second byte), relative
mouse deltas, button masks, disc indices and checkpoints. End/reset clears
held inputs. Per-model guest mouse devices remain quadrature or PS/2.

## Firmware factory baseline (run-3 R3-1)

Only the four accepted firmware SHA-1s load. Ordinary firmware sessions
require a 256-byte cold CMOS and its exact SHA-1. Capture it independently
for every firmware/model pair; never use RPCEmu's bundled CMOS or manually
configure a monitor. `tools/capture_cmos.py` holds Delete (`E0 71`) through
the normal keyboard path from virtual time zero through 12 seconds,
starting with all CMOS bytes zero. It supplies an exclusive scratch IDE,
checks firmware write evidence and records custody. Its result is a
capture requiring visual inspection of the completed firmware desktop.

```sh
python3 probe/riscpc/tools/capture_cmos.py \
  --runner probe/riscpc/run/host/probe_runner \
  --core probe/riscpc/run/host/arcdroid_rpcprobe_libretro.dylib \
  --firmware /external/combined-firmware --model SA110 \
  --out probe/riscpc/run/factory-capture
```

Use `rpcprobe_cmos_baseline=/absolute/path/cmos-final.bin` and
`rpcprobe_cmos_sha1=<recorded SHA-1>` for subsequent cold sessions. Only
drive description, existing model mouse type and deterministic RTC fields
are applied at cold startup. Factory capture applies none of those
configuration changes. The board represents an unprotected CMOS reset
latch, VGA monitor ID 0 and fitted 2 MiB VRAM on RiscPC models. A7000 uses
shared RAM and its native PS/2 ports. Firmware chooses the screen mode.

`rpcprobe_diagnostics=enabled` records bounded numeric boot-device and
mode-request logs. The mode observer reads selectors passively: it never
uses guest I/O accessors, populates a TLB, raises a fault or alters guest
registers. Diagnostics are separate from acceptance performance runs.
Mode rows contain `emu_ns,swi,a,b,c,r3,r4,r5,word_count,words...` (hex
registers/words). `a,b,c` are R0/R1/R2 for OS_ScreenMode, 0/mode/R1 for
Wimp_SetMode or OS_CheckModeValid, and service/mode/monitor for the read-only
mode-service observer. Services expose monitor type, bandwidth and fitted
video RAM in R3/R4/R5. Selectors are bounded to 32 words, ending at their
variable-list terminator. No guest text is written.

External firmware/media stay outside the repository. All private trial
outputs, scripts, IDE images, CMOS captures and screenshots belong under
the ignored `run/` directory. The current path-valued legacy options are
for the headless runner. RetroArch's fixed system-directory configuration,
Windows/Android qualification and complete AC-8/AC-9 tooling remain pending
ordered AC-6 completion; do not use this README as their runbook.
