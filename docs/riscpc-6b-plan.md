<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Item 6b plan after Phase 1a (D-188, D-189)

Status: **adopted (D-191)**, S65, after Astra stage 1 (ACCEPT WITH CHANGES, all eleven findings incorporated at the end). Each slice gets its own work order and Astra stage 2 before implementation. Fable's adjudication of the S65 6b design workflow (read-only research over `1bb22aa`: four area maps, three angled drafts, three judges, a synthesis and a completeness critic; `.work/s65/review/WF-6b-phase1b-design.md`). The synthesis is adopted as the starting plan. Every amendment from the completeness critic is adopted (section **Amendments (incorporated)** at the end) and governs where it differs from the body. Founder decisions since the synthesis: D-188 (design for about 200 MIPS capacity) and **D-189 (6b production work starts now, in parallel with R3)**, which settles the critic's amendment 2. The synthesis's section 0 list of Founder items is superseded by the section **Founder items (Fable's adjudication)** below.

## Founder items (Fable's adjudication)
- **F-a Windows RetroArch evidence:** not a Founder item (critic amendment 3). CI's `windows-retroarch` job already downloads the pinned RetroArch 1.22.2 and loads the core; probe A0 runs there or from the same portable archive on the Windows box. Whether a Smart App Control-on machine is available is an observation for item 5, not a blocker.
- **F-b second Android device (D-135):** asked when probe A1 is ready; parks only Android-breadth claims.
- **F-c Q3 (fork copying):** Fable recommends reference only; encoders are written from scratch. Asked only if a copied fragment is ever wanted.
- **F-d which milestone R4 waits for:** proposed G1 (the 100 MIPS frame gate on 031); asked before G1 is reached.
- **F-e paid arm64 CI:** only if A2 rejects the free route; asked then.
- **F-f Q4 (default on/off, kill-switch wording):** at qualification.
- **F-g C-track decisions** (production SA rate, any timing-contract change, what "near-hardware speed" means for D-163, Auto CPU preference): asked with the C-track evidence pack.
- **D-189** answered the reorder question: production Phase 1a, C-1 and B-0 may start before R3 completes.

---

# Item 6b after Phase 1a under D-188: final synthesized plan

Clone: `.work/s65/6b/clone` at `1bb22aa`, read-only. Nothing was built, run or edited. Labels: **[C]** means I re-checked it in code or docs in this run. **[M]** means a recorded measurement in the canonical docs. **[E]** is an estimate (a research target, not a gate). **[U]** means I could not verify it inside the clone.

## 0. What is waiting on the Founder (the relayed question)

The relayed status, "the only thing waiting on you is Q1", is out of date. **Q1 is answered**: D-188, merged on `main` as `336d787` (PR #420); `D-188.md:1`.

Nothing in this plan blocks Phase 1a or the portable foundation on the Founder. Under this plan, the Founder items are:

| # | Item | When it is needed | Blocks |
|---|---|---|---|
| F-a | **Install RetroArch on the Windows box.** It is not installed (TRANSLATOR-MAIN.md:208, as cited by the drafts). Also say whether a machine or VM with Smart App Control on is available. | Now, for probe A0 | Only A0 (Windows-inside-RetroArch evidence). If it is declined, that evidence arrives only at X-Q. |
| F-b | **Is the second Android device (D-135) available?** | Now, for probe A1 | Android-breadth claims only |
| F-c | **Q3, copying from the fork.** Recommendation: reference only. The encoders are written from scratch in this plan, so Q3 blocks nothing unless copying is wanted. D-188 says it must be answered "before any copied helper" (`D-188.md:1`). | Any time before a copied fragment | Nothing under from-scratch |
| F-d | **Which milestone R4 waits for.** Proposal: G1, the 100 MIPS frame gate on 031 (§3). | Before G1 is reached | R4 start |
| F-e | **Paid arm64 CI**, only if A2 chooses it. The repo is PRIVATE [C: `gh repo view` → `sayenah/ArcDroid`, PRIVATE]. | After A2 | X-1b merge, if qemu-user is rejected |
| F-f | **Q4: default on or off, and the kill-switch wording.** | At B-Q and at X-Q | Shipping the default |
| F-g | **The C-track decisions** (§5): production SA rate, any timing-contract change, what "near-hardware speed" means (D-163), and the Auto CPU preference (D-164(2)). Also whether reference hardware or recordings exist. | After the C-0, C-2 and C-3 evidence | Phase 3 |

## 1. Summary

**Base and grafts.** The base is the platform-risk-first plan (two of three judges preferred it). It is restructured with the exactness-first plan's one-cause-per-slice discipline, and takes the performance-first plan's budget model and levers.

**Five tracks:**

- **A, platform probes now.** Scratch only, written from scratch, run in parallel with R3 and Phase 1a. They retire the risks that could sink D-188(2) before anything else is built on top of them: executable memory and W^X inside the real RetroArch builds, the D-155 non-SEH unwind through emitted frames, Android `execmem` and 16 KiB pages, env 83, and arm64 CI.
- **M, measurement and harness.** Test-only. It closes Phase 0's open items and records the guest-time ledger. The harness is the two-process lockstep differential and an independent reference (Astra finding 3).
- **B, portable C block foundation.** This is D-142's option (c): an IR interpreter and the executable specification. It is built in **one-cause slices**: B-0 skeleton with hooks unchanged, B-1 fetch fold, B-2 base-step regime, B-3 bus window, B-4 chaining, B-5 permission reuse, B-6 cheap observers, B-7 bus fast paths. Each lever has its own ruling, its own slice and its own mutants. Milestone **G1** is the 100 MIPS gate.
- **X, native emitters (arm64 and x64).** Thin emitters over the same IR. X-0 is the dormant exec-memory layer. X-1 is a call-threaded skeleton that exposes only platform mechanics. X-2 and X-3 then add the inline ALU/flags/registers and the hit paths, **for both emitters together**. Milestone **G2** is about 200 MIPS capacity at test rate 200.
- **C, cycle-timing review.** Its own deterministic track, never in the same slice as a speed change or the rate flip.

**Phase 1a proceeds unchanged**, as D-188(1) requires (`D-188.md:1`), in Astra's adjudicated shape (`ASTRA-PHASE1A-1.md`, "Recommended shape"):

1. E0;
2. L1 and S1, in measured order;
3. A1;
4. F1.

D1 and D2 form a small independent slice. B3 and B2 go ahead only on evidence, and B4 is dropped unless it shows a benefit. The rational continuation table is vetted, not admitted. The foundation reuses L1's hint, S1's `finish_ns`, A1's access body and F1's shared hit-and-permit check. It trims nothing from 1a.

**Gains, honestly framed.**

- Today, 031 sustained racing costs 28.5 host ns/instr on the Odin3 [M].
- The 100 MIPS gate needs about 13.4 ns.
- A 200 ceiling needs about 10 ns on 031 if 031 is stall-bound [E/U].
- Only about 3.3–4 ns is needed, and only for code that actually executes at the ceiling: cache-hot loops, polling and spinning.
- The foundation alone lands at about 13–17 ns [E], borderline for G1.
- Native code brings 031 to about 5–8 ns and cache-hot code to about 1–3 ns [E].
- Under the current timing contract, **no rate gives 031 "near-hardware speed"** if it is stall-bound. That makes the C track part of D-163's critical path.

## 2. Verified premises and corrections

**Corrections to the drafts and maps.**

- **Bus service share.** It is **16 %** on the Odin3 (6–9 % on the Mac) for the whole 031 run (`TRANSLATOR-MAIN.md:215`; `PHASE1A-DRAFT.md:7` gives 16.5 %).
  - The draft figure of "25 % in sustained racing" is the timing map's recomputation from private JSON outside the clone [U]. It matches the Mac's warm load/store MMU column (`TRANSLATOR-MAIN.md:145`).
  - The ledger in this plan therefore uses **timing ≈ 14.5, fetch ≈ 4.6, dispatch ≈ 4.6, bus ≈ 4.7 ns** of 28.5.
  - "Bus 7.3 ns" and "bus_calls 0.644" are not used. The documented figure is about **0.38 bus calls per instruction** over the whole run (`PHASE1A-DRAFT.md:8`). The racing-window figures (50.6 MIPS executed, 9.76 ns stall, 0.644) are [U] until M1 reproduces them.
- **The 1,666,667 backstop is at `rpc_session.c:413-415`** [C]: line 413 is the fatal, 414 the quota and 415 the 200 cap. One judge's "415-416" is wrong.
- **Cache-hit handling** is at `rpc_mem_timing.c:356-364` [C], not `:329-336`.
  - A hit increments `stats.hits`.
  - A fetch hit calls `memo_set`.
  - A read hit returns with no request.
  - **Only on SA-110 (model 2)** does a store hit set a dirty half and return (`:363`). On ARM610/710/7500 a store hit falls through to the buffered-write path, which creates a request.
  - So a "regime" survives cache-hit loads on every model, but store hits only on SA.
- **Burst-ROM tracking is per fetch, not only when `rom_sequential` is true.**
  - `arc_rpc_mem_access` sets `track_rom`, `after_branch=false` and `fetch_pc` for any fetch from a burst-ROM region (`(r ? romcr1 : romcr0) & 24`, `rpc_mem_timing.c:339-344`) [C].
  - `arc_rpc_mem_fetch` writes `fetch_pc` on every fetch while `track_rom` is set (`rpc_mem_timing.h:132`) [C].
  - Blocks must therefore exclude **code in a burst-ROM region** as well as any entry with `rom_sequential == true`.
- **The SA empty-data-tag shortcut** (`rpc_mem_timing.c:330-337`) appends DEMAND and calls `uncached_clock()` on reads. That can rebase `base_*` (`:294-300`, `:221-234`) [C]. Re-read `base_*` after any access helper.
- **Event check.**
  - The check runs whenever `arm.event != 0`.
  - In 26-bit modes it first refreshes `reg[16]` from R15's I/F bits (`arm.c:1933-1937`) [C], then takes ABORT/FIQ/IRQ (`:1939-1949`).
  - Plan rule: a block is admitted only with `arm.event == 0` at entry, and the rule is re-checked after any helper. Otherwise the interpreter runs that instruction.
  - That `arm.event` is constant inside a helper-free batch is [I/U]. M1 must count P0s with `event != 0`.
- **Q-3 is already fixed** (PR #375, `44d007e`, an ancestor of HEAD [C]; invariants :218, :456). `TRANSLATOR-MAIN.md:226` ("still open: the Q-3 oracle fix") is stale. No slice is gated on it.
- **ROM has no runtime writer** [C]. The only writes to `romb` are the loaders (`mem.c:74`, `:138`), and RAM/VRAM writes sit in `mem_phys_write32/8` (VRAM case at `mem.c:438-442`). ROM blocks may skip verification. A reset is a G-13 flush.
- **`divide()`** (`rpc_mem_timing.c:89-106`) has an `__int128` reciprocal path, whose proof comment assumes 28 ≤ d ≤ 2800 (`:93-95`), and a plain `%`/`/` fallback (`:101-103`). Only the reciprocal path needs its proof restated for D = 5600.

**Re-checked facts the plan rests on** [C]:

- `arm.c:607` re-reads the opcode on every fetch.
- `cp15.c:426-427` installs before the domain and AP checks (`:429-437`).
- `arc_rpc_attributes` is stored on success only (`:439`).
- `getpccache` clears 0x40 and returns NULL (`:463-466`).
- The fetch-timing call is charged under COST_TIMING (`:470-476`).
- The batch admission ternary is at `arm.c:2011-2015`.
- `rate > 100` is fatal at `rpc_mem_timing.c:203`.
- `arc_rpc_session_test_rate` silently ignores values above 100 (`rpc_session.c:563`).
- Rates are 20/35/100/35 (`rpc_session.c:43-46`). Duplicate tables exist at `libretro/rpc_cmos_store.c:66` and `libretro/rpc_preparation.c:323`.
- `arc_fault_jump.h:10-12` selects `__USE_MINGW_SETJMP_NON_SEH`.
- Env 83/84 are at `libretro.h:2644`/`:2659`, and `GET_JIT_CAPABLE` 74 at `:2476`.
- CI `android-arm64` only builds. Since W6 (S67, D-196(2)) the `host-arm64` job runs the full host suite natively on `ubuntu-24.04-arm`, so CI executes arm64 Linux; Android itself still runs only on the Odin3.
- The memo clear sites are `rpc_mem_timing.c:224, 248, 299, 375, 421, 437`.

## 3. Dependency graph and sequencing

```
NOW (parallel with R3c-2/R3c-3/R3d and Phase 1a; Odin3 legs serial, D-182)
  A0 Windows-in-RetroArch probe (needs F-a) --+
  A1 Android breadth probe (needs F-b) -------+--> X-0
  A2 arm64-in-CI route -----------------------+--> X-1b merge
  R0 rulings pack (docs) + Astra stage 1 on this plan --> B-0 work order
  C-1 design (docs)

AFTER P0i merges onto the reconciled R3c-2 baseline (Astra 6)
  Phase 1a: E0 -> L1/S1 (measured order) -> A1 -> F1 ; D1+D2 separate
  M1 measurement + C-0 ledger (test-only) --> R0 finalised (R-e, R-g data)
  M2 lockstep harness (test-only) ----------> required by every B/X slice
  C-1 (behaviour-preserving) -> C-2 (test-only rate widening)

AFTER 1a's F1 and R3c-3 merge (both touch arm.c)
  B-0 -> B-1 -> B-2 (needs C-2 before B-Q) -> B-3 (needs S1) -> B-4 -> B-5 (if M1 allows) -> B-6
  B-7 bus fast paths (B3 fills only if 1a did not take it; G-8 lazy after its ruling)
  B-Q = G1 (100 MIPS frame gate) --> Founder F-d: R4 may start

X track (in parallel with late B)
  X-0 after A0/A1/A2 report and R3c-3 merges (dormant, test overrides only)
  X-1a x64 call-threaded skeleton after B-2 (IR and exit contract stable)
  X-1b arm64 skeleton after A2's CI route is wired
  X-2 (both emitters) after B-4 ; X-3 (both) after B-5/B-7
  X-Q = G2 at test rate 200 (needs C-2) on both platforms --> Founder Q4

C track: C-0 (in M1) -> C-3 -> C-5 Founder pack ; C-6 rate flip only after X-Q ; C-7 contract changes each alone
```

**Against D-163 and D-182.**

- D-163 orders R1–R3, then 6b, then R4 (`decision-log.md:174`).
- Track A, M1, M2, C-1 and C-2 are test-only or scratch, under D-182's precedent for Phase 0.
- If production Phase 1a or B-0 starts before R3 completes, Fable records that explicitly (Astra finding 6). If it is read as a reorder, the Founder confirms it.
- RiscPC release still waits on D-163's speed condition, which also depends on C-5. G1 being met does not release RiscPC.

**Baselines.** Every production slice is compared against two pinned binaries (Astra finding 6, P-1):

1. the immediate predecessor;
2. the cumulative post-1a tree (R3c-2 + P0i reconciled).

A shared helper is never its own oracle (Astra finding 3).

## 4. Common gates (CG) for every B and X slice

| Gate | Requirement |
|---|---|
| CG-1 | `rpc-memory-timing-0..3` (4,677 intervals) and every `tests/fixtures/rpc-*.trace` golden are byte-identical with the new path **forced on** and forced off, under the Q-2 endpoint mode from E0. Hosts: Linux x64, ASan/UBSan, MinGW Windows, the Android build, and execution on Mac arm64 and the Odin3 where the path exists. Goldens are never regenerated. |
| CG-2 | M2's lockstep differential gives zero divergences against both baselines. It runs on the directed ROM set and the fixed-seed fuzzer corpus, with ordinary block lengths **and** with forced exits at every instruction position. |
| CG-3 | Coverage floors: every fast path, guard outcome and fallback reason is above zero, and the block or native share is at least a stated floor per calibration. A build that falls back everywhere cannot pass. |
| CG-4 | Named mutants are committed and shown killed. Existing mutant suites pass, with their anchors ported. |
| CG-5 | Quotas 1, 7 and 200 give identical chronology. `rpc-observation-plan` parity holds under off, unmatched, trace and active. `integer_attempts > 0`. |
| CG-6 | Cross-platform equality: runner traces and guest instruction counts are identical across Mac, Windows, Odin3 and Linux CI for the 031 recipe, hot and AC-8. |
| CG-7 | Chaos-fallback mode (random disable and re-enable at P0/P3) and tiny-cache mode (64 KiB, constant flush) give identical streams. This proves that host policy decides only **whether** a block runs, never what it computes. |
| CG-8 | D-180: at most 3 % in total on the integrated tree (not per slice), plus the measured held-watch cost. |
| CG-9 | Provenance, `arc_rpc_` namespace (`engine/backend-symbols.json`), import, include-closure and symbol guards pass. No ROM, title or trace bytes in commits or logs. |
| CG-10 | Benefit is judged by a decision rule frozen before measuring: inputs, persistent state, windows, repetitions, noise band and retest rule (Astra 7). Odin3 runs are serial ABBA (D-182). The unprofiled shipping-shaped core decides. Windows has a no-regression criterion. The default path does not regress when the new path is off. |

Every new host-only structure is listed as a §2.2 exclusion and reset at every G-13 point.

## 5. Slices

### Track A: platform probes (scratch, from scratch; Q3 not engaged; no tracked code)

**A0. Windows inside RetroArch.** Needs F-a.

- **Objective.** Rerun the S60 probe as a stub libretro core DLL (no guest) loaded by RetroArch.
- **Scope.**
  - Dual-map publish and `FlushInstructionCache`.
  - **The D-155 `__USE_MINGW_SETJMP_NON_SEH` longjmp through an emitted frame**, with and without `RtlAddGrowableFunctionTable`. S60 tested only the CRT unwinding form and `__builtin_longjmp`.
  - `GetProcessMitigationPolicy` for dynamic code (ACG), CET shadow stack and CFG.
  - Env 83 with size 0.
  - Load, unload and reload 500 times.
  - Defender and Smart App Control observations.
- **Non-goals.** Signing (item 5).
- **AC.**
  - A report recording the RetroArch build, the mitigation states, the env 83 result, 1000 injected fatals per unwind variant, and leak counts.
  - A recommended fatal path for X-0. Expectation [I]: keep D-155 and register unwind data anyway.

**A1. Android breadth.** Needs F-b for the second device.

- **Scope.**
  - The S59 probe inside every RetroArch distribution the Founder ships to.
  - The second D-135 device.
  - **An arm64 Android emulator image with 16 KiB pages on the Mac.** The Mac's own 16 KiB pages do not stand in for this, because macOS `MAP_JIT` is not the Android memfd path.
  - Env 83, pause/resume and process-kill teardown.
- **AC.** Per device and variant: SELinux domain, targetSdk, page size, mapping result, 1000/1000 fatal unwinds with x18 intact, and 500 create/destroy cycles without a leak.

**A2. Executing arm64 in CI.**

- **Options.**
  - (a) GitHub-hosted arm64 Linux runner. The repo is private [C], so it is likely paid; current pricing is [U].
  - (b) `qemu-aarch64` user mode in the x64 job. Free and deterministic.
  - (c) Mac and Odin3 locally only. Not sufficient for D-142's "CI in both modes".
- **Recommendation:** (b) now, with (a) only via F-e.
- **AC.** Measured run time of the RPC suite under qemu-user, plus a wired job plan.

**A-x (part of A0/A1).** Check, against primary sources, whether RetroArch 1.22.2 implements env 83/84 [U today].

### Track M: measurement and harness (test-only, compiled out of shipping)

**M1 (6b-P0j): measurement extension, including the C-0 guest-time ledger.**

- **Objective.** Supply the numbers that R-e, R-g, B-5, B-7, the burst-ROM exclusion and C-5 depend on.
- **Scope (counters):** (D-195: slice 1a-0 owns the data and fetch walks in item 2, the `batch_service` entry classes in item 4 and the integer against rational share in item 5; M1 cites them and does not re-add them.)
  1. **Churn** at word, 16 B, 32 B, 1 KiB and 4 KiB granularity, within 10⁴, 10⁵ and 10⁶ instructions, keyed by (allocation, offset). Plus write-then-execute: a fetched word whose value changed since its previous fetch.
  2. **Translation:** code-slot evictions caused by data installs (denied ones included); data and fetch walks per instruction; DACR class of executed code.
  3. **Exits:** E1 split into direct and indirect.
  4. **Service:** `batch_service` entry classes (pending, busy-not-pending, S1-eligible, due ≥ finish, video), and bus transitions per instruction (`m.stats.words`).
  5. **Admission factors:**
     - P0s with `arm.event != 0`;
     - the share of instructions in burst-ROM regions;
     - request-free run lengths across branches (potential regime length);
     - integer against rational share;
     - driver cost per batch.
  6. **The C-0 ledger, per frame:**
     - base ns;
     - demand stall by class (fill, uncached RAM, ROM, I/O, VRAM);
     - `buffer_wait_ticks`;
     - drain-before-read;
     - video hold (`arc_rpc_bus_ns`);
     - idle (`arc_rpc_idle_ns`);
     - spin share in non-idling poll loops.
  7. **Workloads:** 031, a 3D title, and the SA boot pairs.
- **Exactness.** Test-only. Guest traces must be identical with the instrumentation on and off, and the P0i frame-7000 image and instruction counts must be reproduced.
- **ACs.**
  - AC-M1: instrumented and plain runs give byte-identical guest traces.
  - AC-M2: every counter is checked on a directed ROM with known answers (store to PC+4, aliased store, a denied data access evicting the code slot, a burst-ROM fetch).
  - AC-M3: 031 and the 3D title are reported from the Mac (guest counts are deterministic); host cost from the Odin3.
  - AC-M4: the 50.6 MIPS executed, 9.76 ns stall and racing bus-share figures are either reproduced or corrected in `docs/riscpc-translator.md`.
- **Private legs.** 031 and the 3D title on the Mac; one Odin3 confirmation leg.
- **QA focus.** Counters excluded from §2.2; no shipping footprint.

**M2: lockstep differential harness.**

- **Scope.**
  - **H-1:** a test build emits a normalised §2.2 canonical digest at every P0, P2 and P3 boundary (pointers as (allocation, offset), live ring entries only). Baseline and candidate run from identical inputs in **two isolated processes**, and a comparator names the first divergent field. Two processes are required because MMIO cannot be replayed against already-mutated devices.
  - **H-2:** a seeded forced-exit schedule.
  - **H-3:** a sequence fuzzer covering 26/32-bit modes, MMU and DACR states (none, client, manager, reserved value 2), self-modifying code through the `0x10↔0x12`, `0x11↔0x13` and VRAM aliases, IRQ/FIQ edges with I/F masked in 26-bit modes, full write buffers, the SA clock switch, and timing-register writes mid-drain. Committed fixed seeds.
  - **H-4:** directed ROMs for every untested row in the invariants' coverage map (G-1, G-2, G-3, G-4 including a denied install, G-6, G-8, G-9 including an STM partial abort, G-10, G-13).
- **ACs.**
  - AC-H1: self-consistency (baseline against baseline) over the full corpus.
  - AC-H2: every harness mutant detected (`stats.hits`/`fetch_hits` swapped, attributes stored on fault, `cpu_due` off by one tick, `reg[16]` refresh skipped).
  - AC-H3: runs on Linux x64, Windows and Mac arm64.
  - AC-H4: cross-platform stream equality is itself a CI check.

### Track R: rulings (Fable, challenged by Astra stage 1; one D entry, docs only)

Each ruling is listed with what it decides; the exactness argument is to be written in R0.

| Ruling | Content | Needed before |
|---|---|---|
| **R-f** | **Foundation shape.** An ArcDroid-owned linear non-SSA IR. **B-0 subset:** data processing with shifts and flags, B/BL, single LDR/STR/LDRB/STRB. That is 94.6 % of 031's mix by class (ALU 49.7, loads 21.7, branches 21.0, stores 2.2; `TRANSLATOR-MAIN.md:211`). Every other class ends the block **before** itself, and the interpreter runs it. Later slices may add helper-backed LDM/STM and multiplies, each with its own differential, because ending at every LDM/STM (4.1 %) and multiply (1.3 %) shortens runs. | B-0 |
| **R-g** | **G-9 mechanism.** Verify-on-fetch replaces the `mem_phys_write32/8` hook. Default: **per-instruction** comparison of the cached word with live backing at P0(k), through the block's (allocation, offset). This is equivalent to the oracle's re-read (`arm.c:607`) for any writer, including k writing k+1 and every mirror, and it retires Q-4. ROM blocks skip it. The native fallback (vector verify at entry plus a range check after in-block stores or MMIO) **depends on Q-4** and needs its own wording if adopted. D-142(3)'s keying is kept. | B-0 |
| **R-e** | **G-4 / D-142(2) block permission reuse.** Within one 1 KiB tag, after the pure entry probe: the verdict is a pure function of entry.domain and entry.ap, DACR, `ctrl&0x300`, `memmode` and rw = 0. The entry changes only at the install (`cp15.c:426`, denied accesses included) or a flush (`:96`). DACR, S/R, mmu and memmode change only at E4 exits. So a host-only per-slot install generation plus a flush epoch is a complete dependency set [I, to be stated formally]. A generation change takes E2 at P0(k+1). `arc_rpc_attributes` is re-stored from the code entry at the next fetch after any data access, and never on a deny. Also amend G-4 and §1.4 to "stored on success only". | B-5 |
| **R-t** | **Timing folds, each classified separately.** (A) Base-step regime in closed form, integer and rational. (B) S1-generalised bus window with W = min(`next_ns`, limit, visible(finish)). (D) Fetch timing per line through a static memo-line map. (F) Bulk materialisation. Lazy multi-transition catch-up (G-8) is **deferred** to R-l. | B-1, B-2, B-3 |
| **R-l** | **G-8 lazy catch-up.** A written proof that `run(t)` at P0, before any possibly-requesting instruction, equals stepped service when there are no own requests, `m.count == 0` and `rom_sequential == false`. P0 placement is forced because `run()` calls `commands()` before completing cycles (`rpc_mem_timing.c:629`) [per the timing map; :629 not re-read this run]. | B-7b |
| **R-x** | **§2.2 host-only exclusions:** block tables, slot generations, `slot_unit`, cached words, chain links, way hints, regime state, inline caches. Each is reset at every G-13 point. | B-0 |
| **R-8** | **A1's TLB-exposure shape is chosen once, for 1a and for blocks.** Recommendation: a namespaced header context, because emitters need slot addresses and generations. | 1a A1 work order |
| **R-c2** | **Cycle-model readiness.** The IR carries a static cost per instruction in abstract cost units. The regime budget is in cost units. The exit contract reserves a carried previous-instruction-latency slot. Canonical state gains **no field now**. Every instruction-count bound in translator code is derived from the rate or expressed in ns. Also rule whether a future model may depend on data (SA multiply early termination) or on the previous instruction (interlocks). | B-0 IR freeze |
| **R-burst** | Burst-ROM code is excluded from blocks at first. Folding `track_rom`/`fetch_pc` is a later ruling, only if M1 shows the burst-ROM share is material. | B-0 |

**Phase-gate placement.** G1 carries the 100 MIPS gate; G2 carries 200 capacity. This is a sequencing record. Because it touches D-142's Phase 1 exit wording and R4 timing, it goes to the Founder with F-d and is not settled by Fable alone.

### Track B: portable foundation (C only, every platform; off by default behind a test override selected at `arm.c:2011-2015`)

Every B slice must show that the default path gains no per-instruction cost (an identical-instruction-count check on the hot loop, or CG-10).

**B-0. Former, key, table, pure probes, verify-on-fetch; every per-instruction hook kept.**

- **Scope.**
  - A classifier for the R-f subset, tested exhaustively against the interpreter's switch: `(op>>20)&0xff` × bits 4–7 × the v3/v4 halfword gates (`arm.c:617-640`, per the maps).
  - The IR and the IR interpreter.
  - **Key:** (backing kind, offset), full 5-bit mode, prog32/`r15_mask`, model, observer instantiation class, `arc_rpc_generation` plus a reset epoch. Stored in a two-level table (1 KiB unit → 256 word slots).
  - Blocks end at 1 KiB boundaries, at unsupported instructions and at `updatemode`.
  - G-13 flushes at start, failed start, stop, reset, test-rate rebind and engine switch.
- **Entry guards, all pure and evaluated before `instruction_begin(k)`:**
  1. `arm.event == 0`;
  2. `!test_measure`, or the Q-2 endpoint mode;
  3. pure TLB probe (valid, tag, live verdict), or `arc_rpc_phys_page` when the MMU is off;
  4. the normalised code pointer equals the key;
  5. the word verify;
  6. the region is not burst ROM, and `rom_sequential == false`;
  7. instruction-side `populated || allocate`.

  A failing guard takes E2, and the interpreter runs k whole, including its single real walk (Q-1, Q-10).
- **Inside the block.** The oracle's full sequence per instruction: `instruction_begin`, real `getpccache`, opcode record, condition, IR body, event check exactly as at `arm.c:1933-1950`, raw +4, `instruction_end`, real `arc_rpc_mem_batch_next`.
- **Non-goals.** Folding of any kind; chaining.
- **Exactness.** The only change is where the body semantics come from. They are covered by an exhaustive per-class differential: encodings × NZCV × shifter forms × both word sizes × 4 models, plus LDR/STR abort and writeback rules per model.
- **ACs.**
  - AC-B0-1: CG-1 to CG-9.
  - AC-B0-2: classifier exhaustiveness.
  - AC-B0-3: verify-on-fetch on every G-9 directed case (PC+4, same block, every alias, STM partial abort).
  - AC-B0-4: the path is unreachable from the observed instantiation.
  - AC-B0-5: mutants killed: skip verify, compare a stale pointer, drop the epoch from the key, drop the `event` guard, probe through an installing walk, admit burst ROM.
- **Private legs.** 031 sustained, Mac and Odin3, with CG-6 equality.
- **QA focus.** Probe purity (no `tlbs++`, no install, no attribute store); G-13 address reuse; key completeness.
- **Gain.** About 0 % [E]. This is infrastructure.

**B-1. Fetch fold with a live check (fold D).**

- **Scope.**
  - Replace per-instruction `getpccache` with F1's shared inline hit-and-permit check on live state (R-b).
  - Within one memo line, fold `fetch_hits` and `instructions` in bulk.
  - At line crossings, do the real lookup and `memo_set`, with L1's hint.
  - Materialise `opcode`, `recording`, `pccache2` and `arc_rpc_attributes` before every helper and at every exit.
- **Exactness.** `memo_valid` cannot change without a data access or a helper; after either, re-test it per instruction. `stats.hits` and `fetch_hits` stay individually exact.
- **ACs.**
  - SA D-fill memo clear (`rpc_mem_timing.c:375`) followed by a fetch;
  - a unified-cache data fill evicting the executing line (G-6);
  - mmu-off (attributes 0);
  - unbacked fetch;
  - mutants: fold across a data access, bulk-add into `stats.hits`, omit the `opcode` materialisation.
- **Gain.** −2 to −3 ns [E]. Basis: fetch is about 4.6 ns, and the memo test is charged under timing (`cp15.c:470-476`).

**B-2. Base-step regime (fold A), integer and rational from day one.**

- **Scope.** The `window`/`advance`/`materialise` API stays owned by `rpc_mem_timing.c`.
- **Opening a regime at P0** requires `!pending`, `!bus_busy`, `arm.event == 0` and overflow headroom (G-11). The budget N is in cost units, with B = min(`next_ns`, limit):
  - integer mode: `now + (N−1)·base_whole < B`;
  - rational mode: `issue_ticks + (N−1)·base_ticks ≤ (B−1)·D`;
  - N ≤ `count − linecyc`.
- **Inside.** One subtract and one branch per instruction or chain point.
- **Ends at:** any possibly-requesting instruction (a miss, uncached access, I/O, a non-SA store hit, DRAIN), any helper, and the base re-read after any access helper.
- **Exits.** Closed form per phase:
  - integer: `now += n·base_whole`;
  - rational: fraction carry with one small division (n ≤ 200);
  - `integer_batch` drops exactly where `.h:153-155` would drop it;
  - no `batch_flush` or `next_base` where the oracle has none.
- **Exactness.** Issue times are monotone and request-free steps are pure additions, so one entry check covers all n. Rational mode is needed because ARM710 at 35 MIPS (D-164(2)) is never integral (`base_fraction`, `rpc_mem_timing.h:84-85`).
- **ACs.**
  - per-instruction issue ticks equal to the baseline on 4 models and SA slow and fast clock;
  - `due == next_ns`, `due == limit`, and a tied video event;
  - distinct rational ticks sharing one visible ns;
  - quota-edge ties;
  - the overflow edge;
  - mutants: `≤` for `<`, flush at a P0 exit, quota off-by-one, headroom skipped;
  - **also at test rates 200 and 202 once C-2 lands. C-2 must precede B-Q.**
- **Gain.** −3 to −5 ns [E]. Basis: the timing service is about 14.5 ns. The scratch count of about 70 host instructions per instruction on the hook path is [U].

**B-3. Bus-busy window (fold B).** Requires 1a S1 merged.

- **Scope.** With `m.count == 0`, `!pending`, `!video_active` and `rom_sequential == false`, request-free instructions whose due is below `W = min(next_ns, limit, visible(finish))` (strict) issue with no service call. The integer flush is reproduced at the first busy boundary.
- **Exactness.** `run(now)` for `now < H` is a no-op under these guards (Astra 4, S1 verdict). Strict `<` is required because a queued video starts at finish and pushes `cpu_due`.
- **ACs.**
  - Astra's S1 set lifted to blocks: ties, `count == 0 && pending`, queued video, a ROMCR/CLKCTL write before the next word;
  - the CPU freeze while video is active;
  - mutants: floor against ceiling, `!video_active` omitted, `rom_sequential` omitted, a count-only commands guard.
- **Gain.** −1 to −3 ns [E]. It is unmeasured on 031; M1's service classes decide it.

**B-4. Chaining across E1, plus indirect inline caches.**

- **Direct links** are taken only after the exact continuation at that point and the target's pure probe.
- **Budget** is carried as cost units and re-derived after any request or helper.
- **Indirect links** use a per-site cache confirmed by the same probe.
- **Never chain** across `updatemode`, SWI, c7 or idle.
- **ACs.**
  - a chain through a TLB slot conflict (E2 at interpreter speed);
  - a link to a block whose words changed is caught by verify;
  - mutants: skip the continuation, skip the probe.
- **Gain.** −1 to −2 ns [E]. Basis: E1 is 197 per 1000 instructions.

**B-5. Permission reuse (R-e).** Starts only after Astra has challenged R-e, and only if M1's code-slot eviction rate is low.

- **ACs.**
  - a denied data install into the code slot;
  - a re-install of the code tag with a new descriptor within one access sequence;
  - stale attributes across a faulting data access;
  - DACR value 2;
  - a 1 KiB crossing inside a 4 KiB page;
  - mutants: omit the generation bump, store attributes on deny.
- **Gain.** −0.5 to −1.5 ns [E].

**B-6. Cheap observation mode (D-180).**

- **Scope.** The block path runs under the cheap instantiation. SWI ends a block (E10) through a helper calling `observe_advance`, then `observe_swi_at`/`opSWI`, which may truncate `count`. Prefix accounting happens at batch end.
- **ACs.** Observer canonical state is included in H-1; D-180 at 3 % or less with the path on; the held-watch cost is measured.

**B-7. Bus fast paths (shared C, benefits both tiers).**

- **(a)** A closed-form buffered-store enqueue when `!video_active` and capacity allows, with the active packet held by index. This needs a §2.2 restatement of the compared Packet fields (`PHASE1A-DRAFT.md` §4, "M5 active packet by index").
- **(b)** Lazy multi-transition catch-up, only after R-l.
- B3 closed-form fills belong here only if 1a did not take them, with full side-effect parity: `rom_sequential`, `restart`, word counts, rollback and the near-overflow fallback (Astra 4).
- **ACs.**
  - buffer-full transitions;
  - video ties;
  - a timing-register write before the next word;
  - integer-to-service transitions;
  - mutants on commands-first ordering and on rollback.
- **Gain.** −1 to −3 ns [E]. The per-request service cost is the largest open uncertainty.

**B-Q = G1.**

- **Evidence.**
  - The Odin3 and Windows frame gate (p95 ≤ 0.80 T, no frame over T, compile and fallback cost included) on 031 sustained, the loading tails, the 3D title and hot.
  - Inside RetroArch on both platforms.
  - The 44-title A/B, the seven-pair boot, factory defaults, and a 30-minute thermal leg.
- **Founder.** F-d (R4) and Q4 for the portable tier.
- **If G1 falls short,** G1 is re-taken with X-2.

### Track X: native backends (shared IR, guards, materialiser and helpers; thin per-backend emitters)

**X-0. Executable-memory layer, self-test and unwind.** Dormant: no guest code runs natively and nothing changes in `arm.c`.

- **Scope.**
  - `arc_rpc_jit_mem` providers, always dual RW/RX and never RWX, with no protection toggling:
    - Android: memfd via `syscall`, `ASharedMemory_create` via `dlsym` (minSdk 21, `ci.yml:281`), `sysconf` page size, `__builtin___clear_cache` plus `isb`;
    - Windows: `CreateFileMapping` with two views, `FlushInstructionCache`, and growable function-table registration and deregistration before unmapping;
    - Linux: memfd (CI);
    - macOS: `MAP_JIT` (development only).
  - The env 83 probe with size 0: accept DUAL_MAP or UNRESTRICTED; RWX means "do not use".
  - A trampoline and a self-test stub from a minimal from-scratch encoder. **x18 is never written.**
  - **Test overrides only.** There is no player-visible core option until a path exists to enable (this removes the dormant-option surface).
- **ACs.**
  - Calibrations, goldens and the 031 trace identical with the layer compiled in;
  - the self-test passes on Mac arm64, Linux x64, Windows x64, and inside RetroArch on the Odin3 and on Windows;
  - an injected helper fatal through the trampoline returns cleanly 1000 times on Windows CI (D-155 form) and on Linux CI;
  - injected provider failures disable acceleration with a logged reason and leave guest output unchanged;
  - 500 create/destroy cycles leak nothing;
  - every encoder form is checked against `llvm-mc`/objdump, including range rejection (reject, never mask);
  - CG-9.

**X-1. Call-threaded skeleton (two small PRs: X-1a x64, X-1b arm64).**

- **Scope.** Every IR operation lowers to a call of its IR-interpreter handler, in the same order with the same arguments, plus:
  - real chaining patched through the RW view;
  - cold exit stubs into the shared materialiser;
  - a bump-allocated code cache with flush at a safe point outside `arc_rpc_advance_internal`;
  - a compile budget per frontend entry;
  - ABI tables: AAPCS64, Win64 and SysV.
- **Order.** x64 first, because CI executes both of its ABIs. arm64 is merged only once A2's route executes it in CI. This is decision D-n1.
- **Exactness.** Equal to the IR interpreter by construction. Only platform mechanics are new.
- **ACs.**
  - B's CG set with native forced on;
  - identical streams across all hosts;
  - a 30-minute 031 soak with no flush storm, and a p99 compile-cost histogram against T;
  - mid-session kill-switch flips change nothing;
  - load/unload inside RetroArch leaks nothing.
- **Gain.** Roughly neutral. This is a risk-retirement slice.

**X-2. Native ALU, branches, flags and register cache (both emitters in one slice).**

- **Scope.**
  - Guest r0–r14 cached in host registers within a constant mode: arm64 x22–x28; x64 about 4–5 registers.
  - Lazy NZCV with liveness analysis, materialised at every P-point, helper call, exit and 26-bit R15 merge.
  - The raw +4 carry (Q-6) and x64's inverted CF handled explicitly.
- **ACs.**
  - the condition table exhaustively;
  - at least 10⁸ randomised instructions per backend, IR against native, with forced per-instruction exits;
  - a flag-materialisation mutant per class killed.
- **Gain.** Dispatch about 4.6 → about 0.5 ns [E].

**X-3. Inline hit paths (both emitters).**

- **Scope.**
  - Inline fetch probes at chain points.
  - An inline data TLB hit-and-permit check on live state (R-b), plus a D-cache tag hit via L1's hint (validated by `valid && va==line`), with `stats.hits` exact and SA dirty halves.
  - Misses, allocations, packets, I/O, LDRT/STRT, SWP and halfword forms go to the shared C body.
  - A functional access through `entry->code` after the live check needs classification ruling R-4 against `mem.c:697-698` ("no privilege-dependent host-pointer cache").
- **ACs.**
  - the warm-hit permission matrix including DACR value 2;
  - STRH first-byte abort;
  - stale attributes, FAR/FSR;
  - SA dirty halves.

**X-Q = G2.**

- **At test rate 200** (needs C-2):
  - cache-hot and polling workloads at 3.3–4 ns/instr or better;
  - 031 and the 3D title within the gate;
  - cold start, loading tails, thermal, the A/B and the seven pairs;
  - **on both platforms.** One common guest rate means the slower backend sets it (D-188(2)).
- **Founder.** Q4 for native.

## 6. Cycle-timing review track (D-188(4), (5))

| Slice | Kind | Content | Decided by |
|---|---|---|---|
| C-0 | test-only | The guest-time ledger inside M1. It settles stall against idle for 031: D-163's "memory-stall model halves throughput" against S65's "62 MIPS executed, the rest is idle" (`TRANSLATOR-MAIN.md:211`). It predicts the 200-rate gain with no change. | Evidence |
| C-1 | behaviour-preserving | A rate-derived backstop replacing `1666667u` (`rpc_session.c:413-415`), keeping the `next==limit` extra instruction. One shared rate table (`rpc_session.c:43-46`, `rpc_cmos_store.c:66`, `rpc_preparation.c:323`). One named admission maximum, still 100, covering `rpc_mem_timing.c:203`, `rpc_session.c:563`, `arcdroid_engine.c:474` [U] and `tests/arc_test_plan.c:407` [U]. Out-of-range test rates refused, not silently ignored. The `divide()` reciprocal proof restated for any d < 2³². Overflow horizons documented. Gate: everything byte-identical; mutants on the derived budget killed. | Fable |
| C-2 | test-only | Test rates 1..R_max (R_max ≥ 202). The independent oracle extended to `range(1, R_max+1)`, with long runs at 200 and 202 (202 covers SA rational continuation). New calibrations come only from the independent oracle. Production defaults unchanged. **Lands before B-Q.** | Fable |
| C-3 | test-only, offline | A recorded bus-request stream replayed through oracle variants: G4 open page, G6 read-around-buffer, video-hold overlap, SA per-class cycles. Output is a Founder pack with predicted fps for 031 and the 3D title per variant. | Evidence |
| C-4 | Founder information | Real 202 MHz Risc PC data or recordings, as `docs/riscpc.md:60` requires [path per map, U]. | Founder |
| C-5 | Founder decisions | (1) The production SA rate. **200 is recommended**: base 5 ns keeps integer batching, whereas 202 and 233 are always rational. Also whether it is fixed or an option. (2) Each contract change: per-class or pipeline model, relaxing G4, G6 or §2.6. (3) What "near-hardware speed" means for D-163. (4) Whether Auto (D-164(2)) prefers StrongARM once it is faster. | Founder |
| C-6 | production (Phase 3) | After X-Q only. Pin `rate_mips=100` in today's SA golden plans and prove those runs identical. Flip the registry. Add new-rate goldens only where clock switching is enabled; slow-clock SA goldens are predicted unchanged and then verified. Bump `ARC_RPC_CMOS_TIMING_REVISION` (`rpc_cmos_store.h:11` [U]). Re-qualify the seven pairs, factory capture, observers, AC-10 and the A/B. **Never combined with a contract change.** | Founder decision; Fable executes |
| C-7 | contract change | Any adopted cycle-model or approximation change, each in its own run-B-style slice: oracle first, every delta explained. | Founder decision |

**Fable decisions in the C track:**

- the C-1 policies: the CMOS revision bump rule, and whether observer watch limits (`ARC_RPC_WATCH_ATTEMPT_LIMIT`, instructions) are restated in ns;
- the R-c2 ruling on data-dependent and previous-instruction cost;
- whether the foundation must carry latency state across exits.

## 7. Gains model (031 sustained racing, Odin3)

**Targets.**

- **100 MIPS gate.** p95 34.0 ms must fall to 15.98 ms, which is 2.13×. So 28.5 → **about 13.4 ns** average [M-derived].
- **200 ceiling, stall-bound.** If 031 is stall-bound at about 9.76 ns/instr [U], it executes about 68 MIPS, about 1.34× today's instruction count per frame. It then needs **about 10 ns** [E].
- **Cache-hot at 200 executed.** That is 3.99 M instructions per frame, so **3.3–4 ns** (D-188).

**Starting ledger** [M, from the shares]: timing 14.5, fetch 4.6, dispatch 4.6, bus 4.7 ns. It is a ranking, not a ledger (`TRANSLATOR-MAIN.md:215`).

| After | 031 ns/instr [E] | Cache-hot ns [E] | Main lever and basis |
|---|---|---|---|
| Today | 28.5 [M] | about 10 | — |
| Phase 1a | about 21–23 | about 8 | 20–25 %, a hypothesis until measured (Astra 7) |
| B-0 | about the same | about the same | Infrastructure |
| B-1 + B-2 | about 15–19 | about 4–6 | Fetch fold; per-instruction hooks become a per-regime decrement |
| B-3 + B-4 + B-5 + B-7 | about 13–17 (G1 borderline) | about 4–5 | No-op service removed (about 0.38 calls per instruction documented); chaining; reuse; bus enqueue |
| X-1 | about the same | about the same | Risk slice |
| X-2 | about 9–12 | about 1.5–3 | Dispatch and flags native |
| X-3 (+ B-7b) | about 5–8 | about 1–3 | Inline hits; remaining per-request service |

**What is left after native code** is about 0.40 data accesses per instruction plus per-request service in shared C. If per-request service stays near today's cost, 031 lands at about 6–8 ns: enough for the gameplay gate, marginal for store-dense code at 200.

**Platform note.** Windows is only 0–23 % faster per instruction than the Odin3 (D-188 context), so the x64 backend has to be as capable as the arm64 one.

## 8. Risks

1. **Per-request bus service stays expensive.** Mitigation: M1 service classes first, B-7, and R-l.
2. **Short runs** (average 4.1, median 3). Chaining (B-4) is mandatory. G1 may need X-2.
3. **R-e rejected, or M1 shows high code-slot eviction.** Then fetch remains a live inline check at about 1 ns, which is 25–30 % of the 200 budget [E]. G2 on cache-hot code is at risk; G1 is not.
4. **Burst-ROM exclusion.** If RISC OS runs SWI-heavy code from burst ROM, block coverage drops. M1 measures the share, and R-burst decides on folding.
5. **`arm.event != 0` admission.** Long masked-pending periods would reduce coverage. M1 counts them.
6. **ARM710 is always rational.** The folds are rational from B-2.
7. **Slot conflicts and 1 KiB thrash** run at interpreter speed. That is exact, only slow.
8. **Compile spikes on loading** (56.3 ms p95 today). Mitigated by the compile budget and a tier-up threshold.
9. **Platform policy:** Windows ACG and CET inside RetroArch, the non-SEH unwind, Android OEM `execmem`, 16 KiB pages. Retired by A0, A1 and X-0.
10. **No arm64 CI.** Handled by A2 and qemu-user.
11. **Spinning absorbs a higher ceiling** (C-0 spin share).
12. **A higher rate is not a 202 MHz StrongARM.** Stall-bound titles gain about 1.3× at 200 [E/U]. The C track is on D-163's critical path.
13. **Semantic duplication in the IR.** Contained by the exhaustive classifier, the H-3 fuzzer, and three-way native/IR/oracle differentials.

## 9. Open questions for Astra (stage 1 on this plan)

1. Is R-e's dependency set complete, given the install-before-check order (`cp15.c:426-437`) and attributes stored on success only (`:439`)?
2. R-g: is per-instruction verify-on-fetch equivalent to `arm.c:607` for every writer, including an STM partial abort and a store into k+1? What wording is required for the entry-plus-range fallback (Q-4)?
3. R-t (A): is one entry check sufficient in rational mode, with `integer_batch` dropping exactly at `.h:153-155`?
4. R-t (B): beyond S1's tie set, are any further guards needed when blocks span chains?
5. R-l: what is the formal statement and proof of P0-only lazy catch-up, given `commands()`-before-completion and the bus-side writes to `rom_sequential` (`rpc_mem_timing.c:490-526`, per the map)?
6. Is the `arm.event == 0` admission rule sufficient for G-3 in 26-bit modes (`arm.c:1933-1937`)? Is `arm.event` provably constant inside a helper-free batch?
7. R-burst: is folding `track_rom`/`fetch_pc` (`rpc_mem_timing.c:339-344`, `.h:132`) worth a ruling?
8. R-c2: does reserving a latency slot now, without a canonical field, constrain a later interlock model correctly?
9. Is the call-threaded X-1 worth a slice, compared with going straight to X-2?
10. G1/G2 placement against D-142's phase wording.

## 10. Carried items

**Citation refresh** in `docs/riscpc-translator-invariants.md`:

- §1.1: `arm.c:571/574/581/582` → `:597/600/607/608`; the event check is at `:1933`.
- §1.8: `cp15.c:379-438` → `:390-449` (install at `:426`, attributes at `:439`).
- §1.4 and G-4: change to "stored on success only".
- Memo clear sites (invariants :213-215): `:335/:220/:230-233/:369,382/:262` → `rpc_mem_timing.c:375/224/248/421,437/299`.

**Other carried items:**

- `PHASE1A-DRAFT.md` §4 cites `mem.c:613-614`; the line is now `:697-698`. It also cites `arm.c:571`, `.h:86` and `rpc_session.c:396`; these are now `:574`, `:87` and `:413-415`.
- `TRANSLATOR-MAIN.md:226` lists Q-3 as still open, which is stale.
- D-181 is absent from this clone (Astra 6). Supply its recipe before CG-10 uses it normatively.
- The D-131 figure of about 47 fps native for 031 is unchecked.

## 11. What I did not verify

- **The private `.work/s65/p031/` JSON.** It is outside the clone, so these remain [U]: sustained-window shares (including the 25 % bus figure), 50.6 MIPS executed, 9.76 ns stall, bus_calls 0.644, and batch and event rates.
- **The scratch benchmarks**, including the count of about 70 host instructions on the hook path.
- **RetroArch 1.22.2 support for env 83/84.**
- **GitHub arm64 runner pricing** for private repos.
- **Smart App Control** re-enable behaviour.
- **Lines I took from the maps without re-reading:**
  - `rpc_mem_timing.c` `:629`, `:686-712`, `:724-771`;
  - `rpc_mem_timing.h:79-92`, `:153-166`;
  - `arcdroid_engine.c:474`, `tests/arc_test_plan.c:407`, `rpc_cmos_store.h:11`;
  - `docs/riscpc.md:60` and the `docs/riscpc-timing.md` sections.
- **The `divide()` correction for d up to 5600**, which was only argued by hand in the map.
- **That `arm.event` is constant in helper-free batches.**
- **No build, test or device run was performed.**

Files relied on:
- .work/s65/6b/clone/D-188.md
- .work/s65/6b/clone/TRANSLATOR-MAIN.md
- .work/s65/6b/clone/PHASE1A-DRAFT.md
- .work/s65/6b/clone/ASTRA-PHASE1A-1.md
- .work/s65/6b/clone/docs/riscpc-translator-invariants.md
- .work/s65/6b/clone/docs/decision-log.md
- .work/s65/6b/clone/engine/rpcemu/src/arm.c
- .work/s65/6b/clone/engine/rpcemu/src/cp15.c
- .work/s65/6b/clone/engine/rpcemu/src/mem.c
- .work/s65/6b/clone/engine/rpc/rpc_mem_timing.c
- .work/s65/6b/clone/engine/rpc/rpc_mem_timing.h
- .work/s65/6b/clone/engine/rpc/rpc_session.c
- .work/s65/6b/clone/engine/arc_fault_jump.h
- .work/s65/6b/clone/libretro/include/libretro.h
- .work/s65/6b/clone/libretro/rpc_cmos_store.c
- .work/s65/6b/clone/libretro/rpc_preparation.c
- .work/s65/6b/clone/.github/workflows/ci.yml

---

## Amendments (incorporated)

From the completeness critic, all adopted; amendment 2 is settled by D-189, amendment 3 by the Founder-items section above.

Four of the findings are material (1–4); the rest are amendments. All were checked in the clone at `1bb22aa`, on `main` `336d787`, or in the R3c-3 branch diffs; nothing was built or run.

**1. HIGH, exactness: the block key drops the PC virtual address that G-2 requires.**
- **Evidence:**
  - G-2 (invariants:308-313) keys blocks on the PC VA.
  - B-0 keys on (backing, offset) and checks only that the code pointer matches the key (guard 4).
  - `getpccache` returns `entry->code`, which points at the physical 1 KiB page (cp15.c:426, :466-470).
  - So two VAs that map to the same physical bytes reach the same block. That covers an MMU page mapped twice, and, with the MMU off, the `0x10`↔`0x12` RAM mirror.
  - A block that bakes in a PC-relative branch or link target, a PC-relative literal address or a chain link will then use the wrong PC.
- **Amendment:**
  - Either add the VA (or the 1 KiB VA tag) to the key, or have R0 rule that the IR is VA-independent: PC taken from live `reg[15]`, and chain links checked against the VA.
  - Record whichever is chosen as an explicit G-2 amendment.
  - Add an H-4 directed ROM: one physical page mapped at two VAs, plus the MMU-off mirror, running B, BL, ADR and LDR-literal from both. Add the mutant "VA dropped from key".

**2. HIGH, Fable/Founder split: production 6b code before R3 completes is a reorder, yet the plan says nothing is waiting on the Founder.**
- **Evidence:**
  - D-141: no translator code lands until R4 "unless the Founder reorders".
  - D-163 (Founder): R1–R3 → 6b → R4.
  - D-182 advanced only 6b Phase 0 (test-only profiling).
  - Even so, the plan's graph starts production Phase 1a after P0i/R3c-2, starts B-0 after R3c-3, and treats C-1 as production. R3d-1..6 are still in flight (D-186).
  - "If it is read as a reorder, the Founder confirms it" leaves the call open. §0's claim that nothing blocks 1a or the foundation on the Founder is therefore wrong.
- **Amendment:**
  - Either add F-h, "start production Phase 1a, C-1 and B-0 before R3d completes", or gate those slices on R3 completion.
  - Tracks A and M and C-2 stay test-only under D-182.

**3. MEDIUM, wrongly assigned to the Founder: F-a (installing RetroArch on Windows) blocks nothing.**
- **Evidence:**
  - CI job `windows-retroarch` (ci.yml:238-258) already runs `tools/test-retroarch-windows.ps1`.
  - That script downloads the pinned RetroArch 1.22.2 build from buildbot and checks its SHA-256 (script lines 9-23). It then loads the core and reloads it on windows-latest.
- **Amendment:**
  - A0 runs as a CI job, or from the same portable archive on the Windows box. X-0's "inside RetroArch on Windows" acceptance criterion becomes a CI check.
  - F-a shrinks to the Smart App Control and Defender observations, which are not blocking. S60 already records that Smart App Control was switched off on the box.

**4. MEDIUM, D-142(7) not carried into the X acceptance criteria.**
- **Evidence:**
  - D-142(7) requires the probe to prove execution, **code replacement**, helper calls and fault unwind inside RetroArch, with **bounded code storage and compile work**.
  - X-0's acceptance criteria omit code replacement.
  - X-1 only measures compile cost (a p99 histogram); it sets no bound.
- **Amendment:**
  - X-0: at least 20,000 replacements at reused addresses inside RetroArch on the Odin3 and on Windows. Cache maintenance runs on the executable (RX) range, followed by `isb`.
  - X-1: a fixed code-cache cap and a per-entry compile budget as named constants. Add a test that exceeds the cap and proves the flush happens at the safe point and that guest streams stay identical (CG-7).

**5. MEDIUM, platform risks still open.**
- **qemu-user (A2(b)) cannot fail a missing cache flush.** qemu detects self-modifying code itself and models neither I-cache coherence nor SELinux/memfd policy.
  - Label qemu legs ISA-only.
  - Require real-hardware execution on the Odin3 and Mac arm64 before any X-1b/X-2/X-3 merge.
- **arm64 BTI/PAC is unaddressed.** If RetroArch or the core is BTI-marked, branches from emitted code into helpers must use BLR, or BR through x16/x17.
  - No branch-protection flag appears in CMake or CI today, so A1 should record the BTI state per distribution.
  - Add the constraint to X-0 and X-1.
- **Windows CET shadow stack.** Emitted code must keep calls and returns balanced. The D-155 non-SEH `longjmp` does not adjust the shadow stack pointer.
  - A0 must run the 1000-fatal unwind test under a CET-enabled (CETCOMPAT) host process, not just read the mitigation policy.

**6. MEDIUM, exactness: the harness skips P1.**
- **Evidence:**
  - H-1 takes digests at P0, P2 and P3 only.
  - E3 (helper call inside an instruction) runs at 39 per 1000 instructions, and its state is defined at P1 (§2.1).
  - B-1's materialisation before helpers (`opcode`, `recording`, `pccache2`, `arc_rpc_attributes`) is P1 state. A stale value that corrects itself by P2 would escape.
- **Amendment:** H-1 also digests at every helper entry (P1).

**7. MEDIUM, exactness: measurement endpoints can fall inside a fold.**
- **Evidence:**
  - `arc_rpc_mem_begin_slow` captures the interval at `measure_first` (rpc_mem_timing.c:279-290).
  - `test_stop` ends the batch at `measure_last` (rpc_mem_timing.h, batch_next).
  - B-1's bulk updates to `instructions`/`fetch_hits`, B-2's regimes and B-4's chains must split exactly at those ordinals under E0's endpoint mode.
- **Amendment:** add an acceptance criterion placing endpoints at every position inside a fold, regime or chain, plus a mutant that fails to split.

**8. MEDIUM, D-146 obligations with no owner.**
- **G-14 (idle) is in no slice.** H-4 omits it, and B-4 only says "never chain across idle".
  - Add directed ROMs for: c15 wait (cp15.c:270) and ARM7500 SUSMODE (iomd.c:326) inside a block; writes pending across sleep and wake; wake while masked.
- **Q-5 (`dirtybuffer`) is "re-audited at R4 before inline stores omit it"** (invariants:459). D-163 put 6b before R4, so that trigger now comes after B-7a and X-3.
  - `dirtybuffer` is written at mem.c:443/510/580/649 and only ever reset by `memset` (vidc20.c:607).
  - Make the re-audit a precondition of B-7a and X-3.
- **Q-4 re-audit "after R2c/R3"** (invariants:458) has no owner.
  - The R3c-3a/3b branch diffs add no host writes to guest RAM. D-185's "host replacement of the helper with a no-op on reset" still needs confirming.
  - Assign the re-audit to R3 completion, before R-g freezes. R-g's native entry-verify fallback depends on it.
- **G-13 list.** Add R3d's RPC reset/rebind transaction (D-186) and future save-state loading (item 8) to the G-13 flush points.
- **X-3 is missing the "pending-abort access" case from Astra's R-b correction.** The accessors translate before checking `event & 0x40` (mem.c:701-702). Add it to X-3's acceptance criteria.

**9. MEDIUM, unmeasured assumptions in the gains model.**
- **(a) Verify-on-fetch cost is missing.** R-g's per-instruction verify is not in the ledger. In native code it sits beside the live fetch check, against a 3.3–4 ns budget.
  - Microbenchmark it on the Odin3 and Windows before R-g freezes.
- **(b) RetroArch's own frame cost is not measured.** Every frame and ns/instr figure comes from `rpc_runner`; TRANSLATOR-MAIN:195 and :208 say the RetroArch frontend was not covered. G1 and G2 are gated inside RetroArch.
  - M1 must measure RetroArch's per-frame overhead in the RiscPC output mode on the Odin3 and Windows, and deduct it from the 0.80 T budget.
- **(c) There is no Windows baseline for 031.** The "0–23 % faster" figure comes from synthetic workloads, and AC-M3 reports host cost from the Odin3 only. CG-10 and G1 both need Windows numbers.
  - Add a Windows 031 leg.
- **(d) The 13.4 ns target mixes percentiles.** 28.5 ns is a p50 figure (about 29.5 ms over about 1.03 M instructions), but it is scaled by the p95 factor of 2.13×.
  - Use M1's instruction count for p95 frames.

**10. MEDIUM, C-1 and C-2 enumerations are incomplete.**
- **The scheduler refuses rates above 200.** `rpc_scheduler.c:26` rejects `rate_mips > 200u`, which blocks test rates 202 and 233.
- **One test needs changing.** `tests/rpc_plan_test.py:32` asserts that rate 101 is refused; C-2 must change it.
- **Backstop derivation.** The 1,666,667 backstop is 100 MIPS × the 16,666,667 ns per-advance cap (rpc_session.c:384).
  - At test rate 200, cache-hot code hits the fatal within a single advance.
  - State the derived backstop as rate × maximum elapsed, keeping the extra instruction at the limit.
- **Add a Fable ruling on the 200-instruction batch quota** (`arm.c:574` fatal; headroom at `.h:86`): fixed or derived from the rate. Any change extends the Q-7 proof (D-142(9)).

**11. LOW–MEDIUM, a check that cannot be met as written.**
- **Evidence:** CG-6 requires 031 equality on Linux CI, but 031 is private content (protocol §8, D-183). AC-H3's Mac arm64 leg is likewise local only.
- **Amendment:**
  - CI checks cross-platform equality on authored workloads only (hot, AC-8, directed ROMs, fuzzer seeds) across Linux x64, Windows and qemu-arm64.
  - 031 equality becomes Fable's private legs on the Mac, Windows and the Odin3, with hashes recorded.

**12. LOW, corrections.**
- **D-181 is not missing.** It is on `main` (decision-log line 195). Its recipe: Odin3, authored hot workload, cells 252, realtime 60 + 300 s, two ABBA orders of six runs, p50 and p95.
  - Use it as the recipe for CG-8 and CG-10, and drop the carried "supply D-181" item.
- **Restate CG-8 as D-180's ratio.** Compare observed (cheap instantiation) against unobserved within one build, with the block path on, at p50 and p95.
- **Held-watch cost grows as the CPU speeds up.** With blocks off while a watch is live, today's +55 % becomes roughly 4× slower than the block path by G2. Add a G1/G2 leg with a held watch (a pending disc miss) to state the bound D-180 requires.

The other premises I re-checked hold:
- rpc_session.c:413-415 and :43-46;
- rpc_mem_timing.c:203, and the store hit setting a dirty half and returning only on SA;
- burst-ROM tracking at :339-344 and `.h:132`;
- `uncached_clock` rebasing time;
- cp15.c storing attributes on success only;
- `arc_fault_jump.h:10-12`;
- env 83/84 and 74;
- `arcdroid_engine.c:474`, `arc_test_plan.c:407` and `rpc_cmos_store.h:11`.

Main files: `.work/s65/6b/clone/docs/riscpc-translator-invariants.md`, `.work/s65/6b/clone/engine/rpcemu/src/cp15.c`, `.work/s65/6b/clone/engine/rpc/rpc_session.c`, `.work/s65/6b/clone/engine/rpc/rpc_scheduler.c`, `.work/s65/6b/clone/.github/workflows/ci.yml`, `.work/s65/6b/clone/tools/test-retroarch-windows.ps1`.

---

## Astra stage 1 (S65, ACCEPT WITH CHANGES): Fable's adjudication

Report `.work/s65/6b/report2.md` (6b thread `01a13e62-8e2e-7f10-b35a-de57821ce0e7`). All eleven findings are incorporated and govern over the body and the amendments above.

1. **No replay after a helper.** Whole-instruction fallback is allowed only before instruction entry (E2). After a helper, the block reloads the specified state, finishes the current instruction's body, event and base handling exactly once, and exits at P2 or P3. Mutants: replay after helper; repeated abort or exception handling.
2. **Bus shortcuts.** B-7a keeps the eligible command prefix (a fetch DEMAND ahead of a buffered store stays ahead), pending completion, and never services the current instruction before its functional effects finish. B-7b (lazy catch-up) is restricted to a proved background-buffer regime with no queued or active video, stopping before every event, helper, timing-register change and observation boundary, and stays optional until that proof exists. Mutants: demand before buffered store, queued video at grant end, delayed video completion.
3. **Phase projection before B-0.** One shared, non-mutating P0/P1/P2/P3 projection specification, including completed-instruction count versus successful-continuation count (the last instruction at a quota or deadline differs from an interior one). Digests read a projected snapshot and never flush folds or lazy state. P1 comparisons align by guest instruction and semantic access or observer site, including inlined helpers. Ordinary long-run tests stay alongside forced exits. Mutants: one-instruction quota, final-instruction overflow, stale integer representation, digest that flushes state. Chaos schedules are seeded by guest ordinal. CG-3 coverage floors apply only to eligible calibration and path pairs.
4. **Cheap observers from B-0.** Cheap-mode (observers subscribed, no live watch) support is carried through every optimisation from B-0; only genuinely active observation uses the observed interpreter. CG-8 (D-180, 3 % total) is never waived; the shared-loop SWI prefix accounting and re-selection from R3c-2 are preserved.
5. **Physical-memory contract for X-3 (R-4).** The functional hit path admits only specified backing kinds and writable ranges, with RAM/VRAM masking, alignment and rotation, byte accesses, absent backing and ignored ROM writes exactly as the interpreter; everything else stays on the shared accessor. Mutants: permitted store to ROM, mirrored and unaligned accesses (with the dirtybuffer and pending-abort checks already adopted).
6. **Executable-memory publication.** A provider operation `publish(rw, rx, length)` with an alias-aware proof (data-cache clean, ordered instruction-cache invalidation, instruction synchronisation on arm64). macOS gets its own supported write-protection protocol (MAP_JIT with per-thread write/execute transitions), not the dual-map contract. Replacements and chain patches are exercised across cache-line and page boundaries. Unsupported env 83 is handled separately from actual executable-memory denial.
7. **Probe qualification.** A0 qualifies the exact pinned CRT and toolchain under active hardware shadow-stack enforcement (not audit or compatibility mode) covering emitted ranges, with a deliberate failing control and a return to ordinary C after each fatal; unwind metadata, shadow stack and CFG targets are separate obligations; unqualified policies keep interpreter fallback. A1 records actual BTI/PAC enforcement and tests emitted entry and chain targets as well as helper branches; the 16 KiB leg loads the actual core in RetroArch with compatible ELF alignment and records whether page-size compatibility mode is active. A2 tries GitHub's native arm64 runners first (reported available to private repositories; verify), keeps qemu as an extra ISA check, and the Odin3 as the Android policy and cache-coherence qualification.
8. **Resource bounds from B-0.** B-0 has explicit formation-work and total-storage bounds covering IR, cached words, links, inline caches, unwind registrations and recompilation; X extends them. On exhaustion, stop forming blocks and interpret until a safe reclamation point; host budget exhaustion never adds a guest batch flush or timing boundary. Tests: sustained churn, allocation failures, invalidated links, fatal-unwind cleanup.
9. **Gains model.** The cost split ranks priorities; it is not a ledger. M1 measures joint eligibility and realised regime lengths and assigns incremental savings against each predecessor, including verification, formation, linking and frontend overhead, with per-window estimates for loading tails and racing. G1 and G2 stay measured gates: G1 is plausible through B but unproved; native emitters are D-188's commitment for G2, not a proven necessity.
10. **C track.** "Stall-bound" shows diminishing returns, not that no rate can meet D-163. C-0 is a reconciled CPU-time ledger with explicit overlap rules; fixed-stream replay is labelled conditional sensitivity evidence; FPS predictions come only from closed-loop execution of separately identified experimental timing models. The C review stays on the decision path without presuming a contract change. The production rate is chosen from hardware and product evidence.
11. **Dependencies.** The R3-completion writer audit gates only entry-only verification and any immutable-backing assumption that needs it; per-instruction RAM verification proceeds under its stated proof. D-189 already authorises parallel production work. The frozen performance anchor and the correctness oracle are recorded separately per revision, with the authorised functional differences as R3 continues.

**Before B-0 (rulings, recorded with B-0's work order):** the block key with the full PC VA (or a VA-independent IR, as amendment 1 allows), IR semantics and terminators, the phase projection and resumption rules, the verification policy, host-state exclusions and lifecycle bounds, cheap-observer integration, and the quota policy (the 200-instruction quota stays fixed unless re-proved; the rate-dependent backstop is derived separately). R-e, R-l and the functional-access R-4 are ruled with their consuming slices. **Founder item F-d** (whether R4 waits for G1) is asked before G1.
