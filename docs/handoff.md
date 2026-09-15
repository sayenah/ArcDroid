# ArcDroid Handoff

**Stage:** QA  
**Owner:** Opus  
**Next:** Independent QA of PR #43 (`slice/03a-input-profiles-osk`, head `4273096`) against the Slice 3a work order (PR #42 version of this file, `git show 5157482:docs/handoff.md`), the remediation records R-77…R-96 (`git show 0b177e8:docs/handoff.md`) and the PR thread; report findings ranked by severity to Fable. The Odin3 is reserved for QA for this pass. Do not edit implementation, tests, architecture or this file.

## Objective and governing requirement
Slice 3a: content-keyed input profiles, static per-input option overrides, and an in-core on-screen keyboard (PRD §23 proof 7; FR-28…FR-34, FR-47…FR-51, FR-22, FR-53, P-5; D-033, D-034).

## Verified state (2026-09-15, Fable)
- Fresh clone at `4273096`: `ctest` 6/6, asset/shipping-I/O/export guards, NDK arm64 build + Android guard (sha256 `8d6b8829…9c94`, pushed to the Odin3 `downloads/` for QA); CI 5/5 on push and PR runs (Sol runs 1–3). Fable reviews: run 1 findings R-77…R-90 all closed at `df4d3c7` except the four partials re-stated as R-92…R-96, closed at `4273096` per Fable's re-review (posted on PR #43) except two Lows carried into the post-QA remediation: **R-97** the OSK header renders `(width-4)/8` glyphs but computes capacity as `width/8`, so a profile name at capacity drops the last glyph of the state (`core/osk.c` header path; fix the capacity and add a long-name truncation test at 320 px); **R-98** the PR body must state that the one-frame notice limitation is removed by R-94. Noted Lows for follow-up, not defects of this slice: BUSY may stay displayed until 1 s plus the next action after the queue recovers (by design of R-94); a touch contact that began before a pad activation still presses its key on lift.
- Device (Odin3, `b976632` build): Chess 3D launches as on `main`; the OSK opens on L3, touch hit-testing types into RISC OS (`*cat` executed from the F12 line). Not exercised over adb: d-pad focus navigation (synthetic d-pad keyevents reach RetroArch as keyboard arrows on this device), physical sticks.
- Profile registry ships empty: the required Repton 4 profile is blocked because the title never leaves its title screen on the device (Founder question open). The slice is therefore **not complete for shipment** until that is resolved; QA proceeds on everything else.
- Suspected regression on `main` (Slice 2a, outside this PR): with a RetroArch `saloon.ldci` present, disc changes after load through the disk-control interface did not take effect in two runs and worked in two runs without the file (Fable, Odin3, UDP disk commands). Reproduce with logging.

## QA focus (in addition to AC-1…AC-11 and the R-items)
1. Aggregation/ordering under adversarial pad sequences across both ports; overflow mid-batch (R-78); release ordering (R-93).
2. OSK ownership gates: pad and touch neutral gates on entry/exit (R-79, R-95), held activation lock (R-81), completion when another source holds the key, generation invalidation, busy/interrupted hold (R-94).
3. Renderer/compositor bounds under geometry change while visible (ASan), scratch growth failure during an activation, engine pixels untouched, duplication rule.
4. Key-set provenance: `tests/keytable_provenance.c` against the engine headers; the omitted-legend list in the PR body (£ at (1,13), Print Screen, (0,13)/(1,13)).
5. Options: 33 keyed options, legacy `.opt` load, no `SET_VARIABLE`, live vs reset resolution, descriptor refresh.
6. Device (Odin3, reserved): reproduce the OSK typing evidence with the `4273096` NDK build (sideload via `downloads/` → Load Core → end of list → Install or Restore a Core); exercise the physical d-pad/A/B/X/Y/Start/Select on the OSK by hand if a pad is available to you, otherwise touch; a RetroArch keyboard overlay and a RetroPad remap with Save Game Options; menu/background/resume; ten reloads; Chess 3D pointer regression; the `.ldci` disc-swap reproduction with `saloon.m3u` (playlist in `downloads/`; set the index back to 0 or remove the `.ldci` you created before leaving).
7. Private titles: describe in words only; no screenshots of game art in the PR; no game or firmware bytes anywhere.

## Acceptance criteria
AC-1…AC-12 of the PR #42 baton. AC-12(a) real-title profile play is blocked (Founder); all other AC-12 items are QA's to reproduce on the device.

## Deterministic evidence required
QA runs the host suite, the sanitizer suite (Linux CI supplies leak coverage; local `detect_leaks=1` is unsupported on macOS ASan), the guards, and any adversarial/mutation runs it adds locally (not committed). Report exact commands and results.

## Licensing / security / provenance
No new third-party code or assets; the OSK font is ArcDroid-authored; digests plus words only.

## Known limitations and follow-ups
1. Repton 4 title-screen blocker (Founder). 2. Saloon Cars Deluxe keyboard controls unobserved (menu is mouse-driven; demo ignores keys). 3. Suspected `.ldci` disk-control regression on `main`. 4. Carried: Windows PC real boot and mouse/XInput (D-024), Retroid Pocket, hand two-finger tap and Odin3 sticks, D-022 audio, Slice 2b follow-ups. 5. Next-slice candidates: Slice 3b RTFM joystick; audio quality; New-IO machine class; save states; media formats; launch follow-ups.

## Orchestration notes (Fable)
- **Background runs (protocol §15, revised 2026-09-15)**: `watchdog2.sh <label> <pid> <stream> <report> <checkout> [poll=300] [stall-checks=3]` under the harness Monitor (30-min lease; re-arm on expiry; the first check after a re-arm catches a completion that landed at the lease boundary). Healthy checks emit nothing; only COMPLETED / DIED / STALLED / NEEDS_INPUT are surfaced. Only PIDs recorded at dispatch are watched or stopped. Subagents use the harness's native completion notification.
- Sol: Codex CLI from a fresh clone under the session scratchpad (`codex exec -m gpt-5.6-sol --sandbox workspace-write -C <clone> -c sandbox_workspace_write.network_access=true --json -o <report.md> - < <prompt.md>`; resume with `codex exec -C <clone> resume <thread-id> -m gpt-5.6-sol -c sandbox_mode="workspace-write" -c sandbox_workspace_write.network_access=true …`); side git dir `build/sol-git`. Slice 3a thread `01a0a69b-3f6d-70c3-83e9-6c6dadf1fd7d`. Astra: `codex exec -m gpt-6-astra --sandbox read-only -C <clone>` then `codex exec -C <clone> resume <thread-id> -m gpt-6-astra -c sandbox_mode="read-only"`. Opus QA: Agent tool (`model: opus`, `general-purpose`) on a fresh clone; targeted re-checks by SendMessage.
- Dev Mac: NDK `/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865`, adb under `platform-tools`, MinGW `x86_64-w64-mingw32-gcc`, `JAVA_HOME=/opt/homebrew/opt/openjdk`, no ripgrep, no RetroArch. Odin3 (LAN 192.168.50.215, adb `cf0be0f3`): RetroArch system `/storage/emulated/0/BIOS`, cores `/data/user/0/com.retroarch.aarch64/cores/` (not readable over adb), logs `/storage/emulated/0/RetroArch/logs`, run config `/storage/emulated/0/RetroArch/arcdroid-run.cfg`, saves `/storage/emulated/0/Retroarch/saves/ArcDroid`; launch `am start -n com.retroarch.aarch64/com.retroarch.browser.retroactivity.RetroActivityFuture --es ROM <path> --es LIBRETRO <core> --es CONFIGFILE <cfg>` after `input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard`; touch is a trackpad for the guest and an absolute hit-test for the OSK; keyboard via `input keyboard keyevent`; pad buttons via `input keyevent KEYCODE_BUTTON_A/B/THUMBL` (Android BUTTON_A = RetroPad B; d-pad and L1/L2 keyevents do not reach the core as pad input); disk control via UDP 55355 `DISK_EJECT_TOGGLE`/`DISK_NEXT`/`DISK_PREV` (tray phase is a toggle; RetroArch writes `<playlist>.ldci` on quit); quit via UDP `QUIT`. Reproduction on `main`: `cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/host && ctest --test-dir build/host --output-on-failure`; real boot via `build/host/libretro_harness <test dylib> <ROM> <disc> <dir> --real-boot`.

## Branch / PR
`main` = `0b177e8`. Implementation PR #43 `slice/03a-input-profiles-osk` at `4273096` (open; QA). Evidence branches `evidence/slice1-android-run7`, `evidence/input-slice-s5`, `evidence/slice2a-odin3`, `evidence/slice2b-host`, `evidence/slice2b-odin3` (never merge).
