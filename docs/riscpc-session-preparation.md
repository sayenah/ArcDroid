# RiscPC session preparation (D-168, D-170, D-171, D-172)

Written before implementation, against `d374114`. Shipping ordinary loads keep
Arculator. The shipping-source module is dormant; plan v2 is its low-level client.

States: VERIFY → RECOVER → VALIDATE → RECORD → DEFAULTS → GENERATE →
SAVE_DEFAULT → SAVE_RECORD → POPULATE → START → RUNNING. Existing records skip
defaults and optional seed-file I/O; a seed file is read only after verified
absence; cached defaults skip generation; receipted discs skip all package I/O.
Every state can fail to FAILED (no requested guest), reset, or unload. Low-level
clients may explicitly omit IDE, create a blank, import, or supply authored
firmware/seed; they do not pass through firmware qualification policy. Production
requests require a qualified R3a pair and reverify size, CRC-32 and SHA-1.
Low-level requests without a catalogue entry skip unused CRC/SHA-1 calculations,
while retaining complete SHA-256 derivation for their persistence identity.

VALIDATE stops exactly on the first FORMAT state, after ALLOCATE. The module
owns the boot context, open package handles and offline candidate until publish
or abort. Generation owns only a separate firmware copy and zero CMOS; no media,
helper or IDE is attached. No store maintenance occurs at the validation pause.
Later extraction/hash/readback/seal/rehash failure stops preparation but preserves
completed CMOS publications. A receipt is creation provenance, not integrity of
subsequent guest files.

Run D retains D-172 and D-171's measured admission. Offline preparation covers recovery,
package validation, population and publication before the requested session;
every unit kind, namespace and final attachment included, shares a 13,333,333 ns
(0.80 T) frame budget. Generation has its separate allowance below. The remaining
fifth of the offline frame is available for frontend presentation.
Once a frame has begun, admission reserves another 833,333 ns (0.05 T) within
that allowance for estimator variation. A first indivisible unit retains the
full allowance. POPULATE learns inflate, hash, format, read and write costs
separately by boot phase as well as namespace class; cheap inflate units cannot
evict a format or hash observation from its sixteen-sample window. Estimators
use the recent maximum, explicitly clamped to the declared ceiling.
Admission uses elapsed time plus the recent maximum of
that unit kind's last sixteen costs, bounded by its declared ceiling. Cold
estimates use the ceiling: ordinary 1,000,000 ns, namespace 8,333,333 ns.
A namespace frame may contain several units; a unit making an actual namespace
attempt and exceeding its estimate ends the batch. Stat-only path checks do not
end a batch as namespace spikes. Ordinary units update the estimate without wasting
the remaining frame allowance. Advances also stop on an underestimated cost. CMOS and disc work share elapsed accounting across transitions.
A stationary/backward clock cannot reduce an estimate: declared accumulated
costs supply a conservative fallback cap (twelve ordinary units), while an
absolute 4096-unit cap also bounds nearly stationary clocks. Equal readings
at any point in a frame activate declared-cost accounting from the last measured
elapsed time, including the unit whose clock reading stalled. Clock progress
resumes measured accounting. Equal samples never lower learned estimates.
D-172's Odin3 shared-storage targets replace D-171's: first use within 50 s,
including generation, and a receipted reopen within 1 s, on all seven pairs.
These measured admission limits still require Fable's whole-frame device
qualification; D-169 permits isolated indivisible namespace spikes.
Engine construction suspends after each owned allocation, at most 1 MiB RAM
clear, at most 32 KiB ROM copy, and device initialization. Low-level blank/import
also uses the stepped candidate I/O, seal and publish APIs. Final media backing
is frontend-owned; attachment and teardown still require whole-frame device
measurement alongside the storage units. Final attachment is a separate,
indivisible 8,333,333 ns unit running in an otherwise empty frame; a scoped
VFS census includes attempted file callbacks, transfer bytes, namespace work
and partial-start cleanup. It does not pretend this whole frontend operation
is a bounded 1 ms storage step.

Generation retains the original 16,666,667 ns virtual quantum, 45 s capture,
45 s Delete release and fixed generation epoch. Its numeric host allowance is
66,666,664 ns, with at most eight quanta/frame. Admission learns the recent
per-advance maximum independently of construction. A cold advance's declared
ceiling is the complete generation budget, replacing the disproven 8.33 ms
ceiling. Slow hosts admit fewer advances; elapsed time never changes guest
capture, rate or quantum. Once a frame has begun, admission leaves 2,666,664 ns
for cost variation and pending presentation within that 4 T allowance. A cold
indivisible advance can use the full allowance.
The existing one/eight-quantum seam can reduce this cap. No smaller virtual
quantum or host-dependent rate/deadline is introduced. Qualification of that
measured advance estimate on all seven private pairs remains Fable's AC-6 evidence.
Generation frames cannot combine with storage frames. Host time is used only
here and in offline preparation; running maintenance is one deterministic HD
tick and eager CMOS persistence per frontend call. Live HD budgets remain
0.20 T ordinarily, 0.50 T with a namespace attempt, and 0.20 T for acknowledgement;
neither elapsed host time nor the offline allowance changes that schedule.
The requested epoch is
captured once in the request; requested counters and scripted input start only
at START's successful transition, never during generation.

The module owns copied firmware and request paths, CMOS stores, boot context
and HD handle. Engine resolved construction borrows firmware through its start steps, then owns
the copied ROM and CMOS; floppy mounts copy
the descriptor and borrow JFD backing, IDE copies callbacks and borrows the
store. The frontend retains media backing until engine detach/stop. All partial
starts stop the engine before freeing any backing. Final attachment includes
both media drives and IDE before any requested instruction. Engine test exports
wrap these production operations, with rate/fault seams outside policy. Synchronous starts drain the same bounded
construction without altering the low-level allocation order or final bytes. V1
factory capture keeps its separate 30 s default.

Preparation reset stops generation/requested construction, closes inspections,
aborts the candidate and restarts VERIFY/recovery with the retained request.
Running reset retains volatile CMOS and HD, using the established engine reset.
Unload snapshots the latest CMOS, drains/reconciles pending publication, then
attempts the latest snapshot before detach/free. Failure is explicit; already
published generations remain recoverable. Cancellation does not promise a new
publication. Process termination recovers only already-published roots/records.

Status begins on the first pending frame: checking hard disc (including firmware
verification), checking boot files, preparing settings, preparing hard disc.
Status refreshes every 60 pending frames with a 60 s extended-message lifetime; completion, reset
and unload send an empty status. A fallback ordinary message also lasts 120
frames, so an uncleared fallback expires within 120 frontend frames (2 s at 60 Hz). Terminal errors clear
status first, then emit one terminal status; FAILED never refreshes progress.
Extended terminal status uses the maximum unsigned lifetime and is cleared on
reset/unload. Its ordinary-message fallback refreshes every sixty frames until
clear. A valid saved record alongside refused CMOS data follows the same
clear/error precedence and suppresses completion's progress clear. Receipted
reopens with saved CMOS say "Starting RiscPC". Memory seeds, like seed paths,
never override bytes 0–15 when a saved record is authoritative.
Legacy discs say “without a creation receipt”. No time estimate is shown.
Recovery advisories, including a recovered read-only view, are ordinary timed
messages (5 s extended / 300 frames fallback), not persistent error statuses.
The store carries notice severity; it is not inferred from text or read-only
state. Only terminal errors use the retained error status. An advisory does not
clear an existing terminal status.

Pinned plan baseline: `d374114`. V1 device/AV/input/factory goldens must remain
unchanged. V2 gains preparation black/silent frames and preparation reset now
restarts recovery; requested virtual traces and published bytes remain identical.
Each affected expectation is updated individually; no fixture regeneration.

## Concurrent-writer ownership primitive (R3d-3, D-192, D-193)

This section is written before the 12a implementation, against `7b5a87a`.
`libretro/arc_lock.c` is dormant: preparation, stores and shipping frontend
paths do not call it. Enforcement and migration of every caller belong together
on `slice/r3d3-host` (12b). Existing store and preparation contracts are unchanged
by 12a. No lease, owner record, entropy, automatic breaking or file deletion is
involved.

Each independent acquisition opens a dedicated sentinel and asks the OS for an
exclusive, non-blocking lock. Two opens in one process must conflict through the
backend itself; there is no library-static owner registry. Closing the last
description/handle or process death releases ownership. Release never removes
the sentinel. Descriptors carry no data: no read, write, truncation or extension.

| Platform | Open and lock | Lifetime |
|---|---|---|
| Linux / Android | `open` with `O_RDWR\|O_CREAT\|O_CLOEXEC`, mode `0600`; `fcntl(F_OFD_SETLK)`, `F_WRLCK`, `SEEK_SET`, start/length/pid zero | Last close or process death; compile fails without OFD declarations, runtime refusal without OFD support. No classic-lock or Linux `flock` fallback. |
| Windows | UTF-8 converted strictly with `MultiByteToWideChar(CP_UTF8)`; `CreateFileW` read/write, share read/write only, NULL security attributes, `OPEN_ALWAYS`, normal attributes; `LockFileEx` exclusive + fail-immediately, byte 0 length 1 | Non-inheritable synchronous handle. No delete sharing; explicit unlock then close. Release after process termination can be delayed by the OS. |
| macOS (development) | Same POSIX open flags/mode; `flock(LOCK_EX\|LOCK_NB)` | Independent open descriptions conflict; last close or process death releases. |

The public flag-free backend table supplies open, try-lock, identity, unlock and
close operations, plus the error-number domain (OFD, macOS, Windows). NULL selects
the native backend. Tests inject raw errors through the same mapping used by the
native backend. A token is an owned, non-copyable object, initialized once before
use and released before reinitialization. There is no state across library loads.

Content keys are exactly `c-<content40>.lock`; defaults keys are exactly
`d-<fw64>-m<N>-p<proc>-t<timing>.lock`, with model index 0–3 encoded as 1–4 to
match the CMOS store. Hex keys are validated and normalized to lowercase. Paths
are under `<save>/arcdroid/locks-riscpc/` only. Absolute native roots are required:
POSIX `/…`, or Windows drive-absolute / UNC with server and share. Relative,
scheme, malformed, invalid-key and overlong paths are refused before any I/O.
Trailing root separators are normalized; other aliases resolve through the OS.
The same store reached through a save-root symlink, spelling or case alias must
reach the same sentinel when its keys agree.

`arc_io_mkdir` calls VFS mkdir directly, without a prior stat, returning created,
exists (`-2`) or error. Acquisition creates one parent per step, checking that an
exists result names a valid directory. POSIX root, Windows drive and UNC
server/share prefixes are not created. `arc_lock_next_namespace` identifies these
steps for the future scheduler; they count in the VFS census. Existing
`arc_io_make_path_step` is untouched. A directory step returns transient until
the native attempt is ready. Each subsequent acquire call makes at most one
open and one non-blocking lock attempt; an open handle survives contention and
transient lock errors. No loop, wait, sleep, clock or deadline lives in the
primitive. Its host will attempt before checking W, using an injected monotonic
clock (10 s content, 120 s defaults), and will never displace a paused holder.

| Raw result | Primitive outcome / future notice |
|---|---|
| Lock succeeds and identity/stat agree | held |
| OFD `EAGAIN`, macOS `EWOULDBLOCK`, Windows `ERROR_LOCK_VIOLATION` | contended; host eventually N-14 / N-16 |
| POSIX `EINTR` (open or lock), open `EEXIST`, Windows open `ERROR_SHARING_VIOLATION` | transient; host retries within W, then N-15 `lock-create` |
| OFD `EINVAL`, POSIX `ENOSYS` / `ENOLCK` / `EOPNOTSUPP` / `ENOTSUP`, Windows `ERROR_INVALID_FUNCTION` / `ERROR_NOT_SUPPORTED` | unsupported; N-15 `lock-unsupported` |
| Root validation or post-lock VFS stat not a regular zero-byte file | root refused; N-15 `lock-root` |
| mkdir or other native open failure (including read-only sentinel) | create error; N-15 `lock-create` |
| Other lock error, including OFD `EACCES` | lock error; N-15 `lock-error` |
| Identity mismatch immediately after lock | unlock, close, transient; reopen next attempt |
| Identity mismatch while held | identity lost, held query false, unlock/close; sticky until release; N-18 |

On POSIX, after each successful lock and on explicit verify, compare `fstat(fd)`
to `stat(path)`: both must have at least one link and agree on device and inode.
Failed identity syscalls fail closed. On
Windows, omitted delete sharing prevents rename-over and deletion while held;
the native identity callback therefore needs no polling syscall. On every
platform, a VFS stat after acquisition must report a regular zero-byte file.
This is a consistency check, not proof that VFS and native I/O resolve one
object. Identity/stat refusal closes the handle before returning. Held-state
loss cannot silently reacquire; release is explicit. Repeated release is safe.

12b must acquire content ownership before any recovery/open, then defaults
ownership only while holding content ownership. It must verify every maintenance
frame and at every publication begin, stop all mutation on loss, and release
only after final persist, fault drain and store close. It must also implement
the re-check's no-dirty-buffered-handle invariant and publication-phase oracle.
None of those store integrations belongs to this dormant primitive.

The guarantee is conditional on cooperating clients using a stable sentinel in
a shared, stable namespace. Residuals: network filesystems and synced replicas
are outside the guarantee; aliases below `arcdroid/` can share stores with
different sentinels; different Android mount views need not conflict. POSIX
replacement between verification and a later write is detected, not excluded;
a new opener can lock the replacement in that window. A fork without exec can
retain an inherited OFD description until the child exits. Hung FUSE calls,
debuggers and live crash-reporting processes retain locks. Non-blocking lock
requests do not bound filesystem syscall latency on an unhealthy filesystem.
Pre-OFD kernels refuse RiscPC per D-192; sdcardfs remains unverified.

P10b expectations (Fable qualifies devices after 12a merges): two processes and
two same-process acquisitions conflict; kill and reap the holder, then acquire
on the first POSIX attempt (Windows retries within W, count and time recorded).
Rename-over and deletion trigger POSIX identity loss; Windows must refuse both
and ownership must remain held. Record each operation's actual error code;
neither operation is required to return one particular code. Reacquire after
release and alias/case legs must pass. Run on
Mac APFS, Windows NTFS, Odin3 shared storage, internal app-private storage and
external `Android/data` storage. Android partners must be app processes or prove
matching mount views; record each partner's mountinfo, OS/kernel, MediaProvider
versionCode and observed diagnostic `flock` result (never used by production).
Windows also needs a transient-open leg with antivirus or sync activity. Device
results are separate from cross-build evidence. A shared-storage exclusion
failure goes to the Founder before 12b; the second Android device is additional
evidence when available.

Primary sources checked for the implementation: [Linux OFD locks](https://man7.org/linux/man-pages/man2/fcntl_locking.2.html),
[Microsoft LockFileEx](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex),
and [macOS flock](https://keith.github.io/xcode-man-pages/flock.2.html).
The governing R-10 research and adjudications are in
`docs/riscpc-r3d3-work-order.md`; the supplied P10a research is in
`.evidence/LOCK-RESEARCH.md` (its earlier fallback/share-delete suggestions are
superseded by R-10 and D-193).

### Windows probe contract audit (Run B)

Written before the Run B probe correction. Fable's Windows 11 / NTFS probe
observed `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` fail with
`ERROR_ACCESS_DENIED` (5), and `DeleteFileW` fail with
`ERROR_SHARING_VIOLATION` (32); the holder's verify still reported held.
[MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)
and [DeleteFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-deletefilew)
document a zero result on failure and extended information from `GetLastError`.
They do not promise the same error code for these operations. The probe captures
the error immediately, logs it, and asserts failure plus continued ownership.

The remaining Windows assertions are retained after this audit:

| Probe leg | Microsoft contract and assertion |
|---|---|
| Deny-sharing opener | [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew) explicitly specifies `ERROR_SHARING_VIOLATION` for conflicting access/sharing modes. The fixture's share mode zero conflicts with the primitive's read/write request: transient, then successful retry after fixture close. This error-code mapping does not apply to rename/delete. |
| Inheritance | NULL security attributes make the sentinel handle non-inheritable per `CreateFileW`. [GetHandleInformation](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-gethandleinformation) exposes `HANDLE_FLAG_INHERIT`; it must be clear. [CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw) with inheritance enabled inherits handles marked inheritable, allowing the fixture pipes without inheriting the sentinel. |
| Kill and reap | [TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess) initiates termination asynchronously. The probe waits on the process with [WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject) before reading its exit status or attempting reacquisition. [GetExitCodeProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess) reports the supplied termination code (98) after exit; the crash fixture instead exits with 97. The holder handshake ensures the kill target is live and awaiting input. |
| Post-death release | [LockFileEx](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex) allows OS-dependent release delay. Windows therefore retries contention after reap, recording attempts and elapsed time, rather than requiring the first attempt to succeed. The 5 s reap timeout and 10 s acquisition budget are probe qualification limits, not API timing guarantees. |

No production lock behavior changes in Run B. Native Windows execution of the
rebuilt probe remains a separate P10b device leg.
