# RPC observation primitives (R3c-1, D-174/D-175)

The adapter is owned by the single RPC session. Production has an empty
qualification table: firmware recognition is never observation authority.
Observer slots are boot, launch, error and disc; sub-capabilities are observation,
answer completion and media replacement. Records key catalogue entry, CPU pair,
profile and sub-capability, and carry an explicit disabled reason. Test profiles
and trace admission are compiled only with ARCDROID_TEST_FAULTS.

## CPU boundaries and records

One inactive gate at batch entry precedes origin capture, classification,
register copies and time materialisation. The armed SWI edge also gates before work.
There is no allocation, inspection or translation walk on the inactive path.
An active instruction context retains its original VA and the backing pointer
returned by its actual fetch. Backing is reported as allocation kind and byte
offset, including aliases; never derived from an exception-rewritten PC.
Records carry epoch, monotonically ordered event and instruction identities,
scheduler ns (microseconds are floor(ns/1000)), kind, full 24-bit SWI including X,
full old/new modes, privilege, 26/32 address width, NZCV, R0–R3, filter-requested
R4–R12, SP and LR. Unrequested register lanes are zero with a validity mask.

Continuation runs after successful fetch, before condition and body. A prefetch
abort produces no continuation; a condition-failed instruction does. Eight
watches are bounded by owner, generation, epoch and instruction-attempt deadline.
Attempt bounds are caller-selected, at most one billion active instruction
attempts; trace watches use that upper bound and mark expiry as lost evidence.
Only user-mode returns from user-mode SWIs are admitted (D-176). The first
eligible fetched instruction in the requested user mode ends a watch with a
match (VA and SP equal) or disqualification; there is no accidental later match.
User watches wait for a completed status-restoring PC write before
considering a fetch a return; handler entry in that same mode does not count.
Controlled completion also releases its same-mode watch for the next fetch.
A watch can be armed only from a synchronous SWI event, so that event's fetch
cannot consume it. Replacing an owner's watch cancels it; full capacity refuses
admission. Cancellation and expiry are synchronous. A watch carries a distinct
identity. Reset, stop, failed start and fault retire every watch and answer.

SWI entry is in opSWI after condition success, before exception(). Mode events
are checked after the completed body, before the ordinary event check, then
after that check (and on the prefetch-abort path). This retains distinct SWI
entry and interleaved interrupt transitions. No hook is placed in updatemode().
Banked SP/LR and full mode are settled; raw R15 is transitional until the
interpreter's final +4. Event VA/backing always describe the originating attempt.
The adapter neither flushes integer batching nor services current requests.

## Inspection and completion

The inspector takes explicit effective privilege and writes only to caller-owned
host output buffers. It reads aligned little-endian
words atomically, bytes, bounded buffers and NUL/CR terminated strings (maximum
256 bytes including terminator). Unaligned words and overflowing ranges refuse.
Buffers return their copied prefix and refusal status; strings return a copied,
NUL-terminated prefix, with success only after NUL/CR, truncation at the bound,
and refusal on inaccessible data. No guest pointer escapes.

With MMU enabled, an existing tagged TLB entry wins, including stale mappings.
Current DACR and AP/S/R apply at the requested privilege. A miss uses local
section/coarse-table large/small-page descriptors; descriptor and target reads
use only allocated ROM/RAM/VRAM, never device dispatch or unbacked apertures.
No installs, FAR/FSR/attribute writes, timing calls, cache changes or scheduler
changes occur. The execution translator is never called. Test-only bounded
snapshots cover private CP15/TLB and timing state, alongside CPU/device/memory.

Completion is permitted only synchronously during the matching SWI callback,
with its event identity, epoch and owner generation. The filter approves a
subset of R0–R12; SP/LR/PC/mode/banks/SPSR are never writable. Only C/V are
accepted as flag results, leaving N/Z and all other state intact. Completing
returns directly from opSWI: no exception, branch charge or added PC advance.
The interpreter's raw +4 and event check still run. Pending answers are copied
intent tagged with generation/epoch and may be consumed only by a later eligible
SWI; they cannot authorize an old event.

## Lifecycle and deferred host work

Epoch is monotonic across start, reset, contained fault, failed start and stop;
reset does not change session generation and rewinds scheduler time. The idle
service reads FDC internal state per drive. Held/resetting, parameters (including unknown target), execution/results and
FIFO/DMA service make the shared controller busy for both drives. Background
seek/recalibrate activity and outstanding causes make their affected drive busy. IRQ acknowledgement alone does
not clear a seek cause. Motor/index/head-unload alone are idle. Unknown command target makes both drives busy; a background seek on the other
drive is isolated after the shared command engine returns IDLE.

Replacing a host handler cancels its owner generation before replacing borrowed
callback state. Sixteen copied host requests hold owner generation, epoch, media generation,
observation ns and at most 256 payload bytes. Only frame return drains them,
after the fault boundary has returned. Validate before resolution and again
before mutation, including owner cancellation, epoch, current media and caller
eligibility. Overflow cancels only the overflowing owner's generation and its
queued actions. Recognition and cancellation remain synchronous; handlers are
host-only and never execute the guest. RPC protected execution admission refuses
entries while draining, and nested drains return immediately. The queue never creates an early yield.

## Qualification trace

Test plans may select numeric observation trace, with a separate explicit private
path opt-in. The bounded 65536-record buffer has its own loss counter and write
failure marker. Overflow emits a gap and marks evidence incomplete; writing
occurs only outside protected execution. A failed sink marks evidence incomplete
without cancelling recognisers or changing execution. The plan client retains
final sink/close failure status through unload, and the runner prints an explicit
single exit summary; qualification must check both JSONL and runner diagnostics. Lifecycle discards pending
records while preserving an incomplete marker when evidence was lost.

Schema version 2 records event/instruction/epoch/ns/kind, original VA/backing,
old/new modes, NZCV, allowlisted registers, SP/LR, watch identity and inspection
status. The explicit SWI allowlist includes OS_Byte, OS_Word, OS_CLI, OS_File,
OS_Find, OS_UpCall and the Wimp launch/poll/report/close calls. OS_Word 21 may
record only its first reason byte; continuation errors and Wimp_ReportError
may record only the error number. Trace scalar/path reads refuse ROM backing,
even though the general inspector permits it. No opcodes, ROM bytes, message bodies or unrestricted blocks are recorded.
Paths are copied only for explicitly enabled private traces on allowlisted path
calls; CI paths are authored. Unknown SWIs have no register payload. Mode events
have no arbitrary register payload. The schema and limits are tested.

## Translator equivalence

SWI callbacks are P1 helpers; controlled completion leaves through ordinary
P2/P3, never post-fetch resume. Successful-fetch continuation is a P1 barrier
before condition, including condition failure, excluding prefetch abort. Completed
body and event-check mode transitions are distinct barriers with stable banks
and transitional R15. Observation requires the current issue timestamp without
batch flush or early memory service. Watches, pending answer intent, generation,
epoch and copied host work join canonical equivalence; traces use a separate
loss domain. Inactive and unmatched/trace-only operation preserve the pinned
30c0bab projection (guest outputs, numeric execution traces, audio counts and
persisted bytes; host duration/path and new diagnostics excluded). Quotas 1, 7
and 200 and integer batching are included; measurement mode is not used to
prove batching parity.

## Run B: inactive loop and usable qualification traces

The batch entry selects two compile-time instantiations of the same interpreter
body. The unarmed instantiation contains no observer loads, branches or hooks,
including SWI and status-restoring PC helpers. Selection never changes batch
length, memory service or flush boundaries. The armed instantiation retains the
original successful-fetch and completed-transition hooks; disarming during a
callback is still checked there. Arming occurs outside execution or from an
already active callback, so it is visible at the next batch admission.

Default trace selection is SWI only. `observation_classes` accepts comma-separated
`swi`, `mode`, `continuation`; `observation_swis` accepts at most 64 distinct
24-bit decimal or 0x-prefixed numbers (X forms are distinct). Selection precedes
trace register copies and inspection. Continuation tracing creates bounded
watches only for selected SWIs. The test-only buffer holds 65536 records, covering
65536 selected events between frame drains: 256 times the former capacity,
and above the private boot's reported 120 SWIs when the earlier trace overflowed. This is a bounded
qualification budget, not a guarantee for arbitrary guests. A test-only
`observation_capacity` plan key can lower it to exercise overflow.

An overflow accumulates a pending gap containing the count, first and last lost
scheduler timestamps, and first lost event identity/epoch. The next available
slot emits that gap before any subsequent selected event; frame drains also
emit a pending gap after retained records. Recording resumes after a drain.
Gap records use schema 2 and kind 8. Every loss marks the entire trace incomplete;
write failures and lifecycle discards remain incomplete. Hooks never block or
perform host I/O. The runner reports cumulative attempts/loss/failure once at
exit, including close failures. Trace selection does not alter guest execution,
observer watches, answers or host work.

## Run C: D-176 eligibility and identity isolation

A continuation watch is admitted only synchronously in a user-mode SWI callback,
and only for a user-mode return (full mode 0 or 16). Privileged SWIs and
privileged return modes are refused. Other call nesting is outside R3c-1.
Every admitted watch waits for a completed status-restoring PC write into its
requested user mode, or controlled completion, before considering a successful
fetch. The test trace uses the same admission and return rule, including LDM^;
an MSR/TEQP plus non-S PC write alone does not release either watch.

Trace event, instruction-attempt and watch identities have separate counters.
Trace-only captures never increment observer order or watch counters, and trace
watches use trace attempt deadlines. A trace record has its own ordered identity
even when it copies an observer event. Turning trace on, changing selection or
losing trace records cannot change any observer-visible identity.
