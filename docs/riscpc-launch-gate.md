# RiscPC revision-2 startup gate (R3c-3a)

This note fixes the interface before implementation. All guest code is
ArcDroid-authored GPL-2.0-only. Ordinary loads and Arculator remain unchanged.
Revision eligibility is provenance, never evidence of delivery.

## Run B freeze decisions (D-185; specified before code)

The returning CALL is a **live controller extent**, not a short wrapper around
StartTask. BASIC owns the application slot and the image/plan/return-summary
allocations until CALL returns. The helper saves the BASIC callee-saved
registers and SP, enters in USR, calls Wimp_Initialise (version 200, TASK
identifier, helper-owned name), and owns that Wimp task throughout its polling
loop. Wimp maps this same slot back before returning control to this task.
The controller never resizes or replaces its own slot, returns while registered,
or lends pointers to children. Child tasks have their own Wimp domains/slots.
It issues Wimp_StartTask only after Initialise/Poll has returned control in USR.
Nonzero StartTask return means a child is still alive, not terminated. A blocked
StartTask leaves the controller suspended; R3c-3b's host observer owns prompt
recognition during that interval. The controller copies any asynchronously
needed child data into child-owned storage; it never publishes borrowed gate
pointers. It polls until its own shutdown policy is satisfied, closes down with
its exact task handle and TASK identifier, clears the handle, restores registers
and returns to BASIC. The gate performs only bounded kernel diagnostics then
END/OS_Exit; it makes no further Wimp call. Errors after registration join the
same CloseDown-before-return path. If CloseDown itself fails, the controller
terminates through OS_Exit with the Wimp-installed Exit handler still in place;
it never returns a registered task to BASIC. That fatal path has no gate-return
notification and cannot establish delivery. It creates no application replacement
or surviving pointer into the gate. Normal shutdown closes the exact task before
returning; the later BASIC END is its immediate process termination.
R3c-3b may keep CALL live for its controller lifetime without changing gate
bytes. The 120-second qualification limit applies to this slice's no-op and
stub, not to a future command's execution lifetime. This model requires private
proof on both requested pairs before the bytes are offered for freeze.
[Wimp contract](https://www.riscos.com/support/developers/prm/wimp.html).

All unallocated authored UpCall notifications are removed. Transport is passive
observation of the existing **XOS_Byte 0, R1=1, R2=0** machine-type query. The kernel
and ByteV receive an ordinary documented query, with its normal machine-type result and
interrupt behaviour; R3..R6 are caller-owned preserved registers. R3 points to
an aligned 64-byte block, R4=0x53435241 (ARCS), R5=64 and R6=role. Neither the
kernel nor other compliant ByteV claimants interpret those registers as a new
reason. The host observes at SWI entry without completing or changing the call.

The block is sixteen little-endian u32 words: magic ARCS, version 1, length 64,
role, token low/high, epoch low/high, event, sequence, detail 0/1, four zeros.
Role 0 is qualification diagnostics: zero token/epoch; events 1 entry, 2 return,
3 helper-return summary, 4 controller registered, 5 child start returned,
6 controller poll returned, 7 controller closed. Sequence is nonzero; details
are scalar result/count values. Entry/return detail 0 is monotonic centiseconds;
return detail 1 reports a release failure. Role 1 reserves authoritative attempt events
1..15, with nonzero 64-bit token/epoch and sequence. Their transition meanings,
origin binding, consumption and replay checks belong to R3c-3b; a role-0 event
can never consume an attempt or establish an outcome. The existing 32-byte
ACK1 CALL return summary remains separate and is not an event transport.
Qualification selects tagged OS_Byte queries, suppressing unrelated ByteV
traffic before inspection or recording. Other selected SWIs remain observable.
Each emitter owns its block while issuing the synchronous query; the observer
copies/validates only these scalar fields before the call continues. It cannot
retain a guest pointer. Trace capture admits only RAM/VRAM, alignment/range,
magic/version/size/role/event/binding/reserved-field checks; no arbitrary memory
dump or path is added. Invalid blocks produce no status record. A ByteV
claimant coexistence fixture must see a normal query and preserve its results.
[OS_Byte contract](https://www.riscos.com/support/developers/prm/restkernel.html).

The UpCall guard's code and active word move to **one 256-byte RMA allocation**
owned by the gate for the guest OS lifetime, cached in numeric ArcGate$Guard.
This allocation is retained even after successful release, reused on subsequent
gate calls, and reclaimed by machine reset. Its sixteen-byte header is AGR1 (0x31524741), version 1,
allocation length 256 and a zero word; code starts at +16 and the active word
is at +252. The numeric variable must have exactly four bytes and number type,
and the entire aligned allocation must lie in the OS_ReadDynamicArea(1) RMA
range before any header read. Its header/version/range are checked before reuse; malformed cached ownership refuses instead of allocating again.
Fresh code is assembled/synchronized before publishing ownership and claiming.
Existing code is never rewritten while a residual claim might refer to it.
The workspace is a single RMA active word, with no application pointer. An
inactive handler passes every reason unchanged. Cleanup closes the file while
active, **clears active before XOS_Release**, and checks its error result. A
release failure suppresses helper invocation and leaves an inactive, callable
RMA handler. Application exit, remapping or reuse cannot invalidate it. A later
OS_Claim of the identical code/workspace removes any duplicate residual claim;
there is no unbounded retry or per-invocation retained allocation. The guard
never owns controller data. Fault injection must skip release, reuse application
storage, and invoke the remaining vector, proving pass-through and continued
RMA ownership. Normal and error paths both deactivate before application exit.
[RMA](https://www.riscos.com/support/developers/prm/modules.html),
[vector identity/lifetime](https://www.riscos.com/support/developers/prm/softvecs.html).

The descriptor, entry ABI, receipt tuples and attachment policy below remain
unchanged. These Run B decisions supersede Run A's application-resident guard
and diagnostic/acknowledgement UpCalls; those mechanisms are not frozen.

The final command of `!Boot.ArcTasks` is `BASIC -quit ADFS::4.$.ArcGate`.
The gate's source and pathname are frozen for authored revision 2. It reads
only `ADFS::1.$.ArcHelper`, then calls its validated in-memory ARM entry as a
subroutine. It never runs an alias, Obey script, file-type command or pathname
from the descriptor. Action interpretation belongs to the helper's separately
versioned plan. This slice's only generated action is no-op.

## Interception and cleanup

The gate claims UpCallV (vector 0x1d) using OS_Claim immediately before its
synchronous drive-1 open/read/close sequence. OS_Claim inserts ahead of existing
claimants, including the Wimp media-request routine. While the gate owns that
sequence, ADFS media-request reasons 1 and 2 return R0=-1 by intercepting the
vector, causing FileCore to return an error instead of asking or retrying.
The handler does not use R3 as drive identity. Ownership comes from the single
foreground synchronous operation issued by the gate on its fixed drive-1 path;
it makes no Wimp poll, task start, CLI call or other filesystem call while
claimed. It passes other filesystems/reasons unchanged. UpCall 4 caused by its
cancel is consumed within that sequence. UpCall 256 is rejected while claimed,
so an application replacement cannot strand the handler.

Lookup, size, read and code synchronization use X SWIs and check V (bit 0 of BASIC SYS flags,
per [the BASIC SYS documentation](https://www.riscos.com/support/users/firstprog/chap14.htm)). A BASIC local error handler
covers Escape and language errors, closes a captured handle while interception
is still installed, then releases the exact (code, workspace) claim. Normal,
short-read, oversize, missing-file and malformed-descriptor paths join the same
cleanup. No helper code executes until the exact claim is released. Escape is acknowledged in cleanup. Cleanup uses X calls, deactivates the RMA guard before XOS_Release, and
refuses helper invocation if release reports V. The retained inactive allocation
has no reference to BASIC storage. Fresh allocations not yet published are
freed on failure; a published allocation is reused until OS reset. Numeric
XOS_PlatformFeatures uses the documented older-OS fallback (no sync requirement).
Where required, code synchronization precedes publishing/claiming and helper
invocation ([Acorn StrongARM support notes](https://www.riscos.com/support/developers/strongarm/sasupport.htm)).
The diagnostic entry precedes guard allocation and assembly, after allocating
its own bounded scalar block. The gate does not replace Error, Escape or Exit
environment handlers. Hardware reset clears vector claims and gate ownership.

Primary contracts: [software vectors](https://www.riscos.com/support/developers/prm/softvecs.html)
and [UpCalls](https://www.riscos.com/support/developers/prm/comms.html).

## Bounds and evidence

There is one open, one size query, at most one 8192-byte read, and one close.
There is no retry loop, directory enumeration, pathname expansion, task poll or
wait. A media request is cancelled on its first UpCall. The emulated FDC's
finite command deadlines bound physical access; firmware/controller retries
remain part of private qualification. The admission bound is **120 seconds of
virtual time from gate entry to return**, including cleanup and the no-op.
Each open/read/close has a 30-second qualification ceiling; size query, claim,
release and all descriptor processing together have a 30-second ceiling.
These are acceptance limits fixed before measurements, not a host watchdog.
Authored entry/return markers and OS_ReadMonotonicTime establish execution and
elapsed guest time. A hang or missing return fails qualification. Startup
before entry (including 3.50's blank interval) is measured separately.
An arbitrary future helper owns its own execution limits; it cannot inherit
this gate's vector claim. R3c-3b must qualify its separately versioned plan.

## Frozen descriptor and invocation ABI

`ArcHelper` is little endian, 68..8192 bytes inclusive, word aligned. Its
64-byte header is followed by ARM code and an optional helper-owned plan.

| Offset | Field |
|---:|---|
| 0..7 | ASCII `ARCRHLP1` |
| 8 | u32 descriptor version, exactly 1 |
| 12 | u32 header length, exactly 64 |
| 16 | u32 total file length, exact read and size match |
| 20 | u32 minimum gate ABI, exactly 1 |
| 24 | u32 maximum gate ABI, >=1 and <=1 in this slice |
| 28 | u32 entry offset, aligned, >=64, <total length |
| 32 | u32 media binding, exactly 1 (the fixed drive-1 image just read) |
| 36 | u32 cleanup owner, exactly 1 (gate releases all lookup resources) |
| 40 | u32 acknowledgement ABI, exactly 1 |
| 44 | u32 plan offset, aligned, >=entry+4, <=total length |
| 48 | u32 plan length, <=total-plan offset |
| 52..63 | three reserved u32, zero |

The header is not a signature or a trust grant for arbitrary media. Valid code
is supplied by the caller; malformed/unknown descriptors never execute. The
helper plan for this slice is 8 bytes: version u32=1, action u32=0 (no-op).
The helper owns plan validation and rejects every other version/action without
action. Replacement helpers can change their code/plan without touching the HD.

All code, image and acknowledgement buffers are explicitly word aligned
inside allocations with three bytes of alignment slack. At entry R0=image base, R1=length, R2=pointer to a zeroed 32-byte acknowledgement
block, R3=gate ABI 1. The helper returns through LR, restores the link PSR on a 26-bit return,
uses an ordinary branch on a 32-bit return, preserves R4..R12 and SP,
and must not replace the application or retain any pointer into gate memory.
The gate owns the image and acknowledgement until return. All lookup resources
are already closed/released. R3c-3a supplies a returning authored no-op only.

Reserved notification uses the role-1 ARCS block and ordinary OS_Byte query
specified above. The emitter must keep that block valid throughout the SWI;
R3c-3b owns token/epoch validation and deferred-action revalidation. R3c-3a
implements no host status consumer. The ACK1 block is only the CALL return
summary; it is never emitted as an authoritative attempt event.

Reserved acknowledgement layout (eight u32): magic `0x314b4341` (`ACK1`),
ABI 1, plan-owned attempt token, status, detail, and three zero words. The gate
initializes magic/ABI and token zero. Status 0 means unobserved, 1 helper
accepted, 2 returned, 3 rejected; 4..7 reserve dispatched, terminated, desktop
handoff and waiting for input for R3c-3b. This slice's no-op reports returned
or rejected. No host delivery-success claim is derived from eligibility,
startup-file presence or attachment; R3c-3b owns attempt binding and observation.

Role-0 ARCS notifications mark entry, helper return and gate return. Numeric
`ArcGate$Entry` and `ArcGate$Return` remain qualification-only monotonic
centisecond variables. Trace selection uses SWIs 0x6 and 0x20006; the added
status fields contain validated scalars only. Neither these diagnostics nor
the return summary establishes delivery authority.

## Receipt and attachment policy

Only tuples (receipt,populator,authored) `(1,1,1)` and `(1,1,2)` are admitted.
Envelope versions remain independent. New populations atomically publish
`(1,1,2)` with their root. Recovery/checkpoint copy supported receipts verbatim;
any authenticated unsupported root refuses before cleanup or older-root
fallback. Revision 1 opens unchanged, without package access, with one manual
launch notice and no generated helper. Edited/missing startup is preserved.

Preparation generates an in-memory, read-only 640 KiB old-map FileCore helper
only for population requests with a validated revision-2 receipt and no explicit
drive-1 choice. A present drive-1 plan key, including an empty value, overrides
it. Low-level non-population requests and isolated CMOS generation get none.
Preparation owns backing until engine stop, including failed attachment,
reset/reopen and unload. Generation and attachment are separate bounded units;
helper construction uses no namespace or VFS operation and copies at most
655360 bytes. Explicit media remains frontend-owned. Drive 0 and IDE policy
are unchanged. Cached helper identity excludes it from disc automation.
