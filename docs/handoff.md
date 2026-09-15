# ArcDroid Handoff

**Stage:** ARCHITECTURE  
**Owner:** Fable  
**Next:** (1) Carried validation of `main` when the hardware is available: Windows-PC real boot + tone (AC-12(b) of Slice 1) plus a Windows physical-mouse/XInput pass of the Input Slice on the Founder PC; on-device CMOS-mutating persistence; Retroid Pocket AC-11; two-finger tap and the Odin3's own sticks by hand. (2) Slice 2 architecture: write the work order for launch automation (boot-option-0 discs), m3u/libretro disk control, and hardened readers (JFD first, truncated-dump policy), obtain the Astra challenge, adjudicate, then set `Stage: IMPLEMENTATION`, `Owner: Sol`. If the Founder prefers media (m3u/JFD) before launch automation, split Slice 2 and put media first. No implementation is authorized by this baton.

## Objective and governing requirement
Reach the PRD north star (game-first launch, §2) on both platforms: a boot-option-0 disc should reach its game without desktop interaction (FR-24…FR-27), multi-disc titles should swap through RetroArch disk control (FR-8), and protected/unusual media should load through hardened readers (FR-9, FR-10, D-007). Governing: PRD §11, §8, §23; protocol §8–§13; D-007, D-012, D-014, D-019, D-025.

## Verified state (2026-09-14, S5 close)
- `main` = Slice 1 (PR #5) + Input Slice (PR #24, `slice/01b-input-controller-pointer`, final head `22f9244`) + governance PRs. Input Slice delivered: controller-driven pointer (right stick velocity, dead zone, quadratic curve, R2 precision, R1/L1/L2 = Select/Adjust/Menu, options `arcdroid_pointer_stick/speed/deadzone`), touch gestures per D-027, source-aware bounded input FIFO with consumption-acknowledged delivery through one hook in `keyboard.c`, E-format ADFS recognition, guard-script export assertions. Evidence: `evidence/input-slice-s5` (Odin3), Opus QA + targeted re-check at the final head, CI 8/8.
- Input-slice facts that constrain Slice 2: input is sampled once per `retro_run` and delivered one KART event per 10 ms guest poll; the FIFO holds 256 transitions with reconciliation on overflow; touch thresholds are 250 ms / 1 % / 80 ms in guest time; `arc_guest_time_us` advances only when a frame ran.
- Media facts: `arc_media_is_recognised_adfs` recognises old-map (D/L) and single-zone E-format 819 200-byte images; both Founder discs are E-format with boot option 0; Repton 4's real dump is 814 080 bytes (5 KiB short) and is rejected by exact-size geometry; the engine's JFD/APD/HFE/FDI/SCP readers are excluded from the import (D-007, `engine/UPSTREAM.md`); the libretro disk-control extension is registered since Slice 1 with a single image.
- Device: Odin3 state as left by S5 (`ROM311` private, `chess.adf`, `repton4.adf`, run config with logging/game focus/Virtual keyboard/Remote RetroPad, `22f9244`-equivalent core not yet installed — the installed core is `10b7bf0`). RetroArch not running.

## Slice 2 candidate scope (to be challenged by Astra)
1. Launch automation: for FileCore discs whose boot option is 0, discover a root `!App` (or a single application directory) and dispatch it after the desktop is up, with a per-title override and a fallback to the desktop (FR-24…FR-27, P-3, no fragile title hacks per FR-25). Decide the mechanism (guest-side keystroke injection through the new FIFO vs. a CMOS/boot-option strategy vs. a generated `!Boot`) with Astra.
2. m3u and disk control: multi-image playlists, eject/insert/next through `RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE`, per-content identity for CMOS (D-019) across a playlist, VFS staging of every image (D-014).
3. Hardened readers: JFD first (bounds-checked import of the upstream reader or a rewrite, fuzz-style malformed-image tests, synthetic JFD fixture in `tests/generate_fixtures.py`), then a truncated-dump policy (pad vs. reject) for short ADF/ADL images.

### Non-goals (Slice 2)
Save states (D-011, separate slice), A3010 joystick, on-screen keyboard, per-title input profiles, APD/HFE/FDI/SCP (after JFD), CD media, PRD changes.

## Acceptance criteria
To be written with the Slice 2 work order after the Astra challenge.

## Deterministic evidence required
Generated fixtures only in CI (D-012, D-020); private discs as local Founder/Opus evidence; evidence branches per D-025.

## Licensing / security / provenance and QA focus
JFD reader provenance and licence (upstream Arculator GPL-2.0-only) must be recorded if imported; no game or firmware bytes in fixtures; launch automation must never write to the user's original image (D-014).

## Known limitations and follow-ups
1. Pad-only access to the full keyboard and per-title profiles (FR-29) for titles beyond the ten-key map.
2. Optional tap-and-a-half touch drag if the Founder wants touch-only dragging.
3. RetroArch Android keyboard routing (`input_android_physical_keyboard`, game focus) is player documentation. Audio 50 ms underruns at mode changes (D-022); host `ctest` never dlopens the shipping library; `arcdroid_os` override gaps; 16 KiB-page runtime check.
4. Remote RetroPad UDP packet loss can drop an injected edge during device runs; use screenshot-verified sequences.

## Orchestration notes (Fable)
- Sol dispatch: Codex CLI from a fresh clone under the session scratchpad: `codex exec -m gpt-5.6-sol --sandbox workspace-write -C <clone> -c sandbox_workspace_write.network_access=true --json -o <report.md> - < <prompt.md>` via Bash `run_in_background`; resume with `codex exec resume <thread-id>` for remediation rounds. Astra: `codex exec -m gpt-6-astra` (blind pass with the candidate withheld from the scratch clone's handoff, then `resume` for the candidate pass). Opus QA: Agent tool (`model: opus`, `general-purpose`) on a fresh clone; may use the device; targeted re-checks by SendMessage to the same agent.
- Process ownership: record PIDs of anything started; force-stop only RetroArch instances started in the run.
- Dev Mac: NDK `/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865` (`darwin-x86_64` prebuilt dir), adb under `platform-tools`, MinGW `x86_64-w64-mingw32-gcc`, `JAVA_HOME=/opt/homebrew/opt/openjdk`, no ripgrep, no RetroArch. Odin3 (LAN 192.168.50.215): RetroArch system `/storage/emulated/0/BIOS`, cores `/data/user/0/com.retroarch.aarch64/cores/`, logs `/storage/emulated/0/RetroArch/logs`, run config `/storage/emulated/0/RetroArch/arcdroid-run.cfg`; launch `am start -n com.retroarch.aarch64/com.retroarch.browser.retroactivity.RetroActivityFuture --es ROM <path> --es LIBRETRO <core> --es CONFIGFILE <cfg>` after `input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard`; sideload = push to `RetroArch/downloads/` then Load Core → (fling to end) → Install or Restore a Core → tap the file; pad injection via the Remote RetroPad sender on `evidence/input-slice-s5` (`rpad.py`); touch via `input swipe` (120 ms same-point swipe = tap); keyboard via `input keyboard keyevent/text`. Reproduction on `main`: `cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/host && ctest --test-dir build/host --output-on-failure`; real boot via `build/host/libretro_harness ... --real-boot` with the private ROM, staged copy deleted afterwards.

## Branch / PR
`main` = Slice 1 + Input Slice + governance PRs. No open implementation PR. Evidence branches `evidence/slice1-android-run7`, `evidence/input-slice-s5` (never merge).
