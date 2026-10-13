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
