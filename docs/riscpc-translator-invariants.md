# 6b Phase 0 — timing-contract amendments: exit contract and guard invariants (DRAFT)

Status: item 6b Phase-0 draft (S59), for an Astra check. Not accepted and not project law; no code. Drafted by an orchestrator subagent. Fable reviewed it and spot-verified it against the code: fetch translation runs at every instruction, a prefetch abort skips the event check, the calibration measurement mode disables the fast paths, and the fetch memo is keyed by virtual line only. The S59 Odin3 profile agrees: `getpccache` plus `checked_translation` are about a third of cache-hot cost.
Grounded at `main` `ee364d9`. Governing: `docs/riscpc-translator.md` constraints 1–7 and
Phase 0; `docs/riscpc-timing.md` §§2–5; D-137, D-140, D-141, D-142.

Notation. **[C]** = the cited code shows this. **[I]** = my inference from the cited code;
not executed, not tested. Paths: `arm.c`, `arm_common.{c,h}`, `cp15.c`, `mem.{c,h}` are under
`engine/rpcemu/src/`; `rpc_*.{c,h}` are under `engine/rpc/`. "The oracle" means today's
interpreter, including its ArcDroid timing patches. Equivalence means equivalence to the oracle,
including its quirks. It does not mean equivalence to hardware.

---

## 1. Interpreter facts the invariants rest on

### 1.1 Per-instruction order (one loop iteration, `arm.c:566-1917`)
1. `arc_rpc_mem_instruction_begin()` (`arm.c:571`): increments `arc_rpc_mem_fast.instructions`, sets `recording`, copies `rom_sequential` into `track_rom` (`rpc_mem_timing.h:67-77`). [C]
2. Fetch: `getpccache(PC)` (`arm.c:574`) calls `checked_translation(PC,0,1)` when `mmu` is set (`cp15.c:448`), then `arc_rpc_mem_fetch` (`cp15.c:455`). This happens at **every** instruction. There is no instruction pointer cached across iterations except the TLB entry's `code` pointer (`cp15.c:51,415`). [C]
3. Prefetch abort (`arm.c:575-579`): `exception(ABORT,0x10,4)`, then `reg[15]+=4`, then a jump to `instruction_done`. **The event check is skipped**, so no IRQ/FIQ is sampled at this boundary. An MMU fault charges no fetch (`cp15.c:449-452` returns before `:455`). Absent backing with a valid translation **does** record a fetch (`cp15.c:454-455`, `backed=false`). [C]
4. The opcode is read from the backing allocation at fetch time (`arm.c:581`). There is no prefetch buffer, so a store by instruction *k* to *k+1* is seen by *k+1*'s fetch. [C for the read path; I for "no pipeline": nothing in the loop retains a fetched word]
5. `arc_rpc_mem_opcode(opcode)` (`arm.c:582`) records the opcode. The timing service then classifies data requests by it: `swap()` and `multiple()` (`rpc_mem_timing.c:255-256`). [C]
6. Condition test via `flaglookup[...][(*pcpsr)>>28]` (`arm.c:584`). A failed condition still keeps the fetch and the base charge (§5 of the timing contract; `instruction_end` is unconditional). [C]
7. Body: functional effects happen at the issue timestamp. Each data access goes `readmemfl/fb` or `writememfl/fb` → `translateaddress` (`checked_translation`) → `arc_rpc_mem_access` (records a timing request only) → functional `mem_phys_*` (`mem.c:577-608`). [C]
8. Event check (`arm.c:1890-1907`), only if `arm.event!=0`:
   - In 26-bit modes it first copies the I/F bits from `r15[27:26]` into `reg[16][7:6]` (`:1891-1894`).
   - Priority: data abort (bit 0x40, then cleared), else FIQ (bit 1, if F clear), else IRQ (bit 0, if I clear). At most one exception per boundary. [C]
9. `reg[15]+=4` (`arm.c:1909`). This is the raw +4 on the whole of R15. In 26-bit modes the PSR bits share the register. [C] At PC 0x3fffffc it would carry into bit 26 (F). [I, edge case]
10. `arc_rpc_mem_instruction_end(PC)` (`arm.c:1911`; `rpc_mem_timing.h:102-126`):
    - ROM-burst discontinuity: `next_pc != fetch_pc+4`.
    - If requests are pending, it calls `end_slow`: `m.cursor=issue_ticks`, and the requests are serviced later.
    - Otherwise it charges the base: the next `cpu_due_*`.
11. Batch continuation (`arm.c:1912-1916`). It stops after `count` instructions or when `arc_rpc_mem_batch_next` returns false. On the quota's last instruction `batch_next` is **not** called. [C]
12. After the loop: `inscount+=linecyc` and, if `integer_batch`, `arc_rpc_mem_batch_flush()` (`arm.c:1918-1919`). [C] `inscount`, `tlbs` and `flushes` are write-only counters; nothing reads them. [C, grep]

### 1.2 What is charged when
- **Base:** charged once per instruction, including condition-failed and prefetch-aborted ones, at `instruction_end`.
  - With requests pending, `next_base(cursor)` runs only after service completes (`rpc_mem_timing.c:544-549`, `396-400`).
  - Integer fast path: `cpu_due_ns = now + base_whole` (`rpc_mem_timing.h:114-118`). [C]
- **Fetch timing:**
  - The single-line memo hit (`rpc_mem_timing.h:133`) increments `fetch_hits` only.
  - Otherwise `fetch_slow` → `arc_rpc_mem_access(fetch)`: tag lookup, allocation with victim-cursor advance and `memo_clear`, or a DEMAND packet (`rpc_mem_timing.c:265-364`).
  - SA uncached reads switch to the MCLK base (`uncached_clock`, `:257-264`). [C]
- **Costs are computed at service time, not at enqueue.**
  - `cycle()` reads live `iomd.romcr0/1`, `clkctrl`, `dramwid`, `iotcr` and `ectcr` when a word starts (`rpc_mem_timing.c:132,424-425,430,443,465,472,482-485`).
  - So an instruction that writes ROMCR has its own queued fetch costed with the new value. [C for the reads; I for the consequence, from body-before-service ordering]
- **Page-table walks** (`mem_phys_read32`, `cp15.c:387,393`) never enter the timing ledger. `arc_rpc_mem_access` returns when not `recording` (`rpc_mem_timing.c:288`). [C] A walk through I/O space would run `iomd_read` and other device reads, with side effects (`mem.c:158-185`). [C for the path; I for reachability]
- **Aborted data accesses** are not charged: `readmemfl` and friends return before `arc_rpc_mem_access` when the 0x40 event is set (`mem.c:580,588,596,604`). [C]
- **STRH** is two byte writes, each translated and charged (`arm_common.c:79-80`). [C]

### 1.3 Where IRQ/FIQ are sampled, and how they change
- Sampled only at step 8, after the instruction body. [C]
- Device events are delivered only by the driver, between batches (`rpc_session.c:330-338,364-367`). A batch never issues at or after the next deadline (`rpc_mem_timing.h:149-152`). [C] So inside a batch, `arm.event` bits 0/1 change only through MMIO in the batch's own instructions, which call `updateirqs` (`iomd.c:221-237`). [I, from the absence of other writers]
- **Consequence [I]:** an interrupt asserted by an event is taken only after **one more instruction** executes (step 8 of the next issued instruction). An MMIO write that asserts or unmasks an interrupt has it taken at the end of that same instruction.
- `arm.event` is a cached value, not a function of IOMD registers. For example, the IRQMSKD write does not call `updateirqs` (`iomd.c:386-388`). [C]
- **Mask changes inside a body** (all [C]):
  - `arm_write_r15`, used by data-processing S with Rd=15 and by LDM^ with PC (`arm_common.h:301-333`; `arm_common.c:528`);
  - `arm_compare_rd15`, used by TSTP/TEQP/CMPP/CMNP (`arm_common.h:342-375`). In 26-bit it writes PSR bits in R15; in 32-bit privileged modes it copies SPSR to CPSR;
  - `arm_write_cpsr` (MSR, `arm_common.h:386-423`);
  - `exception()` (`arm.c:494-536`), which SWI and Undefined call mid-body.
- **26-bit `reg[16]`:**
  - Its I/F bits are refreshed only when `arm.event!=0` (`arm.c:1891-1894`) or by MRS (`arm.c:881-885`).
  - `exception()` in 26-bit+`prog32` builds the SPSR from `reg[16]` (`arm.c:519`). [C]
  - So raw `reg[16]`, stale bits included, is architecturally consequential state. [I]

### 1.4 State one instruction can mutate
All [C] unless marked:
- **CPU:** `arm.reg[0..16]`; the banks via `updatemode` (`arm.c:81-201`): `user/fiq/irq/super/abort/undef_reg`, `spsr[]`, `mode`, `mmask`, `r15_mask`, `usrregs[]`, `cpsr`, `pcpsr`, `memmode`; `arm.event` bit 0x40 (set by `checked_translation` `cp15.c:432`, cleared at `arm.c:1899`/`cp15.c:450`).
- **CP15** (`cp15_write`, `cp15.c:160-281`): `ctrl`, TTBR, DACR, FSR/FAR (`cp15.c:434-435`, data faults only), globals `mmu/dcache/icache/prog32`, and `updatemode` when `prog32` is cleared in a 32-bit mode (`:186-188`).
- **TLB:** the 256-entry array; fills on every successful walk (`cp15.c:415`), including walks later denied by domain or AP (the checks at `:418-427` run after the install); full flushes (`:93`).
- **Memory and devices:** guest memory and `dirtybuffer` (write-only, `vidc20.c:73-74`; `mem.c:359,424-426,475,542-544`); device state; scheduler deadlines (`rpc_timing.c:6-9`); `arc_rpc_cpu_idle` (`cp15.c:270`, `iomd.c:326`).
- **Timing:** the whole `m` struct and `arc_rpc_mem_fast` (`rpc_mem_timing.c:38-53`, `.h:10-27`). `unpredictable_count` also changes (logging is a no-op, `rpc_session.c:100`).
- **Functional partial transfers** are kept: STM stores before an abort stay in memory; LDM registers loaded before an abort stay loaded; base restore or writeback follows `abort_base_restored` (`arm_common.c:270-555`). [C]

### 1.5 Batch, quota and driver
- **Driver loop** (`rpc_session.c:324-375`), at each `now`:
  1. deliver all events due ≤ now in enum order (`rpc_scheduler.c:59-72`);
  2. wake idle on `arm.event&3`;
  3. `arc_rpc_mem_idle`, then `arc_rpc_mem_run(now)` (completion, release, grant, video);
  4. `arc_rpc_mem_ready`, then a batch with `quota=min(200, 1666667-work)` (`:343-348`), or quota 1 once a field has completed (`:346`).
- **Inside a batch** `arc_rpc_mem_batch_next` (`rpc_mem_timing.h:137-158`):
  - returns false on idle or test stop;
  - with `pending||bus_busy` it flushes integer mode and services locally up to `boundary=min(next_ns,limit)` (`rpc_mem_timing.c:634-689`);
  - otherwise it requires `cpu_due_ns < boundary`, so a tie goes back to the driver, which delivers events first.
  - `next_ns` is re-read on every call, so an MMIO-armed nearer deadline (for example `arm_event(TRANSFER,0)`, `fdc.c:528`) stops the batch. [C]
- **Integer fast path** (`rpc_mem_timing.h:78-91`):
  - active only when `base_fraction==0`, the clock is aligned to the issue state, there are 200·base ticks of headroom (the quota constant appears here and at `arm.c:562`), and `!test_measure`;
  - it leaves `issue_*` stale until `arc_rpc_mem_batch_flush` (`rpc_mem_timing.c:403-411`). [C]
- **The 200 split is chronologically transparent [I]:** the driver path at a quota exit repeats the same event→service→ready order. Not proven by a test.
- **Observable [C]:** the 1,666,667 backstop is a session fatal (`rpc_session.c:343`), and the single instruction after field completion ends the frame (`:346,352`).

### 1.6 Session fatal
- `fatal()` (`rpc_session.c:46-60`):
  - records text, purges sound, clears all `armed` deadlines;
  - `longjmp`s to the `setjmp` in `arc_rpc_advance_internal` (`:316`) or reset (`:259`).
- After that, `arc_rpc_fault` refuses every later advance (`:314`). [C]
- **Guest-reachable mid-instruction fatals** [C]:
  - `updatemode` "Bad mode" (`arm.c:169`; `arm.mode` already overwritten at `:120`), tested as "real guest invalid-mode fault" (`tests/rpc-session-expectations.md:35-37`);
  - request and packet budgets (`rpc_mem_timing.c:168,179`);
  - time overflow (`:79,85`);
  - "pending memory work" (`:244`).
- `UNIMPLEMENTED` compiles to nothing without `_DEBUG` (`rpcemu.h:254,278`). [C]

### 1.7 Memory backing, mirrors and writers
- **Allocations** [C]:
  - `rom` 8 MiB; 2/4 MiB images are memcpy'd into it (`mem.c:66-73`);
  - `ram00` and `ram01`, 32 MiB each, mirrored 4× across `0x10–0x13` and `0x14–0x17` by `mem_rammask` (`mem.c:114,206,212`);
  - `vram` 2 MiB, mirrored across `0x02xxxxxx` (`mem.c:115,156`);
  - `ram1` is NULL.
- `arc_rpc_phys_page` returns pointers into these allocations at 1 KiB granularity (`mem.c:124-136`). The fetch `code` pointer comes from it (`cp15.c:415,454`). [C]
- **Functional writers of guest memory are CPU stores only** [C]: `mem_phys_write32/8` (`mem.c:344-573`), reached solely from `writememfl/fb` (`mem.c:593-608`); grep finds no other writer.
  - ROM and bank-1 ROM writes are functionally ignored: there is no case for them (`mem.c:354-448`).
  - On the RiscPC, floppy "DMA" is FIQ pseudo-DMA: `fdc_dma_read/write` are MMIO ports (`fdc.c:524-534`; `mem.c:269,493`) and the guest moves the bytes with ordinary stores.
  - Video and audio DMA only **read** guest memory at event time (`vidc20.c:173,272`; `rpc_audio.c:165`).
  - IDE is a stub (`rpc_session.c:110-116`).
  - Wholesale writers: `mem_reset` (`mem.c:117-119`, from reset `rpc_session.c:265`) and ROM load.
- A video bus hold is raised only inside a VIDEO event (`vidc20.c:347,359` within `scan_line`, called from `arc_rpc_video_event`). So no new hold arrives mid-batch. A hold queued before the batch sets `bus_busy` (`rpc_mem_timing.c:551-557`). [C]

### 1.8 Translation cache and fetch memo
- **TLB** [C]:
  - one array `tlb[256]`, direct-mapped by `(t ^ t>>10 ^ t>>18)&255` with `t=VA>>10`, shared by fetch and data (`cp15.c:379-438`);
  - domain, AP, `memmode` (which LDRT/STRT force to 0, `mem.h:120-182`) and ctrl S/R are checked on **every** hit (`:418-427`);
  - descriptor changes in memory are not seen until the entry is evicted or flushed (`tests/rpc_cpu_test.c:184`);
  - full flush on: ctrl M/R/S change, TTBR write, DACR change, ARMv3 c5/c6, SA c8 (`cp15.c:174-176,191-202,214-222,251-263`).
- **Fetch memo** [C]:
  - VA-line keyed (`rpc_mem_timing.h:133`);
  - cleared by any allocation, data or fetch (`rpc_mem_timing.c:335`); by control writes (`:220`), TLB flush (`:230-233`), c7 invalidation (`:369,382`), and SA slow-clock uncached reads (`:262`);
  - **not** cleared by mode change, since permission is checked before it by `checked_translation`.

---

## 2. Exit contract

**Materialisation points.**
- **P0(k)** = the state at `arm.c:571` before instruction *k*: after `batch_next(k-1)` returned true, or at batch start.
- **P2(k)** = the state just after `arc_rpc_mem_instruction_end(PC)` for *k* (`arm.c:1911`), before the continuation decision.

Every exit lands on exactly one of these points. The resumer performs exactly the steps the oracle would perform next, and no others.

**Canonical state at P0 or P2** (compared field-by-field, never as raw host structs):
- **CPU:** `ARMState` raw, including the stale 26-bit `reg[16]` bits (§1.3), all banks and SPSRs; the derived `cpsr`, `pcpsr`, `usrregs`, `memmode`.
- **CP15 and TLB:** CP15 and globals `mmu/prog32/dcache/icache`; the entire TLB array (valid, tag, physical, domain, AP, page, attributes; `code` compared as allocation+offset).
- **Guest:** memory; device and scheduler state (`now_ns`, deadlines, armed, `next_*`, `cpu_fraction`); `arc_rpc_cpu_idle`.
- **Timing:**
  - `arc_rpc_mem_fast` in its **flushed** representation (`issue_ticks/floor/fraction`, `cpu_due_ticks/ns`, `due_floor/fraction`, `base_*`, `pending`, `bus_busy`, `memo_valid/line`, `rom_sequential`, `after_branch`, `track_rom`, `fetch_pc`, `opcode`, `instructions`, `fetch_hits`);
  - all of `m`: tags, dirty bits, `populated`, victim cursors, command ring, buffer ring, `bwords`, active packet and `grant_left`, `cursor`/`bus_cursor`/`finish`, `cycle_active`, `video_active`, `queued_video`, `video_arrival`, `restart`, `last_rom`, `wait_start`, `switching`, `fast_clock`, `control`, `idle`, and every stats field.
- **Counts:** the executed-prefix count `linecyc` (feeds `instruction_work`, `arc_rpc_executed`, `inscount`).

| # | Exit kind | Lands at | Must be materialised | Must NOT be repeated on resume |
|---|---|---|---|---|
| E1 | Normal block end | P0(k+1) if the block's own continuation check passed; else P2(k) handed to the oracle's continuation | Everything above; `now_ns` = visible issue time of k+1; the integer representation flushed or provably identical | Nothing of k |
| E2 | Guard miss before k | P0(k) | Everything above. **No part of k:** no `instructions++`, no TLB probe-with-fill, no memo/tag lookup, no allocation, no opcode-record, no base | Nothing of k was done, so the resumer runs k whole. A guard needing fetch information must use **non-mutating** probes (§3 G-4/G-5) |
| E2′ | Guard miss **after** k's fetch | P0(k) + "fetched": the k fetch request (or hit count) already recorded, opcode known | As E2, plus k's TLB/memo/tag effects and `instructions++` | The fetch: TLB walk/fill, memo/tag effect, fill packet, victim advance, `uncached_clock`, `instructions++`. **The oracle has no post-fetch entry today** (Q-1) |
| E3 | Helper call inside k (MMIO, CP15, complex op, oracle routine) | Mid-k | Before the call: registers/mode/`memmode`; `now_ns` = k's issue time (read by `arc_rpc_time_ns`, `rpc_timing.c:5`); `recording=true`; `opcode`; k's fetch request and earlier data requests enqueued in program order; `arc_rpc_attributes` as left by the current access's translation. After the call, re-read: `arm.event`, registers, mode and `r15_mask` (CP15 can call `updatemode`), TLB/memo generation, `next_ns`, `cpu_idle`, `base_*` (clock switch), `pending` | k's earlier stores and loads (MMIO reads pop FIFOs, `fdc.c:524-528`) |
| E4 | Side exit after k | P2(k) | Everything above. The resumer then runs the continuation check **once** (`batch_next`, or quota/idle exit) | Fetch, data transfers and base of k |
| E5 | Pending event / deadline / limit / quota | P2(k) with `batch_next` returning false, or the quota reached; `linecyc` returned; integer mode flushed (`arm.c:1919`) | Everything above. Pending requests stay queued with `pending` and `bus_busy` as left (the driver services them, `rpc_session.c:341`) | Base of k (already in `cpu_due_*`); queued requests (serviced once) |
| E6 | IRQ/FIQ taken | P2(k): `exception()` inside k's event check, `arc_rpc_mem_branch` called (`arm.c:496`), `instruction_end(vector)` | Everything above, including the SPSR/LR/mode bank swap and the cleared 0x40 bit | k's body; the exception entry |
| E7a | Data abort in k | P2(k) via the event check (`arm.c:1896-1899`) | Partial functional transfers and charged transfers up to the abort; FAR/FSR; base-register rule per model; no FIQ/IRQ at this boundary | Completed stores and loads; their packets |
| E7b | Prefetch abort at k | P2(k) via `instruction_done` (`arm.c:575-579`) | Base charged; fetch charged only for absent backing (§1.1-3); no event check (Q-8) | The exception entry; the base |
| E8 | Session fatal | `longjmp` to `rpc_session.c:316/259` | Nothing for continuation: the session is dead. Translator bookkeeping (executing-block references, code-cache locks) must be valid after the unwind and reset by `arc_rpc_stop`/start. Post-fatal registers are visible to `arc_rpc_test_snapshot` (`rpc_session.c:510-517`), diagnostically only today (Q-9) | No retry in the interpreter after effects (constraint 6) |
| E9 | Code invalidation by a store in k | P2(k): k finishes atomically, including its later STM transfers, abort behaviour, event check and `instruction_end` | Everything above; dependent blocks invalidated **at the functional write** | k's stores and base. The next instruction is refetched from memory by the resumer |

---

## 3. Guard invariants

**G-1 Block entry budget.**
- Let t₀ be the issue ticks and b the current `base_ticks`. With nothing pending and `bus_busy` clear, instruction i of a block issues at tᵢ = tᵢ₋₁ + b (`rpc_mem_timing.c:112-124`).
- It may issue only if ⌈tᵢ/D⌉ < min(`next_ns`, `limit`) evaluated **after** instruction i−1 (`rpc_mem_timing.h:149-152`), and only while `linecyc+1 < count` or as the last of the quota.
- A folded end time before the next event is necessary but not sufficient. Any helper may lower `next_ns` (MMIO deadlines with delay ≥ 0), set `pending`, change b (SA clock, `rpc_mem_timing.c:234-240,257-264`) or set idle. The budget must be re-evaluated after every helper.
- Any instruction that enqueues requests leaves `pending` set, and every later instruction goes through service (`rpc_mem_timing.h:145-147`). A folded block therefore covers at most a run of request-free instructions, unless G-8 holds.

**G-2 Block key.** The key is:
- PC VA (`PC` uses `r15_mask`, `arm.h:67`);
- the full 5-bit `arm.mode`, not just the word size. Privilege selects the `arm_write_r15` and `arm_compare_rd15` masks (`arm_common.h:308-317,349-369`) and `memmode`;
- `prog32` for 26-bit exception entry (`arm.c:517-523`);
- the session model (`arch_v4`, `r15_diff`, `abort_base_restored`, `stm_writeback_at_end`, `arm.c:280-290`).

`r15_mask`, `mmask` and `cpsr` derive from the mode (`arm.c:172-197`). Any `updatemode` call ends the block (E4).

**G-3 Interrupt state.**
- **Entry rule:** if at P0 `(event&2 && !F) || (event&1 && !I)`, with I/F read from `r15[27:26]` in 26-bit modes, the oracle executes **exactly one** instruction and then takes the exception at its event check (§1.3). The exception may not follow if that instruction masks it.
- **Per-instruction rule:** after every instruction that may change I, F or `arm.event`, the event check must run exactly as `arm.c:1890-1907`, and the block must end on any exception.
  - Instructions that change I, F or `arm.event`: MSR (`arm_write_cpsr`), the P-forms through `arm_compare_rd15`, S-forms with Rd=15, LDM^ with PC, SWI/Undefined/CP15 (CP15 can switch the mode), and every MMIO helper.
  - The ordering detail matters: data abort precedes FIQ, which precedes IRQ; one exception per boundary; the 26-bit refresh of `reg[16]` happens only when `event≠0`.
- Without MMIO and without these instructions, IRQ/FIQ pending state is constant for the block [I, §1.3].

**G-4 Fetch translation (shared TLB).**
- The oracle runs `checked_translation(PC,0,1)` per instruction. It can walk and fill on a miss, and it checks domain, AP, `memmode` and S/R on a hit.
- A block may replace per-fetch translation with a guard only if all of the following hold since the last real fetch translation of the same 1 KiB tag:
  - (a) no TLB flush (the §1.8 triggers);
  - (b) no data access in the block installed an entry into the slot of any fetch tag the block uses. Data walks install on success, **including accesses later denied by domain or AP** (`cp15.c:415` precedes `:418-427`). Translation faults install nothing (`:403-414`);
  - (c) `memmode`, DACR and ctrl S/R are unchanged;
  - (d) PC stays within one 1 KiB tag, or each new tag is translated for real;
  - (e) `mmu` is unchanged.
- After an eviction the oracle **re-walks** and sees current descriptors, possibly faulting. The guard must reproduce that re-walk, not reuse the old entry.
- Guard probes must not fill or walk. A walk can read MMIO (§1.2), and a fill changes replacement.
- Even with hoisting, the fetch's `attributes` must equal what the oracle's translation would have left in `arc_rpc_attributes` before `arc_rpc_mem_fetch` reads it (`cp15.c:92,428,455`).
- Hoisting the permission check itself changes R1's every-fetch contract and needs a Fable ruling before Phase 1 (constraint 3).

**G-5 Instruction-cache residency and generation.**
- A block may skip per-fetch timing work only if every fetched VA line would hit, and the residency state after the block equals the oracle's: `fetch_hits`+`stats.hits` sum, memo valid on the last fetched line, victim cursors unchanged by hits (`rpc_mem_timing.c:316-320`), and `instructions` counted.
- The generation must advance on everything that clears the memo:
  - any allocation in the structure fetch uses (`cache_for`, `:148-151`: unified models share it with data);
  - c7 maintenance (`:371-385`);
  - control writes and TLB flushes (`:220,232`);
  - SA uncached-read clock handling (`:262`);
  - session bind or reset (`:195-216`).
- The proof is also tied to G-4, because eligibility depends on the current physical region and `backed` (`:309`).
- Fetches from **burst-mode ROM** carry sequential-fetch state (`track_rom`, `after_branch`, `fetch_pc`, `last_rom`, `rom_sequential`; `rpc_mem_timing.h:94-113`, `rpc_mem_timing.c:299-304,449-459`). They need exact per-fetch handling or a separate proof.
- Retained tags still hit with C/I disabled (`:154-156,270`).

**G-6 Unified caches (ARM610/710/7500): data breaks the fetch proof.**
- A cacheable data load miss with M+C allocates in the same tag array as code, advances that set's cursor and can evict an executing line (`rpc_mem_timing.c:325-343`).
- A store hit changes no tag but still emits a write (`:322-361`). A store miss never allocates. SWP is locked and uncached (`:305-315`).
- So for these models every data access must perform the real lookup, or a filter that provably hits. The fetch-residency generation must advance on **any** allocation, matching `memo_clear` at `:335`.

**G-7 SA-110 split caches and data-side filter.**
- D accesses never touch I tags (`cache_for`). A data-hit filter must reproduce all of the following:
  - permission before hit (`mem.c:595-597`), keyed by effective privilege (LDRT/STRT), direction, DACR, S/R and `mmu`;
  - load hit: zero bus, `stats.hits++`;
  - store hit: set the dirty bit for the half `(va>>4)&1` with no packet (`:323`). The **functional store still happens**: ROM writes are ignored functionally (`mem.c:354-448`), while their dirty halves later cast out via G7 (`:186-194,333`);
  - misses: allocate on loads only, with castout of dirty victim halves to the retained physical address before the fill;
  - store-miss buffering only with M, W and B set in a memory region (`:347-348`).
- Any uncached read on SA may switch the base clock (`:257-264`).
- A filter reopens R1's no-host-pointer-cache rule and needs its own ruling (constraint 3).

**G-8 Lazy bus catch-up.**
- Today, at every boundary with `pending||bus_busy`, the oracle advances the bus to the next issue chronologically (`arc_rpc_mem_batch_service` → `arc_rpc_mem_run`). Word costs are fixed at word start from live IOMD registers (§1.2).
- Packet release frees buffer capacity, checked in `commands()` (`:520-543`). DEMAND reads wait for the buffer FIFO first (`:599-601`). A queued video hold starts at a grant boundary and pushes `cpu_due` back by its length when the CPU has no pending work (`:585-593`). `restart` forces N after a hold (`:566`).
- A lazy drain must catch the bus up to the current issue time **before**:
  - any new CPU request is enqueued;
  - any IOMD write, at minimum to ROMCR0/1, CLKCTL, DRAMWID, IOTCR or ECTCR (`iomd.c:344,391,394,431,435,450`);
  - any MMIO access or observation, and any CP15 c7 drain or clean;
  - any clock switch, idle, and the batch boundary or driver return.
- At an equal visible ns the order stays: events (driver) → `arc_rpc_mem_run` (completion, release, grant: video, then buffer FIFO, then CPU demand last) → CPU issue (`rpc_session.c:330-342`; `rpc_mem_timing.c:558-606`).
- Bulk subtraction of elapsed time is not a proof. Equivalence must show identical per-word start ticks, release ticks and `buffer_wait_ticks`.

**G-9 Self-modifying code: dependency key and invalidation point.**
- **Key:** blocks depend on **(allocation, offset)** ranges, derived from the `code` pointer (`cp15.c:415,454`; `mem.c:124-136`), never from VA or physical address. This lets `0x10000000`, `0x11000000`, `0x12000000` and `0x13000000` alias, and lets VRAM `0x02000000+n·2 MiB` alias.
- **Hook:** the only runtime writers are `mem_phys_write32/8` from CPU stores (§1.7). The invalidation hook belongs at that functional write, after the permission check and only if the write reached RAM or VRAM.
- **Coverage:** it must cover STR, STRB, STRH (two byte writes), STM/STM^ (each word), SWP/SWPB, STRT/STRBT, and stores by oracle routines invoked from translated code.
- **Wholesale invalidation:** `mem_reset`/session reset and ROM load.
- **Order:** the store's instruction completes and the block exits at P2 (E9). The next fetch reads current bytes (§1.1-4), including a store to PC+4.

**G-10 Guest cache maintenance is timing-only.** CP15 c7 has no functional effect on fetched bytes: only the dead `pccache` (`cp15.c:244-249`; `resetcodeblocks` → `pccache`, `rpc_session.c:101`; `pccache` is never read by `arm.c`). c7 advances the G-5 generation but must not be the trigger for G-9.

**G-11 Time representation and base changes.** The block must produce the same rational `issue_*` and `due_*` values at every exit, whether or not the oracle would have been in integer mode (`rpc_mem_timing.h:84-91,114-125`). Base changes take effect for the next `next_base` after the instruction that caused them (`rpc_mem_timing.c:217-229`). Overflow guards must fail at the same instruction with the same fatal (`:77-87`; `.h:86,107`).

**G-12 Counters and test hooks.**
- `instructions`, `fetch_hits`, every `stats` field, `linecyc`, the `instruction_work` backstop and the quota of 1 after field completion must match at every exit.
- Under `ARCDROID_TEST_FAULTS` with `test_measure`, the oracle takes the slow path for **every** instruction (`rpc_mem_timing.h:74-76,87-88,108-111`). It stops at `measure_last` (`rpc_mem_timing.c:393`; `.h:142-144`).

---

## 4. What an equivalence test must show; existing coverage

Unless noted, "show" means isolated deterministic runs or a full snapshot/restore with identical inputs, comparing the §2 canonical state at every exit, with forced exits at every instruction position.

| Inv. | The equivalence test must show | Existing test that would catch a violation |
|---|---|---|
| G-1 | Issue ticks, deadline ties (event at ⌈tᵢ⌉), MMIO-armed delay-0 deadlines, limit and quota=1 exits are identical | Partly. `rpc-plan-devices.trace` and `rpc_session_test.c` (equal-timestamp timers, "events precede instruction", `tests/rpc-session-expectations.md:14-21`); flat/burst agreement (`rpc-plan-expectations.md:63-65`); `rpc_hot_test.py` (exact count at the ceiling, integer path). None checks per-instruction issue ticks inside a translated block |
| G-2 | 26↔32 and privilege changes inside a would-be block exit and re-key; R15 PSR masks per mode | `rpc-cpu.trace`: SVC26/SVC32 phases (`tests/rpc-cpu-expectations.md:10`); `rpc-plan-matrix-*` mode records. **No test** for a user-mode `arm_write_r15` mask inside a hot block, or the 26-bit PC carry |
| G-3 | Pending-at-entry executes exactly one instruction; unmask by MSR, TEQP or MMIO is taken at that instruction's end; abort > FIQ > IRQ; stale 26-bit `reg[16]` flows into the SPSR identically | `rpc-plan-devices` ("forced FIQ taken exactly once", timer IRQ 0x60). **No test** pins the "one instruction after an event-raised IRQ" ordinal or the `reg[16]` staleness |
| G-4 | A data access colliding with the fetch slot (incl. a domain/AP-denied one) forces a re-walk that sees a modified descriptor or faults; no speculative fill/walk | `rpc_cpu_test.c:252-292` (data-data collisions m1, privileged→user fetch m4, c6 purge m11) and `:184` (stale TLB persists). **No test** for a data access evicting the *fetch* entry mid-sequence |
| G-5 | `hits`/`fetch_hits` sums, memo line, cursors and ROM-sequential state are equal after hot blocks, including burst-ROM loops and C/I-disabled retained tags | The calibrations (4,677 intervals, `tests/rpc-timing-expectations.md:79`) and `rpc_qa_mutations.py` cover the oracle, **but run under `test_measure` (`tests/rpc_timing_test.c:46`), which disables the fast paths** (Q-2). `rpc_hot_test.py` catches only base/count errors |
| G-6 | A unified data fill evicting the executing code line gives an identical refill charge and cursor state | `cache-geometry` probes (fills with code in sets 1/2, `rpc-timing-expectations.md:14-46`), under `test_measure` only. **No test** has data and code in the same set mid-block |
| G-7 | Filtered hits equal real hits: dirty halves, castouts, ROM castout G7, permission-before-hit with LDRT/STRT | `rom-castout-*`, the SA geometry probes and `rpc_cpu_test` m12 (AP2 user write denied on a warm hit). Calibrations again only under `test_measure` |
| G-8 | Per-word start/finish ticks, release ticks, `buffer_wait_ticks` and video-hold placement are identical with lazy vs eager drain; a timing-register write mid-drain | `rpc_mem_timing_unit.c` (request-level holds/frontend boundaries, independent oracle) and buffer-boundary calibrations (`rpc-timing-expectations.md:81-91`); `rpc-d2-*`/`rpc-video-*` hashes move if MMIO chronology shifts. **No test** writes ROMCR/CLKCTL while buffered packets drain |
| G-9 | Stores through every mirror alias and every store form invalidate; a store to PC+4 is seen; STM partial abort then exit | `rpc_plan_matrix_rom.py:53-60` (SMC through guest stores, then entry by `MOV pc`); `rpc_cpu_test.c:107` (functional mirror). **No test** for aliased-mirror SMC, same-block SMC or PC+4 |
| G-10 | c7 changes timing only; code bytes stay authoritative | No direct test |
| G-11 | Rational state equal across integer/non-integer transitions and SA clock switches inside blocks | SA clock-disable calibrations (`rpc-timing-expectations.md:92-101`), under `test_measure` only |
| G-12 | Counters equal; test hooks honoured | Calibrations check instructions/words/fills/packets (`rpc_timing_test.c:52-56`) |

Goldens observe state only at frontend boundaries (`arc_rpc_test_snapshot`, `rpc_session.c:510-517`) and in guest-recorded RAM. So a divergence that later self-corrects can pass every existing golden. This is why per-instruction differential testing (constraint "Verification") is required, not optional.

---

## 5. Open questions for the architect

- **Q-1 Post-fetch resume.** The oracle fuses fetch and execute in one iteration (`arm.c:571-582`). E2′, a guard evaluated after a mutating fetch, needs an oracle entry point that takes an already-fetched opcode. That changes oracle code. Should guards be non-mutating pre-fetch probes only, which is E2 and is feasible because `lookup()` is pure (`rpc_mem_timing.c:152-164`)? Or is an oracle refactor authorised under byte-identical goldens?
- **Q-2 Calibrations bypass fast paths.** `rpc_timing_test.c:46` arms `measure` for the whole session, and `test_measure` forces `begin_slow`/`end_slow` on every instruction and disables `integer_batch` (`rpc_mem_timing.h:74-76,87-88,108-111`). So "4,677 calibrations byte-identical with the translator forced on" would not exercise a translator gated like `integer_batch`. A ruling is needed on one of two options:
  - the translator must run under measurement, with exact `measure_first` and `measure_last` instruction ordinals and `test_stop`; or
  - calibrations get a non-intrusive measurement mode.
- **Q-3 Memo vs tag lookup (possible R2a defect, [I]).** A memo hit (`rpc_mem_timing.h:133`) does not check `backed` or the region. The slow path refuses a lookup for non-eligible physical addresses (`rpc_mem_timing.c:309`). The memo is not cleared when a TLB slot is evicted and re-walked onto a remapped descriptor (§1.8). So with stale descriptors plus a collision eviction, a memo'd VA line remapped to I/O or absent memory would count a hit where the tag path charges a DEMAND fetch. This is unexecuted and probably unreachable by RISC OS. Should it be confirmed by a targeted test and fixed in the oracle before 6b, or matched as is? Contract §4.2 says memos "must not create residency".
- **Q-4 Constraint-4 wording.** The code shows no device writes guest memory in R2b (§1.7). "Device writes such as floppy DMA" is a future-proofing requirement, not a current path. Keep it as a rule that any future writer must call the G-9 hook? R2c IDE is PIO, as the doc says.
- **Q-5 `dirtybuffer`.** It is written by VRAM stores and by DRAM stores in the video window (`mem.c:359,424-426,475,542-544`) but never read. Rule it dead state, so inline stores may skip it, or require parity?
- **Q-6 Raw R15 arithmetic.** Is the raw `reg[15]+=4` carry into bit 26 in 26-bit mode at the top of 64 MiB (`arm.c:1909`) to be matched or corrected? It is an oracle quirk.
- **Q-7 Quota coupling.** 200 is hard-wired in the integer-batch overflow guard (`rpc_mem_timing.h:86`) and the batch fatal (`arm.c:562`). That the quota split is chronologically transparent is my inference (§1.5) and untested. Should a quota-transparency test, the same trace with quotas 1, 7 and 200, be a Phase 1 prerequisite?
- **Q-8 FIQ after a prefetch abort.** The prefetch-abort path skips the event check (`arm.c:579`). Abort entry masks IRQ only (`arm.c:500-505`), so a pending FIQ waits until after the abort handler's first instruction. Is this an oracle quirk to be matched exactly? It is required for equivalence.
- **Q-9 Post-fatal state.** `fatal()` can fire mid-instruction with partially updated state (`arm.c:120` then `:169`). Snapshot exposes registers after a fault (`rpc_session.c:515`). Is post-fatal register state in or out of the equivalence surface?
- **Q-10 Page-table walks with side effects.** A walk can read MMIO (§1.2). Decoding a block must not translate at all (constraint 3). Does the ruling also need to forbid **guard-time** re-walks that the oracle would not perform at that instant?
- **Q-11 Where the code makes an invariant hard.**
  - G-4: the shared TLB's hash and fill-before-permission behaviour (`cp15.c:384,415-427`) means proving "no data access hit a fetch slot" needs per-access slot comparison, which is roughly the cost of the TLB lookup it would save.
  - G-8: service-time costing from live IOMD registers forces catch-up before every IOMD write, so MMIO-dense code gains nothing from lazy drain.
  - G-6: on unified models every cacheable data load can break fetch residency, so the data-line filter (option b) is the precondition for multi-instruction hot blocks with loads on ARM610/710/7500.
