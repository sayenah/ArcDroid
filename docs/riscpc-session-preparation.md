<!-- SPDX-License-Identifier: GPL-2.0-only -->

# RiscPC session preparation (D-168, D-170, D-171, D-172)

Written before implementation, against `d374114`. Shipping ordinary loads keep
Arculator. The shipping-source module is dormant; plan v2 is its low-level client.
The original scheduler/resource description below records that implementation;
the Run A host contract supersedes its eager live-publication policy and defines
the proposed host/reset/notice behavior before host implementation.

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

## R3d-3 host contract, before implementation (Run A, 2026-10-18)

This section records the accepted host work order against dispatch base
`6b8b583`. R-1a (Fable, S68) resolves the initial Run A stop: execution
admission, RUNNING reset and persist retain the three existing module ownership
checks, as pinned by `rpc_session_source_guard.py`. The RUNNING body remains
byte-identical to merged `d42adbe`, including its ownership check. The frontend
uses injected session kind and host-pending policy. Next item 3's module-site
conversion wording is superseded. Implementation proceeds under that ruling;
the ownership-primitive section below remains the unchanged 12a record.

### Host ownership, presentation and boundaries

The production host will be `libretro/rpc_preparation_host.c/.h`. It owns the
preparation object and presentation state of one preparation-route session.
The frontend injects session kind and session ID; neither is inferred from a
retained engine backend. Production begin takes a resolved request. Only the
plan build exposes low-level begin and an optional extension table, whose
started-after, attach-after, attached-after and diagnostics-after hooks follow
production behavior. Rate substitution, trace, scripts, observer overrides,
gate faults and the HD cost wrapper remain test-only. Shipping must reference
neither low-level begin nor the extension table.

Media is staged at load, under the load-time option snapshot. The frontend owns
backing through engine detach; preparation copies descriptor/path views and is
the only drive-0 attacher at START. It also owns generated read-only helpers.
Explicit drive 1, including empty, is supplied at begin and retains its identity.
Production attach performs frontend rebind only: no second staging, drive mount,
IDE assignment or ADFFS attachment. Observer binding follows attachment, using
the module's verified catalogue. Production started is a no-op; diagnostics
drain through the engine boundary without enabling generation input or hints.

A preparation-route session is pending when the object is absent, its state is
anything except RUNNING, or execution is barred. Run, reset, all four setters,
keyboard and input polling consult that predicate before any active-engine route.
FAILED and CANCELLED cannot fall through to Arculator. Direct-start plans are
outside the preparation route and retain their existing engine-reset path.
Setter rejection precedes every observer, disc, notice-ring or launch mutation.

Pending frames use the normal video path exactly once and retain coherent
presentation/pitch state. They send black and 48000 / announced-rate silent
samples, carrying fractional samples. Load establishes 640×480 at 60 Hz once;
pending work never repeats AV negotiation. FAILED after RUNNING keeps the last
announced geometry/rate. Restart from such a failure re-baselines once if needed.
The first guest field and a successful RUNNING reset issue AV only on changed
values. Guest-driven and frontend-caused changes have separate counters.
Each running frame preserves refusal → diagnostics → disc → maintenance order,
with exactly one maintenance call. Pending and drain frames do not call it.

The load path negotiates pixel format, RPC controller info/descriptors without
the A3010 joystick, device capabilities and resolved input, keyboard callback,
OSK/sampling, AV baseline and frame/fault counters. Failed pixel negotiation
logs its reason, releases and returns false. OSK activation/interruption,
overflow, pad, keyboard and pointer behavior require frontend parity evidence.

### Reset and media-option lifetime

Both reset branches first clear the frontend input, raw keyboard, sampling,
guest time, error-hint generation/state, owned/previous/swapped disc state,
notice ring, engine disc state and disc cache. The host clears its notice at
priority 3. No module pre-terminal, reset-head, cancel or free clear remains.

Before a RUNNING reset, the host finishes any in-flight CMOS publication using
the dedicated completion entry. The RUNNING body at dispatch-base
`rpc_preparation.c:673–699` stays byte-identical to merged `d42adbe`: close the
barrier, detach, re-stage drive 0, restore drive 1 under admission policy,
reset/rebind and resume only on complete success. The synchronous failure
publication stays inside it. `reset_rebind` is the preparation route's sole
direct engine-reset callback; it also resets frontend state and binds the
verified catalogue, without the test input-ready seam. Guest-initiated reboots
retain D-204's separate helper-retirement behavior.

Every non-RUNNING Restart invalidates the attempt, stops generation or START
construction and releases offline resources first. It keeps content ownership,
drops defaults ownership and drops the old drive-0 view. Only then does it call
`restage_stopped`, which stages frontend backing without engine calls, replaces
the module view and updates explicit-helper write policy. Success restarts
VERIFY on the next frame. Failure leaves FAILED, stopped, unattached and without
store mutation, holding the content lock; another Restart retries. The same
view-replacement entry is legal under the closed RUNNING reset barrier.

Disk Writes, Write-protect Tab, short-tail and JFD-omitted policy freeze at the
load-time values while pending or running. Pending frames poll variable updates.
Each changed option gets N-11 once per session, tracked separately from the
Arculator flag. Restart reads current values and re-stages drive 0 in the
appropriate window; drive 1 is never re-prepared. Explicit-helper write policy
updates in both windows. Generated helpers remain read-only.

### Notices and privacy oracle

Severity is supplied by the module: progress, timed advisory, terminal or
completion clear. The host never derives it from text or a test phase. Both
message paths enforce one display slot: terminal > advisory > progress. The
first terminal remains, except a FAILED terminal replaces a non-stopping
refusal, and a drain outcome replaces its fault terminal once. N-18 is final
and never gains a drain suffix. Each terminal/advisory is logged once; progress
logging is deduplicated by severity and text, independently of refresh.

Progress refreshes every 60 frames and expires within 120 frames of the last
refresh. Advisories last 5 s extended / 300 fallback frames. During that lifetime
progress and hold updates are logged but hidden; current progress resumes on
expiry. Terminals persist until host clear, with fallback refresh every 60
frames. Reset, unload and switch clears have priority 3; host arbitration is
authoritative even when the frontend ignores priority. Every extended host
message targets `RETRO_MESSAGE_TARGET_OSD`. The exact same redacted text is used
on both paths, per frozen plan §7a, regardless of frontend logging behavior.

At RUNNING, the host emits "Starting RiscPC" and defers completion clear unless
a terminal is retained. It ends on the guest-time bound or a terminal, yields
to advisories and resumes within the bound. Successful RUNNING reset restarts
it; pending reset, unload and switch cancel it. These S68 defaults are not
measurements and make no claim of desktop readiness:

| Firmware | CPU | Hold bound, emulated seconds |
|---|---|---|
| 3.50 | ARM610 | 25 |
| 3.60 | ARM710 | 25 |
| 3.60 | ARM7500 | 25 |
| 3.70 | SA-110 | 25 |
| 3.71 | ARM710 | 25 |
| 3.71 | ARM7500 | 25 |
| 3.71 | SA-110 | 25 |

The ID-to-text oracle for N-1 through N-18 is plan §7/§7a, with these source
mappings at `6b8b583`. N-1–N-6, N-9, N-10 and N-12 have no new ordinary-load
caller in this slice. The same source-site mapping determines the family;
there is no substring classification of an error buffer.

| Source/event | ID or family | Text/redacted form |
|---|---|---|
| `status`, preparation lines 70–78 | N-7 | Checking hard disc; checking boot files; first-use settings; preparing hard disc; Starting RiscPC (exact existing literal texts) |
| RECOVER revision-1 notice, line 372 | N-8 | Existing revision-1 text, advisory |
| Changed fixed media-option label | N-11 | `{option} takes effect after Restart.` |
| RECORD unsaved marker | N-13 | RiscPC settings from the last session could not be saved. |
| Content contention after 10 s | N-14 | This RiscPC title is open in another RetroArch. Close it there or restart RetroArch, then close and reload this content. |
| Lock failure | N-15 | ArcDroid cannot lock RiscPC save files in this folder. Choose another save folder in RetroArch's directory settings. |
| Defaults contention after 120 s | N-16 | Another copy of RetroArch is preparing RiscPC settings. Wait for it to finish, then close and reload this content. |
| HD close failure | N-17 reset/unload | Recent RiscPC hard disc changes could not be saved. |
| Ownership identity lost | N-18 | Another app changed ArcDroid's RiscPC save files; this session stopped without saving. Close and reload this content. |
| Begin validation/allocation/path failures | N-B begin | Existing constant reasons; missing-firmware reason uses literal `<system>` |
| VERIFY fingerprint, lines 342–345 | N-B firmware | Existing family-name verification reason; no firmware path |
| Boot begin/step, lines 376/384; `rpc_boot.c` failure producers | N-B boot/boot-zip/boot-named | Frozen boot-family sentence with redacted reason |
| HD open/outcome, import, population, seal/publish, lines 362–420 | N-B hd | Existing constant outcomes, including without a creation receipt |
| CMOS begin/inspect/default/store publication failures | N-B cmos | Existing store constant reasons |
| Seed read, line 427 | N-B seed | Existing unavailable/wrong-size reason |
| Isolated construction/capture/advance/oracle, lines 449–475 | N-B generate | Existing engine or generation constant/numeric reasons |
| Requested construction, lines 489–492 | N-B engine | Existing engine constant/numeric reason |
| Product verification, line 504 | N-B product | Existing verification reason |
| Helper construction/mount, lines 524/689–690 | N-B helper | Existing constant/numeric reason |
| Launch binding, line 547 | N-B launch | Existing binding reason |
| Attach callback, lines 536–542 | N-B attach | Allow-listed literal or generic attach form below |
| RUNNING restage callback, line 680 | N-B restage | Allow-listed cause or generic restage form, with frozen snapshot suffix |
| New stopped restage callback | N-B restage-stopped | Allow-listed cause or generic restage form |
| Reset failure, lines 675/694–696 | N-B reset | Existing cause and snapshot outcome |
| Fault/invalidation, lines 717/727 | N-B fault/observation | Existing cause; later drain outcome under N-B drain |
| Rollback/retirement, lines 287/300 | N-B rollback/retire | Existing constant cause and applicable snapshot outcome |
| RUNNING one-drive notice, line 550 | launch-unavailable | Existing delivery-unavailable text, advisory |
| Running HD recovery/terminal, lines 719–721 | hd-recovered / N-B hd | Store severity, existing safe text |
| Usable record with refused CMOS, line 435 | cmos-refused | Store reason, non-stopping terminal |
| Live persist refusal, line 723 | cmos-persist | Store reason, non-stopping terminal |
| Free persist/marker failure | unload-cmos / unload-marker | Safe unload advisory/reason, always logged |
| Boot statistics, line 388 | boot-packages | Qualification/counts only |
| HD diagnostic, line 719 | hd-diagnostic | Redacted diagnostic; no store path |
| Progress and measurement lines | status / summary | Transition or numeric data only |

The line format is `<ID-or-token>[ <reason>]: <redacted text>`. N-15 uses only
`lock-root`, `lock-create`, `lock-unsupported` or `lock-error` (optional numeric
errno); N-18's unsaved outcome is logged as `N-18 unsaved`. Boot text is
"RiscPC boot files in <system>/arcdroid/riscpc/boot/ cannot be used ({reason}).
Replace them with unmodified copies." Member-derived portions of the reason
become `a file in the boot archives`; required-member/authored constant names
remain. Redaction happens at the producer/callback before the module's error
buffer. Other tokens are `the selected disc`, `another disc`,
`the selected content`, `the content folder`, and `the save folder`.

Media callbacks stage into a private scratch error buffer, clear the module
output on every successful exit, and forward only the following literal
allow-list or a frozen generic form. Locations are pre-edit frontend lines;
tests must assert each source literal (including absence of conversions).
The module clears its error buffer before a reset sequence, so a stale cause
cannot survive a callback that returns without a diagnostic.
Trailing full stops are removed from restage causes. The digest-mismatch
literal maps to "The JFD disc changed on storage".

| Literal allow-list | Source line |
|---|---|
| Unable to publish staged media with flush, close and rename. | 1530 |
| invalid media preparation input | 1562 |
| Short image cannot use original writes; choose shadow or off. | 1647 |
| out of memory | 1652 |
| unable to prepare staged media directory | 1677 |
| Writable media shadow has a structural fault and was not replaced. | 1691 |
| unable to replace invalid immutable staged media | 1700 |
| Zip archive exceeds 32 MiB. | 2081 |
| Unable to read the zip through the frontend VFS. | 2082 |
| Unsupported archive container; ArcDroid supports .zip files only. | 2303 |
| Unable to read the selected content through the frontend VFS. | 2312 |
| prepared logical media exceeds 256 MiB | 2358 |
| Zip archive exceeds 32 MiB during reset. | 2490 |
| Unable to re-open the zip during reset. | 2491 |
| Staged JFD digest mismatch. | 2781 |
| RPC drive 1 requires raw helper media | 7649 |

All other attach causes use "The discs in this content could not be loaded.
Check that the file is complete and a supported type." All other restage
causes use "The disc could not be reloaded". ZIP-index reason pass-through and
playlist-line formatting are excluded. Shared staging `arc_log` producers use
the tokens in RiscPC sessions only, including JFD title, ZIP leaf/member/order
and folder diagnostics. Arculator log bytes are unchanged. The shared
`arc_message` function and its carried disk/media notices remain unchanged for
R3d-4; host/module catalogue notices never enter it. The test-only Windows
crash locator stays outside this catalogue.

### Publication, fault drain and teardown

S-4 uses the frozen stepped default: publish changed CMOS through maintenance
steps within 60 frames. Reset and swap rollback finish an earlier publication
before their synchronous publication; these two error-path exceptions are
counted separately with measured costs. Unchanged-payload persistence succeeds
without a new generation. Failure expectations follow the actual publication
phase, not a blanket generation increment or a no-files-changed assumption:

| Last completed boundary | Allowed authoritative/temp outcome |
|---|---|
| Unchanged-payload no-op | Slots, generation and payload unchanged |
| Candidate written/read back | Previous authoritative slot(s) retained; exact candidate `.tmp` may remain |
| Older destination retired | Latest authoritative slot retained; older target may be absent; candidate `.tmp` retained |
| Rename published, readback fails | Candidate slot may already be authoritative at the next generation; `.tmp` moved |
| Completed publication | Exact candidate slot/generation accepted; marker removal is a separate namespace unit |

Tests enumerate every file and byte at each injected boundary, reopen without
false CORRUPT, and apply the same oracle twice when an earlier publication is
in flight. HD, launch-record and defaults bytes otherwise remain equal.

Both RUNNING maintenance failures (engine fault and observation invalidation)
capture the latest CMOS while readable, then stop and emit their cause at once.
FAILED frames drive a separate drain entry: finish any earlier publication,
then publish the captured snapshot with begin/step, within 60 frames under the
offline allowance. No guest or maintenance work runs in that drain. Its outcome
replaces the cause once with saved/could-not-save. A queued Restart waits for
drain completion; unload/switch finish it synchronously inside free. On ownership
loss no mutation or drain publication occurs, stores close in discard mode and
N-18 stays final. A publication-begin token check and per-running-frame identity
check both stop mutation on loss. The accepted D-208 same-UID shared-storage
rename-over residual remains explicit; it is not an exclusion guarantee.

Free on a live RUNNING engine captures/persists before stop. Stopped FAILED or
failed-attachment cleanup does not require a live engine. Engine stop detaches
IDE and both borrowed drives before freeing generated helper, media entries or
backing. Cleanup runs exactly once. Release retains the HD flush/close result;
reset reports N-17 and unload logs/displays it after the host clear. Content
ownership is released only after final persistence, drain and HD close. A
pending reset keeps it, dropping only defaults ownership. Cancel releases after
resource cleanup. No dirty writable VFS handle may survive an ownership-loss
boundary: write/flush/close is one bounded unit in each store; a buffering-close
VFS proves it. Close-error detection is limited by what the frontend exposes.

An unload publication-only CMOS failure always logs and displays its timed
advisory after the host clear and writes a zero-byte `.unsaved` marker while
holding content ownership. RECORD queries it for N-13; a successful persist
removes it as one namespace unit. Refused/corrupt records write no marker.
Marker-write failure logs/displays the immediate outcome but cannot cause N-13
on a later load. This applies to unload, deinit without unload and switching.

### Permanent measurement channels

One pending summary is emitted at RUNNING, FAILED or release before RUNNING,
and one live summary at release. Each records frames, elapsed host time, final
state and per-phase (offline, generation, attach, live, drain) preparation-work
maximum, complete-run duration maximum/p99 and run-interval maximum/p99. T uses
the phase's announced rate. Histograms are static, nearest-rank, 0.05 T buckets
through 8 T plus overflow. Summary fields ending in `p99_005T` report the
upper bucket index in units of 0.05 T; 161 denotes overflow. Exact maxima
remain in nanoseconds. Intervals over 0.5 s count as frontend gaps and are
excluded from over-2 T counts. Fast-forward uses GET_FASTFORWARDING or unknown.
Counters separately attribute D-169 namespace spikes, D-162 checkpoint ticks,
reset/rollback synchronous exceptions, frontend/guest AV changes, lock attempts,
lock-wait frames, identity checks and maximum native lock-call cost (outside
the arc_io census); the dedicated RUNNING identity check also has its own
`identity_max_ns` field. Store ownership checks remain part of their measured
store unit. D-172 endpoints are milliseconds since load entry: first
pending presentation, RUNNING and first guest field; desktop readiness is
unavailable. One first-field AV line records values, whether full AV was issued
and frame index. No channel includes a private name, path or data bytes.

### A-6 comparison projection

The host comparison base is `6b8b583`, containing merged R3c-3b part 2
`d42adbe` and the dormant lock primitive. No fixtures are regenerated. Each
changed expectation must assert the applicable delta individually:

1. Catalogue, transition, summary and first-field log lines.
2. N-8 advisory precedence/lifetime.
3. First-use wording during receipted regeneration.
4. Host-owned startup hold and deferred completion clear.
5. Priority-3 host clears and removal of module pre-terminal clears.
6. Per-option N-11 wording.
7. Unload failure log/advisory and conditional N-13 marker.
8. AV suppression while pending/unchanged; explicit failed-session re-baseline.
9. RUNNING detach → drive-0 restage → reset; restored drive 1.
10. Stopped terminal result on reset failure.
11. Side-effect-free pending setter rejection.
12. Disc-state/cache reset through real retro_reset, including runner flags.
13. Load-time media staging and post-release stopped restaging.
14. Load-time option freeze baseline.
15. RPC load negotiation/controller descriptors.
16. Persistent zero-byte content/default sentinel files and lock directory.
17. Catalogue-ID logs and redacted text at every level.
18. Fractional silence at the announced rate.
19. Captured-snapshot FAILED drain and explicit outcome.
20. Redacted fallback/extended text and OSD-only extended targeting.
21. Retained/reported HD close failure.
22. Lock mkdir, attempt, identity and wait counts in unit/frame output.
23. Earlier in-flight publication completed before reset/rollback.

Unaffected persisted bytes remain strictly equal. Authorized filesystem changes
are only zero-byte lock sentinels/directory, conditional `.unsaved` creation and
removal, and phase-qualified CMOS slot/temp changes holding captured snapshots.
Shipping ordinary-load output admits no new R3d-3 delta: only the cumulative
R3d-1 G-3 line and R3d-2 diagnostic wording/reason changes already accepted.
Ordinary loads do not call the host or locks. The library A/B in both package
states and real firmware/RetroArch/device evidence remain Fable's private legs.

## R3c-3b run B: generated helper-drive configuration (D-190)

Procedure revision 4 retains the isolated 45 s capture and its complete-mask,
stability and checksum oracle. After that oracle, while still in GENERATE and
before SAVE_DEFAULT, the host changes only logical CMOS byte 135 bits 0–2 to
2. On the RPC PCF8583 mapping this is raw byte 199, payload offset 183.
Recompute the existing checksum at raw byte 63/payload offset 47 as one plus
the sum of the other 239 payload bytes modulo 256. The upper five drive-count
bits, all other payload bytes, RTC and guest capture evidence are unchanged.
The public [CMOS allocation](https://www.riscos.com/support/developers/prm/cmos.html)
specifies the floppy-count field; the existing RTC mapping/checksum code supplies
the physical mapping. Authored fixtures and private 3.50/3.60/3.70/3.71 captures
verify this same two-byte-only transformation.

The defaults path's procedure key advances from p3 to p4. Old defaults remain
untouched and cannot satisfy a p4 lookup; the next request needing defaults
regenerates them. An authoritative per-content record still bypasses defaults
and is never repaired or rewritten by this policy. If its count is less than
two, preparation leaves it unchanged, attaches only the admitted default no-op,
issues no launch token and reports delivery unavailable with reason
`unconfigured`. The frozen gate retains its quick return. Drive :1 remains
read-only and excluded from drive-0 Disk Control/automation.

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

### QA follow-up contract (Run E)

Written before the Run E implementation. The primitive remains dormant. A NULL
token is tolerated consistently: initialization, acquisition and verification
return root-refused; held/namespace queries return false; release does nothing.
No backend or VFS operation occurs for a NULL token.

The shipping guard admits only explicitly enumerated calls in the lock module.
Native open, lock/unlock, identity and close expressions are pinned with their
arguments, occurrence counts and platform preprocessor context. Windows permits
the existing strict UTF-8 conversion and error query as supporting operations.
Pure string/memory utilities, internal helpers, the exact backend callbacks and
the existing VFS mkdir/metadata operations are separately enumerated; no other
call is admitted. In particular there is no sentinel data, descriptor mutation,
classic lock, blocking lock, or alternate namespace operation. Stat-family
identity operations, including lstat, remain confined to this module. Raw
open/dump references in comments and strings remain forbidden, except for the
existing exact arc.h comment; other comment/string text is not executable I/O.
Guard self-tests exercise type-only dormancy references, arc_io itself, every
shipping source tree, comments, strings and both separator/newline forms.

Each self-test creates an exclusive new child directory below its supplied
root, using PID plus a collision counter (no OS entropy). It reports that path;
stale sentinels and read-only fixtures from an interrupted earlier run are left
alone. A supplied alias is mapped to the same child when its parent names the
same directory. Explicit try/hold commands still share the exact supplied root.
Every unqualified optional leg must lack that leg's passed result, including
permission checks, alias checks, restoration and cleanup; mandatory lock and
identity assertions are unchanged. The runner VFS reports -2 for an existing
directory before checking mutation confinement, so walking existing ancestors
does not grant permission to create anything outside --save. This matches the
existing-directory result in [RetroArch 1.22.2's mkdir implementation](https://github.com/libretro/RetroArch/blob/v1.22.2/libretro-common/vfs/vfs_implementation.c#L1055-L1131),
which maps an EEXIST result to -2.

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

### Android/FUSE probe capability audit (Run C)

Written before the Run C probe changes. Fable's Odin3 (Android 15, kernel
6.6.87-android15, shell UID 2000) reported `ENOSYS` (38) for symlink creation on
MediaProvider shared storage; the required dot-root contention leg had passed.
The diagnostic `flock` also returned ENOSYS; it remains diagnostic only and
does not select or qualify the production OFD backend. Fable reports the Run B
probe completed every Windows/NTFS leg with exit zero.

POSIX optional fixture operations accept `ENOSYS`, `ENOTSUP`, `EOPNOTSUPP`,
`EPERM` and `EACCES` as capability/permission refusals, recording the actual
errno and marking that leg unqualified. Other errors still fail the probe.
These are probe classifications, not changes to production error mapping.
Linux documents filesystem/permission limits for [symlink](https://man7.org/linux/man-pages/man2/symlink.2.html),
[chmod](https://man7.org/linux/man-pages/man2/chmod.2.html),
[rename](https://man7.org/linux/man-pages/man2/rename.2.html) and
[unlink](https://man7.org/linux/man-pages/man2/unlink.2.html); the ENOSYS
observation above is the supplied device evidence.

| Optional leg | Qualification and retained lock assertion |
|---|---|
| Symlink (the probe has no hard-link fixture) | Success or an existing alias still requires contention through that alias. Refusal is unqualified and reported. Dot-root contention remains mandatory regardless. |
| Supplied POSIX case/spelling alias | Stat the roots first. A missing, inaccessible, unsupported or distinct root is unqualified and reported, without creating a second namespace. Matching device/inode still requires contention. Windows retains its existing required alias check. |
| Rename-over / delete | Success still requires sticky identity loss and no held ownership. An unsupported/refused operation instead requires held verification and a contended second acquisition, and is reported unqualified. Unsupported fixture cleanup is reported separately. Release/reacquire and zero-byte checks still run. |
| Chmod / read-only | Unsupported chmod, ignored modes, or an unsupported permission-check open are reported unqualified. Actual write-open permission denial still requires the primitive's create-error refusal. Mode-restoration capability failures are reported rather than hidden; use disposable roots. |

Required open/lock, same-process/two-process contention, crash/kill release,
identity checks following successful mutation, and nonzero/non-directory/root
refusals retain their assertions. A successful probe with unqualified legs is
not evidence that those optional capabilities work on that filesystem.

### Guard portability audit (Run D)

Written before the Run D guard corrections, after merging `ea8bfde` at
`7fd079f`. Both dormancy and shipping-I/O guards compare repository-relative
names using forward slashes regardless of the host's separator. Normalization
changes separators only; exemption names and their case remain exact. The
dormancy scan emits POSIX-form relative keys, and both checkers also normalize
input keys so Windows-form test inputs exercise the same exemption logic.
The dormant header-reference check recognizes header-name case variants because
Windows can resolve `ARC_LOCK.H` to `arc_lock.h`. C identifiers and exemption
filenames retain exact case. Additional uppercase/mixed-case include mutants
exercise this rule with both path separators and line endings.

Every existing migration/I-O negative input is checked with POSIX and Windows
path forms and with LF and CRLF text. The dormant primitive and arc_io exemptions
must pass in both forms; shipping callers outside them must still fail. The
native Windows process/unit tests retain their Windows branches and CRLF pipe
normalization. POSIX-only capability/native-contract/mutant tests remain excluded
from Windows by CMake. No production or process-probe behavior changes in Run D.
