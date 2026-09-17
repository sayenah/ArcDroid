# ArcDroid Handoff

**Stage:** ARCHITECTURE  
**Owner:** Fable  
**Next:** Founder picks the next slice from the candidates below; Fable then grounds in `main`, verifies the changing external premises from primary sources, defines the capability slice with deterministic evidence, obtains the Astra challenge, and writes the `Stage: IMPLEMENTATION`, `Owner: Sol` work order. No implementation or test edits until that work order exists.

## State of `main` (2026-09-16)
- Slices merged: 1 (skeleton boot, PR #5), input slice, 2a, 2b (launch automation, PR #33 + #38), **3a (input profiles, per-input options, on-screen keyboard, PR #43 merged as `7941854`, final head `f3139b8`)**. Decisions through D-035. CI on `main` at `7941854`: 5/5 green (host, host-sanitizers, windows, android-arm64, windows-retroarch); Fable fresh clone of `7941854`: `ctest` 6/6.
- Slice 3a evidence: Opus full QA at `4273096` and targeted re-checks at `acd75e4`, `c0b9a71`, `f3139b8` (no defect); Fable fresh-clone `ctest` 6/6 at every head; CI 5/5 at `f3139b8`; Odin3 OSK typing, profile resolution and Elite pad play verified (`git show 7941854:docs/handoff.md` for the merge-gate baton with the full Elite evidence and the Opus adjudication).
- Odin3 has the `f3139b8` core installed (sha256 `84ef312407c86403a2c04cbe786990a04dc684c3f15330ff5bb521750f1b71e4`); RetroArch stopped; no Fable files or `.ldci`/`.opt` left behind.

## Next-slice candidates (Founder choice)
1. **Slice 3b — RTFM joystick** (D-034 deferred item; A3010 joystick needs the New-IO machine class).
2. **New-IO machine class** (A3010/A3020/A4000: 82C711 import, ARM250, CMOS defaults) — widens compatibility and unlocks the A3010 joystick.
3. **Save states** (D-011: complete machine-state serialization, `savestate=true`).
4. **Audio quality** (D-022: per-field incremental resampler flush).
5. **Media formats** (FR-9 roadmap) and **launch follow-ups** (Slice 2b non-ASCII root entry; Elite launch helper).
6. **Compatibility investigation**: Repton 4 title-screen hang.
Small items that can ride along with any slice: per-profile setup note surfaced through FR-34 (Elite: Mouse preference off, D-035); INFO log of resolved per-port device types (M-2); L-2/L-3 test gaps; R-115 and the other carried Lows.

## Carried validations and known limitations
- AC-12(b) Founder-PC Windows real boot and mouse/XInput; AC-12(c) Retroid Pocket (D-024).
- Odin3 split pad: OSK modality and pointer buttons are port-0 only (M-1); RetroArch per-port binding check (stick-click L3).
- Slice 2b discovery marks a non-ASCII root entry unreadable (Elite launches by Filer double-click).
- D-022 audio underruns at mode changes; hand two-finger tap.

## Licensing / security / provenance
No third-party code or assets beyond the D-018 engine files with notices; no game or firmware bytes anywhere; private titles described in words only; the Elite manual PDF and disc image were used locally and are not in the repository.

## Orchestration notes (Fable)
- **Background runs (protocol §15)**: `watchdog2.sh <label> <pid> <stream> <report> <checkout> [poll=300] [stall-checks=3]` (session scratchpad; recreate from the description if lost: read-only 5-min polls of stream size, process-tree CPU and `git status` hash; emit only COMPLETED/DIED/STALLED/NEEDS_INPUT with run id, elapsed, log path and a bounded tail; record PID/start/log in `<stream>.watch`) under the harness Monitor with a 30-min lease, re-armed on expiry. Only PIDs recorded at dispatch are watched or stopped. Subagents use the harness's native completion notification.
- Sol: Codex CLI from a fresh clone under the session scratchpad (`codex exec -m gpt-5.6-sol --sandbox workspace-write -C <clone> -c sandbox_workspace_write.network_access=true --json -o <report.md> - < <prompt.md>`; resume with `codex exec -C <clone> resume <thread-id> -m gpt-5.6-sol -c sandbox_mode="workspace-write" -c sandbox_workspace_write.network_access=true …`). Side git dir `build/sol-git`. Astra: `codex exec -m gpt-6-astra --sandbox read-only -C <clone>`. Opus QA: Agent tool (`model: opus`, `general-purpose`) on a fresh clone under the scratchpad, never the main checkout; `git checkout <ref> --` in the main checkout detaches HEAD — use `git show <ref>:<path>` for reads.
- Harness permissions: in auto mode the classifier blocks `gh pr merge` of implementation PRs and any edit of `.claude/settings*.json` by Fable, even with Founder consent in chat; the Founder merges implementation PRs from the terminal (`gh pr merge <n> --merge`) or adds `Bash(gh pr merge:*)` to `.claude/settings.local.json`. Governance (docs) PRs merge normally.
- Dev Mac: NDK `/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865`, adb under `platform-tools`, MinGW `x86_64-w64-mingw32-gcc`, `JAVA_HOME=/opt/homebrew/opt/openjdk`, no ripgrep, no RetroArch, no PIL; private ROM `ROMS/ROM311` (git-ignored); Elite dump on the Founder's Desktop and in the Odin3 `downloads/` as `elite.adf`. Reproduction: `cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/host && ctest --test-dir build/host --output-on-failure`; real boot `build/host/libretro_harness <test dylib> <ROM> <disc> <dir> --real-boot [--osk-script FILE]`.
- Odin3 (adb `cf0be0f3`, Android 15, RetroArch 1.22.2_GIT): system `/storage/emulated/0/BIOS`, cores `/data/user/0/com.retroarch.aarch64/cores/`, log `/storage/emulated/0/RetroArch/logs/retroarch.log` (one file per run, `grep -a`; KART `device->guest values=RR,CC`, `C`=down `D`=up), run config `/storage/emulated/0/RetroArch/arcdroid-run.cfg`; launch `am start -n com.retroarch.aarch64/com.retroarch.browser.retroactivity.RetroActivityFuture --es ROM <path> --es LIBRETRO <core> --es CONFIGFILE <cfg>` after `input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard`; sideload via `RetroArch/downloads/` → Load Core → Install or Restore a Core. Input injection: synthetic keyboard does not reach the core (RetroArch autoconfigures "Virtual" as a pad); true pad holds via the kernel node `/dev/input/event8` (`sendevent $dev 3 16 -1; sendevent $dev 0 0 0 … 3 16 0`, HAT codes 16/17, `BTN_SOUTH 304`→B, `BTN_EAST 305`→A, `BTN_NORTH 307`→Y, `BTN_START 315`); `KEYCODE_BUTTON_THUMBL` toggles the OSK, `KEYCODE_BUTTON_L2` = Menu click; OSK touch cells rows y≈665/740/812/885/960/1035, columns x≈273+68.5·n; pointer by swipe ≈1.5 screen px per swipe px, tap = Select, two taps 0.35 s apart = double-click. Elite: Filer click `:0`, double-click `!élite`, click Jameson; verification word from the manual PDF (printed page N ≈ PDF page ⌈(N+1)/2⌉+3); launch aims at the planet (crash ≈12 s unless you turn or decelerate); Preferences via Menu on the Jameson icon-bar icon.

## Branch / PR
`main` = this baton. No implementation PR open. Governance PRs #42, #44–#58. Evidence branches `evidence/slice1-android-run7`, `evidence/input-slice-s5`, `evidence/slice2a-odin3`, `evidence/slice2b-host`, `evidence/slice2b-odin3` (never merge).
