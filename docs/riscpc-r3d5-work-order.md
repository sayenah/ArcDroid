<!-- SPDX-License-Identifier: GPL-2.0-only -->

# R3d-5 work order: launch wiring on ordinary RiscPC loads (M2)

Status: **adopted (S71)** after Astra stages 1 and 2 (REJECT) and 3 (ACCEPT WITH CHANGES); every finding incorporated. Installed-product (PRODUCT) launch is split out to **R3d-5b** (below). Plan: `docs/riscpc-ordinary-load-plan.md` §6.5, §3 (three epochs), §7/§7a, amendment 23, Astra Q10/Q11. Governing: D-184, D-185, D-190, D-203, D-204, D-217, D-218, `docs/riscpc-launch-plan.md` (launch contract, cost ceilings), `docs/riscpc-r3d3-work-order.md` R-6, `docs/riscpc-r3d4-design.md` (R-2, R-8), `docs/riscpc-r3d6-work-order.md` R6-10. Base: `main` after R3d-4; in parallel with R3d-6 (PR #601).

## Objective

On gate-open ordinary RiscPC loads, deliver the chosen content at most once per explicit attempt through R3c-3a's gate and R3c-3b's controller, with a notice for every unavailable, declined or ambiguous case; the presentation-only status hold ends on the attempt's real state.

## Verified state (`main` `294fecd`, corrected by Astra)

- R3c-3b part 2 merged (`d42adbe`, D-205); boot-watch and launch-outcome cells promoted on all seven pairs (D-203; `engine/rpc/rpc_observer.c:86-103`).
- Pure modules: plan v2 (`core/rpc_launch_plan.h:5-33`; BOOT/APPLICATION/PRODUCT, `RAMFS`; "index -1 selects only an unambiguous application; boot is always explicit"), attempt and token (`core/rpc_launch_attempt.h`; `arc_riscpc_attempt_prepare` is the only issuer; RESET, failed SWAP_ROLLBACK, FAULT, UNLOAD interrupt and bump the epoch; a successful rollback preserves the attempt), controller (DESKTOP when `Wimp_StartTask` returns a task).
- Preparation accepts a plan (`libretro/rpc_preparation.h:73-74`) and issues one token at START when the store is populated, revision 2, without a supplied helper (`libretro/rpc_preparation.c:658-694`); fewer than two configured floppies suppress launch (D-190); Restart sets `launch_suppressed`, never cleared by restart; D-204 retirement is separate (`:393-406`). `product_step` builds the PRODUCT plan with `arc_riscpc_plan_build_product` and, when the product is missing, calls `failed()` and stops the session (`:306-331`, `:666`).
- Ordinary RiscPC loads build no plan (`libretro/arcdroid_libretro.c:7746-7747`) and log Launch, Chooser and RAM disc as ignored (`:7765`). The RPC run branch returns before the existing chooser branch (`:8991`); chooser confirmation constructs an Arculator machine (`:7468`).
- N-12 ("not available yet") and the 25 s presentation-only hold live in the host (`libretro/rpc_preparation_host.c:71-75`, `:276`, `:341-365`); the host has no consumer of `arc_riscpc_preparation_attempt()` (`rpc_preparation.c:847`).
- Discovery: floppy drive 0 via `arc_launch_discover` (`core/launch_discovery.c:2131`); RPC hard disc via the bounded `arc_launch_discover_rpc`; `arc_launch_recovery_decide` returns HELPER, CHOOSER or NONE (`:1699-1717`) and ignores `boot_option`, which discovery records separately (`:2029`).
- Records: full key (`core/rpc_boot_record.h:8`, plan and helper digests included); read and removed by recovery under ownership (`libretro/rpc_launch_record_store.c:54`); the observation accessor requires RUNNING and a captured binding (`rpc_preparation.c:853`); no production selection consumer. The record media lease `arc_rpc_media_identity` succeeds only with Disk Writes Off, so under the default no record is published (`arcdroid_libretro.c:8123-8145`); the launch contract rejects base digests or mount generations as substitutes for acknowledged writes (`docs/riscpc-launch-plan.md:350`), and floppy shadows survive reload (`arcdroid_libretro.c:1870`).
- Choice and binding files (`<save>/arcdroid/launch/<content_id>.choice|.bind`, D-031) are keyed by `arc_plan.content_id`, set only on the Arculator path. Arculator requests RAMFS from helper mode, the option and OS support (`:4710-4714`).
- `test_cells` only masks cells (`engine/rpc/rpc_observers.c:550`); authored firmware enables zero production cells (R3d-4 R-2, `engine/arcdroid_engine.c:324-333`).
- Ordinary-route `retro_*` launch coverage does not exist; production-source orchestration tests exist (`tests/rpc_orchestration_test.c`).

## Rulings (Fable; revised after Astra stage 1)

- **R5-1 Attempts.** An explicit attempt is created by `retro_load_game`, or, when the RiscPC chooser is shown, by its confirmation — never both for one load. Exactly one token for an admitted launch; zero when launch is off, unavailable, declined or refused. Restart, failed swap rollback, fault and unload interrupt and never relaunch (existing machinery); a genuine close and reopen is a new load. The player is told to close and reload the content to run it again (Q10). Secondary-instance refusal is preserved.
- **R5-2 RiscPC chooser lifecycle.** A new owned RiscPC selection state, entered at load before any preparation status: it snapshots the options and the RTC epoch at load; it shows drive-0 candidates (applications by ordinal, BOOT when a root `!Boot` file exists, Desktop); confirmation hands the chosen plan to preparation, which issues the one token; cancel or Desktop starts the session with no plan; disk-control setters are refused while choosing; Restart before confirmation returns to the chooser without an attempt; unload before confirmation releases everything with no attempt. It never constructs an Arculator machine.
- **R5-3 Option precedence and mapping.** Launch Off → no plan. Launch App-N → APPLICATION with that ordinal, guarded by the D-031 binding (an invalid ordinal or a changed mapping refuses with a notice, never another program). Launch Auto with Chooser Ask → the chooser. Launch Auto with Chooser Remember → the remembered answer while its binding holds, else R5-4. Mid-session option changes apply on the next load. RAM disc follows Arculator's rule (helper mode, option, OS support) and maps to the plan's RAMFS flag; Disabled never.
- **R5-4 Auto on RiscPC.** This rule governs Auto without a valid remembered choice; a remembered or newly confirmed choice is an explicit selection and follows R5-3's validation (binding, ordinal, unique `!Boot`). Auto launches only when `arc_launch_recovery_decide` on drive 0 returns HELPER (exactly one verified application, complete scan) and the disc does not start itself. Every other result gets no plan and a readiness-neutral notice: CHOOSER (ambiguous) → the chooser instead when Chooser is Ask, otherwise the notice; NONE for any reason (no verified target, unknown, unaddressable or non-application candidates, incomplete or unreadable scan) → notice. A disc that declares its own startup (discovery records `boot_option` with a root `!Boot`) is never auto-launched — this is conservative, not a claim that the guest ran it; a frozen notice tells the player how to choose its startup or an application explicitly. Auto never selects BOOT (the launch contract keeps BOOT explicit). The chooser's BOOT row appears only for a unique, valid root `!Boot` file.
- **R5-5 PRODUCT is out of R3d-5.** Installed-product launch needs selection under ownership after RECOVER (the existing interface verifies only an already named target, and an unchanged installer disc would otherwise win), typed verification results and an installer-coexistence policy. It becomes slice **R3d-5b** with its own order and Astra challenge; R3d-5 never builds a PRODUCT plan, and the plan's 084 install-then-launch leg moves to R3d-5b.
- **R5-6 Records.** Records stay advisory and never grant execution. Publication keeps today's refusal wherever a complete presentation identity cannot be supplied (default Disk Writes included; records were never required). A maintained presentation fingerprint is out of scope. Amendment 23 (path spelling invalidates a record without refusing the store) is implemented.
- **R5-7 Plan media digest.** A production source digest of the ordered media, derived from the already computed member digests and lengths with its own versioned encoding (no full read at load); documented as a source identity only, never a presentation identity.
- **R5-8 Presentation and hold (frozen).** The host samples a copied attempt state at its existing serialized boundary (read-only accessor). Presentation expiry never affects execution permission. Rules: (a) no attempt (Off, declined, unavailable, one-drive CMOS, revision-1 store, refused) — the readiness-neutral notice for the reason, then today's 25 s presentation cap and clear; (b) PREPARED, PENDING or WAITING — status shown, cleared at the 25 s cap without any claim (the attempt may still complete or be consumed later; a late consumption shows nothing new); (c) DESKTOP, RETURNED, TERMINATED — clear at once; (d) REJECTED — clear and the rejected notice; (e) interrupted by Restart, failed swap rollback, fault or unload — clear; for Restart, one notice per session to close and reload the content to launch again (N-10); (f) UNKNOWN (cells disabled, or the 120 s horizon passed) — clear at the cap, no notice claiming cancellation or success, a log line only; (g) a guest prompt reported by the boot-watch or outcome cells clears the presentation, and it is not shown again for that attempt. Terminal priority, unload cancellation and advisory lifetimes are unchanged; the hold never claims readiness (R3d-3 R-6).
- **R5-9 Notices.** N-12's "not available yet" is replaced: distinct readiness-neutral texts for launch intentionally off, delivery unavailable, and each refusal (rejected plan, invalid ordinal, binding changed, ambiguity unanswered, no verified application, incomplete scan, disc declaring its own startup); N-10 says close and reload. Fresh IDs after R3d-6's N-22 to N-25; texts frozen in the design note before code; privacy rules of §7a (no paths, titles or member names).
- **R5-10 Test seam.** A synthetic test-only seam that enables launch cells for the ordinary-route harness, applied before attempt binding, disclosed in the build identity, excluded from shipping builds and distribution by the existing guards; independent assertions that authored production binding enables zero cells remain.
- **R5-11 R3d-6 coordination (R6-10).** Whichever merges second reconciles the combined head: Launch, Chooser and RAM disc help texts and meaning-matrix rows, the ignored-options log line, N-12's pin, notice IDs.

## Scope

R5-1 to R5-11; a design note (`docs/riscpc-r3d5-design.md`) written before code with the chooser state machine, precedence table, Auto branches, file keys and encoding (kind-tagged names, bounds, publication owner; Arculator filenames unchanged), digest encoding, presentation matrix and notice texts.

## Non-goals

Part 2's state machines, transport, profiles or cells; Arculator launch behaviour; PRODUCT (R3d-5b); a presentation fingerprint for records under shadow or JFD overlays; automatic Arculator fallback (D-213).

## Acceptance criteria

- **AC-1** Zero tokens when off, unavailable, declined or refused; exactly one for an admitted launch, through the ordinary frontend route; Restart, failed swap rollback, fault, unload, guest reboot before and after consumption, late consumption and retirement failure never cause a second execution; close and reopen launches once more.
- **AC-2** R5-2's lifecycle: chooser before any preparation status; cancel and Desktop; Restart and unload while choosing; disk setters refused while choosing; load plus confirmation create one attempt.
- **AC-3** R5-3 and R5-4 tables, including invalid ordinals, stale remembered choices, binding changes, the ambiguous branch, every NONE case, and a disc declaring its own startup and holding both an application and `!Boot`: Auto without a remembered choice issues zero tokens, a remembered choice and a newly confirmed choice each issue one (three separate cases; preserved CMOS included); tests distinguish the metadata, the helper tokens and actual guest execution.
- **AC-4** Ordinary-load selection never requests or builds a PRODUCT plan (R5-5); existing explicit plan-route PRODUCT support and its tests are unchanged.
- **AC-5** R5-6/R5-7: records unchanged in behaviour (refused without a complete presentation identity), amendment 23, digest vectors for the source digest.
- **AC-6** R5-8's presentation matrix; R5-9 texts pinned; privacy sentinels.
- **AC-7** R5-10's seam is test-only and disclosed; shipping cells unchanged.
- **AC-8** Arculator launch behaviour and persisted bytes identical (harness and Fable's library A/B); gate-closed option metadata and behaviour identical, with only enumerated gate-open differences.
- **AC-9** Builds, guards, the full local PR tier (D-212), new tests placed exactly once in the PR partitions.

## Evidence and legs (Fable, private)

Required, recorded as owed until done: on the Android test device in joint sessions (D-218) — the seven pairs (firmware permitting; any pair not run is listed as owed), returning commands, desktop applications, non-returning titles and prompts, chooser visibility and input release, Restart mid-attempt, real close and reopen, pause and resume,  preparation and launch cost against part 2's numeric ceilings and D-180 with observers on and off in paired runs, against the phone's own baseline (the long rule-4 baseline of S1 is a separate obligation). On Windows RetroArch the same functional set, after the box's RetroArch crash under the ordinary-route configuration is investigated (D-216 follow-up). Mac library A/B in both gate states.

## QA focus

At-most-once through RetroArch's own Restart and close; chooser lifecycle and timing; Auto branches and remembered choices; the three epochs kept distinct; records never granting execution; notice truth and privacy.

## Astra stage 1 (S71): adjudication

REJECT; all nine findings incorporated: (1) PRODUCT not offered from records (superseded by stage 2: PRODUCT moved to R3d-5b); (2) no source-for-presentation substitution; record refusal kept, source digest only (R5-6, R5-7); (3) chooser lifecycle specified, zero-or-one tokens (R5-1, R5-2, AC-1, AC-2); (4) Auto branches separated, never BOOT, boot-enabled disc case (R5-4); (5) full presentation matrix, disabled cells not a no-attempt case (R5-8); (6) precedence, RAMFS rule, file encoding, N-12 replacement, N-10, privacy (R5-3, R5-9); (7) named test seam and wider ACs (R5-10, AC-3 to AC-8); (8) device legs enumerated, owed results recorded (Evidence); (9) verified-state corrections.

## Astra stage 2 (S71): adjudication

REJECT; all five findings incorporated: (1)–(3) PRODUCT needs deferred selection under ownership, an installer-coexistence policy and typed verification results — split out to R3d-5b rather than specified half-way (R5-5); (4) Auto launches only on HELPER and a non-self-starting disc, every NONE case refused with a notice, BOOT row only for a unique valid `!Boot` (R5-4, AC-3); (5) presentation rules frozen per state (R5-8).

## R3d-5b (to be ordered after R3d-5)

Installed-product launch: a deferred selection request resolved under ownership after RECOVER; product eligibility excluding the supplied boot environment; typed results (verified, proven absent, ambiguous or incomplete, operational or store failure) with a desktop fallback only when the session stays usable; precedence against the installer disc and remembered choices; the plan's 084 install-then-launch leg.

## Astra stage 3 (S71): adjudication

ACCEPT WITH CHANGES; all three incorporated: (1) R5-4 governs Auto without a remembered choice; remembered and confirmed choices follow R5-3, three AC cases; (2) "declares its own startup" wording, a frozen notice, tests separating metadata, tokens and execution; (3) the PRODUCT prohibition scoped to ordinary-load selection, QA focus updated.
