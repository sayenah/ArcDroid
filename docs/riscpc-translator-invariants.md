# 6b Phase 0 — timing-contract amendments: exit contract and guard invariants

Status: Accepted as the item 6b Phase-0 basis (D-146) after the S59 Astra check (ACCEPT WITH CHANGES, all incorporated). Not an implementation authorisation.

Grounded at `main` `ee364d9` and revised for the S59 Astra findings 1–7, which Astra checked at `6cde473`.
Governing documents:
- `docs/riscpc-translator.md`, constraints 1–7 and Phase 0;
- `docs/riscpc-timing.md` §§2–5;
- D-137, D-140, D-141, D-142.

Notation:
- **[C]** = the cited code shows this.
- **[I]** = an inference from the cited code. It has not been executed or tested.

Paths:
- `arm.c`, `arm_common.{c,h}`, `cp15.c` and `mem.{c,h}` are under `engine/rpcemu/src/`.
- `rpc_*.{c,h}` are under `engine/rpc/`.

"The oracle" means today's interpreter, including its ArcDroid timing patches. Equivalence means equivalence to the oracle, quirks included, after the Q-3 oracle correction (§5). It does not mean equivalence to hardware.

---

## 1. Interpreter facts the invariants rest on

### 1.1 Per-instruction order (`arm.c:566-1917`)

1. **Begin.** `arc_rpc_mem_instruction_begin()` (`arm.c:571`) increments `arc_rpc_mem_fast.instructions`, sets `recording`, and copies `rom_sequential` into `track_rom` (`rpc_mem_timing.h:67-77`). [C]
2. **Fetch, at every instruction.** `getpccache(PC)` (`arm.c:574`) calls `checked_translation(PC,0,1)` when `mmu` is set (`cp15.c:448`). It then calls `arc_rpc_mem_fetch` (`cp15.c:455`). Nothing caches an instruction pointer across iterations except the TLB entry's `code` pointer (`cp15.c:51,415`). [C]
3. **Prefetch abort** (`arm.c:575-579`). The sequence is `exception(ABORT,0x10,4)`, then `reg[15]+=4`, then `goto instruction_done`. **The event check is skipped.** [C]
   - An MMU fault records no fetch, because `cp15.c:449-452` returns before `:455`.
   - Absent backing with a valid translation **does** record a fetch (`cp15.c:454-455`, `backed=false`).
4. **Opcode read.** The opcode is read from the backing allocation at fetch time (`arm.c:581`). Nothing in the loop retains a fetched word, so a store by instruction k to k+1 is seen by k+1's fetch. [C for the read; I for "no prefetch"]
5. **Opcode record.** `arc_rpc_mem_opcode` (`arm.c:582`) records the opcode. Data requests are then classified by `swap()`/`multiple()` (`rpc_mem_timing.c:255-256`). [C]
6. **Condition and body.** The condition is tested next (`arm.c:584`). The body follows. Each data access goes through `readmemfl/fb` or `writememfl/fb`:
   1. `translateaddress`, which is `checked_translation`;
   2. `arc_rpc_mem_access`, which only **collects** a timing request;
   3. the functional `mem_phys_*` call (`mem.c:577-608`). [C]
7. **Event check** (`arm.c:1890-1907`), whenever `arm.event!=0`. [C]
   - In 26-bit modes it first copies I/F from `r15[27:26]` into `reg[16][7:6]` (`:1891-1894`). This happens even when every pending interrupt is masked.
   - Priority is data abort (bit 0x40, then cleared), then FIQ (if F is clear), then IRQ (if I is clear). At most one exception is taken per boundary.
8. **PC advance.** `reg[15]+=4` (`arm.c:1909`) is raw arithmetic on R15. In 26-bit modes it can carry into bit 26 (F) at PC `0x3fffffc`. [C for the code; I for the carry]
9. **End.** `arc_rpc_mem_instruction_end(PC)` (`arm.c:1911`; `rpc_mem_timing.h:102-126`) does three things:
   - it records burst-ROM discontinuity;
   - if requests are pending, it calls `end_slow`, which sets `m.cursor=issue_ticks` (`rpc_mem_timing.c:386-401`);
   - otherwise it resolves the next due time (§1.2).
10. **Continuation** (`arm.c:1912-1916`). The batch stops at `count`, or when `arc_rpc_mem_batch_next` returns false. On the quota's last instruction, `batch_next` is not called. [C]
11. **Batch end.** `inscount+=linecyc`, then `arc_rpc_mem_batch_flush()` if `integer_batch` is still set (`arm.c:1918-1919`). [C]

### 1.2 Base, requests and service

**The base precedes issue.** [C] Instruction j issues at `due_j = s + base`, where s depends on what came before:

| What came before j | s | Code |
|---|---|---|
| j−1 collected no requests | issue time of j−1 | non-integer `rpc_mem_timing.h:121-125`; integer `:114-118` |
| j−1 collected requests | completion time of j−1's service, applied when `commands()` empties the queue | `next_base(m.cursor)`, `rpc_mem_timing.c:544-549`; fast path `:663-675` |
| j is the first instruction after bind | 0 | `next_base(0)`, `:215` |
| j is the first instruction after idle wake | now | `next_base(ticks(now))`, `:694` |

While requests are pending, `cpu_due_*` is **unresolved**. No reader uses it until service completes:
- `arc_rpc_mem_next` reads it only when not pending (`:616`);
- `arc_rpc_mem_ready` requires not pending (`:627`);
- the video hold adjustment requires not pending (`:589`);
- `batch_next` takes the non-pending path only (`.h:145-155`). [C]

The two oracle paths leave different stale values: `end_slow` in integer mode flushes first, which sets `cpu_due = issue + base` (`:389,410`), while the non-integer path leaves the value from issue. [C]

**Requests are collected during the instruction and serviced after it.** [C]
- `cycle()` reads live `iomd.romcr0/1`, `clkctrl`, `dramwid`, `iotcr` and `ectcr` when a word **starts** (`rpc_mem_timing.c:132,424-425,430,443,465,472,482-485`).
- So an instruction that writes ROMCR has its own queued fetch costed with the new value.

**Issue happens only with an empty command queue.** `pending` is cleared only once `m.count==0` (`:544-545`), and issue requires `!pending` (`:627`; `.h:145-155`). [C]
- At issue, the only work in flight is **older work**: the buffer ring, the active packet, and queued or active video.
- The current instruction's requests are exactly the command-ring entries appended after its begin.

**Descriptor walks bypass timing.** They call `mem_phys_read32` directly (`cp15.c:387,393`), and that function never calls `arc_rpc_mem_access`; only `readmemfl/fb` and `writememfl/fb` do (`mem.c:581,589,597,605`). [C]
- `recording` is normally true during fetch and body.
- A walk through I/O space runs `iomd_read` or other device reads, with their side effects (`mem.c:158-185`). [C for the path; I for reachability]

**Charging edge cases.** [C]
- Aborted data accesses are not charged: the accessors return before `arc_rpc_mem_access` (`mem.c:580,588,596,604`).
- STRH is two charged byte writes (`arm_common.c:79-80`).

### 1.3 Interrupt sampling

**Sampled only at the event check.** IRQ/FIQ are sampled only at step 7 (§1.1). Device events are delivered only by the driver (`rpc_session.c:330-338,364-367`). A batch continues only while `due < boundary` (`rpc_mem_timing.h:149-152`). [C]

**An event-raised interrupt is taken at the next event check that is reached while it is still eligible** [I, narrowed per Astra 3]. That check is normally the end of the next instruction attempt. Any of the following can prevent or defer it:
- **A prefetch abort** skips the check.
- **A data abort** wins at that boundary.
- **The instruction body** can mask the interrupt, or acknowledge it through MMIO (`updateirqs`, `iomd.c:221-237`).
- **The fetch's own translation walk** can perform MMIO (§1.2), and so can change `arm.event` before the body runs.

**What can change interrupt eligibility inside a batch** [C]:
- `arm.event`, changed by any MMIO, including walk reads;
- the I/F bits. In 26-bit modes these are `r15[27:26]`, so they change through:
  - any write to raw R15 outside `r15_mask`: `arm_write_r15` (`arm_common.h:301-333`, privileged 26-bit mask `0xffffffff`), the P-forms through `arm_compare_rd15` (`:342-375`), MSR (`:386-423`), and LDM^ with PC (`arm_common.c:528`);
  - the final `+4` carry (§1.1 step 8);
- `exception()` itself (`arm.c:494-536`), called mid-body by SWI (`arm_common.c:571`) and Undefined (`arm.h:122-126`).

**`arm.event` is cached.** It is not recomputed from IOMD registers. For example, the IRQMSKD write does not call `updateirqs` (`iomd.c:386-388`). [C]

**26-bit `reg[16]`.** Its I/F bits are refreshed by the event check (`arm.c:1891-1894`) and by MRS (`arm.c:882-884`). [C]
- Correction to the draft: stale bits do **not** reach the SPSR. In the `prog32` path, `exception()` calls `updatemode()` first (`arm.c:517`). The 26→32 transition rebuilds `reg[16]` from R15 (`arm.c:177-181`) before the SPSR is formed (`arm.c:519`). [C]
- Raw `reg[16]` is still compared, because the refresh is a state mutation and `arc_rpc_test_snapshot` exposes `regs[16]` (`rpc_session.c:515`).

### 1.4 State one instruction can mutate

All of these are [C].
- **CPU registers:** `arm.reg[0..16]`.
- **Banked state through `updatemode`** (`arm.c:81-201`):
  - the bank arrays and `spsr[]`;
  - `mode`, `mmask` and `r15_mask`;
  - the derived pointers `usrregs`, `cpsr` and `pcpsr`;
  - `memmode`.
- **The abort bit** `arm.event` 0x40: set at `cp15.c:432`, cleared at `arm.c:1899` and `cp15.c:450`.
- **CP15 state** (`cp15.c:160-281`): `ctrl`, TTBR, DACR and FSR/FAR (data faults only, `:434-435`).
- **MMU globals:** `mmu`, `dcache`, `icache` and `prog32`. A `prog32`-clear write in a 32-bit mode also calls `updatemode` (`:186-188`).
- **The TLB.** A successful walk always installs, even when the access is then denied, because `cp15.c:415` runs before the checks at `:418-427`. A flush clears it (`:93`). `arc_rpc_attributes` is updated on each translation (`cp15.c:91,428`).
- **Guest-visible state:** guest memory, `dirtybuffer`, device state, scheduler deadlines, and `arc_rpc_cpu_idle` (`cp15.c:270`, `iomd.c:326`).
- **All timing state:** `m` and `arc_rpc_mem_fast`.
- **Partial transfers** are kept on abort, with the model's base rule (`arm_common.c:270-555`).
- **Write-only state:** `inscount`, `tlbs`, `flushes`, `pccache`, `unpredictable_count` and `dirtybuffer` are never read for behaviour. Grep finds no reader.

### 1.5 Batch, driver, quota and idle

**Driver loop** (`rpc_session.c:324-375`). At each `now`, the driver:
1. delivers every event due at or before `now`, in enum order;
2. wakes the CPU from idle if `arm.event&3`, **regardless of the CPU's I/F masks** (`:339,369`);
3. calls `arc_rpc_mem_idle`, then `arc_rpc_mem_run(now)`;
4. if `arc_rpc_mem_ready`, runs a batch with `quota=min(200, 1666667-work)` (`:343-348`).

**Driver-entry cases** (Astra 7). [C]
- **Strict-less-than applies to continuation only.** The `< boundary` rule governs batch **continuation** (`rpc_mem_timing.h:149-152`). It does not govern every CPU issue.
- **At the limit,** the driver delivers events and services the bus. If the CPU is ready, it then executes **one instruction at the limit itself** with `arc_rpc_arm_exec_batch(1,NULL)`: no batch and no continuation (`rpc_session.c:363-373`).
- **At field completion,** quota is 1 **only if `arc_rpc_mem_ready`**. Otherwise the driver returns without executing an instruction (`:346,352,355`).
- **Capture deadline.** It clips `limit` (`:320-321`), and the capture is evaluated at `now==deadline` (`:376-381`).
- **Backstop.** 1,666,667 instructions is a session fatal (`:343`).

**Continuation inside a batch** (`rpc_mem_timing.h:137-158`).
- It returns false on idle or on a test stop.
- With `pending||bus_busy`, it flushes integer mode and calls `arc_rpc_mem_batch_service` (`rpc_mem_timing.c:634-689`). That call can **advance `now_ns` and partially service work before returning false** (`:681-688`). The demand-only shortcut either commits (`:668-676`) or rolls back its ROM history (`:679`).
- Otherwise it requires `cpu_due_ns < boundary`. `next_ns` is re-read on every call, so an MMIO-armed delay-0 deadline (`fdc.c:528`) stops the batch. [C]

**Integer batch.** It is active only when all of these hold (`rpc_mem_timing.h:84-91`):
- `base_fraction==0`;
- the clock is aligned with the issue state;
- there is headroom of 200·base (the quota constant appears here and at `arm.c:562`);
- `!test_measure`.

`issue_*` is stale until `arc_rpc_mem_batch_flush` (`rpc_mem_timing.c:403-411`). That function calls `next_base`. [C]

**Idle.**
- **Set** by CP15 c15 wait (`cp15.c:270`) or by ARM7500 SUSMODE (`iomd.c:326`). Continuation then returns false (`rpc_mem_timing.h:141`).
- **While idle:**
  - `arc_rpc_mem_next` ignores CPU due (`rpc_mem_timing.c:616`);
  - pending writes keep draining through `arc_rpc_mem_run`;
  - a video hold does not push CPU due (`:589`);
  - `idle_ns` accrues (`rpc_session.c:359`).
- **Wake.** `arc_rpc_mem_idle(false)` sets due to now + base (`rpc_mem_timing.c:690-695`). [C]

**Quota transparency.** That splitting the work at 200 instructions leaves chronology unchanged is an inference [I] (§5 Q-7).

### 1.6 Session fatal and lifecycle

**Fatal.** `fatal()` (`rpc_session.c:46-60`):
- clears every armed deadline;
- `longjmp`s to the `setjmp` in advance (`:316`), reset (`:259`) or start (`:193`);
- after that, `arc_rpc_fault` refuses every later advance (`:314`). [C]

**Guest-reachable mid-instruction fatals:** [C]
- "Bad mode" (`arm.c:169`), with `arm.mode` already written at `:120`;
- the request budget (`rpc_mem_timing.c:168`) and the packet budget (`:179`);
- time overflow (`:79,85`).

**Lifecycle.** [C]
- **Start** (`rpc_session.c:179-222`): `arc_rpc_stop()` frees all memory (`:126-142`, `mem.c:75-82`). A failed start also runs `arc_rpc_stop` (`:193-196`). Allocation is a fresh `malloc` (`:75-84`), so **a later session can reuse the same host addresses**. Every successful start increments `arc_rpc_generation` (`:219`).
- **Reset** (`:252-272`) clears RAM (`mem.c:117-119`), rebinds timing, flushes the TLB (`cp15.c:133`) and resets the ARM. It **does not** bump the generation.
- **Test rate.** The rate override rebinds timing at time zero (`:490-499`).
- **Engine switching.** Arculator and RPC switch inside one library (`tests/rpc-session-expectations.md:39-41`).

### 1.7 Memory backing, mirrors and writers

**Allocations.** [C]

| Allocation | Size | Physical aperture | Mirroring |
|---|---|---|---|
| `rom` | 8 MiB | — | 2/4 MiB images are memcpy'd into it (`mem.c:66-73`) |
| `ram00` | 32 MiB | `0x10000000–0x13ffffff`, 64 MiB | **repeated twice** by `mem_rammask` (`mem.c:114,206`): `0x10xxxxxx`≡`0x12xxxxxx`, `0x11xxxxxx`≡`0x13xxxxxx` |
| `ram01` | 32 MiB | `0x14000000–0x17ffffff` | same pattern (`mem.c:212`): `0x14`≡`0x16`, `0x15`≡`0x17` |
| `vram` | 2 MiB | `0x02xxxxxx` | repeated through the 16 MiB region (`mem.c:115,156`) |
| `ram1` | — | — | NULL |

`arc_rpc_phys_page` returns pointers into these allocations at 1 KiB granularity (`mem.c:124-136`). [C]

**Writers of guest memory at runtime.** Only CPU stores write guest memory: `mem_phys_write32/8` (`mem.c:344-573`), reached only through `writememfl/fb` (`mem.c:593-608`). [C]
- ROM writes have no functional effect (`mem.c:354-448`).
- RiscPC floppy transfers are FIQ pseudo-DMA: the guest stores bytes it reads from the MMIO ports (`fdc.c:524-534`).
- Video and audio DMA only read memory (`vidc20.c:173,272`; `rpc_audio.c:165`).
- IDE is a stub (`rpc_session.c:110-116`).
- Wholesale writers: reset (`mem.c:117-119`) and ROM load (`mem.c:66-73`).
- Video holds are raised only inside the VIDEO event (`vidc20.c:347,359`).

### 1.8 Translation cache and fetch memo

**TLB.** `tlb[256]` is direct-mapped by `(t^t>>10^t>>18)&255`, with `t=VA>>10`. Fetch and data share it (`cp15.c:379-438`). [C]
- **Checked on every hit:** domain, AP, `memmode` (which LDRT/STRT set to 0, `mem.h:120-182`) and the S/R bits.
- **Stale descriptors:** they persist until the entry is evicted or flushed (`tests/rpc_cpu_test.c:184`).
- **Flush triggers:** a change to M/R/S; a TTBR write; a DACR change; ARMv3 c5/c6; SA c8 (`cp15.c:174-176,191-202,214-222,251-263`).

**Fetch memo.** It is a virtual-line-only key (`rpc_mem_timing.h:130–138`); since Q-3 a hit also requires a backed, cache-eligible physical region (`:135–137`).
- **Cleared by:**
  - **any** allocation in either cache, **SA D-cache fills included** (`rpc_mem_timing.c:335`);
  - control writes (`:220`) and TLB flush (`:230-233`);
  - c7 invalidation (`:369,382`);
  - SA slow-clock uncached reads (`:262`).
- **Not cleared** by a mode change. [C]
- **Defect (§5 Q-3) — resolved (PR #375, `44d007e`).** A memo hit did not check `backed` or the region, whereas the slow path requires both before any tag lookup (`rpc_mem_timing.c:306–309`). A collision eviction followed by a re-walk could change that eligibility without clearing the memo. The gate now mirrors the slow path; `rpc-fetch-memo-rewalk` reproduces the sequence. Note for 6b: in the integrated system `backed` implies a cache region (`mem.c:124–135`), so the region term never decides the outcome there, but it is kept to mirror the slow path exactly (Opus QA, S61).

---

## 2. Exit contract

### 2.1 Phases

Every exit lands on exactly one of the phases below. The resumer then performs exactly the oracle's next steps, no more and no fewer.

| Phase | Where | Materialised time representation [C] |
|---|---|---|
| **P0(k)** | `arm.c:571`, before k. Reached after continuation returned true, or by a driver issue through `arc_rpc_mem_ready`. The command queue is empty; older work has run to `now`. | `now_ns` = visible issue time of k. `issue_*` = issue of k and `cpu_due_*` = the same values (`rpc_mem_timing.h:153-156`; `rpc_mem_timing.c:629-631`). In integer mode, `issue_floor/ticks` are derived with the flush **formulas** (`rpc_mem_timing.c:408-409`) **without** `next_base`. Calling `batch_flush` here would set `cpu_due=issue+base` and so add a base: forbidden as a canonicaliser. |
| **P1(k)** | Mid-k, at a helper call, including successful-fetch continuation (after opcode capture, before condition), SWI entry and completed-transition observation (§6). | As P0. Plus k's collected requests in program order, `recording=true`, `opcode` set after a successful fetch (a prefetch-abort mode report retains the prior value), and `arc_rpc_attributes` as the current access's translation left it. |
| **P2(k)** | Just after `instruction_end(k)` (`arm.c:1911`), before continuation. | No requests: `cpu_due_*` = issue + base. Here a flush is equivalent (`:410`). With requests: `pending`, `cursor=issue_ticks`, `cpu_due_*` unresolved and excluded from comparison (§1.2). |
| **P3(k)** | After the continuation step returned false, or the quota/`NULL`-batch return, and the final flush (`arm.c:1912-1919`). | `now_ns` and the bus may have **advanced past P2** through batch service (`rpc_mem_timing.c:681-688`). The state is whatever service left; the resumer is the driver at `rpc_session.c:324`. |

### 2.2 Canonical comparison contract (Astra 6)

**Included fields.**
- **CPU:**
  - raw `ARMState`, including `reg[16]`, all banks, `spsr[]`, `event` and the model flags;
  - `memmode`;
  - the `cpsr` index;
  - pointers normalised by identity: `pcpsr` as the register index; `usrregs[i]` as (array, index); TLB `code` and `pccache2` as (allocation, offset).
- **CP15:** CP15 state, `mmu/prog32/dcache/icache`, `arc_rpc_attributes`.
- **TLB:** valid entries field-by-field. Invalid entries are compared by validity only.
- **Scheduler and devices:**
  - scheduler `now_ns`, `deadlines`/`armed` (armed only), `next_event`, `next_ns`, `cpu_fraction`;
  - device state;
  - `arc_rpc_cpu_idle`;
  - guest RAM and VRAM.
- **`arc_rpc_mem_fast`:**
  - `enabled`, `pending`, `bus_busy`, `memo_valid` and `memo_line` (the line only if valid), `recording`, `rom_sequential`, `after_branch`, `track_rom`, `fetch_pc`, `opcode`, `integer_batch`;
  - `instructions` and `fetch_hits`;
  - the time fields per phase (§2.1);
  - `base_*` and `denominator`.
- **`m`:**
  - `model`, `rate`, `control`;
  - `switching`, `fast_clock`, `idle`, `restart`;
  - per cache: valid tags (`va`, `physical`, `dirty`), `populated`, and every `victim[]` cursor;
  - the command ring and buffer ring: `head`/`count` and `bhead`/`bcount`, `bwords`, and the **live** entries only;
  - `active` only when `active_valid`, plus `active_kind` and `grant_left`;
  - `cycle_active`, `video_active`, `queued_video`, `video_arrival`;
  - `cursor`, `bus_cursor`, `finish` (only while a cycle or video is active), `wait_start`, `last_rom`;
  - every `stats` field;
- **Measurement state, when `ARCDROID_TEST_FAULTS` is enabled:** `test_measure`, `test_stop`, `measure_first`/`last`/`drained`, and the interval.
- **RPC observer semantic state (R3c-1, §6):** epoch, ordered event and active
  instruction identities, original VA/backing/mode context, owner generations
  and enabled filters (callback pointers normalised by owner), live watches and
  their deadlines/return eligibility, future answer intent/order, and live copied
  host requests with generation/epoch/media/time. Inactive slots and diagnostic
  trace buffers/counters are excluded. Host handlers are identified by owner.
- **Counts:** `linecyc`, `instruction_work`, `arc_rpc_executed`, `arc_rpc_idle_ns`.

**Excluded fields.**
- Packet lanes at index ≥ `count`. `append()` deliberately leaves stale lanes (`rpc_mem_timing.c:170-175`).
- Ring slots outside the live window.
- `va`/`physical` of invalid tags. `invalidate()` keeps them (`:367`).
- Write-only state (§1.4), including `dirtybuffer` (Q-5).
- Diagnostic post-fatal registers (Q-9).

**Hit counts.** `m.stats.hits` and `fetch_hits` must be **individually** identical. The draft's "the sum suffices" is withdrawn. The split stays until Fable accepts an explicit projection.

### 2.3 Exit kinds

| # | Exit | Lands at | Must be materialised | Must NOT be repeated on resume |
|---|---|---|---|---|
| E1 | Block end, continuing | P0(k+1), when the block itself performed the exact continuation | §2.2 at P0 | Nothing of k |
| E2 | Guard miss before k | P0(k) | §2.2; **no part of k**: no `instructions++`, no TLB fill or walk, no memo or tag effect, no base. Guards use non-mutating probes only (`lookup()` is pure, `rpc_mem_timing.c:152-164`) | — (k runs whole) |
| E2′ | Guard miss after k's fetch | **Disabled** (Q-1) | — | — |
| E3 | Helper call inside k | P1(k) | Before the call: P1 contents and `now_ns` = issue of k (read via `rpc_timing.c:5`). After it, re-read `arm.event`, registers, mode and `r15_mask`, the TLB and memo generations, `next_ns`, idle, `base_*`, `pending` | Earlier functional effects of k; MMIO reads that pop FIFOs (`fdc.c:524-528`) |
| E4 | Side exit after k (mode or mask change, idle, MMIO, CP15, c7, invalidation) | P2(k); the resumer runs continuation **once** | §2.2 at P2 | Fetch, transfers and base of k |
| E5 | Deadline, limit, quota, idle or test stop | P3(k), with `linecyc` returned | §2.2 at P3, including any service progress | Service already performed; k's base |
| E6 | IRQ/FIQ at k's event check | P2(k) (PC = vector; `arc_rpc_mem_branch`, `arm.c:496`) | §2.2 | k's body; the exception entry |
| E7a | Data abort | P2(k) | Partial functional and charged transfers; FAR/FSR; the model's base rule; no FIQ/IRQ at this boundary | Completed transfers and their packets |
| E7b | Prefetch abort | P2(k) via `instruction_done` | Base resolved; fetch charged only for absent backing; no event check | The exception entry; the base |
| E8 | Session fatal | `longjmp` | Translator bookkeeping must survive the unwind and be released by `arc_rpc_stop` (G-13) | No retry after partial effects |
| E9 | Store into translated code | P2(k), after k completes atomically (remaining STM transfers, abort, event check, `instruction_end`) | Dependencies invalidated at the functional write | k's stores and base; the next fetch rereads memory |
| E10 | SWI/Undefined (mid-body `exception()`) | P2(k) after the event check. FIQ can still be taken there, since only I is set (`arm.c:500-505`) | §2.2 | The exception entry |
| E11 | Cache maintenance only (c7) | P2(k), as E4. c7 may append DRAIN (`rpc_mem_timing.c:384`) or castouts (`:381`) and advances the G-5 generation | §2.2 | The maintenance effects |

---

## 3. Guard invariants

**G-1 Budget and driver entry.**
- **Continuation.** Within a batch, instruction i may issue only if ⌈tᵢ⌉ < min(`next_ns`, `limit`), re-evaluated after every helper (`rpc_mem_timing.h:149-152`).
- **Base step.** tᵢ = tᵢ₋₁ + b holds only when i−1 collected no requests (§1.2).
- **Pending work.** With `pending||bus_busy`, every boundary goes through batch service. A fold across such boundaries requires G-8.
- **Driver entries** must also be reproduced exactly: the `NULL`-batch instruction at the limit, quota 1 only when ready at field completion, the capture-deadline clip, and the 1,666,667 backstop (§1.5).

**G-2 Block key.** The key is all of:
- the PC VA (`arm.h:67`);
- the full 5-bit `arm.mode`. Privilege selects the R15 masks (`arm_common.h:308-317,349-369`) and `memmode`;
- `prog32` (`arm.c:517-523`);
- the session model (`arm.c:280-290`);
- `arc_rpc_generation` and the reset epoch (G-13).

Any `updatemode` ends the block (E4).

**G-3 Interrupt state.**
- **Per-instruction check.** After **every** instruction that may change eligibility, the event check must run exactly as `arm.c:1890-1907`. That includes the 26-bit `reg[16]` refresh whenever `event≠0`, even if masked. The instructions are:
  - any MMIO, including a translation walk reading I/O;
  - any write to raw R15 outside `r15_mask`, and the final `+4` carry;
  - MSR, the P-forms, S-forms with Rd=15, LDM^ with PC;
  - SWI/Undefined/CP15.
- **Entry.** If an enabled interrupt is pending at P0, the block may run at most one instruction attempt. It then exits through the oracle's own check; it does not assume that the exception is taken (§1.3).
- **When pending state is constant.** In a block with no MMIO, no walk that can reach I/O, and none of the instructions above, interrupt-pending state is constant [I].

**G-4 Fetch translation (shared TLB).**
- **Skipping per-fetch translation** requires all of these to hold since the last real translation of that 1 KiB tag:
  - no flush;
  - no data access installed into any fetch slot in use. Installs include denied accesses (`cp15.c:415` precedes `:418-427`); translation faults install nothing;
  - `memmode`, DACR, S/R and `mmu` unchanged.
- **After an eviction,** the real re-walk must be performed at the fetch the oracle would perform it, never speculatively (Q-10).
- **Attributes.** `arc_rpc_attributes` must equal the oracle's value whenever a timing call reads it (`cp15.c:92,428,455`).
- **Permission hoisting** needs a Fable ruling before Phase 1 (constraint 3).

**G-5 Instruction-cache residency.**
- **What a skipped fetch-timing step must reproduce:** the memo state, `stats.hits` and `fetch_hits` **separately** (§2.2), unchanged victim cursors, `instructions`, and the burst-ROM sequential state (`rpc_mem_timing.h:94-113`; `rpc_mem_timing.c:299-304,449-459`).
- **The generation advances on every memo clear:**
  - any allocation in **either** cache, SA D fills included (`:335`);
  - c7 (`:371-385`), control writes and TLB flush (`:220,232`);
  - SA uncached-read clock handling (`:262`);
  - bind or reset.
- **Tie to G-4.** The proof is tied to G-4, because eligibility depends on the current region and backing (`:309`).
- **Retained tags.** Retained tags still hit with C/I clear (`:154-156,270`).

**G-6 Unified caches (ARM610/710/7500).**
- **Shared array.** A cacheable data load miss allocates in the same array as code. It advances that set's cursor and can evict an executing line (`rpc_mem_timing.c:325-343`).
- **Stores.** Store hits emit a write and leave tags unchanged. Store misses do not allocate.
- **Data accesses therefore** use the real lookup, plus revalidation of the fetch proof or an exit (Q-11). Any allocation advances the fetch generation.

**G-7 SA-110 split caches and any data-side filter.**
- **Guards a filter must check:**
  - permission before the hit (`mem.c:595-597`), keyed by effective privilege (including LDRT/STRT), direction, DACR, S/R and `mmu`;
  - **tag validity and replacement state** (`lookup`, victim cursors);
  - **the current backing and region** (`eligible = backed && cache_region(r)`, `rpc_mem_timing.c:309`);
  - the empty-D-tag fast path, taken when C/W are clear (`:290-297`).
- **Load hit:** zero bus, `stats.hits++`.
- **Store hit:** dirty half `(va>>4)&1` with no packet (`:323`). The functional store still happens; ROM stores are functionally ignored but cast out under G7 (`:186-194,333`).
- **Load miss:** allocate, cast out dirty victim halves, fill, and clear the memo.
- **Store miss:** no allocation; buffered only with M, W and B set in a memory region (`:347-348`).
- **SWP/SWPB stay on the exact path.** Their `mapped_swap`/`cached_swap` locking and write-allocate cases (`:305-315,325,341`) are outside any filter.
- **Clock switch.** An uncached SA read can switch the base clock (`:257-264`).
- **Ruling needed.** A filter needs its own ruling (constraint 3).

**G-8 Lazy bus catch-up: older work only.**
- **Catch up older work to the issue time of k at P0(k),** before k is entered, whenever k may:
  - enqueue a request;
  - perform any MMIO, including IOMD writes to ROMCR0/1, CLKCTL, DRAMWID, IOTCR or ECTCR (`iomd.c:344,391,394,431,435,450`);
  - execute c7 drain or clean;
  - switch the clock or go idle;
  - reach a boundary or return to the driver.

  "Older work" is the buffer ring, the active packet and queued video (§1.2).
- **Never service k's own collected requests before k's remaining functional effects.** The ROMCR example: k's queued fetch must be costed with the value k writes (§1.2).
- **Equal-ns order:** events → `arc_rpc_mem_run` (completion, release, grant: video, then buffer FIFO, then CPU demand last) → CPU issue (`rpc_session.c:330-342`; `rpc_mem_timing.c:558-606`).
- **What equivalence must show:** identical word start, finish and release ticks, `buffer_wait_ticks`, hold placement (`:585-593`) and `restart`. Bulk subtraction of elapsed time is not a proof.

**G-9 Self-modifying code.**
- **Key.** Dependencies are keyed by (allocation, offset) from the `code` pointer (`cp15.c:415,454`). For example `0x10000100` and `0x12000100` are the same `ram00` bytes. `0x11000100` and `0x13000100` alias each other at offset `0x1000100`, which is a **different** offset from `0x10000100`.
- **Hook.** Invalidate at `mem_phys_write32/8` (the only runtime writers, §1.7), after the permission check and for RAM/VRAM only.
- **Coverage.** The hook covers STR, STRB, STRH (two bytes), STM and STM^ (per word), SWP/SWPB, STRT/STRBT, and stores made by oracle routines.
- **After the write.** k completes, then the block exits (E9).

**G-10 Guest cache maintenance is timing-only.**
- **No functional effect.** c7 has no functional effect on fetched bytes (`cp15.c:244-249`; `rpc_session.c:101`).
- **Timing effect.** It advances the G-5 generation and exits as E11.
- **Not an SMC trigger.** It must never be the trigger for G-9.

**G-11 Time representation.**
- **Phase-specific values.** Each phase in §2.1 has its own time values. Reach them by the oracle's own formulas, never by calling `batch_flush`/`next_base` where the oracle would not.
- **Base changes.** A base change applies from the next `next_base` (`rpc_mem_timing.c:217-229`).
- **Overflow.** Overflow fatals must fire at the same instruction (`:77-87`; `.h:86,107`).

**G-12 Counters, measurement and observation barriers.**
- **Counters.** All §2.2 counters must match at every exit.
- **What measurement mode changes.** Under `test_measure`:
  - integer batching is disabled and slow begin/end runs on every instruction (`rpc_mem_timing.h:74-76,87-88,108-111`);
  - the run stops at `measure_last` (`rpc_mem_timing.c:393`; `.h:142-144`).
- **What it does not change.** The **fetch memo** (`.h:133`) and the **demand-only service shortcut** (`rpc_mem_timing.c:642-678`) still run.
- **Observation barriers.** Every enabled trace or debug hook needs materialised state at its observation point:
  - event trace in `arc_rpc_deliver` (`rpc_session.c:279-286`);
  - interval start and end (`rpc_mem_timing.c:66-75,246-252,578-581`);
  - snapshot and `read_ram` (`rpc_session.c:510-523`);
  - R3c-1 successful-fetch continuation before condition/body, condition-passed
    SWI entry before exception entry, and completed body/event-check mode events
    (§6). No batch flush or early service is permitted at these barriers.

  This requires no new debugger.

**G-13 Lifecycle invalidation.**
- **Drop everything.** Drop every block, dependency and generation at reset, unload or stop, failed start, test-rate rebind, and Arculator↔RPC switch (§1.6).
- **Address reuse.** Keys must include `arc_rpc_generation` plus a reset epoch, because reset does not bump the generation and fresh allocations can reuse host addresses.
- **Executing code.** Code that is executing is never reclaimed immediately (constraint 4). After an E8 unwind, no reference may stay pinned.

**G-14 Idle.**
- **Entry.** An idle-setting instruction exits at P2 and then P3 (E4/E5).
- **Wake.** Only the driver wakes the CPU: on `arm.event&3` regardless of masks, then a new base from now (§1.5).
- **Writes during idle.** Pending writes drain during idle. Tests must cover writes across sleep and wake.

---

## 4. Equivalence evidence and existing coverage

**Method.** Unless noted:
- isolated deterministic runs, or a full snapshot and restore, with identical inputs;
- §2.2 comparison at every exit;
- forced exits at every instruction position;
- coverage counters proving that native execution, every guard and every fallback reason ran.

| Inv. | The equivalence test must show | Existing coverage |
|---|---|---|
| G-1 | Issue ticks; delay-0 MMIO deadlines; the limit `NULL`-batch instruction; quota 1 only-if-ready; the capture clip; quotas 1, 7 and 200 agree in chronology and live state (Q-7) | Partial: `rpc-plan-devices.trace`, `rpc_session_test.c` (equal-timestamp timers, `tests/rpc-session-expectations.md:14-21`), flat/burst agreement (`rpc-plan-expectations.md:63-65`), `rpc_hot_test.py`. **None** checks per-instruction ticks or the limit/field cases directly |
| G-2 | Mode and privilege changes exit and re-key; raw R15 arithmetic, including the 26-bit carry (Q-6) | `rpc-cpu.trace` SVC26/SVC32 (`tests/rpc-cpu-expectations.md:10`), `rpc-plan-matrix-*`. **No test** for the carry or for user-mode R15 masks in hot code |
| G-3 | The event check after every eligibility-changing instruction; eligibility narrowed by prefetch abort, data abort, body masking and walk MMIO; the 26-bit refresh while masked | `rpc-plan-devices` ("forced FIQ taken exactly once"; timer IRQ 0x60). **No test** pins the ordinal of an event-raised IRQ or the masked-refresh case |
| G-4 | A data access, including a denied one, evicting the fetch slot causes a real re-walk at the next fetch; no speculative walks | `rpc_cpu_test.c:252-292` (data-data m1, fetch privilege m4, c6 m11), `:184` (stale persists). **No test** for data evicting the fetch entry |
| G-5 | Memo, cursors, `stats.hits` and `fetch_hits` individually equal; burst-ROM state; the SA D-fill memo clear | Calibrations (4,677, `tests/rpc-timing-expectations.md:79`) and `rpc_qa_mutations.py`. Under `test_measure` the integer batch is off, but the memo and the demand shortcut are exercised (G-12). Integer-path parity rests only on `rpc_hot_test.py` (count and base) |
| G-6 | A unified data fill evicting the executing line | `cache-geometry` probes (`rpc-timing-expectations.md:14-46`; code in sets 1/2). **No test** with data and code in one set mid-sequence |
| G-7 | Filtered hits equal real hits: validity, backing and region, dirty halves, G7 castout, LDRT/STRT; SWP excluded | `rom-castout-*`, SA geometry probes, `rpc_cpu_test` m12 |
| G-8 | Lazy and eager drain give identical per-word ticks; a timing-register write mid-drain; k's own requests costed after k's effects | `rpc_mem_timing_unit.c` (request-level holds and boundaries); buffer-boundary calibrations (`rpc-timing-expectations.md:81-91`); `rpc-d2-*`/`rpc-video-*` hashes. **No test** writes ROMCR/CLKCTL while draining, or in the instruction whose own fetch it reprices |
| G-9 | Invalidation through aliases (`0x10↔0x12`, `0x11↔0x13`, VRAM) and through every store form; a store to PC+4; STM partial abort | `rpc_plan_matrix_rom.py:53-60` (SMC through `MOV pc`); `rpc_cpu_test.c:107` (functional mirror). **No test** for aliased, same-block or PC+4 SMC |
| G-10/E11 | c7 changes timing only; exact exit | None directly |
| G-11 | Phase-correct rational state across integer and non-integer modes and SA clock switches | SA clock-disable calibrations (`rpc-timing-expectations.md:92-101`) |
| G-12 | Counters; measurement endpoints; barrier materialisation | Calibrations check instructions, words, fills and packets (`rpc_timing_test.c:52-56`) |
| G-13 | Reset, reload, failed start and engine switch with reused addresses leave no stale block | Allocation-reuse and switching tests (`rpc-session-expectations.md:35-41`) cover the oracle only. **No translator test** |
| G-14 | Writes pending across sleep and wake; wake while masked | Request-level idle drain (`rpc-timing-expectations.md:103-110`); session SA wait and SUSMODE (`rpc-session-expectations.md:6-10`) |

**Why per-instruction differential testing is required.** Goldens observe state only at frontend boundaries and through guest-recorded RAM (`rpc_session.c:510-523`). A divergence that later self-corrects can therefore pass them all.

---

## 5. Rulings (D-146)

- **Q-1 Post-fetch resume.** Use only non-mutating pre-fetch probes for now; E2′ is disabled. A post-fetch resume entry point may be reviewed separately later, with differential evidence beyond the existing goldens.
- **Q-2 Calibration measurement.** Add non-intrusive endpoint measurement alongside the existing measurement mode. Keep the exact first and last ordinals, the drain semantics and the stop behaviour. Require counters that prove the production fast paths executed.
- **Q-3 Memo eligibility defect — fixed by PR #375 (`44d007e`).** Fix the oracle before translator qualification. A separate R2a corrective work order, opened now, covers a targeted reproduction and the smallest fix: a virtual-line-only memo must not override changed eligibility. No blanket golden regeneration.
- **Q-4 All-writers rule.** Keep it, with device writes labelled as future scope. Floppy transfers reach RAM through CPU stores. Re-audit new writers after R2c/R3.
- **Q-5 `dirtybuffer`.** Excluded from semantic parity at this baseline, since it has no reader. Record the exclusion and re-audit at R4 before inline stores omit it.
- **Q-6 Raw R15 arithmetic.** Match it exactly, including the 26-bit carry. Any fidelity fix is separate work. Boundary tests use `PC=(reg[15]-8)&r15_mask` and never equate PC with raw R15.
- **Q-7 Quota transparency.** Quotas 1, 7 and 200 must be compared before batching changes. Compare guest chronology and live semantic state across fractional timing, pending service, interrupts, idle and boundaries, not local `linecyc`.
- **Q-8 FIQ after a prefetch abort.** Match the oracle exactly. A pending FIQ is reconsidered at the handler's next ordinary event check; another prefetch abort or a mask change can defer it further.
- **Q-9 Post-fatal state.** Diagnostic registers after a fatal are excluded from equivalence. Required instead: equivalent guest-fault behaviour, preserved prior committed effects, stopped execution, and safe teardown and reload. Never retry after partial effects.
- **Q-10 Guard-time walks.** Forbidden. A walk runs only as the actual fetch or access the oracle would perform, with its effects retained exactly once.
- **Q-11 Hard spots.** These are costs to measure, not prerequisites that expand scope.
  - Slot generations can track TLB replacement.
  - Unified-model loads can use the real lookup plus revalidation or an exit, without a data filter.
  - MMIO still requires chronological catch-up of older work.

**Phase order (D-142) stands.**
- **Start with the fetch path.** Phase 1 starts with exact fetch-path overhead reduction that keeps every per-fetch check.
- **Permission reuse needs a ruling.** Any guarded reuse of a permission check is benchmarked only under an explicit Fable ruling.
- **Not yet justified.** Neither the S59 profile nor the executable-memory probe justifies bypassing the minimal portable foundation or advancing the guest-rate decision.

## 6. R3c-1 observation equivalence (D-174/D-175)

The contract in `riscpc-observation.md` adds G-12 barriers without changing
P0–P3 or enabling E2′. SWI entry is a P1 helper after condition success, before
exception entry. Completion writes only approved R0–R12 and C/V, then the
ordinary event check and raw R15 +4 lead to P2/P3. It adds no branch cost.
A successful-fetch continuation is P1 before condition/body, even when the
condition fails; prefetch abort reports no continuation. Its first requested-mode
return is either a match or disqualification and retires the watch. Mode events
report full old/new modes after the completed body and after the event check,
including CP15 and exception returns. Banks/SP/LR are settled; raw R15 can still
be transitional before its final +4. Origin VA and backing remain those fetched.

Active observation requires visible issue `now_ns` without batch flush,
`next_base` or early service of current requests. Inspector descriptor walks
never install execution translations, even on a miss. Canonical equivalence
includes observer epoch/generations, watch identities/deadlines/return outcomes,
pending future answer intent, copied host requests and cancellation state.
Host work drains only after a protected frame entry returns; both revalidations
must agree. Trace buffers and loss/failure state are a separate diagnostic domain,
excluded from guest projection and unable to cancel observers. G-9/G-10 remain
unchanged: inspection never writes memory or affects cache maintenance.

Run B selects an observation-free interpreter instantiation at batch admission
when nothing is armed. Both instantiations share one source body and identical
P0–P3, integer-batch and service boundaries; admission is not an early yield.
An armed callback may disarm during the batch without creating a later event.
Trace classes/SWI selection, recoverable gap counts/windows and incomplete status
remain diagnostic state and cannot change canonical guest or observer state.

D-176 restricts continuation admission to user-mode SWI callbacks and user-mode
returns (mode 0/16), refusing all other requests. Successful-fetch eligibility
requires a completed status-restoring PC write into that mode or controlled
completion; trace watches use the identical rule. Trace event/attempt/watch
counters are separate from canonical observer identities, which must compare
equal with tracing enabled or disabled.

R3c-2 adds the verified firmware tuple, actual CPU/profile revision, per-cell
capabilities, explicit launch-phase arms and parameterised pure observer states
(error modal knowledge, form/button qualification and learned poll identity;
disc identities and copied misses; launch outcome and boot prompt deadlines).
The dispatcher identity cache also carries validated helper/IDE names, the raw
ten-byte IDE record name, known bits and its epoch. Guest IDE record writes
invalidate it before the backend write. A successful write with unchanged names
and valid boot identity can restore prior knowledge from its payload; changed
names and failures stay manual until lifecycle refresh. Frame-boundary queries
perform no I/O.
Watch admission/expiry/cancel/replacement loss synchronously retires eligibility.
The bounded host queue also carries each miss's copied operation, continuation,
SP/watch and canonical object, so two same-frame misses cannot exchange identity.
The inactive instantiation and P0-P3 timing boundaries are unchanged. Preparation
owns diagnostic drains outside execution and before isolated-session retirement;
requested observers bind only after attachment.
