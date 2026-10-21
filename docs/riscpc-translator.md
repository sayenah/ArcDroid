<!-- SPDX-License-Identifier: GPL-2.0-only -->

# RiscPC faster CPU translator (roadmap item 6b) — evaluation and plan (D-133, D-141)

Status: **evaluation, research and design only** (D-141, amended by D-142 after the S58 Astra challenge). No translator code lands before R4 qualifies the RiscPC core and the release baseline (item 5) is done, unless the Founder reorders. Private evidence: S57 scratchpad `s57/6b/` (the evaluation report and a fork clone at `94f7c9a`).

## Baseline and target
- **Today (Odin3, R2a interpreter):**
  - 9.8–10.0 host ns per guest instruction on cache-hot code;
  - p95 ≈ 1.0 T at the 100 MIPS SA-110 ceiling;
  - gameplay about 67 % of a frame (D-131).
- **Native frame rates for the heaviest 3D titles:** these need about 200 MIPS (a 202 MHz StrongARM). Inside the 0.80 T budget that means about 3.3–4.0 host ns per instruction.
- **Hard 100 MIPS caps in code:** they must be lifted before any higher rate:
  - `rpc_session.c`: the instructions-per-field fatal and the test-rate check;
  - `rpc_mem_timing.c`: rate > 100 is rejected.

  The 200-instruction quota in `rpc_session.c` is a bounded batch size, not a rate cap, and can stay at higher rates. A rate change must recalculate the per-entry instruction backstop and audit admission and arithmetic limits, without enlarging batches unnecessarily. The timing arithmetic (D = 28·rate) works at any rate.

## Prior art (verified)
**RPCEmu 0.9.5 dynarec** is x86-only and experimental. It counts only at branches, checks IRQs only after memory instructions, uses whole-page invalidation, and calls `exit(1)` on `mprotect` failure. Not reusable.

**`rpcemu-extended`** (GPL-2.0-or-later):
- **History.** Its arm64 JIT landed on 2026-07-22. The first boot on real arm64 came only after fixes on 2026-08-23, and a flush storm was fixed on 2026-08-26.
- **Provenance.** It is largely AI-assisted, unreviewed code.
- **Fit.** Its memory path bypasses ArcDroid's checked translation (forbidden since R1), and its timing is per block.
- **Use.** Reference only: encoder helpers, flag mapping, word-size block keys, and its defect history as a test checklist.

**Other translators — ideas, not code:**
- QEMU icount: budget check at block entry, exact fallback.
- melonDS: per-block cycle folding.
- PPSSPP's IR interpreter: a threaded-interpreter precedent.
- Rejected: Dolphin-style fault-based fastmem, because of containment.

## Constraints a translator must keep
1. **R2a timing, exactly:**
   - base cost per instruction;
   - every fetch, load and store through the timing caches, with permission before hit;
   - write-buffer drain;
   - SA-110 clock state;
   - event precedence.

   Calibrations and goldens stay byte-identical.
2. **Determinism and exits:** a folded end time that precedes the next known event is necessary but not sufficient, because R2a §5 makes each instruction's effects atomic at its issue time and MMIO can create a nearer deadline or raise an interrupt mid-block.
   - **Exit contract.** It defines exactly what is materialised before each helper call, side exit and event: the executed-prefix count, PC, flags and banks, the issue fraction, timing requests, counters, pending service and bus state. A guard miss resumes at the right instruction without repeating its fetch, its stores or its base charge.
   - **Exact path first.** Multi-transfer, MMIO and other uncertain instructions stay on the existing exact path at first.
   - **Interrupts.** Already-pending IRQ/FIQ and every instruction that changes interrupt masks or execution state are guarded, including the 26-bit P-form comparisons that `arm_compare_rd15` handles.
   - **Lazy bus drain.** It needs chronological catch-up before bus-dependent work, timing-register changes, observations and device arbitration. At equal visible nanoseconds the order stays devices → service/release/grant → CPU, including a newly queued video hold. Bulk subtraction of elapsed time is not an equivalence proof.
   - **I-cache generation guard.** It must certify that every fetch line is resident. On ARM610/710/7500, data accesses share that cache and can break the proof mid-block. SA-110 data-hit filters keep dirty-half bookkeeping, retained physical write-back addresses, ROM write suppression and functional stores.
3. **MMU:**
   - `checked_translation()` (`engine/rpcemu/src/cp15.c`) uses one direct-mapped 256-entry cache for instruction **and** data translations. A data access can evict the current fetch entry with no CP15 or mode change, and the interpreter's next fetch then re-walks the tables. Block guards must preserve that replacement and refill behaviour, including collisions caused by data accesses; a generation advanced only by explicit TLB maintenance is not enough.
   - Hoisting fetch permission to once per block changes R1's every-fetch checking contract, so it needs a Fable ruling **before Phase 1**, not only before Phase 2.
   - Block construction must not make speculative fetches that change live TLB state, timing tags or faults.
   - A data-side permission filter reopens R1's no-host-pointer-cache rule and needs its own ruling. Its key includes the effective access privilege (LDRT/STRT), the direction and the applicable control state.
4. **Self-modifying code:**
   - Code dependencies are keyed by **backing allocation plus offset/page**, owned by the session. `mem.c` masks addresses into allocations, so RAM and VRAM mirrors (for example physical `0x10000000` and `0x12000000`) are the same bytes, and raw physical-page keys would miss them.
   - Every successful backing-memory mutation invalidates the affected blocks: inline cache-hit stores, byte, halfword and multiple stores, device writes such as floppy DMA, and any later observer intervention. The IDE in R2c is PIO-only, so its writes arrive as CPU stores; audit the actual R2b and R2c writers when they land.
   - Invalidation happens at the functional write, not at write-buffer completion. The current instruction finishes atomically, keeping partial-transfer and abort behaviour, and then exits before any stale instruction runs. Host code that is executing is never reclaimed immediately.
   - Guest cache maintenance never flushes translations; it invalidates timing-residency guards separately.
5. **26/32-bit:** blocks are keyed by word size and `r15_mask`.
6. **Containment (D-137):**
   - no signal handlers, no fault-based memory, bounds-checked emission;
   - an interpreter fallback per session;
   - on Windows, unwind tables if JIT frames ever exist.
7. **Android executable memory:** AOSP app policy allows `execmem` for JIT compilers, but OEMs can add policy, so the deployed RetroArch process must be probed. A dual RW/RX mapping is preferred; an automatic RWX fallback is not, and the interpreter is the fallback.
   - Code and metadata storage are bounded, and so is compilation work per frontend entry. Changing working sets can cause repeated compilation and frame spikes, so "8 MiB is ample" needs measurement.
   - A failure to allocate or publish code may disable acceleration at a safe boundary. A guest or session fatal still ends the session; it never turns into "retry in the interpreter" after effects have happened.

## Options (Odin3, cache-hot, estimated)
| Option | ns per instruction | Notes |
|---|---|---|
| (c) block-structured pre-decoded interpreter, C only | 5–7 | Portable, including Windows. Proves blocks, guards, invalidation and folding. About 150 MIPS at about 1.0 T |
| (a) block JIT calling the timing service on every access | 3–5 | Memory density bounds it |
| (b) guarded arm64 fast path for proven cache-hot blocks, interpreter fallback | 1.5–3 hot | Needs timing-contract amendments (lazy bus catch-up, I-cache generation check, data-line filter) with equivalence proofs |

These ranges are research targets, not predictions (Astra S58):
- **The hot workload is a poor proxy.** `tools/rpc_hot_workload.py` repeats one warmed branch with no data access or video DMA, so a branch-terminated translator would see one-instruction blocks.
- **Some savings already exist.** The interpreter already has a fetch memo, a cached instruction pointer and an integral-batch timing shortcut, so pre-decoding does not automatically remove the costs option (c) assumes.
- **Option (a) still pays timing work on every instruction fetch,** not only on loads and stores.
- **Headroom at 200 MIPS.** If device work costs 0.10–0.15 T, the 0.80 T budget leaves 3.25–3.50 ns per instruction.

## Plan
- **Phase 0 (now, evidence only).**
  - **Profile on the Odin3 and on Windows.** Workloads: the branch fixture; multi-instruction ALU, condition and shift blocks; warm loads and stores; buffered streaming; cache and TLB conflicts; MMIO and device workloads; representative R2a gameplay.
    - Measure host cost split by translation, dispatch, timing, bus service and devices.
    - Measure guest instructions and useful work per emulated second.
    - Measure the block-length distribution, likely fast-path coverage, fallback frequency, code invalidation and churn, and cold-start against sustained frame tails.
    - Record the exact device and SoC (the Odin3 is the Snapdragon 8 Elite device, D-017), performance settings, temperature, toolchain and binary identity.
  - **An executable-memory probe inside the real RetroArch on the Odin3.** It emits through RW and executes through RX, repeatedly replaces code at reused addresses, does the required cache synchronisation, and calls a C helper and returns with ABI state intact. It includes an injected helper-to-session-fault unwind, and records Android and RetroArch versions, runtime page size, mapping failures and teardown. If the probe copies or adapts any fork code, Q3 must be answered first.
  - **Draft timing-contract amendments.** They cover the exit contract and guard invariants (constraints 2–4).
  - **This Astra challenge.** Done S58: ACCEPT WITH CHANGES, all incorporated here (D-142).
  - **Progress (S59, S60).** Done: the executable-memory probe, a first Odin3 profile, the guard and exit invariants (accepted as the Phase-0 basis in `docs/riscpc-translator-invariants.md`, D-146), and the Windows profile (S60). Open: representative gameplay, block-shape and churn measurements, and the Q-3 oracle fix. Results are under *Phase 0 evidence* below.
  - **Outputs:** accepted guard and exit invariants, a workload-based cost model, the smallest justified implementation scope, and a Founder decision on the design objective (Q1). Performance is re-baselined against the integrated R4/item-5 core before implementation.
- **Phase 1 (after R4 and item 5).**
  - **First,** exact interpreter optimisations that the profile identifies.
  - **Then** a minimal shared block and exit foundation (option (c)) on every platform. It is not a second elaborate executor, and missing a microbenchmark target does not by itself disqualify a later JIT.
  - **Gates:**
    - byte-identical calibrations and goldens;
    - the frame gate: p95 ≤ 0.80 T and no frame over T, including compilation and fallback cost.

    ≤ 7 ns hot on the Odin3 is a microbenchmark target, not the gate: at 100 MIPS, 7 ns alone is 0.70 T.
- **Phase 2.** Option (b), ArcDroid-owned code, arm64, behind a core option.
- **Phase 3.** A rate change, only on a Founder decision.

**Verification.**
- **Differential testing against an independent oracle.** The original interpreter stays the oracle throughout qualification. Shadow mode cannot replay general MMIO against already-mutated devices: reads consume FIFO data, and writes change IRQs, schedule events or commit media.
  - Use isolated deterministic executions with identical inputs, or a complete snapshot and restore of everything the slice touches: shared TLB, scheduler deadlines, and device and controller state.
  - An undo-log shadow is limited to ordinary-memory blocks at first; MMIO and device transitions are checked by independent replay.
- **Instruction-level coverage.** Block-exit comparison is block differential testing. Controllable exits at every instruction position add instruction-level coverage, alongside ordinary multi-instruction execution. Comparisons use canonical state fields after materialisation, never raw host structs.
- **Coverage counters.** They prove that native execution, guards and every fallback reason ran, so that "translator forced on" cannot pass by falling back throughout.
- All timing calibrations and `rpc-*` goldens byte-identical with the translator forced on.
- CI running the RPC suite in both modes.
- Sequence fuzzing: MMU, modes, self-modifying code, DMA, IRQ edges, full write buffers.
- Mutation testing.
- Private seven-pair boot, factory defaults, gameplay replays, the 44-title A/B, and a 30-minute thermal leg.

## Phase 0 evidence (S59, private scratch probes, never Sol sources)

### 6b-P0i instrumentation design (S64)

`ARCDROID_RPC_PROFILE` defaults OFF. Only `arcdroid_rpcplan_libretro_rpc`,
the non-installed runner and dedicated authored profiling fixtures compile it;
the shipping core and other interpreter tests never acquire the definition.
Hooks in the shared interpreter source are compile-time empty when OFF. The
inactive/observed/cheap selection, observer helpers, quotas and service order
are unchanged. No hook reads guest bytes beyond the actually fetched opcode,
walks translations, flushes batches, raises faults or performs host I/O.

Counters count **attempts**, including failed conditions and prefetch aborts.
Successful fetches have mutually exclusive classes: ALU, multiply, branch,
scalar load/store, multiple load/store, swap, half/signed transfer, status,
coprocessor, SWI and other. Classes include failed conditions; a separate
condition-passed count distinguishes executed bodies. Coverage is conservative:
condition-failed instructions and ALU/multiply/branch/scalar memory bodies are
candidates only when they encounter no barrier. It is a scope estimate, never
proof that a guard can be hoisted or a memory access accelerated.

A dynamic run includes its terminating attempt and carries across interpreter
batch/frontend returns. A taken branch or executed PC destination ends E1
(counted separately); slow classes end E3; mode/mask changes, idle and actual
physical MMIO end E4; actual IRQ/FIQ, data abort, prefetch abort and SWI/Undefined
end E6/E7a/E7b/E10; a successful write into a recently executed backing page
ends E9. E11 counts a condition-passed CP15 c7 write whose tuple is classified
as cache maintenance by the profiling hook: every c7 write on ARM610/710/7500;
on SA-110, `(opc2,crm)` is `(0,5/6/7)`, `(1,6/10)` or `(4,10)`.
This is an instruction count, independent of whether timing recording is bound,
a targeted cache line exists, or the timing model performs work. It uses the
CP15 CPU model, not the timing model's acceptance decision. Retaining this
definition keeps schema-1 measurements comparable; it is not a count of
effective invalidations, castouts or drains. Causes may overlap on one
attempt: each cause counts once, but the length histogram closes once. E5 counts
actual batch returns (quota, service/deadline, idle/test stop); it does not truncate
the shape histogram. E2 is explicitly unavailable: no translator guard exists.
E8 counts the existing session fatal and abandons the unfinished attempt through
the existing longjmp, without adding a boundary. Histogram buckets are exact
lengths 1..64 plus >=65, with total lengths and maximum retained; the open suffix
is reported separately. Reset/stop suffixes are censored, with their counts and
lengths separate from the complete-block histogram. Fetch/data/MMIO accesses, scheduled device deliveries,
cache fills/memo hits and bus-service calls are also recorded. `bus_calls`
counts entries into both `arc_rpc_mem_batch_service` and `arc_rpc_mem_run`,
including the nested `batch_service` → `run` entry. It is not a count of unique
bus transactions or an exclusive service classification.

Code churn uses 4 KiB **allocation-offset** pages (ROM, RAM0, RAM1, VRAM), so
mirrors alias. A successful fetch marks a page; a functional RAM/VRAM word or
byte store counts as recent when that page was fetched within the preceding
1,000,000 attempts (including the current attempt). Ignored ROM/unbacked stores
and denied stores do not count. Distinct written recent pages and write operations
are separate; STRH counts its two functional bytes, STM each completed word.
Lifecycle clears page history before allocations can be reused; counters remain
cumulative until runner configuration, with reset/fault counts exposed.

Host cost uses deterministic **scoped sampling**, default stride 1024 (0 means
counts only, 1 times every attempt/service). Sampled CPU attempts split exclusive
wall-clock nanoseconds into fetch translation, dispatch/body, timing model,
bus service and MMIO; independently sampled driver bus-service and device
callbacks cover work outside attempts. Class/block bookkeeping is separately
attributed to `profiling`; minor hook/wrapper costs remain in their enclosing
buckets. Nested scopes charge their child once.
Dispatch includes data translation and ordinary memory effects. Samples include
clock/profiling overhead, reported without subtraction; `sampled_ns * stride`
is an estimate, susceptible to periodic-workload bias, not guest timing or an
exact cost ledger. Reports include sample counts, clock reads, charged probes
per bucket and an 8192-iteration empty-charge calibration (outside execution).
For an approximate bias check, subtract `probes * calibration_ns / 8192` from
raw sampled ns, clamp to zero, then scale by stride. Raw values are preserved;
calibration is not an exact correction and low residuals are unresolved costs. Compare
strides and unprofiled `--timing` frame costs before drawing conclusions. Timers
use the existing test monotonic clock, never guest time. A fatal abandons the
unfinished scope; already recorded sample fragments stay visible and an
abandoned-sample counter marks incomplete cost evidence.

Runner options (profile builds only): `--profile FILE`,
`--profile-interval FRAMES` (default 60), `--profile-sample-stride N` (default
1024, 0..1048576). The sink opens after preparation, before requested frames; writing occurs only
after protected execution returns. Open/write/close failures fail the runner, leaving
guest execution unaffected. JSON schema **2**, object `schema`, `sample_stride`,
`recent_attempts`, `records`: cumulative snapshots at the chosen frame interval,
each crossed emulated second (at the first returning frontend boundary), and
exit. Each record carries frame/guest ns, counters/classes/exits/block histogram,
open block length, coverage and cost samples, plus interval frame p50/p95/max
and summed core ns. Differences between successive cumulative snapshots give
per-second counts; first/last windows may be partial. Reset clock rewinds are
explicitly epoch-labelled. No opcode, guest address, register, ROM byte, path or
private title is emitted. Existing runner `--timing` provides individual frame
tails, so cold start and sustained windows can be compared without host pacing
feeding the guest. Build/run commands are in the
[profiling runbook](../tools/rpc-r1-runbook.md#6b-p0i-profiling-test-only).

**Measured overhead (S64 final code).** On the shared development MacBook Pro
(MacBookPro17,1, Apple M1, 8 CPU cores, 16 GB), macOS 26.6.2, Apple Clang
21.0.0, RelWithDebInfo, three alternating ON/OFF
pairs of the authored hot workload (601 frames, excluding frame 0) gave median
p50 **22.295 → 68.691 ms (+208.10%)**, p95 **23.664 → 93.435 ms (+294.84%)**,
and **13.667 → 44.631 host ns/instruction (+226.56%)** with stride 1024.
Each measured leg executed 1,000,000,020 instructions over 10,000,000,200 guest
ns. No competing test/performance jobs from the measuring Sol session ran;
ambient host load and thermals were uncontrolled (no load-average sample was
recorded). Individual pair p50 overhead ranged +139.65–208.10%; the first OFF
leg had noisy tails. Counts-only stride 0 still cost 67.377 ms p50, and an ON
build without a profile request cost 44.264 ms p50. Use OFF builds for headroom
and R-H evidence. These are Mac hot-loop measurements, not Odin3 predictions.
QA independently reproduced about +150–200% p50 on an 8-core shared Mac at
load about 7.5 (600 frames, stride 1024).

Tiny-scope clock bias is material: raw sampled ns scaled by stride summed to
298–311 s against about 45 s measured execution; empty-charge calibration was
23.80–39.55 ns/probe. Counts-only controls, alternate strides and unprofiled
frame timings are required when interpreting this cost split. Low calibrated
residuals remain unresolved costs. Private aggregate evidence stays local;
no private plan or guest bytes are needed to reproduce the authored hot leg.


**Executable memory inside RetroArch on the Odin3.**
- **Setup:**
  - Android 15, RetroArch 1.22.2, process domain `untrusted_app`, 4 KiB pages.
  - The probe was written from scratch, so Q3 is not engaged.
- **What works:**
  - A dual RW/RX mapping is permitted through both memfd and `ASharedMemory`.
  - 20,000 replacements at reused addresses with cache maintenance gave no mismatch, at about 46 ns per small publish and call.
  - Emitted code calling a C helper keeps x19–x28, d8–d15, x18 and sp intact.
  - An injected session fatal, raised by `longjmp` from the helper through emitted frames, unwinds 1000 out of 1000 times with x18 (the shadow call stack) intact.
  - 500 create/destroy cycles leak no mapping or descriptor.
  - Regions of 8 and 32 MiB work.
  - RetroArch unloads the core cleanly.
- **Design consequences:**
  - The preferred dual mapping is available, so no RWX fallback is needed. RWX was also permitted, but that was recorded only.
  - A W^X `mprotect` toggle costs about 40 times as much per publish.
  - Emitted code must never write x18.
- **Not covered:** a second Android device. Windows is covered by the S60 probe below.

**Interpreter profile on the Odin3.**
- **Setup:**
  - Binary: `main` `ee364d9`, a Release build with debug info.
  - Rate: SA-110 at 100 MIPS, flat pacing, 601 frames per workload.
  - Profiler: `simpleperf`, sampling cpu-cycles.
- **Workloads:** the committed hot and AC-8 workloads, plus scratch sustained loops: a 32-instruction ALU block, warm loads and stores, LDM/STM, streaming reads and writes, translation-cache conflicts, and an MMIO loop.

| Workload | Guest MIPS | Host ns/instr | p95/T | Execute | MMU | Timing | Devices |
|---|---|---|---|---|---|---|---|
| hot (branch) | 100 | 9.7 | 0.98 | 65 % | 33 % | 0 % | 0 % |
| 32-instruction ALU block | 100 | 10.0 | 1.01 | 55 % | 34 % | 9 % | 0 % |
| warm load/store block (63 % memory ops) | 100 | 18.2 | 1.83 | 40 % | 25 % | 22 % | 0 % |
| LDM/STM 8 registers | 4.4 | 163 | 0.72 | 6 % | 9 % | 71 % | 0 % |
| streaming reads | 25.9 | 19.1 | 0.50 | 30 % | 20 % | 40 % | 0 % |
| streaming writes | 19.2 | 32.1 | 0.62 | 16 % | 12 % | 63 % | 0 % |
| translation-cache conflicts | 5.6 | 31.9 | 0.18 | 17 % | 11 % | 65 % | 0 % |
| MMIO loop | 19.4 | 17.5 | 0.34 | 32 % | 23 % | 34 % | 2 % |
| AC-8 (video, audio, aborts) | 1.7 | 43.4 | 0.07 | 8 % | 6 % | 21 % | 54 % |

- **Instruction-fetch translation is a third of cache-hot cost.** `checked_translation` plus `arc_rpc_getpccache` take about a third even inside one page. This is the largest exact-interpreter target, and it sits on constraint 3, so the fetch-permission ruling must come before Phase 1.
- **Warm load/store-dense code misses real time at the 100 MIPS ceiling today** (p95 1.83 T). There, timing-model work is as large as MMU work.
- **Memory-bound code is dominated by the R2a timing service** (40–71 %). A translator that removes only dispatch gains little there.
- **Device work is small at this mode.** 640×480 at 8 bpp with audio DMA costs about 0.04 T. Higher modes are unmeasured.

**Invariants (accepted as the Phase-0 basis, D-146; `docs/riscpc-translator-invariants.md`).** These findings affect the verification plan:
- **The calibrations do not prove the production fast paths.** Their measurement mode forces slow instruction begin/end and disables integer batching (`tests/rpc_timing_test.c`, `rpc_mem_timing.h`); the memo and demand-service shortcut still run. Non-intrusive endpoint measurement is to be added (Q-2).
- **Interrupts.** They are taken at the next event check reached while they are still eligible. A prefetch abort skips that check.
- **The shared translation cache fills before the permission check,** so even a denied data access can evict the fetch entry. Guard-time walks are forbidden.
- **Only CPU stores write guest memory today.** Code invalidation hooks the physical write functions, keyed by backing allocation plus offset (RAM repeats twice per 64 MiB aperture).
- **Bus catch-up covers older work only,** never the current instruction's own requests.
- **Oracle defect (Q-3).** The fetch memo is keyed by virtual line only and can outlive a change of eligibility after a translation-cache re-walk. A separate small corrective work order fixes it after R2c merges, before any translator baseline.

**Interpreter profile on Windows (S60).**
- **Setup:**
  - Binary: `main` `211c9a7` (code identical to the Odin3 run's `ee364d9`), a Release build with `-g`, cross-built on macOS with MinGW-w64 GCC 16.2 against UCRT. The shipped CI build uses MSYS2 UCRT64 GCC, so the compiler version differs.
  - Host: the Founder's Windows box, a Ryzen 7 7800X3D (8 cores, 64 GB), Windows build 26300, High performance power plan.
  - Workloads: the same ten as the Odin3, with the same bytes (all hashes checked against the Odin3 copies). Same rate, pacing and 601 frames.
  - Timing: three unprofiled runs per workload. The table gives the median ns per instruction and the p95 range.
  - Profiler: a scratch sampler that suspends the runner's thread and reads its instruction pointer about 4,000 times a second, then symbolises against the MinGW symbol table. Sampling slows the run by about 10 %; the timing columns come from the unprofiled runs.
- **Cross-platform equality holds.** On all ten workloads, the guest instruction counts and the runner traces are byte-identical to the Odin3.

| Workload | Guest MIPS | ns/instr, Windows (Odin3) | p95/T, Windows (Odin3) | Execute | MMU | Timing | Devices |
|---|---|---|---|---|---|---|---|
| hot (branch) | 100 | 7.5 (9.7) | 0.77–0.78 (0.98) | 67 % | 31 % | 1 % | 0 % |
| 32-instruction ALU block | 100 | 9.3 (10.0) | 0.94–0.96 (1.01) | 62 % | 30 % | 7 % | 0 % |
| warm load/store block | 100 | 15.9 (18.2) | 1.60–1.63 (1.83) | 41 % | 33 % | 20 % | 0 % |
| LDM/STM 8 registers | 4.4 | 159 (163) | 0.70–0.71 (0.72) | 5 % | 12 % | 74 % | 0 % |
| streaming reads | 25.9 | 17.9 (19.1) | 0.47–0.48 (0.50) | 37 % | 22 % | 37 % | 0 % |
| streaming writes | 19.2 | 31.5 (32.1) | 0.61–0.63 (0.62) | 18 % | 13 % | 64 % | 0 % |
| translation-cache conflicts | 5.6 | 29.8 (31.9) | 0.17 (0.18) | 21 % | 12 % | 62 % | 0 % |
| MMIO loop | 19.4 | 15.7 (17.5) | 0.31–0.33 (0.34) | 40 % | 26 % | 28 % | 2 % |
| AC-8 (video, audio, aborts) | 1.7 | 43.6 (43.4) | 0.08 (0.07) | 8 % | 9 % | 25 % | 50 % |

- **The Windows desktop gives little more headroom than the Odin3.** Its cost per instruction is 0–23 % lower (AC-8 is level), so the shape of the problem is the same on both platforms.
  - Warm load/store-dense code misses real time on Windows too (p95 1.60 T at 100 MIPS).
  - Cache-hot ALU code uses 0.94–0.96 T, leaving no room for devices or a higher rate.
  - This bears on Q1 and Q2: neither platform can fund a higher ceiling alone, and the portable path has to help both.
- **Instruction-fetch translation is again about a third of cache-hot cost** (30–33 %). GCC inlines `checked_translation` into `arc_rpc_getpccache` on x86-64, so the cost appears under that one symbol.
- **Memory-bound code is dominated by the R2a timing service** (37–74 %), as on the Odin3.
- **Devices** cost about 0.04 T in the AC-8 mode on both platforms.
- **Run-to-run spread is small:** p95 varies by at most 0.03 T across three runs.
- **Not covered:** the native RetroArch frontend (the runner was used).

**Executable memory on Windows (S60).** A standalone x64 probe, written from scratch (Q3 not engaged), was run on the same box.
- **First attempt blocked.** Smart App Control refused the unsigned probe executable (CodeIntegrity events 3033/3077/3118), while the unsigned runner and plan core from the same toolchain were allowed. It was not worked around. The Founder then switched Smart App Control off and restarted the machine, and the probe ran.
- **What works:**
  - Dual RW/RX views of one pagefile-backed section (`CreateFileMapping` plus two `MapViewOfFile` views).
  - 20,000 replacements at reused addresses with `FlushInstructionCache` gave no mismatch, at about 19 ns per publish and call. A `VirtualProtect` W^X toggle costs about 2.7 µs, roughly 140 times as much.
  - Emitted code calling a C helper.
  - `longjmp` from the helper through an emitted frame works 1000 out of 1000 times when the frame's unwind data is registered with `RtlAddFunctionTable`. `__builtin_longjmp`, which does not unwind, also works 1000 out of 1000 times.
  - 500 create/destroy cycles of 8 and 32 MiB regions leak no handle.
  - RWX allocation was also permitted; that was recorded only.
- **Design consequence (Windows x64):** the CRT `longjmp` unwinds frame by frame. Through an emitted frame with no registered unwind data, the process is killed at once with `0xC00000FF` (bad function table), and no exception filter runs. Any x64 backend must therefore either register unwind data for every emitted frame that a helper can unwind through, or have helpers that leave emitted code use a non-unwinding exit.
- **Release note:** with Smart App Control on, an unsigned ArcDroid-built executable was blocked (see the handoff's item 5 notes).
- **Not covered:** inside RetroArch on Windows (RetroArch is not installed on the box).

**Representative gameplay (S65, 6b-P0i `1bb22aa`).** 031 from a cold start into racing gameplay (the S61 AC-10 inputs), 8000 frames, flat pacing, 3.71 SA-110 at 100 MIPS, on the Odin3 and the Mac. Profiling legs at sample strides 1024 and 1021, and two unprofiled legs on the Odin3 before and after. Guest output and the frame-7000 image are byte-identical across every leg and across both platforms. Private evidence: `.work/s65/p031/`.
- **Instruction mix:** 9.45 × 10⁹ instructions in 151.8 guest seconds (62 MIPS executed on average; the rest is idle). ALU 49.7 %, loads 21.7 %, branches 21.0 %, LDM/STM 4.1 %, stores 2.2 %, multiplies 1.3 %, status/coprocessor/SWI under 0.2 %.
- **Blocks are short.** Dynamic straight-line runs average 4.1 instructions (median 3, 90th percentile 8, 99th 26, longest 193). Only 37 % of instructions sit in runs of 8 or more, and 12 % in runs of 16 or more. Taken branches and PC writes (E1) end 197 runs per 1000 instructions.
- **Exit causes per 1000 instructions:** E1 197, E3 (helper call inside an instruction) 39, E4 (side exit) 10.4, E9 (store into a recently executed page, page granularity) 7.7, E5 6.0, E10 0.3, E11 0.2. 94.4 % of instructions are candidates for a minimal block foundation; the fetch memo hits 84.4 %.
- **Code churn is high at page granularity.** 0.9 % of instructions write into a page executed within the last 10⁶ instructions. Invalidation keyed by 4 KiB page would thrash; a translator needs finer-grained (allocation plus offset, D-142(3)) or data/code-separated invalidation, and the churn has to be re-measured at that granularity before any cache design.
- **Host cost split (sampled, probe-bias corrected, profiling bookkeeping excluded):** timing service 51 % (Odin3) / 58 % (Mac), fetch translation 16 % / 17–20 %, dispatch 16 % / 15 %, bus service 16 % / 6–9 %, devices under 1 %. Both strides agree within a few points. With profiling on, small scopes are probe-dominated; read the split as a ranking, not a ledger.
- **Odin3 frame cost, unprofiled (T = 19.97 ms, the 50 Hz field).** Two runs agree within 1 %: start-up 32.0 ms p50; loading and menus 30.4 ms p50, 56.3 ms p95; sustained racing (frames 6000–8000) **29.5 ms p50 (1.48 T), 34.0 ms p95 (1.70 T)**, about 28.5 host ns per executed instruction. The AC-10 frame gate (p95 ≤ 0.80 T) needs about **2.1×** on sustained gameplay, and more on the loading tails.
- **What this means for the plan.**
  - Exact interpreter work has to attack the timing service first; it is half of the host cost on the Odin3.
  - A block foundation on its own covers little: with runs of four instructions, block entry and exit overhead is paid every few instructions. Its value is in carrying timing and fetch work across a run, not in dispatch alone.
  - The S64 Phase 1a candidate set (exact optimisations, about 20–25 % on 031) is consistent with this, and confirms that Phase 1a alone cannot reach AC-10.

**Still open in Phase 0** (the Q-3 oracle fix merged earlier as `44d007e`; later 6b work is planned in `docs/riscpc-6b-phase1a.md` and `docs/riscpc-6b-plan.md`, D-191):
- a 3D title, profiled the same way;
- code churn at allocation-plus-offset granularity;

## Founder decisions
- **Q1, design objective — answered S65 (D-188): capacity for a ceiling of about 200 MIPS.** Native backends on arm64 and x64 and a cycle-timing review follow Phase 1a. The original question: Is the translator designed for headroom at 100 MIPS, or for the capacity to support a ceiling of about 200 MIPS? This decides whether the portable path is enough and whether a Windows backend may be needed. The actual rate stays a Phase 3 decision.
  - A higher instruction ceiling is not a 202 MHz SA-110: R2a excludes pipeline and per-class cycle modelling. A cycle model would be a separate timing-contract decision. Doubling the ceiling cannot promise native frame rates for bus-limited workloads.
- **Q2, platform parity — answered with Q1 (D-188): native backends on both platforms.** Cross-platform trace equality is a hard requirement, so the question is how to meet one common guest rate on both platforms (the portable path, or a later x64 backend). Per-platform rates are outside the current contract.
- **Q3, the fork — before the first copied or adapted helper, including in disposable probes.** Reference only, or selective copying of `rpcemu-extended` helpers (GPL-2.0-or-later, compatible with the project's GPL-2.0-only distribution).
  - If copying is chosen, record the exact commit, files and functions, dependencies, original notices and modifications through the existing provenance machinery. Every copied encoder gets independent encoding and bounds checks: several fork helpers mask operands rather than reject invalid ranges.
  - melonDS is GPL-3.0-or-later and stays conceptual reference only.
- **Q4, default — at qualification.** Fallback behaviour is defined earlier. Whether the translator is on by default, and the kill switch, are decided from evidence.

## Slice 1a-0 measurement design (D-195, amended S66)

This section freezes the implementation contract before executable edits.
All new instrumentation is authored under SPDX-License-Identifier: GPL-2.0-only.
The retained intrusive mode remains the independent calibration oracle.

For instruction ordinal k let I(k) be its exact issue tick and C(k) its
completion tick: I(k) for a request-free instruction, otherwise the ordinary
`commands()`/committed-shortcut completion tick. C(0)=0. With tick denominator
D=28×rate, visible(t)=ceil(t/D). An interval [f,l] has start=C(f−1),
non-drained end=C(l), and drained end=max(C(l),R), where R is the retirement
tick of the final command/buffer packet still owed at C(l). Empty queues at
C(l) give end=C(l). Video holds can separate C(f−1) from I(f); subtracting a
base from I(f) is not a start-time formula.

Capture phases are: preserve C(f−1) on its ordinary completion; sample start
statistics at admission of f (after prior service, before f's fetch); preserve
C(l) on completion; and, if needed, sample end on final packet retirement.
Fast request-free completion is projected at the ordinary batch return using
the existing integer materialisation formulas or the rational issue tick.
No capture calls `batch_flush`, `next_base`, service or translation. Interval
statistics project instructions and completion tick without writing canonical
statistics. All fields of both interval records are compared, not just elapsed
time. Measurement state includes the mode, bounds, drained flag, saved prior
completion, pending end and both interval snapshots. It has a separate explicit
comparison; the raw inspector snapshot remains unmasked.

The stop mechanism is R-e0-1(i): bound admission at f−1 and l while armed.
The common interpreter entry wraps all three instantiations and the NULL-batch
path. Existing driver ready/next gates stop subsequent issue at l while service
continues to drain. Admission of f captures its start; return after f−1 or l
captures request-free completion. Unarmed admission changes no quota. The
proof compares the unarmed instruction-loop disassembly/instruction count with
the pinned base, in addition to shipping object sections and the three separate
AC-1 comparisons. Admission/return work is outside the instruction loop. A
flag branch newly executed on every unarmed instruction is not accepted proof.

Late arming refuses without changing measurement state when `first > 1`,
`first - 1 == instructions`, and the preceding instruction is no longer
pending: its unarmed completion was not retained. A pending predecessor can
still supply its exact completion; a more distant first instruction is also
accepted. This prevents accepting a window that would fault at next admission.

The macro split retains `ARCDROID_TEST_FAULTS` as the hot-hook part and adds
`ARCDROID_TEST_API` for test APIs. Ordinary TEST_FAULTS implies the API part.
`ARCDROID_RPC_MEASUREMENT` selects the API/plan-only measurement target.

| Build | API/plan | Hot test hooks / exercise | Ranking | Cross-check | Frame timer / event diagnostics |
|---|---|---|---|---|---|
| Shipping | no / no | no | no | no | no / no |
| Ordinary test/calibration | yes / no | yes | no | no | yes / yes |
| Plan | yes / yes | yes | optional profile option | optional | yes / yes |
| Profile/cross-check | yes / target dependent | yes | profile only | cross-check option only | yes / yes |
| Measurement (non-installed) | yes / yes | no | no | no | yes / yes |
| Lockstep (non-installed, non-Android) | yes / yes | yes | no | no | yes / yes |

Lockstep alone reports capability bit 32 and accepts the stream sink. Other
test cores export explicit refusing stubs. The measurement identity exception
is limited to those stubs in `rpc_session.o` (M2b R-M2b-S1); shipping exports
and hot objects remain unchanged.

Measurement has shipping optimisation, visibility and shared hot layouts.
The four named CPU/timing/translation objects match shipping. The exceptions
outside that set are session event counting and the observer API differences
listed below. Intrusive and
endpoint measurements, observer trace, quota clamp, allocation injection and
exercise/ranking counter requests refuse explicitly. Runner capability checks
produce a nonzero exit naming the absent facility. Production observer
subscription and the frame timer remain functional. Ordinary targets retain
their existing APIs and forced-slow request-level tests.

Exercise counters live outside both timing structs and reset at timing bind,
including unbind before an early return: reset/stop/unload/failed start/rate
rebind and backend switching all retire the timing binding. New ranking counters (schema 2) reset at each G-13 timing binding and report
`ranking_epoch`, current-binding `ranking_attempts`, and successful-fetch
`ranking_fetched`. AL resets with those counters. Legacy P0i fields retain
their cumulative reporting and their existing lifecycle epoch/reset semantics. Neither class participates in guest
or measurement comparisons. No counter reads guest memory or performs I/O.

Counter units are unsigned integer occurrences (probes count ways the linear
reference would examine,
fills count packets, instructions count attempts including aborted fetches).
Rates use the reported instruction-attempt denominator; ratios retain numerator
and denominator separately. Mean probes/hit uses hit-probes/hits, never all
probes/all lookups. Counter families and increment points:

- Lookup: access and c7 maintenance callers separate; one empty return, searched
  miss or hit per call; hit-way histogram indexed from zero; total probes and
  hit probes. Empty returns have zero probes, a searched miss has `ways`, a
  way-k hit has k+1. Beyond-way-zero hits are L1 opportunities.
- Service: classify once at batch-service entry, before mutation. Exclusive
  precedence is pending, video active, valid finish with due≥visible(finish),
  S1-eligible, other busy, other. S1 is nested within busy-not-pending and
  requires !pending, !video_active, cycle_active (valid finish), and strict
  now<due<min(visible(finish),next event,frontend limit). Invalid finish is never
  used in a due comparison. Report valid/invalid-finish entry counts separately.
- Fills: increment on actual fill allocation, by fetch/data and memory/ROM.
  Shortcut attempts increment on admission to prediction; commits only after
  its boundary test succeeds; rollbacks after failed prediction. B3 counts
  memory fill packets in committed shortcuts, never predicted/rolled-back fills.
- TLB: at checked translation entry distinguish tag hit from walk, split by
  fetch/data. On successful permission return count a hit opportunity only if
  the entry hit on admission and no abort is pending; denied installs are walks,
  not hit opportunities. A tag hit denied by DACR/AP or pending abort remains
  only a raw tag hit. MMU-off accesses are neither walks nor tag hits.
- Integer instructions: count each integral prefix at its existing flush;
  rational continuations: successful non-service, non-integer continuation
  edges, aggregated at batch return from executed prefix, integer prefix and
  successful service continuations. Keep the three instantiations separate.
  This adds no increment inside the instruction loop. Counts inside endpoint
  windows are differences of snapshots taken at the interval boundaries. The
  interval exercise getter returns false for intrusive windows; cumulative
  exercise getters remain available there.
- Profile AL: increment on a successful fetched opcode whose condition is AL,
  regardless of condition result; denominator is successful fetches. Existing
  condition-passed/attempt and fetched/attempt counts remain available.

Cross-check is an opt-in, test-hook-only field comparison/reporting contract.
It counts comparisons and mismatches, names a mismatching field on stderr and
makes a mismatch fail. Its only 1a-0 client is an authored self-test; isolated
reference execution for future paths belongs to their own slices.

The implementation adds `arc_rpc_test_capabilities` so the runner never queries
absent trace diagnostics as if they were available. Unsupported direct trace
APIs latch a refusal too; ordinary measurement-plan cleanup does not request
those APIs. The final test target retains frontend test APIs while its forced
RPC namespace header clears TEST_FAULTS and retains TEST_API. The four named
CPU/timing/translation objects therefore have the shipping preprocessor shape;
this is not a claim about every RPC object.

Run C observer comparison caveat (AC-9): `rpc_observer.c` appends API-only
functions without changing the production source (host QA found those existing
functions instruction-identical).
`rpc_observers.c` retains API-controlled policy and reset restoration, so its
object does not match shipping. The test-only `state.overridden` member shifts
the offsets of `inserted`, the callbacks/opaque pointer, resolved candidate,
retry/suppression fields and copied retry work. Event handlers and frame/status
code addressing those members use the shifted offsets. In `error_event` and
`disc_event`, the watch/renew budget is loaded from mutable `watch_budget`
instead of using shipping's constant 1,000,000,000 instruction attempts (the
`test_watch_budget` API causes this; it is not the launch time horizon).
The separate `test_horizon` API can redirect `state.launch.profile` to a mutable
profile for `arc_outcome_frame`/launch handling. Bind/override/rebase retain
that API state and reset restoration; rebase adds save/restore work per reset.
These differences add no per-instruction hook, but observer-event code is not
shipping-identical. With the current single-translation-unit ownership, full
identity would require an API/state-boundary redesign, removing retained policy
APIs, or changing shipping state/reset code. Run C keeps those APIs and the
AC-5(a) shipping identity and uses the authorized documentation fallback: AC-9 measures this
same measurement core with production cells versus `observer_cells=off`, and
records these per-event differences as a caveat, not as shipping-object proof.

`observer_cells=off` explicitly disables all eight cells on every firmware/CPU
pair, including a qualified pair, and logs `RPC observers: TEST OFF; all cells
disabled (observer_cells=off)`. It applies at attachment and after reset.
An absent key or numeric `observer_cells=0` retains production qualification;
a nonzero numeric mask retains the existing candidate override semantics.
The same setting is supported by plan and measurement cores. Observer trace
and `observation_profile=unmatched` are separate diagnostic subscriptions;
omit both for the unobserved AC-9 leg.

`rpc_measure_compare.py --test-loops` checks R-e0-1 on `arcdroid_libretro_test_rpc`,
which contains the endpoint mechanism: the three preprocessed loop bodies and
their reachable local/inline helpers must match the pinned base. The single
`arc_rpc_mem_exercise_begin` call stays in the common dispatch wrapper before
all three instantiations. Each emitted loop must have no more instructions
than the base and the same RPC relocation-reference multiset. The
committed guard self-test rejects added inline work, a new loop call and moving
batch entry into the instruction loop. Measurement/shipping object identity
alone does not prove this test-core property.

`--prefix-instructions N` captures an authored functional prefix at the normal
interpreter return. `--single-step-prefix` supplies the independent unarmed
quota-one reference. Comparison includes CPU/banks/mode, all RAM/VRAM, exact
issue tick and live scheduler deadlines; hashes stay on runner stderr. The
endpoint run intentionally stops afterward; the reference continues. This
diagnostic is outside canonical and measurement snapshots, is cleared on CPU
reset and allocation retirement, and is absent from the measurement core.

RPC inline headers apply the same hot-part condition in every translation unit
(`TEST_FAULTS && !RPC_MEASUREMENT`), so frontend/test-API users of those headers
see the same shared timing layout as the measurement RPC objects. Prefix
reference runs set the existing quota-one API; they introduce no additional
unarmed quota mechanism.


L1 counter ABI (D-200): revision 3 appends l1_hint_accepted, l1_hint_rejected,
l1_mirror_search, l1_mirror_recheck_failed and l1_linear_fallback. Acceptance
requires an in-range way and a valid equal line in the current cache/set.
Rejection includes uninitialised and seeded-poison hints. Mirror searches count
once per rejected hint with gates 2 and 4; linear fallbacks count nonempty gate-0
calls and rejected hints with gates 1 and 3. Gates 3 (production SA-110 way0-hint)
and 4 (way0-mirror) first compare way 0: a hit bypasses all five L1 path counters
and the hint store. On a way-0 miss they validate the hint as above; rejection
searches the remainder from way 1, using linear search for gate 3 and the mirror
for gate 4. Gates 1 and 2 search from way 0. Recheck failures are structural zeros
with the exact mirror; their diagnostic increment is tested by explicit white-box
corruption.
Run F leaves linear-gate side arrays at their bind-cleared values. Way0-hint
also leaves its unread mirror dormant and learns hints only from hits beyond
way 0; streaming misses and fills do not write either side array. Unknown hints
skip hinted-tag reads and use the same linear remainder. Counter meanings and
the revision-3 ABI are unchanged; acceptance counts reflect the new hit-only
learning policy.
Empty returns do not enter any L1 mechanism. Reference probe counts derive from
the returned slot, never the hint. The scalar name order is append-only; both
histogram offsets and sizeof(ArcRpcExercise) move when scalars are appended.
Profile JSON remains schema 2 and is compatible by name only. Every exercise,
ranking, interval-exercise and profile snapshot now uses a versioned v4 symbol plus
revision/size arguments, refusing mismatches before writing output. Always use
a runner/core pair from the same revision; baseline comparisons use two pairs.

## Slice 1a-3 S1 design (2026-10-17)

Recorded before implementation, under D-200 and the binding corrections in
`riscpc-6b-s1.md`. R-s1-1 uses the exact on-demand `visible(m.finish)` ceiling;
there is no stored finish memo, new writer obligation or M5 cache. B-3 may
consume this test only while a cycle or video is active. The denominator is
fixed per binding; control-register writes cannot change this ceiling.
R-s1-3 and R-s1-4 exclude the M5 demand tweak and B2 resumable-loop skip.
R-s1-5 guards both commands calls with `m.count || pending`; the inner call
must clear pending after the final demand retires. R-s1-6 moves the conditional
integer flush to service entry, before classification and every return; the
inline fast continuation is unchanged. PROFILE attributes that flush to BUS
rather than TIMING.

Production invariants: !pending implies count==0 (append sets pending before
incrementing count; both clearing sites require an empty queue). A cycle
implies active_valid, !video_active and !restart: all start_cycle callers have
selected an active packet and passed the video branch; start_cycle clears
restart. Outside run(), a cycle also implies bus_busy, recomputed at run exit.
The denominator is assigned only at bind. Cross-check builds assert these.

Given those invariants and !pending, !video_active, cycle_active and
now < due < visible(finish), due < next_ns and due < limit_ns:
run(now) has inert commands (count==0), bypasses video, and breaks in the
cycle branch before retirement or queued-video admission. It recomputes the
already-true bus_busy. ready() is false because due>now. next() returns due,
the strict minimum of finish and boundary. run(due) repeats the same inert
work; ready() now copies due ticks/floor/fraction to issue and returns true.
The skip performs exactly that copy, sets now=due and counts one service.
It changes no packet, cursor, restart, ROM history or measurement state.
The two omitted ticks(now) overflow checks cannot fire: due<ceil(finish/d)
implies due*d<finish<=UINT64_MAX, and now<due. Tick comparisons cannot replace
the ceiling test: distinct rational times may share a visible nanosecond.

R-s1-2 retains the TEST_FAULTS measure_last gate. Endpoint admission bounds
count at last and the loop breaks at linecyc+1==count before batch_next.
Intrusive end_slow sets test_stop, tested before service in batch_next.
The gate is reachable through direct batch_service calls or measure(0,last,...),
so its directed test calls service directly. It compiles out of shipping and
measurement cores. The test-only force-off switch resets before bind's early
return and the measurement API explicitly refuses it.

The counter-schema choice is H3: service_s1_taken joins the stable schema,
with a revision and revision/size handshake on every structure-copy API.
It remains host-only, outside m and fast, in the existing exercise/interval
roots. Run B extends L1's revision 3 to counter ABI revision 4, appending the scalar
after l1_linear_fallback and advancing the four versioned copy APIs to v4.
Each still refuses a revision or size mismatch before copying. Profile JSON
remains schema 2, keyed by name; the rank report includes the new scalar.
The eligibility classification remains independent of the shipping predicate;
per-call assertions account for the gate and force-off switch.

Cross-check isolation copies only the small fast state and scheduler time
that the retained run/ready/next sequence can write, and hashes the unchanged
timing state and measurement record. It does not copy the packet rings or
caches per skip. Reference execution bypasses only PROFILE wrappers, so no
profile timer/stage is touched; exercise service bookkeeping is restored.
Failure evidence survives timing rebind. Directed tests and calibration clients
consume the latch. Target CI budget: 30 minutes per calibration and 120 seconds
for the directed test; observed durations must be reported before review.
The differential begins after flush and uses the guarded reference: it cannot
verify the flush placement or commands guard. Direct post-call state/counter
assertions and predecessor comparisons provide those independent checks.

Mutant dispositions (S1 owns all): delete cycle||(video) under !video as
logically equivalent; dropping !video is equivalent for production states,
but a synthetic exclusion test may enforce the predicate literally. A missing
guard at either site and count-only at the outer site are behaviourally
equivalent; enforce the two-site source contract separately. Count-only at
the inner site is killed by final demand completion. Late flushes can be
equivalent at interpreter return but are killed by immediate post-batch_next
state assertions, including the false return. Flush-after-classification is
killed by a source-order contract (entry due is unchanged by an integral
flush). bus_busy and restart mutants write false and true respectively.

S1 is dispatched after M2b (D-198(1)); merge order is M2b, L1, S1 (D-200).
Run B integrates L1's merge e9ce589 and M2b v1. S1 adds only the force-off
leaf to the reviewed inventory: counter storage uses the existing excluded
exercise roots. Only inventory metadata changes; wire compatibility stays fixed.
Projection/materialisation at quotas 1/7/200, stale-representation mutants and
lockstep against L1's merge are required after the service-entry flush move.
Cumulative correctness evidence against the post-P0i anchor remains required.
Private benefit legs follow the frozen 1a-0 rule and D-201 bands, and report
p50/p95/p99, guest MIPS, ns/instruction on both cores and the hot/031 budget
line (p95 <=0.80T, max<=T). MinGW carries D-199(4)'s emitted-loop caveat.
