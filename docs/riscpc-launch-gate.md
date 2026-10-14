# RiscPC revision-2 startup gate (R3c-3a)

This note fixes the interface before implementation. All guest code is
ArcDroid-authored GPL-2.0-only. Ordinary loads and Arculator remain unchanged.
Revision eligibility is provenance, never evidence of delivery.

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
cleanup. No helper code executes until the exact claim is released. Escape is acknowledged in cleanup. Cleanup uses X calls for close/acknowledgement and then OS_Release for the known valid claim; there is no documented error return for that release. Numeric XOS_PlatformFeatures uses the documented older-OS fallback (no sync
requirement). Where required, code-area synchronization precedes vector
claim and helper invocation
([Acorn StrongARM support notes](https://www.riscos.com/support/developers/strongarm/sasupport.htm)); a reported synchronization failure returns without
execution. These bounded kernel operations fit the miscellaneous ceiling.
The entry marker precedes allocation and assembly. The gate does not
replace Error, Escape or Exit environment handlers. No OS_Exit or application
replacement is issued while a claim exists. Hardware reset clears vector
claims. OS_Release has no documented error exit for the valid vector and
exact code/workspace pair held here. Release occurs synchronously before
anything can free or replace that storage.

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

Reserved notification: `XOS_UpCall` R0=0xA3C300, R1=acknowledgement pointer,
R2=32, R3=attempt token. The helper emits it while the borrowed block is valid;
the future host copies and validates the block synchronously and revalidates
its token/epoch before any deferred action. R3c-3a implements no host status
consumer. The helper installs its plan-owned attempt token before notification;
token zero denotes this slice's no-attempt no-op. The gate can reuse the block
after helper return, so future helpers must notify before returning.

Reserved acknowledgement layout (eight u32): magic `0x314b4341` (`ACK1`),
ABI 1, plan-owned attempt token, status, detail, and three zero words. The gate
initializes magic/ABI and token zero. Status 0 means unobserved, 1 helper
accepted, 2 returned, 3 rejected; 4..7 reserve dispatched, terminated, desktop
handoff and waiting for input for R3c-3b. This slice's no-op reports returned
or rejected. No host delivery-success claim is derived from eligibility,
startup-file presence or attachment; R3c-3b owns attempt binding and observation.

Authored diagnostic UpCalls 0xA3C301 and 0xA3C302 mark gate entry and return.
0xA3C310 plus the status marks helper return. They use XOS_UpCall, contain no
message or private data, and are not a host delivery acknowledgement. The
gate also leaves numeric `ArcGate$Entry` and `ArcGate$Return` diagnostic
variables containing monotonic centiseconds; these are qualification evidence
only, never host delivery authority. Their memory-only X SWIs run outside the
claim and fit the miscellaneous-operation ceiling. The existing test trace
can select SWIs 0x33 and 0x20033 to measure these markers.

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
