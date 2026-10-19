<!-- SPDX-License-Identifier: GPL-2.0-only -->

# SS-1: Arculator state and publication design

Order: [SS-1](handoff.md), D-011, D-210, R-SS3a and run-C R-SS4b. Compatibility is the
same core build, platform, firmware, machine, prepared media and options.
RiscPC serialization remains unavailable. Capability advertising is the last
acceptance gate; see [Sol's report](../SOL-REPORT-SS1.md) for actual results.

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

## Compact machine state and stable frontend capacity (run C)

R-SS4b supersedes the variable-size rule. At content load, a registration-only
walk of the same reviewed visitors measures complete compiled static owners,
including unused media/discovery capacity. It does not execute the engine or
capture a frame. Twice native storage covers pointer bindings on the shipping
64-bit targets; the record limit reserves all record headers, and 512 bounded
string/identity slots cover OSK labels, active JFD reasons and observer strings.
The retained presentation array is counted in the raster allowance instead.

The session capacity adds machine RAM; three 4096×2048 retained rasters and
row-span headers; admitted raw-media limits (1,600 KiB drive 0 and an 800 KiB
helper); four raw track caches; the maximum active JFD descriptors, replacements
and created sectors; and bounded speculative publication ownership. Native HD
capacity covers profile changes at reset. Disk replacement and reset never grow
this capacity. Release clears it; every context returns the same value once the
Arculator engine is initialized. RiscPC and pre-boot chooser states return zero.
The fixed-buffer AC-6 harness queries at load and retains its buffers through
real/speculative frames, restore, media swaps and reset.

The bound is conservative and independent of current compression. The payload
still captures only used retained spans and dirty media. Size queries are now
constant-time. The report includes capacity, payload sizes and actual costs;
capacity padding is part of save cost. Input never treats unused capacity as
machine state. Source caches are primed during validated load preparation so the
first public state load does not pay immutable-media hashing costs.

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

Parser, aggregate heap replacement and decoded data remain bounded to 1 GiB.
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

R-SS3a accepts the input-callback switch in [RetroArch 1.22.2
runahead.c](https://github.com/libretro/RetroArch/blob/v1.22.2/runahead.c).
The initial real frame publishes eagerly. A special same-instance save captures
the real callbacks; switched callbacks identify fake frames, including the final
fake frame which has A/V enabled. Successful restore makes the next baseline
frame real. A baseline frame without that evidence remains staged; a later
switch can prove that the preceding uncertain baseline frame was real. A/V
suppression is not used as a commit signal. The harness follows this callback
sequence through `retro_*`, injects speculative JFD writes, checks zero native
mutations, restores, and compares real-frame video/PCM/CPU traces.

CMOS and raw Original/shadow files have a new two-slot durable frontier under
`arcdroid/state-stores/`. An `ACFR2` record has generation, intent/applied phase,
identity, whole after-image, checksums and an Original-media origin identity.
A flushed and validated intent is renamed into an inactive slot before target
mutation; the target is then written and flushed, followed by an applied
acknowledgment of the same generation. Normal cold preparation redoes an
interrupted intent. Applied bytes changed externally are adopted at a new
frontier without overwriting the external edit; existing CMOS validation and
healing still run. A state load encountering changed target bytes refuses.

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
