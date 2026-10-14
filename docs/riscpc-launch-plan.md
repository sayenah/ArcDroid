# RiscPC launch host contract — R3c-3b part 1

This note fixes the host contract before implementation, under W2, D-184 and
D-185. It adds no shipping route. The revision-2 hard-disc gate, CMOS,
Arculator, guest code and observer profiles are unchanged. Part 2 must reconcile
this contract with the reviewed R3c-3a run B before integration.

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

One session owner initialises a nonzero, process-unique session ID and nonzero
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
the future record-publication owner, and is mandatory integration coverage.
Damaged, unsupported, pending, unpublished, population and read-only views also
refuse. An offline modification that violates immutable-file/exclusive-owner
rules is detected by existing chunk validation on use; this API does not promise
to detect arbitrary external file tampering without reading those files.
Namespace distinguishes stores; moving/copying a store to another save root
invalidates records. No store-envelope revision or new persistent file is added.

## RPC boot records

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

## Part-2 placeholders — unresolved by this note

- **Controller lifetime:** R3c-3a run B determines Wimp registration, slot,
  code/plan/status ownership and shutdown. No BASIC-subroutine assumption.
- **Transport:** run B determines collision-free transport, status-block
  validation and the synchronous consume acknowledgement. No authored UpCalls.
- **Release-failure storage:** run B determines an independent safe lifetime.
- **Observers/cost:** qualified pending ReadLine context, exact-command outcome
  profiles, short-watch limits and numeric active-phase cost ceilings are fixed
  before part-2 code. No production cell or long-lived watch is added here.
- **Integration:** reset barrier/no-op replacement, duplicate guest latch,
  frontend input/swap policy, preparation ownership and platform/private legs
  are checked after both prerequisite branches are rebased. Identity refusal
  must discard persisted observations even if a later clean handle has an
  identical identity; no caller may retry that old record.
