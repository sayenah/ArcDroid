# RiscPC hard-disc store contract (D-139)

Status: **accepted by Fable (D-145)**. Run A at `b4bc633`, amended by D-147 (R2c-1) for run B
on `slice/r2c-ide`; runs M/N apply D-156/D-158/D-159/D-160. This freezes work-order item 9 and the store interfaces
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
temporaries. ROTATE first creates an empty inactive journal and retains its writer across
one frame, without capturing S. Empty unbound journals are abandonable on open.
Journal headers are written directly in that **empty inactive slot**;
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

### R3b creation receipt (D-167; envelope version 2)

Version 1 remains exactly as above. A populated creation uses magic `ARCRHDM1`,
envelope version 2, length **24452**, the same 128-byte header and 504 entries,
then a 128-byte creation receipt at offset 24320 and CRC at 24448 over all
preceding bytes. Header reserved bytes remain zero. There is no separate receipt
file or guest sector: manifest rename publishes the receipt and root together.

| Receipt-relative offset | Field |
|---:|---|
| 0 | magic `ARCRBOOT` (8 bytes) |
| 8, 12, 16 | receipt version 1, populator revision 1, authored-step revision 1 or 2 |
| 20 | zero u32 |
| 24, 56 | HardDisc4 and PlingSystem SHA-256 (32 raw bytes each) |
| 88, 96 | created file count and file byte count (u64; after transformation, including authored assets) |
| 104–127 | zero |

Supported receipt tuples are `(1,1,1)` and `(1,1,2)`; new populations use
`(1,1,2)`. Validated revisions are exposed by the receipt API. Revision 1
opens unchanged with one manual-launch notice and no package access or generated
helper. Revision 2 is gate-eligible, which never proves the gate ran.
Unknown envelope/receipt/populator/authored revisions refuse before mutation,
including an authenticated unsupported recovery slot beside a supported root.
A version-2 manifest must contain a valid receipt; version 1 contains none.
The complete exact-length manifest (including receipt and CRC) is used in journal
parent bindings. Checkpoint/rotation/recovery preserve the receipt verbatim.
Guest writes only update sectors and journal records; neither can create or
alter the manifest receipt. A receipt proves creation provenance, not the current
integrity of boot files after guest edits.

Only an unpublished all-HOLE population can set a creation receipt. Existing
published stores with a known receipt return success without opening packages;
legacy stores without a receipt are unchanged with a notice. Interruption before
manifest publication restarts package preflight and population from scratch;
fixed-path candidate cleanup uses the existing bounded inventory (504 leaves,
`chunk.tmp`, `manifest.tmp`, and unbound journal header), never a guessed tree.
Relative to the version-1 bounds below, published owned storage increases by
at most 256 bytes to **272,811,720**, temporary storage by 128 to **286,728**,
and their combined ceiling by 384 to **273,098,448**. Two-manifest open payload
reads increase by at most 256 to **8,437,512**. The conservative tick hash
formula uses three extended envelopes: **18,949,292** bytes. Existing transfer,
callback, resident-memory and chunk/journal bounds still cover version 2;
geometry and guest-write contracts do not change.

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

Cache at most six 256 KiB **effective** chunk payloads, tagged with chunk index,
manifest generation and live sequence. Use deterministic LRU (index breaks ties).
Evict clean or journal-backed dirty chunks alike. If all are not yet checkpointed,
evict the LRU after its command has passed acknowledgement checks: the journal and
index are authoritative. Never pin dirty payloads until checkpoint or flush
them on eviction. Failure to reconstruct does not allocate beyond the cap.

A read miss verifies the entire referenced base into one scratch buffer (or
initializes an explicit HOLE), then applies indexed journal sectors in order.
Read only records named by the sector index that overlap this chunk; never
scan unrelated records. Group by command so each contributing framed record is
read/CRC-checked once per miss. Open each contributing journal once, visit its
indexed records in increasing file-offset order, and seek/tell only when the
confirmed read cursor differs from the next offset. A frozen reconstruction uses
one parent handle; a live miss can need one handle for each of the two bound
journals. Never reopen per record. A record's verification precedes copying *any* payload from it. This
can read up to the full ≤8 MiB journal set plus one chunk for a pathological
single-sector miss. The same rule applies to checkpoint reconstruction, using
only the frozen prefix. A cached live payload that contains post-boundary writes
cannot be used as the checkpoint's frozen payload. No 126 MiB allocation, mmap,
whole-journal payload retention or filesystem-sized formatter buffer is permitted.

Budget all store-owned buffers: indexes 2,064,384 bytes; six cached payloads
1,572,864; guest read-miss scratch 262,144; a **private checkpoint payload**
262,144; command verification 131,168; three manifests 72,972; other metadata,
paths, hash state and the one retained ROTATE writer within the existing allowance.
Total remains **≤4,889,964 bytes**, with 262,144 bytes released by exchanging
two of the former eight cache slots for one private payload. The existing IDE
131,072-byte latch is outside the store and reported separately. No checkpoint
payload or candidate entry retained between ticks aliases the guest scratch,
record buffer or effective cache. Guest reads/writes can run between every step.
Allocation counters and a structure-size assertion cover this arrangement.

## 4. Checkpoint state machine (runs M/N, D-158/D-160)

One bounded maintenance tick runs at the existing frontend frame boundary;
no worker, host deadline, scheduler event, catch-up credit or virtual-time change.
Population's synchronous offline operations retain their existing API; ownership
suppresses ticks until publish/abort. Their subsequent retirement and abort GC
use the same budgeted ticks below. Unload closes the sole retained writer without
forcing a checkpoint. Reset retains the store.

Each tick makes **at most four metadata callbacks**, failed attempts included:
open, close, flush, rename, remove, path stat and mkdir. An already-open handle's
cached size is excluded (D-151/D-158). Data callbacks and hashes remain counted
under the existing limits: requested bytes ≤9,437,712, total VFS callbacks ≤4,096,
hash input ≤18,948,908 and explicit copies ≤2,097,152 per tick. Tests independently
count actual VFS metadata calls and compare the store counter, including cleanup.
D-160 additionally permits **at most one namespace operation per tick**: create
(open with create/truncate mode), rename, remove or mkdir, failed calls included.
Update-existing and read-only opens are not namespace operations. Creating a
candidate counts even when it replaces an existing temporary leaf. Test VFSs
count the attempted callback independently, including failed creates.
A failed stat may consume stat + exact-path open + close =3 calls; allocation
failure after VFS open may consume open + cleanup close =2. No step relies on
success-only counts. Failure closes the retained writer immediately.

The 1 MiB request trigger remains. IDLE admits ROTATE; preparation never freezes
parent/index or binds S. New command admission always checks actual aggregate
journal lengths; the switching step separately reserves its 256-byte header.

| Phase / step | Metadata calls, including worst failure | Retained data and next step | Recovery after kill |
|---|---:|---|---|
| IDLE / admit | 0 | Requested → ROTATE / prepare | Selected root and current journal |
| ROTATE / prepare | ≤4: destination stat/absence probe, writer open (or its allocation cleanup); existing destination refuses | Keep an empty inactive writer; S/index/parent remain live → switch | Empty inactive file is unbound partial header, removed only after root validation; acknowledged parent writes remain |
| ROTATE / switch | ≤4: writer flush, close, fresh read open, close | Capture S/frozen index/digest **now**, write full header, verify fresh length/bytes, then switch commits; → MATERIALIZE | <256-byte unbound header abandoned; valid full successor binds exactly this S; no later parent append after failure; full-invalid header is damage |
| MATERIALIZE / reconstruct | ≤4: base open/close plus parent journal open/close, including failed-open cleanup | Fully verified frozen payload in private checkpoint buffer; no use of live scratch → write | Old root and both journals recover all acknowledgements |
| MATERIALIZE / write | ≤3: candidate writer open, flush, close | Encode header/CRC/hash; write complete `chunk.tmp`; → verify | Candidate is unreachable; old roots/journals retained |
| MATERIALIZE / verify | ≤2: fresh candidate read open/close | Verify full envelope, SHA-256 and CRC into private payload against candidate entry; → destination | Candidate remains unreachable; guest misses cannot overwrite verified payload |
| MATERIALIZE / destination | ≤3: stat/probe/close, optional private-leaf remove | Prove destination empty under exclusive ownership; → publish | Any removed leaf belongs only to candidate generation; old view remains |
| MATERIALIZE / publish | ≤3: rename plus fresh final read open/close | Verify full final chunk SHA-256/CRC; advance ascending dirty index; last chunk → MANIFEST | Candidate leaves are not roots; recovery discards/rebuilds them from bound journals |
| MATERIALIZE / zero alternative | ≤3: destination stat/probe/close and optional private-leaf remove | After reconstruction, declare HOLE only for fully verified all-zero payload; finish chunk | Old view remains; explicit candidate HOLE only |
| MANIFEST / write | ≤3: writer open, flush, close | Encode candidate root and write; → verify | `manifest.tmp` is never selectable |
| MANIFEST / verify | ≤2: fresh read open/close | Exact length and bytes/CRC; → PUBLISH | Old root and both journals suffice |
| PUBLISH / rename | ≤4: destination stat/probe/close or absence probe + rename | Only empty inactive slot; → verify | Valid published new root or old root + journals gives same acknowledged view; invalid published root is damage |
| PUBLISH / verify | ≤2: fresh read open/close | Exact length/bytes; switch selected root, retain successor offsets, invalidate effective cache; → RETIRE | New root + successor; old root is retiring inventory |
| RETIRE / manifest | ≤2: fresh root read open/close | Exact root length/bytes; → header | New root + successor; no removals yet |
| RETIRE / header | ≤2: fresh journal read open/close | Compare immutable header; → length/binding | Same, even if guest appended between checks |
| RETIRE / length/binding | ≤3: path stat with failed-stat probe/close | Check **current** expected length and streaming parent binding without rescan; → GC, or removals after GC | Same; current header is immutable, expected append length sampled in this step |
| RETIRE / manifest removal | ≤1: remove old manifest | Only after both complete validation passes and old-only GC; → journal removal | New root remains; parent journal is retirement debris recognized by its exact SUCCESSOR/POPULATION binding |
| RETIRE / journal removal | ≤1: remove old journal | Compact record metadata → IDLE; no next rotation/population before completion | New root + successor recover all acknowledgements, including writes between the removals |
| GC / one inventory leaf | ≤3: stat/probe/close and optional remove | Scan fixed manifest/inventory in RAM, at most one unreferenced leaf/tick; finish → RETIRE or IDLE | Retiring manifest stays until all old-only chunks removed; missing already-removed leaves are expected |

Namespace census per substep (before run N → after): IDLE 0→0; ROTATE
prepare 1→1, switch 0→0; MATERIALIZE reconstruct 0→0, write 1→1,
verify 0→0, destination 1→1, publish 1→1, zero alternative 1→1; MANIFEST
write 1→1, verify 0→0; PUBLISH rename 1→1, verify 0→0; RETIRE manifest/header/
length-binding 0→0, combined removals 2→two separate steps of 1; GC and abort
GC 1→1. These are worst-case attempted namespace calls, not successful changes.
No mkdir or hidden namespace cleanup occurs in any maintenance substep.
Synchronous offline create/import/population APIs retain their existing scope;
their maintenance retirement and abort cleanup obey both tick budgets.

An empty ROTATE writer is the only handle retained across ticks. It is closed
before fresh header verification, including all failures and teardown. The
existing file encodings are unchanged: no prepared header, third journal,
pointer file, backup or temporary journal is introduced. The existing <256-byte
unbound-header recovery rule covers the preparation boundary.

Candidate destination absence is established in its own step, then rename is
performed without a duplicate stat under §10's exclusive-writer/no-external-edit
assumption. Final readback still verifies the entire chunk. A zero reconstruction
uses the alternative cleanup step; it never skips validation of its source.
A tick never starts another chunk. GC scans remaining unchanged entries in RAM;
there may be one final empty-inventory tick. Abort GC inspects at most 16 RAM
inventory entries but performs at most one deletion per tick.

A checkpoint with N dirty chunks and at most N old-only leaves needs ≤5N
materialization ticks, ≤N+1 GC ticks and 15 control/admission/rotation ticks:
**≤6N+16 ticks** from one commit switch to the next immediately requested switch.
This includes both RETIRE validation passes. A first cold creation/population
is offline; subsequent maintenance uses these bounds. All-504 dirtiness remains
budgeted and recoverable but is outside D-159's sustained scattered envelope.

Maintain streaming journal SHA-256 on append/recovery. Reconstruction checks
only indexed overlapping records, once per contributing record, in increasing
file-offset order. Its base plus frozen-parent callbacks fit ≤1,600, and the
512-record case stays under the old byte/hash/copy ceilings. RETIRE never rescans
a journal. The per-step budget is an operation bound; the Odin3 FUSE + RetroArch
flush measurements remain the host-time gate, never inferred from the arithmetic.

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
generation. Remove at most one per tick; then remove old manifest, then old journal.
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
MATERIALIZE applies at most one chunk of indexed sectors, and a GC deletion moves
at most 1007×23 =23161 metadata bytes. Hash-internal state copies, zero fills
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
| Maintenance | One phase step/tick, ≤4 metadata callbacks and ≤1 chunk removal; never two chunk reconstructions | Tick operation trace, bytes, elapsed host time; frontend/runner parity |
| Published disk | At most 1008×262276 +2×24324 +8388608 =272811464 bytes, excluding ≤286600 temporary bytes | Peak physical owned bytes; sparse HOLE and unchanged-reference cases |
| Tick work | §4 state-specific limits; MATERIALIZE ≤9437712 transferred bytes and ≤4096 VFS operations, one chunk | Counters include verification/publication and failed calls; host frame cost measured |
| Open | ≤8437256 bytes of manifest+journal payload reads, bounded metadata; ≤1 s on Odin3 | VFS byte counts and wall time, cold and near-cap recovery |

New immutable chunks retained alongside old ones are **published candidate
storage**, included in the published-disk bound, not hidden as `.tmp` space.
There is no requirement for total physical storage to equal the logical 126 MiB.

D-159, retained under D-160, replaces the old peak-command/all-504-scatter floor.
Rates count **encoded journal bytes**, including all 96 command-framing bytes. Define a sustained
baseline and an additional **B=2,097,152-byte burst allowance**. For any interval
of n running frames, admitted workload bytes are bounded by `r*n+B`; the burst
can arrive within 60 frames (the proof tolerates earlier arrival). B is a single
allowance across an interval covering both retained journals, not an allowance
added independently to each checkpoint. Repeated bursts require intervening
traffic below r to replenish the allowance. Tests drive a baseline above 4,300
B/frame throughout every complete 600-frame window and add a full B-byte burst.
This defines workload traffic; it introduces no guest admission throttling.

* **Scattered:** eight one-sector commands/frame give r=8×608=**4,864 journal
  B/frame**, 4,096 guest bytes/frame. At most **80 distinct frozen dirty chunks
  per checkpoint** (writes through chunk boundaries count both). N≤80 gives
  6N+16≤496; use **H=500** conservatively. The baseline yields 2,918,400 journal
  bytes per 600 frames, above D-159's 2,580,000 floor.
* **Sequential:** one 32-sector command/frame gives r=**16,480 journal B/frame**,
  16,384 guest bytes/frame. Include unaligned starts and disc-end wraps. Induct
  P≤6 MiB: a contiguous interval of that payload length touches ≤25 chunks,
  hence 6N+16≤166; use **H=170**. The next parent's length is at most
  `256+max(r*H,1048576)+3r+B =4,948,448 <6 MiB`, including request/switch
  delay. Initial threshold crossing has the smaller bound. The sequential rate
  exceeds the scattered floor; no unbounded locality assumption is substituted.

For either stream, if a successor already exceeds the trigger when retirement
finishes, the combined parent/successor lifetime is at most two H-frame active
cycles. If it is below threshold and an idle fill intervenes, charge that prefix
as at most the trigger and cover the following H-frame active cycle plus three
admission/rotation frames. There are at most two 256-byte headers. Therefore
aggregate journal occupancy is bounded by:

`512+B+max(2*r*H,1048576+r*(H+3))`.

| Stream | Active-cycle term | Idle-fill term | Aggregate upper bound | Cap |
|---|---:|---:|---:|---:|
| Scattered (r=4864,H=500) | 4,864,000 | 3,495,168 | **6,961,664** | 8,388,608 |
| Sequential (r=16480,H=170) | 5,603,200 | 3,899,616 | **7,700,864** | 8,388,608 |

This applies a burst once to the complete retained interval, so a burst spanning
rotation is included without double counting. The proof is independent of host
latency and supports arbitrarily many wraps/checkpoints. AC-5 keeps ≥512 MiB
sequential and ≥64 MiB scattered without reopen, comparing after every checkpoint
and recovering all bytes in a fresh process. Tests also cover an exact B-byte
burst, requests during ROTATE preparation, guest misses between materialization
steps, the 512-record reconstruction, import/population and abort cleanup.

Traffic outside these envelopes, including a 504-chunk frozen scatter footprint,
retains the exact aggregate admission check: the first record that would make
actual journal lengths exceed 8,388,608 gets deterministic ABRT/read-only/notice.
No retry, capacity wait, asynchronous writer, virtual-time change or reduced
verification is permitted. D-160 keeps the four metadata calls and adds one
namespace operation per tick.
D-161 sets the Odin3 HD-work bound at **0.50 T for ticks making a namespace operation**,
and **0.20 T for every other tick and for write acknowledgement**. Total
AC-9 workload frame time still must not exceed T. The extra retirement tick
fits the existing conservative H=500/170, so both aggregate proofs and the
sequential parent-length induction above remain unchanged.
The new metadata and tick bounds apply even outside
the sustained envelopes; native FUSE frame costs remain a separate measured gate.

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

### R3b capacity and sharing (before allocation implementation)

The supplied shape is approximately 2,289 HardDisc4 entries / 31 MB expanded,
plus 63 PlingSystem files after overlap merging; the largest directory has 54
entries. One minimum fragment per entry would charge 2,352 fragments, or
294 MiB, exceeding 126 MiB. Sharing is mandatory.

The proposed strategy follows the [FileCore PRM, internal disc addresses](https://www.riscos.com/support/developers/prm/filecore.html)
and [FileCore extensions, sharing](https://www.riscos.com/support/developers/prm/filecorenew.html):
sharing is confined to siblings, optionally with their parent directory at the
start; never pack unrelated parents or two directories into one disc object.
Objects are sector aligned. An indirect address is `(fragment_id << 8) | (s+1)`
for shared sector offsets s=0..254; an unshared object uses offset zero.
Keep share_size=0 for RISC OS 3.50. Ordinary directory objects start at sector
offset zero (shared encoding 1); the reserved root keeps address 0x20b. Eligible
small child files follow their directory; remaining files form sibling-only groups.
Empty files use address zero. Larger files may have several fragments, resolved
from id/254 with zone wraparound (object 2 starts in zone 2).

The usable map has 16,128 units, with 32 units reserved for object 2: **125.75
MiB** available. For each sibling group, sector-round file sizes, include 2 KiB
for its directory when appropriate, split groups when the next start would
exceed sector 254, and charge each resulting disc object
`8192 * max(16, ceil(group_bytes / 8192))`, plus minimum-fragment padding at
zone splits and any zone-tail padding shorter than 16 units. The final sum must be <=125.75 MiB. This encoding lets guest
FileCore growth/deletion identify shared neighbours by directory entries.

The supplied metadata-only filtered/merged inventory was independently tallied:
**390 directories, 1,444 files, 30,430,101 file bytes**, maximum fanout 54 and
maximum component length 10. No inventory listing or individual package path
is retained in the repository. Per directory, reserve four sectors in its first
255-sector window; pack nonempty sibling files of at most 251 sectors first-fit
in decreasing sector-rounded size, using additional windows when needed. Each
window occupies a 128 KiB fragment; larger files have private disc objects,
rounded to 8 KiB, with a 128 KiB minimum. Equal-size files use FileCore name order.
The root keeps its existing reservation; charging a separate first window with
four unused sectors for its files is conservative and keeps the tally simple.

This calculation gives **434 shared windows + 22 private objects** (18,096,128
bytes for the latter): **74,981,376 bytes = 71.5078125 MiB**, leaving
**54.2421875 MiB** of the 125.75 MiB usable space. Reserve a conservative further
2 MiB for authored boot assets, new directories and fragment/zone padding:
**at least 52.24 MiB remains for game installs**. Repeating the tally after
startup relocation and all seven authored assets (6,470 bytes) gives 391
directories, 1,451 files, fanout still 54, **435 shared windows + 22 private
objects = 75,112,448 bytes**, leaving **54.1171875 MiB before zone padding**.
The production allocator was also exercised directly on that metadata-only
transformed tree: **75,177,984 allocated bytes**, including 65,536 bytes of
fragment padding, leaving **56,680,448 bytes = 54.0546875 MiB for installs**.
No package content or inventory listing was used by a public test.
The authored tally adds one 128 KiB window. Five zone tails and four split
objects can add at most 135 padding units (1.055 MiB), so the total addition
over the original tally stays below the reserved 2 MiB. Population also
performs the exact allocation calculation on every supplied transformed tree
and refuses capacity exhaustion before publishing. Geometry is unchanged.

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

D-150/D-151: buffered `seek` returns zero on success (`fseeko` or `_fseeki64`),
while another VFS may return the resulting position. Every store positioned
access MUST accept a nonnegative seek result and confirm the exact offset with
`tell` before transferring bytes. A missing, failed or mismatching tell refuses
the operation. Count the tell callback in host-operation accounting. Preserve
the first failed operation in diagnostics when closing its handle for cleanup.
Handle `size` starts at the file length at open and is updated by that handle's
own writes and truncates; it does not reflect another handle's or process's
changes. Use stat or a fresh open for an independent length. Recovery and
publication readbacks use freshly opened read-only handles; acknowledgement
checks the journal's length with stat after close. Frontend test VFSs model
these per-handle size semantics and successful buffered seek-zero by default.
Replay confirms its initial journal offset once, then consumes records
sequentially using exact read counts, preserving the accepted open-work bound.

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

The runner VFS (`tools/rpc_runner_vfs.h`) defaults to `fflush` followed by `_commit`
or `fsync`; `--vfs-flush retroarch` uses `fflush` only for D-152/D-156 timing.
Neither mode syncs directory metadata or currently supplies truncate.
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
| ROTATE: empty writer prepare, then bind S/header/verify/switch | C; <256-byte unbound header abandonable | C | — | C | K | D on use | D | D for complete-invalid header; incomplete unbound header abandoned |
| MATERIALIZE: base reconstruction | — | C on read-close failure | — | C on allocation/write | K | D **before any copied byte** | D | D |
| MATERIALIZE: chunk.tmp write/flush/readback | C | C | — | C | K | C for candidate; D for source | D | D |
| MATERIALIZE: chunk rename/final readback | — | C on read-close failure | C | C | K | C for candidate; D for source | D | D |
| MANIFEST: candidate write/flush/readback | C | C | — | C | K | D on use | C for candidate; D for root | D |
| PUBLISH: empty-slot rename/final verification | — | C on read-close failure | C (may leave valid new root; reconcile on reopen) | C | K; invalid published leaf → D | D on use | D for published slot | D |
| RETIRE: bounded revalidation, old-only GC, old manifest then journal removal | — | C on revalidation-close failure | — | C if operation fails | K (new root remains) | D on use | D | D |
| Population RETIRE: kill between source manifest and source journal removals | — | C on revalidation failure | — | C if removal fails | K: complete new root; leftover source journal recognised by POPULATION generation/length/digest, never replayed | D on use | D | D |
| GC: fixed old-only references/at most one removal | — | C on metadata/read-close failure | — | C if removal fails | K | D for referenced, unreferenced removed only by ownership rule | D suppresses GC | D suppresses GC |
| Open: root selection/replay | — | D on read-close failure | — | Read-only if mutation unavailable | K (no mutation before validation) | D on first use | D, preserve both slots | T/D; failed truncate → read-only |
| Create/populate: begin and private chunk writes | U | U | U | U | Source unchanged or unpublished first candidate | U for candidate, D for source | U for candidate, D for source | D for bound source; no private guest records |
| Create/populate: seal/header/manifest.tmp | U | U | U | U | Source unchanged or unpublished first candidate | U/D | U/D | D for bound source |
| Create/populate: final publish/retire | U/C | U/C | U/C | U/C | K (old/unpublished or complete new root) | D for selected chunk | D for published slot | T/D for selected journal |

Apply the matrix before/after every step in §4, including retained-writer
preparation, frozen-payload write/readback, destination probe, final verification,
each RETIRE validation and the separate manifest/journal removal ticks. The
new boundary retains a bound parent journal after its old manifest is removed;
guest appends to the successor can intervene and must survive fresh recovery.
Guest writes/read misses intervene between steps; every acknowledgement survives recovery. A failed step must close
its retained handle and preserve the first failing diagnostic.

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
| `rpc-hd-reconstruction` | AC-5/M1: 512 single-sector records, confirmed sequential cursor, bounded miss/tick VFS callbacks |
| `rpc-hd-qa-integrity` | Q04/05/11/12/16: independently CRC-valid integrity failures and fixed-path interrupted-candidate reconciliation |
| `rpc-hd-import-roundtrip` | Nonzero full-disc import, every-sector readback/reopen/checkpoint, source unchanged, second-import refusal and bounded abort GC |
| `rpc-hd-store` | AC-5: holes versus missing chunks, full-chunk pre-use verification including partial/full overwrite, command atomicity, last LBA, crossings, cache eviction when all entries dirty |
| `rpc-hd-store-bounds` | AC-5: allocation/physical file/temp/journal totals after every boundary; ≥512 MiB sequential and ≥64 MiB scattered without reopen; all earlier writes compared; adversarial cap → ATA failure/notice |
| `rpc-hd-store-faults` | AC-5: complete §11 Cartesian matrix, every repeated operation, valid uncertain commit, invalid full tail versus truncated tail, missing flush/truncate, no-replace and case-fold VFS, GC retains unchanged chunks and both roots |
| `rpc-hd-store-process` | AC-5/AC-9 support: fresh executable process kill/reopen at every boundary, including writes after snapshot/materialization/rename, two roots, interrupted creation and two consecutive checkpoints |
| `rpc-hd-population` | AC-5: candidate invisible, source unchanged on every abort/fault/kill, stale expected generation refused, source corruption before reconstruction, effective source overlays preserved even in untouched chunks, begin refused mid-checkpoint and ROTATE excluded throughout ownership, kill between RETIRE removals recognised, complete population published atomically, no boot-resource dependency |
| `rpc-ide-registers` | AC-5: frozen item-10 command matrix, 256-sector latch/one backend write, range/width/nIEN/IRQ/status/reset/cancel, absence, read failure IDNF and write failure ABRT |
| `rpc-ide-guest` | AC-5/AC-7: authored guest through real MMIO/scheduler/store, virtual timestamps, host VFS delay/outcome equivalence, reset/unload/stale generations, audio counts, two frontend pacings |
| `rpc-hd-plan` | AC-7: `hd_import=<file>` exact-size read-only raw import only from empty/unpublished state through population and real adapter/store; published-store refusal; content identity frozen across swaps/edits/model selection; shipping routes still reject RPC plans |
| `rpc-hd-frame-cost` | AC-9 support: authored checkpoint workload at §8’s sequential rate exports per-state byte/operation/metadata/namespace counters (including indexed reconstruction, streaming ROTATE and bounded RETIRE), allocations, host frame time/T, open time and guest progress; no mock timing pass presented as Odin3 evidence |
| Existing `native-hard-disc-store`, `hard-disc-formatter`, `install-ide-registers` | AC-5/AC-6: run unchanged A3020 fixtures/assertions; identical store bytes and no writes outside RPC namespace |
| Existing `rpc-frontend-boundary-guard`, `rpc-symbol-guard`, `rpc-windows-test-exports`, `rpc-shipping-route`, `rpc-session-switch`, `rpc-session-lifecycle` | AC-7: dormant reachability, namespace, ABI and Arculator→RPC→Arculator isolation remain intact |

Fable supplies AC-6 private A/B, AC-8 private FileCore save/install/cold-start and
catalogue-before-install replay, and AC-9 **native RetroArch** on Windows/Odin3:
save/reload, process termination, open≤1 s, peak PSS≤240 MB at mount/checkpoint/
reload, p95≤0.80 T/no frame>T while installing, and 20 sessions without growth.
Record command rates/locality and all high-water counters alongside results.
AC-10 remains owed. Host tests, analytic bounds and runner fsync cannot stand
in for these platform measurements or stronger durability guarantees.

`--hd-cost` reports the executed maintenance `substep` (captured before the
tick) and its attempted `namespace_operations` (captured after it), separately
from `next_phase`. The D-161 checker attributes total HD work to phase/substep,
checks acknowledgement independently at 0.20 T, and permits total ≤0.50 T only
when that tick made one namespace call. Rows with more than one are invalid;
measuring clocks never changes virtual execution or persistent bytes.

## 13. R3b population implementation and authored step

`libretro/rpc_boot.*` owns one offline creation. `begin` checks store provenance
before touching packages. `step(n)` advances at most min(n,32) deterministic work
units; cancellation aborts the candidate. Package indexing and extraction use
`rpc_boot_zip.*`, independently of the unchanged `core/zip.c` and `core/adffs.c`.
All reads use frontend VFS. Each raw package is capped at 32 MiB, each at 4,096
entries, each expanded member at 16 MiB, combined expanded data at 96 MiB;
there are at most 4,096 transformed nodes, depth 32, fanout 77 and names of 10
FileCore bytes. ZIP member paths and transformed paths are capped at 255 bytes
each. These cover the current inventories with margin (raw HardDisc4
2,289 entries and PlingSystem 88, against 4,096 each; merged 1,834 entries
and 30,430,101 file bytes against 4,096 nodes and 96 MiB; the largest
filtered member is 2,280,588 bytes, over seven times smaller than the member cap).
The supplied metadata paths reach 104 bytes / depth 14; startup relocation adds
at most nine bytes / one level, still well below 255 / 32. The 32 MiB raw-input
limit also exceeds the handoff's approximately 31 MB expanded HardDisc4 size,
leaving room for ordinary ZIP framing even without compression. Metadata extra
fields/comments remain bounded by ZIP's 16-bit framing. ZIP64, encryption,
unsupported methods, duplicate/overlapping local records, traversal and missing
or mismatched ARC0 fields refuse. CRCs are checked for every member, including
excluded/omitted members. Implied/created directories use load/exec zero and
attributes 3 (plus the FileCore directory flag); explicit ARC0 takes precedence.
ARC0 attributes use the OS_File access byte. Owner/public read/write bits stay
in place; OS_File lock bit 3 maps to Nick lock bit 2, while Nick bit 3 marks a
directory independently. Bits with no Nick representation refuse. The independent
reader returns OS_File attributes and checks Nick's reserved bits. The mapping
follows the [FileSwitch attributes](https://www.riscos.com/support/developers/prm/fileswitch.html)
and [FileCore NewDirAtts](https://www.riscos.com/support/developers/prm/filecore.html)
tables, rather than the trial builder's direct-byte assignment. Filename syntax
also excludes FileSwitch's `%` library-directory marker; other printable bytes
remain representable, including slash from a ZIP dot.

Additional resident memory is <=8 MiB (fixed indices/tree/buffers plus bounded
zlib allocations); no whole archive, expanded member or disc is allocated.
One work unit performs one 32 KiB hash/inflate/write/readback quantum, one index
entry, one bounded tree/directory operation, one seal-chunk check or one control
operation: <=4 MiB I/O and <=128 callbacks, measured deterministically. A call
with n units multiplies those maxima by min(n,32). Index-entry comparisons are
bounded by 4,096 preceding entries; a transformed insertion by 32x77 siblings;
allocation/sorting by 77 siblings and five zones. No host clock changes work or
output. The existing synchronous seal API loops over the new seal-step API;
legacy caller output remains identical. Seal forbids further candidate writes.
Deflate may produce a partial sector before its stream ends. Writing retains
at most 511 bytes between units, emits complete sectors, and pads only the final
sector. Thus an output write is at most 33,280 bytes (32 KiB plus one sector),
within the same I/O/callback bounds; no member-sized buffer is introduced.

Preflight hashes both complete packages and pins SHA-256 for each initial
32 KiB archive range. Every subsequent index, member or output read verifies
the complete touched range before copying any bytes, preventing a changed or
changed-and-restored package from mixing snapshots. It validates framing/metadata, then
streams every member and CRC/hash. Tree transformation implements D-167's
restricted merge (SHA-256 byte equality, exact attributes, equal file type;
newer 40-bit date retained for typed files). Untyped load/exec must match exactly.
Case aliases and collisions refuse; directory metadata stays with HardDisc4.
Each archive-root file keeps its RISC OS name under its own release-doc directory;
dots map to slash characters in names. The exact numeric exclusions precede
FileCore name/depth checks. The candidate is formatted and populated only after
preflight, transformation and allocation succeed. Its boot block, maps,
directories and file hashes are read back against the expected tree before seal.
After receipt encoding and seal, both packages are reopened and SHA-256 checked
immediately before publication. This detects changed supplied content, including same-size
replacement. The initial pinned snapshot supplies every consumed byte; final revalidation
checks the current paths against it. The VFS has no atomic multi-file snapshot
or package lock. No package writes are performed.

The test-only plan-v2 key is `hd_populate=on`; it implies hard_disc=on, conflicts
with hard_disc=off and hd_import, and is rejected by v1 and shipping routes.
The plan opens with create_blank=false, drives the same step API offline, and
attaches IDE only after a complete root. R3b-2 supplies frame-loop preparation.
A known receipt is a no-op without package I/O; legacy roots get a notice and
remain unchanged. Unknown receipt or envelope revisions refuse before mutation.
Qualified package SHA-256s are diagnostic only; they never gate acceptance.

### Project-authored assets and custody (D-167(6))

All files in `assets/riscpc-boot/` are ArcDroid-authored **GPL-2.0-only**, adapted
from trial commit **5ff5481** (`origin/probe/riscpc-trial`); no third-party guest
bytes are embedded. The build script embeds exactly these eight files. Sources
use LF; all deployed scripts use CR only, while the MDF retains LF. Project
files and imported package data remain separately represented until candidate
sector writing. Each authored destination collision refuses. The intentional
active-startup replacements are possible because the supplied originals move,
unchanged in bytes and metadata, under `!Boot.Inactive`. That also holds for
PreDesktop and all retained numeric RO<n>Hook subtrees. Those relocated startup
files are never called by the authored step. Supplied System SysPaths/!Boot and
the Resources applications' !Boot entries do run.

| Repository asset | Destination | Type | Source at 5ff5481; adaptation |
|---|---|---:|---|
| Multisync | $.Multisync | FED | probe/riscpc/boot/Multisync; published timings retained, LF MDF |
| BootEnv | $.BootEnv | FFF | probe/riscpc/boot/BootEnv; trial labels removed; Choices creation made idempotent |
| BootRes | $.BootRes | FFF | probe/riscpc/boot/BootRes; resource dispatch retained; trial ResAudit output removed |
| BootTasks | $.BootTasks | FFF | probe/riscpc/boot/BootTasks; installed-task enumeration retained |
| Run | $.!Boot.!Run | FEB | probe/riscpc/boot/HDRun; ProbeTasks renamed ArcTasks, trial label removed |
| Boot | $.!Boot.!Boot | FEB | probe/riscpc/tools/boot_disc.py:134–160; authored IconSprites entry, trial label removed |
| ArcGate | $.ArcGate | FFF | authored R3c-3a; bounded gate described in `riscpc-launch-gate.md` |
| ArcTasks | $.!Boot.ArcTasks | FEA | probe/riscpc/boot/HDTasks; desktop-stage dispatch retained, destination renamed |

Authored files have zero date stamp and attributes 3. The boot option is run (2)
in the boot record and both map records, with all checks recomputed. Root,
boot/map reservations, sector size, map scale and five-zone geometry remain R2c's.

At guest startup BootEnv creates missing Choices/Boot/PreDesk/Tasks directories
through ordinary guest FileCore calls. BootRes creates `$.ResBoot`, a CR-only
Obey file (FEB) containing the resource dispatch commands, through the guest.
These runtime outputs use guest-assigned dates/attributes and are not embedded
assets or part of the creation receipt's initial file/byte counts. ResAudit is
not generated.


## R3b-2a operation inventory and stepping plan (D-168)

This inventory is written before implementation. A step is deterministic work,
never a host-time deadline. Namespace calls (including failed calls) are create,
mkdir, rename and remove. The device gates remain 0.20 T without a namespace
call and 0.50 T with one; only Fable's Odin3 FUSE/RetroArch-flush measurements
can establish those time limits.

| Surface | Indivisible work | Planned suspension boundaries |
|---|---|---|
| Directory preparation | one stat or mkdir of a canonical prefix | one prefix per step, at most one mkdir |
| Advisory listing | opendir; readdir/name/type; closedir | one entry per step; listing is never absence evidence |
| Open manifests | fixed-path stat/absence probe; open/size/read/close; CRC, framing and identity | one slot per control step; no mutation until all slots and bindings validate |
| Journal recovery | header read; one framed record read, CRC, sequence/index update, streaming SHA | bounded record batch; retained cursor; a torn tail is decided only at EOF |
| Recovery inventory | one retained/candidate leaf stat; candidate removal; truncate/flush/close | stat and removal on different steps; one candidate per step |
| Population reads/writes | verified base chunk; indexed framed overlays; copy of at most 128 KiB input | retain reconstruction state; bounded overlay batch; copy precedes private publication |
| Chunk publication | SHA/CRC/envelope; create/write/flush/close; temporary readback; destination stat/remove; rename; final readback | separate write, verify, destination, rename and final verification steps; one namespace operation |
| Seal | inspect one chunk's frozen offsets; materialize overlays; verify referenced chunk | one chunk/job step; journal creation and verification separated from manifest preparation |
| Receipt | encode 128 bytes of host provenance into candidate manifest | memory-only step; no separate receipt file |
| Manifest publication | encode CRC; create/write/flush/close; temporary readback; destination stat; rename; final readback; switch indexes | each control transition separately; rename is the root/receipt publication boundary |
| Retirement | root/header/length binding read; one old-only chunk removal; manifest removal; journal removal | existing retirement ordering retained; at most one namespace callback |
| CMOS inspection | canonical directory preparation; listing entry; fixed-slot existence probe; read/size/close; 512-byte decode | retry reported inspection failures at most three times; never admit a seed while incomplete |
| CMOS persistence | encode; destination inspection; create/write/flush/close; candidate readback; older-slot removal; rename; final readback | explicit steps; retain immutable candidate bytes and reconcile after ambiguous rename/readback |
| ZIP population | package open/size; pinned range read; central/local framing; one inflate quantum; one directory allocation | existing 32 KiB inflate limit; separate store job pumping from package work |

The synchronous APIs remain adapters which drain these steps for offline test
clients. Frame clients use the step APIs once per frame. Larger caller quanta
cannot combine multiple namespace calls into a single frame step. Encodings,
geometry, checksums, receipt bytes and guest acknowledgement are unchanged.
Counters expose requested transfer bytes, callbacks, metadata and namespace
attempts, hashing and copying, including failure cleanup. Tests compare these
against independently counted VFS operations and compare complete output across
caller quanta. Shipping RPC admission remains closed.


### R3b-2a implemented step bounds and compatibility

The step APIs are `arc_riscpc_hd_open_begin/open_step`, population
`io_begin/io_step`, `seal_step` and `publish_step`, and CMOS
`store_begin/store_step` and `store_put_begin/store_put_step`. A nonzero
`arc_riscpc_boot_step` advances exactly one unit, including when its caller
requests 8 or 32. Zero does no work. Synchronous adapters drain the same units
for offline callers; the plan client remains synchronous until R3b-2b adds
session orchestration. No production load reaches these APIs.

| Counter / indivisible limit | Per HD preparation or maintenance step |
|---|---:|
| Requested transfer bytes | 788,612 = 262,276-byte verified base + 526,336-byte framed-overlay batch |
| VFS callbacks, including failure cleanup | 1,600; a maximally gapped reconstruction needs at most 512 × 3 + 8 = 1,544 |
| Metadata callbacks | 4; advisory directory callbacks are separately counted as callbacks |
| Namespace attempts | 1 |
| Hash input | 1,572,864; full base SHA/CRC plus bounded batch CRC fits this ceiling |
| Explicit copies | 2,097,152; largest control copy is the 1,032,192-byte frozen index plus manifest/header |
| Journal recovery batch | at most 32 records; at most 262,208 framed bytes including the last whole record |
| Reconstruction batch | at most 512 contributing records and 526,336 framed bytes; the next record waits when it would exceed the byte allowance |
| CMOS step | at most 4 callbacks, 512 requested bytes, one namespace attempt; at most 3 inspection attempts |
| ZIP index step | one central/local/descriptor range, at most three pinned 32 KiB blocks; extraction stays at 32 KiB output |

Every framed record is fully validated before any of its sectors are copied.
Reconstruction resumes with a private record buffer while guest reads and writes
use their original buffers. Five effective-cache slots plus that private
131,168-byte record buffer replace six slots without increasing the existing
4,889,964-byte resident ceiling. No persistent format or guest byte changes.
The reduced transfer/callback/hash ceilings supersede the older coarse per-tick
ceilings above; guest acknowledgement remains the original whole-command path.

Under the two sustained envelopes in section 8, a frozen chunk's contributing
records fit one reconstruction batch: scattered records are at most 512 × 608
= 311,296 bytes; sequential intervals contribute at most one chunk payload plus
two edge-command payloads and their bounded framing, below 526,336 bytes.
Thus the original five materialization steps and 6N+16 / H=500,170 occupancy
proofs remain. Outside those envelopes, reconstruction may need several steps;
the exact journal admission cap still applies. The full and sanitizer stream
oracles verify the original cycle and aggregate limits.

Open distinguishes unpublished, legacy and receipted valid stores from
unsupported, damaged and unavailable refusals. In a known manifest it verifies
length, content identity, CRC and entry framing before classifying an unknown
receipt/populator/authored revision. Unknown envelopes are opaque and never
used for cleanup or fallback. Unbound partial journal headers must have a known
magic prefix; unfamiliar bytes are preserved. Both roots and journal bindings
are validated before candidate cleanup. Notice strings are exposed separately
from production loads.

CMOS `reconciled` gates payload use, seeding and new publications. A reported
inspection error retries without clearing a corruption refusal. Exhaustion is
unavailable, not missing. A fixed-path stat and read-only probe supplement the
listing, so a silently short listing is not absence evidence. As in section 10,
a frontend that simultaneously falsely reports absence through both stat and
open cannot be distinguished from actual absence. Found-plus-refused remains
available to the old plan client for its existing read-only fallback; no seed
or publication can replace either refused slot. An ambiguous publication must
reconcile before another put; the newest verified generation remains authoritative.

The authored package output is byte-identical to the branch parent `2efcfee`
(R3b-1 `0a18209` plus main) at quanta 1, 8 and 32, including every envelope,
chunk, receipt and journal. Hardware timing remains a separate Fable gate:
these operation bounds do not certify 0.20 T / 0.50 T on the Odin3.


### R3b-2a verification profiles

Ordinary builds keep every in-memory and real-process fault/termination cut.
Sanitizers use three explicit profiles, each with a 900-second timeout:

- Boot population retains every callback class and each phase, with fixed
  class labels and ordinals; failure-before and termination-after cuts are
  distinct. It runs two independent save trees concurrently.
- The real-process store matrix retains every state/substep/population/file-kind/
  callback/side class from its pinned trace, also in two independent save trees.
- The in-memory store matrix retains every mutation callback (create, write,
  flush, remove, rename, mkdir and truncate), and the first and last inspection
  callback in each file-kind/transfer-size class. All command, corruption,
  pre-use verification and retirement checks remain exhaustive.

Native path-prefix stat probes have platform-dependent ordinals. Profiles
canonicalise only those probes, verify their callback class set and 2,048-call
ceiling, and still inject them separately. Every remaining ordinal stays pinned.
A verified torn-tail prefix remains legacy/receipted and readable when truncate
is absent or fails, with a read-only notice. This is a write-capability failure,
not a failed inspection of the already verified root. Unknown/unverified roots
never expose a backend or a receipt.

### R3b-2a run B classification and independent verification

Manifest inspection authenticates any same-magic frame through 131,168 bytes
(the existing record scratch capacity), using its length, content identity and
trailing CRC before interpreting revisions. An authenticated unknown length or
revision is unsupported. Journal headers use the same bound and ordering,
including complete headers shorter than 256 bytes; only a supported header's
geometry and journal contents are interpreted. An envelope above the inspection
bound is opaque and refused as unsupported. Reported read/close failures remain
unavailable. Supported encodings and per-step limits are unchanged.

CMOS advisory enumeration stops at 4,096 entries and continues through both
fixed-path probes; reaching that cap never consumes a retry. Torn-tail recovery
with a verified committed prefix keeps the readable, read-only view and notice
when either the writable open or truncate fails.

The VFS test boundaries independently count requested transfer bytes (including
failed attempts), callbacks, metadata and namespace attempts. Coverage includes
a 240-file authored package with members through 1 MiB, a >5 MiB journal reopen,
overlapping journal reconstruction and checkpoint writes, and cancellation
before and after every publication step. The larger package's sanitizer profile
executes the whole workload at quantum 8; the host profile compares owned bytes
at 1, 8 and 32. The existing long fault profiles remain unchanged.
