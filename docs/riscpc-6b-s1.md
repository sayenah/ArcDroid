<!-- SPDX-License-Identifier: GPL-2.0-only -->

# 6b Phase 1a — slice 1a-3 (S1: bus-busy skip in batch service, with the commands() guard)

Governing: D-142, D-146, D-180, D-182, D-188, D-191, D-194 (as corrected by D-199(1)), D-195, D-198, D-199 (`e1deb41:docs/decision-log.md:212`; not present at `b57f772`). Documents: `docs/riscpc-6b-phase1a.md` (S1 row `:42`, B2 row `:48`, AC-7 tie case `:163`, AC-13 `:179`, QA focus `:183`, Astra stage 1 #4 `:225`); `docs/riscpc-6b-plan.md` (CG-1..CG-10 `:164-173`, B-3's dependence on S1 `:58`, `:357-362`); `docs/riscpc-6b-1a0.md` (frozen benefit rule AC-8 `:42-49`, D-180 baseline AC-9 `:50`, cross-check facility scope 5 `:26`); `docs/riscpc-6b-m2a.md` (R-M2-1 `:30-33`, A-3 `:347`, A-15 `:359`, A-25 `:374`, A-29 `:378`, A-31 `:380`); `docs/riscpc-translator-invariants.md` (§2.2 `:255-296`, G-1 `:326`, G-8 `:397-398`, G-11 `:411-414`, G-8 coverage row `:472`, Q-7 `:492`); `docs/riscpc-translator.md` (exclusion lifecycle `:417-423`, service classes `:435-440`, shortcut counters `:442-443`). Code citations are at `b57f772`. Code at `e1deb41` is identical; only `docs/decision-log.md` and `docs/handoff.md` differ.

**Branch:** `slice/6b-1a3-s1`, cut from `main` once this order is accepted. Under D-199(2), S1 may be developed in parallel with L1 (slice 1a-2), but it **merges only after L1 merges**. Before review it is rebased onto L1's merge commit. That commit is S1's immediate predecessor for every "predecessor" comparison below. S1 changes no shipping default, rate, quota, batch size or libretro surface.

## Rulings needed (proposed by Fable; each to be recorded before dispatch)

- **R-s1-1 Where the visible finish comes from (`finish_ns`).**
  - *Conflict to settle:* the draft puts host-only `finish_ns` "in `m`" (`phase1a.md:42`). The invariants (`invariants.md:290-293`) and the design note (`riscpc-translator.md:417`) say host-only state stays outside `m` and `arc_rpc_mem_fast` and is reset at every G-13 point.
  - *Proposed:* S1 adds **no stored memo**. The skip computes the ceiling of `m.finish` at its decision point with the existing exact `visible()`/`divide()` (`rpc_mem_timing.c:220-243`). This adds no field and no new writer obligation at `:765`, `:840` or `:957`. It needs no M2b inventory entry and no G-13 reset. It also cannot go stale in tests that write `m.finish` directly (`tests/rpc_endpoint_test.c:189`, `:205`).
  - *Fallback:* Sol may propose a memo only with Mac per-call evidence that the on-demand ceiling costs materially more. If so, it is a file-scope host-only static outside `m` and `arc_rpc_mem_fast`, with these properties:
    - written at every site that leaves `cycle_active || video_active` true (`:765`, `:840`);
    - reset in `arc_rpc_mem_bind` before the `!clock` early return (`:362-374`);
    - invariant: equals `visible(m.finish)` whenever `cycle_active || video_active`, and asserted in cross-check builds;
    - read only by the skip. The reference reads at `:816`, `:820` and `:877` stay unchanged, so the retained path stays the oracle (Astra stage 1 #3, `phase1a.md:224`). The M5 visible cache stays out (`phase1a.md:209`).
  - Either way, `docs/riscpc-6b-plan.md:58` and B-3 (`:357-362`) are amended to "S1's exact visible-finish test". B-3 inherits the ceiling semantics: valid only while a cycle or video is active.
  - The denominator is written only at bind (`:379`), so a ceiling taken at any time inside a binding is stable across `arc_rpc_mem_control` writes (`:393-405`).
- **R-s1-2 TEST_FAULTS measurement gate.** In builds where `ARCDROID_TEST_FAULTS` is defined, the skip is not taken while `measure_last && instructions >= measure_last`. This is the gate `ready()` and `next()` already apply (`:873-875`, `:888-890`); in that state the reference advances to the finish, not the due. The check compiles out of shipping and out of the measurement core, because `rpc_namespace.h:9-11` undefines TEST_FAULTS there. The design note also records why the gate cannot be reached in endpoint mode:
  - admission bounds the batch at `measure_last` (`:147-163`);
  - the loop breaks at `linecyc + 1 == count` before `batch_next` (`arm.c:1966-1967`);
  - so `batch_next` only runs with `instructions < measure_last`.
- **R-s1-3 M5 head-not-DEMAND tweak: out of S1.** It only removes a no-op loop and rollback in the demand shortcut (`:935-943`, `:971-972`). It changes when `shortcut_attempts`/`rollbacks` increment, which `riscpc-translator.md:442-443` defines and the AC-10 ranking used. B3 is dropped (D-199(2)) and its benefit is unmeasured. One cause per slice. It may return as its own item with Odin3 evidence.
- **R-s1-4 B2 resumable-loop skip: out.** Do not apply the skip after each `ready() == false` in `:975-986`. B2 is "only on evidence" (D-195(1); `phase1a.md:48`).
- **R-s1-5 commands() guard: in.** At both call sites in `arc_rpc_mem_run` (`:813`, `:850`), `commands()` is called only when `m.count || pending`.
  - It is exact: with `count == 0 && !pending`, the while loop (`:777`) and the completion branch (`:792`) are both inert.
  - A count-only guard is wrong. When `count == 0 && pending` (a demand finished while video was active), `:792` must still clear `pending` once video ends.
  - This edits the reference itself, so its oracle is the predecessor (CG-1 evidence and M2 lockstep where it applies) plus the directed `count == 0 && pending` case. The cross-check does not cover it.
- **R-s1-6 Flush placement (G-11).** The integer flush moves from the inline caller (`rpc_mem_timing.h:171-174`) to the start of `arc_rpc_mem_batch_service`:
  - It stays conditional only on `integer_batch`, as today.
  - It is unconditional with respect to every other test, comes before the 1a-0 classification block (`:908-920`, which must read the flushed due), and comes before the `now >= boundary` return (`:921`).
  - The inline fast continuation (`h:175-183`) is unchanged.
  - Under `ARCDROID_RPC_PROFILE`, flush cost moves from TIMING to BUS attribution, because the service wrapper enters BUS (`:995`). This is recorded in the design note; it is not a gate.
- **R-s1-7 Counters.** The 1a-0 eligibility counter `service_s1` (`:916`; enum `rpc_exercise.h:12-13`) keeps its meaning. S1 adds a host-only exercise counter `service_s1_taken`, excluded under §2.2, outside both structs and reset at G-13. Two assertions hold in TEST_FAULTS cores:
  - `taken == service_s1` whenever the R-s1-2 gate is inactive;
  - `taken > 0` per model in the calibration endpoint leg (`tests/rpc_timing_test.c:98-102`, `:151` today only prints).
  The skip also performs the same `++exercise_batch.services` the reference performs (`:977-979`), so `rational_*` does not drift (`:72-82`).
- **R-s1-8 Cross-check ownership.** The shared consumer wiring comes from L1 because L1 merges first:
  - a runner and ctest read of `arc_rpc_crosscheck_status`;
  - a failure latch that survives `bind`'s reset (`:363-365`);
  - cross-check builds of the calibration, unit and endpoint test executables.
  S1 adds only its own comparison site and directed tests. If L1's merged head lacks any of these pieces, S1 adds them and the PR says so. Today the facility is wired only into `arcdroid_rpcplan_libretro_rpc` and its self-test (`CMakeLists.txt:2609-2620`), with no consumer.
- **R-s1-9 Production invariants (AC-13 style).** Stated in code comments and in the invariants:
  - (a) `!pending ⇒ m.count == 0`. `pending` is set only in `append` (`:332`) and cleared only at `:793` and `:959`, both when `count == 0`.
  - (b) `cycle_active ⇒ active_valid`. `start_cycle` is reached only from `:835`, `:849` and `:855`.
  - (c) The denominator is fixed per binding.
  Synthetic tests that violate (b) are made realistic, or excluded from the differential with a stated reason. `service_counts` sets `cycle_active` without `active_valid` (`rpc_endpoint_test.c:188-190`, `:205`).
- **R-s1-10 Sequencing with M2b.** S1 is not held for M2b (D-199(2); D-198(1) requires M2b only before F1). If M2b merges before S1 does, S1 meets A-15/A-31:
  - M2 lockstep against its immediate predecessor;
  - "M2 self-consistency passes";
  - every new static classified in M2b's inventory (A-3/A-29). Under R-s1-1 the only expected new static is the excluded `service_s1_taken` counter in TEST_FAULTS builds.
  Whichever of S1 and M2b merges second rebases onto the other: shared `rpc_mem_timing.h` lines near `h:171-174`, and hooks in all three loops near `arm.c:1963-1978`.

## Objective

Remove the host cost of servicing the batch when the only bus work is an active cycle that ends after the next CPU issue. In that state the reference `run(now)` → `ready()` → `next()` → `run(due)` → `ready()` sequence (`:974-986`) has exactly three effects: it sets `now = due`, copies `issue_* = due_*` (`:893-896`), and counts one service. It performs no `complete`, retirement, video start or endpoint capture. S1 does the same thing in constant time, out of line. It also removes the empty `commands()` calls. Targets:
- ARM710 bus-bound code: 66 % of instructions see an S1-eligible entry on the authored ARM710 streaming workload (D-199(2)).
- 031: 14.5 % (D-199(2)).

S1 must leave guest behaviour, timing and every §2.2-compared field identical.

## Scope

1. **Bus-busy skip** inside `arc_rpc_mem_batch_service` (`:901-987`), after the flush (R-s1-6). It is taken exactly when all of the following hold:
   - `!pending`;
   - `!video_active`;
   - `cycle_active`;
   - `now < due`, with `due < visible(m.finish)` and `due < min(next_ns, limit_ns)`, all strict;
   - in TEST_FAULTS builds, the R-s1-2 gate is inactive.

   When taken, it sets the scheduler `now_ns` to `cpu_due_ns`, copies `issue_ticks/floor/fraction` from `cpu_due_ticks/due_floor/due_fraction`, performs the reference's service count and returns true. It writes nothing else:
   - not `bus_busy`, which is already true on the only production entry (`h:171-174`; the sole production caller is `h:173`);
   - nothing in `m`, `cursor`, `restart`, `rom_sequential` or measurement state.

   The comparison uses the visible-ns ceiling of `m.finish`, never `cpu_due_ticks` against `m.finish` in ticks. Placement relative to the demand shortcut is free because the predicates are disjoint: the shortcut needs `m.count > 0` (`:922`), and S1 implies `count == 0` by R-s1-9(a).
2. **Inline caller.** `arc_rpc_mem_batch_next` (`h:164-185`) delegates the `pending || bus_busy` case straight to service, with no flush in the header. This changes the code in all three `arm.c` instantiations (`arm.c:1986-2011`) without editing `arm.c`.
3. **commands() guard** (R-s1-5).
4. **Counters** (R-s1-7), including the per-model `> 0` assertion in `rpc_timing_test.c`.
5. **Test-only force-off switch** in TEST_FAULTS builds, so CG-1 can run with the skip forced off. It must refuse or be absent in the measurement core, like 1a-0's other test APIs.
6. **Cross-check comparison site** (`ARCDROID_RPC_CROSSCHECK` only).
   - Whenever the skip is about to be taken: snapshot the §2.2-compared state; run the retained reference on that state; record the result; restore; apply the skip; compare.
   - Compared: every §2.2 field, scheduler `now_ns`, measurement state (`invariants.md:269-275`) and the return value.
   - Excluded counters are restored, so the reference run is never counted twice.
   - Also assert R-s1-9(a)/(b) and, under the R-s1-1 fallback, the memo invariant.
   - Wiring per R-s1-8.
7. **Directed tests and mutants** (AC-3, AC-4), on the batch path through `arc_rpc_mem_batch_next`/`batch_service`, not only direct `run()`/`ready()`. Today only `batch_boundaries` (`tests/rpc_mem_timing_unit.c:340-383`) uses `batch_next`, and its tie is driver equality, not S1.
8. **Docs:**
   - invariants: §2.2 (R-s1-1, the new excluded counter), R-s1-9, and the G-8 coverage row `:472`, which still says no ROMCR/CLKCTL write while draining;
   - design note: the S1 exactness argument (below), the overflow argument, the endpoint argument (R-s1-2) and the PROFILE attribution change;
   - `phase1a.md` and `plan.md:58` amendments;
   - refresh of stale anchors:

     | Location | Cites | Current |
     |---|---|---|
     | `phase1a.md:248` | `:894`, `:768`, `:802` | `:901`, `:775`, `:809` |
     | `invariants.md:147` | `:894-980` and related lines | `:901-987`, `:974-986`, `:949-963`, `:972` |
     | `invariants.md:428-431` | — | `:147`, `:173`, `:187`, `:829-833` |
     | `m2a.md:54` | — | flush `:644-658` |
     | `m2a.md:309` | — | access `:498` |

   **Exactness argument (to be written in full):** given the S1 predicate, R-s1-9(a) and (b):
   - `run(now)`: `commands()` is inert because `count == 0`. The video branch is skipped. The cycle branch breaks because `now < due < visible(finish)` (`:820`), before the queued-video branch (`:837`), so a queued video does not start. `bus_busy` is recomputed to true (`:857`).
   - `ready()` is false because `due > now` (`:891-892`).
   - `next()` returns `due`. It is the minimum of `boundary`, `visible(finish)` and `due` (`:876-883`).
   - `run(due)` repeats `run(now)`.
   - `ready()` is now true.
   - The two skipped `ticks()` overflow checks (`:812`, via `:214-219`) cannot fire: `due < visible(finish)` implies `due·d < finish ≤ UINT64_MAX` (G-11, `invariants.md:414`).

## Non-goals

- B2's loop skip (R-s1-4), the M5 head-not-DEMAND tweak (R-s1-3) and B3 (dropped, D-199(2)).
- The M5 visible-finish cache in `run()`/`next()`.
- Any change to integer-batch representation (M4/B5), quota, rate or batch size; the rational continuation table.
- L1's lookup work; A1/F1.
- `.info` or libretro changes; edits to `arm.c` source.
- B-3's block-level window. B-3 consumes S1's semantics and is not part of S1.

## Architectural constraints and affected interfaces

- **Files:**
  - `engine/rpc/rpc_mem_timing.c` and `rpc_mem_timing.h`;
  - `engine/rpc/rpc_exercise.h` (new counter);
  - cross-check site under `ARCDROID_RPC_CROSSCHECK`;
  - tests: `tests/rpc_endpoint_test.c`, `tests/rpc_mem_timing_unit.c`, `tests/rpc_timing_test.c`, `tests/rpc_measure_mutants.py` (anchors must stay unique, `:111-112`), new S1 unit and mutant tests;
  - `CMakeLists.txt` only as R-s1-8 requires.

  No edit under `engine/rpcemu/` is expected. If one becomes necessary, it carries a dated "ArcDroid modification (YYYY-MM-DD, 6b-1a3)" notice and the UPSTREAM.md/manifest lineage must satisfy `check-rpcemu-import.py`.
- **Overlap with L1** (S1 rebases onto it): both edit `rpc_mem_timing.c` in `bind` (`:360-391`), the cross-check include and reset (`:6-8`, `:363-365`), the §2.2 exclusion text (`invariants.md:290-296`), `rpc_endpoint_test.c`, the `rpc_measure_mutants.py` anchors and the cross-check CMake block. L1's own regions are `lookup` (`:300-329`) and the tag writers. S1 must not touch them.
- **Namespace:** any new extern is `arc_rpc_`-prefixed, registered in `rpc_namespace.h` and `engine/backend-symbols.json`, and exported for Windows tests through `tests/rpc_test_exports.def` if a test calls it.
- **Test-only compile-out:**
  - the R-s1-2 gate, force-off switch, `service_s1_taken` and the snapshot differential exist only under `ARCDROID_TEST_FAULTS` or `ARCDROID_RPC_CROSSCHECK`;
  - shipping and measurement objects contain none of them (the measurement core undefines TEST_FAULTS, `rpc_namespace.h:9-11`);
  - the 1a-0 measurement-vs-shipping comparison (`tools/rpc_measure_compare.py`) must still pass with no new exception.
- **§2.2 classes:**
  - no new field in `m` or `arc_rpc_mem_fast`;
  - `service_s1_taken` (and any R-s1-1 fallback memo) is host-only, excluded, outside both structs and reset at every G-13 bind or unbind before the early return;
  - the raw snapshot and `excluded_counters` memcmp (`rpc_endpoint_test.c:214-227`) keep their meaning.
- **Time (G-1, G-8, G-11):**
  - strict `<` against both `next_ns` and `limit_ns`;
  - at `due == visible(finish)` the reference retires first, and a queued video then starts and pushes the CPU due (`:841-845`), so the skip must not be taken;
  - no `batch_flush` or `next_base` where the oracle has none;
  - overflow fatals fire at the same instruction.
- **Shipping identity:** shipping objects change only in `arc_rpc_mem_batch_service`, `arc_rpc_mem_run` (guard) and the inlined `batch_next` sites in the three `arm.c` loops. Any other changed function is explained in the PR.

## Acceptance criteria

- **AC-1 Calibrations and goldens (CG-1).**
  - What: `rpc-memory-timing-0..3` (all 4,677 intervals, intrusive and endpoint modes) and all 25 `tests/fixtures/rpc-*.trace` goldens are byte-identical with the skip forced on and forced off, and against the predecessor.
  - Where: host, ASan/UBSan, native MinGW and the Android build; executed on Mac arm64 and the Odin3 (Fable).
  - Goldens are never regenerated.
- **AC-2 Retained reference as oracle.**
  - The cross-check build reports zero mismatches over the full `ctest -R rpc` in the sanitizer CI job.
  - The S1 site is reached in that run, with its counts reported.
  - An injected skip defect fails the run and names the field.
  - The R-s1-9 assertions hold throughout.
  - Evidence: CI log and a self-test.
- **AC-3 Directed batch-path cases.** Each runs through `batch_next`/`batch_service` with the skip on and with it forced off, with a known answer, comparing all §2.2 fields, `now_ns`, the return value and the counters:
  - `due == next_ns`;
  - `due == limit` with a tied video event;
  - `due == visible(finish)` (AC-7 tie, `phase1a.md:163`);
  - distinct rational ticks sharing one visible ns, both with `cpu_due_ticks < finish` and `cpu_due_ns == visible(finish)`;
  - an integer batch entering S1, which proves the flush happens before the test and before the `:921` return;
  - `count == 0 && pending` with video active, then ending;
  - a queued video with due below the finish (skip taken, video not started) and at the finish (not taken);
  - a full write buffer;
  - a ROMCR and a CLKCTL write before the next word;
  - an overdue due (`due <= now`);
  - the R-s1-2 gate active;
  - all four models.

  This closes the G-8 coverage gap at `invariants.md:472`.
- **AC-4 Mutants committed and killed.** Each is killed by a named test.
  - Omitted flush; flush after the `:921` return; flush after classification.
  - Floor instead of ceiling for finish; ticks comparison (`cpu_due_ticks < m.finish`).
  - Boundary `<=` against `next_ns` and against `limit_ns`; `<` changed to `<=` against finish. This one survives all calibrations today (`phase1a.md:42`), so a directed case must kill it.
  - Drop `!pending`; drop `!video_active`; replace `cycle_active` with `cycle_active || video_active`; drop `now < due`.
  - Drop the service count (killed through `rational_*`).
  - Skip ignores the R-s1-2 gate.
  - Skip also writes `bus_busy` or `restart`.
  - Count-only `commands()` guard; guard at only one site.
  - Under the R-s1-1 fallback only: memo not updated at video start; memo not reset at bind.

  The existing 1a-0 mutants `s1-boundary-inclusive`, `s1-drop-now-lt-due`, `swap-service-classes`, `invalid-finish-read` and `rational-ignores-services` (`tests/rpc_measure_mutants.py:103-104`, `:114-122`) keep passing, with anchors ported.
- **AC-5 Exercise proof (CG-3).**
  - `service_s1_taken > 0` for every model in the calibration endpoint leg, asserted.
  - `taken == service_s1` wherever the gate is inactive, asserted.
  - A per-model table of taken entries over calibrations, goldens and the observation plan is in the PR; structural zeros are listed with reasons.
  - All 1a-0 counter tests pass unchanged.
- **AC-6 Parity paths (CG-5, Q-7).**
  - `rpc-hot-throughput` passes `|10·instr − ns| ≤ 9`.
  - `rpc-observation-plan` parity holds at quotas 1, 7 and 200 under off, unmatched, trace and active, with `integer_attempts > 0`. Quota 1 never calls `batch_next`, so it is the S1-free reference. Cross-quota comparison excludes `integer_batch` and local counts by rule (A-25(b)).
  - A hand-run comparison of the predecessor's and the candidate's projections (argv[6]) is recorded.
- **AC-7 Equivalence and state classes.**
  - `interpreter_equivalence`, `cheap_events`, `cheap_fault` and the raw inspector memcmp pass unchanged.
  - A test shows that changing `service_s1_taken` is invisible to the measurement-state comparison and the raw snapshot, and that the counter resets at every G-13 point.
- **AC-8 Benefit (frozen rule, `docs/riscpc-6b-1a0.md:42-49`; Fable's private legs).**
  - Deciding core: the 1a-0 measurement core, comparing the immediate predecessor (A) against the candidate (B) with that rule's workloads, windows, two six-run ABBA orders, statistics, clock-step exclusion and outcomes.
  - Target workloads: ARM710 streaming stores and 031. Gain on at least one is required, with no regression on any AC-8 workload. Windows: no-regression only, on hot and 031.
  - The A/A band must be recorded before these legs run (D-199(3)). The cumulative result against the post-P0i anchor is also reported (D-195(4)).
  - Inconclusive or no gain means S1 is not merged on benefit grounds.
- **AC-9 D-180 (CG-8; Fable's private legs).**
  - On the measurement core: observers genuinely off (`observer_cells=off`, logged `TEST OFF`) against production cells, at p50 and p95.
  - Gated workloads: hot (D-181) and 031 (S61 recipe, frames 6400–7400). Both stay ≤ 3 % on the integrated tree.
  - Re-run because the inline header changes in all three loops. The D-199(4) caveats are carried.
- **AC-10 M2 lockstep (CG-2).** Applies only if M2b has merged before S1 merges (R-s1-10):
  - zero divergences against the immediate predecessor on the directed ROM set and the fixed-seed corpus;
  - "M2 self-consistency passes";
  - every new static classified in the inventory.
- **AC-11 Shipping-shaped code (1a-0 AC-5).**
  - `tools/rpc_measure_compare.py` passes with no new exception.
  - The shipping objects' changed functions are exactly those listed under Constraints, on host, MinGW and Android.
  - The forced-slow request-level tests (`rpc-memory-service-boundaries`, `-hot-video-boundaries`, `-fetch-memo-eligibility`, `CMakeLists.txt:2222-2224`) pass.
- **AC-12 Cross-platform (CG-6).** Runner traces and guest instruction counts are identical across Linux CI, Mac, Windows and the Odin3 for hot and the AC-8 synthetics (Sol); the 031 leg is Fable's.
- **AC-13 Existing suites and guards (CG-4, CG-9).**
  - `rpc-qa-mutations`, `rpc-observation-mutants`, `rpc-observers-mutants`, `rpc-profile-mutants` and the 1a-0 measure mutants pass under Ninja, with anchors ported.
  - Import, include-closure, symbol, frontend-boundary and Windows test-export guards pass.
  - No ROM, title or trace bytes appear in commits or logs.
- **AC-14 Documentation.**
  - The R-s1-9 invariants are in code comments and in the invariants document.
  - §2.2 is updated (R-s1-1, the new counter).
  - The design note carries the exactness, overflow and endpoint arguments and the PROFILE attribution note.
  - The `phase1a.md` and `plan.md:58` amendments are made, and the stale anchors in Scope 8 are refreshed to the merged head.
- **Not applicable in 1a:** CG-7 (no host code cache or chaos policy). CG-2 applies only through AC-10.

## Evidence

**Sol:**
- commands and results for AC-1..AC-7 and AC-10..AC-14;
- the AC-3 case table and AC-4 kill list;
- the AC-5 per-model table;
- the AC-11 comparison output;
- the cross-check CI log;
- Mac A/B per-call cost, only if proposing the R-s1-1 fallback;
- the design-note text.

**Fable (private):**
- AC-1's Odin3 execution;
- AC-8 (Odin3 and Windows legs, A/A band first);
- AC-9;
- AC-12's 031 leg.

## Risks and QA focus

- **G-8 ties.** `due == visible(finish)`, rational ticks sharing one visible ns, a queued video that would start at the finish, and `due == next_ns` or `due == limit`. The non-strict `<=` mutant survives every calibration today, so the directed batch-path cases carry the proof.
- **Flush order.** The flush must come before classification and before the `:921` return, and run in every one of the three instantiations. Check for PROFILE attribution confusion in later reports.
- **The `commands()` guard.** It edits the reference itself, so the cross-check cannot see it. The `count == 0 && pending` case and the predecessor comparisons are its only oracle.
- **Synthetic test states.** States that violate `cycle_active ⇒ active_valid` (`rpc_endpoint_test.c:188-190`, `:205`) can mask or fake differential mismatches.
- **TEST_FAULTS gate.** It must stay out of shipping and measurement objects, and the skip must not fire while the gate is active in intrusive mode.
- **Counter drift.** Watch `services`, `rational_*` and `taken` versus eligible.
- **Merge order.** Rebase conflicts with L1 (`bind`, cross-check wiring, mutant anchors) and with M2b (`h:171-174`, inventory), and the "predecessor" being L1's merge commit, not the post-P0i anchor.
- **D-180.** Loop code size and the D-180 ratio after the inline header shrinks.

## Completeness critic (S67 design workflow): binding corrections

Every finding below is incorporated as a binding correction and overrides conflicting text above. Where a finding offers alternatives, Fable's choice in the next section governs.

**HIGH**

H1. R-s1-10 misstates the sequencing rule. D-198(1) (`e1deb41:docs/decision-log.md:211`) and R-M2-1 (`docs/riscpc-6b-m2a.md:32`) both say M2b is dispatched before the L1/S1 work orders take `arm.c` and `rpc_mem_timing`. Neither says "M2b only before F1". D-199(2) lets L1 and S1 run in parallel with each other; it says nothing about M2b. At `e1deb41`, M2a is still at Stage IMPLEMENTATION (`docs/handoff.md:49`), so M2b has not been dispatched. Correction: hold S1's acceptance until M2b is dispatched, or record a decision entry that amends D-198(1). Cite D-198(1) correctly in R-s1-10.

H2. AC-4 lists mutants that are equivalent and cannot be killed, so AC-4 cannot pass as written:
- "Replace `cycle_active` with `cycle_active || video_active`" is equivalent by logic, because the predicate already requires `!video_active`.
- "Guard at only one site" is the predecessor's behaviour. An unguarded `commands()` is a no-op when `count==0 && !pending` (`rpc_mem_timing.c:777`, `:792`).
- A count-only guard at `:813` alone looks equivalent. A video started at `:840` always retires before the `:850` call clears `pending`, so `count==0 && pending && !video_active` never reaches `:813`. At `:850` the count-only guard is killed by any demand completion, because `:826` leaves `count==0 && pending`. So the parenthetical in R-s1-5, "a demand finished while video was active", is also wrong.
- "Flush after classification" is equivalent. With `base_fraction==0` (`rpc_mem_timing.h:103`), `cpu_due_ns` before the flush (`h:135`) equals `cpu_due_ns` after it (`:656-658` → `next_base`). Both are `now + base_whole`.
- "Flush after the `:921` return" is equivalent on every `arm.c` path. A false return breaks the loop, and `arm.c:1978` flushes at the same `now_ns` and the same `instructions`.
- "Drop `!video_active`" is equivalent in production. A cycle never starts while video is active (`:837-849`).
- "Skip writes `bus_busy`" and "skip writes `restart`" are equivalent if the values written are `true` and `false`, because `cycle_active` implies `bus_busy` outside `run()` and `cycle_active` implies `!restart` (`:765`, `:817`).

Correction: give each mutant a named killing test and a mechanism (behaviour, a direct state assertion after `batch_next`/`batch_service` returns, or the per-call counter assertion). Label each one demonstrated or equivalent, in the A-30 style. Delete the logically equivalent ones. State the values for the `bus_busy` and `restart` mutants (`bus_busy=false`, `restart=true`). Make the guard mutants per site.

H3. `service_s1_taken` collides with the stable counter schema. `rpc_exercise.h:6-19` is the "stable schema-2 scalar order". `tests/rpc_measure_mutants.py:64` raises unless there are exactly 40 names, ending in `rational_cheap`. `ARC_RPC_COUNT_TOTAL` and the histogram offsets (`:21-24`) shift, and `arc_rpc_exercise_add` feeds `arc_rpc_profile_rank` under PROFILE (`:31-35`), which is JSON schema 2 (`tools/rpc_rank_report.py:16`). If the counter is kept outside `ArcRpcExercise`, `rpc_timing_test.c:99-101` cannot read it through `arc_rpc_test_interval_exercise`, so the per-model `> 0` assertion in AC-5 needs a new exported test API (namespace, `backend-symbols.json`, `tests/rpc_test_exports.def`). Correction: rule on one of two options before dispatch. Either the counter joins the schema, with a schema revision and updates to `rpc_measure_mutants.py:64`, the rank report and the `rpc_endpoint_test.c` loops over `ARC_RPC_COUNT_TOTAL`. Or it is a separate excluded static with its own interval capture and an exported accessor.

**MEDIUM**

M1. The flush move is invisible to every oracle the order relies on. The cross-check snapshots state after the flush, and calibrations and goldens see only chronology. Integer mode also heals itself: `issue_*` is rebuilt from `now_ns` at every later flush (`:655-658`), and `end_slow` flushes before it reads `issue_ticks` (`:630-632`). So an omitted flush probably changes only `integer_batch`, a §2.2-compared field, and the `integer_*`/`rational_*` counters. Correction: AC-3's integer-batch case asserts `integer_batch==false`, the exact `issue_*`, the exact `cpu_due_ticks` and the integer and rational counters straight after `batch_next` returns, on both the true return and the false (boundary) return. Add "the cross-check cannot see the flush move" under Risks, next to the guard.

M2. R-s1-9 leaves out invariants that the exactness argument and the "writes nothing else" clause depend on. These are `cycle_active ⇒ !video_active`, `cycle_active ⇒ !restart` and, outside `run()`, `cycle_active ⇒ bus_busy` (writers only at `:804` and `:857`). Correction: state and prove them in code comments and in the invariants document, and assert them in cross-check builds. `service_counts` (`rpc_endpoint_test.c:188-190`) violates `cycle_active ⇒ active_valid`. It must be fixed or excluded before any cross-check build of that test can pass.

M3. The two counter assertions are underspecified:
- "taken == service_s1 whenever the gate is inactive" cannot be checked on aggregate counters, and it ignores the force-off switch.
- If Sol computes the skip from the TEST_FAULTS classification `kind` (`:913-918`), the equality becomes circular, and TEST_FAULTS builds test a different predicate from shipping.

Correction: assert per call at the decision point that `eligible && !gate && !forced_off` implies the skip was taken. Require one predicate expression that compiles identically in shipping and test builds, computed independently of the `:916` classification, with mutant anchors kept unique (`rpc_measure_mutants.py:111-112`). This assertion is the only thing that kills the conservative floor-versus-ceiling mutant. It needs a directed case with `finish % d != 0` and `due == floor(finish/d)`.

M4. The AC-3 harness and the kill tests are not pinned down:
- `rpc_mem_timing_unit.c:29` arms intrusive mode, which disables integer batching (`h:106-108`). The integer→S1 case must disarm with `arc_rpc_mem_measure(0,0,false)`, as `batch_boundaries` does (`:360`), or run in endpoint mode.
- "Drop the service count" is killed only through `rational_*`, and the existing kill (`rational-ignores-services`, `rpc_measure_mutants.py:114-115`) is in `rpc_endpoint_cpu_test`, which is not shown to reach S1.

Correction: name the test for each case and show `taken > 0` in it.

M5. The cross-check cost and its interaction with PROFILE are not bounded:
- The site snapshots and restores all of `m` and `arc_rpc_mem_fast` at every skip it takes. That is a 128-slot ring of 64-byte-aligned packets plus two 512-tag caches (`rpc_mem_timing.c:20-56`), done at 14.5–66 % of instructions, under ASan.
- The CI job that enables the cross-check also enables PROFILE (`ci.yml:123`). The reference run enters the BUS profile stage through the `run()` wrapper (`:866`) and increments `exercise_batch.services` (`:978`).

Correction: compare only the fields the reference can write under the predicate, and hash the rest. Restore profile and `exercise_batch` state as well. Set a CI time budget and timeouts.

M6. The baselines fall short of the governing rules. AC-1 and AC-6 compare only against the predecessor. D-195(4) and A-31 (`m2a.md:380`) also require the evidence against the post-P0i anchor (goldens, calibrations, observation plans) to be kept. Phase 1a AC-3 (`phase1a.md:152`) requires a projection comparison against the pinned baseline. Correction: add the cumulative-anchor legs.

**LOW**

L1. AC-11 requires function-level identity of shipping objects on MinGW. D-199(4) accepts that MinGW has no emitted-loop proof, because of GCC inlining. Correction: carry that caveat into AC-11, not only AC-9.

L2. Phase 1a AC-10 (frame-gate budget line for hot and 031, `phase1a.md:171`) and AC-9's report fields (p99, guest MIPS, ns per instruction, both cores, `:168`) are missing. Correction: add them to AC-8 and AC-9.

L3. AC-3 overstates what it closes in the G-8 coverage row. The skip spans no cycle start, so a ROMCR or CLKCTL write cannot reprice anything inside it, and "in the instruction whose own fetch it reprices" (`invariants.md:472`) is not covered. Correction: add that case, or narrow the claim.

L4. The R-s1-2 reachability note covers only endpoint mode. Correction: also cite intrusive mode, where `test_stop` set at `:634` returns at `h:169`. State that the gate is reachable only by direct `batch_service` callers (or by `arc_rpc_mem_measure(0, last, …)`), so the gate case and its mutant must be direct-call unit tests.

L5. R-s1-10 says the only new static is `service_s1_taken`, but the force-off switch is a static too. Correction: classify both in the A-3/A-29 inventory, and make sure the measurement core refuses the switch explicitly, as 1a-0 AC-5(c) requires.

L6. Quota 1 is free of S1 but not of the `commands()` guard, because the driver path calls `run()`. Correction: do not cite Q-7 as the guard's oracle.

L7. One citation is off by one. `arc_rpc_mem_batch_next` is at `h:163-184`, not `164-185`.

L8. Optional: the 1a-0 prefix comparison (`arm.c:2017-2055`, an independent quota-one reference) gives S1 a check on real batched workloads at a boundary. Consider adding it to AC-6.

## Fable's choices on the critic's alternatives (S67)

- **H1:** S1 is dispatched after M2b is dispatched (D-198(1)) and may be developed in parallel with L1; it merges after M2b and after L1 (D-199(2)). R-s1-10 is restated with the correct D-198(1) citation.
- **H2:** every mutant has a named killing test and mechanism, labelled demonstrated or equivalent (A-30 style); logically equivalent mutants are deleted; the `bus_busy`/`restart` mutants write `bus_busy=false` and `restart=true`; guard mutants are per site.
- **H3:** `service_s1_taken` joins the stable counter schema with an explicit schema revision (on top of L1's revision and the runner-core handshake L1 introduces), updating `tests/rpc_measure_mutants.py`, the rank report and the `ARC_RPC_COUNT_TOTAL` loops; the force-off switch is a classified static that the measurement core refuses explicitly.
- **M1, M3:** the per-call assertion at the decision point (`eligible && !gate && !forced_off` implies the skip), with a single predicate expression identical in shipping and test builds and independent of the classification; the integer-batch state assertions straight after `batch_next` on both returns.
- **M5:** the cross-check compares only the fields the reference can write under the predicate, hashes the rest, restores profile and `exercise_batch` state, and has a stated CI time budget.
- **M6:** add the post-P0i anchor legs (goldens, calibrations, observation plans).
- **M2b obligation (from M2b's M13):** after moving the integer flush, S1 re-verifies M2b's projection (projection equals materialised state at quotas 1, 7 and 200, plus the stale-representation mutants) and updates the projection prose in the same PR.

## Astra challenge (S67, ACCEPT WITH CHANGES): Fable's adjudication

6b thread `01a13e62-8e2e-7f10-b35a-de57821ce0e7`, S67 (report private). Astra found no further exactness failure in the corrected predicate or `commands()` guard; findings that concern S1 are incorporated and override earlier text.

- **AS-S1-6 (Astra 6)** As in L1's AS-L1-6: S1's counter revision goes through the ABI handshake on every structure-copy API, the predecessor comparison uses two runner/core pairs, and S1's inventory additions change only the inventory metadata hash.
- **AS-S1-9 (Astra 9) Shipping-identity allow-list.** S1's base-against-candidate shipping comparison uses an exact allow-list of the objects S1 deliberately changes (the timing object and the ARM interpreter object that inlines the moved flush), rejecting any other changed object; candidate measurement-against-shipping equality and the layout checks stay unrelaxed; the MinGW emitted-loop caveat (D-199(4)) is carried explicitly.

## Fable's rulings on Sol run A (S67)

Sol run A implemented the service path, its tests and docs against `main` before L1 merged; shipping identity holds on all three toolchains. These rulings bind and override earlier text.

- **R-S1-A1 Counter and ABI integration follows L1.** The `service_s1_taken` counter joins the stable counter schema on top of L1's schema revision and ABI handshake (H3, AS-S1-6). That work, AC-5's taken counts and the counter-specific parts of AC-1, AC-7 and AC-14 are done in the run that rebases S1 onto L1's merge commit; no standalone revision or handshake is added before then. The M2b v1 emitter checks (projection, materialisation, stale-representation mutants, metadata-only hash) likewise follow whichever of M2b and L1 has merged by that run.
- **R-S1-A2 Cumulative identity across accepted intervening changes.** The cumulative comparison against the post-P0i anchor (`1f4bbae`) is judged on the anchor's own goldens and calibrations, except for fields that an accepted, merged decision changed in between; each such field is listed with its decision. The one listed now is the `rpc-d2-guest-0` fault silence (960 at the anchor, 800 after R3d-1's session-identity change `60457bc`, D-193). No fixture is regenerated or suppressed for S1. Byte identity of the current goldens against the immediate predecessor stays required and is repeated against L1's merge commit.
- **R-S1-A3 Pre-existing Mac sanitizer census failure.** `rpc-allocation-census` fails under Mac ASan on unmodified `main` (`tests/arc_alloc_census_test.c:40`, the zero-size `realloc` expectation). It is an R3c-3a follow-up, not an S1 defect; S1's sanitizer evidence is judged without it, and CI's Linux sanitizer job remains the gate.
