<!-- SPDX-License-Identifier: GPL-2.0-only -->

# 6b Phase 1a: exact interpreter optimisations (D-191)

Status: **adopted (D-191)** with Astra stage 1's seven corrections (end of this file); the reconciled slice work orders go into `docs/handoff.md` once R3c-2 merges and 6b-P0i is rebased, each with its own Astra stage 2. The body below is the S64 read-only research workflow's ranked candidates and draft work order (verified against `7b11b47` by its agents with scratch prototypes on a loaded Mac; nothing in it is gate evidence). This header re-ranks it with the 6b-P0i profile of representative gameplay and states Fable's proposed rulings. Q1 (design objective) is with the Founder; Phase 1a is the same under either answer.

## Evidence since the draft (6b-P0i `1bb22aa`, `docs/riscpc-translator.md`, *Representative gameplay (S65)*)
- 031 sustained racing on the Odin3 (unprofiled, two runs within 1 %): p50 1.48 T, p95 1.70 T (T = 19.97 ms); the frame gate needs about 2.1×.
- Host cost split (sampled, probe-bias corrected): **Odin3: timing service 51 %, fetch translation 16 %, dispatch 16 %, bus service 16.5 %, devices < 1 %.** Mac: timing 58 %, fetch 17–20 %, dispatch 15 %, bus 6–9 %.
- Class mix: ALU 49.7 %, loads 21.7 %, branches 21.0 %, LDM/STM 4.1 %, stores 2.2 %; about 0.38 bus calls per instruction; fetch memo hits 84.4 %.
- Blocks average 4.1 instructions; E1 197, E3 39, E4 10.4, E9 7.7 per 1000 instructions.

## Re-rank (Fable)
1. **1a-0 (E0)** stays first: without it the calibrations never exercise S1 and no fast path is proven to run. Add the counters the re-rank table marks "(add)": lookups per instruction with hit way and probes per hit; batch_service entry classes (pending, busy-not-pending, skip-eligible, due ≥ finish, video active); condition-pass and AL share; data and fetch TLB walks per instruction; fill packets per instruction and shortcut commits.
2. **L1 (lookup acceleration)** and **S1 (bus-busy skip, with the commands() guard)**: the timing service is half the Odin3 cost and bus service another sixth, so both stay at the top. Their relative order is fixed by 1a-0's counters on 031 and an ARM710 workload.
3. **A1 then F1** (data and fetch hit paths): fetch translation is 16 % on the Odin3, below the cache-hot Phase 0 third, so F1 is not promoted; A1 precedes F1 as drafted.
4. **D1 + D2**: dispatch is 16 %, under the 30 % promotion bar; keep D1 (nearly free) and bundle D2 with it.
5. **B3, B2, B4**: only with Odin3 evidence of gain.
6. **New vetting item:** the rational continuation table for ARM710/ARM7500 (the area maps flagged it; ARM710 is the preferred CPU, D-164(2), and never uses integer batches). It needs a vetting round and its own exactness argument before entering 1a.

## Rulings (recorded as D-195, S66; slice 1a-0's work order is `docs/riscpc-6b-1a0.md`)
- **R-a** admit timing-service exact items into Phase 1a beside the fetch path, in the order above (amends D-146's "starts with fetch-path overhead").
- **R-b** a single shared inline hit-and-permit check on live DACR, ctrl, memmode and rw per access counts as keeping every per-fetch check; no F3, R1 or R2 in 1a.
- **R-c** the §2.2 host-only exclusions, E2 wording, citation refresh and D5(b) re-record as drafted.
- **R-d** 1a slices that edit `arm.c` stack after 6b-P0i merges; P0i's pinned baseline is measured first.

## Expectation
Phase 1a together is estimated at 20–25 % on 031; it narrows the gap to AC-10 and does not close it. The block/exit foundation (and, depending on Q1, a native backend) follows. With runs of four instructions, the foundation's value lies in carrying timing and fetch work across a run, not in dispatch alone.

---

# Body: the S64 workflow's ranked candidates and draft work order

## 1. Surviving candidates, ranked by expected gain per unit of risk

Duplicates are merged: L1 = F5 + M3; S1 = B1 + M1 plus the exact part of M5; A1 = F2 + M2, with F4 folded in. "Exact" means exact under the current contract once the stated conditions hold. The only ruling any of them needs is a classification or wording ruling. None of them reuses a permission result.

| Rank | ID | Where (7b11b47) | Idea | Expected gain | Risk | Verification it needs |
|---|---|---|---|---|---|---|
| 0 (enabler) | **E0** (B6 plus harness work) | `rpc_mem_timing.c` TEST_FAULTS gates; CMake measurement core; a new test-only cross-check option | (1) Narrow the measurement gate to `measure_last && instructions >= measure_last`, the same test ready()/next() already use. (2) Add committed exercise counters for each fast path. (3) Add a separate cross-check build option, not TEST_FAULTS, that compares each fast path with the reference on every call. (4) Add a measurement core with shipping flags; rpc_runner currently needs TEST_FAULTS symbols. | None by itself. It is what makes the calibrations exercise S1: with the narrowed gate they take 12.7k–23.5k skips per model, against 0 today. | Low | Calibrations must still pass and counters must be above zero for each model. The counters must be listed as excluded from the §2.2 comparison. |
| 1 | **L1** lookup acceleration | `rpc_mem_timing.c:152-164` lookup(). Tags are written at `:336`, `:367`, `:382` and in the bind memset `:197`. Host state must be reset before the `if(!clock) return` at `:200`. | A host-only way hint keyed by line, accepted only after checking `valid && va==line` and `hint<ways`, with the linear scan as fallback. Optionally add the M3 "bf" key mirror (a branch-free mask and ctz search) and re-check the tag on a key match. **No negative memo.** | This is the largest isolated result. **031, 8,000 frames, Mac: −14.9 % cycles, −11.9 % instructions, byte-identical.** Synthetic workloads with populated sets: −3.8 % to −29.6 % cycles. Code that hits way 0: +0.3 % to +0.9 %. The hot workload makes no lookups, so 0 %. | Low. It is outside arm.c, the hint is checked on every use, and it does not affect D-180. | A randomized hinted-against-linear test over all four geometries, or a self-check mode in the cross-check build. Mutants: drop the va compare, drop the valid check, drop the bound, skip a mirror update, fill the mirror with 0 instead of 0xFF. Amend E2 to "pure in canonical state" and add the hint to the §2.2 excluded list. Odin3 ABBA per model. |
| 2 | **S1** skip service while older bus work drains | `arc_rpc_mem_batch_service` `:634-689`. Store a visible(finish) memo at `:517` and `:588`. Move the h:149-151 flush into service. | Out-of-line skip. Do the integer flush first. Then, if `!pending`, `!video_active`, `cycle_active` and `now < due < min(finish_ns, boundary)` (strict), set now = due and copy issue_\* = due_\*. Also fold in M5's exact piece: skip the demand shortcut when the head command is not DEMAND. | Mac host user time −16 % to −25 % on bus-bound workloads (swrite SA-110, hotls and mixed ARM710). Cache-hot code is neutral. **The effect on 031 is unmeasured.** It is the largest lever for ARM710, the preferred CPU (D-164(2)). | Medium. A non-strict `<=` tie mutant survives all 4,677 intervals. It adds host-only `finish_ns` to `m`. It also needs a D-146 ruling admitting it (see 3.1). | A committed per-call differential (the B1_CHECK model). A unit case with due exactly equal to the finish time, a full buffer, and a ROMCR/CLKCTL write. Mutants: change `<` to `<=`, drop the `:588` memo update, drop `!pending`, drop `now<due`. D-180 re-run, because the inline header shrinks in all three loops. Odin3 ABBA on ARM710 stores and on 031. |
| 3 | **D1** one-mask gate | `arm.c:609-632` | Wrap the ARMv3/ARMv4 halfword pre-decode in `(op & 0x0e000090)==0x90`. The block inside stays unchanged. | About 10 host instructions fewer per guest instruction that passes its condition: −1.5 % to −4 % instructions retired. Estimated cycles: −1 % to −3 % cache-hot, about −0.5 % to −1 % on 031. | Very low. The gate is a necessary condition, shown by enumerating all bit combinations. | The standard gates. Coverage of the halfword encodings is thin (`rpc_cpu_test.c:145-149` only), so add v3-undefined and v4 LDRH/STRH/LDRSB/LDRSH cases on every model. |
| 4 | **D2** AL fast path (bundle with D1) | `arm.c:608` | `(op>>28)==0xE ||` placed before the flaglookup test. | D1+D2 measured cycles: hot −1.8 %/−2.8 %, ALU −6.8 %/−4.4 %, mixhot −5.5 %/−3.8 %, mixmem about 0 to −1.8 %, ARM710 +0.6 %. D2 accounts for about 40 % of the instruction saving. | Low, but it currently **breaks** `rpc-observation-mutants`: the anchor at `tests/rpc_observer_mutants.py:26` no longer matches. An LE-always mutant survives the entire rpc suite. | Port the "condition" anchor. Add a condition-table unit test covering 16 conditions × 16 NZCV values × {26-bit, 32-bit}. Add `al-nv` and `al-le` mutants and show they are killed. |
| 5 | **A1** data-access hit path, one design | `mem.c:615-647`, `cp15.c:380-445`, `rpc_mem_timing.c:265-364` | (a) Split checked_translation into one shared inline hit-and-permit check (live DACR, ctrl&0x300, memmode, rw) plus the existing outlined walk and fault. The accessors then take physical, attributes and `backed = entry->code != NULL` from the entry. MMU-off is unchanged, and the abort test stays `event & 0x40` before any charge. (b) Split arc_rpc_mem_access into a lean D-hit path plus one internal body that knows the lookup missed. fetch_slow calls that body directly, which takes over F4 without its double scan. | 031: −3.5 % host instructions (M2). The estimate is about 2–4 % cycles on 031 and 0 % on fetch- or ALU-bound code. ARM710 gains 0 % or slightly loses. | Medium-high. **The warm-hit data permission path is not covered today:** a mutant that bypasses the domain check on AP3 survived 116 tests. Exposing static cp15 state also costs namespace and header work. | Fable picks one of three shapes: (1) a header exposing the TLB state, (2) a translate-and-access function in cp15.c, or (3) translateaddress returns the entry. Tests: a warm-hit permission matrix through all four accessors plus LDRT/STRT, primed by a denied install; STRH where the first byte aborts; data-hit `stats.hits` checks; an `arc_rpc_attributes` check after each access; a lifecycle assertion that `entry->code` is non-NULL exactly when `phys_page` is. Mutants: always-allow, ignore memmode, ignore rw, ignore domain, missing return (double charge), backed forced true, attributes not stored, ++hits dropped. |
| 6 | **F1** inline fetch TLB hit | `arm.c:595` → `cp15.c:447-458` | Call A1's shared hit-and-permit check inline in the loop. Miss, deny, a pending 0x40 or MMU-off all fall back to the unchanged getpccache. **Live check, no F3 table.** | Isolated: hot −0.5 % to −0.7 % cycles, ALU −8.6 % to −8.9 %, far −5.6 %. 031 about 1–3 %. Caveat: the memfetch A/B saw only 0–3 % from inlining alone, and Windows GCC already inlines checked_translation, so the gain depends on the toolchain. | Medium. It lands in all three loops, so code size grows and the D-180 ratio rises (+1.95 % could become about +2.1 %). It adds an extern TLB symbol. | A loop-level comparison of the fast path against a forced-slow path on a permission-matrix ROM: user and SVC, AP 0-3, S/R 0-3, DACR none/client/manager, DACR or S/R changes between fetches, a pending 0x40, and a data access (including a denied one) that evicts the fetch slot. Today's unit tests call getpccache directly and so test only the fallback. Measure with both NDK clang and MinGW GCC. |
| 7 | **B3** closed-form cost for memory line fills | demand shortcut `:642-678` | For `fill && memory_region` packets, compute the total cost in one step: SA 22q, unified 11q, wide model-3 19q. Every word is still derived from one shared memory-word cost rule. ROM fills and other packets keep the per-word cycle(). | Small and unmeasured on the target. It only applies to fills that commit in the shortcut, which excludes fills preceded by a castout and ROM fills. | Low-medium, because the cycle() rules would exist in two places. | A shared helper, or a permanent cross-check. The calibrations exercise it (24,580 packets checked in the shadow build) but the goldens barely do (10 packets). Mutants: drop the SA (addr&15) rule, 22→21, wide 4→2, wide 7→5. |
| 8 | **B2** skip in the resumable loop, plus the commands() guard | `:681-688`, `:562`, `:598` | Call the S1 skip after each `ready()==false`. Call commands() only when `count \|\| pending`. | Marginal. One mix-sa110 run was +2.5 % slower, probably layout noise. | Low, but it depends on S1. | Land the guard with S1. Keep the loop skip only if the Odin3 shows a gain. |
| 9 | **B4** incremental scheduler minimum | `rpc_scheduler.c:33-41` | Compare the new deadline with the cached minimum, and rescan only when re-arming the minimum. | Tiny: the driver is about 1–2 % at quota 200. | Low, but stale-cache states exist after `fatal()` (`rpc_session.c:114`) and in tests that zero-initialise the scheduler. | Also rescan when the cached event is not armed. Add tests for the stale-cache cases. Bundle it with other work or drop it. |

**Ordering note.** A1 should land before F1, because F1 reuses A1's shared check. L1 and S1 are independent of each other and do not touch arm.c.

### Ideas from the area maps that nobody has vetted yet

These are worth a vetting round before the work order is frozen:
- **Rational continuation table (ARM710/ARM7500).** A table of ns steps indexed by fraction, used per instruction (`rpc_mem_timing.h:121-125`). ARM710 at rate 35 never uses integer batches, and it is the default CPU, so this may matter more than anything above for most titles.
- **Integer-path tidy-ups.** Move the stale overflow test at h:107 out of integer mode, merge end and continuation, and fix recording/track_rom per phase.
- **One slow flag.** Fold the test_\* checks into a single flag.
- **Quantum per bind.** Precompute `875*rate` per bind and keep the live `clkctrl&2` read. This is the exact form of M5's quantum idea.

---

## 2. What 6b-P0i would confirm or re-rank

6b-P0i already provides: the cost split (fetch translation, dispatch, timing model, bus/MMIO service, devices), block lengths, class mix, a code-churn proxy, block coverage with exit causes, and cold-start against sustained frames. Several rankings also need counters P0i does not have. Those are marked **(add)**. Fable should either fold them into P0i before it merges or run them as a small follow-on.

| Candidate | P0i measurement that decides it | Re-rank rule |
|---|---|---|
| L1 | Timing-model share of the cost split on 031 and the 3D title. Load/store share of the class mix. **(add)** lookups per instruction, hit/miss split, mean probes per hit and the way index of hits, per model. | Stays first if lookups take more than about 10 % of sustained 031 cost and hits average more than 4 probes. If hits are mostly way 0, it drops below A1. |
| S1 / B2 | Bus/MMIO service share. Exit causes for service and pending. **(add)** batch_service entries split into pending / `bus_busy && !pending` / skip-eligible / `due >= finish` / video_active, per model. | Rank first if skip-eligible entries per guest instruction on 031 or ARM710 titles are more than a few percent. Rank B2 separately by its own loop-site count. |
| D1/D2 | Dispatch share. Class mix. **(add)** condition-pass rate, AL share, share of halfword encodings. | D1 is worth taking whatever the numbers, because it is nearly free. D2's value scales with the AL share. Neither should be ranked above L1, S1 or A1 unless dispatch is more than 30 % of sustained cost. |
| A1 | Class mix: LDR/STR/LDM/STM per instruction and LDM/STM word counts. MMU against timing in the cost split. **(add)** data TLB walk rate. | Promote above S1 if 031 makes more than about 0.5 data accesses per instruction and MMU plus timing-hit cost dominates. |
| F1 | Fetch-translation share in the cost split. Block length, as a proxy for how often the memo line changes. **(add)** fetch TLB walks per instruction. | Promote above A1 if fetch translation stays at about a third of sustained 031 cost, as it does in the cache-hot Phase 0 figures. Demote it if gameplay is stall-bound, as AC-10 suggests. |
| B3 | **(add)** fill packets per instruction, shortcut commit rate, and ROM against memory fills. | Drop it if shortcut-committed memory fills are under about 1 per 100 instructions. |
| B4 / quota | Block and batch length distribution, and the share of exits caused by deadlines. | Short batches make driver cost visible, which raises B4. That also opens Q-7 quota work, which belongs outside 1a. |
| F3 / R1 (deferred) | **(add)** whether executed code pages are in a manager or client domain (DACR), and the rate at which data accesses evict the fetch slot. | Revisit only if RISC OS or 031 code runs in client domains. In manager domains F3 adds about 2 host instructions per access, so it cannot pay. |
| R2 (deferred) | LDM/STM share and 1 KiB tag crossings. | Revisit after A1 if LDM/STM is more than about 10 % of 031. |
| M4/B5 (rejected) | **(add)** per model, integer against rational continuation share, and the length of request-free runs after service. | Reopen only if long request-free runs after service on SA-110 are common, and then only after a §2.2 ruling. |
| Phase 1a against the block foundation | Block-coverage share and exit-cause mix. Cold-start against sustained frames. | If the projected Phase 1a total on sustained 031 is under about 25 %, set the expectation that Phase 1a cannot meet AC-10 and that the foundation follows. Code churn does not rank any 1a item. It only confirms that stores must keep funnelling through mem_phys_write\* for the G-9 hook. |

P0i's pinned baseline must be measured before any 1a slice lands. 1a slices that edit arm.c must stack after P0i, because P0i's compile-time hooks sit in the same loop.

---

## 3. Draft work order: 6b Phase 1a, exact interpreter optimisations

**Stage:** PLANNING (Fable). **Owner:** Fable until the rulings in 3.1 are recorded and P0i has reported. **Next:** an Astra challenge on this draft, then the rulings, then Sol on slice 1a-0.

### 3.1 Rulings Fable must record before dispatch

1. **Ruling R-a (D-146 scope).** Admit timing-service exact items (L1, S1, A1(b), B3, B2) into Phase 1a beside the fetch-path items, and fix their order. D-146 currently says Phase 1 *starts* with exact reduction of fetch-path overhead.
2. **Ruling R-b (classification).** A single shared inline hit-and-permit check, evaluated on live DACR, ctrl, memmode and rw for every access, counts as "keeps every per-fetch check". It is not G-4 or G-7 reuse. No precomputed verdict table (F3), no same-tag reuse (R1) and no in-instruction reuse (R2) are allowed in 1a. A1(b) is a split of the code path, not a G-7 filter.
3. **Ruling R-c (contract amendments).**
   - §2.2 lists these as host-only derived state, excluded from comparison and reset at every G-13 point: the lookup hint and key mirror, `finish_ns`, and the exercise counters.
   - E2 says lookup() is "pure in guest/canonical state".
   - Refresh the stale citations: invariants §1.1 (for example arm.c:571→:592 and :574→:595), the G-4 coverage row (`:436`, partly stale) and the G-8 row (`:440`; `rpc_mem_timing_unit.c:126` does write CLKCTL mid-drain).
   - Re-record D5(b) as "guest-exact, deferred for low value".
4. **Ruling R-d (sequencing).** Dispatch only after P0i reports and the Section 2 re-rank is done. Q1 does not block exact 1a work, because 1a is useful under any answer to Q1.

### 3.2 Objective and governing requirements

Reduce host cost per guest instruction on the RPC interpreter without changing guest behaviour, timing or canonical state.

Governing decisions:
- D-142(1): the frame gate.
- D-142(2) and D-146: exact Phase 1 and permission rules.
- D-163 AC-10: game 031 is at about 51 MIPS, 1.46 T p50.
- D-180: the observed path costs at most 3 % more than the unobserved path.
- D-181 and D-182: Odin3 measurements use serial ABBA runs.

Phase 1a narrows the gap to AC-10. It does not close it.

### 3.3 Scope

Each slice is its own PR, gated and measured on its own.

- **1a-0 (E0).** The narrowed TEST_FAULTS gate, the exercise counters, the cross-check build option, a shipping-flag measurement core, and the documented Odin3 command.
- **1a-1 (D1 + D2).** Pre-decode gate and AL fast path, with the condition-table and halfword tests.
- **1a-2 (L1).** Lookup hint, with an optional key mirror gated by geometry, the gate set from Odin3 data.
- **1a-3 (S1, the commands() guard, optionally B3).** Bus-busy skip and the demand-shortcut tweak.
- **1a-4 (A1).** Data-access hit path in the shape Fable chose, with F4 folded in.
- **1a-5 (F1).** Inline fetch hit using A1's shared check.
- **Optional (B2, B4).** Only if the Odin3 shows a gain.

### 3.4 Non-goals

- The translator or block foundation.
- Permission or translation reuse (F3, R1, R2).
- Changes to how integer batches are represented (M4, B5).
- Changes to quota, rate or batch size.
- Lazy NZCV and threaded dispatch.
- A host-pointer data cache or a data-side permission filter.
- Widening the fetch memo or changing its shape.
- Lazy folding of device events.
- Rational-continuation work (it needs its own vetting).
- Any change to shipping defaults or the libretro surface.

### 3.5 Constraints

- **Per-instruction order is fixed:** begin, fetch with translation and permission on every fetch, opcode record, condition, body, event check (including the 26-bit reg[16] refresh, priority abort > FIQ > IRQ), raw +4, instruction_end, continuation. A prefetch abort skips the event check. A failed condition still pays fetch, begin and end.
- **Shared TLB semantics.** Fill before the permission check. tlbs++ on every walk. arc_rpc_attributes is stored on every translation. No speculative walks (Q-10). A data access that evicts the fetch slot forces a real re-walk (G-4).
- **Counters.** Memo eligibility (Q-3) is unchanged. m.stats.hits and fetch_hits are each identical. Victim cursors, populated and dirty state are unchanged.
- **Time (G-1, G-8, G-11).** now_ns is current before any device, MMIO or observer call. next_ns is re-read after anything that can schedule. Events, then service, then CPU at equal ns, with strict `<`. No batch_flush or next_base where the oracle has none. Overflow fatals fire at the same instruction.
- **Three instantiations.** All three stay equivalent. Any new check belongs out of line unless it is in the shared body. Growth in hot-loop code size is a cost: an inlined prototype cost +5 % to +10 %.
- **No compiler dependence.** Correctness never depends on compiler-specific inlining. Gains are reported per toolchain: NDK clang, MinGW GCC and Apple clang.
- **Cores.** Every measurement names its core: the plan core with TEST_FAULTS, or the shipping-shaped core.

### 3.6 Acceptance criteria

- **AC-1. Calibrations.** rpc-memory-timing-0..3 (4,677 intervals) and all 25 `tests/fixtures/rpc-*.trace` goldens are byte-identical on host, ASan/UBSan, native MinGW and Android builds. The independent goldens are never regenerated.
- **AC-2. Exercise proof.** Every fast path has a committed exercise counter, above zero for each model in the calibrations, goldens and observation plan where it applies. It also has a cross-check build, separate from TEST_FAULTS, that compares the fast result with the reference on every call and reports zero mismatches over the full `ctest -R rpc` in CI. This covers L1, S1, B3, A1 and F1.
- **AC-3. Parity paths.** `rpc-hot-throughput` passes the `|10·instr − ns| ≤ 9` check. `rpc-observation-plan` gives projection parity at quotas 1, 7 and 200 under off, unmatched, trace and active, with `integer_attempts > 0`. A hand-run comparison of the pinned baseline against the candidate projection (argv[6]) is recorded per slice.
- **AC-4. Equivalence.** The three instantiations stay equivalent: `interpreter_equivalence`, `cheap_events` and `cheap_fault` pass, and so does the raw snapshot memcmp (`rpc_observer_test.c:348-356`).
- **AC-5. Canonical state.** No field compared under §2.2 changes. Every new host-only field is listed as excluded under R-c and dropped at reset, stop, failed start, rate rebind and engine switch.
- **AC-6. Mutants.** The existing suites (rpc-qa-mutations, rpc-observation-mutants, rpc-observers-mutants) pass with their anchors ported, under the Ninja generator. Every new mutant in Section 1's verification column is committed and shown killed.
- **AC-7. Directed tests:**
  - the condition table: 16 × 16 × 2 modes;
  - v3/v4 halfword encodings on all four models;
  - warm-hit permission matrices through fetch and all four data accessors, including LDRT/STRT;
  - STRH with a first-byte abort;
  - data-hit stats;
  - a data access (including a denied one) evicting the fetch slot;
  - the S1 tie case where due equals the finish time;
  - a randomized lookup test against the reference across all four geometries;
  - for B3, all four models with DRAMWID;
  - if B4 lands, the scheduler stale-cache cases.
- **AC-8. D-180.** On the final tree of each slice that touches the shared loop or its inline header, the Odin3 observed/unobserved ratio stays at or below 3 % at p50 and p95. Measure with `tools/rpc_observer_cost.py` or paired ABBA runs, serially (D-182).
- **AC-9. Odin3 R-H.** Serial ABBA runs (D-181 style), pinned baseline against candidate, per slice. Workloads: hot, warm load/store, streaming writes on SA-110 and ARM710, LDM/STM8, 031 under the AC-10 recipe (frames 6400–7400), and one 3D title. Report p50, p95, p99, guest MIPS and ns per instruction, on both cores.
  - A slice is dropped if it shows no gain beyond the ABBA noise band on any target workload.
  - No workload may regress beyond that band.
- **AC-10. Frame gate (D-142(1)).** The runner's budget line (p95 ≤ 0.80 T, max ≤ T) is reported for hot and for 031. The remaining distance to AC-10 is recorded. Closing AC-10 is not claimed.
- **AC-11. Cross-platform.** Runner traces and guest instruction counts for the 031 recipe are byte-identical between the Odin3 and Windows MinGW. Windows timing is reported.
- **AC-12. Provenance.** Every edit under `engine/rpcemu/` carries a dated "ArcDroid modification" notice.
  - The UPSTREAM.md and manifest.json patch lineage must satisfy `check-rpcemu-import.py:176-183`.
  - Any new header goes in `PROJECT_HEADERS`.
  - New externs carry the `arc_rpc_` prefix in `rpc_namespace.h` and `backend-symbols.json`.
  - rpc-import-guard, rpc-include-closure and rpc-symbol-guard pass.
  - No ROMs, private titles or traces are committed.
- **AC-13. Fast-path invariants are documented.** Each one is stated in a comment and in the invariants: "`!pending` implies `count==0`", "entry->code lifetime is tied to the cp15_reset flush", "tag writers update the mirror", and "fetch helpers never write R15 or r15_mask" if it ever applies.

### 3.7 QA focus

- The tie and ordering rules of G-8 in S1.
- The warm-hit permission matrix and STRH aborts in A1 and F1.
- Mirror consistency at every place a tag is written in L1.
- Code size and the D-180 ratio in the shared loop.
- Exercise counters that prove each fast path actually ran.

---

## 4. Rejected or deferred, one line each

- **D3 (PC kept in a local).** Exact, but it saves about one host instruction net, grows the cheap loop by 12 % and doubles its spills, and E3 would need a narrow invariant. Rejected.
- **D4 (force-inline the shift and flag helpers).** Gains at most 0.3 % of instructions even on shift-dense code, adds 2.4–12 KB to each of the three loops, and the prototype workloads never ran shift3. Revisit only if an Odin3 or Windows profile shows the helpers above 2 %.
- **D5a (threaded dispatch).** Unproven, duplicates the whole tail in three loops against the D-180 limit, and gains at most 2 % on cache-hot code. Rejected.
- **D5b (pccache2 as a local).** Guest-exact, deferred for low value (D-195). It needs a §2.2 projection change and saves one store per instruction; defer it to the block-foundation contract.
- **D5c (lazy NZCV).** Not exact as an interpreter change: the flags live in reg[15] in 26-bit modes, the raw +4 can carry into V (Q-6), and capture_event reads the flags at G-12 barriers. This is translator work.
- **D1 extra (`unreachable` default).** It becomes undefined behaviour if a case label is ever removed, and it is unmeasured. Not in 1a.
- **F3 (permission table).** Exact, but it is a precomputed verdict needing classification, and it costs about 2 extra instructions per access in manager domains, which is every perf fixture. Revisit only on P0i domain data.
- **F4 as a standalone change.** It doubles the lookup on a miss (+23 % cycles on big12k-arm610). Replaced by A1's single access body.
- **F6 (memo eligibility stored per TLB entry).** The region term is always true for live installs, it breaks the Q3 mutants, and its roughly 1 % gain cannot be measured.
- **R1 (same-tag fetch reuse with a generation).** Permission reuse that needs a D-146 ruling. It is a net loss in manager domains and about 1 % in client domains.
- **R2 (translation reuse within one LDM/STM).** Needs a ruling. The ceiling is about 7 % on the LDM8 row, and about 0.3–1 % once A1 lands.
- **M3 negative memo.** Reuses a lookup result and needs a G-7 classification. It adds host instructions, and the hint and mirror already do the job.
- **M4 / B5 (keeping or re-admitting integer mode across bus-busy continuation).** Not exact under §2.2, because integer_batch is a compared field. It adds batch_flush and next_base calls (G-11) and gains at most about 1 %.
- **M5 quantum cache.** Not exact as specified. iomd_reset runs after bind, tests write clkctrl directly, and unit test `:126` would fail. The exact alternative is to precompute `875*rate` per bind.
- **M5 active packet by index.** Needs a §2.2 amendment, and only one of the two 128-byte copies can go. Deferred.
- **M5 region per lane.** Exact, but it adds a field to the Packet layout, which §2.2 compares. Deferred.
- **M5 visible(finish) cache.** Duplicates S1's `finish_ns`.
- **Wider or multi-line fetch memo.** Not exact, because it moves counts between the hits and fetch_hits fields.
- **Lazy device-event folding (48 kHz sound, VIDC lines).** Changes canonical scheduler state, and devices cost only about 0.04 T.
- **Larger quota.** Needs the Q-7 proof and changes three coupled constants (`arm.c:571`, `rpc_mem_timing.h:86`, `rpc_session.c:396`). Not 1a.
- **Host-pointer data cache or data permission filter.** Reopens the R1 "no host-pointer cache" rule (`mem.c:613-614`) and needs its own ruling.

The scratch evidence cited above is in `/private/tmp/claude-501/arcdroid-p6b-*/` and the builds in `.work/s64/review/r3d/build/p6b-*/`. The key sources are `.work/s64/review/r3d/docs/riscpc-translator-invariants.md`, `.work/s64/review/r3d/docs/handoff.md` (lines 219–252) and `.work/s64/review/r3d/engine/rpc/rpc_mem_timing.c`.

---

# Astra stage 1 (S65, ACCEPT WITH CHANGES): Fable's adjudication

Report `.work/s65/6b/report1.md`. All seven findings incorporated; the reconciled work order must carry them.
1. **A1/F1 attributes.** `arc_rpc_attributes` updates only on successful translation-and-permission; fault paths keep the previous value. Translation stays before the pending-abort (`event & 0x40`) check. Tests: stale attributes across a denied access, FAR/FSR, denied installs, pending-abort accesses, DACR value 2 as well as none/client/manager. R-b stands with this correction.
2. **E0 reconciled to this head.** The narrowed stop condition already exists; drop that edit. E0 adds the endpoint-measurement mode the invariants require (non-intrusive: integer batching and fast begin/end stay live), preserving exact ordinals, drain and stop semantics, covering integer-to-service transitions and rational continuation; the existing calibration mode stays as an independent check.
3. **Equivalence oracle.** AC-2 names the reference (the retained pre-optimisation path), the compared §2.2 fields (including returned continuation decisions, faults, issue times and separate hit counts) and the isolation method; pointers normalised, only accepted exclusions omitted. A shared helper is never its own sole oracle. A1/F1 fallbacks never repeat a descriptor walk or functional MMIO against live state; use isolated replay. The inspector raw-memcmp test stays unchanged.
4. **S1 boundaries.** Directed comparisons for `due == next_ns`, `due == limit` (with a tied video event), distinct rational ticks sharing one visible nanosecond, an integer batch entering S1 (flush before test/return), `count == 0 && pending`, queued video, and a timing-register write before the next word. Mutants: omitted flush, floor-versus-ceiling finish, boundary `<=`, count-only commands guard. B3 stays a separate slice and must also preserve `rom_sequential`, `restart`, word counts, commit/rollback and the near-overflow fallback, with side-effect and rollback mutants.
5. **B4** (if ever taken): lexicographic (deadline, event) minimum, absent-minimum handling, rescan when the minimum moves later or is stale; keep the reverse-arm and maximum-deadline tests; tie and sentinel mutants.
6. **Baseline and sequencing.** Every production slice starts from the reconciled, merged R3c-2 plus 6b-P0i baseline (P0i rebased after R3c-2 merges) and compares against both its immediate predecessor and the cumulative baseline. Order: E0 → L1/S1 (order set by E0's counters) → A1 → F1; D1/D2 a small independent slice; B3/B2 separately justified; B4 dropped unless measured benefit warrants it. D-180 holds on the integrated tree as 3 % total, rechecked after any change to shared execution cost, held-watch cost included. Starting Phase 1a implementation before R3 completes is recorded as a D-182 scheduling decision (D-163's order otherwise holds). D-181's recipe is cited from `main`.
7. **Benefit rule frozen before measuring.** Workload inputs, initial persistent state, observer configuration, measured windows, device settings, repetitions and the noise/retest rule are fixed in the work order; the unprofiled shipping-shaped core decides benefit; a regression criterion also applies on native Windows. A1 is ranked by executed-access counts (P0i class counts include condition-failed instructions). The 20–25 % aggregate is a hypothesis until cumulative measurements support it.
- **Rational continuation table:** vet now (exactness of every canonical time field, bus-carried fractions, overflow, rebind lifecycle); not admitted to 1a yet.

# Rational continuation vetting (S66): Fable's ruling

Read-only vetting at `1f4bbae` with scratch prototypes on a loaded Mac (`.work/s66/rct/RCT-VETTING.md`; numbers are estimates).
- A table that still writes every timing field per instruction is exact but costs about 2.9 more host instructions per guest instruction: **rejected**.
- A **lazy rational batch** (the integer-batch scheme extended to rational bases, with a table step indexed by fraction, built per rate, model and SA clock at bind and selected in `arc_rpc_mem_control`) matched the reference in 7,200 prototype cases and fired the overflow fatal at the same instruction in 576 edge cases. It needs a separate host-only lazy flag (not `integer_batch`, a compared field), exactly the integer flush sites, no re-admission after service, the integer step kept for integral bases, a widened P0 projection rule and a D-180 re-run. Two prototype mutants survived (dropped alignment check; omitted flush before bus-busy service) and need white-box cases.
- Benefit: about 72 instead of 84 host instructions per guest instruction on request-free ARM710 code, falling with request density; estimated 0.5–3 % on ARM710 titles; nothing on 031 (SA-110 on the integer path).
- **Ruling:** not admitted to Phase 1a. It belongs to Track B regime B-2 (`docs/riscpc-6b-plan.md`), with the exclusions above. It returns as a 1a slice after S1 only if 1a-0 or 6b-P0j counters show at least about 30 % request-free-prefix share on an ARM710 workload, or if the Founder's SA rate (F-g) does not divide 1000, which would put SA-110 on the rational path.

## 1a-0 current code anchors

The draft table retains its explicitly labelled `7b11b47` prototype locations
and results. For follow-on work use the current definitions below, refreshed
with 1a-0; no fast path is implemented here.

| Candidate | Current reference path |
|---|---|
| L1 | `engine/rpc/rpc_mem_timing.c:293` (lookup), `engine/rpc/rpc_mem_timing.c:518` (access caller), `engine/rpc/rpc_mem_timing.c:604` (maintenance caller) |
| S1 / B2 | `engine/rpc/rpc_mem_timing.c:894` (service), `engine/rpc/rpc_mem_timing.c:768` (commands), `engine/rpc/rpc_mem_timing.c:802` (resumable run) |
| B3 | `engine/rpc/rpc_mem_timing.c:660`, `engine/rpc/rpc_mem_timing.c:943`, `engine/rpc/rpc_mem_timing.c:964` |
| A1 / F1 | `engine/rpcemu/src/cp15.c:390`, `engine/rpcemu/src/cp15.c:471` |
| A1 accesses | `engine/rpcemu/src/mem.c:704`, `engine/rpcemu/src/mem.c:722` |
| D1 / D2 | `engine/rpcemu/src/arm.c:619`, `engine/rpcemu/src/arm.c:620` |
