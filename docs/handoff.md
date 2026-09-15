# ArcDroid Handoff

**Stage:** ARCHITECTURE  
**Owner:** Fable  
**Next:** (1) Founder decision on the next slice from the candidates below; no implementation is authorized by this baton. (2) Carried validation: Windows PC real boot and mouse/XInput (D-024), Retroid Pocket pass, hand-driven two-finger tap and Odin3 physical sticks.

## Objective and governing requirement
Slice 2b (launch automation, D-030/D-031) is merged and now device-validated on Android: PR #38 (`fix/02b-android-readonly-flush`, merged as `20222b4`) fixed the Android-only first-frame fault found by the carried AC-12(b) run (D-032: flush only writable drive handles; harness VFS reproduces RetroArch-on-bionic flush semantics). Governing: PRD §2, P-3, FR-2, FR-24…FR-27; protocol §11–§14.

## Verified state (2026-09-15, PR #38 merged)
- `main` = `20222b4` = Slice 1 + Input Slice + Slice 2a + Slice 2b + PR #38 (`c734af1`: `libretro/arc_io.c` records the open mode and treats a read-only flush as a successful no-op; harness/Windows-harness VFS fail `flush` on read-only handles; R-73 sessions for launch-helper ≥ 500 fields with direct-boot signature equality, write-policy-off, injected writable flush failure still fatal; run-wide flush-fault assertion; `libretro-harness` in the sanitizer job with a 300 s timeout; ASan heap-accounting shim labelled) + governance PRs #36, #37, #39, #40.
- Evidence: CI 5/5 at `c734af1`; Fable fresh clone `ctest` 6/6 and guards at `ca3d8f3` and `c734af1`; Opus QA at `ca3d8f3` (no defect; fault reproduced on real RISC OS 3.11 at `14c042d`, clean at `ca3d8f3`; new assertions mutation-tested) and targeted re-check at `c734af1`. Odin3 (Android 15, RetroArch 1.22.2) with the `ca3d8f3` NDK build (shipping sources identical to `c734af1`), on `evidence/slice2b-odin3`: `saloon1.jfd` reaches the Saloon Cars Deluxe intro scene with no desktop interaction (and later the game's own disc-two prompt); `repton4-short.adf` its title screen; `chess.adf` launches `!Chess3D` onto the icon bar (its window opens on a Select click, not automated); `saloon.m3u` launches `!SALOON` from disc 1; `saloon2.jfd` and `saloon-extra-courses.jfd` fall back to the desktop with no OSD; per-game `Launch = Off` and `Application 3` honoured at load and after a Quick-Menu reset (second `Launch:` decision, second boot); `am force-stop` during a helper session leaves drive 0 / one floppy in the persisted CMOS; 0 `[ERROR]`, 0 flush faults in every run.
- Device notes: RetroArch rewrites `config/ArcDroid/<content>.opt` on quit, so per-game options must be written while it is stopped; a single UDP `RESET` is ignored (reset needs the confirmation press); keyboard keyevents do not drive the glui menu (use touch or the Remote RetroPad); the installed core on the Odin3 is the `ca3d8f3` build; `downloads/` still holds the seven Founder content files and the generated fixtures.

## Next-slice candidates (Founder decision)
1. Audio quality: per-field incremental resampler flush to remove the D-022 underruns at VIDC mode changes (~500 underrun frames per Odin3 session still observed).
2. Save states (D-011): complete machine-state serialization, currently `savestate=false`.
3. Input: FR-29 automatic pad-to-keyboard profiles (content-id keyed, first real use of FR-47 profiles), on-screen keyboard, A3010 joystick interface.
4. Media: APD/HFE/FDI/SCP protected formats (D-007 fidelity), 320K/M-format discovery, playlist entries without media extensions (FR-5).
5. Launch follow-ups: `Application N` UX (binding refusal needs `Off` then re-select; playlist bindings key on the playlist id), disc-name collisions with `1`/`ArcDroid`, log truncation past ~70 candidates, automatic first Select click for desktop applications that start iconised (Chess 3D).

### Non-goals until the Founder chooses
No implementation is authorized by this baton; PRD unchanged.

## Acceptance criteria
None open. Slice 2b AC-12(b) device evidence is complete (above).

## Deterministic evidence required
None pending; future device evidence per D-025 on `evidence/*` branches.

## Licensing / security / provenance and QA focus
No new third-party code in PR #38; no game or firmware bytes in the repository, CI or evidence branches (Founder-title screens described in words only).

## Known limitations and follow-ups
1. From PR #38 QA: the run-wide flush-fault assertion aborts the harness's legacy 4-argument manual invocation (`ctest` and `--real-boot` unaffected; gate it on `media_tests` when the harness is next touched); `ARCDROID_LIBRETRO_SOURCES` shared targets get sanitizer link only (core instrumented, adapter not); `adf_loadex` read-only fallback and the `original` write policy are verified by hand, not pinned by a test; NDK artifact sha256 is not path-reproducible; `flush-save-<pid>` directories accumulate in the harness data dir like `fault-save-<pid>`.
2. Slice 2b: Windows RetroArch launch probe runs the test DLL; binding fingerprint stored but unused; `ioc_discchange(1)` raised on the no-helper path; CMOS bytes 11/135 clamped for every OS class; 320K discs get no automation; malformed 10-character disc names produce "Bad drive" inside the launched task; two drive icons appear on the desktop while a helper is mounted.
3. Carried from earlier slices: Windows PC real boot and mouse/XInput; Retroid Pocket; two-finger tap and Odin3 sticks by hand; D-022 audio underruns; FR-29 pad keyboard profiles; SAF content paths untested; playlist media-extension gating (FR-5); JFD reader track-register/density-cadence notes; harness `argv[9]` and PowerShell `finally` masking.
## Orchestration notes (Fable)
- Sol dispatch: Codex CLI from a fresh clone under the session scratchpad: `codex exec -m gpt-5.6-sol --sandbox workspace-write -C <clone> -c sandbox_workspace_write.network_access=true --json -o <report.md> - < <prompt.md>` via Bash `run_in_background`; resume with `codex exec resume <thread-id> -m gpt-5.6-sol -c sandbox_mode="workspace-write" -c sandbox_workspace_write.network_access=true --json -o <report.md> - < <prompt.md>`. Codex protects the clone's `.git`; Sol commits through a side git dir (`build/sol-git`) and pushes from it. Astra: `codex exec -m gpt-6-astra --sandbox read-only` (blind pass) then `codex exec resume <thread-id> -m gpt-6-astra -c sandbox_mode="read-only"`. Opus QA: Agent tool (`model: opus`, `general-purpose`) on a fresh clone; targeted re-checks by SendMessage; tell Opus when the device is reserved.
- **Watchdog (Founder rule)**: every background model run is watched by `watchdog.sh <label> <pid> <stream> <report> [stall s] [heartbeat s]` under the harness Monitor (30-min lease, re-armed on expiry); it emits HEARTBEAT, STALL, RECOVERED, DONE and EXITED-NO-REPORT. Only PIDs started by this session are watched or stopped.
- Dev Mac: NDK `/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865`, adb under `platform-tools`, MinGW `x86_64-w64-mingw32-gcc`, `JAVA_HOME=/opt/homebrew/opt/openjdk`, no ripgrep, no RetroArch. Odin3 (LAN 192.168.50.215): RetroArch system `/storage/emulated/0/BIOS`, cores `/data/user/0/com.retroarch.aarch64/cores/`, logs `/storage/emulated/0/RetroArch/logs`, run config `/storage/emulated/0/RetroArch/arcdroid-run.cfg`; launch `am start -n com.retroarch.aarch64/com.retroarch.browser.retroactivity.RetroActivityFuture --es ROM <path> --es LIBRETRO <core> --es CONFIGFILE <cfg>` after `input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard`; sideload = push to `RetroArch/downloads/` then Load Core → Install or Restore a Core; touch is a trackpad; keyboard via `input keyboard keyevent/text`; disk control and quit via UDP 55355 (single `QUIT`). Reproduction on `main`: `cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/host && ctest --test-dir build/host --output-on-failure`; real boot via `build/host/libretro_harness <test dylib> <ROM> <disc> <dir> --real-boot` (screenshots at frames 100…3000 under `<dir>/evidence`).

## Branch / PR
`main` = `20222b4`. No open implementation PR. Evidence branches `evidence/slice1-android-run7`, `evidence/input-slice-s5`, `evidence/slice2a-odin3`, `evidence/slice2b-host`, `evidence/slice2b-odin3` (never merge).
