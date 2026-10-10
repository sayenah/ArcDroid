# RiscPC hard-disc store contract (D-139)

Status: **accepted by Fable (D-145)**. Run A at `b4bc633`, amended by D-147 (R2c-1) for run B
on `slice/r2c-ide`. This freezes work-order item 9 and the store interfaces
needed by items 10–12. Platform qualification remains separate; RPC is not
exposed through a shipping route. MUST statements below are requirements.

## 1. Ownership, identity and commit guarantees

The content session owns one store at
`<save>/arcdroid/hd-riscpc/<content_id>/`. The ID is D-029's frozen 40-character
lowercase SHA-1 identity; swaps, disk-control edits and resets never rekey it.
CPU model and firmware are absent from this key. Only one process/handle may
write this directory at once. Cross-process locking remains an explicit
unimplemented assumption; run B must not imply concurrent-writer safety.

Geometry is 512-byte sectors, 63 sectors/track, 16 heads, 256 cylinders:
258,048 sectors = 132,120,576 bytes = **126 MiB**. Last LBA is 258,047.
Store revision 1 and RPC formatter revision 1 are independent of the A3020
revisions. Overflow, foreign IDs, unsupported revisions or inconsistent geometry
are refused before mutation. Generation/sequence wrap refuses writes.

An acknowledged command has passed exact seek-position and write-count checks,
a present and successful VFS flush callback, successful close and the expected
file length; only then is the live view updated and the backend returns true.
There is no per-record reopen or readback (D-145 A1). Record CRCs, recovery
validation and publication readbacks remain mandatory. Recovery yields every acknowledged command, atomically,
under the process-termination assumptions in §10. A command interrupted before
acknowledgement can be absent or wholly present; it must never be partly replayed.
An ATA error does not promise that a complete record whose flush/close failed
cannot be discovered on reload. Report that uncertainty; never retry it internally.

This is checked publication to the frontend filesystem, **not a power-loss
durability promise**. Neither file nor directory durable sync is provided by
the core's VFS contract. Power-loss/reboot recovery validates and refuses damage;
it cannot promise retention of acknowledged bytes that the host did not persist.

The A3020 uses `hd/`, whole immutable `disc.gN.hdf` bases, `ready.a/b`, a
whole-image resident view and open-time compaction (`install_store.c`). RPC uses
its own envelopes, chunks and incremental checkpoints. No A3020 or legacy
`install/` file is enumerated, read, written, removed or migrated. Reuse the
CRC framing *pattern*, hashing primitives and `ArcIdeBackend`, not the A3020
store or its constants. `install_store.*` and its executable behavior stay intact.

## 2. Exact files and encoding

All integers are unsigned little endian, u32 unless specified u64. Reserved bytes
MUST be zero. SHA-256
fields are 32 raw bytes; content ID fields are 40 ASCII bytes without NUL.
CRC-32 means the repository's `arc_crc32`. Exact lengths, checksums, identity,
geometry, revision, generation and canonical leaf names are checked; no path
from an untrusted record is opened. All generated names are lowercase ASCII.
A truthful advisory listing that reveals case aliases, unknown entries or
unexpected directories causes refusal, not cleanup. An empty or failed listing
proves nothing and cannot affect recovery, GC or any bound. The store opens only
canonical fixed names; hidden foreign entries cannot affect correctness. Do not rely on case to distinguish files on Android or Windows.

| Kind | Canonical leaf | Size and purpose |
|---|---|---|
| Published manifest | `manifest.a`, `manifest.b` | 24,324 bytes each; only these are manifest recovery roots |
| Manifest candidate | `manifest.tmp` | At most one, same framing; never a recovery root |
| Journal | `journal.a`, `journal.b` | 256-byte header plus whole-command records; slot = generation modulo 2 |
| Immutable chunk | `cIII.gGGGGGGGGGGGGGGGG` | III = 3 hex digits, index 000–1f7; G = 16 hex digits, generation; 262,276 bytes |
| Chunk candidate | `chunk.tmp` | At most one, same framing as a chunk; never guest-visible |

There are no other file kinds, pointer files, backups, lock files or journal
temporaries. Journal headers are created directly in an **empty inactive slot**;
no command is admitted there until the closed header has been verified. This
keeps the two-journal and temporary-space limits literal, even during rotation.
Probe writability with the actual inactive header/candidate operations, not
extra probe files. A published chunk is never changed in place.
There are at most **two manifest files including `manifest.tmp`**: preparation
has one published root plus the candidate; successful rename consumes the
candidate and leaves two published roots. Retire the older root before writing
another candidate. Thus the three allowed manifest leaf names never coexist.
Manifest slot also equals manifest generation modulo 2 (a=0, b=1); reject a
published slot whose parity is wrong. Journal generation advances at rotation,
and manifest generation advances at checkpoint/population publication.

### Manifest (128-byte header, 504 entries of 48 bytes, 4-byte CRC)

| Offset | Field |
|---:|---|
| 0 | 8-byte magic `ARCRHDM1` |
| 8, 12 | envelope version 1, total length 24324 (u32 each) |
| 16 | content ID |
| 56, 60 | store revision 1, formatter revision 1 |
| 64, 68, 72, 76 | sector bytes 512, SPT 63, heads 16, cylinders 256 |
| 80, 84 | chunk payload bytes 262144, chunk count 504 |
| 88, 96, 104 | manifest generation, exact journal generation, committed-sequence boundary B (u64 each) |
| 112–127 | zero |
| 128 | entries in ascending chunk index, no omissions |
| 24320 | CRC over bytes 0–24319 |

Each entry: kind u32 (0 = HOLE, 1 = CHUNK), reserved u32, chunk generation u64,
payload SHA-256[32]. HOLE requires all remaining entry bytes zero. CHUNK derives
its leaf from entry index and generation, which cannot exceed manifest generation.
Only an explicit HOLE permits zeros. Missing/unreadable/wrong-length CHUNK is
corruption, even if the guest reads only one sector or overwrites part of it.
An all-zero materialized chunk may become HOLE only after complete reconstruction
and verification. Unchanged entries are copied verbatim into the next manifest.

### Chunk (128-byte header, 256 KiB payload, 4-byte payload CRC)

Header: magic `ARCRHDC1` at 0; envelope version/total length at 8/12; content
ID at 16; index/store revision/formatter revision/payload length at 56/60/64/68;
generation u64 at 72; payload SHA-256 at 80; zeros at 112–123; header CRC at
124 over bytes 0–123. The trailing CRC covers the complete payload. Header and
payload hashes must also match the manifest entry. A chunk MUST be verified in
full before any byte is used, including reconstruction of a partially overwritten
chunk. Even a full overwrite does not excuse verification of a referenced base.
Verification is per resident instance; eviction invalidates it. A cache miss
always checks full SHA-256 plus CRC (D-145 A2). Faster implementations are
permitted only with identical results and a portable fallback; CRC-only misses
and moving verification to open are forbidden.

### Journal header (256 bytes)

| Offset | Field |
|---:|---|
| 0 | magic `ARCRHDJ1` |
| 8, 12 | envelope version 1, header length 256 |
| 16 | content ID |
| 56, 60 | store revision, formatter revision |
| 64–84 | sector/SPT/heads/cylinders/chunk bytes/chunk count, as manifest |
| 88 | own journal generation (u64) |
| 96, 104 | parent journal generation, parent manifest generation (u64) |
| 112 | initial sequence B (u64); first record is B+1 |
| 120 | SHA-256 of complete parent manifest |
| 152 | frozen parent journal byte length (u64) |
| 160 | SHA-256 of that exact frozen journal prefix |
| 192 | role u32: 0 INITIAL, 1 SUCCESSOR, 2 POPULATION |
| 196–251 | zero |
| 252 | CRC over bytes 0–251 |

INITIAL is generation 0, B=0, parent fields/hashes zero. SUCCESSOR is parent
journal generation +1, and binds the *selected* parent manifest plus its complete
valid journal prefix ending at B. Parent records cannot subsequently be appended.
POPULATION is an offline candidate (§9). With a published source it advances
both manifest and journal generation by one, carries the exact source manifest
binding, source journal generation, frozen source journal length and digest,
and initial sequence B=S (the captured last committed sequence). Its parent
journal fields recognise retirement debris only: POPULATION is never a replay
successor. With no published source it uses generation/B 0 and zero parent
fields; the first authored blank uses INITIAL. A manifest binds to its journal's generation and initial B
exactly, not merely its filename. A selected successor/population manifest is
self-contained; its journal's historical parent need not remain on disk.

### Command record (existing 80+payload+16 framing pattern)

80-byte header: magic `RCJR` at 0, envelope version 1 at 4, content ID at 8,
journal generation u64 at 48, global sequence u64 at 56, first LBA/count/payload
length u32 at 64/68/72, CRC over bytes 0–75 at 76. Payload is count×512 bytes,
count 1–256, wholly within geometry. 16-byte trailer: same sequence u64,
payload CRC u32, magic `RCCM`. Sequences are contiguous from B+1, globally
monotonic across rotation. No record spans journal files. At most 131,168 bytes
per command. Journal CRCs protect each command; the successor additionally
hashes the frozen prefix. No filesystem bytes or journal offsets enter content ID.

## 3. Guest write/read path and resident memory

The IDE controller accumulates the complete command in its existing maximum
128 KiB transaction latch. Zero ATA count means 256 sectors. Reset, unload or
incomplete transfer discards it. The last completion event calls backend write
**once**, with the whole command, including a chunk crossing. Per-sector commits
and speculative cache mutation are forbidden.

Before append: validate range and writability, reserve index/cache bookkeeping,
and check **the sum of actual lengths of both journals plus the new record**
against 8 MiB. Population ownership refuses guest writes. No capacity wait,
emergency checkpoint or journal-cap overshoot is allowed inside the command.
Append header → payload → trailer at the verified tail, checking the exact seek
position and each write count; require a present successful flush, successful
close and expected file length. Do not reopen/read back the record.
Only after success update sequence, sector-offset index and resident cache
entries and streaming journal digest; then return true and acknowledge at the controller's already prescribed
virtual completion event. Failure returns false, latches store/controller
read-only and raises ATA ABRT through the normal completion/nIEN rules (§7).

Keep two 32-bit sector-offset arrays of 258,048 entries: frozen checkpoint and
live. An entry encodes the journal slot and payload byte offset; zero means
use the selected manifest. Offsets fit below 8 MiB (use high bit for slot).
Record boundaries are derived from validated framing; rebuild indexes on open.
The frozen array is immutable until the checkpoint finishes. The live array
continues to reflect writes to the successor. At publication clear entries
backed by the retired parent, retaining **every** successor entry; this changes
representation, never guest bytes.

Cache at most eight 256 KiB **effective** chunk payloads, tagged with chunk index,
manifest generation and live sequence. Use deterministic LRU (index breaks ties).
Evict clean or journal-backed dirty chunks alike. If all are not yet checkpointed,
evict the LRU after its command has passed acknowledgement checks: the journal and
index are authoritative. Never pin dirty payloads until checkpoint or flush
them on eviction. Failure to reconstruct does not allocate beyond the cap.

A read miss verifies the entire referenced base into one scratch buffer (or
initializes an explicit HOLE), then applies indexed journal sectors in order.
Read only records named by the sector index that overlap this chunk; never
scan unrelated records. Group by command so each contributing framed record is
read/CRC-checked once per miss. A record's verification precedes copying *any* payload from it. This
can read up to the full ≤8 MiB journal set plus one chunk for a pathological
single-sector miss. The same rule applies to checkpoint reconstruction, using
only the frozen prefix. A cached live payload that contains post-boundary writes
cannot be used as the checkpoint's frozen payload. No 126 MiB allocation, mmap,
whole-journal payload retention or filesystem-sized formatter buffer is permitted.

Budget all store-owned buffers, not just cached data: indexes 2,064,384 bytes;
cache 2,097,152; scratch 262,144; command verification 131,168; three manifests
72,972; all other metadata/paths/hash state ≤262,144. Total ≤4,889,964 bytes
(<8 MiB). The IDE's existing 131,072-byte latch is outside the store but MUST
be reported separately. Use streaming readback/hashing, not a second chunk
payload buffer. Run B measures allocation high-water marks, including open,
recovery, creation and population, and checks the actual structures fit this budget.

## 4. Checkpoint state machine

No worker thread or host-time deadline. At the end of each RPC `retro_run`, after
the guest frame and before frontend notices, call one bounded maintenance tick.
The runner's equivalent frame boundary uses the same tick. Do not add a
scheduler event or change its nine-slot trace format. A tick either advances one
control state, materializes **one** chunk, or removes at most 16 garbage chunks.
It never loops into another state/chunk. Work limits count I/O, verification and
allocation; guest reads and per-command commits are separately measured, not
hidden as maintenance. Multiple internal frames must not multiply work per
frontend call. Population ownership suppresses every maintenance state, including ROTATE,
from begin through publish/abort. An unload closes handles and leaves recovery state; no forced
whole checkpoint. Reset cancels the IDE transfer and retains the session store.

Arm the checkpoint request immediately after the commit that crosses **1 MiB**,
at a journal length no greater than 1 MiB+131168 (<4 MiB). Service it on the
next tick (IDLE → ROTATE, then the rotation tick). If publication
has just finished and the successor is already over threshold, begin the next
checkpoint on the first tick after retirement/GC. No third journal/root exists.

| State | One tick and next state | Files after a process kill |
|---|---|---|
| IDLE | Under threshold: no work. Otherwise → ROTATE | Current manifest + journal recover current acknowledged view |
| ROTATE | Capture S=last committed sequence; freeze parent journal length, a snapshot/finalisation of its streaming SHA-256 state, and frozen index (no journal rescan). Reserve 256 header bytes within the aggregate cap, then write/flush/close/readback the empty-slot SUCCESSOR header binding current root and S. Switch future commits to it **only after verification**; make candidate entries from current manifest; → MATERIALIZE | Before verified header: old root/prefix; after header: old root follows successor and includes all later committed commands |
| MATERIALIZE | Ascending dirty chunk index: verify parent base, apply frozen records through S; write one `chunk.tmp`, flush/close, verify, rename to empty generation leaf, verify final. Update candidate entry; zero payload → HOLE with no chunk. One chunk maximum. Last dirty chunk → MANIFEST | Old root + both journals remain sufficient; new chunks are unreachable candidates, not roots |
| MANIFEST | Encode next manifest generation, binding successor generation and S; write/flush/close/readback `manifest.tmp`; → PUBLISH | Old root + successor; complete `.tmp` still not selectable |
| PUBLISH | Inactive manifest slot must be empty (any older root was retired before this cycle). Rename candidate into it; readback/check exact bytes; switch selected manifest and live index/cache tags only after verification; → RETIRE | Before rename: old root + journals; after valid rename: new root + successor, or old root + journals. Both recover the same acknowledged view |
| RETIRE | Revalidate newest published root and journal header, expected length and recorded streaming digest without rescanning records. Before GC → GC; after old-only GC finishes, remove old manifest then old journal → IDLE. Next ROTATE waits for completion | Old manifest is retained as the garbage inventory until every old-only leaf is gone. After manifest removal the new root suffices; a leftover journal is recognised retirement debris |
| GC | Compute old-only CHUNK leaves from the retiring and new manifests, excluding every pin; remove at most 16 per tick by fixed paths. On completion → RETIRE for manifest/journal removal; abort cleanup → IDLE | New root remains selectable; missing chunks of a bound retiring manifest are expected garbage, never damage |
| READ_ONLY | No further publication, append, truncation or GC in this session | Preserve recovery roots and uncertain files; reopen diagnoses them |
| DAMAGED | No mutation or attachment of an unverified disc | Preserve every file; report damage |

D-145 freezes the following worst-case **VFS transferred-byte and operation**
budgets per maintenance tick. Count each open, size/stat, seek, read, write,
flush, close, rename, remove and directory operation, including failed calls;
count actual transferred bytes on failure and requested bytes separately. Hash
input bytes, memory copied, allocations and peak allocated bytes are separate
counters. An operation never processes another state or a second chunk.

| State | Maximum transferred bytes | Maximum VFS operations | Derivation |
|---|---:|---:|---|
| IDLE | 0 | 0 | Request/ownership check only |
| ROTATE | 768 | 32 | Header write and two header-sized checks; streaming digest snapshot has no journal I/O; frozen-index copy is 1,032,192 RAM bytes |
| MATERIALIZE | 9,437,712 | 4,096 | At most 8,388,608 indexed journal bytes plus four complete chunk envelopes (base read, candidate write, candidate and final readback); at most 512 distinct overlapping records, checked once each |
| MANIFEST | 48,648 | 32 | One manifest write and one readback |
| PUBLISH | 24,324 | 32 | Empty-slot rename and final manifest readback |
| RETIRE | 24,580 | 32 | Manifest plus 256-byte journal header; size/digest comparison uses validated streaming state, no record rescan; two removals |
| GC | 0 | 1,040 | Scan at most 504 manifest entries in RAM; fixed-path stat plus remove for at most 16 old-only leaves per tick; no listing or chunk read |

Maintain a journal SHA-256 context during each successful append and during
recovery. ROTATE snapshots/finalises a copy; it never rereads the journal.
RETIRE compares the header, actual size and previously recorded digest/state
under §10's no-external-edits assumption; it never hashes the journal again.
Reconstruction seeks only indexed overlapping records, deduplicates them, and
checks each complete frame before use. D-147 adds the final RETIRE control tick after GC; §8 revises both
cycle lengths and the sustained-write occupancy proof accordingly. These maxima bound work,
not wall time: AC-9 measures every state inside the installing frame. A measured
frame-budget failure returns to Fable rather than weakening verification.

Writes arriving in MATERIALIZE/MANIFEST/PUBLISH append exclusively to the
successor. The new manifest is the view **at S**, not the latest live cache.
Successor records start at S+1 and are retained/replayed by both old-root recovery
and new-root recovery. After publication, reads use new base plus successor;
after a kill before publication, old base plus parent journal plus successor.
This also covers commands crossing several dirty chunks. Allocation/publication
errors transition immediately to READ_ONLY; no synchronous retry loop.

Old-root recovery MUST recognize the SUCCESSOR header in the other slot. Check
its exact parent manifest digest, generation, frozen prefix length/hash and S
against replayed parent. Do not infer a successor from a numerically newer file.
An older stale journal cannot be a successor; a POPULATION candidate cannot be
one. Only one successor is allowed; cleanup must finish before another rotation.

## 5. References and garbage collection

All correctness references and garbage inventories come from fixed-path
manifests and journal bindings, never directory enumeration. During a checkpoint,
the old selected root and both journals pin every reconstruction base; live
candidate entries and `chunk.tmp` are additional pins. A verified newer root
whose SUCCESSOR or POPULATION header binds the old manifest makes that old
manifest a **retiring inventory**, never a selectable recovery root. Its chunks
may already be missing after GC; only its manifest CRC and binding are checked.
An unchanged old-generation chunk copied into the new manifest remains pinned
indefinitely. Invalid newest roots remain damage; falling back is forbidden.

After PUBLISH, retain the old manifest while removing its CHUNK leaves referenced
by neither the new manifest nor any pin. Derive each leaf from its index and
generation. Remove at most 16 per tick; then remove old manifest, then old journal.
Required remove failure latches READ_ONLY. No next ROTATE or competing population
starts before retirement completes. Thus **no generation other than selected+1
can contain unreferenced candidate chunks** (generation zero before first
publication); retiring chunks remain discoverable from the old manifest until
all are gone. Test this invariant across checkpoint and population kills.

Open probes all 504 canonical selected+1 index names (zero if unpublished), plus
`chunk.tmp` and `manifest.tmp`, before writable use, removing only candidate
leaves not referenced by a selected root. Interrupted materialization restarts
from the frozen index after this bounded reconciliation. No directory listing
is used. An abort's private leaves obey the same fixed generation rule.

A listing is advisory hygiene only: truthful unknown/aliased entries are refused;
empty, truncated or failed listings establish nothing. All file and physical
space bounds cover **store-owned canonical files only**; foreign files are outside
them. Normal operation and every interrupted cycle have at most 1,008 owned
chunk leaves plus one `chunk.tmp`. No unbounded directory-name array is retained.

## 6. Open, recovery and damage

1. Validate local save path using the existing native-path predicate's semantics:
   POSIX absolute, UNC, drive-absolute Windows or unambiguous relative paths;
   URI/scheme and drive-relative paths refused. Never fall back to libc I/O.
2. Inspect fixed manifest/journal paths with VFS stat and exact reads, independently
   of listing. Check every present published manifest before choosing highest
   generation. Equal generations, invalid slot, unsupported binding or unexplained
   journal are damage; do not choose an older apparently good root and lose writes.
   Validate every selected-root journal and binding. When the newest valid root
   binds an older manifest, check the older manifest CRC and binding only, never
   its chunks: it is a retiring inventory and is never selected. Resume old-only
   GC on the next maintenance tick before removing that manifest and journal. A
   leftover parent journal after RETIRE removed its manifest is explained only
   by the selected SUCCESSOR or POPULATION journal's exact parent generation,
   frozen length and prefix digest; it is retired debris, not a second replay
   path. POPULATION never causes replay of that retired source journal. A verified header-only unbound POPULATION
   journal may be retired after validating the source root, before slot reuse.
3. Validate selected journal header and bounded length; replay complete records,
   checking header CRC, sequence, range, trailer and payload CRC into offsets, not
   a full disc. Update the streaming SHA-256 state while validating each journal
   byte once; retain it for append/ROTATE. For an older root with a valid SUCCESSOR, validate its parent
   prefix binding and replay successor through its last complete record too.
4. A structurally **incomplete final frame** (short header, or valid header with
   insufficient payload/trailer at EOF) is a torn tail. Retain the complete prefix.
   A complete invalid frame/CRC, invalid header, sequence gap, oversized file,
   inconsistent prefix hash or invalid non-final record is corruption, even at
   EOF. Never search later bytes for a plausible commit marker.
5. Before new writes, truncate a proven torn tail to the complete boundary,
   flush/close/readback. If truncate is absent/fails, keep recovered prefix readable,
   latch READ_ONLY, preserve tail and show notice. Validate both roots before any
   cleanup. Resume an interrupted checkpoint from the frozen parent/S and rebuild
   candidate; completed unreferenced candidates may be verified/reused or removed.
6. Referenced chunk payload verification is **lazy**, on first use (§3), keeping
   open independent of disc capacity. Open checks manifest/journal integrity; it
   does not claim to certify every chunk. Missing/corrupt referenced chunk on read
   or checkpoint immediately reports damage, fails the operation, latches read-only
   and preserves files. Previously verified cached data may be read; affected
   content cannot be replaced by zeros or by an older root.

Open reads at most two 24,324-byte manifests and **8 MiB total journal bytes**,
streams SHA/CRC, builds bounded indexes, and inspects bounded metadata. No full
disc hash or eager chunk payload scan, no open-time whole checkpoint/GC. Fixed
metadata reconciliation probes 504 candidate leaves and two temporaries, plus
at most 1,008 retained manifest leaves; these ≤1,514 stats, at most 1,514 read-only absence probes, retained-root
checks and at most 506 candidate removals count against the measured open bound.
A successful exact-path open after a failed stat is inconsistent metadata and
refuses mutation; simultaneous dishonest absence remains the §10 assumption.
Advisory listing work is separately reported and cannot determine any result
or physical bound. Reconciliation
may schedule bounded maintenance before another rotation; admission uses the
existing byte-cap check meanwhile. AC-9's **≤1 s** is a measured Odin3 limit,
not an algorithmic guarantee on arbitrary storage. Log byte/operation counts
and open time separately from initial formatting/population.

With no published manifest, only an empty directory or recognized interrupted
first-creation candidates/header (no command records) qualifies as unpublished.
Restart the authored blank creation only in that case. A directory containing
unbound guest records, a damaged published slot or truthfully observed unexpected entries is refused,
never new. Known partial inactive header of <256 bytes can be abandoned only
when no manifest binds it and it could not yet have admitted a command; a
full-sized invalid header is damage. A valid unbound POPULATION header is an
abandoned offline candidate, not evidence to select its chunks.

## 7. Failure and virtual-time contract

If the next command would exceed the aggregate journal cap before maintenance
finishes, fail it immediately. Full storage, short writes, failed flush/close,
publication/readback/rename failure, failed required cleanup or damaged data
likewise latch READ_ONLY (DAMAGED when no verified view can attach).
An affected read returns ATA IDNF; a failed write or later write returns ABRT
with ERR set. A maintenance failure after a command has already succeeded must
not retroactively change its result: the next in-flight/future write completes
with ABRT at its prescribed virtual event. Preserve all earlier commits.

Queue one frontend-visible error per failure category/session, drain it outside
the protected guest stack: “Hard-disc changes can no longer be saved this
session.” For damage: “The saved RiscPC hard disc is damaged; files have been
preserved.” Torn-tail recovery: “An interrupted hard-disc write was recovered.”
Include read-only status when truncation fails. Logs name phase, operation,
relative leaf, journal generations, S and reason without dumping guest bytes.
An unavailable/failed creation does not attach a temporary RAM disc.

IDE IRQ is IOMD IRQB bit 1; nIEN masks its presentation, status versus alternate
status follows item 10. No storage callback chooses IRQ time from a host clock.
All device delays and MMIO costs use R2a's integer virtual-time service and the
reserved `ARC_RPC_EVENT_IDE`, ordered between FDC and SOUND at ties. Maintenance
neither advances scheduler time nor accrues CPU catch-up credit. Identical I/O
outcomes and guest input produce identical virtual traces however slow the host.

Synchronous VFS calls **can block wall time**; the API supplies no cancellation
or maximum latency. “No guest stall depends on the host” means no host-dependent
guest BUSY duration, polling deadline, backpressure wait or rate adjustment. It
cannot mean guaranteed wall-clock return time with this VFS. AC-9 still requires
p95≤0.80 T and no frame>T on its device workload, including host I/O. If that
measurement fails, return to Fable; do not skip flushes, exceed bounds, lower
guest rates or move failure timing to a host-dependent thread.

## 8. Bounds and sustained writes (AC-5/AC-9)

MiB = 1,048,576 bytes. Bounds include framing and all retained **store-owned
canonical** files. Foreign files, even hidden by an incomplete listing, are
outside the 1,008-chunk and 272,811,464-byte physical bounds; no bound or
correctness decision depends on directory enumeration.

Tick transfer counters count requested read/write bytes, including unsuccessful
calls; returned bytes are also accumulated separately. Hash counters count SHA-256
and CRC input bytes, and copy counters count explicit store buffer copies and
overlapping moves. Record per-tick and session maxima. Hash input per maintenance
tick is bounded by twice the transfer ceiling plus three manifest envelopes and
512 header bytes: 2×9437712+3×24324+512 = **18948908**. Explicit copies are bounded
by **2097152** bytes: ROTATE copies one 1032192-byte index plus a manifest/header;
MATERIALIZE applies at most one chunk of indexed sectors, and a GC batch moves
at most 16×1007×23 =370576 metadata bytes. Hash-internal state copies, zero fills
and scalar index stores are fixed CPU work, covered by the frame measurement.
Advisory directory callbacks have a separate exact operation counter; they
never populate the fixed-path inventory. Reconciliation/replay uses at most
**49152** non-listing open operations: 13796 records×3 callbacks, ≤1514 fixed
stats plus ≤1514 exact-path absence opens, ≤506 candidate removals,
≤2048 prefix stat/mkdir calls for the bounded 1024-byte native path, plus
bounded root/header, close and torn-tail controls. Report advisory work separately from that ceiling.

| Item | Chosen bound and derivation | Run B measurement |
|---|---|---|
| Disc | 63×16×256×512 = 132120576 = 126 MiB | Last-sector CHS/LBA roundtrip and independent formatter disc record/IDENTIFY |
| Chunk | 262144 payload +128 header +4 CRC =262276 <1 MiB; 126 MiB/256 KiB=504 chunks | Max allocation/file length, missing/partial/corrupt chunk cases |
| Resident | ≤4889964 store bytes <8 MiB (§3), plus IDE latch reported | Allocation counters across open/create/write/checkpoint/reload/population |
| Journal | **Both files together ≤8388608**; request armed by threshold crossing at <1179744; freezing awaits the bounded tick, with stream-specific bounds below | Actual file-size and append-reservation high-water counters; cap-edge failure |
| Files | ≤2 manifests **including candidate**, ≤2 journals, plus chunks; ≤1 chunk temporary | Count canonical fixed paths after every operation/kill/reload; count candidates too; no listing dependence |
| Temporary disk | ≤262276+24324 =286600 bytes, one chunk and one manifest | Count `.tmp` bytes after each boundary; no journal/header temporary |
| Maintenance | One chunk step/tick or one control step or ≤16 chunk removals; never two chunk reconstructions | Tick operation trace, bytes, elapsed host time; frontend/runner parity |
| Published disk | At most 1008×262276 +2×24324 +8388608 =272811464 bytes, excluding ≤286600 temporary bytes | Peak physical owned bytes; sparse HOLE and unchanged-reference cases |
| Tick work | §4 state-specific limits; MATERIALIZE ≤9437712 transferred bytes and ≤4096 VFS operations, one chunk | Counters include verification/publication and failed calls; host frame cost measured |
| Open | ≤8437256 bytes of manifest+journal payload reads, bounded metadata; ≤1 s on Odin3 | VFS byte counts and wall time, cold and near-cap recovery |

New immutable chunks retained alongside old ones are **published candidate
storage**, included in the published-disk bound, not hidden as `.tmp` space.
There is no requirement for total physical storage to equal the logical 126 MiB.

Two sustained streams have conservative *frame-count* proofs, independent of
host latency. Let R be journal bytes added per frontend tick and P the frozen
parent bytes. No third journal is opened while a cycle is incomplete.

* Sequential installer-like stream: ≤128 KiB guest payload/frame in ≤256
  commands/frame gives R≤131072+256×96=155648. A contiguous frozen interval
  with P≤4 MiB touches ≤17 chunks, including an unaligned start. A cycle takes
  ≤17 materialization ticks +5 control ticks +ceil(17/16) GC ticks +1 admission
  tick =25. Successor length ≤256+25R=3891456 <4 MiB. Initially
  P≤1048576+2R=1359872 (threshold crossing and next rotation frame);
  by induction every later P<4 MiB. Aggregate ≤4194304+3891456=8085760 <8 MiB.
  Thus arbitrarily many disc wraps, far beyond 8 MiB, require no reopen or stall.
* Scattered stream: ≤4096 guest bytes/frame in ≤8 commands gives R≤4864.
  Even all 504 chunks dirty: ≤504+5+32+1=542 ticks; successor ≤2636544 <4 MiB.
  Aggregate ≤6830848 <8 MiB. This tolerates scatter without assuming allocation
  locality. At 60 Hz the payload allowance is 240 KiB/s; use **per-frame**
  limits at other refresh rates, not a wall-clock rate.

Run B must run ≥512 MiB sequential and ≥64 MiB scattered traffic without reopen,
with data comparison after each checkpoint and fresh-process recovery. Include
one-sector commands, maximum commands, unaligned chunk crossings, disc-end wraps
and writes to already materialized chunks during checkpoint. These rates are
test workload definitions, not a universal measured installer throughput claim.
Fable must record the actual install's command rate/locality and verify it fits
one bound or directly measure its occupancy. More aggressive/adversarial streams
can exhaust headroom: precisely the first command for which retained journal
lengths +80+512×count+16 >8388608 gets ABRT/read-only/notice. Host duration cannot
change that decision. There is no evidence here for a promise about *every*
possible real installer or an AC-9 wall-time pass; those are measured gates.

## 9. Creation, formatter and R3 population hooks

Run B exposes bounded `begin`, sector `read/write`, `seal`, `publish`, `abort`
candidate operations, plus open/read/write/failed/writable/maintenance/close for
the ordinary store. Exact C spelling is local implementation choice; behavior
and ownership are fixed here. Candidates are private handles starting from the **effective source view through
S**, the captured last committed sequence, including every outstanding journal
overlay. Copied manifest entries alone do not define that view. Changed chunks
are pinned in the same directory; the candidate is never attached to IDE.
Begin requires no in-flight guest command, no unfinished checkpoint (including
IDLE → ROTATE), and completed retirement. It takes exclusive offline ownership
from begin through publish or abort, excluding guest writes, all checkpoint
maintenance (ROTATE included), and competing populations. Capture source
manifest/journal generations, S, frozen length and streaming digest; publish
revalidates the snapshot. Any change refuses publication rather than merging
or overwriting concurrent saves. A blocked ROTATE interleaving must be tested.
Creating any journal header, including a population header, first reserves its
256 bytes against the same aggregate journal cap. Otherwise refuse the operation.
Repeated private writes to a candidate chunk use `chunk.tmp`, verification,
retirement of that candidate's own previous leaf, then rename/readback; this is
allowed only when no published root references that leaf. Kill in that private
replacement gap aborts the candidate and preserves the source. Never retire a
source chunk to save space, and never overwrite a published chunk in place.
When repeated private writes change a CHUNK to HOLE, remove its private leaf
before seal/publication loses the candidate inventory.

For first creation the source is an all-HOLE unpublished view. Generate
ArcDroid's authored formatter output as bounded ranges/chunks, verify it, write
only nonzero chunks, create INITIAL header, then verified manifest publication.
Only after publication/readback may guest execution start. R2 automatically
seals/publishes the blank. No RISC OS, boot-resource or user-media bytes are built
in or fetched. Do not allocate a complete formatted image to seed the store.

Parameterize the project formatter (`core/install_disc.*`, not the nonexistent
`libretro/install_disc.*`) with geometry and allocation scale while keeping
`arc_install_hd_format` output **byte-identical** for A3020. RPC revision 1 uses
512-byte zones, 5 zones, ID length 15, zone spare 32 bits and log2(bytes/map bit)
**13** (8 KiB), scaling A3020's log2=10 by eight. This deliberately keeps the
five-zone representation; do not retain 1 KiB/map bit while declaring 126 MiB.
With U=4096−32=4064 bits/zone and D=126 MiB/8192=16128 allocation units:

* map start = (floor(5/2)×4064−480)×8192 = **62652416**;
* each map copy =5×512=2560; root follows both at **62657536**;
* last-zone end =32+D−(4×4064−480)=**384** bits;
* object 2 reservations in zones 0 and 2 remain 16 allocation bits (128 KiB
  each), covering boot and maps/root; free links and beyond-end object 1 match
  the existing formatter's bit rules at this scale;
* root indirect address and parent stay **0x20b** (map/root sharing offset in
  512-byte sectors); root checksum, each zone checksum, cross-check byte, both
  copies and boot checksum are recomputed from the new disc record;
* disc record declares bytes 132120576, sector log2=9, CHS 63/16/256; IDENTIFY
  capacity/current-geometry words agree with those 258048 addressable sectors.

Run B's independent multi-zone resolver must prove these reservations, root
resolution, all free extents and final allocation boundary; an unchanged hash
test alone is insufficient. Fable's AC-8 validates guest FileCore mounts/writes.
The allocation scale is an explicit format choice, not an inferred historical
RiscPC requirement. Existing A3020 SHA-256 remains
`9ec06c758da485f6b7c26b70e4c2b268ec959991cc43a9b6d3bffc495b4a57d2`.

R3 can begin from unpublished blank or a published source, populate a private
candidate through sector operations and its own validated guest/host mechanism,
and call seal only after its completion oracle succeeds. Candidate writes flush
and verify changed chunks but **do not return guest IDE acknowledgements** or
modify the source journal/cache. Seal materialises **every outstanding source overlay chunk**, including
chunks untouched by population, through S, before its source journal can be
retired. It verifies every referenced chunk and the source snapshot, then
writes a POPULATION header (INITIAL for first authored blank) with the source
journal generation, frozen length and digest solely for retirement recognition,
and prepares `manifest.tmp`. Publication advances source manifest generation
by one and binds the new journal with initial sequence S; an unpublished
source starts at generation and sequence zero. Publish uses the same verified empty-slot transition
and retirement order as §4. Abort leaves published source bytes unchanged and
removes only candidate-owned/unreferenced files. Kill before publish leaves the
old root, or an unpublished first candidate; kill after valid publish yields the
complete new root. Population never resumes from guessed incomplete application
state. R3 owns validation, preservation of existing guest files and boot-resource
policy; R2 implements transaction mechanics only. No whole-disc temporary image.

Plan v2 (plan core only) accepts `hard_disc=on|off` and `hd_import=<file>`.
Omitting both keeps IDE absent. `on` automatically publishes the authored blank
when no import is provided; import implies on, and off plus import is refused.
V1 rejects both new fields. It reads a raw image of
**exactly 132,120,576 bytes**, imports all sectors through the private population
hooks, and publishes only after complete validation. Begin is allowed only
from an empty or recognised unpublished store; a published store refuses the
import without modification. The source is opened read-only and never written,
renamed or removed. The result belongs to the content's own `hd-riscpc` store
and uses ordinary recovery rules. It does not rekey the content or select a
foreign store. Shipping routes continue to reject RPC plans.

## 10. VFS/platform assumptions

Repository evidence: `libretro/arc_io.c` checks returned read/write counts, close,
rename/remove status and readback; it exposes no file/directory sync, exclusive
create, compare-and-swap or timeout. `arc_io_flush_file` returns true if the
flush callback is missing: RPC admission MUST explicitly require the callback
instead of inheriting that weaker behavior. Truncate is optional; lack of it
means torn-tail read-only recovery. Listing cannot distinguish end from readdir
failure; successful close does not repair that ambiguity. Root presence must
come from fixed-path stat/read, never an apparently empty listing.

The pinned [RetroArch 1.22.2 VFS implementation](https://raw.githubusercontent.com/libretro/RetroArch/v1.22.2/libretro-common/vfs/vfs_implementation.c)
uses `fflush` for its flush operation, `_wrename` on modern Windows and `rename`
on native non-Windows paths. This is source evidence, not certification of the
installed frontend build, filesystem, or power-loss ordering. SAF is a separate
path; native-path admission excludes it.

| Step/assumption | Windows | Android native/emulated storage | macOS | Safe behavior without stronger guarantee |
|---|---|---|---|---|
| Exact reads, seeks, sizes, writes and close return results accurately | Required; device-test actual VFS | Required; device-test actual VFS | Required; test actual VFS | Wrong length/status/readback → refusal, no acknowledgement |
| Rename into an empty same-directory leaf preserves whole candidate under process kill | **Open assumption**, not an atomic-replace claim | **Open assumption**, including emulated storage | **Open assumption** for supplied VFS | Preserve old root until final verification; torn/invalid published leaf causes visible damage/refusal, never silent fallback |
| Replace an existing leaf atomically | Not relied on | Not relied on | Not relied on | Destination empty; retire only older verified root while newest stays recoverable |
| Flush drains process buffers; accepted bytes survive process termination | **Open assumption** for frontend; validate by termination tests | **Open assumption** for frontend; validate on Odin3 | **Open assumption** for frontend | Missing callback refuses writable admission; failed flush → ATA error/read-only. Successful but dishonest flush cannot be detected reliably |
| Durable file and directory sync after rename | Unavailable through wrapper | Unavailable through wrapper | Unavailable through wrapper | No power-loss promise; all reopened roots/prefixes checked, damage preserved and reported |
| Stat absence is reliable; no concurrent external edits | Required, unproved installed behavior | Required, unproved installed behavior | Required, unproved installed behavior | If uncertain refuse creation/retirement; never infer absence from listing |
| Listing completeness, case sensitivity | Not assumed | Case-insensitive storage already recorded in handoff; no case-distinct names | Not assumed | Advisory hygiene only; truthful unknown/alias entries refuse; failed/empty listing proves nothing. GC and debris reconciliation use fixed paths and retiring manifests |
| Maximum I/O latency | None | None | None | Virtual time unchanged; device host-budget failure returns to Fable |

D-147 resolves the `readdir` ambiguity: no correctness rule or bound relies
on listing. Retiring manifests remain until old-only GC completes; candidate
debris is found by 504 fixed-generation stats and the two temporary paths.
Only store-owned canonical files count toward the physical bound; foreign
entries remain untouched and outside it. A silently failing `readdir` must leave
all canonical-file recovery, maintenance and guest outcomes identical.

The runner VFS (`tools/rpc_runner_vfs.h`) performs `fflush` followed by `_commit`
or `fsync`, but does not sync directory metadata or currently supply truncate.
Runner results cannot establish RetroArch's stronger guarantees. Native Windows
and Odin3 save/reload/termination legs are mandatory; macOS is host evidence.
Use a fault VFS with no replace support, missing flush/truncate, case-folded
names and failed listing/stat in addition to the native VFS. No platform promise
may be inferred from a successful mock test. A stronger reboot/power-loss promise
or guaranteed wall-time latency would require a new accepted platform contract.

## 11. Fault-injection matrix

Apply the following matrix to **each invocation** of open/seek/read/write,
flush, close, rename, remove, stat and listing at the boundaries below, including
creation/population and every repeated chunk/record. Kill before and after each
operation and between cache/index update and acknowledgement. Inject short
writes at header/payload/trailer offsets, not just zero-byte failures.

Codes specify on-disc state / guest result / notice:

* **W:** old roots and all earlier commits retained; possible torn/whole
  unacknowledged last record / ABRT, latch read-only / saving-failed notice.
* **C:** old root plus bound journals retained, candidates unreachable; if valid
  publication already happened, retain new root too / next write ABRT, latch
  read-only / saving-failed notice. Reopen recovers every earlier acknowledgement.
* **K:** no live guest after kill / reopen old+successor or valid new root, all
  earlier acknowledgements; incomplete tail handled by §6 / recovery notice for
  torn tail, damage notice for invalid published content. Never select `.tmp`.
* **D:** retain every file, no mutation / damaged-open refusal, or IDNF read and
  read-only if detected live / damage notice; no zero fill or reformat.
* **U:** an unbound private first/population candidate only; source root unchanged
  / creation/population refused, no guest execution on candidate / creation or
  population failure notice. A published source may still be opened separately.
* **T:** incomplete final frame only: preserve/replay valid prefix, truncate only
  when proven safe / recovered writable if truncate succeeds, otherwise read-only
  with later writes ABRT / interrupted-write notice plus read-only when needed.

“—” means the boundary has no such operation; inject that fault at the next
applicable operation. Corruption columns mean corruption of a **referenced**
object; corrupt unbound `.tmp` is abandoned, never promoted. CRC-invalid complete
journal tails are D, not T. Every code inherits the virtual-time rule in §7.

| Boundary | Short write | Failed flush/close | Failed rename | Full storage | Process kill | Corrupt chunk | Corrupt manifest | Corrupt journal tail |
|---|---|---|---|---|---|---|---|---|
| Command header append | W | W | — | W | K/T | D on use | D | T if incomplete; D if complete-invalid |
| Command payload append | W | W | — | W | K/T | D on use | D | T/D |
| Command trailer append | W | W | — | W | K/T | D on use | D | T/D |
| Command exact seek/count, flush, close, expected length (no record readback) | W | W (whole command may recover) | — | W | K (whole or absent unacknowledged command) | D on use | D | T/D |
| Index/cache update, backend return, ATA ack | — | — | — | — | K (whole command, including after ack) | D on next uncached use | D | D; intact complete record replays |
| ROTATE: successor header create/verify | C; <256-byte unbound header abandonable | C | — | C | K | D on use | D | D for complete-invalid header; incomplete unbound header abandoned |
| MATERIALIZE: base reconstruction | — | C on read-close failure | — | C on allocation/write | K | D **before any copied byte** | D | D |
| MATERIALIZE: chunk.tmp write/flush/readback | C | C | — | C | K | C for candidate; D for source | D | D |
| MATERIALIZE: chunk rename/final readback | — | C on read-close failure | C | C | K | C for candidate; D for source | D | D |
| MANIFEST: candidate write/flush/readback | C | C | — | C | K | D on use | C for candidate; D for root | D |
| PUBLISH: empty-slot rename/final verification | — | C on read-close failure | C (may leave valid new root; reconcile on reopen) | C | K; invalid published leaf → D | D on use | D for published slot | D |
| RETIRE: bounded revalidation, old-only GC, old manifest then journal removal | — | C on revalidation-close failure | — | C if operation fails | K (new root remains) | D on use | D | D |
| Population RETIRE: kill between source manifest and source journal removals | — | C on revalidation failure | — | C if removal fails | K: complete new root; leftover source journal recognised by POPULATION generation/length/digest, never replayed | D on use | D | D |
| GC: fixed old-only references/at most 16 removals | — | C on metadata/read-close failure | — | C if removal fails | K | D for referenced, unreferenced removed only by ownership rule | D suppresses GC | D suppresses GC |
| Open: root selection/replay | — | D on read-close failure | — | Read-only if mutation unavailable | K (no mutation before validation) | D on first use | D, preserve both slots | T/D; failed truncate → read-only |
| Create/populate: begin and private chunk writes | U | U | U | U | Source unchanged or unpublished first candidate | U for candidate, D for source | U for candidate, D for source | D for bound source; no private guest records |
| Create/populate: seal/header/manifest.tmp | U | U | U | U | Source unchanged or unpublished first candidate | U/D | U/D | D for bound source |
| Create/populate: final publish/retire | U/C | U/C | U/C | U/C | K (old/unpublished or complete new root) | D for selected chunk | D for published slot | T/D for selected journal |

Kill before and after every old-only GC removal and between old manifest and
old journal removal, after both checkpoint and population publication. Recovery
selects the new root and resumes inventory GC, tolerating already missing retired
chunks. Repeat the scattered stream with silently failing `readdir`; canonical
chunk high-water stays ≤1,008 and no correctness outcome changes.

Also inject out-of-memory, failed stat/opendir/closedir, missing callbacks,
generation wrap, wrong key/revision/geometry, two conflicting roots/successors,
record count 0/257, last-LBA overflow and stale callbacks. Failure must never
delete a retained root, skip an acknowledged successor or expose partial data.

## 12. Run B test/evidence plan

These new CTest names are required deliverables, **not tests implemented in run
A**. Wire component/fault tests into host CI and its ASan/UBSan selection; expose
test hooks/namespace guards on Windows exactly as other RPC tests do. Fixtures
are authored bytes, never firmware, games or user boot resources.

| CTest | Required observable evidence |
|---|---|
| `rpc-hd-formatter` | AC-5: independent five-zone FileCore parsing, allocation/reserved/free extents, root, checksums, 126 MiB and IDENTIFY agreement; A3020 hash unchanged |
| `rpc-hd-store` | AC-5: holes versus missing chunks, full-chunk pre-use verification including partial/full overwrite, command atomicity, last LBA, crossings, cache eviction when all entries dirty |
| `rpc-hd-store-bounds` | AC-5: allocation/physical file/temp/journal totals after every boundary; ≥512 MiB sequential and ≥64 MiB scattered without reopen; all earlier writes compared; adversarial cap → ATA failure/notice |
| `rpc-hd-store-faults` | AC-5: complete §11 Cartesian matrix, every repeated operation, valid uncertain commit, invalid full tail versus truncated tail, missing flush/truncate, no-replace and case-fold VFS, GC retains unchanged chunks and both roots |
| `rpc-hd-store-process` | AC-5/AC-9 support: fresh executable process kill/reopen at every boundary, including writes after snapshot/materialization/rename, two roots, interrupted creation and two consecutive checkpoints |
| `rpc-hd-population` | AC-5: candidate invisible, source unchanged on every abort/fault/kill, stale expected generation refused, source corruption before reconstruction, effective source overlays preserved even in untouched chunks, begin refused mid-checkpoint and ROTATE excluded throughout ownership, kill between RETIRE removals recognised, complete population published atomically, no boot-resource dependency |
| `rpc-ide-registers` | AC-5: frozen item-10 command matrix, 256-sector latch/one backend write, range/width/nIEN/IRQ/status/reset/cancel, absence, read failure IDNF and write failure ABRT |
| `rpc-ide-guest` | AC-5/AC-7: authored guest through real MMIO/scheduler/store, virtual timestamps, host VFS delay/outcome equivalence, reset/unload/stale generations, audio counts, two frontend pacings |
| `rpc-hd-plan` | AC-7: `hd_import=<file>` exact-size read-only raw import only from empty/unpublished state through population and real adapter/store; published-store refusal; content identity frozen across swaps/edits/model selection; shipping routes still reject RPC plans |
| `rpc-hd-frame-cost` | AC-9 support: authored checkpoint workload exports per-state byte/operation counters (including indexed reconstruction, streaming ROTATE and bounded RETIRE), allocations, host frame time/T, open time and guest progress; no mock timing pass presented as Odin3 evidence |
| Existing `native-hard-disc-store`, `hard-disc-formatter`, `install-ide-registers` | AC-5/AC-6: run unchanged A3020 fixtures/assertions; identical store bytes and no writes outside RPC namespace |
| Existing `rpc-frontend-boundary-guard`, `rpc-symbol-guard`, `rpc-windows-test-exports`, `rpc-shipping-route`, `rpc-session-switch`, `rpc-session-lifecycle` | AC-7: dormant reachability, namespace, ABI and Arculator→RPC→Arculator isolation remain intact |

Fable supplies AC-6 private A/B, AC-8 private FileCore save/install/cold-start and
catalogue-before-install replay, and AC-9 **native RetroArch** on Windows/Odin3:
save/reload, process termination, open≤1 s, peak PSS≤240 MB at mount/checkpoint/
reload, p95≤0.80 T/no frame>T while installing, and 20 sessions without growth.
Record command rates/locality and all high-water counters alongside results.
AC-10 remains owed. Host tests, analytic bounds and runner fsync cannot stand
in for these platform measurements or stronger durability guarantees.
