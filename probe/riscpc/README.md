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

## General monitor boot (run-4 R4-1)

Cold firmware sessions attach an immutable project-authored FileCore L helper
on drive 1. `boot/!Boot` runs `LoadModeFile` on `boot/Multisync` using normal
ADFS/FDC reads. CMake embeds these source assets; the MDF keeps LF lines and
the Obey uses CR lines. The MDF header records published timing provenance.
It describes a multisync display with TV-rate low geometries and standard
VGA/SVGA desktops; ScreenModes derives each hardware-legal pixel depth.
No third-party MDF, firmware patch or manually selected CMOS mode is used.

The same Obey runs the authored numbered-text BASIC `BootEnv` through the
guest's ordinary FileSwitch/IDE path. It creates `ADFS::4.$.Choices` and
`Choices.Boot.Tasks`, sets the documented Choices variables and Boot directory,
path and Tasks variables, and enumerates installed startup objects with
OS_GBPB 9 before invoking each through normal `Filer_Run`. Enumeration is
bounded to 256 objects; no workload names occur in the helper. This supplies
session-owned general preferences and normal installer startup storage,
with no supplied boot/resource bytes. Sources are the RISC OS
[boot application](https://www.riscos.com/support/developers/prm/bootapp.html)
and [FileSwitch](https://riscos.com/support/developers/prm/fileswitch.html) PRMs.
The Obey parameter is 1 for an attached IDE, 0 otherwise; no IDE means MDF only.

The core's general launch prefix uses the ordinary PS/2 keyboard: F12 at
14 virtual seconds, `Obey ADFS::1.$.!Boot 1` at 15 s for IDE sessions, Return
to desktop at 25 s.
Its events are logged separately as `boot-N` rows. Private scripts must have
no input before 26 s (origin/checkpoint-only events are allowed). All times
remain relative to reset; the prefix runs again on reset. Physical input is
ignored during this prefix and throughout scripted replay. This prefix and
these assets are identical for all cold firmware/model/content sessions.
Factory CMOS capture and synthetic authored ROMs omit the helper and prefix.
The drive-description CMOS field reflects the attached second floppy; monitor
configuration stays firmware-owned. A guest error or missing mode acceptance
is a failed qualification, even if the runner footer says COMPLETE.

For private sound inspection, the headless runner accepts `--wav=FILE`. It
writes checked 48 kHz stereo signed-16 PCM and `FILE.csv` with callback sample
counts, nonzero pairs, peaks and clipping counts. Callback and core CSV frame
numbers are zero-based; numbered PNG captures use the completed-frame count
(PNG 2500 corresponds to CSV frame 2499). This adds frontend I/O
cost and is a diagnostic run, not performance acceptance evidence. Nonzero
PCM alone does not establish correct audible game sound or frontend delivery.
