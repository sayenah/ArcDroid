# Disposable RiscPC trial

This directory is the unit P feasibility probe on `probe/riscpc-trial`.
It is never merged into ArcDroid. The work order and thresholds are in
`docs/handoff.md` and `docs/riscpc.md`. No feasibility pass is implied by a
successful build or a completed headless run.

Current qualification uses the R10-1 single-drive IDE startup described at the
end of this file. Earlier helper/modern-boot sections retain historical
construction evidence and do not authorize those modes in the public core.

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

`tools/audio_check.py SESSION --output RESULT.json [--segment START:END]`
independently checks the WAV container, total sample count, each callback's
PCM hash against `frames.csv`, and callback peak/nonzero/clipping statistics.
It reports audible fidelity and physical output as MISSING; these require
separate listening/output evidence.

For a bounded diagnostic capture, `rpcprobe_register_trace=enabled` records
numeric VIDC palette/register and IOMD video/sound DMA writes plus frame
boundaries in `registers.csv`. Device IDs are 0 (palette index), 1 (VIDC top
byte), 2 (IOMD offset), and 3 (frame boundary/new period). This passive observer
does not read guest memory or change guest deadlines. It fails explicitly at
two million records. It is disabled by default and its extra I/O must be
excluded from performance qualification unless separately measured.

## Private standard boot disc (run-5 R5-1)

`rpcprobe_boot=standard` disables the authored helper floppy and its keyboard
prefix. The guest boots its session IDE using its own boot configuration.
The bounded trial IDE is now 126 MiB (63 sectors, 16 heads, 256 cylinders);
older 16/63 MiB trial baselines are rejected. The probe-local formatter copies
retain their ArcDroid provenance; production formatting is unchanged.
Cold reset preserves the guest's IDE CMOS setting instead of forcing it.

`tools/boot_disc.py` reads only the two approved external release archives,
verifies their exact SHA-256s and writes an exclusively created FileCore
image plus custody JSON. Acorn ARC0 ZIP load/execute words and attributes
are preserved, including filetypes; component dots map to RISC OS slash.
Three RO5-only files have names exceeding the classic ten-byte directory
limit and are explicitly recorded as omitted. All pre-5 boot resources are
retained; archive licenses/readmes remain on disc. No supplied bytes are
embedded in this source tree.

```sh
python3 probe/riscpc/tools/boot_disc.py \
  --harddisc4 /external/HardDisc4.zip --system /external/PlingSystem.zip \
  --out probe/riscpc/run/unmerged.hdf
python3 probe/riscpc/tools/disc_inventory.py \
  probe/riscpc/run/unmerged.hdf probe/riscpc/run/unmerged-inventory.json
```

The initial image has boot option off and a separate `SysUpdate` directory.
The full release tree is retained. The smaller 63 MiB construction left only
about 2 MiB free because of legacy allocation units; the larger bounded geometry
provides room for the normal guest installs.
Run the supplied `SysUpdate.!SysMerge` **in the guest**, pointing System$Dir
at the on-disc `!Boot.Resources.!System`; establish Boot$Dir and Boot$Path
at the standard !Boot directory. Its ReadMe requires this updater instead
of the Configure System Merge dialogue. Verify copied module payloads and
metadata against the source inventory; unchanged newer destination files
may be retained by the supplied updater. Configure the guest with normal
`Opt 4,2`, `Configure Boot`, `Configure FileSystem ADFS`, `Configure Drive 4`.
Capture its CMOS, inspect a cold boot, then freeze the disc and record hashes.
Each session clones the frozen image into an exclusive scratch backing.
Do not treat a runner COMPLETE footer as boot or installation qualification.

HardDisc4's normal pre-5 boot hooks currently select its Acorn AKF60 monitor
definition and 800×600 desktop. The authored MDF is not injected in standard
sessions. The current private report records the seven-pair qualification,
guest-written CMOS changes and remaining gameplay/provider qualification.
The source archives, baselines, inventories and screenshots are private trial
inputs/evidence, not approved shipping assets.

### Period ROM modules (run-6 R6-1)

For the period environment, repeat `--period-rom /external/combined-image`
four times on `boot_disc.py`, once for each approved firmware image. The
builder verifies their SHA-1s, inventories module header title/help offsets,
and applies one common union of ROM module names to both archives. A
presence-only `RMEnsure <module> 0.00` guards each original versioned
`RMEnsure ... Run/RMLoad/RMRun` load for those names. Thus existing ROM
modules stay resident and genuinely additional modules retain their original
loads. Every modified script line has an ARCDROID-PROBE R6-1 comment; custody
records before/after lines, module versions and firmware membership.
Dependency errors, non-load commands, binary payloads and metadata remain
unchanged. Archive notices stay on disc. The guest System merge follows the
same documented procedure above. No firmware bytes enter the generated source.

Run `python3 probe/riscpc/tools/test_period_boot.py` for authored policy
checks. The original boot's minimum-version requirements can conflict with
retaining period ROM modules. A constructed image or completed merge is not
a qualified boot baseline; stop and record the guest error if such a
dependency fails. No replacement module or lowered requirement is authorized
to resolve that failure.

### RetroArch trial layout

Use ordinary Load Content on `.jfd` or `.m3u` (relative playlist entries resolve beside the playlist). Combined verified firmware lives at `<system>/arcdroid/firmware/ROM350`, `ROM360`, `ROM370` or `ROM371`; select it with `rpcprobe_firmware`. Explicit external path options remain available to the headless runner.

Select `rpcprobe_workload` (`trial-a` through `trial-d`, or `authored`). Its directory is `<system>/arcdroid/rpcprobe/<workload>/` with `script.txt`, `cmos.bin`, `cmos.sha1`, and, when IDE is enabled, `baseline.hdf` plus `baseline.sha1`. Digest files contain one hexadecimal SHA-1 line. The baseline can be the standard boot disc or a qualified installed-game disc. These are private runtime inputs, never bundled. Set model/rate and enable IDE explicitly. Automatic boot selects the session IDE disc (R10-1). Physical inputs are disabled while the script is active.

Each load exclusively creates a fresh `run-NNNNNN` subdirectory for scratch IDE, CSV and diagnostics; the baseline is opened read-only and hash checked. Do not reuse a run directory as an input baseline without separately qualifying/freezing it. The core refuses missing system paths, malformed digest sidecars, unsafe workload names and invalid/missing media. No private path or decoded title is logged. The runner supports `--system=DIR` and `--content=FILE`. Authored portable integration checks:

```sh
python3 probe/riscpc/tools/frontend_check.py --runner <runner> --core <core> --output <fresh-evidence-dir>
```

### Resource-only environment (run-7 R7-1; historical startup)

The run-5 full modern boot and run-6 guarded scripts above are historical
construction/diagnostic modes. Qualification now uses **pristine archive
resources without executing their modern !Boot sequence**. Build with `--resources-only` and without
`--period-rom`, run the documented supplied `SysUpdate.!SysMerge` in the
guest, leave the root boot option at 0 (`Opt 4,0`) and use guest
`Configure NoBoot` in the construction session. Qualification starts with
each firmware/model's independently captured factory CMOS; root option 0
prevents reset dispatch even if that factory configuration says Boot. Freeze
and hash the merged image, then clone it per session. Set
`rpcprobe_boot=helper` (also the automatic workload default).

The same authored helper and fixed keyboard prefix are used for every
firmware/model/content session. Its ordinary guest commands:

1. Load the authored R4-1 `Multisync` MDF.
2. Run authored `BootEnv` to create writable Choices, PreDesk and Tasks
   directories and set the standard Boot/Choices/resource variables.
3. Run the supplied !System `SysPaths` utility with the ROM BASIC. It
   discovers UtilityModule's OS version and builds `Sys$Path` and
   `System$Path` from the installed numeric version directories in descending
   order, followed by the !System directory. This utility loads no modules.
4. Obey that !System's own !Boot. Since `System$Dir` is already set, it
   skips the module-replacing !Run and registers sprites only.
5. Run authored `BootTasks` through normal Filer_Run for installed startup
   objects, retaining the R4-1 bounded enumeration. No supplied PreDesktop,
   ROxxx hook or modern !Boot.!Run is invoked.

`Boot$Dir`/`Boot$Path` point at `ADFS::4.$.!Boot`; `BootResources$Dir` and
`BootResources$Path` point at its Resources tree. Choices stay in the
session-owned `ADFS::4.$.Choices`, with trailing dots on search paths.
`Boot$ToBeLoaded` and `Boot$ToBeTasks` refer to Choices.Boot.PreDesk/Tasks;
`Boot$State=desktop`, `Boot$Unique=Local`. Run$Path includes !Boot.Library.
PreDesk is available to installers; no module is preloaded there by this
construction. A title's own RMEnsure commands may load required modules
through the normal System path on demand. A title which needs resources
only created by the omitted modern scripts is a blocker to report.

Sources for Boot, Choices and startup directory semantics are the
[boot application PRM](https://www.riscos.com/support/developers/prm/bootapp.html)
and [RISC OS 3.7 User Guide, startup](https://www.riscos.com/support/users/userguide3/bookb/book_5.html).
BootResources uses the supplied HardDisc4 RO350/360/370Hook.Boot.PreDesktop
fallback convention (`Boot:Resources.`); these scripts are read as evidence
and never run. System registration/version search is evidenced by the
hash-approved archive !System.!Boot and tokenised BASIC SysPaths, retained
unchanged on the private disc. Runtime variable/module audits, guest CMOS
changes, merge comparison and image hashes belong in private custody.

`tools/boot_audit.py --image /private/session/scratch.hdf --firmware
/external/combined-image --out /private/session/boot-audit.json` checks
EnvAudit and ModAudit created in the guest with ordinary redirected
`Show *` / `Modules` commands. It checks exact standard paths, the discovered
System version search order and each observed module code address against
its header in the hash-approved ROM (including resource modules and named
incarnations). It cannot establish desktop/input success; that is recorded
separately from screenshots. No host guest-memory changes are involved.

At the ordinary desktop command line, collect both files with:

```text
Show * { > ADFS::4.$.EnvAudit }
Modules { > ADFS::4.$.ModAudit }
```

The audit also requires Run$Path to start with the boot Library directory;
the existing ROM search suffix is preserved.

With `rpcprobe_diagnostics=enabled`, `boot-devices.csv` also records numeric
content-drive `media-read` and `media-id` requests: virtual time, image index,
drive, requested ID/cylinder/head, density and size code. No payload or label
is logged. The added observer fails explicitly after two million requests
in a session. Use actual request progress to distinguish an unchanged old-ROM
error box during copying from a failed media change. Screenshots alone do not
establish whether a retry began; disabling diagnostics removes this logging.

The resource-only builder additionally preserves the supplied root
!Boot.!Boot and !Run exactly under !Boot.Inactive. The active !Boot is an
authored icon-registration Obey; the active !Run invokes the authored helper.
This is necessary because Filer scans application !Boot entries while opening
the IDE root, independently of the reset boot option. Merely disabling reset
boot is insufficient to prevent modern scripts from running. The original
root scripts are retained as inactive evidence, with custody hashes and
unchanged metadata/notices. The source resource tree and all other files stay
unchanged. Do not qualify an image built without `--resources-only` for R7-1.

### Drive-scoped media replacement (run-8 R8-1)

Content eject/insert cancels only drive-0 media execution; the independent
helper's READ ID, data transfer and empty-drive search retain their deadlines.
Controller reset still cancels both. Disk-control calls with an unchanged
insertion state are idempotent. Media replacement leaves the guest's DOR
motor enables intact; DOR bits 1:0 select the drive independently of motors.
DIR bit 7 is the inverted nDSKCHG of that selected drive. The drive latch
survives DIR reads, controller reset, insertion and same-cylinder seeks;
a physical step clears only the selected drive with media present.

`expectations.md` cites the SMSC controller and TEAC drive specifications.
`probe_controller` generates two authored JFDs and a playlist in its test
working directory, performs real disk-control swaps with pending guest
commands, and removes its synthetic files on normal completion. It tests
unaffected helper READ ID and empty searches, eight repeated swaps,
interrupted reads/writes, an early-swap replacement read, per-drive DIR
selection and physical-step clearing. No private firmware/content is used.

### Independent helper-presence control (run-9 R9-1)

`rpcprobe_boot=manual-helper` inserts the unchanged authored drive-1 medium
without scheduling automatic boot keystrokes. A replay can establish the
environment through normal guest commands at its own fixed timestamps,
as with `standard` (which omits the helper). This diagnostic option separates
helper presence from variables, System registration and task dispatch;
`automatic` and `helper` retain their existing startup behavior. Record the
boot script, baseline, CMOS and frozen binary hashes for each comparison.
The extra drive icon changes the initial opening-click position: normalize
the pointer during setup, before the shared installer-event sequence.
Keep media replacement and confirmation timestamps identical.

### Active single-drive hard-disc boot (run-10 R10-1)

This supersedes the helper-based qualification modes described above. The
public libretro core always attaches just content floppy drive 0; drive 1
has no helper medium. `rpcprobe_boot=automatic` resolves to `standard`; old
helper option values are refused. The authored two-drive controller fixtures
remain available only to mechanism tests, as coverage for the retained bridge.
The unexplained two-floppy FileCore interaction after cataloguing drive 1 is
an accepted residual risk; no workload in scope requires that configuration.

Reset preserves the drive-count/IDE CMOS fields written by firmware or by
ordinary Configure commands. Never patch those bytes on the host. Documented
setup is `Configure Floppies 1`, `Configure IDEDiscs 1`, `Configure Boot`,
`Configure FileSystem ADFS`, `Configure Drive 4`; on the IDE root use
`Opt 4,2`. Sources: [ADFS PRM](https://www.riscos.com/support/developers/prm/adfs.html).
Start each pair from its independently captured factory CMOS, configure in
the guest, record the exact resulting bytes and hash, then cold-qualify.

`boot/HDRun` replaces only the active `!Boot.!Run` with authored startup:
R4-1 MDF, R7-1 BootEnv and supplied System registration/version paths. It
then invokes `Desktop -file` on `!Boot.ProbeTasks`, whose sole command runs
the general BootTasks enumerator after the ROM desktop modules have started.
This is the documented [Desktop command](https://www.riscos.com/support/developers/prm/wimp.html).
No RISC OS Open modern !Run/PreDesktop/ROxxx hook executes, and no ROM module
is replaced at startup. The original archive entries remain in Inactive.

New `boot_disc.py --resources-only` images contain the five authored startup
assets on IDE; perform the supplied guest !System merge as above. For an
already merged R7-1 disc whose root has Multisync/BootEnv/BootTasks, generate
normal guest Build/SetType/Configure input with:

```sh
python3 probe/riscpc/tools/harddisc_setup.py --entries --out probe/riscpc/run/setup.txt
```

Without `--entries`, it generates configuration only. The JSON sidecar
records the effective guest commands, source asset hashes and required
emulated duration. Run the script through the headless runner, inspect the
resulting guest files/status and cold boot, then freeze/hash the baseline.
Neither construction nor COMPLETE constitutes boot or gameplay acceptance.
Record all seven firmware/model results, including 3.50's behavior, privately.

Run-10 qualification stopped at workload 084's cold launch: its ordinary
application checks `Wimp$Scrap`, which the current common startup does not
establish. The supplied Scrap resource is present, but its registration is
not dispatched. This is an unresolved common-environment startup failure,
not evidence that the resource requires the modern boot sequence. The seven
smoke pairs and workloads 031/074 reached their required desktop/gameplay
states; no full AC-6, calibration, determinism or platform pass is implied.
The private report records the failed launch and proposed general resolution
for adjudication. Do not use these partial results as a qualified AC-8 suite.
