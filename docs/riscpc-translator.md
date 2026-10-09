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
  - **Progress (S59).** Done: the executable-memory probe, a first Odin3 profile, and the guard and exit invariants, accepted as the Phase-0 basis in `docs/riscpc-translator-invariants.md` (D-146). Open: the Windows profile, representative gameplay, block-shape and churn measurements, and the Q-3 oracle fix. Results are under *Phase 0 evidence* below.
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
- **Not covered:** a second Android device and Windows.

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

**Still open in Phase 0:**
- the Windows profile;
- representative gameplay (after R2c, with AC-10);
- the block-length distribution, fallback frequency and code churn, which need block instrumentation;
- cold-start against sustained frame tails;
- repeated runs;
- the Q-3 oracle fix.

## Founder decisions (none needed now)
- **Q1, design objective — at the end of Phase 0.** Is the translator designed for headroom at 100 MIPS, or for the capacity to support a ceiling of about 200 MIPS? This decides whether the portable path is enough and whether a Windows backend may be needed. The actual rate stays a Phase 3 decision.
  - A higher instruction ceiling is not a 202 MHz SA-110: R2a excludes pipeline and per-class cycle modelling. A cycle model would be a separate timing-contract decision. Doubling the ceiling cannot promise native frame rates for bus-limited workloads.
- **Q2, platform parity — with Q1.** Cross-platform trace equality is a hard requirement, so the question is how to meet one common guest rate on both platforms (the portable path, or a later x64 backend). Per-platform rates are outside the current contract.
- **Q3, the fork — before the first copied or adapted helper, including in disposable probes.** Reference only, or selective copying of `rpcemu-extended` helpers (GPL-2.0-or-later, compatible with the project's GPL-2.0-only distribution).
  - If copying is chosen, record the exact commit, files and functions, dependencies, original notices and modifications through the existing provenance machinery. Every copied encoder gets independent encoding and bounds checks: several fork helpers mask operands rather than reject invalid ranges.
  - melonDS is GPL-3.0-or-later and stays conceptual reference only.
- **Q4, default — at qualification.** Fallback behaviour is defined earlier. Whether the translator is on by default, and the kill switch, are decided from evidence.
