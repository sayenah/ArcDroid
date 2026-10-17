<!-- SPDX-License-Identifier: GPL-2.0-only -->

# RiscPC launch contract — R3c-3b

This note fixes the contract before implementation, under W2, D-184 and D-185.
Part 2 uses the frozen R3c-3a run-B CALL extent and passive ARCS query. Gate
bytes, descriptor ABI, receipt admission and attachment policy do not change.
Ordinary-load routing belongs to R3d. Launch and prompt cells remain disabled
in production pending Fable's qualification and promotion.

## Helper plan 2

The helper plan is independent of the gate descriptor. Unsigned integers are
little endian. Its exact grammar is 64 header bytes followed by 1–64 target
bytes, without a NUL or trailing data:

| Offset | Field |
| --- | --- |
| 0 | `ARCRLPLN` (8 bytes) |
| 8, 12 | version 2, exact total size (u32) |
| 16 | action: 1 selected content boot, 2 application, 3 verified product |
| 20 | flags: bit 0 optional RAMFS; other bits zero |
| 24 | content/media-state SHA-256 (32 bytes) |
| 56, 60 | target length (u32), zero reserved u32 |
| 64 | exact absolute target |

Content boot explicitly selects the unique discovered root `!Boot` and starts
`%Run ADFS::0.$.!Boot`. It does not alter the media boot option. Application
and product actions start `%Filer_Run <target>`. The exact generated command,
including the wrapper if any, is the observation subject. This is not a claim
that a wrapper's children terminated. Targets are `ADFS::0.$.` (content) or
`ADFS::4.$.` (product), followed by a safe application leaf, optionally below
one safe parent, as supported by existing discovery. Paths use the literal
representability rules of `core/launch_disc.c`; diagnostic escaping uses
`arc_launch_escape_name`. No untrusted command text is accepted.

The builder consumes `ArcLaunchDiscovery` from `arc_launch_discover`. Automatic
application selection uses `arc_launch_recovery_decide`; ambiguous/incomplete
discovery needs an explicit candidate index. Resources, ambiguous !Run objects,
unsafe/unaddressable parents and malformed counts refuse. Products additionally
need matching captured and current store identities around post-install
structural discovery; installer return and a creation receipt alone are not
product proofs. The caller must discover the current product tree using bounded,
guest-tolerant FileCore validation (part 2). Plan validation is independently
repeated at decode/command generation. Unknown versions/actions/flags, wrong
drive, unsafe components, embedded NUL, bad lengths and trailing bytes refuse.
Optional RAMFS is only a request; part 2 creates it only if absent, after token
consumption. There is no Desktop restart, sibling boot or CMOS operation.

## Delivery and outcome

One session owner initialises a nonzero ID unique within a library load and nonzero
epoch. Within that owner, explicit attempts receive strictly increasing u64
serials, refusing wrap. A token contains session, epoch, serial and SHA-256 of
the canonical plan. Tokens are identity bindings, not security credentials.
Reinitialisation with reused session IDs is forbidden. Every event must match
all token fields. Normalized statuses also carry a nonzero increasing u64 event
ordinal: an identical repeat of the last ordinal is inert, a conflicting repeat
or an older ordinal refuses. This prevents an old waiting marker from reopening
the waiting state after input resumed. Part 2 maps validated source statuses to
these ordinals; this is not a guest transport layout. Unrelated, stale and
phase-ineligible events refuse without mutation.

Delivery progresses `prepared -> consumed -> dispatch attempted -> observed`.
Outcome independently progresses `none -> pending -> returned / terminated /
desktop handoff`; waiting for input is a transient pending-command state.
StartTask returning with a live child remains pending, never terminated.
Consumption synchronously changes prepared to consumed, before RAMFS creation,
Filer operations or dispatch. Its API has no capability-cell argument. A second
consume cannot grant permission; duplicate statuses are inert. Dispatch can be
attempted only once after consumption. A rejected plan/action is terminal with
no effect. Expiry makes delivery/outcome unknown, never failed or retried.
Normal input, prompt-hint dismissal, authorised swaps and successful rollback
do not change the token or cancel observation.

Reset at any phase, failed swap rollback, fault and unload invalidate the epoch
synchronously and interrupt the attempt. Reset closes a resume barrier. The
host may open it only after successful replacement with a no-op helper; failure
leaves it closed and requires stopping the guest. Fault/unload close the session.
Part 1 models these transitions but performs no engine or media calls. Part 2
must replace/detach the backing before any resumed instruction, and provide a
guest-owned latch that refuses duplicate invocation in the same guest boot.
After interruption/expiry, only a new explicit attempt can issue a token.
The guarantee is at-most-once automated execution, not execution across reset.

## Store identity API and acquisition budget

`arc_riscpc_hd_identity` captures identity revision 1 from an exclusively owned,
validated, published, writable store in IDLE, outside any population/checkpoint.
It performs **zero filesystem callbacks, zero disc reads, zero allocation**,
hashes at most **32768 bytes** and uses at most **1024 bytes of local workspace**.
Revalidation reacquires the same identity and compares all 32 bytes. Neither
API reads or hashes a whole disc or walks the live sector index. Tests measure
the I/O and hash counter deltas. Store open/recovery and ordinary chunk reads
retain their existing validation and costs; identity is not a substitute for
lazy chunk integrity validation.

The domain-separated SHA-256 covers the namespace (full accepted store base),
exact accepted manifest including its chunk hashes and receipt, current journal
generation/length/committed sequence and the SHA-256 of its validated stream
(maintained by replay and acknowledged writes), plus the recovery flag. IDLE has
one current journal; retired parent metadata is deliberately excluded. Every
acknowledged guest edit, including product edits, changes the stream/sequence.
Checkpoint changes the accepted root even if logical bytes match: conservative
invalidation is intentional. A clean reopen of the same root/journal matches.

Recovery of torn/partial/unbound journals, resumed checkpoint/retirement, or
candidate cleanup sets a sticky per-handle recovery flag. A recovered handle
cannot supply an identity, even after maintenance completes; a clean subsequent
reopen is required. Identity refusal invalidates the caller's observation cache:
part 2 must discard the associated persisted record before retrying or closing
that handle. A later clean reopen permits a new observation, never restoration
of the discarded one. This matters when recovery discards an uncommitted suffix
and returns to identical committed bytes. There is no persistent recovery nonce
in the existing store envelopes; irreversible record invalidation belongs to
the preparation-owned record publisher, and is mandatory integration coverage.
Damaged, unsupported, pending, unpublished, population and read-only views also
refuse. An offline modification that violates immutable-file/exclusive-owner
rules is detected by existing chunk validation on use; this API does not promise
to detect arbitrary external file tampering without reading those files.
Namespace distinguishes stores; moving/copying a store to another save root
invalidates records. No store-envelope revision or new persistent file is added.

## RPC boot records

Record capture requires the host's bounded `media_identity` callback for the
complete ordered current presentation. The plan frontend supplies it for read-only
sets; writable raw/JFD presentations refuse under the Run B contract below.
The original unused immutable-media boolean is replaced by this verified lease.

Revalidation compares the live 240-byte CMOS payload with the starting payload
captured for this attempt. Both configuration hashing and revalidation exclude
the 16 RTC bytes; configuration hashing uses the remaining 240 bytes plus
canonical model/layout and attachment-policy bytes. Any detected payload
or store identity mismatch permanently suppresses publication for this attempt;
returning later to the same bytes does not restore an observation candidate.

`ARCRBOOT` records use a separate fixed binary schema version 1, semantic
revision 1, length 288 and CRC-32, unrelated to Arculator `ARCBOOT1`.
Bindings are raw SHA-256 values for verified firmware, relevant configuration,
content, media state, current store identity, selected action/target (canonical
plan digest), and helper implementation. Configuration digest must encode the
resolved machine/layout, memory, actual CMOS and write/attachment policies in a
canonical caller-owned representation; actual CPU and plan revision are explicit.
Observation-profile revision is explicit. This pure module never asserts that
an arbitrary firmware hash is recognised or a CPU/profile is qualified: the
part-2 adapter supplies those verified bindings.

Only established command return, command termination or desktop handoff can
create a record from an observed attempt. Pending/StartTask return, timeout,
reset, rejection, fault, disabled observer and waiting-for-input states cannot.
Record creation checks token/plan identity and unchanged captured/current
bindings. Capture occurs after post-install product validation (when applicable)
and immediately before issuing the attempt. Revalidate before accepting a
record, before an effect, and immediately before publication of an observation.
Any store/media/configuration edit or recovery during the attempt suppresses
publication. Installation is verified separately against the post-install
state, and a fresh explicit product attempt uses that state. Parsing enforces
exact size/revisions/enums/reserved bytes/CRC; matching compares canonical keys.
There is no filesystem record publication or observer wiring in part 1.

Product checking during preparation is resumable. A range cache holds at most
1 MiB and 4096 descriptors; a cache miss schedules one aligned range of at most
5120 bytes for the next unit, using one bounded HD read command. Discovery is
replayed against cached ranges without I/O until complete, with identity checked
again before acceptance. This gives product work a separate 8,333,333 ns unit
ceiling inside the existing offline budget. No guest instruction runs during
these units. Verified firmware SHA-256 is retained from VERIFY, never rehashed
as part of helper construction or consumption.

## Controller and transport (part 2, fixed before code)

The helper owns its code, plan, command and status block in the BASIC allocation
for the entire CALL. It preserves R4–R12, SP and the 26/32-bit return convention.
Before Wimp registration it checks plan 2, checks a session-specific guest
variable latch, emits CONSUME synchronously, and sets that latch with
OS_SetVarVal. The latch is a system variable, rather than an application-slot
word: reloading the helper must not recreate permission. A failed latch write
rejects without registration or dispatch. A duplicate invocation returns with
no effects. Variable names contain the complete injected session ID, epoch and serial;
one preparation owns one explicit attempt. A subsequent explicit attempt uses
a fresh explicit token under its session and invalidation epoch. R3d supplies
one nonzero ID per frontend session, unique within a library load, surviving
init/deinit and preparation recovery/Restart. No authoritative token or replay
input survives library unload; an observation record never grants execution.

The host subscribes to role 1 independently of capability cells while delivery
is live. ARCS is an ordinary passive XOS_Byte 0 query, never a completion or an
acknowledgement channel. Consumption is the synchronous host transition before
the query executes. Store revalidation suppresses stale observation records;
it does not change, complete or abort the query. Products are verified before
being offered and before issuing their plan. Unattributable/stale queries are
ignored. The guest latch and reset transaction supply execution exclusion.
There is one attempt per preparation lease; replacing it requires retiring the
old guest, so an unconsumed old helper cannot run beside a new attempt.

The controller calls Wimp_Initialise in USR, emits REGISTERED, then emits
DISPATCH immediately before Wimp_StartTask of the exact generated command.
On successful StartTask return it emits START_RETURNED followed by RETURNED
(R0 zero) or DESKTOP (R0 nonzero: the started task is still alive). Neither
claims that a wrapper's children terminated. The controller closes its exact
task with TASK magic immediately after this result; CLOSED is emitted only
after successful CloseDown, then it returns directly to the gate. Errors after
registration still close that exact task. CloseDown failure exits via OS_Exit
and never returns to BASIC. No controller wait loop or whole-command return
watch is needed. A blocked StartTask can remain live indefinitely; host expiry
does not terminate it or retry. These interpretations follow the documented
[Wimp_StartTask return contract](https://www.riscos.com/support/developers/prm/wimp.html).

Role-1 ARCS uses the frozen 64-byte layout. Token is the nonzero session ID;
wire epoch is `attempt epoch + attempt serial - 1`, refusing overflow. Both
counters are monotonic under one owner, so every newly issued attempt has a
strictly newer wire epoch, and distinct sessions have distinct tokens. The
state machine retains the separate invalidation epoch and serial. Sequence is
increasing u32 (no wrap); the host also
binds the full part-1 token, plan digest, exact command, authored query PC and
status-block location. All reserved words are zero. Events are:

| Number | Meaning / emission |
| --- | --- |
| 1 | CONSUME: validated plan, before latch write or other effects |
| 2 | REGISTERED: successful Wimp_Initialise; detail 0 exact task handle |
| 3 | DISPATCH: immediately before exact-command StartTask |
| 4 | START_RETURNED: successful StartTask return; detail 0 returned handle |
| 5 | RETURNED: command returned, returned handle zero |
| 6 | DESKTOP: live task handoff, returned handle nonzero |
| 7 | REJECTED: validation/latch/registration/dispatch error |
| 8 | CLOSED: successful exact CloseDown |
| 9–15 | Reserved, refused by this implementation |

Identical sequence/event/details repeats are inert; older, conflicting,
out-of-order and stale bindings refuse without state mutation. Role 0 and ACK1
are diagnostics only. WAITING and INPUT_RESUMED are host observations, not
guest events. TERMINATED is reserved for an independently established exact
command termination; this controller does not infer it from other tasks' exits.

## Reset and frontend ownership

RUNNING preparation reset performs the corrected A-3 transaction: interrupt and
close the barrier; detach drive 1; invoke host re-stage-drive-0 callback; install
a preparation-owned no-op only where START would generate one, or reattach an
explicit low-level drive-1 override unchanged under reset-time write policy;
invoke host reset/rebind-input callback and open the barrier only on complete
success. Revision-1 and helper-free sessions gain no helper on Restart. Neither callback
may execute guest instructions. Missing required callbacks or any failed step
stop the engine with a terminal status. The drive-0 callback never touches
drive 1. The second callback resets the engine and rebinds observers and input.
Pending/FAILED reset invalidates and suppresses launch across restart of
VERIFY/recovery; only normal START attaches backing. The preparation barrier
also exists without an attempt. Failure reports the latest CMOS persistence
outcome explicitly. R3d owns reset dispatch, load routing, early pending setter
rejection and release/unload ownership.

RPC input dismisses a prompt hint and preserves delivery/outcome tracking.
Authorised in-set swaps preserve the attempt; failed rollback interrupts it.
External replacement invalidates observation bindings. All disk setters check
RUNNING admission before touching cancellation state. Arculator policy stays
as before. Fault/unload invalidate the attempt; neither issues a fresh token.

## Observation budgets and publication owner

RPC profiles use attempt phase and exact command; legacy Arculator profiles
retain their existing semantics. Pending OS_ReadLine recognition requires a
user-mode, backed RAM buffer, R1 1–255, R2=32 and R3=126, during the exact
blocked command. Its own user-return continuation has a **64,000,000 instruction
attempt** ceiling and a **2 s virtual** ceiling; a return, disqualification,
watch loss or competing launch ends recognition. A still-live read after
**250 ms virtual** offers manual interaction. It never schedules a relaunch.
At most one prompt watch exists. No mode subscription is used. Delivery has a
**120 s virtual** observation horizon; expiry removes launch/prompt filters subject to the narrow pre-consumption exception below.
Only a prepared delivery installs the otherwise cell-independent status filter.
Observation expiry before consumption marks the outcome unknown and disables
outcome capabilities while retaining prepared delivery. A narrow OS_Byte-only
delivery filter remains until the first consumption or session retirement.
A late first entry is consumed synchronously, then marked unknown; it is not a
retry and cannot establish an observation. This needs no return/mode watch and
preserves the frozen query's normal kernel/vector handling. Expiry after
consumption needs no delivery filter. No expiry issues another token.

Tracing-off cost ceilings, measured against identical helper execution with
observation disabled: dispatch and desktop handoff **+10 % p50/p95**; blocked
command and expired observation **+3 % p50/p95**; pending prompt **+100 %
p50/p95** for at most the watch budget above, then **+3 %**. No-launch cost
remains D-180's **+3 % p50/p95** on the pinned Odin3 hot workload. Mac numbers
are development evidence; Odin3 measurements remain Fable's platform evidence.

Preparation owns publication at
`<save>/arcdroid/hd-riscpc/<content SHA-1>/launch.observation`, with a bounded
temporary sibling and rename. Before opening a store it reads the old record
into memory and removes the published file; failure to remove prevents store
open/recovery. Thus recovery cannot erase its own invalidation signal while
leaving an old record available for a later clean open. A temporary file is
never a candidate. Only a clean matching identity may retain an in-memory old
observation; identity refusal permanently discards it. Capture occurs after final
attachment, before requested guest instructions; consumption and publication revalidate all bindings. Publication rechecks
after writing/flushing/closing the temporary file and before rename. Recovery,
guest edits, changed media/configuration or interruption suppress publication.
Publication failure discards the candidate; it never establishes an outcome.

RPC installed-product discovery inspects root applications and one level of
ordinary parent directories even when the boot environment contributes a root
`!Boot` application. It retains shared name, collision, escaping and candidate
classification rules, plus the same 77-candidate and read budgets. Root-only
selection priority remains unchanged for Arculator disc discovery. An explicit
product path still requires current directory identity and allocated `!Run`.

A read return, competing phase, watch loss or watch budget end clears the prompt
hint. A matched return emits INPUT_RESUMED; lost observation emits the distinct
host-only WAIT_UNOBSERVED transition from waiting to command-pending. It claims
no read return, termination or new execution permission. Hint dismissal alone
keeps the pending watch and attempt; recognition can still finish normally.

A failed swap rollback on a bound attempt notifies its preparation owner before
frontend stop. The owner closes its execution barrier, saves the latest CMOS
snapshot, and records a terminal stopped status with the snapshot outcome.
Observer filters and the lease are retired before that owner callback runs.

A preparation still marked RUNNING after an external engine stop fails Reset
closed: no CMOS snapshot is invented from absent engine state. Its terminal
status explicitly reports the latest snapshot unavailable. A later explicit
recovery uses the pending/FAILED branch and retains launch suppression.

## Run B closure contract

D-190's generated-defaults procedure is fixed in
`riscpc-session-preparation.md`. A preserved zero/one-drive record suppresses
this attempt before issuance, reports `unconfigured`, and keeps the default
no-op attachment policy. No gate or descriptor change is required.

Record capture moves to the final attached state, before any requested guest
instruction. The host may provide a synchronous `media_identity` callback.
It must perform no I/O or allocation, inspect at most 32768 metadata bytes,
and identify the current complete ordered presentation, including selected
member/ejection, write policy, native JFD policy and tail presentation. Its fingerprint joins the configuration digest; the record media field retains
the canonical ordered-source SHA-256, so the pure attempt/media binding remains
unchanged. Reacquire and compare the fingerprint at consumption and every publication/reuse check. Media mutations
still suppress publication permanently for the attempt. This replaces the unused run-A immutable-media assertion; an identity callback
is required from every publication owner.

The plan frontend implements this callback only for Disk Writes Off. It hashes
a domain, ordered validated source digests and presentation metadata, alongside
the canonical plan's full ordered-source SHA-256 binding. Stores/overlays must
be hidden; loaded visible/damaged/failed JFD state refuses. Guest write attempts
cannot change the read-only presentations. External edits that violate the
exclusive immutable-file lease are outside the identity guarantee, as for the
HD API. Capture after attachment observes the policy actually mounted.

Writable raw/JFD media refuse publication. The existing ordered-media API
`arc_boot_media_digest` reads each staged raw file and reparses/loads unselected
JFD overlays; its current-state hashes are not maintained at every write.
Those operations exceed the fixed zero-I/O, 32768-byte acquisition budget and
cannot safely run in the synchronous consume callback. A mount generation or
base-image digest omits acknowledged writes and is not an exact substitute.
Supporting writable media requires a separately maintained, complete ordered
fingerprint/lease; this run uses B-2's explicit refusal alternative rather than
asserting such an identity. Tests require positive established publication for
read-only sets, no publication for writable sets or unestablished outcomes,
revalidation before rename, invalidation after media edits, and clean reopen.

Pending-state setter rejection remains scoped to the test plan route. The
ordinary-route admission predicate and its future pending state belong to
R3d-3, per the ownership correction; cancellation after admission remains here.

The run-B scheduling audit found that START's checkpoint wait asked the HD
store's open-only namespace predictor. A waited maintenance tick now reserves
the namespace ceiling whenever the current store is non-IDLE and writable.
This is conservative across every checkpoint substep, including creation,
rename and retirement. IDLE helper construction retains its ordinary class;
product verification retains its separate class. The query reads resident
statistics only, performs no I/O and creates no live maintenance subscription.

Mutation qualification enumerates each new atomic guard and state transition
in a checked inventory, including guards whose removal is equivalent because
an independent bound or invariant still rejects. The runner executes every
non-equivalent mutant against observable tests and rejects surviving mutants.
No compilation failure counts as a killed mutant. Final full-suite and sanitizer
runs use the completed tree; later production/test changes require a fresh full
host run again.
