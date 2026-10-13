# RiscPC session preparation (D-168, D-170)

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

VALIDATE stops exactly on the first FORMAT state, after ALLOCATE. The module
owns the boot context, open package handles and offline candidate until publish
or abort. Generation owns only a separate firmware copy and zero CMOS; no media,
helper or IDE is attached. No store maintenance occurs at the validation pause.
Later extraction/hash/readback/seal/rehash failure stops preparation but preserves
completed CMOS publications. A receipt is creation provenance, not integrity of
subsequent guest files.

Preparation budgets are 3,333,333 ns without namespace work and 8,333,333 ns for
an isolated namespace step. Declared costs: bounded ordinary unit 1,000,000 ns,
namespace-capable unit 8,333,333 ns. Namespace-capable work starts only in an
otherwise empty frame; any attempted namespace callback ends the batch. Admission
uses both elapsed monotonic time and reserved declared costs, across state
transitions; at most 32 units/frame even with a stationary/backward clock.
These are admission bounds requiring Fable's whole-frame device qualification;
VFS latency has no hard bound and D-169 permits isolated indivisible spikes.
Engine construction suspends after each owned allocation, at most 1 MiB RAM
clear, at most 32 KiB ROM copy, and device initialization. Low-level blank/import
also uses the stepped candidate I/O, seal and publish APIs. Final media backing
is frontend-owned; attachment and teardown still require whole-frame device
measurement alongside the storage units.

Generation retains the original 16,666,667 ns virtual quantum, 45 s capture,
45 s Delete release and fixed generation epoch. Its numeric host allowance is
66,666,664 ns, reserving 8,333,333 ns per quantum, at most eight quanta/frame.
The existing one/eight-quantum seam can reduce this cap. No smaller virtual
quantum or host-dependent rate/deadline is introduced. Qualification of that
host reservation on all seven private pairs remains Fable's AC-6 evidence.
Generation frames cannot combine with storage frames. Host time is used only
here and in offline preparation; running maintenance is one deterministic HD
tick and eager CMOS persistence per frontend call. The requested epoch is
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
status first, then emit one notification; FAILED never refreshes progress.
Legacy discs say “without a creation receipt”. No time estimate is shown.

Pinned plan baseline: `d374114`. V1 device/AV/input/factory goldens must remain
unchanged. V2 gains preparation black/silent frames and preparation reset now
restarts recovery; requested virtual traces and published bytes remain identical.
Each affected expectation is updated individually; no fixture regeneration.
