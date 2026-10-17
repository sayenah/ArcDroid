<!-- SPDX-License-Identifier: GPL-2.0-only -->

# 6b Track M: slice M2a (lockstep contract, independent comparator, schedule reference and corpus)

Governing:
- **Decisions:** D-142(5) (`docs/decision-log.md:155`), D-146 (`:159`), D-182 (`:194`), D-188, D-189 (`:202`), D-191 (`:204`), D-195 (`:208`; R-b, R-c, cross-check naming), D-196 (`:209`).
- **`docs/riscpc-6b-plan.md`:**
  - baselines `:153-158`; CG-2 `:165`; CG-7 `:170`; host-only exclusions `:175`;
  - Track M, M2 `:250-261`;
  - amendments 1 `:633`, 6 `:673-678`, 7 `:680-685`, 8 `:687-696`, 11 `:717-721`;
  - Astra stage 1 findings 1, 3, 8 and 11 `:747,749,754,757`; rulings due before B-0 `:759`.
- **`docs/riscpc-translator-invariants.md`:** phases `:226-235`, §2.2 `:237-282`, exits `:286-300`, G-13 `:414-416`, method and coverage map `:427-450`, Q-5 to Q-9 `:459-464`.
- **Other work orders:** `docs/riscpc-6b-phase1a.md`; `docs/riscpc-6b-1a0.md` (R-e0-2, R-e0-4, scope 5 cross-check, AC-5).
- **Review:** drafted by the S67 design workflow (two independent drafts, synthesis, completeness critic; amendments A-1 to A-24); Astra challenge S67, ACCEPT WITH CHANGES, all seven findings incorporated (A-25 to A-31). Decision D-198.

**Branch:** `slice/6b-m2a-lockstep` from `main` at `2d8078e`. It is independent of PR #469 (1a-0), W6 and the R3c-3 branches. M2a edits no file those branches change, except for one `include()` line at the end of `CMakeLists.txt` (after the profile block; a trivial end-of-file conflict with #469 is expected and accepted). M2a changes no existing compiled source and no core: it adds new test-only files only, so guest behaviour, shipping output and the libretro surface are untouched.

**Binding amendments.** The section "Amendments (Fable, S67)" at the end of this order overrides any conflicting text above it.

**Why the work is split.** The H-1 emitter needs hooks at P0 to P3:
- in all three instantiations of `ARC_RPC_ARM_LOOP` (`engine/rpcemu/src/arm.c:568-1978`, instantiated at `:1981-2009`);
- in `rpc_mem_timing.{c,h}`, `cp15.c` and `tools/rpc_runner.c`;
- under a macro split that exists only on PR #469.

Everything that must stay independent of the emitter is built first, so that it can act as the emitter's oracle (`docs/riscpc-6b-plan.md:158`). The split:
- **M2a** (this order) is written in full.
- **M2b** (emitter and lockstep driver) and **M2c** (CI and lifecycle) are outlined at the end.

## Rulings needed (proposed)
- **R-M2-1 Order.**
  - M2a starts now.
  - M2b is dispatched as soon as PR #469 merges, and before the L1/S1 work orders take `arm.c` and `rpc_mem_timing`. The AC-10 ranking has to come before L1/S1 anyway (`docs/handoff.md:46`).
  - M2b merges before F1.
  - M2c follows W6 and M2b.
- **R-M2-2 Baselines.** The wording at `docs/riscpc-6b-plan.md:156` is ambiguous against D-195(4).
  - Proposed: baseline 2 for every B and X slice is the `main` commit at which Phase 1a's last slice merges. Because M2b merges before F1, both pinned baselines carry the emitter natively.
  - Any later schema change re-pins both baselines onto a test-only back-port branch. That branch is proven by object identity of every non-harness object (the method of 1a-0 AC-5(a)), never by assertion.
  - The performance anchor and the correctness oracle are recorded separately for each revision (Astra 11, `:757`).
- **R-M2-3 Record key and density.**
  - **Key:** (session generation, reset epoch, guest ordinal k, phase, P1 site kind, P1 sequence within k).
    - The generation is at `rpc_session.c:75`, bumped at `:267`. Reset does not bump it (`docs/riscpc-translator-invariants.md:180`), so the reset epoch is bumped at every G-13 point: start, reset, stop, failed start, test-rate rebind, the Arculator↔RPC switch, and R3d's reset/rebind (`docs/riscpc-translator-invariants.md:414-415`; `docs/riscpc-6b-plan.md:292,696`).
    - k counts instruction attempts since the last G-13 point, incremented once at the instruction-begin boundary (never by emitted records, so an E2 retry keeps its k), including instructions that prefetch-abort. Local `linecyc` is never part of the key (Q-7, `:462`).
    - P1 site kinds are semantic, never code locations: successful-fetch continuation; each data access (each LDM/STM transfer counts as one); SWI entry; CP15 read/write; exception entry; observed mode transition. This lets an inlined helper align with the interpreter's helper call (Astra 3).
    - An E8 fatal produces a terminal fatal record and no digest (Q-9, `:464`).
  - **Density.** A baseline stream is dense in every phase. A candidate stream must contain:
    - a P1 record at every helper entry, called or inlined (data accessors, CP15 read/write, exception entry, SWI), and a fetch-continuation P1 only for instructions it runs on the interpreter (amendment A-2);
    - exactly the baseline's set of P3 records, because host policy never adds a timing boundary (Astra 8);
    - P0 and P2 records at its exits, inside spans it declares sparse; the number of P0 and P2 records equals the candidate's own exit-class counters, and in the "exit after every instruction" schedule density is enforced as full (amendment A-5).
  - **Every candidate key must exist in the baseline.** Every emitted instance of a key (for example a second P0(k) after an E2 guard miss) is compared against the single baseline record (amendment A-4).
  - **Format version.** M2a's format is v0. M2b may change it once, with a version bump, when the projection ruling fixes the P1 points; from M2b on (v1) it is frozen (amendment A-6).
- **R-M2-4 Projection.**
  - Fable writes the shared, non-mutating projection specification for P0 to P3 into M2b's order (Astra 3; due before B-0, `:759`).
  - Until then, M2a encodes phase-conditional inclusion as provisional schema data. The data follows `docs/riscpc-translator-invariants.md:232-235`:
    - At P0, `issue_*` equals `cpu_due_*`. In integer mode, the issue time comes from the flush formulas without `next_base`. `batch_flush` (`engine/rpc/rpc_mem_timing.c:467-474`) is never used as a canonicaliser or as a test oracle.
    - At P2, `cpu_due_*` is excluded while requests are pending.
    - At P3, the state is whatever batch service left.
  - Completed-instruction count and successful-continuation count are separate fields.
- **R-M2-5 Independence.**
  - The comparator, stream reader, schedule reference and every expectation are Python and share no code with any C emitter.
  - Field names reuse PR #469's `arc_rpc_crosscheck_field` vocabulary where a name exists. Each field's meaning is defined once, in the schema.
  - Directed-ROM expectations come from models that never run the core (as `tests/rpc_plan_goldens.py:8-24` does), or the ROM is marked "differential-only" with a reason.
- **R-M2-6 Memory and MMIO.**
  - Each record carries the small canonical fields by value.
  - Between consecutive records, the stream carries two ordered logs:
    - guest-visible memory writes: agent (CPU, or a named device or DMA), address, size and value;
    - MMIO accesses: address, size, direction and value.
  - RAM and VRAM are page-hashed (4 KiB pages) at every frame end, every epoch change and stream end.
  - A divergence in a hash or log is localised by a deterministic detail rerun of both processes, stopping at the divergent key. If the rerun does not reproduce the divergence, the verdict is "non-deterministic", which fails.
  - Only stream hashes persist. No trace bytes go into the repository, logs or artifacts (CG-9, `:172`).
- **R-M2-7 Build class (applied in M2b; ruled now so Astra reviews it).**
  - A new option, `ARCDROID_RPC_LOCKSTEP`:
    - it requires the hot-path test-hook class after the split;
    - it is `#error` with `ARCDROID_RPC_MEASUREMENT`;
    - its hook macro compiles to `((void)0)` when off (the pattern at `engine/rpc/rpc_profile.h:6-9,55-58`).
  - It is enabled only in a non-installed core, `arcdroid_rpclockstep_libretro`.
  - Every other core refuses a lockstep request explicitly, and the runner exits non-zero.
  - 1a-0's capability matrix gains this row, and §2.2's measurement-state clause (`docs/riscpc-translator-invariants.md:266`) is restated in terms of the split macros.
- **R-M2-8 H-2 meaning (amendment A-1).**
  - An H-2 forced exit is a host-side block exit (E1, E2 or E4 shape) to P0(k+1) or P2(k) that never ends the batch. A forced candidate is compared in same-schedule mode against the unforced baseline stream, with every P3 required.
  - Before blocks exist, H-2 is specified and self-tested on synthetic streams only (M2a) and exercised by B-0 onwards. 1a-0's armed-only admission bound (R-e0-1) is a measurement exception and is not the H-2 mechanism.
  - Quota schedules 1, 7 and 200 are a separate Q-7/CG-5 family compared in cross-schedule mode (P0, P1 and P2 only); any disagreement there is an oracle finding for Fable.
- **R-M2-9 AC-H4 form.** A fan-in CI job compares per-item stream hashes that `host`, `host-arm64` and `windows` upload as hash-only text. This avoids creating a golden class that is never regenerated (CG-1). It is M2c's job.

## Objective
Before the emitter exists, deliver:
- a versioned lockstep contract and a machine-readable §2.2 schema;
- an independent streaming comparator that names the first divergent (key, field);
- the reference schedule for H-2 and CG-7, keyed by guest ordinal;
- the H-3 fuzzer and H-4 directed corpus, each with detection power **proven** on today's plan core.

## Scope
1. **Design note `docs/riscpc-lockstep-harness.md`.** It defines:
   - the key and density rules (R-M2-3);
   - a header carrying the format version, schema hash, model, plan identity, seed, schedule identity and per-span density declaration;
   - compact records (per-group 64-bit hashes plus the logs) and detail records (every named field, for a key range);
   - an end-of-stream record;
   - the encoding: little-endian, fixed width, field order taken from the schema, no padding;
   - pointer fields, typed only as (allocation id, offset) or an index (`docs/riscpc-translator-invariants.md:244`). A record carrying an untyped address is refused.
   - three comparison modes: same-schedule, sparse-candidate and cross-schedule;
   - the localisation-rerun protocol (R-M2-6);
   - a table mapping every §2.2 bullet and every exclusion (`docs/riscpc-translator-invariants.md:239-282`) to schema entries.
2. **Schema `tests/rpc_lockstep_schema.json`** (provisional, versioned, with a `"license": "GPL-2.0-only"` key in its first 1024 bytes).
   - Every field has a class: canonical, phase-conditional, measurement, batch-local, host-only/excluded or diagnostic.
   - Every field has a phase condition and an exclusion reason with its citation.
   - The schema keeps 1a-0's classes (R-e0-2) and D-195 R-b/R-c.
   - It is grounded on `main`'s §2.2. The grounding commit is recorded in the schema.
3. **Comparator `tools/rpc_lockstep_compare.py`.**
   - Reads two streams, from files or pipes.
   - Enforces header, schema-hash and density rules.
   - Stops at the first divergence and reports: key, phase, site, field, both values and the last matching key.
   - Memory use does not depend on stream length.
   - Emits the detail-rerun key range for the M2b driver.
4. **Synthetic stream writer and self-test `tests/rpc_lockstep_compare_test.py`**, plus committed comparator mutants. The writer is independent of any C code.
5. **Schedule reference `tests/rpc_lockstep_schedule.py`.**
   - A self-contained PRNG, specified in the design note with test vectors. It uses no Python `random`, no `hash()`, no set iteration order and no floating point.
   - Pure functions of (seed, epoch, ordinal):
     - "force exit after k" (H-2);
     - "disable or re-enable at P0/P3" (CG-7).
   - Two families:
     - exit after every instruction;
     - a phase-shifted cadence that, for a maximum block length L, places every ordinal at every intra-block position p < L (CG-2).
6. **H-3 fuzzer `tests/rpc_lockstep_fuzz.py`**, with seeds in `tests/rpc_lockstep_seeds.txt` split into CI and local-full subsets.
   - **Output:** project-authored ROMs and plans, generated at build time, for `arm610`, `arm710`, `sa110` and `arm7500` (`tests/arc_test_plan.c:400`).
   - **Program shape:** terminating, with recording vectors, a bounded budget and a RAM signature that fits `--ram-words` ≤1024 (`tools/rpc_runner.c:249-250`).
   - **Features** (`docs/riscpc-6b-plan.md:255`):
     - 26-bit and 32-bit modes, including the 26-bit carry at `0x3fffffc` and user-mode R15 masks (Q-6);
     - MMU off and on, with DACR none, client, manager and the reserved value 2;
     - self-modifying code through `0x10↔0x12`, `0x11↔0x13` and the VRAM alias, using every store form;
     - IRQ/FIQ edges with I/F masked in 26-bit modes;
     - full write buffers;
     - the SA clock switch;
     - ROMCR/CLKCTL writes while the bus drains;
     - observation quotas 1, 7 and 200 (`tests/arc_test_plan.c:461`).
   - **Reach:** each program carries a manifest of the features it intends, and the guest writes realised-feature counts into its signature.
   - The generator imports the `Guest` and `Matrix` helpers (`tests/rpc_timing_rom.py:10-17`; `tests/rpc_plan_matrix_rom.py:8-91`) and edits neither file.
7. **H-4 directed ROMs `tests/rpc_lockstep_rom.py`**, with expectations in `tests/rpc-lockstep-expectations.md` and an expectation module that never runs the core. At least one ROM per row:
   - **G-1:** per-instruction issue ticks; a delay-0 MMIO deadline; the NULL-batch limit instruction (`rpc_session.c:446`); quota 1 at field completion (`:420`); the capture-deadline clip (`:391-392`); the cheap-copy SWI truncation (`arm.c:1909`); a prefetch abort (`arm.c:601-606`).
   - **G-2:** the 26-bit carry, and user-mode R15 masks in a hot loop.
   - **G-3:** the ordinal at which an event-raised IRQ is taken, and the masked `reg[16]` refresh (`arm.c:1933-1950`).
   - **G-4:** a data access, and separately a denied install, evicting the fetch entry.
   - **G-6:** data and code in one cache set mid-sequence.
   - **G-8:** ROMCR/CLKCTL written while the bus drains, and in the instruction whose own fetch they reprice.
   - **G-9:** all three alias pairs; STR, STRB, STM, STRT and SWP; a store to PC+4; same-block SMC; an STM partial abort.
   - **G-10/E11:** c7 maintenance in a hot loop.
   - **G-14:** c15 wait on `sa110`; ARM7500 SUSMODE; writes pending across sleep and wake; wake while masked (`docs/riscpc-6b-plan.md:688-689`).
   - **Amendment 1:** one physical page at two VAs plus the MMU-off mirror, running B, BL, ADR and LDR-literal from both (`:633`).
   - **AC-M2 overlap** (`:244`).
   - **AC-H2 support:**
     - interleaved fetch and data cache hits;
     - a faulting data access after a successful one;
     - integer batching at a rate with `base_fraction==0` and at one without;
     - 26-bit mode with a pending event and changing PSR flags.
   - G-13 is deferred to M2c.
8. **Oracle consistency test `tests/rpc_lockstep_corpus_test.py`** on `arcdroid_rpcplan_libretro`.
   - Two runs per item.
   - Quotas 1, 7 and 200 per item, compared with the projection at `tests/rpc_observation_plan_test.py:17-18`.
9. **Row-sensitivity mutants `tests/rpc_lockstep_mutants.py`.**
   - Single-site perturbations of existing oracle code, built in temporary objects through `rpc_ninja_commands.py`, the same way as `tests/rpc_qa_mutations.py`.
   - They run only on Ninja builds without sanitizers (`CMakeLists.txt:2198`).
   - Each anchor must match exactly once, or the test fails.
   - Anchors inside 1a-0's hunks are listed for porting.
10. **Coverage proof `rpc-profile-lockstep-coverage`.**
    - Exit-class and instruction-class counts over the CI corpus, taken from the profile build (`tools/rpc_runner.c:235`).
    - Its build dependency attaches to the existing `rpc-profile-tests` target, so CI's profile step (`.github/workflows/ci.yml:55-59`) runs it without a workflow edit.
11. **Build wiring (amendment A-13).**
    - One `include(cmake/rpc-lockstep.cmake)` line at the end of `CMakeLists.txt`, after the profile block (`rpc-profile-tests` is defined at `:2943` inside the `if(ARCDROID_RPC_PROFILE …)` opened at `:2907`).
    - The included file:
      - guards itself;
      - attaches the coverage test with `if(TARGET rpc-profile-tests)` and registers `rpc-profile-lockstep-coverage` only under `ARCDROID_RPC_PROFILE`;
      - has its own generation command and output directory;
      - does not edit the shared `RPC_PLAN_DIR` DEPENDS list (`:2457-2459`);
      - gives every test an explicit timeout.

## Non-goals
- **Edits to existing files:**
  - no file under `engine/` or `libretro/`, and not `tools/rpc_runner.c`;
  - no change to `.github/`, `tests/arc_test_plan.c` or any existing test, golden, generator or mutant suite;
  - no change to the §2.2 text.
- **Deferred to M2b and M2c:**
  - the emitter, the build class and the C side of H-2 (M2b);
  - device and observer serialisation (M2b);
  - G-13 ROMs and cross-platform CI (M2c);
  - tiny-cache and code-cache-cap modes (B-0/X-1).
- **Excluded from the corpus:** private titles.

## Architectural constraints and affected interfaces
- **Test-only and zero footprint.** No compiled source changes, so every core is untouched and the guards at `CMakeLists.txt:450-461` see no change. Mutants exist only in temporary objects.
- **Namespace.** Nothing new references `arc_rpc_*` symbols (`tools/check-backend-symbols.py:168-195`). The C interfaces for M2b are specified in the design note, not implemented.
- **§2.2 classes.**
  - A field with no class is a schema error, not a default.
  - Every host-only structure M2a defines for later slices is listed as an exclusion that is reset at every G-13 point (`docs/riscpc-6b-plan.md:175`).
- **Provenance and public repository.**
  - Every file is written from scratch.
  - Every authored file carries `GPL-2.0-only` in its first 1024 bytes or a `.license` sidecar (`tools/check-licenses.py:17-18,32-34`).
  - ROMs and programs are emitted at build time by text generators (`tests/rpc_cpu_rom.py:2`). Generated bytes are never committed; the asset guard enforces this (`tools/check-assets.sh:7-35`).
  - No private name appears in any file, comment or output.
  - There are no engine/rpcemu notice or manifest changes in M2a.
- **Determinism.** Identical bytes and verdicts on Linux, Windows and macOS under every supported Python 3: no locale, time or path dependence, and LF-only output (`tests/rpc_plan_test.py:16-25`).
- **Frozen interfaces.** The key, density rules, schedule functions, record encoding and localisation protocol are the contract that M2b and every B/X candidate must satisfy. Any change to them is a versioned format change ruled by Fable.

## Acceptance criteria
- **AC-1 Contract and schema.**
  - The mapping table covers every §2.2 bullet and exclusion.
  - The schema validates: every field has a class, a phase condition and a citation, and names are unique.
  - Schema mutants are rejected: an unclassified field; a duplicate name; an untyped pointer field.
- **AC-2 Comparator.** Tested on synthetic streams.
  - **Must pass:**
    - identical streams;
    - differences only in excluded fields: lanes past `count`, dead ring slots, invalid-tag `va`/`physical`, and diagnostic counters;
    - a P2 `cpu_due` difference while requests are pending;
    - a sparse candidate whose P0 and P2 records are subsets of the baseline's, inside declared spans.
  - **Must fail, naming the earliest (key, field):**
    - one perturbation per field class at each phase;
    - a P2 `cpu_due` difference with no requests pending;
    - two divergences, where the earlier one must be reported;
    - a candidate key absent from the baseline;
    - a P1 or P3 set mismatch in either direction;
    - a log entry that is extra, missing, reordered or attributed to the wrong agent;
    - an epoch or generation mismatch;
    - fatal records at different keys;
    - a truncated stream;
    - a mismatch in version or schema hash (refused);
    - a localisation rerun that does not reproduce the divergence ("non-deterministic").
  - **Constant memory:** peak memory is the same, within noise, for 10⁵ and 10⁶ records compared over pipes.
  - **Comparator mutants, each committed and killed:**
    - ignore one field;
    - align by record index instead of key;
    - drop a phase condition;
    - widen a phase condition;
    - accept truncation;
    - accept an extra candidate key;
    - accept a missing P1 or P3;
    - report the last divergence instead of the first;
    - ignore log order;
    - treat a non-reproducing rerun as a pass.
- **AC-3 Schedule reference.**
  - The PRNG and schedule vectors pass.
  - For L ∈ {1, 2, 7, 64, 200}, the cadence family is shown, over a synthetic ordinal range, to place every ordinal at every position p < L.
  - Output is identical under a different call order.
  - Mutants killed: a schedule keyed on call count instead of ordinal; a family with one position missing.
- **AC-4 Fuzzer reach.**
  - Every CI seed, for every model, terminates within its budget on the plan core.
  - Generation is deterministic within a job: two generations under different `PYTHONHASHSEED` values give identical bytes. No hash pins are committed; cross-platform equality of generated bytes is checked by M2c's fan-in job (amendment A-16).
  - Every scope-6 feature has a realised count above zero for each model that supports it.
  - In the profile leg (scope 10), E3, E4, E6, E7a, E7b, E10 and E11 are above zero. Each structural zero is listed with its reason; for example, E9 cannot occur on the oracle.
  - Generator mutants that each remove one feature each fail the floor.
- **AC-5 Directed ROMs.**
  - Every scope-7 row passes on the plan core against its independently stated expectation.
  - Differential-only ROMs are listed with reasons.
  - No expectation is derived by running the core.
- **AC-6 Row sensitivity.** Each row has at least one committed single-site oracle mutant that its ROM kills through runner stdout or the RAM signature:
  - 26-bit carry into F dropped;
  - IRQ taken one instruction late;
  - masked `reg[16]` refresh skipped (`arm.c:1934-1937`);
  - FIQ taken immediately after a prefetch abort (contrary to Q-8);
  - a denied install that does not evict the fetch entry;
  - a data fill that does not evict the executing line;
  - CLKCTL or ROMCR repricing applied only after the drain;
  - a store to PC+4 not seen by the next fetch;
  - c7 maintenance as a no-op;
  - writes not drained during idle;
  - wake ignored while masked;
  - DACR 2 treated as manager;
  - permission check skipped on a TLB hit.

  Mutants that are invisible without the emitter are listed as M2b digest-only kills, each with its predicted first divergent field. At minimum this covers the four AC-H2 mutants (`docs/riscpc-6b-plan.md:259`), the amendment-1 "VA dropped from key" mutant and the G-10 generation mutant. The kill matrix is reported, and any mutant killed by only one item is flagged.
- **AC-7 Oracle self-consistency (AC-H1-lite, Q-7).**
  - Two runs give byte-identical stdout.
  - Quotas 1, 7 and 200 give identical projections.
  - Any disagreement fails the test and is reported to Fable as an oracle finding. No item is excluded without a ruling.
- **AC-8 No footprint.** `git diff --stat` against the base shows only the new files and the single `include` line. All guards and the full host suite pass.
- **AC-9 CI cost.**
  - Every new test has an explicit timeout. The worst host and Windows durations are at most 60 % of it (W4's rule).
  - The CI seed subset is sized to that rule, and the size of the local-full subset is documented.
  - Mutant tests are `RUN_SERIAL`.
- **AC-10 Provenance and public text.** The licence, asset, public-text and import guards pass. No generated byte is committed.

## Evidence
- **Sol** reports in `SOL-REPORT-6B-M2A.md` at the clone root (not committed):
  - commands and results for AC-1 to AC-10;
  - comparator, schema and schedule mutant kill lists;
  - feature and exit-class coverage per model, with structural zeros;
  - the AC-6 kill matrix and digest-only list;
  - per-seed hashes from host and MinGW;
  - CTest durations.
- **Fable (private):**
  - the M2a suite on Mac arm64, as an early AC-H3 leg, with corpus hashes compared;
  - Windows durations checked against W4's rule;
  - a ruling on any AC-7 oracle finding before M2b's order;
  - the M2b projection specification taken through Astra stage 2.
  - There are no Odin3 legs.

## Risks and QA focus
- **The comparator acting as its own oracle.** Check that it shares no code with C and that phase conditions are data.
- **Density and sparse rules too loose.** A missing P1 or P3 must fail.
- **Over-wide exclusions.** The P2 no-pending `cpu_due` control must be killed.
- **Projection drifting toward a flush.** Watch for `cpu_due` being treated as issue + base at P0.
- **A fuzzer that runs but never reaches its features.** Reach is proven by realised counts, not by intended manifests.
- **Expectations derived from the core.**
- **Mutant anchors in 1a-0 hunks**, especially `cp15.c` `checked_translation`.
- **Generator drift** across Python versions and platforms.
- **Windows CI time.**
- **Leakage into the public repository** through names or output.

## M2b outline (after PR #469 merges; before L1/S1 and F1)
- **Setup.** Apply R-M2-7, then re-ground §2.2, the schema and every citation at the post-#469 head.
- **Hooks in all three loop copies:**
  - P0 before `arc_rpc_mem_instruction_begin` (`arm.c:597`);
  - P1 at fetch continuation (`:613`), the accessors (`mem.c:699-731`), `arc_rpc_mem_access` (`rpc_mem_timing.c:325`), LDM/STM (`arm_common.c:271,351,428,494`), SWI (`:569`), CP15 (`cp15.c:163/300`) and the cheap-copy SWI (`arm.c:1907-1909`);
  - P2 after `instruction_end` (`:1960`);
  - P3 after the final flush (`:1975`), including the NULL-batch path.
- **Sink.** Bytes go through a sink the runner owns. The engine has no stdio (`tools/check-backend-symbols.py:137-155`).
- **Serialisers:**
  - per owning translation unit, field by field;
  - pointers expressed as allocations, and an unmapped pointer fails;
  - device state in upstream translation units, with notices in the first 2 KiB, manifest lineage and `PROJECT_HEADERS` (`tools/check-rpcemu-import.py:28-33,176-189`);
  - observer canonical state (B-6).
- **Driver.** Two processes over pipes into the M2a comparator. It extends 1a-0's prefix and endpoint runner flags. H-2 is checked against M2a's vectors.
- **ACs:**
  - `offsetof`/`sizeof` accounting, so that every struct member has a class;
  - field sensitivity;
  - runner stdout identical with digests on and off;
  - P0, P2 and P3 records identical with P1 on and off;
  - identical streams across the three loop copies;
  - an allocation-shift run with an identical stream;
  - projected P0 at quota 200 equal to materialised P0 at quota 1;
  - full AC-H1, and Q-7 in cross-schedule mode;
  - object identity of the shipping and measurement cores.
- **Mutants:**
  - the four AC-H2 mutants;
  - Astra 1, 2 and 3: replay after a helper, repeated exception handling, demand before a buffered store, queued video at grant end, delayed video completion, one-instruction quota, final-instruction overflow, stale integer representation, a digest that flushes state;
  - a P1-transient mutant, shown killed with P1 on and escaping with it off;
  - a pointer hashed as an address;
  - M2a's digest-only list.

## M2c outline (after W6 and M2b)
- **AC-H3:** Linux x64, Windows and `host-arm64`. Mac stays local unless a free macOS runner is verified.
- **AC-H4:** the R-M2-9 fan-in job, sanitizer-regex additions, job `timeout-minutes` under W4's rule, and D-100's cost rules.
- **G-13 ROMs:** reset, reload, failed start, rebind, switch and R3d reset/rebind, with reused addresses.
- **CG-7:** the disable/re-enable consumer interface and the chaos-stream equality test, ready for B-0.
## Amendments (Fable, S67)

From the S67 design workflow's completeness critic; all incorporated. Each overrides conflicting text above.

- **A-1 H-2 meaning.** As restated in R-M2-8: a forced exit is a host-side block exit that never ends the batch, compared same-schedule with every P3 required; quota schedules are a separate Q-7/CG-5 family.
- **A-2 P1 density.** A candidate emits P1 at every helper entry, called or inlined; the hook lives inside the shared helpers (accessors `mem.c:699-731`, `cp15_read`/`cp15_write`, exception entry, `opSWI`) so no caller can bypass it. Fetch-continuation P1 is required only for interpreted instructions. Candidate P1 records match by (k, site kind, sequence). M2b mutant: an inlined helper with no P1 hook is killed.
- **A-3 State inventory.** New scope item 12: a symbol-inventory check maps every data and bss symbol of the RPC and rpcemu objects (from `nm` on the host build) to a schema class; an unmapped symbol fails and any new exclusion needs a Fable ruling. The schema lists device state field by field per translation unit (including `iomd.c`, `fdc.c`, `keyboard.c`, `i8042.c`, `vidc20.c` statics), and the capture state and deadline, `arc_rpc_test_quota`, scheduler `rate_mips` and `cpu_delta`, `arc_rpc_bus_until` and `arc_rpc_bus_ns` (`rpc_session.c:56,390-392,417-419`; `rpc_scheduler.h:27,35`). Projection rules are stated for `arc_rpc_executed` (lags until batch return, `rpc_session.c:423,446`: projected as base plus the batch-local count) and for `instruction_work` (a stack local at `:394`: driver-local, ruled out of the digest or exposed by M2b, stated in the design note). AC-1 gains: the inventory passes on `main`, and an added unclassified static fails it.
- **A-4 Duplicate keys.** Every emitted instance of a key is compared against the single baseline record. Comparator tests: an identical duplicate passes; a differing duplicate fails. M2b mutant: a guard miss that applies `instructions++` or a TLB fill.
- **A-5 Sparse declarations are checked.** A candidate's P0 and P2 counts equal its own exit-class counters; the every-instruction schedule enforces full density. Comparator mutant: a sparse declaration that is never checked.
- **A-6 Format v0.** M2a's encoding is v0; M2b may change it once (v1) when Fable's projection ruling fixes one exact P1 point per helper (before or after translation, aborted accesses, whether the fetch-timing call counts). M2b's order carries that ruling, taken through Astra.
- **A-7 Observer configurations.** Every H-3 and H-4 item runs under observer off, unmatched, trace and active (as `rpc-observation-plan` does, `tests/arc_test_plan.c:455-465`), so all three loop copies are exercised; the G-1 cheap-copy truncation ROM names its observer keys. M2b AC: the stream is identical with trace on and off.
- **A-8 Reach evidence.** Guest-reported counts prove only the program path. In M2a, reach uses profile-build counters where they exist (MMIO and write counts, exits; `rpc_profile.h:26-31`). Bus-state features (write buffer at capacity, timing-register write with `m.count > 0`, stale fetch after SMC) are proven in M2b from stream field values and listed in M2b's ACs.
- **A-9 Timing-row channels.** For each timing row (G-1 issue ticks and delay-0 deadline, G-8 repricing mid-drain) the design note names the evidence channel: either an amplification loop whose frame-level instruction count shifts by a stated amount, or a new test-only C harness on the existing intrusive interval API with `tests/rpc_timing_oracle.py` as the independent model. New test-only C files are allowed; existing compiled sources and cores are not edited.
- **A-10 Conditional exclusions.** AC-2 lists every conditional §2.2 pair explicitly (invalid TLB entry contents, `memo_line` while `!memo_valid`, `finish` with no cycle or video active, `active` while `!active_valid`, unarmed scheduler deadlines, inactive observer slots, post-fatal registers, lanes past `count`, dead ring slots, `cpu_due` at P2 with requests pending): a must-pass when the condition holds and a must-fail when it does not. Comparator mutants "condition inverted" and "condition ignored" for each.
- **A-11 Fatal paths (Q-9, E8).** The fatal record carries the RAM page hashes and the committed write and MMIO logs up to the fatal. M2a adds directed fatal ROMs for the guest-reachable fatals (bad mode `arm.c:169`, request or packet budget, instruction budget `rpc_session.c:414`; time overflow if guest-reachable, else listed) with expectations stated without running the core. The reload-after-fatal leg is M2c.
- **A-12 Stray writes and walks.** MMIO is logged at the physical I/O dispatch, tagged by agent (CPU access, descriptor walk, device or DMA), so speculative or guard-time walks appear as extra entries (Q-10). Directed items take page-hash checkpoints at every P3. Comparator mutant: an unlogged stray write later overwritten. H-4 adds a ROM with descriptors placed so walks are observable (G-3 walk MMIO narrowing, G-4 no speculative walks).
- **A-13 Build wiring.** As restated in scope 11.
- **A-14 Cadence cost and real positions.** CG-2's every-position proof needs candidate-side (block, position) coverage counters (CG-3 style), owned by B-0. In CI the cadence family is bounded by AC-9's rule with seeded position sampling; the full family runs locally.
- **A-15 Obligations on Phase 1a (R-M2-2).** Baseline 2 is pinned to the F1 merge commit by a decision entry when F1 merges. Every 1a slice that merges after M2b adds "M2 self-consistency passes" to its ACs. Fable records the scheduling change against D-195(1)/(4) in that entry.
- **A-16 No golden pins.** No per-seed hash is committed (R-M2-9); determinism is checked within a job under two `PYTHONHASHSEED` values, cross-platform in M2c's fan-in job.
- **A-17 Constant memory, structurally.** The comparator exposes its maximum buffered-record count; AC-2 asserts it is bounded independently of stream length, rather than measuring process memory.
- **A-18 Build class.** R-M2-7 requires both halves of 1a-0's split (the test-API part and the hot-path test-hook part); the sink API is exported on Windows through `tests/rpc_test_exports.def` and checked by `rpc-windows-test-exports`; `docs/riscpc-translator-invariants.md:266` is restated against the split in M2b.
- **A-19 Field names.** Alignment with 1a-0's cross-check field names is deferred to M2b's re-ground at the merged #469 head.
- **A-20 G-13 deferral.** Fable rules G-13's lifecycle ROMs into M2c (they need lifecycle hooks). M2a keeps the oracle-only reset and allocation-fault legs that need no edits (`--reset-at`, `--fail-alloc`; `rpc_runner.c:251-253`).
- **A-21 Amendment 7 owner.** Endpoints inside a fold (`docs/riscpc-6b-plan.md:680-685`) are owned by B-1, B-2 and B-4, using M2's schedule functions over 1a-0's endpoint mode.
- **A-22 Citation.** G-14 expectations cite the current c15 wait (`cp15.c:280`, SA-110 only) and SA clock switch (`:281-283`); the plan's `cp15.c:270` is stale.
- **A-23 Hash-only output.** The comparator has a mode that prints hashes, never values, for Fable's private legs; in CI only authored items ever print values (CG-9).
- **A-24 Astra 8 under lockstep.** Allocation-failure injection and fatal-unwind cleanup under lockstep belong to M2c's G-13/E8 leg.

## Astra stage 1 (S67, ACCEPT WITH CHANGES): Fable's adjudication

Thread `01a13e62-8e2e-7f10-b35a-de57821ce0e7` (the 6b thread), report `.work/s67/m2/report1.md` (private). All seven findings are incorporated; they override earlier text.

- **A-25 (Astra 1) Comparison modes.** Schema conditions gain a comparison-mode axis beside the phase axis. Four modes, each defined in the design note: (a) strict, same schedule and same configuration (every included field); (b) cross-quota chronology projection (quotas 1/7/200; representation fields such as `integer_batch`, local counts and batch-local state excluded by named rule); (c) cross-configuration guest-output comparison (observer off/unmatched/trace/active: guest-visible state and stdout only, since filters and owner state legitimately differ); (d) trace on/off with unchanged semantic subscriptions (everything except trace output). M2a tests that every mode-specific exclusion is inert in strict mode (a perturbation of each such field fails strict comparison). A-7's "identical across loop copies" and the M2b trace AC are restated in these modes.
- **A-26 (Astra 2) Ordinal and compiled fetch.** k counts instruction attempts (R-M2-3 as amended). A "full audit" mode requires a fetch-continuation checkpoint for every instruction, compiled or interpreted, produced for compiled code by the non-mutating projection; ordinary sparse runs may omit compiled fetch-continuation records only under an explicit, counted coverage rule. Mutant (owner: the first slice that compiles instructions): a compiled-fetch transient (wrong opcode, attributes or fetch accounting at continuation, restored before P2) is killed by the full audit. A forced exit adds no batch boundary; a naturally required P3 still occurs, with the continuation performed exactly once.
- **A-27 (Astra 3) One event chronology.** Memory writes and MMIO accesses carry one common sequence number per epoch, with the semantic instruction and phase identity (k, phase, P1 site and sequence) and the guest issue or service time appropriate to the event. The comparator aligns events by this identity, independently of where record boundaries fall. Comparator tests: a RAM/MMIO cross-order swap fails; a time shift with unchanged values fails; the same events packaged under different sparse checkpoint boundaries pass.
- **A-28 (Astra 4) Two obligations for writes.** The comparator proves only that corrupted or missing **represented** evidence is detected; the "unlogged stray write later overwritten" mutant leaves the comparator kill list. Proving that real writes cannot bypass the evidence (native stores, helpers, DMA) is an emitter obligation: M2b specifies an independent write-observation mechanism for the full audit mode (for example a shadow copy of guest memory checked against the log at every P3, or page protection), and B/X slices that add native stores inherit it. No claim is made that denser checkpoints alone detect a write-and-restore between checkpoints.
- **A-29 (Astra 5) Inventory in three categories.** A-3's inventory covers (i) static roots (data and bss symbols), (ii) reachable storage (pointee contents: observer filter SWI arrays `rpc_observer.h:35`, the VIDC presentation buffer `vidc20.c:240,412`, every allocation reachable from a static root), and (iii) live control locals of the batch driver (`instruction_work`, `event_work`, `video_start`, `rpc_session.c:393-394`). Each entry is classed as contents-compared, immutable-input identity, output-compared, or excluded with a ruling. Remaining budgets are preserved through a phase-specific projection, not excluded. `arc_rpc_executed` is defined separately at P0, P1, P2 and P3 and on the NULL-batch path, stating whether the current attempt has completed.
- **A-30 (Astra 6) Kill-matrix owners and evidence levels.** Every mutant has an implementation owner: existing-core mutants in M2a; serialiser and projection mutants in M2b; lifecycle mutants in M2c; block-key, VA-key (amendment 1) and G-10 generation mutants in the slice that introduces the mechanism (B-0 onward). The ROMs and independently stated expectations for all of them are delivered now. The kill matrix labels each entry **demonstrated**, **awaiting emitter** or **awaiting candidate mechanism**.
- **A-31 (Astra 7) Anchors and backports.** Fable records the scheduling change in a decision entry before M2a is dispatched (D-198). D-195's Phase 1a anchor (cumulative post-P0i tree) stays as it is; the B/X anchor is the F1 merge commit, pinned when F1 merges. Later 1a slices that merge after M2b run M2 lockstep against their immediate predecessor (both carry the emitter) and keep D-195's existing evidence against the post-P0i anchor (goldens, calibrations, observation plans); no emitter is back-ported to the post-P0i anchor. Any future back-port requires identity of **all** production objects with lockstep disabled plus hook-on/off equivalence, and records a semantic baseline revision separately from an instrumentation revision. This replaces R-M2-2's back-port text and A-15's obligation.

## Rulings on Sol's M2a run (Fable, S67)

- **A-32 (A-11 at this head).** The plan core refuses guest-RAM reads after a fatal (`engine/rpc/rpc_session.c:598`), and M2a may not edit sources. The fatal items keep their register and terminal-fault expectations; their committed-RAM expectation is labelled **awaiting emitter**. M2b's fatal record carries the RAM page hashes and the committed write and MMIO logs up to the fatal (A-11), and M2c owns reload after fatal.
- **A-33 (AC-7 on fatal frames, Q-9).** On a frame that ends in an E8 fatal, the runner's frame instruction count (`cpu=`) depends on the quota, because `arc_rpc_executed` is updated only when a batch returns and the fatal unwind skips that accounting. That count is post-fatal diagnostic state, outside equivalence under Q-9. The cross-quota comparison mode (A-25(b)) therefore excludes the frame instruction count on the first frame whose `fault=1` appears, by a named schema rule citing Q-9. Strict same-quota comparison keeps it, and every other field of that frame and every non-fatal frame stays compared. A comparator test shows the rule applies only to that frame and field. M2b defines the executed count at a fatal exactly (A-29).
- **A-34 (inventory classes).** `arc_rpc_jump` (the host continuation used only to unwind a fatal), `arc_rpc_logger` and `arc_rpc_logger_opaque` (the frontend's log callback and its context) are **host-only, excluded**: they hold no guest-visible state, their values are host addresses that legitimately differ between processes, and they are rebound by the frontend at session start. The inventory records each with this reason.

## M2b citation refresh (S67, merge-base `0966af3`)

This refresh locates the existing obligations after the 1a-0 macro split;
A-1 through A-34 remain binding. The shared loop starts at `arm.c:572`, P0
precedes `:592`, instruction end is `:1963`, and the wrapper is `:2057`.
The accessors are `mem.c:704,713,722,731`; CP15 write/read are
`cp15.c:163,300`, successful translation attributes are assigned at `:449`,
and the generic fetch implementation starts at `:494`. Timing access is
`rpc_mem_timing.c:498`, flush `:644`, ready `:886`, and continuation/service
`:901`. Session fatal is `rpc_session.c:117`, successful generation update
`:283`, reset `:321`, instruction budget `:430`, and test-rate rebind `:633`.
Request/packet budgets are `rpc_mem_timing.c:333,344`, and time-overflow checks
are `:211,217`. These are pinned merge-base citations, rather than line numbers
in the changing emitter working tree. The v1 points and projections are in
`riscpc-lockstep-harness.md`; H-2 consumption remains deferred to B-0 per M12.
