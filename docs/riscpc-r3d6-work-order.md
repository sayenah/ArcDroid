<!-- SPDX-License-Identifier: GPL-2.0-only -->

# R3d-6 work order: manual RiscPC choice and option meaning per session kind

Status: **draft for the Astra challenge** (S71). Plan: `docs/riscpc-ordinary-load-plan.md` §6.6, amendments 12 and 13. Governing: `docs/riscpc.md` §4 item 12, D-163, D-164, D-165(1)(2), D-166(1)(2), D-186, D-211 (R-5), D-213, `docs/riscpc-r3d4-design.md` (R-1 to R-10). Base: `main` after R3d-4 (merged as `4480a77`).

## Objective

On gate-open builds a player can choose the RiscPC model and RISC OS version by hand. Every existing core option has a stated, tested meaning in each session kind (Archimedes, RiscPC). Per-game option files keep loading in both gate states. Gate-closed builds stay byte-identical in options and behaviour.

## Verified state (`main` `f41b0f5`)

- One static option table, `arc_base_options` (`libretro/arcdroid_libretro.c:6514-6572`): 20 base options plus 23 built at run time (pads, OSK toggle, left stick), `ARC_OPTION_COUNT` 43, asserted at `:6579-6580`. The core uses `SET_CORE_OPTIONS_V2` with one category (`pad_mapping`); no display or update-display callback.
- The gate (`ARC_RPC_ORDINARY_LOAD`, `libretro/rpc_route.h:7-10`) changes only seven help texts (`:6509-6513`); `tests/rpc_route_metadata_test.py` asserts that and nothing else differs.
- No RiscPC option exists. Manual model and OS exist only in the pure API `arc_riscpc_select(..., manual_model, manual_os)` (`core/rpc_selection.h:60-65`; per-axis override per D-166(2) at `core/rpc_selection.c:64-65`). The only production call passes Auto (`libretro/arcdroid_libretro.c:7698-7699`).
- RiscPC is considered only when Machine, CPU and OS are all Auto (`:7859`) and a prepared disc carries RiscPC hints (`core/rpc_selection.c:30-33`); a non-Auto Archimedes axis keeps flagged content on Arculator with N-9 (`:1782-1788`).
- `arc_read_options` refuses the whole load on any unknown value of any key except Disc Requests (`:867-993`, effect at `:7837-7841`; test `tests/libretro_harness.c:4896-4899`).
- On RiscPC sessions: Machine/CPU/OS decide the kind only at load; Memory has no effect anywhere; Joystick, Hard Disc, ADFFS, JFD Boot, Launch, Chooser and RAM disc are ignored (one log line, `:7765`); Disk Writes, Write-protect Tab, Short tail and Omitted sectors are frozen at load with the N-11 advisory on change (`:8238-8262`); Disc Requests, pad, pointer, OSK and sticks are live.
- RiscPC model and OS values are written to disk in the CMOS key (`:7748`, `libretro/rpc_cmos_store.h:18-22`) and the pair sidecar (`libretro/rpc_route.c:123-160`, bytes 16 and 17); no test pins them.
- The pair-change advisory (`libretro/rpc_preparation.c:713-718`) names firmware change as the cause.

## Rulings (Fable, before Astra)

- **R6-1 Options.** Two new options, declared only in gate-open builds, after the existing base options and before the run-time ones: `arcdroid_rpc_model` (Auto, ARM610, ARM710, StrongARM, ARM7500) and `arcdroid_rpc_os` (Auto, RISC OS 3.50, 3.60, 3.70, 3.71); default Auto. Gate-closed tables, counts and help texts stay byte-identical (AC-1). The open-build count asserts and `rpc_route_metadata_test.py` change deliberately to admit exactly these two keys.
- **R6-2 Precedence.** Any non-Auto Archimedes axis (Machine, CPU, OS) keeps the session on Arculator, as today (N-9 on flagged content), whatever the RiscPC options say; the RiscPC options are then inert and the log says so. With all Archimedes axes Auto, a non-Auto RiscPC axis requests a RiscPC session for any content (amendment 13: the inventory scan runs then, including raw ADF/ADL and legacy JFD) and passes its value to `arc_riscpc_select` per D-166(2). Recognition and the qualified-pair gate are never bypassed: an unqualified or unavailable pair is refused with the existing class notices (J3 for a pair whose firmware is not installed), never a silent fallback to Arculator (D-213).
- **R6-3 When changes apply.** Changing a RiscPC axis, or an Archimedes axis, during a session applies on the next content load (close and reload), never on Restart (amendment 12; R3d-4 R-1). A mid-session change gives one advisory per option per session saying so.
- **R6-4 Unknown or stale values.** No option value ever refuses a load. An unknown value for a known key falls back to that key's default with one notice per session naming the option; `tests/libretro_harness.c:4896-4899` changes deliberately to test the fallback. The launch binding file (D-031) keeps refusing a silently changed ordinal-to-application mapping; that is not an option-value check.
- **R6-5 Meaning and visibility.** Every option's help text states its meaning in RiscPC sessions where it differs (ignored, frozen until Restart, applies on reload, Archimedes only). Hiding options per session kind (`SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK`) is used only if Sol shows from RetroArch 1.22.2's sources and a test that it works on the menu drivers used on Windows and Android and that hidden options are still saved and restored; otherwise help text alone.
- **R6-6 Memory.** Stays an Archimedes option (help text says so); RiscPC memory is unchanged.
- **R6-7 Manual pair change.** When a manual choice changes the pair of content that already has a RiscPC store, the hard disc is kept (identity per content, D-211 R-5), the pair's own CMOS record is used or created (never copied), boot records keyed to the old pair are not used, and the advisory names the manual choice as the cause (a distinct text from the firmware-change advisory at `rpc_preparation.c:713-718`).
- **R6-8 Pinned values.** The RiscPC model and OS values stored in the CMOS key and the pair sidecar are pinned by a test, as `tests/rpc_selection_test.c:46-51` pins the legacy enums; the option values map onto them without changing them.

## Scope

1. R6-1 to R6-8.
2. `.opt` compatibility in both directions: a per-game and a global `.opt` written by a gate-open build (with the two RiscPC keys) loads in a gate-closed build, and the reverse, without failure; what RetroArch 1.22.2 does with keys the running core does not declare (kept, dropped on save) is established from its sources and recorded.
3. The meaning matrix (every option × both kinds × load, Restart, run-time change), tested through `retro_*` in a switching sequence.

## Non-goals

R3d-5 launch wiring; RiscPC memory or new machines; automatic Arculator fallback (D-213); changes to recognition, qualification or pair preference (D-164(2)); any gate-closed behaviour change.

## Acceptance criteria

- **AC-1** Gate-closed option tables, counts, help texts and load behaviour byte-identical to `main` (except R6-4's fallback, which applies in both states and is tested in both).
- **AC-2** The override matrix of R6-2 and D-166(2) through the frontend route: each axis alone, both, with each Archimedes axis set, with unqualified and uninstalled pairs; recognition and qualification never bypassed.
- **AC-3** `.opt` files written by either gate state load in the other (scope 2), per-game and global.
- **AC-4** The meaning matrix (scope 3) in a switching sequence, including R6-3's advisories.
- **AC-5** Arculator library A/B unchanged (Fable's Mac leg); Arculator output byte-identical in the harness for every option combination that does not involve the RiscPC options.
- **AC-6** R6-7 through a store reused across a manual pair change; R6-8's pinned values.
- **AC-7** Full local PR tier, MinGW and Android builds, every guard, export and identity checks (D-212).

## Evidence and legs

Sol: the tests above, mutants for precedence, fallback and the pinned values. Fable (private): one manual-override leg on Windows RetroArch and one on the Android test device (D-218, joint session), visible in the menu; gate-closed Mac library A/B.

## QA focus

`.opt` compatibility both ways; precedence; pinned stored values; no load refusal from option values; gate-closed identity.
