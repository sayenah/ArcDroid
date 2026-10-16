<!-- SPDX-License-Identifier: GPL-2.0-only -->

# RPC observation primitives (R3c-1, D-174/D-175)

The adapter is owned by the single RPC session. Production enables only individually qualified firmware/CPU/profile cells;
firmware recognition alone is never observation authority.
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

## R3c-2 observer contract (D-177)

RPC revision 1 is a parameterised profile of the shared pure observers. The
3.1 ordered error sequence remains the Arculator default. RPC instead opens on
a user26/user32 ReportError and owns a same-width user return watch. Old-style
synchronous flag forms are explicitly 0x00 through 0x1f; each form has separate
button/default and poll qualification. Bit-8 forms are observation-only; bits
5/6 (immediate return/close control) are excluded. Unknown flags still block
replacement as an open user report. No serious-error classification is inferred
from an error number. A qualification must prove the effective buttons, default
and absence of competing reads for that form; otherwise manual interaction is
the only hint. The initial test candidate uses the ordinary low-five-bit button
interpretation and poll R1=R2=0, pending Fable's re-recording and adjudication.

Learning and consumption require the profile's exact full SVC mode (3 or 19),
ROM fetched backing, OS_Byte 129 and exact R1/R2. The first eligible poll learns
VA/SP; it cannot consume. Later eligible polls must match both. IRQ activity is
neutral. Nested reports retire the parent and pending answer; no parent resumes.
The return, disqualifying return, watch admission refusal, expiry, cancellation,
replacement, reset, stop or fault ends eligibility synchronously. Watch loss
leaves modal state unknown until a new bounded report, a new epoch, or
a user-mode Wimp_Poll/Wimp_PollIdle entry with no pending user report. The
asynchronous-control uncertainty described below additionally excludes poll recovery. Observation and
completion are separate capabilities; completion also needs form qualification.

The frontend snapshot distinguishes recognised open, answer available and modal
unknown. Unknown forms are modal-unknown while their user return watch is pending;
immediate-return/close-control forms retain uncertainty beyond that watch as
described below. Accepted synchronous forms alone claim recognised-open. An
already queued or consumed answer is no longer available. Only answer availability owns pad A/B, with the existing neutral and
release gates. Observation-only generations retain normal keyboard, mouse, OSK
and pad mapping and show a manual hint. Disc mutation requires a known nonmodal
state, including before poll learning and for unanswerable user reports. Lack of
an error observer never proves nonmodal state.

Disc A admits only ROM SVC XOS_UpCall reason 1/2, filesystem 8, device R3=0xffffffff (Run D correction),
with a terminated bare printable medium name of 1..10 bytes (12-byte inspection
bound). Disc B admits only user OS_File/OS_Find operations already recognised
by the shared logic, a validated ADFS::name.$ path for the inserted floppy,
and a same-width user return watch with the existing miss semantics. A and B
have separate observe/replace cells. Shared two-miss identity history retains
its 120-second window. Copied deferred records contain signal, canonical name
or object, operation, continuation/SP/watch, time, epoch, owner and both drive
generations. A valid signal A synchronously cancels queued B work. Resolution restores this copied identity, never the latest mutable
request. Cancellation is synchronous; validation precedes resolution and
mutation. Overflow retires only the disc generation.

Helper and IDE names come from their current FileCore disc records; missing,
malformed, conflicting or unreadable identity disables automatic replacement.
Identity queries before resolution and commit read only cached host memory.
The helper name is derived at drive-1 mount/eject and reset; writable helper
media stays unknown because its identity can change in the guest. IDE attach,
reopen and reset derive the boot, primary-map and backup-map names once.
An ArcDroid-owned IDE callback bridge invalidates the IDE identity before any
write overlapping those record sectors, including failed writes, and on backend
failure. No file or store read occurs in a frame-boundary identity query.
After a successful write, bounded inspection of its full sector payload can
restore the previously known identity only if every affected name and boot
identity still agrees with the cache. Ordinary map updates therefore preserve
replacement availability. Changed names and unsuccessful writes stay unknown.
Invalidated, unreadable, malformed or conflicting identities keep replacement
manual until a lifecycle refresh validates all records again; a guest rename
can never reuse the previously cached name to resolve content media.
ADFS::1/4, explicit device numbers, helper names and hard-disc names are refused
by the default RPC profile. The unspecified device form is admitted (Run D).
Replacement is drive 0 only, current generation, controller idle, unique content
candidate, successful preflight and known nonmodal state; FileCore retries the
UpCall without answering its box. Manual Disk Control remains independent.

Launch outcome uses the production command arm/snapshot API after attachment,
inside an explicit launch phase. Its profile sets the 600-second horizon and
ROM provenance requirements for exit/close/poll; user-mode SWIs only. It reports
termination, never installed-product or gameplay success. Boot uses the same
phase arm API and profile parameters: 40-second horizon, 2-second prompt wait,
user-mode ROM ReadLine, R0 below 0x8000. Plan/harness keys schedule these arms at
a requested presentation frame; no machine-startup arm. Both stay test-armed
until R3c-3; flag-free harnesses may call production arms but gain no override.

The session hashes the actual owned image incrementally in the existing 32 KiB
copy units; binding checks that digest against the verified catalogue request.
The session binds that fingerprint, actual CPU and profile
revision only after attachment. Run F adopts D-181's cell record in
`docs/riscpc-observer-qualification.md`: error observation/answer and both disc
signals' observation/replacement on exactly its seven pairs at revision 1.
Boot and launch remain disabled with the record's exact reason; enabled cells
carry that record's applicable leg references. Unknown/authored firmware has no authority.
The test-fault build alone offers an explicitly labelled per-cell override and
candidate profile parameters. Promotion is a later run changing exactly Fable's
adjudicated cells with evidence references, followed by override-free QA.

Run F's production error profile admits ROM SVC26/SVC32 polls with R1/R2 zero,
and answers only forms 3 (OK/Cancel, default OK) and 14 (Cancel, default Cancel).
The record exercised error number zero only and excludes serious-error
transformations: a nonzero or unreadable error number remains observable but
manual. The test override retains its candidate forms and parameters for further
qualification. Signal A stays unspecified-device only; signal B is unchanged.
Disabled/idle boundary diagnostics use `action=inactive`, preserving the gate
and reason without announcing a refusal notice.

Boundary ownership: the RPC frontend polls input/arbitrates once, then runs the
protected frame. Engine frame return advances observer horizons and drains copied
host work (including B resolution and commit callbacks) outside the fault boundary.
The frontend then drains trace, performs A resolution/hints/commit, and runs
maintenance once. The next call polls input once for its next guest frame. Preparation owns its own diagnostics
drain after each isolated advance and before stop, with no hints, answers or
replacement during generation. Requested-session binding/activation occurs
only after attachment. A diagnostic sink is supplied by the client so generation
records drain before retirement; otherwise loss is explicitly retained.

Canonical translator state includes profile/identity/capabilities, phase arms,
all four pure states, modal knowledge, learned poll identity, return-watch loss,
pending intent, deferred copied observations and media generation. Unavailable,
unmatched and trace-only operation retain integer batching and quotas 1/7/200.
Enabling any observer selects R3c-1's instrumented interpreter; Fable's Odin3
R-H comparison must enable candidate observers and arm launch/boot, not merely
trace. No firmware addresses or bytes are part of this profile.

Hardware reset preserves the verified binding/profile and host callback leases,
then rebinds in the new epoch with all watches, intent, histories and phase arms
empty. A test override remains session-scoped across reset; it is never promoted.
Stop/failed start/unload clear borrowed helper/IDE metadata before a later session
can refresh the identity epoch. A helper-free reopen cannot inherit a helper name
or pointer from its predecessor.

## Run C qualification tools and boundary diagnostics

RPC error hints are keyed by generation and displayed eligibility (manual or
answer available). A transition within a generation replaces the hint once;
unchanged eligibility does not repeat it. Watch loss after a displayed hint
changes it to a manual hint that states modal uncertainty, without claiming the
box remains recognised open. The Arculator generation rule stays
unchanged. Queued/consumed answers are unavailable in the production snapshot.

The non-shipping runner supplies the libretro log callback on stderr and accepts
`--pad-at FRAME MASK` events: decimal 16-bit RetroPad button masks for port 0,
latched until the next event (zero releases all buttons). Frame zero is the first
requested presentation frame after preparation; preparation always sees neutral
pads. Events use the real libretro input callback, including mask queries. The
plan's keyboard/mouse script remains independent and unchanged.

RPC disc diagnostics copy bounded signal outcomes during guest observation and
combine them with cached identity, media-generation and modal eligibility at the
host boundary. No logger, filesystem or IDE read runs in the observer callback.
Boundary logs include signal refusal, identity exclusion, candidate resolution,
hints, controller busy and commit results. Changes print immediately; identical
busy retries print at most once per two virtual seconds; all other unchanged
states stay quiet (Run D). Missing signals print an
explicit waiting state. These diagnostics neither relax predicates nor enable
production cells. Windows test-only DLL exports explicitly include every seam
loaded by the observer frontend harness. The orchestration mutant idle profile
uses the authored SA-110 factory guest's existing idle instruction after its
last CMOS write, retaining all mutants and all assertions.

## Run D — corrected signal A device evidence and decision logging

Fable's ruling (to be recorded as D-179) corrects the S63 evidence basis: R3
was absent from that trace's allowlist, so its displayed zero was an artefact.
R3-capable traces on 3.50 ARM610 and 3.71 SA-110 show ROM SVC XOS_UpCall
R0=2, R1=8, R3=0xffffffff during both a task catalogue miss and the real disc
install request. The RPC profile therefore admits the unspecified-device form,
not explicit drive 0. It still replaces only content media in drive 0, with
all existing identity, generation, preflight, controller and modal gates.

The shared pure profile's admitted device forms become a two-bit parameter:
bit 0 is explicit drive 0; bit 1 is unspecified (R3=-1). The NULL Arculator
profile keeps its existing acceptance of both forms. RPC's default is bit 1
only. Its test-only plan key is `observer_disc_devices=MASK`, decimal or `0x`
hexadecimal, 0..3 inclusive: 0 admits neither, 1 tests drive 0 alone, 2 tests
unspecified alone (the default), and 3 tests both. Drives 1 and 4 and all other
R3 values remain refused under every mask. This override never enables cells,
is applied after the cell override, cancels prior disc observations if it
changes the mask, survives reset, and is cleared on a new session. The override
label includes the selected device mask. Production cells remain disabled.

Disc decision logs print when the decision changes. Only an active `busy`
retry may repeat, at most once per two virtual seconds. Disabled, waiting,
resolved, hinted and completed decisions never repeat unchanged. Clock rewind
restarts the busy-repeat interval without emitting an unchanged disabled log.

## Run E — observed interpreter cost (Fable ruling, to be D-180)

The inactive interpreter instantiation remains unchanged. An additional
instantiation serves active observers with no live continuation watch, no mode
subscriber and no trace. Attempt prefixes are accounted before SWI admission
and at batch return, so a newly armed watch's deadline includes preceding cheap
instructions. The first SWI that changes the observation requirements ends that
batch after its normal instruction/event work. The next fetch selects the
checked path; no instruction or event is skipped or replayed.

Real sessions already count every attempt in memory timing. Batch admission
records that counter once; normal return closes it, and existing epoch retirement
reconciles an unfinished prefix if a contained fault jumps past batch return.
This adds no per-instruction counter and no unwind mechanism. The faulting
attempt remains counted, and resource retirement still follows observer retirement.

The cheap path builds context only at a condition-passed SWI, using its current
PC, mode, instruction-start clock and actual successful fetch pointer. Mode
changes without subscribers preserve observer ordering without constructing
records. The checked path retains per-attempt watch checks and trace semantics,
with a scalar origin and lazy backing/register/record construction at events.
Watch/filter/trace changes maintain the admission gate outside the common hot
path. Tests must cover a long cheap prefix before watch admission, exact expiry,
condition-failed continuation and trace-independent event identities. Paired
measurement uses the same authored hot workload for inactive, active without a
watch and active with a real user-report watch held; no production cell is
promoted by this cost work.

## Run G — QA adjudication and error-number qualification

An open report retains its original continuation identity and owner generation.
At each valid ROM OS_Byte 129 read, the shared contextual profile validates
R1/R2, mode, learned site and SVC stack independently of pending answer intent.
Learning and subsequent matching polls renew the existing return watch's
instruction deadline by the bounded watch budget. Renewal never creates a new
watch, changes its return PC/SP/mode, or revives a lost watch. Nonmatching reads
do not renew it; cessation of polling still expires it. This work occurs only
at observed SWIs and adds nothing to the no-watch instruction path.

After loss, a user-mode Wimp_Poll or Wimp_PollIdle entry clears modal-unknown
only when there is no current synchronous report watch or recognised open
report. Privileged polls and polls during a report cannot establish non-modal
state. Until that positive evidence, both disc replacement gates stay closed.
Disc losses are accepted only for the current disc watch ID. Before replacing
a watch, policy removes the old ID so its synchronous loss cannot disarm the
new request. RPC reset clears the frontend's error-hint generation, hint state
and seen-generation keys; Arculator reset remains unchanged.

The contextual error profile gains an answerable number class: `zero` or
`ordinary` (bit 31 clear). Production remains `zero`. Both classes refuse
serious numbers (bit 31 set) and unreadable error blocks. This is an authored
predicate, not a firmware classification table. The test-only plan key is
`observer_error_numbers=zero|ordinary`, case-sensitive, default `zero`; it is
applied only with `observer_cells`, logged in the TEST OVERRIDE label, preserved
on RPC reset and cleared on a new load. It changes neither admitted forms nor
effective buttons. Pure Arculator logic with its NULL profile is unaffected.

Digest evidence compares the actual copied-image session digest with Python's
independently computed SHA-1 and CRC32 of the authored input image. Tests use
varied bytes across all copy units and require the size, SHA-1 and CRC32 to
agree. No catalogue injection or firmware material is needed.

### Run H: adjudicated queue, hint and production refinements

D-187 promotes the ordinary error-number class (bit 31 clear) on the same seven
revision-1 tuples. Serious numbers remain observation-only. Cell references
include the graphical and ordinary-number legs in the adopted record; boot and
launch remain disabled. Qualification tests compare table values and evidence
identifiers, rather than narrative wording.

RPC signal B retains a copied triggered observation after BUSY, including its
continuation, stack, candidate, epoch and both media generations. At later host
boundaries it retries only while that identity, generation and trigger remain
valid. A completed commit or hint ends retrying. FOUND classification uses a
copy of the current arm, leaving live history and the trigger latch untouched.
Its retirement and lifecycle observations use the same ordered
host queue; lifecycle events resolve preceding misses before clearing history,
while preserving a later call still live across the boundary. Signal A still supersedes queued B work synchronously. Queue
overflow retires the queue generation and clears identity history, including
eviction order and observation counts. A completed trigger's latch survives.
Before cancellation drops copied work, RPC records
its retirement barriers: a lost lifecycle marker (including the overflowing
item itself) also clears the latch synchronously. If overflow drops a pending
BUSY retry, the shared retry operation lifts that latch so later repeated misses
can trigger again. FOUND never drops earlier queued misses; their observations
and triggers precede FOUND, including when they concern the same object. This uses
bounded copied-data inspection, never guest path resolution. No resolver or
frontend callback runs inside guest execution.
First-observation logging reads the copied callback identity. FOUND classification
also retires an existing BUSY retry for that canonical object synchronously:
presentation runs retries before the drain. This cancellation does not advance
FOUND's queued history/latch effects or retire retries for other objects.

After every overflow, signal B is "possibly latched": RPC suppresses B
observation callbacks, new triggers, hints and replacement eligibility. Lifting
a dropped BUSY retry's old latch does not bypass suppression. Recovery requires
a global history/latch clear: an applied lifecycle marker, an admitted signal A,
`disc_clear` or media reconfiguration, reset, or a new epoch. FOUND only retires
its own object and never releases suppression. A lifecycle marker that itself overflows is applied
synchronously; an earlier lost marker clears old history/latch, while later
lost items remain uncertain. Signal A's existing global history clear,
supersession and replacement route are unchanged.

Quiet recovery is also safe strictly beyond 120 seconds of guest time without
any signal-B miss. Every miss, including an unresolved one, updates the source
timestamp. Non-miss items, including an overflowing FOUND, do not refresh it.
Each copied work item retains its predecessor's timestamp alongside
its own, so recovery occurs in guest order even when later misses are already
queued. Recovery is checked before candidate resolution; suppressed misses do
not rebuild partial history. At that point every shared identity and possible
latch is beyond the shared observer's inclusive 120-second window: the first
fresh miss counts once, and subsequent overflow-free triggers match the shared
observer. Clock regression cannot satisfy quiet expiry. Tests retain the strict
same-event subset assertion over overflow frames, and exact equality on
ordinary, globally recovered and quiet-recovered stretches. All work occurs on
observed events or the host boundary, never per instruction.

Synchronous bit-8 forms may carry category/high bits, but bits 5 and 6 still
exclude them. They receive a recognised-open manual hint and no answers. A
pending or consumed answer retains its answerable hint until the generation
ends; snapshot answer availability still means a fresh answer can be queued.
RPC reset retires hints, answers and disc watches before execution resumes and
clears frontend swap ownership, previous index, time and pending notices.
Helper paths are bounded before mount, so rejection leaves the drive intact.

The cost matrix adds an authored user-mode SWI-density workload. Production
subscriptions may select explicit normalised SWI numbers before event capture;
trace selection remains independent, and watch deadlines still count every
attempt. All four shared pure observers belong to the RPC host-service and
termination guard surface.

The SWI-number interest union is rebuilt only when subscriptions change. A cached
three-word set rejects common OS and Wimp numbers before per-owner matching;
other number ranges and mask-only subscriptions retain the generic path. Trace
subscriptions independently bypass this rejection. The optional bounded list
is host-owned for the lifetime of its filter and does not replace raw SWI masks.

### Run I: immediate-return/close-control modal state

The [RISC OS PRM, Wimp_ReportError](https://www.riscos.com/support/developers/prm/wimp.html)
confirms that flag bit 5 may return without closing the error window; bit 6
selects a button and closes it. It requires a close-control call before the next
Wimp_Poll when using immediate returns. There is no contradiction with the
adjudicated contract. No firmware classification or content is involved.

A user report with either control bit sets a session-owned uncertainty latch
from entry. It offers no answer and only the manual state-unknown hint. A
matching immediate return, expiry, cancellation, privileged nested report or
user Wimp_Poll/PollIdle does not clear this latch. Its bounded call watch ends
normally, so an open asynchronous window adds no launch-long instruction watch.
Only the matching successful return of a user close-control call, completion of
a later recognised synchronous report, reset or a new bound epoch clears it.
A failed/disqualifying close return is not evidence. A synchronous report used
for recovery remains manual until it completes; unrecognised forms cannot
supply recovery. Privileged reports keep their existing invalidation semantics
and do not become new user-report observations.

After a bit-5 report, automatic disc replacement therefore stays manual until
positive closure, reset or a new epoch. Manual Disk Control still works.

Ordinary watch-loss recovery through user Wimp_Poll/PollIdle remains unchanged
when this control-form latch is absent. Watch-renewal failure cancels its watch
before retiring policy eligibility. Routine disc reasons use neutral names
(`path-other`, `outcome-not-miss`) on both shipping decision-log fields.

## R3c-3a Run B: authored scalar status transport

The test trace passively recognizes XOS_Byte/OS_Byte's documented machine-type
query (R0=0, R1=1, R2=0), with R4=ARCS and R5=64. R3 points to the aligned,
bounded ARCS block specified in `riscpc-launch-gate.md`; R6 must match its role.
Only after all sixteen RAM/VRAM words validate does schema 2 set `status_valid`
and copy role, event, sequence, 64-bit token/epoch and two scalar details into
`status`. Invalid blocks leave those fields zero. No status pointer, block bytes,
ROM data or path is emitted. Registers R3..R12 remain zero in these trace rows.
The kernel and ByteV chain still execute their ordinary query unchanged.
Role 0 diagnostics and role 1 reserved attempt events remain distinct; the
trace does not implement an attempt consumer or infer delivery.

The test-only `gate_release_fail=once` plan key injects V on the first exact
UpCallV XOS_Release after its workspace is already inactive. The injector uses
the existing synchronous completion boundary through the engine test interface;
it introduces no fault jump or shipping API. Persistent test plans bind the
recognized firmware's existing qualification table after final attachment,
matching seed plans; low-level production preparation admission is unchanged.

`observation_status=authored` is an optional test trace selector that suppresses
untagged OS_Byte traffic before recording or inspection. Other explicitly
selected SWIs are unchanged. It requires trace admission and accepts no other
value. Qualification uses it to avoid collecting ordinary error-box key polls;
the public fixture verifies identical guest results and identical status data.
