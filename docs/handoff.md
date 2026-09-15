# ArcDroid Handoff

**Stage:** ARCHITECTURE  
**Owner:** Fable  
**Next:** (1) Device validation of `main` on the Odin3 when the Founder reconnects it (AC-12(b) of Slice 2b, carried): sideload the `main` core, then Saloon Cars Deluxe disc 1 (`saloon1.jfd`, staged in `downloads/`) must reach its intro scene with no desktop interaction, `saloon2.jfd`/`saloon-extra-courses.jfd` must fall back silently to the desktop (no OSD: their `!` directories have no `!Run`), and `saloon.m3u` (three-disc playlist) must launch `!SALOON` from disc 1, `repton4-short.adf` must reach its title screen, `chess.adf` its window; then `Launch = Off` and `Launch = Application N` saved as per-game options and honoured after reset; force-stop must leave no drive-1/two-floppy values in the persisted CMOS. (2) Windows PC real boot and mouse/XInput (carried, D-024). (3) Propose the next slice to the Founder from the candidates below; no implementation is authorized by this baton.

## Objective and governing requirement
Slice 2b (launch automation, D-030/D-031) is merged: a boot-option-0 disc with a discoverable root application launches without desktop interaction on RISC OS 3.0x/3.1x (PRD §2, P-3, FR-24…FR-27). The remaining Slice 2b evidence is physical-device validation (PRD FR-2, D-017, D-024). Governing: PRD §11, §23; protocol §11–§14.

## Verified state (2026-09-15, Slice 2b merged)
- `main` = Slice 1 + Input Slice + Slice 2a + Slice 2b (PR #33, `slice/02b-launch-automation`, final head `7905db3`, merged as `6f9c7df`) + governance PRs. Slice 2b delivered: bounded read-only FileCore discovery (L/D/E, through JFD, both new-directory signatures, names ending at the first control byte, real E-map range with ZoneSpare/DiscSize consistency, declared old-map sizes ≤ geometry), deterministic drive-1 launch disc (L-format, boot option 2, Obey `!Boot` → `Desktop -File ADFS::1.$.ArcLaunch` → `Dir`/`URD`/`Filer_Run` on the game disc by name or `ADFS::0`), engine two-drive path with per-drive write policy and reset re-mount, launch-owned CMOS fields (drive, floppies) with payload normalisation (D-031), core option `arcdroid_launch` (Auto/Off/Application 1–8) with a target-name binding file, byte-509/510 boot-option fix, `arm.c` cast, launch fixture family with PRM-derived expected structures, harness/controller/Windows/sanitizer coverage.
- Evidence: CI 5/5 at `7905db3`; Fable fresh clone (`ctest` 6/6); Opus QA at `235afd8` (250 000-mutation fuzz clean, 21 persisted CMOS records normalised) plus targeted re-check at `7905db3`; private RISC OS 3.11 host runs: checked-in application fixture and scratch discs reach the pattern through the helper (final signature equals direct boot), Saloon Cars Deluxe disc 1 launches `!SALOON` to its intro scene, Repton 4 launches to its title screen, Saloon disc 2 and Extra Courses fall back to the desktop. Evidence branch `evidence/slice2b-host` (generated content only).
- Guest-side facts learned: RISC OS 3.11 will not run a typed file whose exec field equals its load address (use exec 0 for generated typed files); FileCore leaves stale bytes after directory-name terminators; real D-format discs declare 3180 old-map sectors; real E floppies have ZoneSpare 1312 and 6400 allocation bits; `Dir`/`URD` inside a Desktop boot file give the launched task CSD/URD on the game disc; a failing `!Run` leaves a Cancel-able error over a usable desktop.
- Device: Odin3 `cf0be0f3` disconnected by the Founder this session; `downloads/` holds `saloon1.jfd`, `saloon2.jfd`, `saloon3.jfd`, `saloon.m3u`, `saloon-extra-courses.jfd`, `repton4-short.adf`, `chess.adf`, the generated fixtures; installed core is the Slice 2a build (`fd5fdc5`) — sideload the `main` build first.

## Next-slice candidates (Founder decision)
1. Audio quality: per-field incremental resampler flush to remove the D-022 underruns at VIDC mode changes.
2. Save states (D-011): complete machine-state serialization, currently `savestate=false`.
3. Input: FR-29 automatic pad-to-keyboard profiles (content-id keyed, the first real use of FR-47 profiles), on-screen keyboard, A3010 joystick interface.
4. Media: APD/HFE/FDI/SCP protected formats (D-007 fidelity), 320K/M-format discovery, playlist entries without media extensions (FR-5).
5. Launch follow-ups: `Application N` UX (binding refusal needs `Off` then re-select; playlist bindings key on the playlist id), disc-name collisions with `1`/`ArcDroid`, log truncation past ~70 candidates.

### Non-goals until the Founder chooses
No implementation is authorized by this baton; PRD unchanged.

## Acceptance criteria
None open; Slice 2b AC-12(b) device evidence is carried as `Next` (1).

## Deterministic evidence required
Device evidence per D-025 on an `evidence/slice2b-odin3` branch (generated content only; Founder-title results in words).

## Licensing / security / provenance and QA focus
No new third-party code in Slice 2b; generated guest files are project-authored templates; no game or firmware bytes in the repository, CI, or evidence branches.

## Known limitations and follow-ups
1. Slice 2b: Windows RetroArch launch probe runs the test DLL (shipping DLL launch path unexercised under native RetroArch); binding fingerprint stored but unused; `ioc_discchange(1)` raised on the no-helper path; CMOS bytes 11/135 clamped for every OS class; 320K discs get no automation; a scratch disc with a malformed 10-character name produced "Bad drive" from `*Cat` inside the launched task (RISC OS behaviour).
2. Carried from earlier slices: Windows PC real boot and mouse/XInput; Retroid Pocket; two-finger tap and Odin3 sticks by hand; D-022 audio underruns; FR-29 pad keyboard profiles; SAF content paths untested; playlist media-extension gating (FR-5); JFD reader track-register/density-cadence notes; harness `argv[9]` and PowerShell `finally` masking (Opus L-11/L-12 of Slice 2a).
## Orchestration notes (Fable)
- Sol dispatch: Codex CLI from a fresh clone under the session scratchpad: `codex exec -m gpt-5.6-sol --sandbox workspace-write -C <clone> -c sandbox_workspace_write.network_access=true --json -o <report.md> - < <prompt.md>` via Bash `run_in_background`; resume with `codex exec resume <thread-id> -m gpt-5.6-sol -c sandbox_mode="workspace-write" -c sandbox_workspace_write.network_access=true --json -o <report.md> - < <prompt.md>`. Codex protects the clone's `.git`; Sol commits through a side git dir (`build/sol-git`) and pushes from it. Astra: `codex exec -m gpt-6-astra --sandbox read-only` (blind pass) then `codex exec resume <thread-id> -m gpt-6-astra -c sandbox_mode="read-only"`. Opus QA: Agent tool (`model: opus`, `general-purpose`) on a fresh clone; targeted re-checks by SendMessage; tell Opus when the device is reserved.
- **Watchdog (Founder rule)**: every background model run is watched by `watchdog.sh <label> <pid> <stream> <report> [stall s] [heartbeat s]` under the harness Monitor (30-min lease, re-armed on expiry); it emits HEARTBEAT, STALL, RECOVERED, DONE and EXITED-NO-REPORT. Only PIDs started by this session are watched or stopped.
- Dev Mac: NDK `/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865`, adb under `platform-tools`, MinGW `x86_64-w64-mingw32-gcc`, `JAVA_HOME=/opt/homebrew/opt/openjdk`, no ripgrep, no RetroArch. Odin3 (LAN 192.168.50.215): RetroArch system `/storage/emulated/0/BIOS`, cores `/data/user/0/com.retroarch.aarch64/cores/`, logs `/storage/emulated/0/RetroArch/logs`, run config `/storage/emulated/0/RetroArch/arcdroid-run.cfg`; launch `am start -n com.retroarch.aarch64/com.retroarch.browser.retroactivity.RetroActivityFuture --es ROM <path> --es LIBRETRO <core> --es CONFIGFILE <cfg>` after `input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard`; sideload = push to `RetroArch/downloads/` then Load Core → Install or Restore a Core; touch is a trackpad; keyboard via `input keyboard keyevent/text`; disk control and quit via UDP 55355 (single `QUIT`). Reproduction on `main`: `cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/host && ctest --test-dir build/host --output-on-failure`; real boot via `build/host/libretro_harness <test dylib> <ROM> <disc> <dir> --real-boot` (screenshots at frames 100…3000 under `<dir>/evidence`).

## Branch / PR
`main` = `6f9c7df`. No open implementation PR. Evidence branches `evidence/slice1-android-run7`, `evidence/input-slice-s5`, `evidence/slice2a-odin3`, `evidence/slice2b-host` (never merge).
