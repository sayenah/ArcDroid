<!-- SPDX-License-Identifier: GPL-2.0-only -->

# RiscPC lockstep contract v0

Grounding: `bb5008fb58bc60bcd2d46bc65c98e67ea2f96956`, D-198 and
`riscpc-6b-m2a.md`, including A-1 through A-34. Emulator sources are unchanged
from the initial `43c804a` grounding. This note precedes executable
implementation. M2a supplies independent Python evidence; it does not emit C
checkpoints or establish candidate correctness. M2b must re-ground this contract
after 1a-0 and obtain Fable's phase-projection ruling before freezing v1.

The key is `(generation, epoch, k, phase, site, sequence)`, all unsigned integers.
`k` counts instruction attempts at instruction begin, including prefetch aborts.
E2 retries retain k. Phase is 0, 1, 2 or 3. P1 sites are fetch continuation,
data read, data write, SWI, CP15 read, CP15 write, exception and mode transition
(1 through 8); other phases use site=sequence=0. Sequence counts helper visits
globally within an attempt, preserving alternating read/write order. The baseline has one instance of each key;
every duplicate candidate instance must match that baseline instance.

Generation is the session generation. Epoch advances at start, reset, stop,
failed start, rate rebind, backend switch and the R3d reset/rebind transaction.
Save-state loading inherits this obligation. Epoch changes never erase event
evidence. Ordinals and event sequence numbers restart only with the epoch.

## Density and comparison modes

Density is independent of comparison mode. Baselines are dense. Candidates may
declare bounded, disjoint inclusive ordinal spans for sparse P0/P2 records.
Outside those spans every P0/P2 is required. Every helper P1 and every P3 is
required. Full audit also requires the fetch-continuation P1 for compiled code;
ordinary audit permits omission only in explicit compiled spans, with an exact
omission count. Counts of candidate P0/P2 equal declared exit-record counters;
the every-instruction schedule forbids sparse spans. Exit counters count emitted
instances, including retries; unique attempt and continuation counters remain
separate. H-2 adds no P3 or batch flush: it only forces a host block exit.

Four modes apply the schema's independent phase and mode axes:

| Mode | Compared evidence |
|---|---|
| strict | Same schedule/configuration; all included fields, logs and P3s |
| cross-quota | P0/P1/P2 chronology under quotas 1/7/200; explicitly named representation fields excluded; no P3 alignment |
| guest-output | Off/unmatched/trace/active; guest state and runner projection only; observer subscription/owner state legitimately differs |
| trace-toggle | Same semantic subscriptions; all included fields except trace output |

Mode exclusions have no effect in strict mode. Guest-output uses the existing
observation-plan projection prefixes (`frame=`, `regs=`, `video=`, `pcm=`,
`first-raster=`, `ram=`, `cmos=`). It does not assert equality of observer state.
Its stream projection retains canonical guest-machine state, events and pages;
observer admission/owner state, raw host batch positions/continuations and
intrusive measurement state are excluded by named A-25 rules. Remaining driver
budgets and epoch completion counts retain their non-mutating projections.

For runner stdout, schema rule `q9_first_fatal_frame_cpu` (Q-9, A-33) excludes
only `cpu=` on the first frame whose `fault=1` appears, independently for each
run, and only in cross-quota mode. Fatal unwind skips batch-return accounting,
so this diagnostic count depends on quota. Strict, guest-output and trace-toggle
retain it. Every other token on that frame and every other frame stays compared,
including later frames with `fault=1`. Comparator counterexamples check these
boundaries and changes to registers, RAM and other output records. No fatal
frame or item is skipped. M2b specifies the exact fatal executed count (A-29).
The corpus also compares observer configurations at each fixed quota using
guest-output mode, which retains the fatal `cpu=` count, in addition to its
cross-quota checks and byte-identical same-configuration repetitions.

Plan identity denotes the common guest recipe and input identities, not the
literal plan-file bytes after appending quota, observer or trace options. Those
host options belong to schedule or configuration identity. Configuration
identity describes semantic subscriptions and test controls; trace destination
and output-enable settings are diagnostic and do not change it. Consequently
trace-toggle can require equal semantic configuration while changing output.
M2b's driver must make these distinctions when constructing its header hashes.

## Encoding and bounds

All integers are little-endian with no native padding. The magic is eight bytes
`ARCLKv0\0`. A frame is `u8 kind, u32 byte_length, payload`. Header kind 0
contains version u32, schema SHA-256 (32 bytes), model u32, plan SHA-256 (32),
seed u64, schedule SHA-256 (32), configuration SHA-256 (32), density flags u32,
P0 and P2 instance counts u64, compiled-fetch omission count u64, span count u32,
then spans `(u8 kind, u64 first, u64 last)` (kind 0 sparse, kind 1 compiled).
At most 256 spans are admitted; count fields are declarations checked at EOF.

Detail kind 1 contains the key (six u64s), then schema fields in schema order.
Each scalar is u64. Arrays have a fixed schema capacity, encoded as one u64 per
element. Signed source integers are widened modulo 2^64; booleans are 0/1,
and enums use their declared numeric value. A source double is encoded by its
IEEE-754 binary64 bit pattern, never by numeric conversion to an integer.
Pointers are two u64s `(allocation_id, byte_offset)` or a single typed
index. An untyped address is a schema error; unknown allocation IDs fail.
All fixed-capacity inactive storage remains encoded but conditions omit it from
comparison. No raw struct bytes or padding are compared.

Compact kind 2 contains the key followed by one u64 per schema group, SHA-256
truncated to its first eight bytes interpreted little-endian, after applying the
declared mode and phase projection. Compact comparison requires the same mode
and cannot diagnose a leaf without a detail rerun. A mode change needs fresh
streams. Hash equality is probabilistic; detail is the conclusive leaf evidence.

Event kind 3 contains `(generation, epoch, sequence, k, phase, site, site_sequence,
time_ticks, agent, kind, address, size, direction, value)`, fourteen u64s. RAM
writes and MMIO share this sequence: kind 0 RAM write, kind 1 MMIO; agents 0 CPU,
1 descriptor walk, 2 device, 3 DMA. Events are ordered independently of checkpoint
packaging, and compared by semantic identity, time and value. Events precede the
checkpoint that covers them. They may cross omitted sparse boundaries without
changing their semantic identity. Physical I/O dispatch must log descriptor
walks too. Invalid widths, agents, phases and sequence discontinuities fail.

Page kind 4 carries key, allocation ID, page index and SHA-256. RAM and VRAM use
4 KiB pages at each P3, frame end, epoch change and terminal boundary. Fatal
kind 5 carries key and numeric fatal reason; logs and page hashes precede it.
Post-fatal registers are diagnostic, never a replacement for committed evidence.
End kind 6 carries total checkpoint/event/page counts (three u64s); trailing
bytes, missing terminal/end frames, partial frames and oversized frames fail.
Maximum frame size is 16 MiB; no length may allocate beyond that cap.

Two ordered cursors retain at most one current checkpoint per side plus one
baseline duplicate reference. Events and page checkpoints use their own ordered
keys. No index of the stream is retained. The comparator exports its maximum
buffered-record count; stress tests bound it independently of record count.

On a mismatch the result includes the earliest key/field, last matching key and
an inclusive detail rerun range ending at that key. The driver reruns **both**
processes from their original inputs, stopping there. A missing or different
reproduction fails as non-deterministic. Hash-only reporting prints hashes of
keys and values and never guest values; private runs require that option.

## State mapping and projection obligations

The machine-readable schema is authoritative for field order, capacities,
conditions, mode exclusions, classes, citations and inventory entries.

| Invariants §2.2 item | Schema group / rule |
|---|---|
| Raw ARMState, banks, SPSRs, event, model flags | cpu; named members, including reg[16] |
| memmode, cpsr index | cpu |
| pcpsr, usrregs, pccache2, TLB code | typed indices / allocation-offset pairs |
| CP15 state and MMU/cache flags, attributes | cp15 |
| TLB valid entries | tlb; validity always, contents only when valid |
| Scheduler time, armed deadlines, cached minimum, fraction | scheduler; deadlines only if armed |
| Device state, CPU idle, RAM/VRAM | device fields, session idle, page records |
| Fast timing flags, opcode, fetch identity, counts | fast; memo line only if valid |
| Fast phase times, base and denominator | fast; cpu_due at P2 only if !pending |
| Memory model/rate/control, switch/idle/restart | memory |
| Cache valid tags, occupancy and victim cursors | cache; invalid tag addresses ignored |
| Command/buffer rings and live entries | packet; live window and lane<count |
| Active packet, kind, grant | packet; active only if active_valid |
| Cycle/video flags, queue/arrival | memory |
| Cursors, finish, wait_start, last_rom | memory; finish only if cycle/video active |
| Every stats field, including hits separate from fetch_hits | stats |
| Measurement test flags, first/last/drained, interval | measurement; compared in strict mode |
| Observer semantic state, owners, filters, watches, intents, requests | observer; live slots only, pointers by owner |
| linecyc, instruction_work, executed, idle_ns | driver; phase projection below |
| Lanes past count, dead ring slots | excluded conditionally, never blanket exclusions |
| Invalid tag VA/physical | conditional; validity remains included |
| Write-only state including dirtybuffer | host-only/excluded, Q-5 ruling |
| Diagnostic post-fatal registers, trace buffers/counters | diagnostic, Q-9 / §6 |

Static roots are enumerated by translation unit and nm symbol, not a prefix
wildcard. Every data/bss symbol in linked RPC/RPCEmu objects must map explicitly;
an added static fails. Reachable allocations are a second inventory category:
RAM, VRAM, immutable firmware, observer SWI arrays, VIDC presentation storage,
audio/input/media allocations and their pointees are classified independently
of pointer identity. A third category covers live driver locals: event_work,
instruction_work, video_start and limit. Remaining instruction/event/scanout
budgets are preserved by projection at each phase. New exclusions need Fable.

A-34 excludes `arc_rpc_jump`, the host continuation used only for fatal unwind,
and `arc_rpc_logger` / `arc_rpc_logger_opaque`, the frontend log callback and
its context. Each inventory entry records that it holds no guest-visible state,
its host addresses legitimately differ between processes, and the frontend
rebinds it at session start. These are accepted host-only exclusions; no
representation ruling remains pending for them.

For `arc_rpc_executed`, let B be its value on batch entry, C the number of
completed attempts in that batch, and I=1 only after the current attempt's
instruction_done. P0/P1 project B+C; P2 projects B+C+I; P3 projects B+C after
return accounting. The NULL-batch path projects B before entry and B+1 after
the single completed attempt; fatal does not fabricate a completed attempt.
Successful continuations are counted separately, since the last quota/deadline
instruction does not perform one. Raw `driver.linecyc` and actual successful
continuations are included in strict mode but excluded by named A-25 rules in
cross-quota mode. `driver.attempts_completed` is the derived epoch completion
count, which remains compared; it is not an existing driver stack variable.
`instruction_work` uses the same completed
count with the driver-entry base; M2b exposes its remaining budget, rather than
excluding this stack local. In the NULL-batch path instruction_work has already
been incremented before entry: project its stored value minus one at P0/P1,
and its stored value at P2/P3 after completion. A fatal before completion does
not count that booked attempt. event_work and video_start preserve the remaining
event and scanout budgets; representation can differ across quotas only after
this projection. At P0 issue=cpu_due with **no next base added**. Integer times
use algebraic flush formulas in a copy, never `batch_flush`. At P2 cpu_due is
ignored only with pending requests. At P3 service progress is retained.

## Schedule reference

SplitMix64 arithmetic wraps modulo 2^64: add `0x9e3779b97f4a7c15`, xor-right-30
and multiply `0xbf58476d1ce4e5b9`, xor-right-27 and multiply
`0x94d049bb133111eb`, xor-right-31. Functions mix seed, epoch and ordinal in
separate rounds. No call counter, hash(), floating point or Python random is
used. A cadence of length L and shift s exits when `(k+s)%L == L-1`; its
position is `(k+s)%L`. Shifts 0..L-1 put each k at every position. CI samples
positions deterministically; the full local family covers L=1,2,7,64,200.
Disable/re-enable decisions at P0 and P3 depend only on the tuple plus phase;
consumers retain a boolean enabled state. B-0 owns actual (block,position)
coverage. These functions do not create batch boundaries.

Precisely, `sample(seed,e,k)=mix(mix(mix(seed)^e)^k)`. The effective shift is
`(s+sample(seed,e,0)%L)%L`. CI selects up to four shifts starting at
`sample(seed,e,0)%L`. `enabled(seed,e,k,p)` is the low bit of
`mix(sample(seed,e,k)^p)`, for p=0 or 3. A consumer uses this as its desired
state, rather than treating each call as a toggle.

| seed | epoch | k | sample (hex) | P0 enabled | P3 enabled |
|---|---:|---:|---|---|---|
| 0 | 0 | 0 | 238275bc38fcbe91 | false | true |
| 1 | 0 | 7 | de98b4be0f3785fd | false | false |
| 71 | 3 | 200 | d7c8dea25630db22 | true | false |
| ffffffffffffffff | 2 | 1 | eabcfa2c623c5a59 | false | true |

## Corpus evidence and ownership

All ROM bytes are authored at build time. Guest markers prove path execution,
profile counters prove represented MMIO/write/exit reach, and M2b must prove
buffer capacity, mid-drain timing writes and stale-fetch state from stream
values. Feature deletion must fail a realised floor. Every item runs twice and
under quotas 1/7/200 and observer off/unmatched/trace/active. Disagreement is an
oracle finding, never an automatic exclusion. Reset and allocation-failure
oracle legs use existing runner controls; lifecycle lockstep belongs to M2c.

Timing rows use a new test-only dynamic-loader harness over the existing
intrusive interval API. Expectations are rational arithmetic from
`rpc_timing_oracle.py`, not core measurements. G-1 issue/delay-zero and G-8
repricing need explicit independent recurrences; any unavailable channel is
reported as missing evidence, not silently labelled a pass. c15 wait and clock
switch cite cp15.c:280–283 and apply only to SA-110; SUSMODE applies to ARM7500.
The guest-reachable bad-mode fatal has an independently specified committed
write. Its committed-RAM check is **awaiting emitter** under A-32: at this head
post-fatal RAM is inaccessible through the existing test API. The item keeps
independent diagnostic register expectations and the terminal-fault check.
Request, packet, instruction-budget and time-overflow reachability limits
are recorded in `tests/rpc-lockstep-expectations.md`; no fatal is fabricated by
changing the core or its budgets.

Existing-core sensitivity variants belong to M2a and require actual
stdout/signature behaviour changes.
Serialiser/projection mutants await M2b; lifecycle mutants await M2c; block-key,
VA-key and G-10 generation mutants await their introducing B/X mechanism.
The report labels each demonstrated, awaiting emitter, or awaiting candidate
mechanism, predicts its first divergent field and flags single-item kills.
M2b also owns independent write observation at each P3: a shadow-memory/log
cross-check or protection mechanism. Represented evidence tests cannot prove
that a native write-and-restore bypassed the log.

All new tests have explicit timeouts; mutations are serial. The profile
coverage dependency attaches to rpc-profile-tests. Windows timeout margins
require native Windows evidence; host timings alone cannot establish them.
No production source/object, existing generator, workflow or shared fixture
dependency is changed. All new host-only structures (reader cursors, hash
accumulators, schedule state, density counters) are excluded from guest state
and reset on each epoch. No emitter is backported to the Phase 1a anchor.


## Run C review contract and re-grounding

Fable's Run C adjudication governs the following refinements. The inventory
classification tool (`tests/rpc_lockstep_inventory.py`) enumerates the current
built RPC/RPCEmu objects every run; engine-source SHA-256 pins are removed.
The schema records reviewed source/symbol identities, not a compiler's spelling
or a blanket approval of new storage. Mach-O's ABI underscore is removed;
reviewed function-local statics use `function::name`, accepting GCC's terminal
`.N` suffix and Clang's `function.name` spelling only for that reviewed local.
COFF section symbols and format-specific local labels are ignored by explicit
rules. All other data/bss symbols must classify, including unknown suffixed
names. Two raw names collapsing to one identity fail as ambiguous; adding a
same-named local in a second function requires an explicit reviewed mapping.
Compiler aggregate symbols have explicit schema mappings; nm cannot
recover storage eliminated or merged without a separately named symbol.

Run D R-1: GCC ASan `__odr_asan.<global>` and Clang ASan
`__odr_asan_gen_<global>` indicators inherit the global's identity, class,
roots and citation only if that exact global is present and independently
classified in the same object. The exported indicator row records `alias_of`;
it is an instrumentation alias, not a second guest-state allocation. An
orphan indicator, unknown global or chain of indicators fails. Mach-O ABI
underscore removal precedes this rule; no sanitizer-name wildcard is ignored.
Synthetic tables exercise both spellings and ABI forms independently of
whether the local compiler enables ODR indicators.
Run D R-1b: the inventory classifies guest state; sanitizer instrumentation
is not guest state. Therefore `rpc-lockstep-inventory` is not registered when
`ARCDROID_SANITIZERS` is on. This covers Clang registry flags, per-global
descriptors (`__asan_binder_*`, `__asan_global_*`) and assembler metadata, and
GCC init/term markers (`Mod.init`) and sanitizer labels. Inventory coverage
remains on Linux GCC ELF in CI's `host` and `host-arm64` jobs, MinGW COFF in
`windows`, and Mach-O locally. The ODR alias mapping and self-tests remain;
the earlier local sanitizer symbol-subset audit is not a full inventory pass.

After an engine merge, build the plan-core object target and run the inventory
with `--write-inventory` into the external build tree. This committed tool emits
current object, raw symbol, normalised identity, class and schema-root mappings;
unclassified entries remain errors. Review each new identity, add its static
root and field/storage classification with citations, and update only reviewed
aliases if the compiler changed a spelling. Re-run host and MinGW inventories
and their unknown-symbol controls. No source-byte change alone invalidates an
inventory. When 1a-0 merges, classify its new measurement statics under R-e0-2.

Schema validation enforces class semantics. Canonical state is unconditional
and present at every phase; amendment-authorised mode exclusions and conditional
rules are enumerated with exact expressions, member names and citations in
reviewed schema lists. Changing a field's condition, phase set or exclusion
without changing its reviewed contract fails validation. `cp15.tlbs` and
`cp15.flushes` are canonical under Run C / Q-10; `arm.unpredictable_count` is
excluded diagnostic state. Cross-quota driver projections are schema data,
including the booked NULL-batch attempt and remaining work/scanout budgets.

Run D R-2: the comparator test additionally pins the complete reviewed lists
in `tests/rpc_lockstep_reviewed_policy.json`, outside the schema. This includes
each conditional/permanent/mode exclusion's fields, rule and citation, the
cross-quota projection expressions, and A-33's stdout rule. Class validation
alone cannot approve an edit to those lists: a schema-only policy edit fails
the independent test. A new adjudicated rule requires reviewing both files;
the test never regenerates the policy fixture. Controls self-approve each of
QA's four exclusions in the schema and verify that the independent pin rejects
them even though class validation accepts them.

Event counterexamples perturb one field at a time, keeping time equal except
for the timestamp test. RAM/MMIO cross-order and direction are separate tests,
as are the agent, address, size and value. Additional malformed-stream controls
cover both-input stale metadata, every header identity, EOF accounting,
chronology, fatal termination/reason, every compact group, record pointers,
baseline duplicates and asymmetric field inclusion.

G-6 and G-10/E11 sensitivity is labelled **detected by differential only**
unless an independent expectation fails. ROMCR-after-drain additionally runs
the independent interval oracle (AC-M2-burst-0). Idle-no-drain's actual channel
is a terminal fault, not an independently observed stale read. Port-to-1a-0
annotations refer to changed anchors within its diff hunks, not whole files.
Timeouts follow W4: whole minutes, at least twice measured worst costs; the
one-minute minimum explains ratios above ten for sub-six-second tests.


Re-grounding command examples after building the current plan core:

```sh
python3 -B tests/rpc_lockstep_inventory.py /private/tmp/arcdroid-m2a-host --apple --probe-compiler /usr/bin/cc --write-inventory /private/tmp/arcdroid-m2a-host/rpc-lockstep-inventory.json
python3 -B tests/rpc_lockstep_inventory.py /private/tmp/arcdroid-m2a-run-c-mingw --nm x86_64-w64-mingw32-nm --probe-compiler x86_64-w64-mingw32-gcc --write-inventory /private/tmp/arcdroid-m2a-run-c-mingw/rpc-lockstep-inventory.json
```

On ELF/Linux, omit `--apple` and use the build's native `nm` and C compiler.
The normal CTest invokes the same classifier/export tool and compiled controls.
It discovers all current `.c.o` / `.c.obj` files in the plan-core RPC object
target, including a newly built source TU; it does not iterate a frozen source
hash manifest. Its inline probe tests real data/common symbols and a function
local, in addition to format fixtures for ELF/COFF/Mach-O section/label parsing.
No compiler suffix is stripped from an unknown variable stem. The generated
inventory belongs in the build tree, not in the repository.

`tests/rpc_lockstep_port_1a0.json` records the old-side changed-line ranges
between `43c804a` and the reviewed 1a-0 head `38eae49`; it is porting metadata,
not a semantic-baseline or engine-source hash pin. Pure insertion hunks do not
replace an unchanged original anchor line. The per-variant output records the
changed line, intersecting hunk data and port flag, without needing that
unmerged revision in CI's shallow checkout.


The projection section uses reviewed `input`, `add` and `sub` expression trees,
with P0/P1/P2/P3 and normal/NULL-batch paths explicit. The comparator validates
the expression grammar and offers reference arithmetic; known-answer tests
exercise 100+7 completed attempts, the final completion, booked NULL work,
event limits and pixel/field budgets. M2b must provide the actual input values
and serialise the projections. M2a does not claim to observe driver locals.
The ELF parser also passed a locally cross-compiled Clang ELF probe; Linux GCC
plan-core inventory and runtime evidence remain CI's decision.
