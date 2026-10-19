<!-- SPDX-License-Identifier: GPL-2.0-only -->

# SS-1: Arculator state and publication design

Order: [SS-1](handoff.md), D-011, D-210, R-SS3a and run-H adjudication.
Run H is not acceptance-complete: the 16 MiB session-bound gate remains unmet,
and the module guard cannot distinguish a legacy Android loader aliasing the
secondary copy to the primary module. Compatibility is the
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
143,758,088 bytes. Three maximal retained rasters account for 100,712,448 bytes;
pending ordered-operation ownership reserves another 33,554,432 bytes. Measuring
only admitted media and actual field encodings does not meet 16 MiB. No proof
currently permits discarding these histories at every incomplete/interlaced
frame boundary. No smaller bound is advertised without that proof. Default
20 MiB RetroArch rewind memory therefore still cannot hold a state. The format
continues to zero spare output bytes so the original full-buffer AC-2 byte
identity holds with arbitrary caller allocations. Consequently save work also
still includes the bound; M-2's exact-work requirement is not claimed complete.

Source caches are primed during validated load preparation, so the first public
state load does not pay immutable-media hashing costs. Size queries are constant
time and never query GET_SAVESTATE_CONTEXT, which can create another core.

The VIDC bitmap, platform framebuffer and frontend presentation history can
contain pixels that are read before a complete redraw. They remain serialized,
with per-row spans marked at every producer write. Encoding trims zero edges
and retains all interior pixels; zero pixels outside those spans are derived.
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
loader resolves matching basenames to the existing primary module; that
aliased-lifecycle case remains an explicit release blocker.
Neither init nor the size getter queries GET_SAVESTATE_CONTEXT. Tests cover
copied modules reached by default run-ahead, cheats and failed-preempt routes,
including RiscPC and preservation of primary Disc Control callbacks.

CMOS and raw Original/shadow files use two-slot durable frontiers under
arcdroid/state-stores/. CMOS retains the eager intent/applied protocol. A raw
store without previous state history follows main's native sector writes. At
admission it caches committed bytes and a labelled 64-bit sector fingerprint;
writes mark sectors only. Its first-ever save reads only changed sectors and
publishes a validated 168-byte ACFR2 checkpoint (phase 3). This is an
accidental-edit check, not authentication. No older state exists before that
checkpoint. Once a store has state history, later writes and later opens retain
the durable protocol; skipping new state use cannot permit old states to roll it
back. This qualification matters for write-cost measurements.

Subsequent intent/applied records include the full after-image. Intent is durable
before native target mutation; applied acknowledgement follows flush. Cold
preparation redoes interrupted intents. Applied/checkpoint bytes changed
externally are adopted at a newer frontier without overwriting the edit; a hot
state load only refuses. State loads never repair native files. Native authority
and untracked caches do not rewind. Persistent store history is retained across
module/session lifetimes.

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

The ordered speculative operation log now has an aggregate ownership ceiling
of 32 MiB, matching the existing per-file speculative-view ceiling. Each operation
is charged its native allocation, payload and sufficient packed-wire overhead;
the container header is reserved too. Thus both queue ownership and its encoded
blob fit a known bound. Restore validates this budget before allocating each
operation and reconstructs its exact remaining capacity. Exhaustion follows the
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

### R-SS3f verification stop (Android API 21–22)

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
module. The existing distinct-module check cannot detect that alias. Per the
Founder's conditional instruction, R-SS3f is stopped: no system-property runtime
gate or claim that size zero protects the primary is shipped here. The modern
module check remains. Native copied-module RPC tests already exercise a primary
whose actual retro_serialize_size is zero, followed by secondary lifecycle entry;
only distinct-module rejection prevents its side effects. The earlier same-handle
native diagnostic remains a failing registration check, not an acceptance pass.
A core-only API-size switch is insufficient; a frontend fix or an additional
reviewed aliased-lifecycle protocol is needed for those legacy Android routes.

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

A fast interlaced retained raster alone needs about 16 MiB in the current
32-bit literal-span representation, before 4 MiB RAM, framing, dirty media or
pending effects. This is a lower reservation for **that representation**, not
an information-theoretic lower bound for a redesigned palette/history codec.
The current 137.10 MiB bound is therefore not fixed by substituting ordinary
640x480 geometry, and an arbitrary 16 MiB cap would lose state. No smaller bound
has been installed without proof. R-SS4d remains unfinished: derive retained
platform/presentation copies from an explicit last-copy/overlay recipe, compact
non-derived VIDC history, and derive a pending-effect bound from actual session
producers and their publication window. The fixed 32 MiB log allowance has not
been presented as such a derivation. The target has not been declared impossible.
