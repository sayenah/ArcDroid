<!-- SPDX-License-Identifier: GPL-2.0-only -->

# R3d-6 work order: manual RiscPC choice and option meaning per session kind

Status: **adopted (S71)** after Astra stage 1 and stage 2 (both ACCEPT WITH CHANGES; every finding incorporated). Plan: `docs/riscpc-ordinary-load-plan.md` §6.6, amendments 12 and 13. Governing: `docs/riscpc.md` §4 item 12, D-163, D-164, D-165(1)(2), D-166(1)(2), D-186, D-211 (R-5), D-213, `docs/riscpc-r3d4-design.md` (R-1 to R-10, G-2). Base: `main` after R3d-4 (`4480a77`) and SS-1b.

## Objective

On gate-open builds a player can choose the RiscPC model and RISC OS version by hand. Every existing core option has a stated, tested meaning in each session kind. Per-game and global option files keep working across both gate states. Gate-closed builds keep their option schema and behaviour.

## Verified state (`main` `f41b0f5`, corrected by Astra)

- One static option table, `arc_base_options` (`libretro/arcdroid_libretro.c:6514-6572`): 20 base options plus 23 built at run time, `ARC_OPTION_COUNT` 43 (asserts `:6579-6580`). `SET_CORE_OPTIONS_V2`, one category; no display callbacks.
- The gate changes seven option help texts and nothing else in the option metadata (`:6509-6513`; `tests/rpc_route_metadata_test.py`). The gate changes other behaviour too (R3d-4 R-6 list).
- No RiscPC option. Manual model and OS exist only in `arc_riscpc_select` (`core/rpc_selection.h:60-65`; D-166(2) per axis at `core/rpc_selection.c:64-65`: a manual model clears only CPU constraints, a manual OS only OS constraints). Production routing passes Auto (`:7698-7699`); diagnostic selection still runs on the legacy path (`:7939`).
- `arc_read_options` refuses the load on an unknown non-empty value of a queried key (except Disc Requests); missing or empty values take the default; undeclared keys are never read (`:860-993`, effect `:7837-7841`). RetroArch 1.22.2 already replaces an unknown value of a declared key with the declared default before the core sees it (`core_option_manager.c:990-1053`), so the refusal is reached mainly from other frontends.
- Archimedes Restart re-reads Machine, CPU and OS, resolves the machine and selects firmware (`:8578`). On RiscPC sessions media options are frozen until Restart, which resamples them (`:8086`); Machine/CPU/OS decide the kind only at load; Memory has no operational effect but an invalid Memory value refuses parsing.
- Stored RiscPC values (Astra finding 8): selection model Auto=0, ARM610/ARM710/SA-110/ARM7500 = 1/2/3/4; engine and CMOS-key model and sidecar byte 16 = 0/1/2/3; CMOS model tag and path = 1/2/3/4; selection OS and sidecar byte 17 = 3.50/3.60/3.70/3.71 = 1/2/3/4; CMOS layout 3.50→1, 3.60→2, 3.70 and 3.71→3, with the firmware SHA-256 distinguishing firmware (`:7748`, `libretro/rpc_cmos_store.c:58`). Existing pins: `tests/rpc_selection_test.c:8`, `tests/rpc_cmos_store_test.c:153`, legacy enums `tests/rpc_selection_test.c:46-51`.
- The pair-change advisory (`libretro/rpc_preparation.c:713-718`) names firmware change as the cause; N-2 (`libretro/rpc_route.c:93`) hard-codes "StrongARM with 3.50 or 3.60".
- RetroArch 1.22.2 and option files: undeclared keys are ignored operationally but retained in an existing global, per-core or per-game file on save (`core_option_manager.c:1114`, `:1890`; `runloop.c:1033`); a new override created while the keys are undeclared does not copy them (`runloop.c:5013`); a per-game file is used instead of, not overlaid on, the global file (`runloop.c:1156`), so keys absent from it default to Auto.

## Rulings (Fable; R6-x after Astra stage 1)

- **R6-1 Options.** Two options declared only in gate-open builds, after the base options and before the run-time ones: `arcdroid_rpc_model` (values `auto`, `arm610`, `arm710`, `sa110`, `arm7500`; labels Auto, ARM610, ARM710, StrongARM, ARM7500) and `arcdroid_rpc_os` (`auto`, `350`, `360`, `370`, `371`; labels Auto, RISC OS 3.50, 3.60, 3.70, 3.71); default Auto. Literal value tokens are frozen independently of labels. Gate-closed option metadata (keys, values, defaults, counts, order, help) is byte-identical to `main`; the open-build count asserts and `rpc_route_metadata_test.py` change deliberately to admit exactly these two keys and the help texts of R6-5.
- **R6-2 Precedence.** Any non-Auto Archimedes axis (Machine, CPU, OS) keeps the session on Arculator, as today (N-9 on flagged content); the RiscPC options are then inert; only when an explicit Archimedes axis and an explicit RiscPC axis are both set is the player told once (a notice naming the conflict, in addition to the log line). With both RiscPC options Auto, Archimedes output is unchanged. With all Archimedes axes Auto, a non-Auto RiscPC axis requests a RiscPC session for any content (amendment 13: the inventory scan runs then, including raw ADF/ADL and legacy JFD) and passes its value to `arc_riscpc_select` per D-166(2). Hints are overridden only on the chosen axis; recognition, the verified firmware read, qualification and preparation validation are never bypassed. An unqualified or unavailable pair is refused with a truthful notice (never a fallback to Arculator, D-213); N-2's hard-coded pair list is replaced by text that is true for every manual and automatic case. All-Auto loads of unflagged content do no extra scanning.
- **R6-3 When changes apply.** The session kind and, on RiscPC, the admitted pair are fixed until the content is closed and reloaded; Restart never changes them. Archimedes Restart keeps its current behaviour (re-reading Machine, CPU and OS within Arculator). A mid-session change of a RiscPC axis, or of an Archimedes axis during a RiscPC session, gives one advisory per option per session that it applies on reload. These advisories and the new help texts are enumerated gate-open differences (R3d-4 R-6 list), not exceptions to gate-closed identity.
- **R6-4 Unknown values.** Existing keys keep today's behaviour in both gate states. For the two new keys an unknown non-empty value falls back to Auto with one notice per session; no RiscPC option value ever refuses a load. The notice is promised only for values the frontend actually delivers (RetroArch substitutes the default for values it does not recognise).
- **R6-5 Meaning and visibility.** Help text only in this slice: every option whose meaning differs on RiscPC says so (ignored in M1, frozen until Restart, applies on reload, Archimedes only), and the RiscPC options state the precedence of R6-2. No option is hidden; selectors needed to set up the next load or the Archimedes override stay visible.
- **R6-6 Memory.** Stays an Archimedes option (help text says so); behaviour unchanged.
- **R6-7 Manual pair change.** The hard disc stays content-owned (D-211 R-5); each pair keeps its own CMOS record, never copied (A→B→A reuses A's record). A boot record is used only when its complete binding matches (CPU, firmware, configuration, content, media, store, plan, helper, revisions; `rpc_preparation.c:337`), never on the pair alone. The advisory states that the model or OS changed, without inventing a cause (manual choice, firmware change and a manual choice reverted to Auto can all change the pair). Sidecars stay advisory; publication stays under the existing content lock.
- **R6-8 Stored values.** The cross-layer mapping above is preserved exactly; add golden-byte tests for the sidecar and the CMOS key and record, and keep the legacy enum pins, record versions, paths and option tokens.
- **R6-9 Privacy before staging.** When a manual RiscPC request is in effect, the effective route is decided before staging and RiscPC-safe diagnostics are used from that point whenever the RiscPC request wins precedence, including staging failures and arena exhaustion; when an Archimedes axis suppresses the request, legacy staging output is kept (apart from N-9 and the conflict notice) (today the safe form follows observed RPC flags, `:7867`, `libretro/rpc_staging.h:48`).
- **R6-10 R3d-5 coordination.** R3d-6 tests M1 semantics (Launch, Chooser and RAM disc ignored on RiscPC), with the rows R3d-5 changes marked in the test matrix; whichever of R3d-5 and R3d-6 merges second re-checks the combined head and updates those rows together with the production texts that describe M1 (help texts such as "RiscPC starts manually", the ignored-option log line, notices) and their exact metadata expectations. Boot-record integration belongs to R3d-5; R3d-6 only guarantees R6-7.

## Scope

1. R6-1 to R6-10.
2. `.opt` compatibility (AC-3 meaning: no option-schema failure; a restored manual choice may still be refused for missing firmware): open → closed → open with a save in between, undeclared keys retained by RetroArch, absent keys defaulting to Auto, new override creation, per-game versus global.
3. The meaning matrix (every option × both kinds × load, Restart, run-time change), through `retro_*` in switching sequences: pending, failed and running sessions; Restart without an intervening `retro_run`; selection changes during preparation; failed-load cleanup; Arculator → RiscPC → Arculator.

## Non-goals

R3d-5 launch wiring; RiscPC memory or new machines; automatic Arculator fallback (D-213); changes to recognition, qualification or pair preference (D-164(2)); option visibility callbacks; gate-closed behaviour changes.

## Acceptance criteria

- **AC-1** Gate-closed option metadata and load behaviour byte-identical to `main`; the closed default, upload classifier, `.info`, notice sources and pinned identity baseline intact.
- **AC-2** R6-2's matrix through the frontend route: each RiscPC axis alone and both; each Archimedes axis set; unqualified pairs (e.g. ARM610 with 3.71, StrongARM with 3.50) and uninstalled pairs; content limited to an Archimedes OS with only a manual model; recognition and qualification never bypassed; conflict notice shown once.
- **AC-3** Scope 2.
- **AC-4** Scope 3, including R6-3's advisories and Archimedes Restart unchanged.
- **AC-5** Arculator library A/B unchanged (Fable's Mac leg, both gate states); Arculator output byte-identical in the harness for every combination not involving the RiscPC options. Gate-open comparisons exclude exactly the authorized option metadata and help changes (R6-1, R6-5) and the applicable new notices (R6-2, R6-3); all other execution, logs, media and persisted bytes stay identical. Gate-closed comparisons have no exclusions.
- **AC-6** R6-7: A→B→A, OS-only change, unchanged resolved pair, failed admission, corrupt or missing sidecar, interrupted publication; R6-8 golden bytes.
- **AC-7** R6-9: malformed raw and legacy JFD, ZIP and playlist failures under a manual request, with RiscPC-safe output wherever the RiscPC request wins; an Archimedes override suppressing it keeps legacy staging output (apart from N-9 and the conflict notice); no extra scan for all-Auto unflagged loads.
- **AC-8** Full local PR tier, MinGW and Android builds, every guard, export and identity checks (D-212).

## Evidence and legs

Sol: the tests above, mutants for precedence, the RiscPC-key fallback, pinned values and the privacy switch. Fable (private): manual-override legs on Windows RetroArch and on the Android test device (D-218, joint session) that check save and reopen of the option and the visibility of refusal notices, not only menu presence; gate-closed Mac library A/B.

## QA focus

`.opt` compatibility both ways; precedence and conflict notices; pinned stored values; Archimedes Restart unchanged; privacy before staging; gate-closed identity.

## Astra stage 1 (S71): adjudication

All ten findings incorporated: (1) fallback limited to the new keys (R6-4); (2) Archimedes Restart preserved, kind and pair fixed until reload (R6-3); (3) AC-3 defined as schema compatibility with RetroArch's retention rules (scope 2); (4) precedence notices and N-2 text (R6-2); (5) privacy before staging (R6-9, AC-7); (6) help text only (R6-5); (7) full boot-record binding and truthful pair-change wording (R6-7); (8) stored-value mapping corrected and golden bytes (R6-8); (9) M1/M2 coordination and lifecycle cases (R6-10, scope 3, legs); (10) verified-state corrections.

## Astra stage 2 (S71): adjudication

ACCEPT WITH CHANGES; all three findings incorporated: (1) the conflict notice only when an explicit Archimedes axis and an explicit RiscPC axis are both set, and RiscPC-safe staging only when the RiscPC request wins (R6-2, R6-9, AC-7); (2) AC-5's gate-open comparison projection stated; (3) R6-10's combined-head reconciliation includes the production M1 texts and their metadata expectations.
