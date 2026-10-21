<!-- SPDX-License-Identifier: GPL-2.0-only -->

# SS-1: Arculator state and publication design

The [Run K design as built](#run-k-design-as-built), R-SS4e and D-215, supersedes
the earlier representation, picture and bound claims below. Run H and Run I
remain as historical design/evidence under their original contract.

Order: [SS-1](handoff.md), D-011, D-210, R-SS3a and run-H adjudication.
Run H is not acceptance-complete: the 16 MiB/minimum-bound gate remains unmet.
Run L integrates R-SS3g with R3d-4's R-1a in both gate states: live-session
admission and nested lifecycle guards also protect a legacy Android loader
aliasing the secondary copy to the primary module. Historical Run H notes below
retain their original limitations; the Run L section supersedes them. Compatibility is the
same core build, platform, firmware, machine, prepared media and options.
RiscPC serialization remains unavailable. Capability advertising is the last
acceptance gate. The committed [state harnesses](../tests/arculator_state_matrix.py)
and [compatibility notes](ss1-release-notes.md) describe the checks and public contract.

## Inventory and ownership

[`arculator_state_inventory.json`](../tests/arculator_state_inventory.json)
records named static objects, mixed-aggregate member exceptions, retained heap
owners and allocation sites. Initial named-object enumeration used Clang's C
AST with the actual CMake compilation commands and test definitions. Field
visitors in `engine/state-generated/` expand that enumeration, with explicit
review of pointer targets, dynamic ownership, callbacks and host authority.
They are reviewed source, not a generator run during a build.

The independent lexical guard follows the production Arculator, frontend and
core source lists and quoted repository includes. It checks inactive
preprocessor branches, macro bodies, global and local static declarations,
record layouts, allocation sites and CMake source attachments. It checks before
optimization; adding an unused object cannot evade it. Every inventory row has
a serialized, derived-on-load or host-only classification and a reason. The
mutation tests add globals, local statics, attributes, initialized externs,
aggregate members, allocations, macro storage and CMake sources. A plain
function-body edit without storage changes remains a passing control.

RAM, banked registers, pipeline/cache contents, device registers, timer ordering
and virtual time, partial floppy/IDE transfers, audio filter/queue history,
retained raster/presentation pixels, input transitions, OSK, observers and
frontend session values are captured. The full open raw-medium view and all
JFD replacements and created sectors are included. A raw track cache alone is
insufficient. Dormant imported LCD support has no reachable bitmap in any of
the four admitted machine profiles; admitting an LCD profile requires its own
bitmap owner codec first.

ROM allocations and deterministic lookup tables come from the validated
current session. Native VFS handles are reopened against current logical media
bindings. Timer callbacks, IRQ handlers, VFS callbacks, logger callbacks and
native stack continuations are current host bindings. Reset-time media/helper
paths use validated prepared-media IDs, and reset-time JFD/data pointers use
typed graph references. Switching the selected image on restore retires old
overlay children only after commit. They never come from a
state. Durable committed frontiers and the input-callback publication boundary
are host authority and cannot be rewound.

## Container and transactional load

`arc_state.c` writes magic `ARCSS1\0\0`, a little-endian 64-bit format version
(currently 3), used length, record count and CRC32 in a 40-byte header. Each
record has a 64-bit logical key, kind and encoded length, followed by payload.
Keys derive from owner names and field offsets/extents, rather than addresses.
Canonical output sorts keys. The header's used length describes the exact
machine payload. `retro_serialize_size()` returns a session-stable capacity for
all contexts, including files. Output zeros the unused capacity and ends it
with a 16-byte `ARCCAP3\0`/little-endian-capacity trailer. Input validates the
payload CRC, every interpreted record and that trailer; spare bytes between
payload and trailer are never interpreted or scanned. A compact, exactly-used
container has no trailer. This framing preserves canonical save/load/save bytes
while avoiding a scan of a large unused frontend buffer on every load. The unit
checks cover ignored spare bytes, corrupt trailers and unchanged destinations.
The parser caps containers at 1 GiB and 262,144 records. Duplicate/unknown fields,
bad checksums and invalid extents refuse the load.

Kinds encode values, bounded owner/offset references, heap extents, bounded NUL
strings, expected identities and bounded blobs. Primitive values use the same
build's ABI. Identity records bind G-3 version/commit/target/configuration/gate,
effective substitutions and catalogue status, a source schema hash, platform
and compiler, ABI and endian marker, engine, machine, firmware digest, ordered
media and effective options. G-3's initialization counter is diagnostic and is
not a build identity. Cross-build/platform migration is unsupported.

Large value and blob records use word runs only when smaller than their exact bytes;
literal packets bound incompressible data. Decoded sizes and aggregate scratch
space are bounded before allocation. Packet syntax is fully checked before
commit; large static arrays decode directly into their destinations at commit,
while replacement heap payloads decode into prepared allocations. Scalar writes
use inline transaction storage and a destination index for projections.
Pointer records identify named regions and
bounded offsets. Timer, register-bank, memory-page, controller, media, input and
file references have separate target classes. Biased MEMC page and instruction
fetch pointers are rebased explicitly. Native addresses are never emitted.

Registration precedes field traversal. A load prepares replacement heaps,
strings, file mirrors and pending queues, and stages writes to live objects.
Validation includes typed reference bounds, boolean representations, JFD
payload/descriptor/track extents and timer-list linkage/cycles. Only a fully
validated transaction installs the write set and transfers ownership. Failure
frees prepared resources, emits the timed libretro notice and preserves guest
state and host files. Snapshots are refused inside `retro_run`, where the native
fault continuation and callbacks are active. A successful geometry/rate change
notifies the frontend after commit.

## Compact machine state and stable frontend capacity (run H)

R-SS4b keeps a stable capacity in every context. A schema-measuring pass walks
the reviewed field visitors at load, counting encoded values and references,
maximum bounded strings and all prepared-media slots without copying guest
payloads. It no longer charges native pointer padding or the parser's complete
record allowance. Machine RAM, display limits, mutable media and pending effects
are added separately. The capacity is host authority, never restored from data.

Only media classes admitted at load reserve payload capacity: the largest current
writable raw image, active JFD capability if present, and native HD if attached.
A larger replacement remains usable but state save/load refuses with a timed
notice. Replacement registration is transactional and retires inactive native
registrations when their slots are needed. Only the current bound media set
enters frontier identities. Disc replacements and reset never grow the bound.
RiscPC returns zero. The launch chooser returns the session's nonzero bound from
load, while save and load themselves return false until the engine is running.
The AC-6 harness retains its original fixed buffers throughout the session.

**Remaining M-2 gate:** the current authored read-only ADF bound is
93,152,803 bytes. Producer-derived packed raster reservation accounts for
50,429,952 bytes; the session's pending wire bound accounts for 33,231,643 bytes.
The remaining 9,491,208 bytes cover 4 MiB RAM, measured fields, references,
identities and framing. This is a smaller conservative upper bound, **not a
proof of the smallest possible bound**. Default 20 MiB RetroArch rewind memory
still cannot hold it. Further joint-history derivation remains necessary to
satisfy the 16 MiB/minimum-bound gate. The format
continues to zero spare output bytes so the original full-buffer AC-2 byte
identity holds with arbitrary caller allocations. R-SS4d permits that clearing
and replaces the proportional-work rule with measured save/load targets.

Source caches are primed during validated load preparation, so the first public
state load does not pay immutable-media hashing costs. Size queries are constant
time and never query GET_SAVESTATE_CONTEXT, which can create another core.

The VIDC bitmap, platform framebuffer and frontend presentation history can
contain pixels that are read before a complete redraw. They remain serialized,
with per-row spans marked at every producer write. Encoding trims zero edges
and retains all interior pixels; zero pixels outside those spans are derived.
VIDC rows pack exact RGB444 channel codes, including the 13 Acorn black-level
levels per channel in `vidc_make_colour`. RGB888 UI rows and full native test
pixels retain their exact values. Uniform rows store one colour. Identical
rows within or across surfaces store backward row-copy recipes instead of
another pixel payload. Validation checks row order, extents, reference ancestry,
channel codes and padding before any installation. Referenced surfaces restore
once before their dependants, and row copies use those restored pixels. The
save-time index addresses transaction-owned raw recipes even when the container
compresses the blob. Regression tests cover that distinction, odd spans,
all 13 Acorn levels, UI colours and malformed/cyclic/forward copy attempts.
Load clears the previous spans, installs the new spans and rebuilds the marking
metadata. This preserves old pixels, interlace history, dupe frames and compact
OSK pitches without copying three 32 MiB buffers. The OSK surface itself is
scratch: its render path copies the entire active framebuffer before drawing or
reading it, and dupe paths do not read it. Its validated capacity is reconstructed.
The original last-pixel history mutants still exercise every retained buffer.

Raw media use changed 512-byte sectors against an immutable cached source.
Admission primes an open-file mirror and sparse indexes. Native writes only mark
changed sectors in every open alias; they add no native reads or writes. Capture
refreshes changed sectors and emits dirty sectors only, sorting their IDs for
canonical output. Restore reconstructs the file view from its validated source
and saved deltas, then derives indexes. No-state writes do not scan the image.
Shadow and Original preparation preserve an immutable `state-base.adf` under
the source identity's media directory; Off mode uses its read-only staged file.
The state binds the base's exact size and SHA-1. Original's durable origin ID
stays stable across acknowledged guest writes. The native HD view uses changed
512-byte sectors against its SHA-256-validated committed immutable generation
base. Ordered, unique sector indexes and exact payload bounds are validated
before reconstruction. Raw track caches are separate deltas against their
restored logical medium and projected track geometry, preserving partially
written and unflushed transfers. A base plus its delta reconstructs every byte
of the emulated medium; the base is never writable guest state.

Decoded JFD base bytes are rebuilt from the immutable prepared staging file and
checked against source SHA-1 and extent. Descriptor/track metadata, overlays,
replaced and created sectors, controller phase and pending publications remain
captured. Read-only ADFFS support/carrier bytes are likewise derived from
validated preparation. Unused media/discovery capacity is derived only where
preparation initializes it before use. Firmware digests are cached at immutable
preparation/reset, avoiding repeated ROM hashing in a frame snapshot.

The speculative namespace is reconstructed by replaying the captured ordered
operation log over unchanged native committed files into a local replacement.
It no longer duplicates every complete logical file alongside that log. Operation
payloads retain exact publication-protocol bytes, including any complete base
rewrite required by compaction; lossless word-run compression applies to them.
No native write occurs while preparing a load.

The parser retains a 1 GiB absolute ceiling. A frontend load additionally bounds
aggregate decoded records and heap ownership by its admitted session capacity.
Blob decompression waits until its visitor has checked the owner-specific maximum;
a tiny record cannot allocate its declared gigabyte before schema validation.
Derived pending file views also have an aggregate session-capacity budget;
resize and rename-copy growth are checked before allocation.
Immutable source caches, sparse-span trackers, reconstruction scratch and native
base caches have explicit inventory ownership classifications. Measured costs
and frontend compatibility evidence belong in the report.

## Store authority, speculative effects and crash recovery

Native HD frontier is **manifest generation plus validated committed journal
sequence**. Generation alone misses acknowledged append commands. JFD frontier
is the committed snapshot generation for every prepared member, including
inactive discs. Inactive probes validate the base-bound snapshot envelope and
ordered record extents; active probes also validate sector eligibility. A hot HD
probe refuses a failed store or incomplete journal tail; cold preparation owns
recovery. Every load reads native store authority
rather than trusting an emulated overlay's generation or a process-local cache.
A stale state refuses; loading itself never publishes or repairs a store.

`arc_state_io.c` provides an in-memory namespace and ordered operation log while
a frame may be speculative. Reads, stat and directory traversal see that view.
The queue records open/write/flush/remove/rename/truncate/mkdir order, including
HD commit trailers and JFD candidate/readback/rename ordering. Paths use current
logical media bindings or bounded save-root-relative names; native callbacks
and arbitrary absolute paths do not come from the blob. The ordered log is serialized and the complete view is derived from it. Restore atomically replaces both; discarded speculation
never touches native files. Publication replays the existing store protocol
and clears the queue only after success. Failure latches a refusal condition.

R-SS3a accepts the poll-callback switch in [RetroArch 1.22.2
runahead.c](https://github.com/libretro/RetroArch/blob/v1.22.2/runahead.c).
Normal polling remains stable across its permanent input-state logging hook;
comparing the state callback would misclassify real frames. After the first
successful save, null/switched polling stages fake frames, including the final
fake frame with A/V enabled. Original polling publishes a retained queue before
the next real frame and keeps that frame's guest writes eager. The stated bound
is entry to the next real retro_run, or unload/deinit, whichever occurs first.
A/V suppression and GET_SAVESTATE_CONTEXT are not commit signals. Failed publish
blocks every later native mutation and emits one warning per loaded session.

Preemptive Frames installs its fixed null poll before its first save. That save
returns false before a preempt frame can execute. RetroArch's preempt_run error
path restores callbacks through preempt_deinit; subsequent real frames and mode
changes remain eager. This mode is unsupported, using RetroArch's actual
failed-save route rather than a quirk it ignores. Native tests model initial
preempt, re-entry and normal callback restoration. The source's line numbering
differs between the web-served tagged file and QA's local copy; function-level
sequences agree. A live RetroArch execution is not claimed.

Before any frontend registration, a module compares its private-function native
module with GET_LIBRETRO_PATH's primary module (borrowed Windows handles or
balanced POSIX RTLD_NOLOAD references). A distinct copied secondary is inert at environment,
init, load, run, reset, cheat, unload and deinit boundaries: no guest, stores,
registrations or persistence. Missing primary-path support leaves ordinary
non-copy frontends usable; an ambiguous probe after a supplied path fails closed.
The distinct-module assumption does not hold on Android API 21–22, whose
loader resolves matching basenames to the existing primary module. R-SS3g
leaves this implementation unchanged and assigns alias-safe protection to the
R3d-4 integration described below.
Neither init nor the size getter queries GET_SAVESTATE_CONTEXT. Tests cover
copied modules reached by default run-ahead, cheats and failed-preempt routes,
including RiscPC and preservation of primary Disc Control callbacks.

CMOS and raw Original/shadow files use two-slot durable frontiers under
arcdroid/state-stores/. Run M replaces full-image redo with range redo. A
captured store writes a validated intent containing only the changed range,
its offset and resulting sector fingerprint, writes and flushes that range in
the target, then acknowledges the same generation. Cold preparation replays
pending ranges; hot state loading only validates, never repairs. Existing
ACFR2 full-image intent/applied/checkpoint records remain readable. ACFR3
records preserve their generations and Original-media identity.

A raw store with no capture in this session follows main's native sector path.
If a previous session left a sealed checkpoint, a durable 168-byte unsealed
epoch invalidates it **before** this session's first native write. This happens
once, including for ABA writes, and is checked before any target side effect.
No state can be captured or loaded at an unsealed epoch. Its subsequent writes
need no additional metadata I/O. Admission caches native bytes and the labelled
64-bit sector fingerprint; native writes mark touched sectors. Capture reads
only those sectors, updates the fingerprint and seals a new checkpoint. An
unchanged reopened checkpoint can still load its existing states. Once capture or a successful load
has activated protection in this session, every real write uses range redo.
The native cache, dirty index, captured/unsealed flags and generation never
rewind and are released at session reset. This fingerprint checks accidental
edits, not authentication. Applied/checkpoint bytes changed externally are
adopted at a newer frontier on cold reopen; a hot load refuses. An unsealed
epoch already invalidates all earlier states regardless of the target bytes.
Changed unsealed Original media establish their current content identity on
reopen, preventing a replacement from inheriting an earlier CMOS/base binding.

Original-media guest writes change the source file's hash. Its first identity
is therefore retained with the durable frontier across its own acknowledged
writes; this also stabilizes its CMOS/content binding on cold reopen. An
external replacement advances the frontier and establishes a new identity.
The replay observer updates the CMOS last-written marker only from the exact
after-image whose applied acknowledgment is durable, preventing duplicate
publication after restore. Recovery occurs before preparing a raw source. This
behavior is a material
storage consequence and is called out for review in the report.

The HD/JFD tests kill the process with SIGKILL at publication boundaries, cold
reopen through libretro, validate whole committed commands and check that a
second reopen is idempotent. The raw/CMOS frontier test also injects failures
at each mutation step and accepts only a complete old or complete new sector.
Fixtures are authored; no ROM, title or private library bytes are included.

## Publication resource limits and frontend compatibility

The ordered speculative operation log retains its aggregate ownership ceiling
of 32 MiB and existing per-file speculative-view ceiling. Each operation
is charged its native allocation, payload and sufficient packed-wire overhead;
the container header is reserved too. The serialized reservation additionally
derives from the session's largest protocol write: admitted raw extents,
helper/carrier extents, the JFD store's 9 MiB snapshot cap when present, and the
native IDE image when attached. Generated logical store paths fit 256 bytes.
For ownership charge `C = sizeof(StateOperation) + 32`, write limit `W` and
path limit `P`, the maximum wire/ownership ratio is the larger of
`(40+P+W)/(C+W)` and `(48+2*P)/C` (rename). Applying it to the unchanged queue
ceiling and adding the 16-byte log header yields the session wire bound, capped
by the existing 32 MiB limit. Capture and restore enforce that bound. It survives
publication and root rebinding; a new session configures its own bound.
Restore validates the ownership budget before allocating each operation and
reconstructs its exact remaining capacity. This does not assume a one-frame
publication window: a frontend can request arbitrarily many speculative frames
before restoring the real poll callback. Per-frame limits alone cannot bound
that lifetime. Exhaustion follows the
existing staged-I/O failure path: no speculative native write, a refused
snapshot, and safe cleanup or restoration of a valid earlier queue. The unit
fills the budget twice around a restore and proves identical exhaustion points,
zero native mutations and complete cleanup. The per-file 32 MiB and 256-view
limits remain. Native eager publication is unaffected.

RetroArch 1.22.2 allocates its [run-ahead buffer once](https://github.com/libretro/RetroArch/blob/v1.22.2/runahead.c#L949-L983),
and its [rewind block size is likewise fixed](https://github.com/libretro/RetroArch/blob/v1.22.2/state_manager.c#L547-L567).
The [rewind/save wrapper](https://github.com/libretro/RetroArch/blob/v1.22.2/tasks/task_save.c#L320-L440)
uses ordinary core serialization and passes the advertised core-block length;
rewind rejects growth beyond its allocated block. The stable capacity directly
supports those paths. The variable-buffer harness
workaround and the test-only 2 MiB override are removed; every shipping profile
has 4 MiB. The engine's 2 MiB mapping is outside this slice.

Only a successful special-context snapshot arms the input-callback boundary.
An undersized failed snapshot leaves the preceding evidence intact, so a frontend
abandoning run-ahead cannot leave known-real writes indefinitely staged.

### Run H RPC census review

The RPC lockstep census retains every existing frontend object and adds twelve
explicit SS-1 owner classifications (mirrored in the independent reviewed policy).
The presentation-row index belongs to Arculator's retained raster; the reentrancy
latch, admitted capacity/media budget, publication warning, immutable firmware
cache/validity, normal poll identity, successful-capture latch and HD/JFD committed
frontiers belong to the Arculator state boundary. RPC states remain size zero and
refuse save/load. Its engine, media and persistent generations continue through
the existing RPC comparisons. The secondary-module latch gates admission before
registrations, outside admitted primary lockstep execution; the production host
frontend tests exercise copied-secondary rejection explicitly. No RPC engine
member, existing comparison or census source is excluded.

The public retro_run wrapper now owns the secondary and reentrancy checks;
arc_run owns the unchanged RPC session-kind policy. The source guard pins both
owners. All 1,508 existing launch mutation IDs and equivalences remain, rebound
within their original function (retro_run's policy body is now arc_run); spacing
changes do not alter their expressions' semantics. This is a reviewed source-pin
refresh, not a regenerated mutation exclusion list.

Native directory entries with a null name must remain entries with an invalid
name through the speculative VFS. They are not EOF: firmware inventory must
report an unreadable inventory. StateDirectory.name_missing is transient native
iteration metadata, never serialized; close releases its owner. JFD fault probes
name the JFD store directory so unrelated CMOS frontier temporary files cannot
consume their intended publication failure.

Linux's independent process heap oracle uses mallinfo2.uordblks, which includes
freed chunks retained in glibc's thread cache. The harness driver disables that
cache before child startup via glibc.malloc.tcache_count=0, preserving unrelated
tunables. Exact process bytes and allocation-census bytes/blocks still have zero
tolerance, and the actual malloc/calloc/realloc/realpath/dropped-free mutants are
retained. The CI symptom was six equal-sized early increases followed by a flat
heap while the intercepted live census stayed zero. This diagnoses allocator
retention; local Mac evidence cannot claim a native Linux re-test. See the
[glibc tunable contract](https://sourceware.org/glibc/manual/2.30/html_node/Memory-Allocation-Tunables.html).

### R-SS3g integration dependency (replaces R-SS3f)

A runtime size-zero switch alone cannot supply the required aliased-module
protection in RetroArch 1.22.2. Its tagged
[core_set_cheat/core_reset_cheat](https://github.com/libretro/RetroArch/blob/v1.22.2/runloop.c)
call secondary_core_ensure_exists when the run-ahead settings and availability
flags permit it, without checking the known serialized size. The tagged
[secondary/preempt code](https://github.com/libretro/RetroArch/blob/v1.22.2/runahead.c)
creates that module directly. A failed preempt_allocate on size zero calls
preempt_deinit, which restores both availability flags. Thus the counterexample
is: size-zero primary; failed preempt initialization; secondary-instance
run-ahead settings enabled; cheat set or reset. The secondary loader is reached
before any successful serialization. A cheat before initial run-ahead sizing is
another route while the initial availability flags remain set. Ordinary
runahead_create does reject size zero, but that does not guard these independent
entry points.

On API 21–22 the loader may alias the same-basename copied library to the primary
module. The existing distinct-module check cannot detect that alias. R-SS3g
replaces the API-level switch: **no Android API-level switch is added**.
Fable reports that R3d-4's R-1a has passed QA: it refuses a load over a live
session before effects and suppresses nested lifecycle calls when a different
environment callback arrives during that session, consuming the matching
deinit. After R3d-4 merges, SS-1 must extend that same guard to both gate states
and every Archimedes load. It must not copy or reimplement the guard ahead of
that integration. Existing distinct-module handling remains unchanged here.
Native copied-module RPC tests already exercise a primary
whose actual retro_serialize_size is zero, followed by secondary lifecycle entry;
only distinct-module rejection prevents its side effects. The earlier same-handle
native diagnostic remains a failing registration check, not an acceptance pass.
The future R-1a integration, rather than size zero or an Android version policy,
must close those legacy Android routes. Its QA result is attributed to Fable;
this continuation does not claim a local test of unmerged R3d-4 code.

The Founder adopted the raw-frontier interpretation: stores with no state
history use the native main IO path until their first capture. Canonical spare
buffer clearing is permitted by R-SS4d; it is no longer a proportional-work
failure. The 16 MiB capacity and save/load cost targets still apply independently.

### R-SS4d producer-limit audit (still open)

The geometry constant is not used as proof of an engine limit. The producer
itself reaches the same order of storage:

* `writevidc` in `engine/arculator/src/vidc.c` masks horizontal total to ten
  bits. `vidc_poll` uses `(htot + 1) * 2`, up to 2,048 clocks; its 8/12 MHz
  pixel paths double horizontal positions and write paired pixels, reaching
  the 4,096-wide backing surface. Fast paths write independent pixels at up
  to 2,048 positions. Current control bits cannot describe older rows retained
  across mode changes.
* The vertical register is ten bits plus one. The draw guard admits line 1,023;
  interlace selects `l * 2 + oddeven`, reaching backing row 2,047. These are
  alternating fields, not two identical rows. `arc_machine_create` fixes
  `dblscan=1` and the standard/SVGA monitor from the machine descriptor; that
  does not disable guest-controlled interlace or horizontal pixel-rate changes.
* `video_renderer_update` clips against the engine bitmap and platform buffer.
  `video_renderer_present` performs a separate in-place line duplication.
  This duplication is derivable if its source and last-copy recipe are retained.
  The platform image can otherwise precede the partially redrawn VIDC bitmap.
  `arc_send_video` then retains the last presented image, potentially with an
  OSK overlay; a boot-prompt transition can re-use that preceding image. The
  three arrays are not three independent full screens in ordinary frames, but
  neither are they unconditionally functions of the current VIDC registers/RAM.

A fast interlaced retained raster alone needed about 16 MiB in the former
32-bit literal-span representation, before 4 MiB RAM, framing, dirty media or
pending effects. This is a lower reservation for **that representation**, not
an information-theoretic lower bound for a redesigned palette/history codec.
The former 137.10 MiB bound is therefore not fixed by substituting ordinary
640x480 geometry, and an arbitrary 16 MiB cap would lose state. The new codec
packs historical colours and derives identical rows, reducing the read-only ADF
reservation to 88.84 MiB. The pending reservation now accounts for session
protocol writes and existing queue ownership, without inventing a finite
speculative-frame window. This still does not establish a minimum joint bound
for all retained images. R-SS4d's remaining work is stronger platform/presentation
derivation (including crop/overlay history) and a minimum-bound proof or a
representation that meets 16 MiB. The target has not been declared impossible.

## Run I: end-of-frame publication and native picture histories

Design investigation only, against `d706d15` (checkpoint `7b7b528` plus `main`).
**The pending-log reservation can be reduced, but this investigation has not
proved a complete representation at or below 16 MiB.** Native pixels alone do
not establish that bound. The missing joint-history proof is explicit below;
the favourable single-picture calculation must not become an advertised
capacity without it. D-214 remains binding. This section stops for architecture
challenge; it does not authorize implementation or relax acceptance criteria.
Local file:line references refer to that head; upstream links pin `v1.22.2`.

### (a) Saved queue versus live speculative queue

RetroArch's single-instance sequence runs real frame zero, saves it, runs the
fake frames and finally restores frame zero
([runahead.c:1104-1150](https://github.com/libretro/RetroArch/blob/v1.22.2/runahead.c#L1104-L1150)).
The fake-frame helper installs null polling and retained input, runs once, then
restores callbacks before returning
([runahead.c:1018-1035](https://github.com/libretro/RetroArch/blob/v1.22.2/runahead.c#L1018-L1035)).
There is no save between fake frames. The special save wrapper disables
run-ahead on failure (`runahead.c:941-983`). Preemptive Frames installs null
polling before its first save (`1356-1359`) and remains refused by ArcDroid.
Secondary isolation remains the independent R-SS3g integration obligation.

Rewind captures initial state and forward iterations at its granularity;
reverse iterations load states
([state_manager.c:547-569](https://github.com/libretro/RetroArch/blob/v1.22.2/state_manager.c#L547-L569),
[615-733](https://github.com/libretro/RetroArch/blob/v1.22.2/state_manager.c#L615-L733)).
Its check is before the iteration's core execution
([runloop.c:5952](https://github.com/libretro/RetroArch/blob/v1.22.2/runloop.c#L5952),
[6716](https://github.com/libretro/RetroArch/blob/v1.22.2/runloop.c#L6716),
[6819-6829](https://github.com/libretro/RetroArch/blob/v1.22.2/runloop.c#L6819-L6829)).
It therefore captures initial state or the preceding completed iteration,
including the restored real state after successful single-instance run-ahead.

**Inference:** every save in those successful sequences can have an empty
operation log, independent of the number of fake frames. This is a saved-state
bound, not a live-queue bound. Current real frames already use eager I/O
(`engine/state-generated/boundary.inc:42-47`). Raw tracks flush before engine
return (`engine/arculator_backend.c:1175`); CMOS persists before frontend return
(`libretro/arcdroid_libretro.c:8990`). The smallest change retains eager real
writes and enforces an empty-log postcondition at public frame return. If real
effects are instead staged, replay them after **all** effects, before that
return, including early exits, CMOS, boot records and helper/media changes.
Use the common `retro_run` wrapper (`arcdroid_libretro.c:9034-9038`), not VIDC
field completion: a call can return with an incomplete field. Do not make
no-state sector writes allocate a new log or copy whole images.

Normal snapshots need only the existing two-u64 empty header: **16 B**.
However, an empty-only codec would break existing coverage: the differential
harness deliberately stages CMOS, raw, HD and JFD effects before saving
(`tests/arculator_state_harness.inc:280-304`); the pending-effects field-drop
mutant depends on a nonempty saved log (`tests/arculator_state_mutants.py:41`).
Preserve a bounded nonempty-log codec. Proposed fixed wire reservation:
**65,536 B including its header, plus a 24-byte record = 65,560 B**. This is
headroom for unusual out-of-run captures, not a claimed bound on fake-frame
production. Validate the existing fixtures against it before accepting this
choice; their continued success is required, not assumed. The successful
RetroArch sequences above use just the empty header.

Before capture work, frontier checkpoint work or caller-buffer writes, count
the exact encoded log size; refuse if it exceeds that reservation or publication
has failed. Count paths and operations with checked arithmetic, without first
allocating an unbounded encoded blob. Load validates the wire and aggregate
ownership/view limits before replacement allocations, then atomically replaces
the live speculative queue. Current layout/restore:
`libretro/arc_state_io.c:482-526`. The runtime queue keeps its 32 MiB ceiling
and ordered failure latch; it can span arbitrarily many fake frames. Refusal
preserves guest state, queue, native files and caller buffer. Never truncate
the log or publish speculation to make it fit.

**Normal polling alone does not prove finality.** The fake-frame helper
restores it before the final load. A save in that gap can contain staged
speculative effects: retain them exactly if within 64 KiB, otherwise refuse.
If that load fails, normal polling must not cause those effects to
publish at the next real call or unload. A revised boundary needs explicit
real/speculative provenance and a failure transition; moving publication to
frame end does not make a fake frame real. Challenge the existing entry/unload
rule (`boundary.inc:30-47`) on this case. Failed publication keeps blocking
later native mutations; no replay from the start after partial application
(`arc_state_io.c:327-348`). The supported normal path must always meet the
empty-queue invariant: rewind pushes its buffer without checking serialization
success (`state_manager.c:729-733`), so safe refusal alone does not prove usable
rewind. Never expose a partially written new blob on failure.

### (b) Native picture data and producer limits

The profiles fix normal-machine operation, no-borders presentation,
`dblscan=1` and the descriptor's monitor
(`engine/arculator_backend.c:932-940`). Guest depth, rate, palette, extents and
interlace remain programmable. VIDC describes 1/2/4/8 bpp and four rates
(`engine/arculator/src/vidc.c:47-56`); horizontal total is ten bits and vertical
total ten bits plus one (`459-474`). Both interlace fields address the
4,096 by 2,048 allocation (`588,779-786,1361-1369`). Neither ordinary screen
geometry nor current registers bound historical rows.

For a *new homogeneous display extent*, slow rates emit paired host pixels,
fast rates independent ones. The nominal active-sample envelope is:

| Depth | Slow 8/12 MHz paths in vidc.c | Fast 16/24 MHz paths | Bytes for 2,048 samples by 2,048 rows |
| --- | --- | --- | ---: |
| 1 bpp | control 0/1, lines 881-893 | 2/3, 895-922 | 524,288 |
| 2 bpp | 4/5, 924-937 | 6/7, 938-951 | 1,048,576 |
| 4 bpp | 8/9, 952-965 | 10/11, 966-978 | 2,097,152 |
| 8 bpp | 12/13, 979-993 | 14/15, 994-1009 | 4,194,304 |

This table is **not a maximum for retained histories**: it excludes fills,
cursor, fetch overhang and old tails. Interlace needs both fields. Platform
line doubling is a separate, derivable copy
(`platform/arcdroid_video.c:85-108`). Dormant mono rendering forces mode 2 and
uses monochrome lookup/cursor colours (`vidc.c:776-777,902-911,1087-1104`);
it is not an admitted monitor in these profiles. LCD remains outside them.

A native draw fragment would retain packed samples **when read by the
producer**, historical palette, black-level selector, depth/rate, expansion
rule and coordinates, border/blank fills, and cursor words/position/colours.
Twenty RGB444 palette registers take 40 bytes; 8-bpp's 256-entry palette is
derived from the historical low 16 registers (`vidc.c:391-410`). Reconstruct
with the exact integer `vidc_make_colour` conversion, including Acorn black
levels (`358-373`). Neither current RAM nor current palette recovers old pixels.
Palette writes occur between whole-line producer invocations (`431-455,765-775`),
so capture per invocation, not once per picture. Display indices cannot also
encode all border/cursor colours in 256 slots (`1012-1184`); use fill/overlay
records. Mono and word-boundary writes need their own exact treatment.

For comparison, a proposed clean-row layout with 96 bytes of metadata (40
palette, 8 cursor words, 48 coordinates/flags/fills) adds 196,608 bytes per
2,048-row picture: **4,390,912 B (4.1875 MiB)** at 8 bpp. This does not
establish the largest representation over all producer modes and sequences:

* Partial writes preserve old tails. A fast row can overwrite the left half
  of an old doubled slow row and leave its right half (`vidc.c:854-873,
  979-1009,1044-1081`). Roughly 2,048 fresh indices plus 1,024 old paired
  indices per row would mean **6,291,456 index bytes** at 2,048 rows, before
  historical palettes. This is a counterexample to the layout's assumed
  sample count, not an executed-guest reachability result from this run.
* Fetch loops test x before emitting a full word; the slow 1-bpp path advances
  x by 32 while emitting 64 positions (`vidc.c:883-889,897-916,981-1004`).
  A codec must account for every emitted position and existing word/row
  boundary behaviour, rather than assume disjoint, exactly clipped samples.
  This note authorizes no engine correction.
* Multiple palette/depth epochs can remain in one row. Historical fragments
  preserve them, but fragment counts and palette bytes need their own bound.
  Compression cannot guarantee small incompressible data. Literal-colour
  escape is safe but restores the large reservation.

### Joint histories and exact copy/overlay recipes

The stronger candidate shares immutable native draw fragments between engine,
platform and presentation spans, retaining source versions **at copy time**.
Record actual clipped platform updates and in-place line-double copies in
execution order (`arcdroid_video.c:63-108`); normalize recipe chains and collect
unreferenced versions. Save traverses the live graph in canonical owner/row/span
order, not allocation-ID order. This avoids independent converted copies and
the current save-time search over pairs of rows.

Last presentation additionally needs its historical dimensions/pitch and
rendering inputs. OSK blends with integer rounding (`core/osk.c:642-665,
687-734`); reconstruct over the same source with the OSK/layout/header state
used **then**, not current input state. The OSK scratch pixels are fully copied
before rendering (`arcdroid_libretro.c:8531-8546`), so need no independent
payload. UI/chooser output requires exact rendering inputs or literal escape;
RGB444 conversion would lose UI colours.

The outstanding proof is the maximum **simultaneously live** source data.
CPU quanta and incomplete fields let the engine precede the platform
(`arculator_backend.c:1149-1182`); post-copy clears separate them again
(`vidc.c:1352-1357`). Boot-prompt handling reuses the preceding presentation
(`arcdroid_libretro.c:8895-8906`), possibly with an old OSK overlay. These
versions cannot be deleted just because current RAM/registers changed.
Omitting inactive tails requires a read-before-write proof for every crop,
clipped update, pitch change, line-double and prompt path. Current spans do
not supply it. No one-picture or bounded-fragment-count invariant is proved.

Load must validate graph order, extents, palette selectors, padding, owner
limits and aggregate allocation before rebuilding converted copies and
committing them transactionally. Malformed graphs and allocation failures
leave the running state and stores untouched. Shared storage must not make
logical-owner field-drop mutants ineffective.

### Component arithmetic and remaining proof obligation

Run H's inspected completion report supplies the following Mac read-only ADF
ledger; these are attributed measurements, not new results. The formula is at
`libretro/arcdroid_libretro.c:9048-9094`.

| Component | Current B | Proposed treatment |
| --- | ---: | --- |
| 4 MiB RAM and size | 4,194,320 | Preserve |
| Other fields, references, identities, framing and admitted media | 5,296,888 | Preserve until independently derived |
| Pending wire payload | 33,231,643 | 65,536 plus conservative 24-byte record allowance |
| Three retained pictures | 50,429,952 | Joint native graph; maximum **unproved** |
| Total | 93,152,803 | **9,556,768 + joint-picture bytes** |

The full picture graph must fit **7,220,448 B** to meet 16,777,216 B with the
remaining schema. Even the favourable clean-row comparison gives:

| Independent clean 8-bpp pictures, including 96 B/row | Total state B | MiB |
| --- | ---: | ---: |
| One | 13,947,680 | 13.3015 |
| Two | 18,338,592 | 17.4890 |
| Three | 22,729,504 | 21.6765 |

Only the first row meets 16 MiB; one picture is unproved, and mixed historical
tails can enlarge it. These are candidate-layout sizes, not lower bounds on
every possible representation. They neither authorize the first row nor
establish that D-214 is impossible.

Writable raw media shrink the budget further: L bytes reserve
`L + 8*ceil(L/512) + 32` (`arcdroid_libretro.c:9050-9056`). L=819,200 adds
832,000 bytes beyond the read-only case's 32-byte media allowance. Preserve
dirty sectors and unfinished transfers. JFD also needs actual admitted
descriptor/replacement/created-sector bounds. Its current formula charges an
8 MiB decoded maximum plus maximum descriptors and created sectors
(`core/jfd.h:10-16`, `arcdroid_libretro.c:9057-9059`). The immutable decoded
base is already derived (`engine/state-generated/frontend_extra.inc:33-41`);
re-derive that reservation while retaining every mutable replacement/creation
(`83-108`). A read-only ADF number cannot prove the bound for writable ADL,
JFD, ADFFS or every folder/playlist session.

Another candidate saving is derived frontend scratch: `arc_frame` alone is
640*480*4 = 1,228,800 bytes and is serialized (`arcdroid_libretro.c:53-54,98`,
`arcdroid_libretro_state.inc:155-156`). Its uses require a separate proof before
omission. Batching scalar records could save framing, but no such reduction is
credited here without a measured maximum per target ABI.

RetroArch adds 24 bytes, alignment and optional achievement data around the
core block
([task_save.c:319-440](https://github.com/libretro/RetroArch/blob/v1.22.2/tasks/task_save.c#L319-L440)).
Its maximum rewind patch adds approximately four bytes per 131,070 bytes plus
pointer/terminator overhead (`state_manager.c:159-168,364-366`). A 16 MiB core
block fits the ordinary 20,000,000-byte configuration with overhead; actual
initialization, repeated capture and reverse playback still need verification.
A small exact payload inside the old advertised block cannot satisfy D-214.

### Cost, risks and verification for a later authorized implementation

The smaller queue reservation removes approximately 33.2 MB of capacity and canonical
clearing. Retaining eager real writes avoids new no-state I/O. Staging those
writes instead adds allocation/copy/replay cost and needs another sector-cost
comparison. Runtime speculative memory remains independently bounded.
Native fragments move packing/history retention to the producer; save becomes
live-graph traversal, payload copies, CRC and canonical clearing. Load must
expand samples and replay historical copies/UI. Three full converted surfaces
alone contain **100,663,296 B**: writing them in 5 ms needs over **20.1 GB/s**
before RAM restore, validation and decoding. Restore only proved live extents
and derived copies where safe. Smaller wire size proves neither the 10 ms save
nor 5 ms warm-load target. Run H's ordinary authored-frame cost table does not
measure this proposed codec or extreme histories. Android cost remains owed.

Required evidence, without weakening existing tests:

1. Keep differential/save-load-save, field-drop and refusal coverage (58 pairs,
   35 mutants and 38 public refusals in the inspected run-H report). Add all
   16 depth/rate control nibbles, maximum extents, both field parities, depth,
   palette and rate changes on retained rows, black levels, cursor/border,
   blanking, word overhang, partial fields and post-copy clear. Compare
   observable pixels with the existing producer, not a duplicate new codec.
2. Model the tagged run-ahead/rewind sequence through `retro_*` with CMOS/media
   effects in real and fake frames. Assert frontiers/native bytes and empty
   queues at each normal save. Test mode changes, unload/deinit, the restored-
   callback/before-load gap, failed final load, failed ordered publication and
   reentrant/oversize-queue saves. Test exact 64 KiB and one-byte-over boundaries,
   plus every existing nonempty-queue differential/mutant fixture unchanged.
   Refusals preserve the caller buffer and
   never publish speculation. Retain preemptive/secondary lifecycle checks.
3. Cover clipped crop/doubling, compact/platform pitches, OSK transparency,
   historical versus current OSK state, dupe/no-dupe, chooser and prompt reuse.
   Stress version collection over long mode sequences; prove its ceiling.
   Malformed graph/palette/length/padding and allocation failure must preserve
   the session without large allocations from tiny blobs.
4. Produce a maximum component ledger for every profile and floppy class,
   including all-dirty media and maximal admitted JFD mutations. Measure Mac
   Release save/first-load/warm-load and producer overhead, then perform the
   owed Windows/Android/default-RetroArch evidence on the accepted final head.
   Successful small fixtures alone do not bound the session.

**Question for architecture challenge:** can shared native fragments plus
exact copy/overlay recipes be proved to fit 7,220,448 B, less writable-media
growth, or can separately proved schema derivations release enough space?
This run has not supplied a complete all-mode 16 MiB representation and does
not claim one impossible. D-214, R-SS3g and cost gates stay open. No production
code or tests changed, and no behavioural suite was run for this design note.


## Run K design as built

D-215 keeps CPU, RAM, DMA/interrupt phase, audio, transfers, storage, input and
historical mapping geometry exact. Host picture content is rebuilt, with no
one-field convergence promise. Run I above remains the challenged investigation.
The Run K section of `SOL-REPORT-SS1.md` records component/media ledgers, costs,
per-case picture differences and completed verification.

`boundary.inc` and the outer `retro_run()` finish real storage effects on every
return. A speculative provenance latch survives callback restoration; successful
rollback clears it, while failed rollback blocks execution/publication until
reset or reload. Unload discards speculation. Capture refuses with that latch
set. Checked operation-log preflight enforces 65,536 encoded bytes before capture
changes frontiers or output; execution may stage more and then refuse capture.
RetroArch 1.22.2 saves after its real run-ahead frame and before speculation;
rewind captures the prior completed iteration (pinned upstream evidence in Run I).

Engine, platform and frontend picture payloads are absent from the wire.
`arc_vidc_rebuild_picture()` reads saved RAM and VIDC/MEMC inputs with local
addresses, advancing no guest address, timer, DMA, interrupt or palette state.
It rebuilds both parities. Platform sequence, historical width/height/pitch,
OSK and input stay exact; load renders the restored OSK without update/layout
calls. Frontend frame/audio scratch derives from retained inputs; audio queues,
filters and sample fractions remain saved. An existing negative signed border
offset shift found by the expanded sanitizer matrix uses bounded multiplication
by two, preserving ordinary scanout arithmetic. Its low-rate 1-bpp loop also bounds
shifts to the word, retaining the existing 32-pixel host scanout.

Immutable CPU count-bit/rotate tables rebuild in a pure commit callback; an
independent oracle checks every entry. Six actual 64-bit timings retain three
uniform memory banks, including historical I/O timings; a nonuniform bank refuses
capture. No CPU reset or device timing routine runs. Writable-ADL discovery lists
stage saved candidate counts before traversing the saved candidates.

JFD source, descriptors, eligibility and creation masks derive from validated
immutable media, using prepared caches on warm loads and parsing on cold loads.
All replacement/created bytes remain in an ordered bitmap payload. Each disc
retains its actual eligible-byte/creation-slot envelope across lazy release;
only one mutable image is active. Native bindings reuse validated source digests.
Cold-created-sector tests release the graph, restore all bytes/canonical state,
and publish once; a forged forbidden slot refuses without execution/publication.
Serialized creation masks must agree with the source. Failed graph construction
stops before field traversal; transactional rollback retires prepared ownership.

The media-independent bound for each 4 MiB profile is **7,356,448 B**:
2,767,128 static + 4,194,320 RAM + 262,848 references + 66,560 metadata +
65,560 log + 32 framing. Metadata includes the remaining RAM record framing.
Raw writable media adds `N + 8*ceil(N/512)`. JFD adds
`65,536 + 16 + ceil((sector_count+800)/8) + eligible_bytes + 1024*creation_capacity`;
48,695 B of maximal field/heap/base/store/payload metadata fits that envelope.
Reserve the largest active raw/JFD graph plus 96 B per inactive JFD, rather
than their sum. The parser/planner maxima (32 discs, 42,496 descriptors,
8 MiB disjoint eligible payload and 800 creations) give **16,638,196 B**,
below 16 MiB. A four-profile regression checks the conservative arithmetic.
Hard-disc image/index reservations are additional and reported separately.

Wire/decoded bytes use the session capacity; reconstructed ownership has
`192 MiB + 2*capacity`, and transaction scratch `128 MiB + 4*capacity`.
Geometry/media/log limits still apply; a 32 MiB derived OSK restores from a
sub-16 MiB wire buffer. Empty logs clear transactionally; nonempty replacement
tables allocate only the validated file count (at most 256), enforce it during
construction, and require an exact count. Rollback precedes scratch destruction.
HD deltas stage only saved/live dirty sectors against the validated immutable
base, including restoring sectors absent from the saved delta. All post-open
writes maintain a derived dirty bitmap; base replacement rebuilds it and load
replaces it transactionally. The bitmap adds no wire bytes or frontier authority.

The differential oracle compares full serialized guest state, CPU and PCM per
frame and counts pictures separately. Coverage includes all depth/rate controls,
both saved interlace parities, mode/cursor/border transitions, partial fields,
repeated loads and one-frame run-ahead. Only `raster`, `platform-video` and
`frontend-video` leave the field-drop set (35 → 32). Default-buffer tests model
RetroArch's wrapper and worst-patch initialization at 20,000,000 bytes and
exercise public state APIs with an independent reverse-patch test encoding.

## Run L lifecycle integration as built

R-SS3g extends the single R3d-4 R-1a implementation to both gate states and all
content. A live session (including an admitted preparation plan) rejects another
load before cleanup, preparation, store access or frontend registration. A
different environment callback during that session marks a nested lifecycle,
retains the primary callback and ignores nested init, callback setters, reset,
run, unload and deinit. The matching deinit consumes the marker. This covers
API 21–22 module aliases without an Android API switch. A restored primary
environment also clears the marker; direct live-load refusal does not set it.
Where GET_LIBRETRO_PATH is answered, native module identity refuses distinct
copies before registration. Without that optional answer, live-session and alias
protection still applies; distinct copied-module identification is conditional
on frontend support. Neither state sizing nor initialization queries the
side-effecting GET_SAVESTATE_CONTEXT callback.

The primary callback sequence is content load followed by callback installation.
The first post-load input poll binds the real-frame identity if load had no poll;
later speculative callback swaps cannot replace it. R-SS3d/R-SS3e and the Run K
publication, rollback and exact guest-state rules remain unchanged. No wire
fields are added: alias/admission authority is host-only and never rewound.

`arculator-state-secondary-routes` uses shipping retro_* exports in each gate
state. It models run-ahead create, failed-preempt re-entry after a refused save,
and primary cheat set/reset followed by secondary creation. It tests independent
same-basename copies and actual same-module aliases; aliases also run with
GET_LIBRETRO_PATH unavailable. Additional cheat set/reset attempts run before
any successful capture, so isolation cannot depend on state-history setup.
Each failed create leaves all writable-open, frontend-registration, notice and
presentation counters unchanged, keeps Disc Control callbacks and permits the
next primary frame. Cases taken after capture also preserve canonical primary
state byte-for-byte. Cold cheat cases first capture and restore only after their
attempts finish. The primary save works with RetroArch's callback order.
The ordinary RPC harness checks zero size, serialize/unserialize refusal and
untouched output at every load and frame, including 200 Arc↔RPC switches; the
plan harness retains its pending/running/reset/switch coverage.

AC-L2 deliberately changes closed-gate identity expectations for live
replacement and secondary attempts. A refused replacement preserves the primary;
legitimate replacement explicitly unloads first, retaining exact outgoing CMOS
publication and its independent 256-byte oracle/removal mutant. These two rules
are now shipping behavior and leave R-6a's gate-open-only differences. The `.info`
notes state both. The remaining R-6/R-6a differences retain their original scope.

The public route test also caught a raw-file cursor defect after the first
checkpoint: a durable sector redo rewrote the whole native image and left its
native cursor at EOF, although the guest write ended earlier. The next save's
metadata then differed from a repeated capture. Publication now seeks that
stream back to the logical guest position after the redo. No-history ordinary
writes keep their original path. The test requires immediate capture→capture
identity before and after secondary attempts as well as continued primary play.

The Run L caller sequences were checked against the pinned primary sources:
[secondary creation and failure cleanup](https://github.com/libretro/RetroArch/blob/v1.22.2/runahead.c#L391-L493),
[preempt callback overrides and availability restoration](https://github.com/libretro/RetroArch/blob/v1.22.2/runahead.c#L1292-L1370),
and [primary cheat calls followed by secondary creation](https://github.com/libretro/RetroArch/blob/v1.22.2/runloop.c#L7164-L7231).
These are authored interface models, not a claim of a live RetroArch device run.

## Run M frontier design as built

ACFR3 keeps the existing 160-byte checksummed header. Phases 1/2 have a
16-byte range prefix (offset and resulting fingerprint), followed by the exact
write payload; the header records resulting extent and whether this was a
truncating open. Phases 3/4 are 168-byte sealed/unsealed checkpoints. Parsing
checks phase-specific lengths, offset/range/extent, truncation and both CRCs
before recovery. The two slots and candidate readback/rename ordering remain.
A fixed-size 512-byte write therefore reads 2 × (160 + 16 + 512) = **1,376 B**
and writes those two records plus 512 = **1,888 B**, for either an 819,200-byte
or 1,638,400-byte image. It touches only affected 512-byte fingerprint leaves;
there is no image allocation, copy, scan or rewrite on that path. Resize and
truncation use their actual resulting extents. The native stream ends at the
guest's logical position. A staged truncation also removes the old native tail.

Uncaptured writes are 0 B read / 512 B written, with one additional 168 B read
and written on the first write that invalidates a previous session's sealed
checkpoint. First-batch cost and I/O include that invalidation; steady write
cost is compared with main's unchanged native I/O functions. Capture and
frontier publication visit only changed sectors (plus fixed metadata); repeated
capture after range writes uses the already updated native cache. Cold reopen
and hot load still validate the entire native image to detect external edits.
Their work is outside the guest-sector-write path.

The tests cover fresh/post-capture/reused-folder writes on both image sizes,
a second process using the same folder, exact I/O counts, legacy ACFR2 recovery,
ABA refusal across reopen, six invalidation cases and eleven
range-publication cases, truncation, and existing actual SIGKILL
publication checks. Failed rollback is tested with restored and fake polling,
and with early null/zero-size refusals followed by a valid retry. These latter
calls deliberately test the public API's fail-closed policy beyond RetroArch's
normal error recovery.

QA mutant M6 (remove the outer finish) has no distinguishing RetroArch 1.22.2
sequence: real writes are eager; a restored exceptional queue is drained by
`arc_state_boundary_frame` at real-frame entry; fake frames retain speculation
until successful rollback replaces the queue. No real-frame body stages a
queue. Thus every real exit already has an empty queue; the outer finish is a
defence for future paths, retained and source-guarded. In [RetroArch 1.22.2
runahead.c](https://github.com/libretro/RetroArch/blob/v1.22.2/runahead.c),
`runahead_core_run_use_last_input` restores callbacks before rollback;
`runahead_load_state` sends failure through `runahead_err`, disabling run-ahead
and removing its hooks. The new fake-poll and early-refusal tests pin M7 and M11
even for a frontend that attempts further calls after failure.

Speculative raw writes use a sparse namespace view: the already admitted native
cache supplies unchanged bytes and the existing ordered log supplies changed
ranges. A sector write neither reads nor copies the image; subsequent reads
copy only their requested span and overlay matching logged writes in order.
Files without a coherent authority cache defer native reads to the requested
read spans; opening their staged view queries size without reading the image.
The wire format remains the operation log. Loads can reconstruct ordinary owned
views, preserving canonical save/load/save bytes and the existing ownership
limits. Rename and resize materialize only when those operations need an owned
view. Overlays remain correct after a failed partial publication because replay
of the complete retained log over its committed prefix is idempotent. Reset
clears views before releasing the native caches. The public write-path check
runs fifteen save/fake-frame/rollback samples for each image size and checks
zero native bytes read or written by the speculative write batch. Unit tests
cover overlapping and unaligned ranges, complete readback, canonical restoration,
rename, resize, truncating open and discard. These tests also exposed and fixed
the staged/publication VFS truncate result check: the vendored libretro contract
returns zero on success, rather than the requested length.

The Run K frontend A/B fixture deliberately changes only its two host frontier
records from ACFR2 to ACFR3. Run M's supplemental comparison validates both
checksums, generations, phases, identities, full extents, truncation, fingerprints
and every 256-byte CMOS payload byte in both slots. All other persisted files
and frontend observations remain byte-identical; no file is excluded. Current
closed/open gate comparisons retain exact file-byte equality. The Run M report
records both timing windows and leaves warm load ≤5 ms open on the shared Mac.

## Run N integration: AY-1 calendar census

The AY-1 overlay has three additional serialized CMOS objects:
`year_original[3]` retains the pre-overlay year/checksum bytes,
`year_written` retains the three guest-write bits (even an equal-value write
changes persistence), and `year_overlay` retains activation. None is recoverable
from the live CMOS image alone. The frontend's `arc_cmos_session_year_valid`
and `arc_cmos_session_year[2]` are serialized too: stopped-engine reset/recovery
uses that historical cache, and canonical saves must preserve its exact bytes.
These five objects add 14 payload bytes plus five 24-byte record headers,
so the advertised bound grows by 134 bytes.

The immutable `year_raw[3]` address table is host-only. The test-only
`test_clock[7]` is an external wall-clock input to construction; the active
virtual RTC remains serialized as `systemtime`, while loading must not rewind
the clock used by a subsequent reset. Backend `arc_resume_year_pending` and
`arc_resume_year[2]` are host-only synchronous constructor arguments, consumed
before any admitted snapshot or guest instruction; reconstruction supplies
them afresh from the restored frontend cache. The test-only RPC load-epoch
counter is host-only and inactive in Archimedes sessions. There are no new
derived-on-load calendar objects. Each declaration has its own inventory row
and reason; RPC's separate census correctly treats the inactive Archimedes
calendar cache as host-only for its backend.

`arculator-state-calendar` checks all eight write masks for raw C0/C1/3F,
years 2025/2099, and all four 4 MiB profiles: 64 fresh-process canonical
save/load/save cases, 1,536 differential frames (wire guest state, CPU, PCM
and live CMOS), and 64 compared resets after load. Each reference and loaded
branch runs 12 frames before reset and 12 after it; host-picture differences
are reported per case. Both branches must finish with byte-identical native
store trees too. Five omission mutants cover the five new serialized objects.

The producer pauses at capture while the parent copies its committed stores,
as in the existing fresh-process matrix. The consumer starts on that disposable
checkpoint and loads the post-first-frame state before publishing its first
frame. This matters for partial write masks: normal construction repairs a
partial guest checksum, and a real frame or reset can publish a newer frontier;
loading the older capture against that newer store must still refuse. The
independent branches preserve that rule. Changing the external clock before
load proves that virtual time replays exactly and both resets sample the new
host month. This is distinct from the documented limitation on states *taken*
before their source session's first frame.

The authored firmware's sequential CMOS read also exposes a valid cursor above
255; restore retains this nonnegative integer because the controller masks
addresses on access, rather than incorrectly restricting the retained cursor
to one byte.
The calendar test is registered outside `ARCDROID_RPC_LOCKSTEP_LOCAL_TESTS`;
it requires no local-shard placement. The incoming placement guard still checks
the complete ON/OFF test-set difference. The accepted first-frame limitation
is recorded in the SS-1 release notes. Fable's Run M quiet-host confirmation
closes the earlier timing gap (floppy maxima: save 1.26 ms, warm load 3.30 ms);
Run N does not repeat an unchanged performance campaign.
## Run O: RetroArch callback restoration and unsupported modes

Run N's model restored the same poll pointer that was installed at content
load. RetroArch 1.22.2 does something different: `runloop.c:4631–4644`
installs `core_input_state_poll_maybe`, but `core_set_default_callbacks`
(`:7702–7715`) puts `input_driver_poll` in `retro_ctx.poll_cb`.
`runahead_core_run_use_last_input` (`runahead.c:1095–1118`) restores that
second pointer after each speculative frame. SS-1 now normalizes that
restoration to the original poll wrapper, only when a speculative frame has
actually executed and the frontend switches away from its fake poll. This
changes neither the speculative provenance latch nor the rule that only a
successful state restore can release it. Repeated fake frames still execute
with the fake callback, and failed rollback still quarantines the guest.

This fixes two related failures. First, the restored real callback was
mistaken for Preemptive Frames: the next run-ahead save failed, and
`runahead_err` (`:1011–1018`) cleared `RUNAHEAD_AVAILABLE`. That is a stable
run-ahead failure, but rewind's `state_manager_check_rewind`
(`state_manager.c:765–788`) continues capturing every frame and ignores the
failure, explaining the later warning flood. The provided CI log contains
1,791 core warning lines and 1,791 matching environment OSD lines (3,582
occurrences), rather than 3,582 independent mode initializations.

Second, the restored `input_driver_poll` bypasses the poll-type wrapper.
With early polling, `core_run` (`runloop.c:8034–8085`) already polls before
entering the core; with late polling, `core_input_state_poll_late`
(`:4586–4595`) polls on the first input query. Calling the direct poll too
therefore polls twice in a real frame. `input/input_driver.c:6416–6427`
clears network-command bits before polling its socket; a second poll can
erase a just-received QUIT or CLOSE_CONTENT before the following runloop
check. The 1.22.2 default is late polling (`config.def.h:1654`); the route
configuration does not override it. This explains a live, frame-producing
process that ignores the test's QUIT pair, sent 300 ms apart
(`tools/test-retroarch-windows.ps1:94–99`); it is an input-command loss,
not a blocked unload call.
That diagnosis is inferred from the live log and the source paths. A
disposable negative control that accepts the direct poll as real reproduces
the lost command: with the earlier poll-count assertion disabled only in
that disposable harness, the one-shot command-delivery assertion fails.
Keeping the original wrapper gives exactly one real input/command poll per
outer frame in all three polling modes, including after run-ahead fallback.

The supported stable outcome is single-instance run-ahead. A requested
secondary is refused before registration/preparation by R-1a; RetroArch
`runahead_run` (`:1243–1255`) clears secondary availability and executes an
ordinary frame, then uses the primary on its next call. Preemptive Frames
is refused at its first save, before any speculative guest execution.
`preempt_run` (`:1655–1681`) deinitializes its buffers and overrides on that
false return. `preempt_init` is called at initialization or an explicit
mode update (`retroarch.c:3266–3274`, `:8124`), not every frame. Its teardown
re-enables run-ahead/secondary availability (`runahead.c:1390–1414`), so the
same bounded secondary fallback applies again. No size-zero latch disables
ordinary saves or rewind. The core's unsupported-mode notice is emitted
once per content session and survives rewind and reset; explicit frontend
mode toggles can additionally produce RetroArch's own mode/error messages.
The runtime Preemptive Frames hotkey also disables `run_ahead_enabled`
(`retroarch.c:3254`), so its failure resumes ordinary frames until run-ahead
is enabled again. The matrix distinguishes that hotkey route from startup
configuration files that set both booleans true.
Thus a configuration settles within three outer calls in the mixed startup
case (preempt refusal, secondary refusal, working primary run-ahead), and
within two for secondary fallback or a failed preempt hotkey activation.
With rewind enabled, the first preempt activation can also refuse one
ordinary rewind capture before refusing preempt's save; subsequent captures
use the restored real callbacks and succeed.

`arculator-state-runahead-modes` runs the shipping library through `retro_*`
in both gate configurations. Its authored model distinguishes the initial
wrapper from the restoration pointer, captures rewind before execution,
models secondary refusal and preempt error teardown/re-entry, counts every
save/refusal/restore and mode notice, and injects one-shot command bits
while each mode is active. It asserts one real poll per outer frame and
bounded unload/deinit with no open VFS handles. The existing differential,
field-drop, store/frontier, failed-rollback and refusal suites remain the
guest-state/publication evidence. The only added static is a host-only
notice latch; the state format and advertised bound are unchanged.

Primary source: [RetroArch v1.22.2](https://github.com/libretro/RetroArch/tree/v1.22.2)
(`runahead.c`, `runloop.c`, `retroarch.c`, `state_manager.c`, and
`input/input_driver.c`). These are authored interface models; native Windows
RetroArch confirmation runs in CI, and Android device evidence remains owed
under D-217.

On Apple Clang 21.0.0 / macOS 26.6.2, unloading an ASan-instrumented copy
leaves poisoned global shadow behind. A later image can reuse that address:
ASan's image constructor calls `getsectiondata`, whose intercepted `strcmp`
then reports a global overflow before any core entry point. The standalone
[`macos_asan_dlopen_repro.py`](../tests/macos_asan_dlopen_repro.py) contains no
core code: two copies of a three-array library, ordinary heap allocation and
repeated `dlopen`/`dlclose` reproduce that call chain; the `RTLD_NODELETE`
control completes 200 iterations. The diagnostic also observes the retained
poison after close. ASan can fault again while formatting the first error
through unloaded global metadata; a terminating signal handler preserves the
original registration stack in the standalone diagnostic.

Only the run-ahead model's copied-image loads on **macOS with ASan** use
`RTLD_NODELETE`. This omits physical image unmapping between retries, while
balancing every loader handle and executing all secondary environment, init,
refused-load, unload and deinit calls. All 48 mode cells, exact retry/save
counts, notice and command assertions, primary teardown and memory/UB checks
remain active. Linux sanitizer jobs and every non-ASan build still execute
the full image unload/reload sequence. The separate secondary-route test is
unchanged. No production loader behavior or sanitizer memory check is changed.
