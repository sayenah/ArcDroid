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
| AC-H2 | Combines DACR permission/fault paths, mode changes, buffered writes and SA slow/fast clock transitions (`cp15.c:281–283`). Models include integral and fractional base timing. Counter split, fault attributes and one-tick chronology corruptions await M2b; masked-reg[16] corruption is already observable through G-3. |
| Observable descriptor walks | TTBR lies in physical SuperIO space. The guest commits 0xcafe before enabling the MMU, then repeatedly prefetch-aborts on invalid descriptors. It ends in ABT32, low CPSR 0xd7. Profile MMIO reads equal prefetch-abort exits and are positive. Exact walk ordering/timestamps and absence of speculative extra walks are differential-only until M2b's physical-dispatch event log. |
| E8 bad mode | A STR commits 0xface to RAM before MSR requests illegal mode 0x14. The terminal fault must be reported. Diagnostic registers remain checked: r0=0x14 (illegal mode), r1=0xface (store operand), r15=last-instruction-address+8=0x140, reg[16]=0x14 (status written by MSR). These checks do not establish the RAM write. The independently specified RAM expectation is **awaiting emitter** (A-32): `arc_rpc_session_read_ram` refuses access when `arc_rpc_fault` is true. The item remains in every oracle run. Named schema rule `q9_first_fatal_frame_cpu` excludes only the first fatal frame's `cpu=` in cross-quota mode (A-33, Q-9); strict mode retains it, and all other fields and frames stay compared. |

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
These fatal ROMs are absent, with the limits reported rather
than lowered budgets or fabricated failures. If Fable requires a different
reachability ruling, that part remains unimplemented pending the ruling.

M2b owns independent write observation at P3, exact transient/buffer reach and
the full-audit compiled-fetch experiment. M2c owns G-13, reload-after-fatal and
allocation/fatal cleanup under lockstep. The oracle-only reset/allocation-fault
legs remain here. Deferred mechanisms already have ROMs and independent
expectations above; their evidence level is awaiting emitter or awaiting
candidate mechanism, never demonstrated.
