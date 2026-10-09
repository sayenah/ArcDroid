# RiscPC faster CPU translator (roadmap item 6b) — evaluation and plan (D-133, D-141)

Status: **evaluation, research and design only.** No translator code lands before R4 qualifies the RiscPC core and the release baseline (item 5) is done, unless the Founder reorders. Private evidence: S57 scratchpad `s57/6b/` (the evaluation report and a fork clone at `94f7c9a`).

## Baseline and target
- **Today (Odin3, R2a interpreter):**
  - 9.8–10.0 host ns per guest instruction on cache-hot code;
  - p95 ≈ 1.0 T at the 100 MIPS SA-110 ceiling;
  - gameplay about 67 % of a frame (D-131).
- **Native frame rates for the heaviest 3D titles:** these need about 200 MIPS (a 202 MHz StrongARM). Inside the 0.80 T budget that means about 3.3–4.0 host ns per instruction.
- **Hard 100 MIPS caps in code:** they must be lifted before any higher rate:
  - `rpc_session.c`: the instructions-per-field fatal, the 200-instruction quota, and the test-rate check;
  - `rpc_mem_timing.c`: rate > 100 is rejected.

  The timing arithmetic (D = 28·rate) works at any rate.

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
2. **Determinism:** a block runs only if its folded end precedes the next event or limit; otherwise the interpreter runs it.
3. **MMU:**
   - fetch permission is checked per block, within one 1 KiB sub-page, with exits at mode/DACR/TTBR/control changes;
   - a data-side permission filter keyed by translation generation reopens R1's no-host-pointer-cache rule, so it needs its own ruling.
4. **Self-modifying code:** invalidation by physical page, including floppy and IDE DMA writes (R2b, R2c). Guest cache maintenance never flushes translations.
5. **26/32-bit:** blocks are keyed by word size and `r15_mask`.
6. **Containment (D-137):**
   - no signal handlers, no fault-based memory, bounds-checked emission;
   - an interpreter fallback per session;
   - on Windows, unwind tables if JIT frames ever exist.
7. **Android executable memory:** AOSP app policy allows `execmem` for JIT compilers. A dual RW/RX mapping is preferred, and an Odin3 probe inside RetroArch is required before design.

## Options (Odin3, cache-hot, estimated)
| Option | ns per instruction | Notes |
|---|---|---|
| (c) block-structured pre-decoded interpreter, C only | 5–7 | Portable, including Windows. Proves blocks, guards, invalidation and folding. About 150 MIPS at about 1.0 T |
| (a) block JIT calling the timing service on every access | 3–5 | Memory density bounds it |
| (b) guarded arm64 fast path for proven cache-hot blocks, interpreter fallback | 1.5–3 hot | Needs timing-contract amendments (lazy bus catch-up, I-cache generation check, data-line filter) with equivalence proofs |

## Plan
- **Phase 0 (now, evidence only).**
  - An Odin3 profile of the hot workload.
  - A disposable executable-memory probe inside RetroArch.
  - Draft timing-contract amendments.
  - An Astra challenge of this plan.
- **Phase 1 (after R4 and item 5).** Option (c) on every platform. Gates:
  - byte-identical calibrations and goldens;
  - ≤ 7 ns hot on the Odin3.
- **Phase 2.** Option (b), ArcDroid-owned code, arm64, behind a core option.
- **Phase 3.** A rate change, only on a Founder decision.

**Verification.**
- An instruction-level lockstep shadow mode on real Odin3 hardware.
- All timing calibrations and `rpc-*` goldens byte-identical with the translator forced on.
- CI running the RPC suite in both modes.
- Sequence fuzzing: MMU, modes, self-modifying code, DMA, IRQ edges, full write buffers.
- Mutation testing.
- Private seven-pair boot, factory defaults, gameplay replays, the 44-title A/B, and a 30-minute thermal leg.

## Founder decisions (needed by Phase 2/3, not now)
- **Q1, target StrongARM rate:**
  - keep 100: headroom only;
  - 150;
  - 200: native frame rates;
  - a ≈ 202 MHz core-clock model with per-class cycle costs: more faithful, and an extension to the timing contract.
- **Q2, platform parity:** one rate everywhere means Windows must keep up through Phase 1 or a later x64 backend. Per-platform rates break cross-platform trace equality.
- **Q3, the fork:** reference only, or selective copying of encoder helpers with recorded provenance.
- **Q4, default:** should the translator be on by default, with a user-visible kill switch?
