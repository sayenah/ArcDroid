<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Independent lockstep corpus expectations

Grounding: `bb5008fb58bc60bcd2d46bc65c98e67ea2f96956`, D-198 and M2a
amendments A-1–A-34 (emulator sources unchanged from `43c804a`). Expectations below are architectural arithmetic or declared
timing recurrences. Neither this document nor `rpc_lockstep_expect.py` takes a
core execution as an input. Generated manifests expand these rules into exact
RAM words. An assertion checks every word, including reserved zero words.

`rpc_lockstep_rom.py` imports the existing Matrix and Guest builders. The Matrix
initial signature checks its recording vectors and RAM/MMU setup. Each realised
feature adds a marker of one. A final `0x600d` marker precedes the terminal loop.
RAM signatures begin at byte offset 262144 and stay below 1024 words. The
fuzzer reorders independent feature bodies using the specified integer PRNG and
chooses immediate values in [2,127]. Its per-frame instruction budget is 200000.
The CI subset is seed 1 on four models; local-full adds seven seeds, for 32
fuzz programs. Directed items add 60 programs, so CI has 64 items and local-full
has 92. Build generation emits 2 MiB ROMs and relative plans; no generated
bytes or per-seed golden hash pins belong in source control.

| Row | Independent expectation and observable channel |
|---|---|
| G-1 issue ticks | Eight separate ROM instruction intervals per model use exact rational `base(c)+rom(c,1)` from `rpc_timing_oracle.py`, converted to integer ticks by its declared denominator. The test-only interval loader calls the existing interval API. |
| G-1 delay zero | FDC reset finishes after 500 us; the guest waits 1000 us and consumes four reset statuses. SEEK cylinder 0 to 0 has zero steps and zero delay. The immediately following IRQREQB load must contain bit 0x10. |
| G-1 NULL batch | First issue is `ceil(1000/rate)` ns, with the SA slow-clock minimum of 63 ns. Advancing to one ns before it executes zero instructions; reaching it executes exactly one; advancing one more ns executes no second instruction. |
| G-1 field/capture/SWI | The existing run-frame API must return a completed 32×8 field within four calls, each bounded by 16666667 ns. Field completion exercises the driver's forced quota-one path. Factory capture clips exactly to 30000000000 ns and remains there on another advance. SWI observer key `swi=0, mask=0xffffff, modes=all, kinds=SWI` unsubscribes itself: exactly one callback and identical guest snapshot to the unsubscribed run. |
| G-1 prefetch | A data fault first establishes FSR=5, FAR=0x00400100. A prefetch fault at VA 0x00400000 records LR=0x00400004 and preserves those data-fault registers. |
| G-2 | In User32 an MSR attempt cannot change I/F: low CPSR remains 0xd0. SVC26 reads 0xc3. Sixteen User26 TEQP operations may change NZCV, but not I/F or mode: MRS yields 0xf0000080. Raw R15 increment over 0x03fffffc carries into F; the following MOV-PC sample is 0x0c000007. |
| G-3 | Forced FIQ increments its handler counter once. Unmasking a pending IRQ observes counter=0 and LR=boundary+8. The zero-delay FDC event variant executes one intervening ADD and observes counter=1, LR=last-parameter-PC+12. A pending masked event after TEQP must refresh reg[16] to low byte 0xc3. |
| G-3/Q-8 prefetch-FIQ | Raw 26-bit carry unmasks F just before a prefetch abort. The first ABT instruction commits LR before FIQ runs. Both the committed ABT LR and the FIQ-observed value must equal 0x03fffffc. This also covers the G-1 prefetch exceptional exit. |
| G-4 | AP=3/domain 1 permits DACR client=1 and manager=3. None=0 and reserved=2 fault with domain FSR=0x19. A privileged AP=1 load warms the TLB; a User-mode hit still faults with FSR=0x1d. Colliding data and denied installs both displace the fetch entry, so the next instruction comes from replacement physical RAM and records 2; the denied case separately records its fault and FAR. |
| G-6 | A cacheable body and more than the declared cache ways of data occupy set zero. The loop counter ends at zero. Unified-cache eviction changes timing; on SA the data and instruction caches are separate, so the unified-data eviction sensitivity variant has a structural zero. Exact transient memo/tag contents await M2b. |
| G-8 | Buffered-store bodies write ROMCR and (ARM7500) CLKCTL before returning, then read ROMCR=0x19. The independent interval test isolates a ROM-fetching STR: its own fetch uses the newly written ROMCR/CLKCTL, followed by the 125 ns IOMD access. Expected interval is `base(c)+rom(c,new_control,1)+125 ns`. Whether pending work is at capacity and which mid-drain fields change require A-8's M2b stream channel. |
| G-9 | Executing through RAM mirrors 0x10/0x12 and 0x11/0x13, and VRAM 0x020/0x022, first records MOV result 1, then the chosen value after STR, STRB, STM, STRT or SWP. STRB changes the immediate byte only. ARM7500 has no VRAM pair. A store at PC+4 must affect the very next MOV. An STM crossing an invalid page commits 17 and 34 only; later words remain zero, FSR=7, FAR=0x00601000. |
| G-10/E11 | Thirty-two calls execute c7 maintenance in a cacheable body, returning 42. The existing timing implementation's c7 sensitivity variant changes runner timing. A future translated-generation variant belongs to its introducing B slice. |
| G-14 | SA c15 wait (`cp15.c:280`) and ARM7500 SUSMODE sleep with buffered stores. Timer0 expires after `(999+1)/2 MHz=500 us`; the read after wake must see the chosen value. A pending force IRQ also wakes while I is masked. ARM610/710 have no corresponding sleep instruction. |
| VA key | One physical body is mapped at 0x00418000 and 0x00518000, then executed through MMU-off 0x12018000. ADR and BL link each yield entry VA+8; LDR-literal yields 0x71; B preserves control flow. A future VA-less compiled key must fail these distinct values. |
| AC-M2 | Combines PC-next SMC, ordinary alias SMC, denied fetch-entry displacement and a ROMCR=0x19 burst of 32 ADDs (result 32). A separate eight-instruction burst interval uses the independent sequential/nonsequential ROM recurrence. |
| AC-H2 | Combines DACR permission/fault paths, mode changes, buffered writes and SA slow/fast clock transitions (`cp15.c:281–283`). Models include integral and fractional base timing. M2b demonstrates counter split, fault attributes and one-tick chronology corruptions through its real-C emitter mutant matrix; masked-reg[16] corruption remains observable through G-3. |
| Observable descriptor walks | TTBR lies in physical SuperIO space. The guest commits 0xcafe before enabling the MMU, then repeatedly prefetch-aborts on invalid descriptors. It ends in ABT32, low CPSR 0xd7. Profile MMIO reads equal prefetch-abort exits and are positive. Exact walk ordering/timestamps and absence of speculative extra walks are differential-only until M2b's physical-dispatch event log. |
| E8 bad mode | A STR commits 0xface to RAM before MSR requests illegal mode 0x14. The terminal fault must be reported. Diagnostic registers remain checked: r0=0x14 (illegal mode), r1=0xface (store operand), r15=last-instruction-address+8=0x140, reg[16]=0x14 (status written by MSR). The M2a diagnostic checks did not establish the RAM write because `arc_rpc_session_read_ram` refuses access when `arc_rpc_fault` is true. The independently specified RAM expectation is now **demonstrated** by M2b (A-32), through the committed write log and independently computed page hash described below. The item remains in every oracle run. Named schema rule `q9_first_fatal_frame_cpu` excludes only the first fatal frame's `cpu=` in cross-quota mode (A-33, Q-9); strict mode retains it, and all other fields and frames stay compared. |

All corpus items and interval/boundary legs run observer off, unmatched, trace
and active configurations. The corpus additionally compares quotas 1, 7 and
200 and two identical runs. Strict stream comparison does not exclude these
semantic subscription differences; the separate guest-output mode does.

The request/packet budget fatals are internal invariants at this head. An ARM
instruction transfers at most 16 registers. A worst-case SA transfer can append
two dirty half-line castouts and one fill; including instruction fetch gives
at most 49 commands, below COMMANDS=128. Each fill has at most eight words and
each direct multiple-transfer packet at most 16, below WORDS=16's overflow
condition. Pending commands are drained before another instruction begins.
No valid bounded corpus program can deliberately overflow either invariant.

The driver instruction cap is 1666667 per advance; an advance is at most
16666667 ns and rate is at most 100 MIPS, hence at most 1666667 issues including
the limit instruction. A valid guest cannot exceed the cap inside this window.
Clock-time overflow needs a scheduler time near its tick-scaled limit; guest
code cannot directly set that time. The denominator is 28×rate
(`rpc_mem_timing.c:207`). At 100 MIPS, reaching UINT64_MAX/2800 ns takes about
76 emulated days, or 395 million maximum-size advance calls. That long-running
case has not been exercised for lack of execution time; this is a missing
fatal-path check, not a proof that overflow is globally guest-unreachable.
The three budget-fatal ROMs remain absent. R-M2b-S2 authorizes **injected**
evidence in the lockstep core alone: request, packet and instruction failures
are forced through their existing checks at a chosen key, with unchanged
budgets. `rpc-lockstep-emitter-fatal` checks the fatal phase, preceding full
RAM/VRAM page frames, committed write log and End. These paths are **injected**,
never described as guest-reachable. Clock-time overflow remains unexercised.

The bad-mode RAM expectation is **demonstrated** by the same M2b test on all
four models: the write event records `0xface` at `0x10040000`, and allocation 2,
page 64 has the independently computed SHA-256 of that committed word followed
by zero bytes. This evidence comes from the stream before observer retirement;
the faulted-session RAM-read API continues to refuse access.

M2b's fatal evidence/kill matrix uses these explicit labels and six-word keys:

| Probe | Reachability label | Fatal key | Committed word |
|---|---|---|---|
| Bad mode, each of four models | guest-reachable authored ROM | `(1,2,11,5,1,0)` | `0xface` |
| Request budget, kind 1 at k=12 | **injected**, lockstep class only | `(1,2,12,5,1,0)` | `0x2a` |
| Packet budget, kind 2 at k=12 | **injected**, lockstep class only | `(1,2,12,5,1,0)` | `0x2a` |
| Instruction budget, kind 3 at k=12 | **injected**, lockstep class only | `(1,2,12,5,3,1)` | `0x2a` |
| Clock-time overflow | unexercised limit | no demonstrated key | none claimed |

The outside-attempt instruction-budget fatal retains the last maintained
site/sequence in dedicated phase 5 (critic H2 overrides draft phase 4).
Every demonstrated probe checks preceding page frames and committed log, End,
and P1-on/off equality. The three injected paths are coverage probes, not three
additional mutant kills. `fatal-after-retire` is a separate real-C mutant killed
by the guest-reachable bad-mode item.

`rpc_lockstep_emitter_mutants.py` supplies the M2b digest evidence that the
M2a-only matrix could not report: all four AC-H2 corruptions, omitted/extra P1,
projection/ordinal/fatal errors, delayed CLKCTL, replay/exception/quota, and
buffer/video chronology. Its generated matrix marks every single-item kill.
The M2a runner retains its historical evidence scope and does not claim to have
executed that companion suite. Block-key, VA-key and G-10 generation remain
awaiting candidate mechanism; compiled-fetch coverage is deferred to the first
compiling slice (A-26).

M2b demonstrates independent write observation at P3 and exact
transient/buffer reach. The full-audit compiled-fetch experiment remains with
the first compiling slice (A-26); no compiled path exists in M2b. M2c owns
G-13, reload-after-fatal and
allocation/fatal cleanup under lockstep. The oracle-only reset/allocation-fault
legs remain here. Deferred mechanisms already have ROMs and independent
expectations above; their evidence level is awaiting emitter or awaiting
candidate mechanism, never demonstrated.

The M2b reach check supplies A-8's actual field evidence on each model: an
authored conditional eight-word STM reaches `memory.bwords=8` (ARM610/710/7500)
or `memory.bcount=8` (SA110); G-8's timing-register data-write exit retains a
pending command; G-9's PC-next store keeps the old opcode at P2 and captures the
stored opcode at the next fetch. The implementation report records those keys.
Actual-stream RAM/MMIO cross-order and physical-time mutations fail comparison
after an unchanged control passes. These are observed guest states, not injected
capacity or timing values.

M2b's per-item P1 counts use the separate no-emitter profile core plus the
declared guest instruction structure (`rpc_lockstep_site_counts_test.py`).
Fetches use the fetched counter; accessor counts add the declared aborted
accesses to successful read/write counters and require paired entry/exit.
Exception counters and the authored MRC/MCR/SWI/mode sequences supply the
remaining expected sites. All 92 local-full items pass on all four models.
During derivation, the separate prefetch/FIQ guest's FIQ-mask STR was initially
classified as a read abort. Inspection of its unmapped section 0x32 and authored
data-abort trampoline corrected the expectation to one write abort. No item or
field was excluded to obtain those counts. This correction is independent of
the cross-quota representation rulings below.

Fable S67 PR #504 resolves the three checkpoint oracle findings only in mode
(b). R-M2b-O1 excludes the requested `rpc_session.arc_rpc_test_quota` as the
independent test configuration. R-M2b-O2 compares `memory.active_kind` only
while `memory.active_valid` is true; validity stays compared. R-M2b-O3 projects
`arm.inscount` to raw counter plus unbooked batch P2 credit modulo 2^32 at
P0/P1/P2, retaining the already booked raw count at P3, as defined in
`docs/riscpc-lockstep-harness.md`. Strict keeps all three raw values.
The reviewed policy and schema pin their reasons. Mode-scope vectors check
inactive and active requests, validity changes, strict quota/counter changes,
guest-register sensitivity and projected completion sensitivity. A real C
`projected-inscount-one-completion` and `raw-inscount-delta` mutants must fail
mode (b) on that projected field, after the unmodified candidate passes.

Full checkpoint consistency compares quotas 1, 7 and 200 for every selected
authored item, including every local-full seed. Each run reaches the separately
derived RAM/feature/CPSR endpoint and both frontend periods; fatal items retain
their committed logs, page frames and End. No event-only equality substitutes
for checkpoint equality. The CPU and CMOS standalone goldens are accepted
component-only coverage under PR #504, with no sink attachment claimed.
Windows runtime durations and W4 timeout evidence come from the PR's CI
durations; no Windows timing is inferred from this host.


Run C evidence qualification: G-6's `data-fill-no-memo-eviction` and
G-10/E11's `c7-no-op` are **detected by differential only**: their independent
RAM signatures pass, and the observed difference is the frame instruction
count. No independent timing recurrence is claimed for those rows yet.
`ROMCR-after-drain` is also noticed differentially by G-8 and is independently
rejected by `rpc_lockstep_timing_test.py` at **AC-M2-burst-0**, using
`rpc_timing_oracle.duration`. `idle-no-drain` is detected by a **terminal fault
exit** (`rc=1`, `fault=1`), before the stale-read-after-wake expectation can be
checked. The sensitivity matrix records each actual oracle and its row.

## Run E cache fidelity and boundary coverage

R-M2b-Q1 compares literal streams and complete guest stdout with the owner cache
on and off. The bounded authored cases are input-0 (12,000 instructions, including
QA's mouse update at k=7389), cursor video, audio, timed floppy, an attached ATA
IDENTIFY command and SuperIO parallel/serial interrupt writes. The cache switch
exists only in the lockstep class and does not enter wire flags. Removing only
mouse_process's cross-unit IOMD invalidation must fail the cached run; the same
compiled mutant must pass with reuse disabled. The independent purity check
re-collects every owner, including owners whose dirty bit was not set.

The G-1 first-issue deadlines 50/29/63/29 ns exercise the existing NULL-batch
branch on all four models. Exactly one instruction executes, with projected
instruction_work 0 at P0 and 1 at P2. No injected budget or new execution route
is used. NULL-batch-booked-work removes the booked-work subtraction and must be
killed by these directed streams.

Phase-4 outside-attempt events remain an unexercised M2c G-13 limit at this head.
The RAM/MMIO event funnels are the functional CPU accessors in mem.c; timing's
buffer service models completion without replaying their functional writes.
Video and sound DMA use direct physical backing reads, and floppy transfers
occur through CPU accesses to the programmed DMA port. The lifecycle/reset
paths initialize backing directly rather than calling these event funnels.
Thus a background service or reset does not currently produce an outside-attempt
RAM/MMIO event. The phase-4 wire contract has synthetic coverage; no real-C
phase-4 event or guest reachability is claimed.

Optional detail/compact/page P3 records are exercised in guest-output (c) and
trace-toggle (d), including unmatched records at online span close. A genuine
P0 state difference still fails each mode. Eight independently authored v1
adversarial witnesses kill QA's surviving comparator mutants; three additional
P3 reader/comparator-skip variants are also killed.

## Run F cache and page-boundary witnesses

The video cache case uses `video-paths-3-words`, bounded to 12,000 instructions
per frame. Removing both VIDC20 IOMD cursor invalidations (`dma_reload` and
`next_quad`) must fail with `lockstep mutating projection`; the same compiled
mutant with caching disabled must match the reference stream and guest stdout.
The cross-unit interrupt-status marks are redundant: each writer calls
`updateirqs()`, which already marks IOMD dirty. Those extra marks remain as
defence in depth and are not claimed as independently killable mutations.

The preparation cache fixture appends a probe only to a disposable lockstep
copy of the preparation TU. It binds real preparation-owned attempt/controller
storage and invokes the real `launch_consumed` callback after the first P0 is
written, without calling an observer mutator. The emitted retirement bit must
change from 0 at P0 to 1 at the next fetch checkpoint. Disabling the preparation
generation invalidation must fail cached and pass uncached with identical guest
stdout and stream. This is an authored test fixture, not guest-reachable launch
coverage; no probe or fixture storage is linked into any repository core target.

The v1 comparator witness supplies a valid first page occurrence, then omits
the marker on the second occurrence of the same boundary kind. It must reject
that stream even when the first occurrence had a marker; adding the missing
second marker is a passing control.
