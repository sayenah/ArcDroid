<!-- SPDX-License-Identifier: GPL-2.0-only -->

# R3d-5 work order: launch wiring on ordinary RiscPC loads (M2)

Status: **draft for the Astra challenge** (S71). Plan: `docs/riscpc-ordinary-load-plan.md` §6.5, §3 (three epochs), §7/§7a (notices), amendment 23, Astra Q10/Q11. Governing: D-184, D-185, D-190, D-203, D-204, D-217, D-218, `docs/riscpc-launch-plan.md` (launch contract, cost ceilings), `docs/riscpc-r3d3-work-order.md` R-6, `docs/riscpc-r3d4-design.md` (R-2, R-8), `docs/riscpc-r3d6-work-order.md` R6-10. Base: `main` after R3d-4; runs in parallel with R3d-6 (PR #601).

## Objective

On gate-open ordinary RiscPC loads, deliver the chosen content at most once per explicit attempt through R3c-3a's gate and R3c-3b's controller, with a notice for every unavailable or ambiguous case. N-12 goes; the presentation-only status hold ends on the attempt's real outcome.

## Verified state (`main` `294fecd`)

- R3c-3b part 2 is merged (`d42adbe`, D-205) and its boot-watch and launch-outcome cells are promoted on all seven pairs (D-203; `engine/rpc/rpc_observer.c:86-103`).
- Pure modules: plan v2 (`core/rpc_launch_plan.h:5-33`; actions BOOT/APPLICATION/PRODUCT, `RAMFS` flag; `arc_riscpc_plan_build` with index -1 = only an unambiguous application), attempt and token (`core/rpc_launch_attempt.h`; `arc_riscpc_attempt_prepare` is the only issuer; interrupts RESET, SWAP_ROLLBACK, FAULT, UNLOAD bump the epoch), controller (`core/rpc_launch_controller.h`; DESKTOP when `Wimp_StartTask` returns a task).
- Preparation already accepts a plan (`libretro/rpc_preparation.h:73-74`) and issues exactly one token at START when the store is populated, revision 2, without a supplied helper (`libretro/rpc_preparation.c:658-694`); fewer than two configured floppies suppress launch (D-190); Restart sets `launch_suppressed`, never cleared by restart (`:194-204`, `:864-908`); swap rollback, fault and unload interrupt the attempt.
- The ordinary route passes no plan (`libretro/arcdroid_libretro.c:7746-7747`) and logs Launch, Chooser and RAM disc as ignored (`:7765`). No production code builds a plan or calls discovery for RPC; only the test plan core does (`tests/arc_test_plan.c:443-481`).
- N-12 and the 25 s presentation-only hold live in the host (`libretro/rpc_preparation_host.c:71-75`, `:276`, `:341-365`); the host cannot see the attempt outcome (`arc_riscpc_preparation_attempt()` has no host caller).
- Discovery: floppy drive 0 via `arc_launch_discover` (`core/launch_discovery.c:2131`, Arculator caller `arcdroid_libretro.c:4760-4788`); RPC hard disc via the bounded `arc_launch_discover_rpc` (used only by `product_step`). RPC automatic APPLICATION uses `arc_launch_recovery_decide` (exactly one verified application, complete scan), narrower than Arculator Auto.
- No production plan-media digest exists; the record-binding lease `arc_rpc_media_identity` succeeds only with Disk Writes Off, so under the default (shadow) no boot record is ever published (`arcdroid_libretro.c:8123-8145`, `rpc_preparation.c:336-338`).
- Choice and binding files (`<save>/arcdroid/launch/<content_id>.choice`, `.bind`; D-031) are keyed by `arc_plan.content_id`, set only on the Arculator path.
- Boot records: `core/rpc_boot_record.h` (full key), stored at `<save>/arcdroid/hd-riscpc/<content>/launch.observation`; published from maintenance; no production reader.
- At-most-once is tested at module and plan-core level only (`tests/rpc_orchestration_test.c` retirement, guest reboots, reset matrix, observation fault); nothing through the ordinary frontend route.
- Authored firmware never enables production cells (R3d-4 R-2, `engine/arcdroid_engine.c:324-333`).

## Rulings (Fable, before Astra)

- **R5-1 Attempts.** Each `retro_load_game` and each RiscPC chooser confirmation is one explicit attempt; nothing else issues a token. Restart, swap rollback, fault and unload interrupt it and never relaunch (existing `launch_suppressed` and epochs). The player is told to close and reload the content to run it again (Q10), never "Restart".
- **R5-2 Option mapping.** Launch Off → no plan (desktop only); Launch App-N → APPLICATION with that ordinal, guarded by the D-031 binding (a changed ordinal-to-application mapping refuses with a notice, never a different program); Launch Auto → R5-3; Chooser Ask, or an ambiguous Auto with no remembered answer → a RiscPC chooser before any preparation status, using drive-0 discovery only; Chooser Remember → the remembered answer when its binding still holds. RAM disc Auto → the plan's RAMFS flag when the chosen application needs it by the same rule as Arculator; Disabled → never.
- **R5-3 Auto on RiscPC.** Auto chooses, in order: a drive-0 application selected by `arc_launch_recovery_decide` (exactly one verified application, complete scan); otherwise a single root `!Boot` file (BOOT); otherwise a product verified in the store (PRODUCT, R5-5); otherwise no plan with a manual-only notice. RiscPC Auto stays narrower than Arculator Auto (no disc-name matching) — a wrong automatic choice on RiscPC costs a full preparation.
- **R5-4 Keys and files.** RiscPC choice and binding files use the existing `<save>/arcdroid/launch/` directory keyed by the content digest (`arc_content_digest.sha1`) with a session-kind tag, so an Archimedes and a RiscPC answer for the same content never collide. Boot records stay in the RiscPC store (separate from `launch/`).
- **R5-5 PRODUCT.** A product is never assumed: it is offered in the chooser only when a boot record whose full binding matches the current content and store shows PRODUCT established, and it is executed only after `product_step` re-verifies it against the recovered store at START; if it is no longer there, the attempt ends with a notice and the desktop. Records never grant execution and never store the target string (launch contract).
- **R5-6 Records on load.** A matching record is used only for R5-5's chooser offer and for diagnostics; it never skips or replaces a launch the player asked for.
- **R5-7 Plan media digest and records under the default options.** Add a production plan-media digest derived from the already computed ordered member digests and lengths (no extra full-media read at load). Boot records must be publishable under the default Disk Writes (shadow): the media lease binds the original media identity and the store generation, not "Disk Writes Off"; identity refusal still discards records (amendment 23: store identity, not path spelling).
- **R5-8 Desktop handoff.** The host learns the attempt's outcome through a new read-only accessor. The status hold ends at DESKTOP, RETURNED or TERMINATED; at REJECTED, UNKNOWN or an interrupted attempt it ends with the matching manual-only notice; with no attempt (Off, one-drive CMOS, revision-1 store, delivery unavailable, cells disabled by a test override) the existing 25 s presentation-only bound remains the fallback. The hold never claims readiness (R3d-3 R-6).
- **R5-9 Notices.** N-12 is removed for sessions with a plan and kept (readiness-neutral, R3d-4 R-8) where no attempt exists. New manual-only notices (fresh IDs after R3d-6's N-22 to N-25): rejected plan, unanswered ambiguity, delivery unavailable or unknown, product no longer present, binding changed. Texts are frozen in the design note before code and follow §7a's wording rules.
- **R5-10 Testing through the frontend route.** AC-1's matrix runs through `retro_*` on the ordinary route with the test-only cell override, labelled as test evidence (R3d-4 R-2 stands: authored firmware never enables production cells); shipping cells and identity are unchanged.
- **R5-11 R3d-6 coordination (R6-10).** Whichever of R3d-5 and R3d-6 merges second reconciles the combined head: the Launch, Chooser and RAM disc help texts and meaning-matrix rows, the ignored-options log line, N-12's pin and the notice-ID allocation.

## Scope

R5-1 to R5-11; the design note section (`docs/riscpc-r3d5-design.md`) with the option mapping table, Auto order, file keys, record use, digest definition, hold rules and notice texts, written before code.

## Non-goals

Changes to part 2's state machines, transport, profiles or cells; Arculator launch behaviour; RiscPC memory or new pairs; automatic Arculator fallback (D-213).

## Acceptance criteria

- **AC-1** Exactly one token per explicit attempt; Restart, swap rollback, fault and unload at each of part 2's points never cause a second automated execution, through the ordinary frontend route; a reopen launches once.
- **AC-2** The chooser appears before any preparation status; a remembered choice skips it next time while its binding holds; Off and App-N hold; a changed binding refuses with a notice.
- **AC-3** No record from an unestablished attempt; records published under the default Disk Writes; discard on identity refusal; with cells disabled by the test override, behaviour equals R3d-4 plus a dispatch with an unknown outcome and no record.
- **AC-4** R5-5: PRODUCT offered only on a matching record, re-verified at START, refused with a notice when absent.
- **AC-5** R5-8's hold matrix (each outcome, each no-attempt case).
- **AC-6** Arculator launch byte-identical (harness and Fable's library A/B); gate-closed builds byte-identical.
- **AC-7** Builds, guards, the full local PR tier (D-212), new tests placed exactly once in the PR partitions.

## Evidence and legs

Sol: the tests above with mutants for token issuance, interrupt paths, chooser timing, record publication and the hold. Fable (private): the plan's legs re-planned for the retired Odin3 (D-217) — on the Android test device in joint sessions (D-218): seven pairs where firmware allows, returning commands, desktop applications, non-returning titles and prompts, Restart mid-attempt, the 084 install from an ordinary load and a later load offering the product; R-H cost within D-180 and part 2's ceilings against the phone's own A/A baseline; the same set on Windows RetroArch where the box allows; Mac library A/B in both gate states.

## QA focus

At-most-once through RetroArch's own Restart and close; chooser timing relative to preparation; no relaunch after interruption; the three epochs kept distinct; records never granting execution.
