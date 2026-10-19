<!-- SPDX-License-Identifier: GPL-2.0-only -->

# 6b Phase 0 — timing-contract amendments: exit contract and guard invariants

Status: Accepted as the item 6b Phase-0 basis (D-146) after the S59 Astra check (ACCEPT WITH CHANGES, all incorporated). Not an implementation authorisation.

Phase-0 findings were grounded at `ee364d9` and challenged at `6cde473`.
Explicit rpc_mem_timing.c citations below are refreshed for the L1 working
tree (2026-10-17); S1 will shift them again. The D-195 endpoint section
supersedes intrusive-only coverage claims.
Historical decision/prototype citations retain their stated baselines.
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

### 1.1 Per-instruction order (`arm.c:585-1971`)

1. **Begin.** `arc_rpc_mem_instruction_begin()` (`arm.c:600`) increments `arc_rpc_mem_fast.instructions`, sets `recording`, and copies `rom_sequential` into `track_rom` (`rpc_mem_timing.h:86-96`). [C]
2. **Fetch, at every instruction.** `getpccache(PC)` (`arm.c:603`) calls `checked_translation(PC,0,1)` when `mmu` is set (`cp15.c:473`). It then calls `arc_rpc_mem_fetch` (`cp15.c:483`). Nothing caches an instruction pointer across iterations except the TLB entry's `code` pointer (`cp15.c:54,433`). [C]
3. **Prefetch abort** (`arm.c:604-608`). The sequence is `exception(ABORT,0x10,4)`, then `reg[15]+=4`, then `goto instruction_done`. **The event check is skipped.** [C]
   - An MMU fault records no fetch, because `cp15.c:474-477` returns before `cp15.c:483`.
   - Absent backing with a valid translation **does** record a fetch (`cp15.c:479-483`, `backed=false`).
4. **Opcode read.** The opcode is read from the backing allocation at fetch time (`arm.c:610`). Nothing in the loop retains a fetched word, so a store by instruction k to k+1 is seen by k+1's fetch. [C for the read; I for "no prefetch"]
5. **Opcode record.** `arc_rpc_mem_opcode` (`arm.c:611`) records the opcode. Data requests are then classified by `swap()`/`multiple()` (`rpc_mem_timing.c:629-630`). [C]
6. **Condition and body.** The condition is tested next (`arm.c:619`). The body follows. Each data access goes through `readmemfl/fb` or `writememfl/fb`:
   1. `translateaddress`, which is `checked_translation`;
   2. `arc_rpc_mem_access`, which only **collects** a timing request;
   3. the functional `mem_phys_*` call (`mem.c:704-739`). [C]
7. **Event check** (`arm.c:1936-1953`), whenever `arm.event!=0`. [C]
   - In 26-bit modes it first copies I/F from `r15[27:26]` into `reg[16][7:6]` (`arm.c:1937-1940`). This happens even when every pending interrupt is masked.
   - Priority is data abort (bit 0x40, then cleared), then FIQ (if F is clear), then IRQ (if I is clear). At most one exception is taken per boundary.
8. **PC advance.** `reg[15]+=4` (`arm.c:1955`) is raw arithmetic on R15. In 26-bit modes it can carry into bit 26 (F) at PC `0x3fffffc`. [C for the code; I for the carry]
9. **End.** `arc_rpc_mem_instruction_end(PC)` (`arm.c:1963`; `rpc_mem_timing.h:124-148`) does three things:
   - it records burst-ROM discontinuity;
   - if requests are pending, it calls `end_slow`, which sets `m.cursor=issue_ticks` (`rpc_mem_timing.c:817-832`);
   - otherwise it resolves the next due time (§1.2).
10. **Continuation** (`arm.c:1966-1970`). The batch stops at `count`, or when `arc_rpc_mem_batch_next` returns false. On the quota's last instruction, `batch_next` is not called. [C]
11. **Batch end.** `inscount+=linecyc`, then `arc_rpc_mem_batch_flush()` if `integer_batch` is still set (`arm.c:1977-1978`). [C]

### 1.2 Base, requests and service

**The base precedes issue.** [C] Instruction j issues at `due_j = s + base`, where s depends on what came before:

| What came before j | s | Code |
|---|---|---|
| j−1 collected no requests | issue time of j−1 | non-integer `rpc_mem_timing.h:143-147`; integer `rpc_mem_timing.h:136-140` |
| j−1 collected requests | completion time of j−1's service, applied when `commands()` empties the queue | `next_base(m.cursor)`, `rpc_mem_timing.c:982-987`; fast path `rpc_mem_timing.c:1134-1153` |
| j is the first instruction after bind | 0 | `next_base(0)`, `rpc_mem_timing.c:552` |
| j is the first instruction after idle wake | now | `next_base(ticks(now))`, `rpc_mem_timing.c:1195` |

While requests are pending, `cpu_due_*` is **unresolved**. No reader uses it until service completes:
- `arc_rpc_mem_next` reads it only when not pending (`rpc_mem_timing.c:1070`);
- `arc_rpc_mem_ready` requires not pending (`rpc_mem_timing.c:1081`);
- the video hold adjustment requires not pending (`rpc_mem_timing.c:1031`);
- `batch_next` takes the non-pending path only (`rpc_mem_timing.h:174-184`). [C]

The two oracle paths leave different stale values: `end_slow` in integer mode flushes first, which sets `cpu_due = issue + base` (`rpc_mem_timing.c:820,848`), while the non-integer path leaves the value from issue. [C]

**Requests are collected during the instruction and serviced after it.** [C]
- `cycle()` reads live `iomd.romcr0/1`, `clkctrl`, `dramwid`, `iotcr` and `ectcr` when a word **starts** (`rpc_mem_timing.c:357,862-863,868,881,903,910,920-923`).
- So an instruction that writes ROMCR has its own queued fetch costed with the new value.

**Issue happens only with an empty command queue.** `pending` is cleared only once `m.count==0` (`rpc_mem_timing.c:982-983`), and issue requires `!pending` (`rpc_mem_timing.c:1081`; `rpc_mem_timing.h:174-184`). [C]
- At issue, the only work in flight is **older work**: the buffer ring, the active packet, and queued or active video.
- The current instruction's requests are exactly the command-ring entries appended after its begin.

**Descriptor walks bypass timing.** They call `mem_phys_read32` directly (`cp15.c:405,411`), and that function never calls `arc_rpc_mem_access`; only `readmemfl/fb` and `writememfl/fb` do (`mem.c:709,718,727,736`). [C]
- `recording` is normally true during fetch and body.
- A walk through I/O space runs `iomd_read` or other device reads, with their side effects (`mem.c:207-234`). [C for the path; I for reachability]

**Charging edge cases.** [C]
- Aborted data accesses are not charged: the accessors return before `arc_rpc_mem_access` (`mem.c:707,716,725,734`).
- STRH is two charged byte writes (`arm_common.c:80-81`).

### 1.3 Interrupt sampling

**Sampled only at the event check.** IRQ/FIQ are sampled only at step 7 (§1.1). Device events are delivered only by the driver (`rpc_session.c:417-425,454-457`). A batch continues only while `due < boundary` (`rpc_mem_timing.h:178-181`). [C]

**An event-raised interrupt is taken at the next event check that is reached while it is still eligible** [I, narrowed per Astra 3]. That check is normally the end of the next instruction attempt. Any of the following can prevent or defer it:
- **A prefetch abort** skips the check.
- **A data abort** wins at that boundary.
- **The instruction body** can mask the interrupt, or acknowledge it through MMIO (`updateirqs`, `iomd.c:221-237`).
- **The fetch's own translation walk** can perform MMIO (§1.2), and so can change `arm.event` before the body runs.

**What can change interrupt eligibility inside a batch** [C]:
- `arm.event`, changed by any MMIO, including walk reads;
- the I/F bits. In 26-bit modes these are `r15[27:26]`, so they change through:
  - any write to raw R15 outside `r15_mask`: `arm_write_r15` (`arm_common.h:304-336`, privileged 26-bit mask `0xffffffff`), the P-forms through `arm_compare_rd15` (`arm_common.h:354-387`), MSR (`arm_common.h:398-435`), and LDM^ with PC (`arm_common.c:529`);
  - the final `+4` carry (§1.1 step 8);
- `exception()` itself (`arm.c:505-548`), called mid-body by SWI (`arm_common.c:571`) and Undefined (`arm.h:122-126`).

**`arm.event` is cached.** It is not recomputed from IOMD registers. For example, the IRQMSKD write does not call `updateirqs` (`iomd.c:386-388`). [C]

**26-bit `reg[16]`.** Its I/F bits are refreshed by the event check (`arm.c:1937-1940`) and by MRS (`arm.c:917-919`). [C]
- Correction to the draft: stale bits do **not** reach the SPSR. In the `prog32` path, `exception()` calls `updatemode()` first (`arm.c:529`). The 26→32 transition rebuilds `reg[16]` from R15 (`arm.c:185-189`) before the SPSR is formed (`arm.c:531`). [C]
- Raw `reg[16]` is still compared, because the refresh is a state mutation and `arc_rpc_test_snapshot` exposes `regs[16]` (`rpc_session.c:672`).

### 1.4 State one instruction can mutate

All of these are [C].
- **CPU registers:** `arm.reg[0..16]`.
- **Banked state through `updatemode`** (`arm.c:89-209`):
  - the bank arrays and `spsr[]`;
  - `mode`, `mmask` and `r15_mask`;
  - the derived pointers `usrregs`, `cpsr` and `pcpsr`;
  - `memmode`.
- **The abort bit** `arm.event` 0x40: set at `cp15.c:453`, cleared at `arm.c:1945` and `cp15.c:475`.
- **CP15 state** (`cp15.c:163-291`): `ctrl`, TTBR, DACR and FSR/FAR (data faults only, `cp15.c:455-456`).
- **MMU globals:** `mmu`, `dcache`, `icache` and `prog32`. A `prog32`-clear write in a 32-bit mode also calls `updatemode` (`cp15.c:196-198`).
- **The TLB.** A successful walk always installs, even when the access is then denied, because `cp15.c:433` runs before the checks at `cp15.c:438-444`. A flush clears it (`cp15.c:96`). `arc_rpc_attributes` updates only on successful translation and permission; faults retain its previous value (`cp15.c:94,449`).
- **Guest-visible state:** guest memory, `dirtybuffer`, device state, scheduler deadlines, and `arc_rpc_cpu_idle` (`cp15.c:280`, `iomd.c:326`).
- **All timing state:** `m` and `arc_rpc_mem_fast`.
- **Partial transfers** are kept on abort, with the model's base rule (`arm_common.c:271-556`).
- **Write-only state:** `inscount`, `tlbs`, `flushes`, `pccache`, `unpredictable_count` and `dirtybuffer` are never read for behaviour. Grep finds no reader.

### 1.5 Batch, driver, quota and idle

**Driver loop** (`rpc_session.c:411-465`). At each `now`, the driver:
1. delivers every event due at or before `now`, in enum order;
2. wakes the CPU from idle if `arm.event&3`, **regardless of the CPU's I/F masks** (`rpc_session.c:426,459`);
3. calls `arc_rpc_mem_idle`, then `arc_rpc_mem_run(now)`;
4. if `arc_rpc_mem_ready`, runs a batch with `quota=min(200, 1666667-work)` (`rpc_session.c:430-438`).

**Driver-entry cases** (Astra 7). [C]
- **Strict-less-than applies to continuation only.** The `< boundary` rule governs batch **continuation** (`rpc_mem_timing.h:178-181`). It does not govern every CPU issue.
- **At the limit,** the driver delivers events and services the bus. If the CPU is ready, it then executes **one instruction at the limit itself** with `arc_rpc_arm_exec_batch(1,NULL)`: no batch and no continuation (`rpc_session.c:453-463`).
- **At field completion,** quota is 1 **only if `arc_rpc_mem_ready`**. Otherwise the driver returns without executing an instruction (`rpc_session.c:436,442,445`).
- **Capture deadline.** It clips `limit` (`rpc_session.c:407-408`), and the capture is evaluated at `now==deadline` (`rpc_session.c:466-471`).
- **Backstop.** 1,666,667 instructions is a session fatal (`rpc_session.c:430`).

**Continuation inside a batch** (`rpc_mem_timing.h:166-187`).
- It returns false on idle or on a test stop.
- With `pending||bus_busy`, it flushes integer mode and calls `arc_rpc_mem_batch_service` (`rpc_mem_timing.c:1091-1177`). That call can **advance `now_ns` and partially service work before returning false** (`rpc_mem_timing.c:1164-1176`). The demand-only shortcut either commits (`rpc_mem_timing.c:1139-1154`) or rolls back its ROM history (`rpc_mem_timing.c:1162`).
- Otherwise it requires `cpu_due_ns < boundary`. `next_ns` is re-read on every call, so an MMIO-armed delay-0 deadline (`fdc.c:529`) stops the batch. [C]

**Integer batch.** It is active only when all of these hold (`rpc_mem_timing.h:103-113`):
- `base_fraction==0`;
- the clock is aligned with the issue state;
- there is headroom of 200·base (the quota constant appears here and at `arm.c:577`);
- `!test_measure`.

`issue_*` is stale until `arc_rpc_mem_batch_flush` (`rpc_mem_timing.c:834-849`). That function calls `next_base`. [C]

**Idle.**
- **Set** by CP15 c15 wait (`cp15.c:280`) or by ARM7500 SUSMODE (`iomd.c:326`). Continuation then returns false (`rpc_mem_timing.h:170`).
- **While idle:**
  - `arc_rpc_mem_next` ignores CPU due (`rpc_mem_timing.c:1070`);
  - pending writes keep draining through `arc_rpc_mem_run`;
  - a video hold does not push CPU due (`rpc_mem_timing.c:1031`);
  - `idle_ns` accrues (`rpc_session.c:449`).
- **Wake.** `arc_rpc_mem_idle(false)` sets due to now + base (`rpc_mem_timing.c:1191-1196`). [C]

**Quota transparency.** That splitting the work at 200 instructions leaves chronology unchanged is an inference [I] (§5 Q-7).

### 1.6 Session fatal and lifecycle

**Fatal.** `fatal()` (`rpc_session.c:132`, M2b working tree; base `:117-134`):
- clears every armed deadline;
- `longjmp`s to the `setjmp` in advance (`rpc_session.c:403`), reset (`rpc_session.c:328`) or start (`rpc_session.c:231`);
- after that, `arc_rpc_fault` refuses every later advance (`rpc_session.c:401`). [C]

**Fatal paths and reachability:** [C]
- "Bad mode" (`arm.c:177`), with `arm.mode` already written at `arm.c:128`;
- the request and packet checks (base `rpc_mem_timing.c:333,344`) are internal
  invariants; R-M2b-S2 permits separately labelled **injected** lockstep evidence;
- time overflow (base `rpc_mem_timing.c:211,217`) remains an unexercised limit.

In the lockstep class, the fatal hook runs after formatting the text and before
fault publication, deadline clearing or observer retirement. Fatal uses phase
5, independent of P1 density; prepared full pages precede its literal/text
hashes, followed by exactly one End. Unprepared memory is represented as absent.
An open attempt keeps its reserved helper identity; an outside-attempt fatal
uses the next k with the last maintained site/sequence, including with P1 off.
The epoch pending latch retires the outer lifecycle flow once, even
when a nested failure ends without another P0. Generation zero is valid.

**Lifecycle.** [C]
- **Start** (merge-base `0966af3`, `rpc_session.c:217-291`): `arc_rpc_stop()` frees all memory (`rpc_session.c:153-178`, `mem.c:78-90`). A failed start also runs `arc_rpc_stop` (`rpc_session.c:231-234`). Allocation is a fresh `malloc` (`mem.c:78-92`), so **a later session can reuse the same host addresses**. Every successful start increments `arc_rpc_generation` (`rpc_session.c:283`).
- **Reset** (`rpc_session.c:321-343`) clears RAM (`mem.c:162-164`), rebinds timing, flushes the TLB (`cp15.c:136`) and resets the ARM. It **does not** bump the generation.
- **Test rate.** The rate override rebinds timing at time zero (`rpc_session.c:633-642`).
- **Engine switching.** Arculator and RPC switch inside one library (`tests/rpc-session-expectations.md:39-41`).

### 1.7 Memory backing, mirrors and writers

**Allocations.** [C]

| Allocation | Size | Physical aperture | Mirroring |
|---|---|---|---|
| `rom` | 8 MiB | — | 2/4 MiB images are memcpy'd into it (`mem.c:69-76`) |
| `ram00` | 32 MiB | `0x10000000–0x13ffffff`, 64 MiB | **repeated twice** by `mem_rammask` (`mem.c:159,255`): `0x10xxxxxx`≡`0x12xxxxxx`, `0x11xxxxxx`≡`0x13xxxxxx` |
| `ram01` | 32 MiB | `0x14000000–0x17ffffff` | same pattern (`mem.c:261`): `0x14`≡`0x16`, `0x15`≡`0x17` |
| `vram` | 2 MiB | `0x02xxxxxx` | repeated through the 16 MiB region (`mem.c:160,205`) |
| `ram1` | — | — | NULL |

`arc_rpc_phys_page` returns pointers into these allocations at 1 KiB granularity (`mem.c:169-181`). [C]

**Writers of guest memory at runtime.** Only CPU stores write guest memory: `mem_phys_write32/8` (`mem.c:432-685`), reached only through `writememfl/fb` (`mem.c:722-739`). [C]
- ROM writes have no functional effect (`mem.c:442-539`).
- RiscPC floppy transfers are FIQ pseudo-DMA: the guest stores bytes it reads from the MMIO ports (`fdc.c:525-535`).
- Video and audio DMA only read memory (`vidc20.c:173,272`; `rpc_audio.c:165`).
- IDE is PIO through CPU accesses; its media backend does not write guest RAM directly (`engine/rpc/rpc_ide.c`, `engine/rpcemu/src/ide.c`).
- Wholesale writers: reset (`mem.c:162-164`) and ROM load (`mem.c:69-76`).
- Video holds are raised only inside the VIDEO event (`vidc20.c:347,359`).

### 1.8 Translation cache and fetch memo

**TLB.** `tlb[256]` is direct-mapped by `(t^t>>10^t>>18)&255`, with `t=VA>>10`. Fetch and data share it (`cp15.c:390-459`). [C]
- **Checked on every hit:** domain, AP, `memmode` (which LDRT/STRT set to 0, `mem.h:120-182`) and the S/R bits.
- **Stale descriptors:** they persist until the entry is evicted or flushed (`tests/rpc_cpu_test.c:189`).
- **Flush triggers:** a change to M/R/S; a TTBR write; a DACR change; ARMv3 c5/c6; SA c8 (`cp15.c:184-186,201-212,224-232,261-273`).

**Fetch memo.** It is a virtual-line-only key (`rpc_mem_timing.h:150–160`); since Q-3 a hit also requires a backed, cache-eligible physical region (`rpc_mem_timing.h:157–159`).
- **Cleared by:**
  - **any** allocation in either cache, **SA D-cache fills included** (`rpc_mem_timing.c:714`);
  - control writes (`rpc_mem_timing.c:560`) and TLB flush (`rpc_mem_timing.c:582-585`);
  - c7 invalidation (`rpc_mem_timing.c:780,803`);
  - SA slow-clock uncached reads (`rpc_mem_timing.c:636`).
- **Not cleared** by a mode change. [C]
- **Defect (§5 Q-3) — resolved (PR #375, `44d007e`).** A memo hit did not check `backed` or the region, whereas the slow path requires both before any tag lookup (`rpc_mem_timing.c:683–686`). A collision eviction followed by a re-walk could change that eligibility without clearing the memo. The gate now mirrors the slow path; `rpc-fetch-memo-rewalk` reproduces the sequence. Note for 6b: in the integrated system `backed` implies a cache region (`mem.c:169–180`), so the region term never decides the outcome there, but it is kept to mirror the slow path exactly (Opus QA, S61).

---

## 2. Exit contract

### 2.1 Phases

M2b format v1 is specified in `riscpc-lockstep-harness.md`. Data P1 has entry
and exit edges at one reserved identity; walk and physical events lie between
them. Entry preserves the previous `arc_rpc_attributes`; exit preserves the
translation's value, including abort paths. LDRT/STRT expose their raw temporary
`memmode`. Fetch reserves its sequence before `getpccache`, emits after opcode
capture, and consumes the sequence without a fetch record on prefetch abort.
Mode P1 precedes `arc_rpc_observe_mode`. P2 precedes continuation; P3 is emitted
only after a positive admission count. Integer P0/P1 project issue and due to
the current issue; P2 projects issue and, without requests, due to issue+base.
Projection is pure arithmetic with status-only overflow and no flush/next_base.

Every exit lands on exactly one of the phases below. The resumer then performs exactly the oracle's next steps, no more and no fewer.

| Phase | Where | Materialised time representation [C] |
|---|---|---|
| **P0(k)** | `arm.c:600`, before k. Reached after continuation returned true, or by a driver issue through `arc_rpc_mem_ready`. The command queue is empty; older work has run to `now`. | `now_ns` = visible issue time of k. `issue_*` = issue of k and `cpu_due_*` = the same values (`rpc_mem_timing.h:182-185`; `rpc_mem_timing.c:1067-1069`). In integer mode, `issue_floor/ticks` are derived with the flush **formulas** (`rpc_mem_timing.c:830-831`) **without** `next_base`. Calling `batch_flush` here would set `cpu_due=issue+base` and so add a base: forbidden as a canonicaliser. |
| **P1(k)** | Mid-k: data-access entry and exit, successful fetch after opcode capture/before condition, SWI/CP15/exception entry, and each changed-mode observation (§6). | As P0, plus k's collected requests in program order and `recording=true`. Data entry retains the previous `arc_rpc_attributes`; exit retains the value left by translation, including abort paths. `opcode` is set after a successful fetch; a prefetch-abort mode report retains the prior value. Temporary LDRT/STRT `memmode` is compared raw. |
| **P2(k)** | Just after `instruction_end(k)` (`arm.c:1963`), before continuation. | No requests: `cpu_due_*` = issue + base. Here a flush is equivalent (`rpc_mem_timing.c:832`). With requests: `pending`, `cursor=issue_ticks`, `cpu_due_*` unresolved and excluded from comparison (§1.2). |
| **P3(k)** | After the continuation step returned false, or the quota/`NULL`-batch return, and the final flush (`arm.c:1966-1978`). | `now_ns` and the bus may have **advanced past P2** through batch service (`rpc_mem_timing.c:1148-1160`). The state is whatever service left; the resumer is the driver at `rpc_session.c:411`. |

### 2.2 Canonical comparison contract (Astra 6)

The executable v1 schema and independent reviewed policy govern field, phase
and mode membership. C tables are generated at build time with drift checks;
owning-TU serializers use member and array capacity probes. The API-only
measurement macro split removes hot test fields and all emitter hooks, while
retaining refusing APIs (R-M2b-S1). Large PCM and presentation contents compare
at output boundaries (M11); observer SWI lists and input-owned storage are
serialized by contents. Observer projections and examples are pinned in
`tests/rpc_lockstep_observer_vectors.json` (AS-2). Compatibility hashes exclude
inventory/citation metadata; inventory has a separate independent policy pin.

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
- **Measurement state with hot test hooks:** `test_measure`, `test_stop`,
  `measure_first`/`last`/`drained`, endpoint mode, previous-completion validity
  and ticks, saved end completion, pending drained capture, and every interval
  field (including separate fetch hits). `ArcRpcMeasureState` compares these
  explicitly; the raw inspector snapshot remains unmasked. Mode-specific
  arming flags differ between modes; their complete interval records compare
  separately. Measurement is absent in the API-only measurement core.
- **RPC observer semantic state (R3c-1, §6):** epoch, ordered event and active
  instruction identities, original VA/backing/mode context, owner generations
  and enabled filters (callback pointers normalised by owner), live watches and
  their deadlines/return eligibility, future answer intent/order, and live copied
  host requests with generation/epoch/media/time. Inactive slots and diagnostic
  trace buffers/counters are excluded. Host handlers are identified by owner.
- **Counts:** `linecyc`, `instruction_work`, `arc_rpc_executed`, `arc_rpc_idle_ns`.

**Excluded fields.**
- Packet lanes at index ≥ `count`. `append()` deliberately leaves stale lanes (`rpc_mem_timing.c:487-492`).
- Ring slots outside the live window.
- `va`/`physical` of invalid tags. `invalidate()` keeps them (`rpc_mem_timing.c:772`).
- Write-only state (§1.4), including `dirtybuffer` (Q-5).
- Diagnostic post-fatal registers (Q-9).
- Host-only live L1 lookup hints/key mirrors, gate and bind-derived geometry masks/shifts, hint-poison PRNG state,
  and `finish_ns` (reserved for S1), exercise and new ranking counters, interval exercise
  snapshots and batch bookkeeping, and cross-check bookkeeping. These remain
  outside `m` and `arc_rpc_mem_fast` and reset at every G-13 timing bind/unbind
  before its early return. Ranking reports carry `ranking_epoch` and a separate
  current-binding `ranking_attempts` denominator. Legacy P0i report fields keep
  their existing cumulative/epoch semantics.
- The authored prefix-comparison diagnostic and its CPU/RAM/VRAM/scheduler
  aggregates. Capture occurs at ordinary interpreter return; CPU reset and
  allocation retirement clear it. It is absent from measurement builds.

**Hit counts.** `m.stats.hits` and `fetch_hits` must be **individually** identical. The draft's "the sum suffices" is withdrawn. The split stays until Fable accepts an explicit projection.

### 2.3 Exit kinds

| # | Exit | Lands at | Must be materialised | Must NOT be repeated on resume |
|---|---|---|---|---|
| E1 | Block end, continuing | P0(k+1), when the block itself performed the exact continuation | §2.2 at P0 | Nothing of k |
| E2 | Guard miss before k | P0(k) | §2.2; **no part of k**: no `instructions++`, no TLB fill or walk, no memo or tag effect, no base. Guards use non-mutating probes only (`lookup()` is pure in canonical state, `rpc_mem_timing.c:393-476`) | — (k runs whole) |
| E2′ | Guard miss after k's fetch | **Disabled** (Q-1) | — | — |
| E3 | Helper call inside k | P1(k) | Before the call: P1 contents and `now_ns` = issue of k (read via `rpc_timing.c:5`). After it, re-read `arm.event`, registers, mode and `r15_mask`, the TLB and memo generations, `next_ns`, idle, `base_*`, `pending` | Earlier functional effects of k; MMIO reads that pop FIFOs (`fdc.c:525-529`) |
| E4 | Side exit after k (mode or mask change, idle, MMIO, CP15, c7, invalidation) | P2(k); the resumer runs continuation **once** | §2.2 at P2 | Fetch, transfers and base of k |
| E5 | Deadline, limit, quota, idle or test stop | P3(k), with `linecyc` returned | §2.2 at P3, including any service progress | Service already performed; k's base |
| E6 | IRQ/FIQ at k's event check | P2(k) (PC = vector; `arc_rpc_mem_branch`, `arm.c:508`) | §2.2 | k's body; the exception entry |
| E7a | Data abort | P2(k) | Partial functional and charged transfers; FAR/FSR; the model's base rule; no FIQ/IRQ at this boundary | Completed transfers and their packets |
| E7b | Prefetch abort | P2(k) via `instruction_done` | Base resolved; fetch charged only for absent backing; no event check | The exception entry; the base |
| E8 | Session fatal | `longjmp` | Translator bookkeeping must survive the unwind and be released by `arc_rpc_stop` (G-13) | No retry after partial effects |
| E9 | Store into translated code | P2(k), after k completes atomically (remaining STM transfers, abort, event check, `instruction_end`) | Dependencies invalidated at the functional write | k's stores and base; the next fetch rereads memory |
| E10 | SWI/Undefined (mid-body `exception()`) | P2(k) after the event check. FIQ can still be taken there, since only I is set (`arm.c:512-517`) | §2.2 | The exception entry |
| E11 | Cache maintenance only (c7) | P2(k), as E4. c7 may append DRAIN (`rpc_mem_timing.c:806`) or castouts (`rpc_mem_timing.c:795`) and advances the G-5 generation | §2.2 | The maintenance effects |

---

## 3. Guard invariants

**G-1 Budget and driver entry.**
- **Continuation.** Within a batch, instruction i may issue only if ⌈tᵢ⌉ < min(`next_ns`, `limit`), re-evaluated after every helper (`rpc_mem_timing.h:178-181`).
- **Base step.** tᵢ = tᵢ₋₁ + b holds only when i−1 collected no requests (§1.2).
- **Pending work.** With `pending||bus_busy`, every boundary goes through batch service. A fold across such boundaries requires G-8.
- **Driver entries** must also be reproduced exactly: the `NULL`-batch instruction at the limit, quota 1 only when ready at field completion, the capture-deadline clip, and the 1,666,667 backstop (§1.5).

**G-2 Block key.** The key is all of:
- the PC VA (`arm.h:67`);
- the full 5-bit `arm.mode`. Privilege selects the R15 masks (`arm_common.h:311-320,361-381`) and `memmode`;
- `prog32` (`arm.c:529-535`);
- the session model (`arm.c:291-301`);
- `arc_rpc_generation` and the reset epoch (G-13).

Any `updatemode` ends the block (E4).

**G-3 Interrupt state.**
- **Per-instruction check.** After **every** instruction that may change eligibility, the event check must run exactly as `arm.c:1936-1953`. That includes the 26-bit `reg[16]` refresh whenever `event≠0`, even if masked. The instructions are:
  - any MMIO, including a translation walk reading I/O;
  - any write to raw R15 outside `r15_mask`, and the final `+4` carry;
  - MSR, the P-forms, S-forms with Rd=15, LDM^ with PC;
  - SWI/Undefined/CP15.
- **Entry.** If an enabled interrupt is pending at P0, the block may run at most one instruction attempt. It then exits through the oracle's own check; it does not assume that the exception is taken (§1.3).
- **When pending state is constant.** In a block with no MMIO, no walk that can reach I/O, and none of the instructions above, interrupt-pending state is constant [I].

**G-4 Fetch translation (shared TLB).**
- **Skipping per-fetch translation** requires all of these to hold since the last real translation of that 1 KiB tag:
  - no flush;
  - no data access installed into any fetch slot in use. Installs include denied accesses (`cp15.c:433` precedes `cp15.c:436-445`); translation faults install nothing;
  - `memmode`, DACR, S/R and `mmu` unchanged.
- **After an eviction,** the real re-walk must be performed at the fetch the oracle would perform it, never speculatively (Q-10).
- **Attributes.** `arc_rpc_attributes` must equal the oracle's value whenever a timing call reads it (`cp15.c:95,449,483`).
- **Permission hoisting** needs a Fable ruling before Phase 1 (constraint 3).

**G-5 Instruction-cache residency.**
- **What a skipped fetch-timing step must reproduce:** the memo state, `stats.hits` and `fetch_hits` **separately** (§2.2), unchanged victim cursors, `instructions`, and the burst-ROM sequential state (`rpc_mem_timing.h:150-160`; `rpc_mem_timing.c:639-660,887-897`).
- **The generation advances on every memo clear:**
  - any allocation in **either** cache, SA D fills included (`rpc_mem_timing.c:714`);
  - c7 (`rpc_mem_timing.c:785-807`), control writes and TLB flush (`rpc_mem_timing.c:560,584`);
  - SA uncached-read clock handling (`rpc_mem_timing.c:636`);
  - bind or reset.
- **Tie to G-4.** The proof is tied to G-4, because eligibility depends on the current region and backing (`rpc_mem_timing.c:686`).
- **Retained tags.** Retained tags still hit with C/I clear (`rpc_mem_timing.c:399-401,644`).

**G-6 Unified caches (ARM610/710/7500).**
- **Shared array.** A cacheable data load miss allocates in the same array as code. It advances that set's cursor and can evict an executing line (`rpc_mem_timing.c:702-736`).
- **Stores.** Store hits emit a write and leave tags unchanged. Store misses do not allocate.
- **Data accesses therefore** use the real lookup, plus revalidation of the fetch proof or an exit (Q-11). Any allocation advances the fetch generation.

**G-7 SA-110 split caches and any data-side filter.**
- **Guards a filter must check:**
  - permission before the hit (`mem.c:724-727`), keyed by effective privilege (including LDRT/STRT), direction, DACR, S/R and `mmu`;
  - **tag validity and replacement state** (`lookup`, victim cursors);
  - **the current backing and region** (`eligible = backed && cache_region(r)`, `rpc_mem_timing.c:686`);
  - the empty-D-tag fast path, taken when C/W are clear (`rpc_mem_timing.c:667-674`).
- **Load hit:** zero bus, `stats.hits++`.
- **Store hit:** dirty half `(va>>4)&1` with no packet (`rpc_mem_timing.c:700`). The functional store still happens; ROM stores are functionally ignored but cast out under G7 (`rpc_mem_timing.c:507-515,712`).
- **Load miss:** allocate, cast out dirty victim halves, fill, and clear the memo.
- **Store miss:** no allocation; buffered only with M, W and B set in a memory region (`rpc_mem_timing.c:740-741`).
- **SWP/SWPB stay on the exact path.** Their `mapped_swap`/`cached_swap` locking and write-allocate cases (`rpc_mem_timing.c:682-692,702,734`) are outside any filter.
- **Clock switch.** An uncached SA read can switch the base clock (`rpc_mem_timing.c:631-638`).
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
- **Equal-ns order:** events → `arc_rpc_mem_run` (completion, release, grant: video, then buffer FIFO, then CPU demand last) → CPU issue (`rpc_session.c:417-429`; `rpc_mem_timing.c:999-1048`).
- **What equivalence must show:** identical word start, finish and release ticks, `buffer_wait_ticks`, hold placement (`rpc_mem_timing.c:1027-1035`) and `restart`. Bulk subtraction of elapsed time is not a proof.

**G-9 Self-modifying code.**
- **Key.** Dependencies are keyed by (allocation, offset) from the `code` pointer (`cp15.c:433,479`). For example `0x10000100` and `0x12000100` are the same `ram00` bytes. `0x11000100` and `0x13000100` alias each other at offset `0x1000100`, which is a **different** offset from `0x10000100`.
- **Hook.** Invalidate at `mem_phys_write32/8` (the only runtime writers, §1.7), after the permission check and for RAM/VRAM only.
- **Coverage.** The hook covers STR, STRB, STRH (two bytes), STM and STM^ (per word), SWP/SWPB, STRT/STRBT, and stores made by oracle routines.
- **After the write.** k completes, then the block exits (E9).

**G-10 Guest cache maintenance is timing-only.**
- **No functional effect.** c7 has no functional effect on fetched bytes (`cp15.c:254-259`; `rpc_session.c:136`).
- **Timing effect.** It advances the G-5 generation and exits as E11.
- **Not an SMC trigger.** It must never be the trigger for G-9.

**G-11 Time representation.**
- **Phase-specific values.** Each phase in §2.1 has its own time values. Reach them by the oracle's own formulas, never by calling `batch_flush`/`next_base` where the oracle would not.
- **Base changes.** A base change applies from the next `next_base` (`rpc_mem_timing.c:557-569`).
- **Overflow.** Overflow fatals must fire at the same instruction (`rpc_mem_timing.c:302-312`; `rpc_mem_timing.h:105,129`).

**G-12 Counters, measurement and observation barriers (D-195).**
- Canonical counters and separate timing/fetch hit counts match at every exit.
  Exercise, ranking and cross-check bookkeeping are excluded as above.
- The retained intrusive mode forces slow begin/end and disables integral
  batching. Its forced-slow request tests remain valid. Endpoint mode keeps
  fast begin/end and both continuation shapes live, bounding armed admission
  at `first−1` and `last`, including NULL-batch entries
  (`arm.c:2062-2063`).
- Start is completion of `first−1`, not first issue minus base. Non-drained
  end is last completion; drained end also waits for final command/buffer
  retirement. Capture never flushes, translates or services the bus.
  Admission, completion, ordinary return and final-retirement captures are at
  `rpc_mem_timing.c:240`,
  `rpc_mem_timing.c:266`,
  `rpc_mem_timing.c:280` and
  `rpc_mem_timing.c:1023`.
- The fetch memo and demand-shortcut semantics are unchanged. Calibrations
  compare both modes' complete records; directed tests cover holds, wake,
  pending writes, quotas, fractions, shortcut commit/rollback, NULL entries and
  driver issue exactly at the limit. Exercise totals prove the model-specific
  integral/rational and S1 paths ran.
- Event traces, interval captures, snapshots and R3c-1 observer transitions
  remain observation barriers, never permission to materialise through a new
  flush or early service of the current instruction. Section 6's P0–P3 phases
  remain unchanged. Equations, counter definitions and capabilities are frozen
  in `riscpc-translator.md`, "Slice 1a-0 measurement design".

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
| G-1 | Issue ticks; delay-0 MMIO deadlines; the limit `NULL`-batch instruction; quota 1 only-if-ready; the capture clip; quotas 1, 7 and 200 agree in chronology and live state (Q-7) | Partial: `rpc-plan-devices.trace`, `rpc_session_test.c` (equal-timestamp timers, `tests/rpc-session-expectations.md:14-21`), flat/burst agreement (`rpc-plan-expectations.md:63-65`), `rpc_hot_test.py`. `rpc_endpoint_cpu_test.c` checks endpoint prefixes and the NULL path; `rpc_timing_test.c` checks driver issue at the exact limit |
| G-2 | Mode and privilege changes exit and re-key; raw R15 arithmetic, including the 26-bit carry (Q-6) | `rpc-cpu.trace` SVC26/SVC32 (`tests/rpc-cpu-expectations.md:10`), `rpc-plan-matrix-*`. **No test** for the carry or for user-mode R15 masks in hot code |
| G-3 | The event check after every eligibility-changing instruction; eligibility narrowed by prefetch abort, data abort, body masking and walk MMIO; the 26-bit refresh while masked | `rpc-plan-devices` ("forced FIQ taken exactly once"; timer IRQ 0x60). **No test** pins the ordinal of an event-raised IRQ or the masked-refresh case |
| G-4 | A data access, including a denied one, evicting the fetch slot causes a real re-walk at the next fetch; no speculative walks | `rpc_cpu_test.c:257-297` (data-data m1, fetch privilege m4, c6 m11), `rpc_cpu_test.c:189` (stale persists). **No test** for data evicting the fetch entry |
| G-5 | Memo, cursors, `stats.hits` and `fetch_hits` individually equal; burst-ROM state; the SA D-fill memo clear | Calibrations (4,677, `tests/rpc-timing-expectations.md:79`) and `rpc_qa_mutations.py`. Intrusive and endpoint modes compare complete interval records, including separate fetch hits. Endpoint counters report integral/rational/S1 coverage, and CPU/plan prefix tests compare through the last functional instruction (G-12) |
| G-6 | A unified data fill evicting the executing line | `cache-geometry` probes (`rpc-timing-expectations.md:14-46`; code in sets 1/2). `rpc_l1_test.c` directed G-6 sequence fills the executing set with data, evicts the code line, clears the memo and verifies the cyclic cursor |
| G-7 | Filtered hits equal real hits: validity, backing and region, dirty halves, G7 castout, LDRT/STRT; SWP excluded | `rom-castout-*`, SA geometry probes, `rpc_cpu_test` m12 |
| G-8 | Lazy and eager drain give identical per-word ticks; a timing-register write mid-drain; k's own requests costed after k's effects | `rpc_mem_timing_unit.c` (request-level holds and boundaries); buffer-boundary calibrations (`rpc-timing-expectations.md:81-91`); `rpc-d2-*`/`rpc-video-*` hashes. **No test** writes ROMCR/CLKCTL while draining, or in the instruction whose own fetch it reprices |
| G-9 | Invalidation through aliases (`0x10↔0x12`, `0x11↔0x13`, VRAM) and through every store form; a store to PC+4; STM partial abort | `rpc_plan_matrix_rom.py:53-60` (SMC through `MOV pc`); `rpc_cpu_test.c:112` (functional mirror). **No test** for aliased, same-block or PC+4 SMC |
| G-10/E11 | c7 changes timing only; exact exit | None directly |
| G-11 | Phase-correct rational state across integer and non-integer modes and SA clock switches | SA clock-disable calibrations (`rpc-timing-expectations.md:92-101`) |
| G-12 | Counters; measurement endpoints; barrier materialisation | All 4,677 calibrations compare complete intrusive/endpoint intervals (`rpc_timing_test.c:46-117`); `rpc_endpoint_test.c` checks measurement/exclusion/reset fields and the endpoint matrix |
| G-13 | Reset, reload, failed start and engine switch with reused addresses leave no stale block | Allocation-reuse and switching tests (`rpc-session-expectations.md:35-41`) cover the oracle only. **No translator test** |
| G-14 | Writes pending across sleep and wake; wake while masked | Request-level idle drain (`rpc-timing-expectations.md:103-110`); session SA wait and SUSMODE (`rpc-session-expectations.md:6-10`) |

**Why per-instruction differential testing is required.** Goldens observe state only at frontend boundaries and through guest-recorded RAM (`rpc_session.c:667-680`). A divergence that later self-corrects can therefore pass them all.

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
- **D-195 order:** 1a-0, L1/S1 in measured order, then A1/F1. Timing-service work joins fetch-path work; every per-fetch permission check remains.
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


### L1 lookup contract (D-200, AS-L1-7)

At most one valid tag per line per set exists. Allocation follows a miss;
locked SWP cannot allocate a duplicate. Tag identity/validity writers update
the exact mirror (UINT32_MAX for invalid slots). A hint is a way index within
the looked-up cache's set, accepted only after bounds, validity and line checks.
SA I and D have separate side state. This contract is stable for B-1 and X.
No hit changes replacement order. Lookup stays pure in canonical state.
Side state resets before bind's early return at every G-13 point; any future
restore of m, including M2b or savestate restore, must rebuild or reset it.
When restored tags stay valid, rebuild their exact mirror before enabling
mirror lookup; hints may be reset. Clearing mirror keys alone is sufficient
only when canonical tags also reset (or lookup remains linear until rebuild).

Run B derives each cache's set/way masks and shifts from its asserted power-of-two
geometry at bind, outside canonical state. Lookup, fill, hint indexing and cyclic
way advance use these masks/shifts; hits and fills store a hint only when its
byte changes. The independently retained linear scan is unchanged.

Run C adds SA-only way0-hint and way0-mirror measurement candidates (gates 3/4).
They compare canonical way 0 first, before any hint access; a way-0 hit leaves
hints and lookup-path counters untouched. After a way-0 miss, validated hints
and a first-match remainder from way 1 preserve the retained linear result.
The ordinary probe/way counters still describe that retained scan. The production
SA gate is way0-hint after Fable's selection run 3 (Run D); no canonical layout,
snapshot, mirror/sentinel, reset or counter ABI changes.
