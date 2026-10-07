# RiscPC memory-system timing contract — R2a run A (D-139)

Status: **proposal for Fable acceptance and Astra challenge; not authority for
run B**. Grounded at `7bd1f27`, branch `slice/r2a-timing-cmos`. This document
defines one deterministic timing model for the four private D-137 models. It
does not qualify their historical instruction throughput. Rates remain ceilings:
ARM610 20, ARM710 35, SA-110 100, ARM7500 35 million instructions/s
(`docs/riscpc.md` §4.4; D-131/D-137/D-139). There is no firmware-specific path,
capture-only rate, or change to keyboard BAT.

The distinction between **documented hardware**, **derived costs**, and
**declared approximations** below is part of the contract. Accepting an interim
choice accepts its stated fidelity limit; missing sources must not turn into
unlabelled hardware claims. All tests use authored instructions and data.

## 1. Source register and scope of fidelity

The following abbreviations cite the private local text/PDF pairs in
`.evidence/`. Page numbers are the documents' printed pages, not PDF indices.
Do not commit those copies. These are paraphrases and calculations, not extracts
of the documents.

| ID | Primary document | Relevant locations |
|---|---|---|
| I | IOMD Functional Specification, 0297,030/FS issue 4, June 1994 | §4, sheets 7–9: clocks, RAM and ROM; sheet 17 table 4.1 and sheet 18: I/O; §5 sheets 23–24: map; §6 sheets 25–26: reset registers; §7 sheets 27–29: arbitration/DMA |
| T | ARM710a macrocell, ARM DDI 0033D | §§5–7: CP15, cache, buffer; §9: descriptors; §10.1 pp. 10-2–10-3: bus concurrency; appendix A pp. A-2–A-3: ARM610 differences |
| A | ARM7500, ARM DDI 0050C, October 1995 | §§4, 6, 7: CP15/cache/MMU; §13.3.16: CLKCTL; §13.3.32: ROMCR; §§13.3.39–40: I/O controls; §§14.1–2: memory; §§15.4, 15.6: I/O; §16.1: clocks; §§17.3–4: bandwidth/arbitration |
| S | Digital SA-110 Microprocessor Technical Reference Manual, preliminary | §5.2 tables 5–7: CP15; §§6.1–3: caches/buffer; §7: MMU; §8.2 p. 27: DCLK; §§9.1–3, 9.8–9.9 pp. 30–40: bus |
| SI | Intel SA-110 datasheet, 278230-001 | pp. 1, 3–5: cache sizes, geometry, write policy; corroborates S, does not specify Risc PC card glue |
| R | Repository evidence at the above base | `rpc_scheduler.h/.c`, `rpc_session.c`, `arm.c`, `arm_common.c`, `cp15.c`, `mem.c`, `iomd.c`, `vidc20.c`; R1 expectation documents/generators under `tests/` |

Official online corroboration: [ARM710a DDI0033D](https://documentation-service.arm.com/static/5ed11a9aca06a95ce53f8fb6)
and [ARM7500 DDI0050C](https://documentation-service.arm.com/static/5ed11babca06a95ce53f8fee).
The supplied editions remain the detailed numeric sources. Searches did not
establish a primary Risc PC StrongARM card timing specification or a complete
ARM610 manual; secondary accounts are not timing authority here.

R2 models external bus occupancy, timing cache residency, dirty writeback traffic,
write-buffer backpressure and clock-switch state. It retains the interpreter's
functional memory and architectural exception semantics. It is **not** an
instruction pipeline, cache-data/coherence, TLB-cycle, pin-level arbitration or
DRAM refresh simulator. Specifically:

- Every issued instruction has one base cost, including a failed condition,
  branch, multiply, exception-causing instruction or CP15 operation. No additional
  pipeline/refill/internal-cycle constants are introduced. T §4/A §5/S §4 have
  differing real instruction cycle counts; D-137's ceiling is the accepted
  abstraction, not a claim that those counts are one.
- Data writes still update the backing memory at instruction issue. Timing tags
  contain no data. DMA and aliases therefore see stores earlier than real
  write-back/write-buffer hardware can. Instruction fetches see current backing
  bytes, not stale cached instructions. This preserves existing self-modifying
  code behavior; it cannot validate software relying on missing cache maintenance.
- Translation permissions remain R1's contract. Retain descriptor B/C (and ARM610
  U if supplied) with each translation, but **do not charge translation-table
  walks** in R2. The existing 256-entry hashed permission cache is not any of the
  hardware TLBs. Charging its misses would make timing depend on a host shortcut.
  A hardware timing TLB is a separately reviewed extension, not hidden in run B.

AC-1 must name these exclusions and the interim choices in §9. Cache presence,
fills, buffer capacity and CPU bus occupancy are still tested, not bypassed.

## 2. Clocks, physical map and bus costs

### 2.1 Clock profiles

For Risc PC, I §4 derives the memory state clock **32 MHz** from a **64 MHz**
reference, peripheral clock **16 MHz**, and VRAM serial clock **64/3 MHz**.
Let `q = 31.25 ns` be a memory-state tick. An S access is `2q = 62.5 ns`;
an N access is `5q = 156.25 ns` (I sheet 8). These apply to CPU DRAM and VRAM
reads and writes. CPU MCLK's nominal S-cycle frequency is **16 MHz**, with
stretched N/ROM cycles. The 32 MHz state clock is not a 32 MHz CPU bus.
The independent VIDC pixel reference in R1 is 24 MHz; it is not MCLK.

For A7000, A §§14.2, 16.1 define the same `5q` N and `2q` S counts using
the **effective MEMRFCK**, with prescalers controlled by CLKCTL at `0x0320003c`.
The proposed external MEMCLK is **32 MHz** (board source gap G2).
Bit 1 set gives `q = 31.25 ns`; bit 1 clear gives `q = 62.5 ns`.
Power-on CLKCTL is zero (A §13.3.16/§16.1.4), so the latter applies until
software changes it. External I_OCLK is **32 MHz**, specified by A §16.1;
bit 0 set gives 16 MHz PC-style I/O and 8 MHz simple I/O; bit 0 clear halves
both frequencies. Costs scale with these clocks. Bit 2's physical CPU divider
does not alter the provisional 35 MIPS base abstraction; qualifying FCLK-based
instruction rates belongs to R4. A model reset restores this power-on timing
state, consistent with R1's reset-as-fresh-session contract; this is not a
physical push-button-reset claim.

No physical ARM610/710 oscillator is inferred from 20/35 MIPS. For SA-110,
retain a nominal 16 MHz MCLK profile from I's bus timing, use standard bus mode
(`CONFIG=0`), and split CPU bursts at aligned four-word boundaries as required
by the work order. Card wiring and extra synchronization waits are G3.
No invented PLL frequency or card wait state is added.

### 2.2 Physical timing classification

Classify the **translated physical address**, masked to the existing 29-bit
aperture; tag caches by virtual address (§4). Decode timing from the documented
map, not from which host allocation happens to be reached. Existing mirrors,
absent-device return values, ignored writes and permission checks are preserved.

| Physical interval, inclusive | Timing class | Population in R1 |
|---|---|---|
| `00000000–00ffffff` | ROM bank 0, ROMCR0 | 2/4/8 MiB image mirrored through existing 8 MiB backing/aperture rules |
| `01000000–01ffffff` | ROM bank 1, ROMCR1 | Absent; no new ROM mapping |
| `02000000–02ffffff` | VRAM | Risc PC 2 MiB mirrored; A7000 absent, never a second DRAM pool |
| `03000000–03ffffff` | I/O/PROG subclasses below | Existing decoder/absent responses |
| `04000000–07ffffff` | Reserved | Existing response; interim N-memory timing, never cache allocate |
| `08000000–0fffffff` | Extended podule I/O | Absent, but still pays selected I/O cycle |
| `10000000–13ffffff` | DRAM SIMM 0 bank 0 | Existing 32 MiB mirrored bank |
| `14000000–17ffffff` | DRAM SIMM 0 bank 1 | Existing 32 MiB mirrored bank |
| `18000000–1bffffff`, `1c000000–1fffffff` | DRAM SIMM 1 banks 0, 1 | Absent in R1; still classed as DRAM for attempted data cycles |

Source: I §5, A §§13.1, 14.2, 15.2; population/mirroring: R `mem.c`.
Absent instruction backing still prefetch-aborts; it must not become executable.
An attempted external access to an absent region pays its class cost; a
translation/permission abort does not issue that target bus access (§5).

### 2.3 RAM transactions

Define `B(n) = q(5 + 2(n-1))` for an uninterrupted burst of `n` words.
Thus, at nominal memory clock, `B(1)=156.25`, `B(4)=343.75`,
`B(8)=593.75`, `B(16)=1093.75 ns`. These are exact derivations from I §4/A
§14.2, **not** the rounded 350 ns DMA figure in I §7.

The first word of each CPU instruction's external data transaction, each cache
line fill and each buffer packet is N. Successive words of that transaction are
S only while addresses advance by four, direction and timing region agree,
and no restart condition applies. Single LDR/STR instructions always begin N,
even in a streaming loop. No open-page state survives between instructions or
buffer packets: a declared conservative approximation, not a hardware rule.

Restart at a RAM bank change, aligned 1 KiB boundary (G4), or after a video
hold actually interrupts service. Risc PC ARM610/710 can arbitrate after eight
words (I §7); A7000 after four (A §17.4). **Arbitration alone does not turn
S into N** if no hold occurs. Thus an A7000 eight-word transfer still costs
`B(8)` with no contention. SA-110 always splits at four-word boundaries (G3):
`Bsa(n)` is the sum of `B(k)` for the aligned subbursts, `k ≤ 4`.
An aligned SA eight-word burst costs `2B(4)=687.5 ns`.

On SA-110, only cache-line fills generate read bursts; uncached LDM is `n`
separate N reads (S §§6.2.3.2, 9.9). Unbuffered STM is likewise `n` separate
N writes in this profile; standard-mode STM bursts require effective B=1 and
the buffer enabled (S §§6.3, 9.9). Buffered STM packs consecutive transfers
within aligned 16-byte entries. This difference must not be generalized to the
older ARM models, whose uncached LDM/STM generate sequential cycles.

A byte transfer consumes one addressed word cycle, not one quarter of a cycle.
Keep current alignment/rotation behavior. SA LDRH/LDRSH read one external word;
the current STRH emits two byte stores and R2 charges those two N cycles, or two
buffer entries, rather than pretending that implementation emits one halfword
bus request. That is an explicit R1 execution-path limit to revisit separately.
SWP/SWPB read and write externally with the buffer drained; no DMA grant between
the locked pair (T §6.4, A §6.2, S §9.8).

### 2.4 ROM timing and fetch

For each ROM bank, `speed = ROMCR & 7`. Codes 0–5 mean `(7-speed)q` per
initial chip access. Reserved codes 6/7 clamp to code 5 for bounded execution
and are outside calibrated hardware programming (no new session fault).
Bits 4:3 give burst off / `4q` / `3q` / `2q`.
With burst off, every word pays initial time. With burst on, the first access
and every aligned four-access boundary pay initial time, others burst time.
Instruction-fetch sequentiality continues only across physically contiguous
fetches with no intervening external data transaction or hold. A taken branch,
exception or other PC discontinuity starts N. Cache hits issue no ROM cycle.

IOMD ROMCR resets to **0**, initial `7q=218.75 ns`, burst disabled (I §4
sheet 9/§6). Bits 7:5 are unused: in particular **0x40 does not select
16-bit ROM on IOMD**. R `iomd_reset()` currently sets 0x40 for both devices;
run B must use the correct model reset/readback as part of timing integration.

ARM7500 ROMCR resets to **0x40**, bit 6 selects 16-bit ROM, bit 5 clear
doubles chip-access time (A §13.3.32/§14.1). Each 32-bit CPU word needs two
halfword chip accesses. In burst mode the second halfword may be S; the burst
boundary is four **chip accesses**, not four CPU words. At effective 32 MHz,
reset word cost is `2 × 2 × 7q = 875 ns`; with the proposed power-on
divide-by-two memory clock it is **1750 ns**. With ROMCR `0x20`, normal
32-bit slow non-burst words cost 218.75 ns. With `0x3d` (32-bit, normal
speed, initial 2q, burst 2q), all words cost 62.5 ns. A7500 byte accesses
need one chip access; R2 32-bit fetches need two when bit 6 is set.

The ROM is cacheable under the same architectural rules as other backing
memory. Do not exempt ROM fetches from bus timing, and do not grant ROM an
always-hot instruction cache. SA I-cache can allocate with MMU off; unified
caches require MMU on. A successful instruction fetch is consulted even when
its condition will fail. Do not charge the host `pccache` lookup as a memory
transaction or confuse it with a guest cache hit.

### 2.5 I/O cycles

Every I/O transaction is unbuffered and non-allocating in this timing profile,
even if software incorrectly maps it B/C=1 (G5). Each transfer in LDM/STM pays
a full I/O cycle, with no sequential discount. Width follows existing device
dispatch: an IDE word read is one 16-bit transaction; a podule word operation
is one existing 16-bit or extended 32-bit transaction, not invented splitting.

| Class | Nominal cost, N and S alike | Source/qualification |
|---|---:|---|
| IOMD internal PROG, `03200000–0320ffff` and existing register aliases | `4q = 125 ns` | A §17.4: four register cycles take 16 ticks; applying this to IOMD is G5 |
| VIDC programming, existing `03400000–037fffff` decoder | `4q = 125 ns` | Same provisional PROG cycle; video hold still applies; no new video arbitration model |
| SuperIO `03010000–03011fff`; combo pseudo-DMA; SCSI/CODEC spaces | A/B/C/D = **562.5/437.5/312.5/250 ns** | I §4 table 4.1: 9/7/5/4 ticks at 16 MHz; full cycle including recovery |
| Simple old podule space, including existing bank-4/bank-7 decode | address bits 20:19: slow/medium/fast/sync = **875/750/625/625 ns** minimum | A §15.4 table 15-2: 7/6/5/5 ticks at 8 MHz; I §4 retains legacy interface; sync wait below |
| Extended podule `08000000–0fffffff` | ECTCR bit for slot: 0 = A, 1 = C | I §4 fig. 4.11/A §§13.3.40, 15.6 |

IOTCR combo timing is bits 3:2; SCSI/PC area is bits 1:0; on IOMD CODEC
uses bits 5:4 (I fig. 4.11). IOTCR/ECTCR start at zero, hence A: A §15.6
specifies this reset; I §6 marks their IOMD reset state undefined. Zero on
IOMD is R1's deterministic initialization retained as an explicit G5 choice,
not a hardware reset claim. Undefined/unimplemented I/O holes use slow A.
The existing permissive register aliases retain their functional behavior and
pay PROG timing; this does not authorize correcting the map in run B.

For PC-style I/O, round cycle start forward to the next 16 MHz edge, using
the effective A7000 clock when divided. The phase penalty is [0, one tick),
and the full table cycle starts there. I §4 sheet 18 mentions half-tick slip;
using a full edge-alignment bound is G5, not an extra fixed penalty. Simple I/O
starts on the next 8 MHz edge; sync-mode selection also aligns to the next
2 MHz edge, so the extra penalty is at most three 8 MHz ticks (A §15.4).
All clock phases originate at scheduler time zero. READY is permanently ready
for the attached/stub devices; variable module-I/O handshakes and real podule
READY waits are not implemented and not claimed.

### 2.6 Sharing the existing video hold

Retain R1's line-granular `arc_rpc_video_bus`: each DRAM quadword holds
**350 ns**; each two-bank VRAM half-SAM copy holds **156 ns**; cursor requests
are DRAM quadwords. These accepted rounded constants come from I §7 and
`tests/rpc-video-expectations.md`; do not silently replace them with CPU costs
343.75/156.25 ns. A7000 remains DRAM-only (D-138).

There is one serialized memory service timeline. A video request adds its
duration once at its sampling event; it delays remaining CPU bus service and
buffer drain. It is not also added as a second per-transfer surcharge. Preserve
R1's global CPU blockage during that hold, including cache-hot instructions;
buffer drain freezes too. This deliberately overstates contention for cached
execution and I/O which real hardware can overlap (I §7/A §17.4). AC-1 tests
the accepted line-granular model, not physical arbitration latency.

If a hold arrives during CPU service, complete the current uninterrupted grant
(at most eight RAM words for ARM610/710, four for A7000/SA, four ROM chip
accesses, one non-burst ROM/I/O transaction), then service the queued hold.
The line sampling/pixel production itself remains at its original event time.
If no CPU bus grant is active, the hold starts immediately; holds accumulate
in event order. A suspended transaction resumes with N at its next word. The
old global `bus_until` cannot independently stall a second time for the same
grant/hold. This integration changes CPU arbitration only, not scanout timing.

## 3. Write buffers and ordering

| Model | Capacity and packet width | Eligibility / packing |
|---|---|---|
| ARM610 | 8 data words, 2 independent start-address slots | T appendix A confirms both; each sequential STM run uses one address slot and up to 8 data slots; other stores use one of each |
| ARM710 | 8 data words, 4 start-address slots | T §7; same packing |
| ARM7500 | 8 data words, 4 start-address slots | A §6.4; same packing |
| SA-110 | 8 entries, each 1–16 bytes at its own address | S §6.3; standard mode merges only STM/castout (S §9.1), within aligned 16 bytes; single STR/STRB gets its own entry even when adjacent |

For the unified-cache models, buffering requires CP15 control M(bit 0)=1,
W(bit 3)=1 and descriptor B(bit 2)=1. Descriptor C(bit 3) controls read
allocation, not write buffering. Cache-hit stores are still write-through.
For SA, cache-hit stores mark a dirty half-line without external traffic,
independent of descriptor B; a miss does not allocate. A missed store buffers
only with M/W/B set. Dirty castouts use the SA buffer even when W is clear
(S §§6.2.2, 6.3.4). No buffered I/O or ROM write is introduced.

Each buffer packet keeps physical start, byte/word transfer mask, timing class,
remaining words and whether it is an STM/castout. No stored data is needed.
Split before its capacity/alignment/bank boundary. Never merge independent
instructions on the unified-cache models in this profile (G6); an STM longer
than eight words streams successive packets rather than reserving 16 slots
at once. Reserve both address and data capacity **before** accepting a packet.
If either is unavailable, the CPU waits while bus service drains the oldest
packet. Release that packet's slots at its last transfer, not at enqueue and
not at its first word. This is a bounded conservative slot-release choice G6.

Drain FIFO from the earliest available bus time with the region timings in §2.
Drain proceeds during subsequent base instruction time, cache hits and idle;
it is suspended by video holds. There is no independent fixed "buffer delay"
and no enqueue cost beyond the instruction base. All buffered writes must
eventually consume the bus even if execution switches to an ALU-only loop.

Cache-hit reads need not drain. A read miss/uncached data read drains pending
packets before its external read in this profile; full-drain ordering is the
conservative G6 rule. An instruction fetch does not perform a buffer-address
lookup or forwarding (S §6.1.2). Its external transaction waits behind any
already active packet, then queued older packets in FIFO order, sharing the
same bus. Unbufferable writes and locked operations drain first (T §7.2.2,
A §6.4.2, S §6.3.2.3). A drain operation waits for empty; W clear stops new
eligible enqueueing, but outstanding writes complete normally. Reset discards
timing packets, matching the existing functional reset, and clears all timing
state; unload/failed start cannot leave packets in the next session.

## 4. Timing-only caches

### 4.1 Geometry, lookup and replacement

| Model | Tags | Set index | Replacement / policy |
|---|---|---|---|
| ARM610 | unified 4 KiB; **provisional** 256 × 16-byte lines, 64 ways | VA bits 5:4, 4 sets | Deterministic cyclic victim, write-through, read allocate; geometry partly from T appendix A; missing direct manual G1 |
| ARM710 | unified 8 KiB; 512 × 16-byte lines, 4 ways | VA bits 10:4, 128 sets | T §6 explicitly says random placement; cyclic victim is G1 approximation; write-through, read allocate |
| ARM7500 | unified 4 KiB; 256 × 16-byte lines, 4 ways | VA bits 9:4, 64 sets | A §6: random placement, approximated by cyclic victim; write-through, read allocate |
| SA-110 | separate 16 KiB I and 16 KiB D; each 512 × 32-byte lines, 32 ways | VA bits 8:5, 16 sets | S §§6.1–2/SI: round-robin per set; D write-back/read allocate, I read-only |

Tags contain virtual line base and valid flag. SA D tags also contain the
physical line base and **two dirty bits**, one per 16-byte half (S §6.2).
Each set has a victim cursor initially zero. On an allocating miss, select the
cursor way, evict its old entry, install there and advance modulo way count.
Hits do not move the cursor; invalidation clears validity/dirty state but does
not move cursors; reset zeros cursors. These exact cursor initialization and
invalid-way rules are deterministic engineering choices, not undocumented
hardware replacement details. At most 1024 tags are needed (SA); a compact
16-byte-per-tag representation occupies ≤16 KiB, plus cursors/lookup shortcuts.
The 16-byte host record bound is a storage choice, not a CPU constant.

Consult on **every successful instruction fetch, data load and data store**,
including accesses whose physical address is ROM and all transfers of LDM/STM.
Check current privilege/permissions before allowing a timing hit; no host-pointer
shortcut can authorize a denied access. Unified stores update timing residency
on hits, never allocate on misses, and always generate writes. SA store hits
set the appropriate dirty bit; misses write via the bus/buffer without allocate.
Successful loads hit with zero bus cost or allocate a whole line on a cacheable
miss. Cacheable data ROM loads use the same read cache as DRAM.

Allocation requires M and control C for unified/D caches and descriptor C.
For SA I-cache, control I(bit 12) permits allocation; with M clear all backed
instruction addresses are cacheable (S §6.1). With M set use descriptor C.
Once valid, tags are still searched when C/I is cleared or descriptor C changes:
these controls stop new allocation, not existing hits (T §6.2, A §6.1.2,
S §§6.1.3, 6.2.5). ROM, DRAM and VRAM follow this rule; I/O never allocates.
ARM610's U behavior is a gap, frozen in §9, not silently copied from ARM710.

Line fills pay all four words on unified caches, all eight on SA, using the
physical line's region/configuration. SA always begins at line word zero in
standard mode (S §9.1); no critical-word early restart is modeled. Unified
fills use ascending aligned line words as the G1 interim order. Warm hits are
zero bus cost; the base charge remains. SA eviction writes each dirty half as
one four-word buffer packet before the replacement fill; one dirty half costs
343.75 ns, two 687.5 ns at nominal RAM timing. Clean/invalid victims cost zero.
Unified evictions cost zero, because stores have already gone through the bus.
ROM fills use actual ROMCR costs, including halfword splitting on ARM7500.

### 4.2 Maintenance, MMU changes and reset

- ARMv3 CP15 c7 invalidates all unified tags; c5/c6 TLB operations do not
  architecturally invalidate the cache (T §§5, 6; A §§4, 6). No dirty eviction.
- SA c7, opc2=0, CRm=7/5/6 invalidates I+D/I/D. Opc2=1, CRm=6 invalidates
  the matching virtual D line. Invalidation discards dirty timing flags **without
  writeback**; do not turn a flush into a clean (S §5.2.8 table 5/§6.2.4).
  Opc2=1, CRm=10 cleans matching D line: enqueue dirty half packets, clear dirty
  bits, retain valid tag. Opc2=4, CRm=10 drains buffer to empty. SA c8 TLB
  operations alone retain cache tags. Unknown/reserved encodings keep existing
  guest Undefined/unimplemented behavior, not a made-up timing operation.
- Changing M, S/R, DACR or TTBR invalidates R1's permission shortcuts as today.
  **It does not automatically invalidate timing tags.** Virtual cache maintenance
  is software's responsibility (T §6.3, A §6.1.3, S §§6.1.2, 6.2.4). B/C changes
  affect later allocations/enqueues when translation is refreshed. Record old
  physical bases for SA dirty writeback after a mapping change.
- C/I/W control changes have the allocation/enqueue behavior above. No free
  implicit clean is attached to disabling a cache. MMU-off prevents new D/unified
  allocations/buffered stores; already valid tags are retained.
- Reset/fresh start clears tags, dirty bits, cursors, buffer, fractions, pending
  costs, bus phase, and SA clock-switch state. No generation from a previous
  model may survive Arculator→RPC→Arculator or failed start.

Timing lookup shortcuts may memoize a hot virtual instruction line after its
real tag lookup, but must be invalidated on replacement of that line, any
applicable maintenance/reset and permission/configuration change. They must not
create residency, skip data-side unified-cache eviction, or bypass permissions.
No per-PC, ROM-identity or firmware-loop recognizer is permitted.

## 5. Base instruction cost, transfers and time boundaries

In the fast state the base interval is exactly `1000/rate` ns: 50,
200/7, 10 and 200/7 ns for models 0–3. Source: D-137, not a silicon oscillator.
Pay base once before issuing an instruction; fetch service, blocking data service,
full-buffer waits and maintenance drain are **additional** intervals before the
next instruction can issue. Buffer drain overlaps base time. In particular:

- A warmed cache-hot ALU/branch stream with no DMA/dirty eviction issues exactly
  `rate` million instructions per emulated second. Branches do not incur an
  extra base multiplier; external fetch discontinuity still costs N on a miss.
- An unbuffered cache-hot-code stream of `K` single LDR/STR instructions takes
  `K × (base + N)` through completion. An unbuffered older-ARM LDM/STM of
  `n` words takes `base+B(n)`; SA uncached LDM or unbuffered STM takes
  `base+nN`. A cache fill replaces the missed demand read, never adds another
  demand N on top of the fill.
- Buffered completion and CPU retirement are different. The CPU can run ahead
  by available buffer capacity; a final explicit drain measures bus completion.
  Sustained transfers are bus-limited. Never add every buffered packet duration
  to CPU time and then charge full-buffer stalls again.

For SA-110, reset disables switching and DCLK is driven by MCLK (S §8.2).
Use base `max(1000/rate, 62.5 ns)` while DCLK is in that state, independent
of cache presence. This caps the interim reset base at 16 MIPS without changing
the declared production rate. Enable via CP15 c15, opc2=2, CRm=1: subsequent
instructions use the normal base. Disable via CRm=2: retain the current domain
until the next external instruction/data read miss switches DCLK to MCLK;
then retain the slow base until enable. Disabling **does not immediately force
MCLK** (S §8.2), and buffered writes do not count as the triggering read miss.
No reset PLL delay is added: the session origin is the first executable state,
not deassertion of board reset. Synchronization/CCLK pipeline detail is G3.

Charge only actual successful transfer attempts before an abort. A translated
permission/domain/alignment fault pays the instruction base and its successful
fetch, but no target bus cycle or buffer slot; no later LDM/STM transfers occur.
An external failed target access pays that attempted bus cycle and preceding
completed transfers, then stops. Never refund preceding bus work/stores or bill
the register-list population after execution stopped. Preserve R1's per-model
base-register restoration, loaded registers, LR/SPSR, FAR/FSR and exception
entry. Empty register lists follow existing execution semantics, not a nominal
16-word surcharge. Failed conditions issue fetch only.

### Integer time and event order

Use exact integer rational arithmetic, not floating point or host-pacing input.
All nominal costs have a common **1/28 ns** quantum: 31.25 = 875/28,
62.5 = 1750/28, and 200/7 = 800/28. Larger clock periods in the selected
profiles are integer multiples. Keep cumulative CPU/service fractions; expose
timestamps as `ceil(ticks/28)` ns. Do not round each 156.25 ns access up to
157 ns. The denominator 28 is an arithmetic derivation (LCM of 4 and 7),
not a hardware clock. Check bounded additions/multiplications for overflow.

Device deadlines keep R1's own carried rational phases. At an equal visible
nanosecond, deliver due device events in enum order: TIMER0, TIMER1, KEYBOARD,
MOUSE, FDC, IDE, SOUND, VIDEO, INPUT (`rpc_scheduler.h`). Then finish due bus
service/release slots, grant waiting bus work (video priority at a legal boundary,
older buffer packets FIFO, CPU demand last), then issue a newly eligible CPU
instruction. Events precede a coincident instruction/transfer. Deadlines created
at the same timestamp are delivered before issuing the next CPU instruction.

Do not advance directly past events while paying a stall, idle, buffer drain or
long instruction. Carry partially serviced costs, queued video holds, and buffer
progress across frontend/scheduler limits; do not execute the next instruction,
erase a fraction or accrue catch-up instruction credit during a stall. Idle
drains pending writes and services events but accrues no base credit.

Retain R1's **atomic functional instruction at its issue timestamp**: collect its
fetch/data timing requests in program order and service them before next issue.
Its functional memory/MMIO effects use that issue timestamp, while the timing
ledger may complete later. Interrupts raised during service are recognized at
the next instruction boundary, preserving architectural atomicity. This means
an MMIO read in a long transfer samples earlier than silicon would; precise
intra-instruction MMIO sampling is an explicit exclusion, not something run B
may choose differently. Charge the pending requests even if a frame ends.
No batch may use the old flat-credit path past a pending cost or clock-state
change. Component-only instruction execution still gets functional results;
timing tests must use the real scheduler/session or the same timing service.

## 6. Independent calibration ROMs and expected durations

These expectations are frozen before implementation. A separate oracle may
evaluate rational sums and the declared queue/event chronology; it must not
import the core, read measured timing to choose constants, or copy traces.
The authored ROM establishes its own MMU mappings, B/C flags, ROMCR, CLKCTL
and SA switching. No firmware, disassembly, copyrighted seed or test asset.
Setup, code warming and result publication are outside each measured interval.
Use test-only issue/completion timestamps so an I/O timestamp write does not
contaminate the interval. Always report **instructions, transferred words,
fills, dirty halves, packets, blocked time and elapsed guest time**.

Except where named otherwise: nominal `q=31.25 ns`, A7000 CLKCTL=7, SA
switching enabled, hot code in DRAM fitting one unified/I line, no DMA/interrupts,
identity-backed mappings, bank-local aligned data, WB empty initially. For
cache-off data runs on unified CPUs, disable allocation after warming code;
existing code tags remain valid, and choose noncolliding/noncacheable data.
Also run truly cold/cache-disabled instruction-fetch cases separately.

### 6.1 Scalar and multiple-transfer numbers

| Measured work | ARM610 | ARM710 | SA-110 | ARM7500 |
|---|---:|---:|---:|---:|
| 1,000,000 hot ALU instructions, or hot taken branches | 50 ms | 200/7 ms | 10 ms | 200/7 ms |
| One uncached DRAM/VRAM LDR or unbuffered STR | 206.25 ns | 184.821428… ns | 166.25 ns | 184.821428… ns (DRAM; VRAM absent) |
| One older-ARM LDM/STM n words, unbuffered | `50+B(n)` | `200/7+B(n)` | see next row | `200/7+B(n)` |
| SA uncached LDM / unbuffered STM n words | — | — | `10+156.25n` ns | — |
| One cold, cacheable data load, clean victim | 393.75 ns | 372.321428… ns | 697.5 ns | 372.321428… ns |
| Buffered STM n words with empty WB, through final drain | `50+B(n)` for n≤8 | `200/7+B(n)` for n≤8 | `10+Bsa(n)` | `200/7+B(n)` for n≤8 |

For n>8 on older models, sum packet costs `B(8)+B(n-8)` (and include
only one base); capacity waits are part of that same service, not additional
transfer costs. WB-on CPU issue can finish sooner than the last drain.
For every length **1 through 16**, the independent nominal bus table is:

| n | Older unbuffered RAM burst B(n), ns | SA aligned buffered STM/fill grouping Bsa(n), ns | SA uncached LDM/unbuffered STM nN, ns |
|---:|---:|---:|---:|
| 1 | 156.25 | 156.25 | 156.25 |
| 2 | 218.75 | 218.75 | 312.5 |
| 3 | 281.25 | 281.25 | 468.75 |
| 4 | 343.75 | 343.75 | 625 |
| 5 | 406.25 | 500 | 781.25 |
| 6 | 468.75 | 562.5 | 937.5 |
| 7 | 531.25 | 625 | 1093.75 |
| 8 | 593.75 | 687.5 | 1250 |
| 9 | 656.25 | 843.75 | 1406.25 |
| 10 | 718.75 | 906.25 | 1562.5 |
| 11 | 781.25 | 968.75 | 1718.75 |
| 12 | 843.75 | 1031.25 | 1875 |
| 13 | 906.25 | 1187.5 | 2031.25 |
| 14 | 968.75 | 1250 | 2187.5 |
| 15 | 1031.25 | 1312.5 | 2343.75 |
| 16 | 1093.75 | 1375 | 2500 |

Exercise each length in IA/IB/DA/DB modes, starting offsets 0/4/8/12,
DRAM and Risc PC VRAM, loads and stores, B/C/W permutations and permitted/
denied transfers. LDM including PC needs an authored return target; STM
including PC checks the model's architectural stored PC. Do not replace
length-16 semantics with a made-up instruction. The harness measures one
transfer instruction excluding return/setup. For cache-on cold LDM, count
distinct crossed lines: one four-word fill per unified line, one eight-word
fill per SA line; already filled words incur zero demand bus cost. Cache-on
STM still emits writes on unified CPUs, and on SA emits none for warmed D
hits, but the appropriate packets on misses. A7000 absent-VRAM probes expect
its existing value/no effect plus an attempted N-class cycle, never VRAM storage.

### 6.2 Streaming, random and buffer saturation

Use **K=1024** transfers and also K=1,2,8,9,32 to expose capacity edges.
The following expected durations are exact through final completion/drain,
with `I` the statically counted instructions in the measured authored stream.
`b` denotes that model's base; helper ALU/branch instructions are included in I.
For streaming scalar read loops, use one aligned `LDR` with postincrement,
`SUBS`, `BNE` code line; I=3K. For random reads, use one aligned four-instruction
line: indexed `LDR` with index shifted by log2(stride), `ADD index,#257`,
`AND index,index,mask` (mask register initialized to 1023), then `B` to the
line start. I=4K; the harness stops after K iterations. This produces the
stated permutation without extra pointer-table memory reads or uncounted
address arithmetic. STR variants substitute STR for LDR. Use set-zero code
residency as in the streaming case for unified random-line counts below.

- Unbuffered scalar streaming reads or writes, cache off: `Ib+KN`.
  Data-random version uses offsets `4*((257*j) mod 1024)`, j=0…1023,
  in one 4 KiB bank-local buffer. Same `Ib+KN`; single scalar operations are
  always N. Run DRAM and VRAM; A7000 DRAM only.
- Cold streaming cacheable scalar reads over 4096 bytes: unified caches have
  256 data fills; SA has 128 data fills. Data-only bus totals are
  `256B(4)` = **88,000 ns** for unified models and `128Bsa(8)` =
  **88,000 ns** for SA. Use one aligned code line in set zero, warm it as
  the only resident line, and align the data VA to 4096 bytes. With §4's
  cursors, the final data fill in set zero evicts code once on ARM610/7500;
  subsequent fetch refills it once. ARM710's larger cache and SA's separate
  I cache do not evict code in this stream. Total is `Ib+88,000 ns`, plus
  **B(4)=343.75 ns** on ARM610/7500. Different data/code addresses alone
  do not eliminate unified-cache contention.
- Cold random distinct-line reads: addresses `base + L*((257*j) mod 1024)`,
  where L is that cache's line length. Each data line visited once: unified
  data work `1024B(4)`, SA data work `1024Bsa(8)`. Data bus totals
  **352,000 / 704,000 ns**. For unified total duration add `Ib` and each
  code refill computed by the independent cyclic-set oracle in §4; explicitly
  include the intervening hot code fetches. The stated cadence gives 4/2/4
  code refills on ARM610/710/7500, hence total bus work
  **353,375 / 352,687.5 / 353,375 ns** respectively. SA total is
  `Ib+704,000 ns` with no I refills.
  A warmed working set of one line expects `Ib`; cache-off expects `Ib+1024N`.
- SA read-then-store to each warmed line makes one or both halves dirty.
  Force eviction with 33 distinct lines in one set (stride 512 bytes).
  An evicted line with one dirty half adds B(4); two add 2B(4), plus its
  replacement Bsa(8). Verify clean-only, invalidate-only and clean-then-drain
  separately. Stores without prior loads must not allocate a write-back line.
- Unbuffered eight-word clear stream: K repetitions of an authored
  `TEQ; STMNE {8}; BNE`, condition true for all measured iterations, so
  I=3K. Older models expect `3Kb+KB(8)`; SA expects `3Kb+8KN`.
  At K=1024 these are **761,600 / 695,771.428… / 1,310,720 /
  695,771.428… ns** for models 0–3. These are synthetic loop counts, not
  firmware-derived constants.
- Buffered version of that stream, initially empty and ending with measured
  drain service (exclude the drain instruction's base): the bus stays busy
  from the first STM issue at `2b`. Through completion expect
  **`2b+KB(8)`** on older models, **`2b+KBsa(8)`** on SA.
  Thus **608,100 / 608,057.142… / 704,020 / 608,057.142… ns**.
  At small K, expose CPU retirement separately; finishing enqueue does not
  mean finishing external stores. Repeat with enough ALU spacing that each
  packet drains before the next; no full-buffer CPU stalls then occur.

On SA use the architectural c7 drain. On older models, use an authored
uncached read to force drain and observe the WB-empty endpoint before that
read's demand service; exclude that read's own base/N from the interval.
Do not invent a SA-style drain opcode on ARM610/710/7500. A test hook observes
completion, never creates guest traffic or empties the buffer for free.

For all other buffered streams the independent FIFO oracle is specified as
follows. Each instruction becomes eligible after its predecessor's completion
plus b. Enqueue its packets in transfer order at that instant; if capacity is
unavailable, move its enqueue instant to the first completion freeing the
needed slots, then retry. A packet's service starts at the later of enqueue
and prior packet/bus service completion, after any eligible hold. Its finish
is start plus its declared region word/burst costs. The final CPU completion
is after its blocking requests; the drained endpoint is the last packet finish.
This recurrence, fixed capacities and §2's clock-phase rules fully specify
finite-K durations; an implementation result is not an oracle input.

Cross C=0/1 and B=0/1 with W=0/1 (and M=0 separately). Read-only runs are
unaffected by W with an empty buffer. Unified cache-hit stores still drain bus
packets; SA hit stores do not. Single SA STRs to adjacent words never merge;
STM does. Alternating nonadjacent writes must hit ARM610's third address-slot
stall, ARM710/7500's fifth, SA's ninth, and all older models' ninth data-word
stall when packets have not drained. Use the declared arrival intervals and
long RAM packets to reach those boundaries; separately schedule an authored
video hold while packets remain queued to test suspension and resumed
backpressure. A global video hold also stops CPU issue, so it cannot be used
to enqueue an arbitrary number of new instructions while the bus is frozen.
Check partial-packet/abort boundaries.

### 6.3 ROM, I/O, DMA, memory size and clocks

| Workload, per model | Independent expectation |
|---|---|
| Cold ROM instruction stream, control cache disabled and flushed, K fetched words | `Kb + ROM(K)`; compute ROM(K) from §2.4, not a data-read surcharge. Test sequential and taken branches separately, reset ROMCR and programmed burst codes 1/2/3 |
| Warm ROM hot loop, MMU on/C=1 unified or SA I enabled | Kb after warm-up; SA also runs with M=0; no external ROM words until eviction/invalidation |
| I-cache/unified fill from ROM | Four words for unified, eight SA; reset IOMD non-burst fills **875 / 1750 ns**, plus base. A reset 16-bit/divided fill is **7000 ns** for four words. Program 32-bit normal-speed burst and half-speed/16-bit variants independently |
| I/O register loop | Hot code, K reads/writes of non-side-effecting IOMD ID/scratch control; `Ib+K×125 ns`, at q=31.25. Start/end phase and aliases stated; A divided memory clock doubles PROG cost |
| SuperIO/podule loops | Full A/B/C/D or slow/medium/fast/sync costs from §2.5, plus b and exact phase alignments. Use safe reads/no-device writes; absent bus values remain as R1. Test IOTCR/ECTCR changes between accesses |
| DMA contention | Authored raster from `rpc_video_rom.py` geometry/data rules, with exact line deadlines and transfer counts. Append 350D+156V ns of hold service at those deadlines, D/V including cursor. Simulate grants/restarts per §2.6; compare hot ALU, LDM/STM, full WB and idle. With no CPU grant active, K hot instructions plus nonoverlapping contained holds take `Kb+350D+156V` |
| RAM sizes | Timing-only/harness bank population 4/8/16/32/64 MiB; same resident bank-local stream has identical times. Scan X bytes by STM8, with packet boundary/alignment fixed: bus work scales with X/32, not a capacity-dependent speed constant. Crossing 64 MiB bank boundaries starts N. R1 production memory remains 64 MiB; do not expose a RAM option to run these checks |
| SA reset/switch/disable | Warm hot loop K=1024 before enable: **64,000 ns** base; after enable: **10,240 ns**. Disable while hot preserves fast base until first external read miss; afterward 64,000 ns. Add read/fill costs explicitly, test same guest at production rate and test rate 20 without changing either declaration |
| A prescalers | At CLKCTL bit 1 clear all memory tick costs double, bit 0 clear all I/O edge periods double. Reprogram only between completed transactions; a pending packet retains its already started cycle timing, subsequent cycles use new register state |

The smaller RAM populations are timing-harness fixtures; the handoff does not
authorize extending R1's production allocation/plan admission. Test mirrors
separately to show they retain their timing class. ROM sizes 2/4/8 MiB do not
change bus speed; cached virtual mirrors may have separate tags. Reject 6 MiB
as before. At reset, SA instruction fetch is slow both from base MCLK state
and from ROMCR; enabling I alone must not also enable clock switching.

### Tolerances

For a fully specified queue/event fixture, compare the final ceiling-rounded
integer nanosecond with **≤1 ns** difference, including fractional carry. Counts,
tag hits/fills, packet traffic, device event order and data/exception results
are exact. One nanosecond covers observation rounding, not percentage fitting.
Phase-aligned I/O fixtures use exact expectations, not an arbitrary ±one-cycle
window. Unaligned fixtures calculate their exact phase from issue timestamps.

Long hot-throughput runs compare cumulative completion to `ceil(Kb)` within
one ns after setup; counting instructions in a fixed time window permits one
boundary instruction. DMA tests compare exact event/hold/grant recurrence;
do not allow a per-line instruction tolerance to accumulate away bus costs.
Private firmware-derived decision estimates below use **±1 ms** only for the
diagnosis's approximate 10.93 M count and 224 ms observation, not for synthetic
calibration. Hardware gaps are labelled fidelity exclusions, not wide test
tolerances. Windows/macOS/Linux/Android and flat/burst frontend pacing must
produce byte-identical analytic timing traces.

## 7. R1-1 validation prediction, never calibration

The work order gives about 10.93 M instructions in a three-instruction
eight-register STMNE clear loop, followed by about 224 ms of timer-paced IIC
reads. Assuming the instruction count is predominantly that loop,
`J ≈ 10,930,000/3 = 3,643,333.33` successful eight-word stores, transferring
about 29.15 M words. The noninteger J indicates an estimate, not an exact
trace length. The decision is after **both** phases; BAT is 500 ms after ACK.

With warmed instruction fetch and nominal clocks, the model predicts:

| Model, production ceiling | Base-only window in R1 | New WB-active streaming estimate | New WB-off estimate | Expected BAT/reset result |
|---|---:|---:|---:|---|
| ARM610, 20 | 770.500 ms | `224+JB(8)` = **2387.229 ms** | `224+546.5+JB(8)` = **2933.729 ms** | BAT before decision; reset expected |
| ARM710, 35 | 536.286 ms | **2387.229 ms** | `224+312.286+JB(8)` = **2699.515 ms** | BAT before decision; reset expected |
| SA-110, 100, switching enabled | 333.300 ms | `224+J×687.5 ns` = **2728.792 ms** | `224+109.3+J×1250 ns` = **4887.467 ms** | BAT before decision; reset expected |
| ARM7500, 35, normal memory clock | 536.286 ms | **2387.229 ms** | **2699.515 ms** | BAT before decision; reset expected |

WB-active includes steady-state base/bus overlap, not a second sum of both
costs. Queue lead/tail changes these estimates by microseconds, not hundreds
of milliseconds. Unified stores remain write-through even with hot data.
SA streaming stores do not allocate, so the write-back cache cannot absorb
an arbitrarily large clear. If its targets are already D-cache-resident, only
that finite resident footprint escapes immediate bus traffic; dirty eviction
still consumes service. SA reset-clock residence and uncached ROM fetches,
video holds and I/O polling overhead may lengthen the window. A7500 divided
memory clock doubles the quoted bus component.

As a sensitivity check, even an ideal uninterrupted all-S stream, faster than
this proposed model, takes `J×8×62.5 ns ≈1821.667 ms` on the bus, giving
**2045.667 ms** with IIC. Thus the qualitative prediction is not selected by
the 240 ns counterfactual. No parameter was derived from BAT or the 500 ms
threshold. The counterfactual's 240 ns/store is not a calibration constant.

These are conditional estimates, **not evidence that the private factory reset
has passed**. The public diagnosis does not state per-pair B/C/W, exact store
count/targets, ROMCR, cache residency or the SA switch timestamp. Fable must
record those numeric phases privately and check the prediction, IIC pacing
and the R2 completion oracle on every supported pair. Additional per-poll I/O
costs can extend the approximately 224 ms phase; they must not be subtracted
to force the table. A pathological SA loop repeatedly storing only already
hot resident lines would defeat the streaming assumption and require a new
diagnosis, not an increased timing constant.

The handoff says "all seven" pairs but its parenthesis lists **six**:
3.71 SA/710/7500, 3.70 SA, 3.60 710, 3.50 610. The seventh pair needs naming.
The older residual says the 20/35 production models succeed already; the
new diagnosis describes their failure when forced to 100. Those statements
are compatible. The table uses the production ceilings, not the forced sweep.

## 8. Host cost and R1 regression surface

At 60 Hz, 100 MIPS implies at most 1,666,667 instructions in a 16.666667 ms
field. R1's Odin3 p95 0.70 T leaves **0.10 T = 1.666667 ms** to the AC-1
0.80 T gate: **1.0 ns extra per instruction** if guest progress stays at the
ceiling. This is derived from D-131/R1 AC-8, not a measured cache budget.

Planning estimate, to be measured on Odin3: a generation-validated hot-line
memo adds roughly **0.3–1.2 host ns/instruction**; a full tag lookup roughly
**3–20 ns/access**, with ARM610 64-way and SA 32-way scans at the costly end.
These ranges are engineering estimates, **not sourced silicon timings or
benchmarks**. At a hypothetical 15% data-access density, 0.3–1.2 ns fetch cost
plus 3–20 ns/data access gives **0.75–4.2 ns/instruction**, about **0.075–0.42 T**
at full guest throughput. A naive full instruction-side way scan is unlikely
to fit. Tags remain preferred, using the ordinary hot-line shortcut, compact
sets and event-bounded batches; allocation/dirty eviction needs full lookup.

The model may execute fewer guest instructions under stalls, reducing host
cost, but that is not proof the tag overhead is cheap. Run B must measure the
unchanged R1 AC-8 workload and this document's hot ceiling case, report guest
instructions/work units per emulated second as well as p95 host/T, and retain
AC-1 p95≤0.80 T/no frame>T and AC-10's >10% gameplay-drop Founder gate.
Do not manufacture headroom by lowering rate or muting device work. No device
performance or implementation correctness was measured in this document run.

If measured tags cannot fit, the bounded alternative for Fable to decide is:
retain exact bus/buffer/clock service; I-enabled cacheable fetches are assumed
hot, and D/unified data accesses are assumed misses with allocation suppressed.
It charges a line fill per cacheable read and an external write per store.
This is a fixed approximation, not an upper bound on total hardware time:
write-back eviction can transfer more words than an individual store. An
independent conservative traffic envelope is zero through one full fill plus
one full dirty castout per data load, and zero through one full dirty castout
plus an external demand write per store, before buffer overlap. I traffic lies
between all hits and a full fill per cacheable fetch. It misses capacity,
conflict, dirty eviction, warm-data speed and ROM I-miss stalls, and can both
overstate and understate runtime. **It is not authorized by this contract.**
AC-1 would have to replace hardware cache calibration with these explicit
bounds, remove cache-residency/dirty-traffic fidelity claims, and revalidate
game progress. The source gaps alone do not justify silently taking it.

### Committed traces and independent goldens

Inspect every changed field against a revised independent timing oracle; never
regenerate goldens from core output. The current committed set is:

| Artifact | Expected effect of run B timing |
|---|---|
| `tests/fixtures/rpc-cpu.trace` | Functional register/MMU/exception records should stay byte-identical; component tests currently have no timing fields |
| `rpc-cmos.trace` | Direct bit-level CMOS/RTC functional trace should stay unchanged; scheduler-issued IIC chronology changes elsewhere |
| `rpc-plan-matrix-0..3.trace` | The 173 functional records should stay identical; full runner snapshots/executed counts at fixed time change. Do not rewrite correct permission/PC values |
| `rpc-d2-0..3.trace` | PCM hashes and possibly DMA counts/state observed by fixed instruction-loop waits change because source-enable/rate/disable MMIO chronology moves; accounting identities, wire bytes and intrinsic sound rates stay exact |
| `rpc-video-0..3.trace` | Geometry and uniform-field analytic hashes remain where setup finishes before sampling; timed split/memory/current/ring/palette hashes can change if slower setup/IRQ handler crosses a raster edge. DMA per consumed pixel remains independently derived |
| `rpc-plan-av-0..3.trace` | Corresponding raster, PCM and mixed first-field hashes can change for the same reasons; input wire content remains unchanged when read after delivery |
| `rpc-plan-devices.trace` | Timer/FDC/absent-bus/keyboard results should remain unless the fixed horizon no longer reaches them; equal-deadline tests need explicit new service chronology, not weakened assertions |
| `rpc-factory-2.trace`, `rpc-factory-3.trace` | Synthetic 256-byte pattern, mask and fixed 30-second endpoint should remain if guest completes; R2 CMOS oracle changes belong to that separate work-order item, not invented timing goldens |

Paths in table rows without a directory are under `tests/fixtures/`.
R1 generators `rpc_d2_goldens.py`, `rpc_video_goldens.py`, `rpc_plan_goldens.py`
and the associated expectation documents must explain actual deltas. Session
tests' instruction ordinals no longer imply `ceil(n*1000/rate)` timestamps when
external work applies. Preserve intended tied-event tests by authoring timed
IOMD waits/known-cost sequences; do not silently accept an accidental untie.
R1 clears base credit fractions during a hold; §5 instead suspends unfinished
base time and retains exact cost fractions. Boundary-instruction deltas from
this change must also be explained, not mislabelled as extra DMA traffic.
AC-8's per-line palette progress also needs a new independent schedule check.
Arculator code, costs, fixture bytes and goldens are untouched, including
`arculator-reset-poweron.trace` and its separate carried follow-up.

## 9. Source gaps and review decisions before run B

The interim values below are frozen proposals so implementation has no hidden
design choices. Fable accepts or replaces them explicitly, then authorizes B.

| Gap | Missing evidence / consequence | Most defensible interim choice and why |
|---|---|---|
| G1 | Full ARM610 cache line/replacement/U behavior; equivalence of shipped ARM710 to supplied ARM710a; exact random replacement algorithms | T appendix A proves 4 KiB/64-way/2-address buffer but not line width. Use 16-byte line and read-allocate/write-through family behavior; use T §6's 16-byte ARM710a lines, not an unsourced eight-word assumption. U is provisionally treated updateable, even when clear. Cyclic victims replace unspecified random placement on 610/710/7500; ascended aligned fills. Hardware conflict/U and critical-word timing claims excluded until direct manuals establish them |
| G2 | A7000 board MEMCLK/prescaler wiring; historical ARM610/710 FCLKs | External A MEMCLK 32 MHz, divisor honored, external I_OCLK 32 MHz per A §16.1; normal memory q=31.25 ns matches A's worked examples. Reset q=62.5 ns follows documented divider. Rates are the D-137 ceilings, not FCLK; this can only be hardware-qualified after board evidence |
| G3 | Risc PC StrongARM CONFIG/SnA/card burst conversion/synchronizer specification; TRM itself describes eight-word line transactions | Standard mode and max-four aligned bus bursts from the work order; nominal 16 MHz MCLK from I, exact IOMD word timings, zero extra card wait. Split eight-word fills/writebacks into two N+3S groups, 687.5 ns. Before switch use 62.5 ns base; retain domain on disable until read miss. This is a controller-derived lower wait estimate, not a proven card schematic |
| G4 | Exact row retention and size-programmed IOMD page breaks | Begin N per instruction/packet, restart at aligned 1 KiB/bank boundary; no row-open state between transactions. A supports 256-word pages and I permits up to 256-word bursts; this conservative common bound may add N where real IOMD retained a row. At most 3q extra per artificial restart versus all-S continuation; bandwidth claims are for this bounded model |
| G5 | Explicit IOMD PROG cycle length, undefined IOMD IOTCR/ECTCR reset values, board I/O phase/glue, unsupported READY/handshake behavior | 4q PROG from A §17.4 (125 ns nominal), full I table PC cycle counts, legacy minima from A table 15-2, clock-edge alignment, no READY waits; preserve R1's zero IOTCR/ECTCR initialization; all I/O noncacheable/unbuffered in timing. Gaps cannot be represented by "free" I/O or fabricated variable device waits |
| G6 | Exact per-word buffer release, read-priority and cross-instruction coalescing in each CPU | FIFO complete-packet release, no old-ARM cross-instruction merging, full drain before external data reads. Documented capacities and eligibility retained. Conservative loss of overlap versus silicon; at most the extant finite queue's service is added to any one read, though repeated reads can accumulate this difference |

Review must also accept the §1 translation/pipeline/coherence exclusions and
§5 atomic issue-time functional sampling. They are architecture limits, not
missing nanosecond constants. The table makes a general correction independent
of firmware outcome, but the available evidence does not support declaring
cycle-accurate hardware timing or a guaranteed host-budget pass. Run B's
authorization must reconcile AC-1 with these precise limits and identify the
seventh private firmware/model pair.
