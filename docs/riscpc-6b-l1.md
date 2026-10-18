<!-- SPDX-License-Identifier: GPL-2.0-only -->

# 6b Phase 1a: slice 1a-2 (L1, cache lookup acceleration, exact)

Governing: D-142(2), D-146, D-180, D-182, D-188, D-191, D-194, D-195 (R-b, R-c, R-d, AC-8 benefit rule), D-198 (M2 scheduling and anchors), D-199 (L1 merges first, then S1; B3 dropped; AC-9 caveats; D-199 is at `e1deb41:docs/decision-log.md:212`, not at `b57f772`). Documents:
- `docs/riscpc-6b-phase1a.md`: L1 row `:41`, slice list `:119`, constraints `:141-145`, AC-13 `:179`, QA focus `:185`, rejection of the negative memo `:204`, Astra stage 1 adjudication `:221-229`.
- `docs/riscpc-6b-plan.md`: CG-1..CG-10 `:164-173`; B-1 and X reuse of the hint `:321`, `:473`.
- `docs/riscpc-6b-1a0.md`: AC-8 benefit rule `:42-49`, AC-9 `:50`, cross-check facility `:26`.
- `docs/riscpc-6b-m2a.md`: A-2, A-6, A-11, A-12, A-25..A-31, A-34 (`:347-384`).
- `docs/riscpc-translator-invariants.md`: §2.2 `:286-300`, E2 `:308`, G-4, G-6, G-7, G-13 (`:349-376`, `:443-446`), coverage gaps `:468-470`.

Code citations are `file:line` at `b57f772`. Code at `e1deb41` is identical; only `docs/decision-log.md` and `docs/handoff.md` differ. Astra stage 2 on this order is pending, and its adjudication will be appended here before Sol is authorised.

**Branch:** `slice/6b-1a2-l1`, from `main` at or after `e1deb41`. Rebase onto `main` before merge. If M2a or M2b merges first, rebase onto it and meet the inventory and lockstep obligations below (R-L1-8). S1 is developed in parallel under D-182 and rebases onto L1 after L1 merges (D-199(2)).

## Rulings needed (proposed; Fable records the accepted set in a D-entry before dispatch)

**R-L1-1. What L1 accelerates.**
- Only the set-associative tag search `lookup()` in `engine/rpc/rpc_mem_timing.c:300-329` is in scope.
- `checked_translation` (`cp15.c:390-459`) is a direct-mapped single-slot TLB and belongs to A1/F1.
- L1 edits no file under `engine/rpcemu/` (not `cp15.c`, `mem.c` or `arm.c`) and no inline helper in `rpc_mem_timing.h`.
- Proposed: accept as stated. Touching any of those files requires returning to Fable.

**R-L1-2. Admissible mechanism.**
- Admissible: a host-only way hint, plus an optional key mirror per geometry.
- Forbidden:
  - any reordering of tags;
  - any movement of a victim cursor (hits leave the cursor unchanged; `:544-550`, `tests/rpc-timing-expectations.md:14-46`);
  - any negative memo (`phase1a.md:204`);
  - any reuse of a lookup result across calls.
- A hint is accepted only after checking that the hinted way is in range, the tag is valid and the tag's line equals the looked-up line. Otherwise the search falls back to a full search.
- The mirror must never give a false negative. Any key match that could be a false positive is re-checked against the tag, and a failed re-check never ends the search.
- Invalid slots hold a sentinel that no line can equal. A line is `va >> shift` with shift ≥ 4, so it fits in 28 bits and an all-ones value is safe. A zero fill is wrong because line 0 is reachable.
- Proposed: accept.

**R-L1-3. Uniqueness invariant (new, load-bearing).**
- Rule: at most one valid tag per line per set.
- It holds today for two reasons. Allocation happens only after a miss in the same cache (`:538`, `:543`). A locked SWP forces `eligible=false` and `t=NULL` (`:528`).
- Given uniqueness, a validated hint or mirror hit returns the same `Tag*` as the linear scan.
- Proposed: state it in the invariants and in a code comment (AC-13 style), and assert it on every fill in the cross-check build.

**R-L1-4. Storage shape and interface.**
- Hint and mirror state is host-only, excluded under §2.2 (R-c, `invariants:290-294`), and lives outside `m` and `arc_rpc_mem_fast`. That keeps the raw snapshot memcmp (`rpc_observer_test.c:348-356` via `rpc_mem_timing.c:1015-1020`) unchanged.
- It is paired with each `Cache` (`m.instruction`, `m.data`), with internal linkage (no new extern).
- Every tag write goes through one tag-writer path that keeps the side state consistent.
- The tests that write tags directly must be ported to that path: `tests/rpc_endpoint_test.c:120` (test-local `Cache c={0}` at `:117`), `:133` and `:136`.
- Sol chooses how a `Cache*` maps to its side state, as long as test-local caches are supported.
- Proposed: accept. If B-1 or X later needs the hint across translation units, that slice adds the `arc_rpc_` extern, its `rpc_namespace.h` entry and its `backend-symbols.json` entry.

**R-L1-5. Hint keying and policy.**
- The hint is keyed by line within its set (the prototype keyed it by line, `phase1a.md:41`).
- It is updated on a hit and on a fill. It is never trusted, because exactness does not depend on the policy.
- Sol chooses the table size within a stated fixed bound and reports the hint acceptance rate per model.
- Proposed: accept.

**R-L1-6. Mirror gate per geometry.** Geometry comes from bind (`:382-386`):

| Model | Sets × ways |
|---|---|
| ARM610 | 4×64 |
| ARM710 | 128×4 |
| SA-110, I and D | 16×32 |
| ARM7500 | 64×4 |

- Proposed initial gate: hint plus mirror for ways ≥ 32 (SA-110 both caches; ARM610 with a 64-bit mask). Hint only for 4-way (ARM710, ARM7500).
- The gate is a constant chosen at bind, placed after the geometry lines so the anchors of `tests/rpc_qa_mutations.py:97-104` (G2, G8, G9, M39, M40) still match verbatim.
- A test-only compile-time override forces each cache to linear, hint-only, or hint plus mirror. Shipping and the default measurement core carry no override.
- Fable's Odin3 AC-8 legs on these variants set the final gate before merge. If hint-only regresses a 4-way workload beyond its band, that geometry gets no acceleration.

**R-L1-7. Counter semantics (Q1).**
- The 40 existing scalars and the two 64-bin histograms (`rpc_exercise.h:7-24`) keep exactly the meaning the linear reference gives them (`docs/riscpc-translator.md:425-434`):
  - empty: 0 probes;
  - miss: `ways` probes;
  - a hit in way k: k+1 probes, with the way taken from the returned tag's index.
  This keeps L1-era ranking reports comparable with D-199.
- New L1 counters (hint accepted, hint rejected then search, mirror search, linear fallback) are appended after `rational_cheap`, append-only. Profile JSON stays schema 2: its consumers key by name (`tools/rpc_profile_json.c:36-42`, `tools/rpc_rank_report.py:16-22`).
- The "Stable schema-2 scalar order" note (`rpc_exercise.h:6`) is amended to say the order is append-only.
- The name pin in `tests/rpc_measure_mutants.py:64` (40 names, last `rational_cheap`) is updated to the new count and last name. S1 then appends after L1's counters.
- Counters stay absent from the measurement core (`rpc_exercise.h:27`).

**R-L1-8. Sequencing with M2.**
- If M2a's inventory has merged, L1 classifies its new statics as host-only, excluded, giving a reason as A-34 does. If L1 merges first, M2a or M2b inherits that classification (A-3, A-29).
- If M2b has merged before L1 merges, AC-10 requires M2 lockstep against L1's immediate predecessor (A-31, D-198(3)).
- Evidence against the D-195 post-P0i anchor is always required.
- Proposed: accept.

**R-L1-9. Oracle (Astra-1 #3).**
- In the cross-check build (`ARCDROID_RPC_CROSSCHECK`, `CMakeLists.txt:10`, `:2609-2614`), every lookup also runs a separately retained linear scan: the pre-L1 search body, kept verbatim and compiled only in test builds.
- The comparison covers the returned result normalised to (cache identity, slot index) or NULL, through `arc_rpc_crosscheck_field` (`rpc_crosscheck.h:15`).
- The reference reads only canonical tags. It never updates the hint, the mirror or any counter, and counting happens once.
- The randomized test (AC-4) uses an independently written model, so no shared helper is its own sole oracle.
- Proposed: accept.

## Objective
Cut the host cost of the cache tag search with a host-only, validated hint and an optional key mirror. Results must stay identical to the linear search, and every piece of canonical state must stay identical: tags, `populated`, victim cursors, dirty halves, `stats.hits`, `fetch_hits`, fills, the memo and packets.

The D-199 profile on 031 (SA-110, 32-way) shows:
- about 0.549 lookups per instruction;
- 16.6 probes per hit;
- 97 % of hits beyond way 0;
- about 9.7 probe iterations per instruction.

From these figures, about 90 % of probe work is on hits (the hint's target) and about 10 % on searched misses (the mirror's target). This is an estimate; Fable checks it against the AC-10 report before fixing R-L1-6.

## Scope
1. **Hint**, per R-L1-2 and R-L1-5.
   - Used by both callers of `lookup()`: access (`:525`) and SA c7 maintenance (`:611`).
   - The early return when `!populated` stays first and unchanged (`:308-313`).
2. **Key mirror**, gated per R-L1-6.
   - The mirror is updated at every tag writer:
     - bind `memset` (`:362`): all slots set to the sentinel;
     - fill (`:549`, inside `:543-553`): set the key;
     - full invalidate (`:594-599`, which keeps `va`): all slots set to the sentinel;
     - single invalidation (`:614`): that slot set to the sentinel.
   - `castout` (`:351-359`) changes only `dirty`, so it needs no key change. No other tag writer exists in `engine/rpc` or `engine/rpcemu/src`.
3. **Reset at bind.**
   - All host-only side state is reset at every G-13 point: stop `rpc_session.c:167`; reset and failed start `:187` via `:278`/`:336`; test-rate rebind `:639`.
   - The reset is unconditional (not inside `#if TEST_FAULTS`, `:366-373`), runs at or after the `memset` (`:362`), and comes before `if(!clock) return;` (`:374`).
   - The layout and gate that depend on geometry are set after `:384-386`.
   - L1 owns this reset block; S1 adds its own state to it after rebasing.
4. **Counters** per R-L1-7, with existing values reference-equivalent.
5. **Cross-check** per R-L1-9, plus the R-L1-3 uniqueness assertion on every fill.
6. **Test-only gate override** per R-L1-6. It is compiled out of shipping and out of the default measurement core.
7. **Tests:**
   - the directed matrix (AC-3);
   - the randomized test (AC-4);
   - committed mutants (AC-5);
   - the ported direct-write tests (R-L1-4);
   - the G-6 case with data and code in one set mid-sequence, closing the gap at `invariants:470`.
8. **Docs:**
   - invariants §2.2: the hint and mirror leave "reserved" and become live (`:290-294`);
   - E2 citation `:277-322` → `:300-329` (`:308`);
   - the §2.2 citation for `va` of invalid tags `:589` → `:596`;
   - G-5 memo clear on allocation `:541` → `:548`;
   - the uniqueness invariant and "tag writers update the mirror" (AC-13);
   - the hint validation contract stated as stable for B-1 and X (`plan:321`, `:473`);
   - L1 counters in `docs/riscpc-translator.md`;
   - refreshed anchors in `phase1a.md:247-250` (all off by 7: lookup `:300`, access `:525`, maintenance `:611`);
   - plan B-1 `:375` → `:548`;
   - the M2a outline's `arc_rpc_mem_access` `:325` → `:498` (`m2a.md:309`).

## Non-goals
- Any change to translation, the shared TLB, permission or `cp15.c`/`mem.c`/`arm.c` (D-142(2), D-146, R-b; Q-10 forbids speculative walks or probes).
- A1/F1, S1, B2/B4, D1/D2.
- Changing replacement policy, way count, cursor semantics or the memo (`:548`).
- A negative memo.
- Quota, rate or batch-size changes.
- `.info` or libretro surface changes.
- Back-porting any emitter (A-31).

## Architectural constraints and affected interfaces
- **Files:**
  - `engine/rpc/rpc_mem_timing.c`: `lookup` `:284-329`, bind `:362-386`, allocation `:543-553`, `invalidate` `:594-599`, maintenance `:611-615`;
  - `engine/rpc/rpc_exercise.h`;
  - tests: `tests/rpc_endpoint_test.c`, a new randomized test, mutant scripts;
  - `CMakeLists.txt` registrations;
  - docs.
  - Overlap with S1 (bind reset block, `rpc_exercise.h`, `rpc_measure_mutants.py`, the §2.2 text) and with M2b (`arc_rpc_mem_access` `:498`, the field serialiser) is expected; L1 lands first.
- **§2.2 classes:**
  - hint and mirror state, L1 exercise counters and the cross-check bookkeeping are host-only, excluded, outside `m` and `arc_rpc_mem_fast`, and reset at every G-13 point;
  - the gate override is build configuration, not state;
  - `lookup()` stays pure in canonical state (E2), so guard-time probes remain admissible.
- **G-6:** unified data loads still use "the real lookup" (`invariants:371`). A validated hint with an identical result qualifies.
- **G-7** needs no ruling, because the hint never filters.
- **G-4** is unaffected (no `cp15.c` edit).
- **Shipping identity:**
  - L1 is production code: the hint and gated mirror are in shipping and in the measurement core.
  - Counters, the cross-check, the reference scan and the override are test-only and compile out.
  - Every shipping object other than `rpc_mem_timing.o` keeps byte-identical code sections against the base (host, MinGW, Android).
  - The measurement-versus-shipping comparison (`tools/rpc_measure_compare.py:154`) still passes for `rpc_mem_timing.c`.
- **Namespace:** no new externs expected. If any appear, they need the `arc_rpc_` prefix, `rpc_namespace.h`, `backend-symbols.json`, and a passing symbol guard.
- **Provenance:**
  - `rpc_mem_timing.c` is ArcDroid-original GPL-2.0-only (header `:1-3`), so it needs no RPCEmu modification notice.
  - No `engine/rpcemu/` file is edited, so the UPSTREAM.md and manifest lineage are unchanged and `check-rpcemu-import.py` passes unchanged. Any `engine/rpcemu/` edit returns to Fable and then carries a dated ArcDroid modification notice.
  - No ROM, title or trace bytes are committed or logged.
- **Code size (D-180, H7):** `lookup` is reached from the data accessors (`mem.c:704-739`) and from `fetch_slow` (`rpc_mem_timing.c:475-482`) through `arc_rpc_mem_access`. Correctness never depends on inlining; gains are reported per toolchain (`phase1a.md:145`).

## Acceptance criteria
- **AC-1 Corpus exactness (CG-1, CG-5).**
  - Run `rpc-memory-timing-0..3` (4,677 intervals) in intrusive and endpoint modes, and all 25 `tests/fixtures/rpc-*.trace` goldens.
  - Each run must be byte-identical with L1 forced off, forced hint-only, and forced hint plus mirror (on every geometry the override admits), and with the default gate.
  - Platforms: host, ASan/UBSan, native MinGW and Android builds.
  - `rpc-observation-plan` must pass at quotas 1/7/200 × off/unmatched/trace/active, with `integer_attempts > 0`.
  - The raw snapshot memcmp is unchanged.
  - Goldens are never regenerated.
  - Kills: the existing `rpc-qa-mutations` geometry mutants (anchors intact).
- **AC-2 Retained-reference cross-check (R-L1-9).**
  - The cross-check build gives zero mismatches on AC-1's corpus.
  - The sanitizer CI job runs with the cross-check on.
  - An injected wrong-slot result fails the run and names the field.
  - Fable private leg: 031 under the S61 recipe (8,000 frames) on the cross-check plan core, zero mismatches.
- **AC-3 Directed lookup matrix.** Expectations are stated without running the core. Per geometry (four unified, plus the SA data cache), the matrix covers:
  - empty cache;
  - searched miss in a full set;
  - hits at way 0, a middle way and way `ways−1`;
  - a stale hint after the hinted way was refilled;
  - a hint to a slot that was invalidated by full invalidate and by c7 single invalidation (crm 6), plus a clean (crm 10) with no key change;
  - the same `va` in an invalid slot and a valid slot of one set;
  - line 0 (VA 0) after bind;
  - a model change on rebind and every G-13 point with stale hint or mirror state;
  - a locked SWP;
  - the G-6 case: a unified data fill with code in one set mid-sequence, evicting the executing line.

  Each case asserts the returned slot, `populated`, the cursors, dirty halves, `stats.hits`, `fetch_hits`, fills and the memo.
- **AC-4 Randomized equivalence.**
  - A seeded test over all four geometries plus SA D, with committed seeds and a stated operation count per geometry.
  - Operations are mixed: lookups, fills through the real allocation path, full and single invalidations, and rebinds.
  - Results are checked against an independently written linear model with a cyclic cursor (sharing no code with the candidate), comparing the returned slot and the full canonical cache state after every operation.
- **AC-5 Mutants (CG-4), committed and killed:**
  - drop the `va` compare;
  - drop the `valid` check;
  - drop the way bound;
  - skip the mirror update at fill, at full invalidate and at single invalidate (three mutants);
  - zero fill instead of the sentinel;
  - host-state reset inside TEST_FAULTS only;
  - reset after the `!clock` return;
  - gate set before the geometry lines;
  - MRU (the hint moves the cursor or reorders tags);
  - the first key match ends the search without a re-check;
  - the ARM610 mask truncated to 32 bits;
  - the reference scan updates the hint;
  - the way counter derived from the hint instead of the returned slot.

  The existing `rpc-qa-mutations`, `rpc-measure-mutants` (ordinary and profile, with the pin updated and `:117` intact), `rpc-profile-mutants` (fill anchor `tests/rpc_profile_mutants.py:71` intact) and observation mutant suites pass, with anchors ported only where lines moved.
- **AC-6 Counters (CG-3).**
  - Existing lookup counters keep the same values on `lookup_counts` and `maintenance_callsite` after porting (`rpc_endpoint_test.c:114-139`), and on a per-model calibration comparison against the base.
  - Each new counter is checked on a directed case with a known answer.
  - A per-model table shows each new counter nonzero wherever it is reachable, with structural zeros listed and the reason for each.
  - A build that always falls back fails the hint-accepted floor.
  - Kills: a dropped increment for each new counter.
- **AC-7 Shipping identity.**
  - Shipping objects other than `rpc_mem_timing.o` have byte-identical code sections on host, MinGW and Android.
  - `tools/rpc_measure_compare.py` passes.
  - Test-only parts are absent from shipping and the default measurement core (symbol check).
  - Fable: measurement-core stdout equals plan-core stdout on hot and 031 (as in 1a-0 AC-5(c)).
- **AC-8 Benefit (frozen rule, `docs/riscpc-6b-1a0.md:42-49`; Fable's private legs).**
  - Deciding core: the measurement core (Odin3 Android Release; Windows native MinGW).
  - Workloads: hot, the four AC-8 synthetics, and 031 (decision window frames 6400–7400).
  - Sequence and statistics: two six-run ABBA orders, run serially (D-182), using the A/A band recorded under D-199(3). That band must be recorded before this AC can be judged.
  - Merge requires **gain** on 031 and **no regression** on any workload, judged per model where a workload is model-specific. Synthetic workloads that hit way 0 and the ARM710 4-way workloads are checked against the R-L1-6 gate.
  - Windows: no regression on hot and 031.
  - An inconclusive or no-gain outcome does not merge.
  - CG-10: with the override set to forced-off, there is no regression against the base.
- **AC-9 D-180 (CG-8, D-194, D-199(1)).**
  - On the measurement core, observers genuinely off (`observer_cells=off`) against production cells.
  - Hot and 031 each ≤ 3 % at p50 and p95, cumulative on the integrated tree.
  - The D-199(4) caveats are restated (no emitted-loop proof on MinGW; observer-event code in the measurement core differs per event).
  - Fable's private legs.
- **AC-10 M2 (A-31, R-L1-8).**
  - If M2b has merged: M2 lockstep shows zero divergences against L1's immediate predecessor on the directed ROM set and the fixed-seed fuzzer corpus, with forced exits (CG-2).
  - If M2a's inventory is on `main`: L1's statics are classified and the inventory passes.
  - Otherwise: recorded as not applicable, with the merge order cited.
- **AC-11 Cross-platform equality (CG-6).** Runner traces and guest instruction counts are identical across Linux CI, Mac, Windows and the Odin3 for hot and the AC-8 synthetics, and privately for 031.
- **AC-12 Invariants documented (AC-13).**
  - Code comments and invariants text for:
    - uniqueness (R-L1-3);
    - "tag writers update the mirror";
    - the hint validation contract (stable for B-1 and X);
    - the host-only reset placement.
  - The Scope 8 citation refresh is complete.
- **AC-13 Guards and provenance (CG-9).**
  - Import, include-closure, symbol, frontend-boundary, export and Windows test-export guards pass.
  - `check-rpcemu-import.py` passes with no `engine/rpcemu/` change.
  - No ROM, title or trace bytes appear in commits or logs.
- **AC-14 Existing suites.** These pass unchanged:
  - `rpc-endpoint-directed` and `rpc-counter-profile-directed` (`CMakeLists.txt:2206-2215`);
  - `rpc-memory-service-boundaries`, `rpc-fetch-memo-eligibility`, `rpc-memory-hot-video-boundaries`, `rpc-memory-long-*`;
  - `rpc-cpu-mmu-memory`, `rpc-fetch-memo-rewalk`;
  - `rpc-endpoint-cpu`;
  - `rpc-crosscheck-selftest` (`:2619`).

## Evidence
**Sol:**
- commands and results for AC-1 to AC-7 and AC-12 to AC-14, with AC-10 where applicable;
- the AC-3 and AC-4 expectation tables and seeds;
- the mutant kill list;
- the AC-6 per-model counter table;
- the AC-7 object comparison output;
- per-model hint acceptance and mirror use on the calibrations;
- the gate-override build commands for Fable.

**Fable (private):**
- AC-2's 031 cross-check leg;
- AC-7's stdout identity on the Odin3;
- AC-8 on the Odin3 and Windows, including the R-L1-6 gate variants;
- AC-9;
- AC-11 on 031.

## Risks and QA focus
- **Mirror consistency** at every tag writer (`:362`, `:549`, `:596`, `:614`) and at the ported test writes. A zero or raw-`va` mirror gives a false miss, which leads to a duplicate allocation and divergence.
- **Uniqueness**, and any path that could break it.
- **Bind placement:** reset before the `!clock` return and outside TEST_FAULTS; gate after the geometry lines; geometry change on rebind.
- **Counter leakage:** counters taken from the hint instead of the returned slot, or a cross-check reference that mutates host state.
- **Canonical state:** hits must leave cursors, `populated`, dirty halves and the hit split (`stats.hits`, `fetch_hits`) untouched.
- **Overhead on way-0 workloads** (the prototype measured +0.3 % to +0.9 %) and on 4-way ARM710 and ARM7500 (H8). Gate per geometry.
- **Code growth** in `arc_rpc_mem_access` reaching the fetch slow path (D-180, per toolchain).
- **Prototype evidence:** the `7b11b47` figures (031 −14.9 % cycles, Mac) are scratch, not gate evidence. Only AC-8 decides.
- **Merge order:** textual conflicts with S1 (bind reset block, the counter pin) and with M2b (`:498`, the inventory). L1 lands first per D-199.

## Completeness critic (S67 design workflow): binding corrections

Every finding below is incorporated as a binding correction and overrides conflicting text above. Where a finding offers alternatives, Fable's choice in the next section governs.

**HIGH**

1. **AC-7 cannot pass with today's tool.** `tools/rpc_measure_compare.py:144-145` (AC-5(a)) requires every object in `arcdroid_libretro_rpc` to have byte-identical code between base and candidate, and that includes `rpc_mem_timing.o`. L1 changes that object on purpose, so "`tools/rpc_measure_compare.py` passes" contradicts "every shipping object other than `rpc_mem_timing.o`".
   - Correction: Sol adds an explicit allow-list option to the tool. It names exactly one object basename and fails if any other object differs. A committed self-test shows that a second changed object still fails.
   - AC-5(b), shipping against measurement for `rpc_mem_timing.c` (`:154-159`), and its layout check (`:160-167`) stay mandatory.
   - Add a base-against-candidate check that the layout values of `m`, `Cache`, `Tag` and `ArcRpcMemFast` are identical. This proves no member was added. Today the layout check compares only shipping with measurement inside the candidate.

2. **Most of the AC-5 mutant list is behaviourally equivalent under R-L1-2's own rules, so CG-4 kills (plan:167) are impossible.** A hint is accepted only after `valid && va==line`, and a failed mirror re-check never ends the search. Under those rules:
   - **Zero fill**, and **skipped mirror update at full invalidate** (`:596`) or **at single invalidate** (`:614`), only cause extra re-checks.
   - **First key match ends without re-check**: with an exact, sentinel-maintained mirror, a key match already implies `valid && va==line`.
   - **Drop the way bound**: the bind reset keeps `hint<ways`. The only symptom of an out-of-range hint is an out-of-bounds read that ASan sees.
   - **Reset after `if(!clock) return`** (`:374`): `lookup` is unreachable while unbound, because of the `recording` gates at `:477` and `:501`, and the next bind with a clock resets anyway.
   - **Reset inside TEST_FAULTS only**: every test build defines TEST_FAULTS, and the measurement core undefines it (`rpc_namespace.h:9-11`). No test build can observe this mutant.

   Correction:
   - Decide which layer carries exactness. Either (a) sentinel plus first-match, which makes the zero-fill, invalidate-skip and no-re-check mutants killable through the line-0 and same-`va` invalid-slot cases; or (b) iterate every match, and declare the sentinel a performance measure only.
   - Add white-box kill means: a test-only accessor for hint and mirror state, and counters for "hint rejected" and "mirror re-check failed", each with exact directed expectations.
   - Kill the TEST_FAULTS-placement mutant structurally, for example by preprocessing `rpc_mem_timing.c` with the measurement-core flags and asserting the reset is present.
   - List any mutant that remains equivalent, with its proof, instead of claiming a kill.

3. **No test makes the validation fallback load-bearing across the corpus.** Stale or wrong hints may almost never occur in the calibrations and goldens, so AC-1 and AC-2 can pass even if validation is incomplete.
   - Correction: add a test-only poison mode in the spirit of CG-7 (plan:170). At seeded points it overwrites hints with random ways, both in range and out of range, and fills the mirror with stale keys.
   - Run AC-1's corpus and AC-4 under it. Results must be byte-identical, and the fallback and re-check counters must be above zero.

4. **Fable's AC-8 variant legs cannot be built as specified.** The measurement core's definitions are fixed (`CMakeLists.txt:2580-2581`), and `rpc_namespace.h:9-11` undefines `ARCDROID_TEST_FAULTS` in it. An override gated like the other test hooks therefore cannot exist in a measurement core, so neither the R-L1-6 gate variants nor CG-10's forced-off leg can be run.
   - Correction: use a separate macro and CMake option that `rpc_namespace.h` does not strip, and build separately named variant measurement targets.
   - AC-7's identity applies only to the default target, and the evidence names each variant core.

**MEDIUM**

5. **L1 raises the D-180 ratio without touching observer code.** It shrinks the unobserved denominator on 031, while the observer overhead stays the same in absolute terms. D-199(1) has 031 at +2.20 % p50 in order 2 (`e1deb41:docs/decision-log.md:212`). At the prototype's −14.9 % that becomes about +2.6 %, which leaves little margin for S1 under CG-8's cumulative 3 %. The risk section mentions only code growth.
   - Correction: add this mechanism to the risks. AC-9 reports the before and after ratio on 031, and states the margin left for S1.

6. **The gate is chosen from the same data that judges benefit, which breaks the frozen rule** (Astra-1 #7 at `phase1a.md:228`, CG-10). Separately, ARM610 and ARM7500 have no frozen workload (`1a0.md:44`), so their gates would have no evidence.
   - Correction: freeze the selection rule before measuring, and confirm the chosen configuration on a fresh ABBA run.
   - For ARM610 and ARM7500, either fix the gate in advance from calibration counters, or freeze an added workload before any measurement.

7. **Risk of regressions on way-0 workloads against the 0.5 % band floor** (`1a0.md:47`). The prototype measured +0.3 % to +0.9 % on code that hits way 0. "No regression on any workload" fails if any frozen synthetic is dominated by way-0 hits, and the SA gate (hint plus mirror) gives no escape.
   - Correction: record each frozen workload's hit-way histogram from the plan-core ranking (the histograms at `rpc_exercise.h:22-23`) in the work order before dispatch. Predict the risk for each workload from those histograms.

8. **The "append-only" counter claim is false at the binary level.** `ARC_RPC_COUNT_ACCESS_WAYS` equals `ARC_RPC_COUNT_SCALARS` (`rpc_exercise.h:21-24`), so appending scalars moves both histograms and changes `sizeof(ArcRpcExercise)`. The runner fills an `ArcRpcExercise` from the core across the library boundary (`tools/rpc_runner.c:539-556`). A runner and core from different revisions, as in base-against-candidate runs, silently misread or overflow.
   - Correction: add a schema or size handshake, or bump the schema explicitly. State that runner and core must come from the same revision. Profile JSON compatibility holds by name only.
   - Also update the `ranking-<name>` loop in `tests/rpc_measure_mutants.py:66-77`: it routes every new counter to `rpc_endpoint_test` and `rpc_counter_profile_test`, so each new counter needs a directed expectation in both builds.

9. **R-L1-9 contradicts itself.** "The pre-L1 search body, kept verbatim" includes the `lookup_count` calls (`:309-311`, `:319-321`, `:325-327`). That double-counts, and conflicts with "never updates … any counter".
   - Correction: the reference is the pre-L1 scan without counting. Add a mutant "reference counts".

10. **The hint validation contract is incomplete while Sol chooses the storage mapping.** On SA-110 the same line is often resident in both `m.instruction` and `m.data` (`cache_for`, `:280-283`). A hint stored as a `Tag*` or an absolute slot, or side state mapped to the wrong cache, passes `valid && va==line` on the other cache's tag. A store hit would then set the dirty half on an instruction tag (`:536`).
    - Correction: the contract fixes the hint as a way index within the looked-up cache's set for that line, and the mirror as one per `Cache`.
    - Add a directed case with the same line resident in SA I and D at different ways. Add a mutant "data lookup uses the instruction side state".

11. **Supporting the test-local cache pushes a foreign-cache branch into shipping.** `tests/rpc_endpoint_test.c:117` uses sets=1, ways=4, which is not a production geometry. In addition, `:133` and `:136` place a tag at way 3 with the cursor at 0, and AC-6's "same values" (probes 4 and 36, way bin 3) depends on that placement.
    - Correction: allow `lookup_counts` to be rewritten on a bound cache with restated expectations, or allow a side-state registration that exists only under TEST_FAULTS.
    - Specify a test helper that writes an arbitrary slot, does not move the cursor, and maintains the mirror.

12. **The citation refresh targets b57f772 numbers that L1's own edits will shift.** L1 inserts code in `lookup` (`:300ff`) and bind (`:362ff`), ahead of most cited lines. Scope 8 also covers only a subset of the stale citations: the invariants also cite `:328-333` (at `:285`), `:468-489`/`:690-700`, `:596-610`, `:389`/`:413`, `:465`, `:515`, `:496-503`, `:529`, `:531-553` and `:344-352`/`:539`.
    - Correction: refresh every `rpc_mem_timing.c` citation at L1's final head, checked by grep, and note that S1 will shift them again.

13. **AC-4 is impractical as written.** Fills through the real path append DEMAND packets (`:551`), and the core fatals at 128 queued requests (`:333`). The existing oracle `CyclicTags` (`tests/rpc_timing_oracle.py:12-23`) does not model dirty state, SA castout, write-allocate dirty halves (`:558`) or store-hit dirty halves (`:536`).
    - Correction: specify how requests are drained between operations, and state the exact compared fields.
    - The independent model covers SA-110 dirty and castout behaviour. It also asserts uniqueness, because the cross-check build alone does not run AC-4.

**LOW**

14. **Mirror search with `ctz`.** Calling `ctz` on a zero mask is undefined behaviour, and correctness must not depend on the compiler (`phase1a.md:145`). No `__builtin_ctz` is used in the engine today. Correction: require an explicit zero test and a directed full-miss case under UBSan.

15. **The gate and mirror layout must come from the bound `sets` and `ways`, not from `model`.** Otherwise the geometry mutants in `tests/rpc_qa_mutations.py:97-104` (M39: ARM7500 at 64 ways, M40: ARM710 at 8×64, G8: SA D at 32×16) become out-of-bounds accesses instead of behavioural kills.

16. **AC-3 and AC-4 miscount the caches.** There are three unified models (ARM610, ARM710, ARM7500) plus SA I and SA D, not "four unified plus SA D". The G-6 case applies to the unified models only.

17. **R-L1-2's wording contradicts the hint itself.** "No reuse of a lookup result across calls" describes what a hint does. Reword it to "no unvalidated reuse and no negative reuse".

18. **CG-1 (plan:164) also requires forced-on and forced-off execution on Mac arm64 and the Odin3.** AC-1 lists only build platforms. Add the execution legs.

19. **`docs/riscpc-translator.md:427-431` ("probes count examined ways") becomes false after L1.** Redefine it as the probes the linear reference would examine.

20. **The benefit estimate needs correcting and sourcing.**
    - With no empty returns and every lookup at 32 ways, hits take about 87 % of probe work and misses about 13 %, not 90/10.
    - "0.549 lookups per instruction" is not in D-199's text (`e1deb41:docs/decision-log.md:212`). Cite its source or drop it.

21. **Full invalidate gets more expensive.** A full invalidate now also writes 512 sentinels per cache, on top of the loop at `:596`. Report this cost for c7-heavy code.

22. **Restore paths must rebuild the side state.** `retro_unserialize` is a stub today (`libretro/arcdroid_libretro.c:8532-8534`). Extend the invariant so that any future restore of `m`, from a savestate or from M2b, also rebuilds or resets the side state.

23. **The M2 inventory needs a ruling for L1's statics.** A-3 (`m2a.md:347`) says a new exclusion needs a Fable ruling, so put it in L1's D-entry. Use file-scope statics with stable names; avoid function-local statics, whose `nm` names the compiler mangles.

24. **R-L1-8's evidence against the post-P0i anchor is not bound to any AC.** Name AC-1's goldens, calibrations and observation plans as that evidence under A-31 (`m2a.md:380`).

## Fable's choices on the critic's alternatives (S67)

- **Finding 1:** add an explicit allow-list option to `tools/rpc_measure_compare.py` naming exactly one changed object basename (`rpc_mem_timing`), failing on any other changed object, with a committed self-test; AC-5(b) and the layout checks stay mandatory; add a base-against-candidate layout identity check for `m`, `Cache`, `Tag` and `ArcRpcMemFast`.
- **Finding 2:** choose (a): a sentinel-maintained mirror with first-match, so the zero-fill, invalidate-skip and no-re-check mutants are killable through the line-0 and same-`va` invalid-slot cases; add test-only accessors for hint and mirror state and "hint rejected" and "mirror re-check failed" counters with exact directed expectations; kill the TEST_FAULTS-placement mutant structurally; list any remaining equivalent mutant with its proof.
- **Finding 3:** a test-only poison mode (seeded stale and out-of-range hints, stale mirror keys) under which AC-1's corpus and AC-4 run byte-identical with the fallback and re-check counters above zero.
- **Finding 4:** variant measurement cores (gate on/off, forced-off) use a separate macro and CMake option that `rpc_namespace.h` does not strip, built as separately named targets; AC-7 identity applies to the default target only.
- **Finding 6:** the gate-selection rule is frozen in this order before any measurement and confirmed on a fresh ABBA run; ARM610 and ARM7500 gates are fixed in advance from calibration counters.
- **Finding 7:** before dispatch, Fable records each frozen workload's hit-way histogram from the plan-core ranking in this order, with the predicted risk per workload.
- **Finding 8:** new counters join the stable schema with an explicit schema revision and a runner-core handshake (schema revision and `sizeof` check, refusing a mismatch); runner and core must come from the same revision; the `ranking-<name>` mutant loop gains directed expectations for each new counter in both builds.
- **Finding 9:** the reference path is the pre-L1 scan without counting; mutant "reference counts".
- **Finding 10:** the hint is a way index within the looked-up cache's set; the mirror is one per `Cache`; directed SA I/D same-line case and mutant.
- **Finding 23:** L1's statics are file-scope with stable names and are classified in the M2 inventory in L1's own PR, each exclusion pinned in `tests/rpc_lockstep_reviewed_policy.json` and recorded in L1's decision entry.
- **Ordering:** L1 is dispatched after M2b is dispatched (D-198(1)) and merges after M2b, before S1; it carries "M2 self-consistency passes" and "M2 lockstep against the immediate predecessor" in its ACs once M2b has merged.

## Astra challenge (S67, ACCEPT WITH CHANGES): Fable's adjudication

6b thread `01a13e62-8e2e-7f10-b35a-de57821ce0e7`, S67 (report private). Findings that concern L1 are incorporated and override earlier text.

- **AS-L1-6 (Astra 6) ABI handshake and predecessor comparison.** Every structure-copy test API (ranking, exercise, interval exercise and profile snapshots, since `ArcRpcProfile` embeds `ArcRpcExercise`) checks a schema revision and structure size before copying and refuses a mismatch, with a mismatch test for each. Comparisons against the predecessor use two explicit runner/core pairs. L1's inventory additions change only the inventory metadata hash, not M2b's wire-compatibility hash (M2b AS-6).
- **AS-L1-7 (Astra 7) Exact mirror; poison hints only.** The mirror stays exact, sentinel-maintained and first-match. The poison mode corrupts **hints only**; mirror integrity is tested by invariant assertions (in cross-check builds) and by mutants on every mirror writer (fill, invalidate, full invalidate, bind reset). Mutants that remove a now-redundant recheck from an exact mirror are reclassified as equivalent with their proof, not counted as kills.
- **AS-L1-8 (Astra 8) Gate table before dispatch (Fable's obligation).** L1 is not dispatched until Fable adds to this order: the candidate gate configurations per geometry (unified ARM610, ARM710, ARM7500; SA-110 I and D), the per-geometry selection workloads with their hit-way histograms from the plan-core ranking, the selection, tie and fallback rules, and the fixed ARM610 and ARM7500 gates. Selection data and the fresh confirmation ABBA data are kept separate. The A/A bands and the cumulative D-180 gate are unchanged.

## Gate table (Fable, S67, AS-L1-8; frozen before any L1 measurement)

**Evidence (plan-core ranking, 1a-0 profile build at `38eae49`; authored synthetics 1200 flat frames, window 200–1199; 031 window 6400–7400).** Lookups per instruction, way-0 share of hits, probes per hit, probe iterations per instruction:

| Workload / model | lookups/instr | way-0 share | probes/hit | probe iterations/instr |
|---|---:|---:|---:|---:|
| warm / ARM610 | 1.242 | 0.098 | 1.98 | 2.455 |
| warm / ARM710, ARM7500 | 1.242 | 0.220 | 1.78 | 2.212 |
| warm / SA-110 | 1.121 | 1.000 | 1.00 | 1.121 |
| stream-sa110, stream-arm710 / ARM610 | 0.333 (misses) | — | — | 21.332 |
| stream-sa110, stream-arm710 / ARM710, ARM7500 | 0.333 (misses) | — | — | 1.333 |
| stream-sa110, stream-arm710 / SA-110 | 0 (stores bypass the lookup) | — | — | 0 |
| ldm-stm8 / ARM610, ARM710, ARM7500 | 5.333 | 0.500 | 1.50 | 8.000 |
| ldm-stm8 / SA-110 | 5.333 | 1.000 | 1.00 | 5.333 |
| 031 / SA-110 | 0.549 | 0.032 | 16.56 | 9.745 |

The ranking counts lookups per access caller, not per SA-110 cache (I and D are not separated); the SA-110 gate therefore applies to both caches together.

**Rules (binding).**
1. **ARM710 (128×4) and ARM7500 (64×4): fixed linear** (no hint, no mirror). A 4-way hit averages at most 1.78 probes and a searched miss costs 4, so a hint's own validation consumes most of any saving, and the preferred CPU (ARM710, D-164(2)) carries no regression risk. Their AC-8 workloads (hot, stream-arm710) must show no regression only.
2. **ARM610 (4×64): fixed hint plus mirror.** Searched misses scan 64 ways (about 21 probe iterations per instruction on the streaming synthetics), which is the case the mirror accelerates; there is no frozen ARM610 benefit workload, so this is fixed from these counters, and exactness rests on the ACs.
3. **SA-110 (16×32, I and D): selected by measurement** among linear, hint-only and hint plus mirror. **Selection workloads:** 031 (deciding gain) and the SA-110 way-0 synthetics warm and ldm-stm8 (regression guards). **Selection run:** the 1a-0 AC-8 procedure (two six-run orders, measurement core, Odin3) on these three workloads only, with variant cores built per finding 4, **separate from the confirmation run**. **Rule:** choose the configuration with the largest 031 p50 gain beyond its A/A band (band₅₀) such that neither warm nor ldm-stm8 regresses beyond its band at p50 or p95 in either order. **Tie** (031 gains within each other's band₅₀): the configuration with fewer mechanisms. **Fallback:** if no configuration gains beyond band₅₀ on 031, SA-110 stays linear.
4. **Confirmation:** after selection, a fresh AC-8 run of the chosen configuration on every frozen workload (hot, the four synthetics, 031) decides merge under the frozen benefit rule (gain on 031, no regression elsewhere), and D-180 is rechecked on hot and 031 with observers genuinely off; the selection data is not reused.
5. The gate is computed at bind from the bound `sets` and `ways` (finding 15), not from the model name.

## SA-110 selection run 1 and run B (Fable, S67)

**Selection run 1** (Odin3, L1 run A variant measurement cores, the 1a-0 AC-8 procedure: two six-run orders per comparison, A = SA linear, 120 s idle; realtime 60 + 300 s for the synthetics, 031 under the S61 recipe, window 6400–7400; D-201 bands). p50 change of the candidate against linear, per order:

| Workload | Hint only | Hint plus mirror | Band₅₀ / band₉₅ |
|---|---|---|---|
| 031 (deciding) | −12.58 % / −13.07 % | −13.50 % / −13.30 % | 0.80 % / 0.62 % |
| warm (guard) | +4.94 % / +3.59 % (p95 +5.86 % / +3.60 %) | +3.61 % / +3.32 % (p95 +3.46 % / +36.7 %, one run met the clock-step rule) | 0.50 % / 0.50 % |
| ldm-stm8 (guard) | +4.69 % / +5.40 % (p95 +4.19 % / +5.00 %) | +4.37 % / +4.55 % (p95 +4.57 % / +4.69 %) | 0.50 % / 0.50 % |

Both candidates gain about 13 % on 031 but regress both way-0 guards beyond their bands in both orders, so by the frozen rule neither qualifies and SA-110 stays linear on run A's code.

**Cause (Fable).** The hint path computes its index with `line / c->sets` (a division by a runtime field) on every gated lookup, and after each hit recomputes it and stores the hint with `% c->ways`: about two to three hardware divides per lookup that the linear path, whose way-0 hit is one tag compare, does not pay (19 `udiv` in the object). Every cache geometry is a power of two, so this is an implementation cost, not a property of the design.

**Run B (ruling).** Sol makes the hint index and the way arithmetic division-free (per-cache shifts or masks fixed at bind, valid for the power-of-two geometries, asserted at bind) and stores a hint only when it changes; no other behaviour change, the same tests, mutants and identity checks. Fable then repeats the selection on run B's variant cores with the rule, workloads and bands unchanged. Selection run 1 stays on record; run B's cores supersede its candidates.

## SA-110 selection run 2 (Fable, S68)

**Selection run 2** (Odin3, run B's variant measurement cores, the same procedure, workloads, windows and D-201 bands; 72 runs, all rc 0; started 23:23 on 2026-10-17 after a device restart restored normal speed). p50 change of the candidate against linear, per order:

| Workload | Hint only | Hint plus mirror | Band₅₀ / band₉₅ |
|---|---|---|---|
| 031 (deciding) | −13.50 % / −12.99 % (p95 −13.17 % / −12.78 %) | −13.05 % / −13.02 % (p95 −13.22 % / −12.72 %) | 0.80 % / 0.62 % |
| warm (guard) | +5.75 % / +6.02 % (p95 +5.58 % / +6.00 %) | o1 invalid (see below) / +4.81 % (p95 +3.29 %) | 0.50 % / 0.50 % |
| ldm-stm8 (guard) | +7.99 % / +7.36 % (p95 +8.13 % / +6.32 %) | +8.73 % / +7.51 % (p95 +9.14 % / +7.70 %) | 0.50 % / 0.50 % |

**Device state.** The 031 legs and the hint-only warm legs ran at normal speed. From 03:39 (warm, hint plus mirror, order 1, run 4) the Odin3 slowed down again, as it did on the S67 evening: warm frame cost rose from about 40 ms to about 79 ms and ldm-stm8 from about 177 ms (run 1) to about 270 ms. The hint-plus-mirror warm order 1 straddles the change (two A runs slow, one flagged by the clock-step rule), so it is invalid. The later legs are internally paired (A and B alternate in the same state) but were taken on a slowed device.

**Outcome by the frozen rule: SA-110 stays linear.** Hint only fails on legs taken at normal speed (warm +5.75 % / +6.02 %, far beyond the 0.5 % band). Hint plus mirror fails its guards in every valid order (warm order 2, ldm-stm8 both orders), consistent with its normal-speed result in run 1 (+3.3 % to +4.6 %). Removing the divisions (run B) did not reduce the way-0 regression, so the cost is the hint path itself on way-0 hits, not the arithmetic. The selection rule is not revisited after measurement. With SA-110 linear, 031 runs the unchanged lookup, so L1 could not show the 031 gain that rule 4's confirmation requires, and the drop rule (D-195(6): a no-gain slice is not merged on benefit grounds) would apply to the whole slice.

**Run C (Fable's ruling, S68).** The cost sits on way-0 hits, which dominate the guards (every warm and ldm-stm8 lookup hits way 0) and are rare on 031 (97 % of 031's hits lie beyond way 0, AC-10). The SA-110 candidate set is therefore amended before any further measurement, with the rule, workloads, bands and procedure unchanged:
- **way0-hint:** the gated lookup compares way 0's tag first, exactly as the linear path does; only on a way-0 miss does it consult the validated hint, then the linear remainder (ways 1 onward). First-match semantics are unchanged because way 0 is the first way.
- **way0-mirror:** the same with the mirror for the remainder.
- Hint-only and hint-plus-mirror stay on record as measured; they are not re-run.
Sol builds both variants as selectable SA-110 gate configurations in the variant measurement cores (the same mechanism as run A's variants), with the same exactness tests, reference cross-check, mutants (including one that skips the way-0 check and one that consults the hint before way 0) and identity checks; ARM610, ARM710 and ARM7500 gates unchanged. Fable then runs **selection run 3** (way0-hint and way0-mirror against linear, the same three workloads, on a device verified at normal speed before the run and re-checked after it). If neither qualifies, SA-110 stays linear and L1 is not merged on benefit grounds; S1 is then re-planned onto `main`.
