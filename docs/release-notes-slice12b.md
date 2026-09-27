# Slice 12b — JFD identification track

Native JFD discs whose first track-side omits standard sector IDs can now identify
as their captured FileCore format. ArcDroid presents those omitted IDs to READ
ADDRESS/READ ID only. It invents no sector data; a data read of a presented
ID-only slot still reaches Record-Not-Found. Existing C-11c allocated
blank reads and saved sector writes retain their own conditions.

At native preparation, a captured-only proof identifies a valid D, E or F map,
checks every stored sector on cylinder 0 head 0 against the standard schedule,
and requires a non-empty strict subset with no duplicates, bad CRCs, slow or
data-less sectors, shared payloads or coincident rotational positions. Old D
map extents are validated against the declared size before they are clipped
to the geometry, matching FileCore's clamped physical disc size. Every missing
sector must qualify in every valid map copy either through complete positive
free coverage or, on F-format only, through a bounded exception for allocated
system-fragment bytes below the boot block. That exception requires full
fragment-2 coverage and excludes both complete map ranges, the root's two
resolved sectors and the boot sector. It does not establish that those bytes
have no other references or protection use. All missing sectors must fit the
deterministic rotational schedule; the whole track-side is admitted or
refused together. L-format media and other track-sides keep their prior
behavior. The proof is recomputed on preparation from original captured
bytes and never stored in the JFD write journal.

`JFD identification:` logs one bounded decision per image preparation, with
the image number, native path, format, schedule, timing, captured and presented
IDs, admission basis (`free`, `prefix` or `mixed`), valid map-copy mask and
refusal reason (`metadata-unresolved` if a needed root cannot be resolved).
`JFD:` and `JFD writes:` retain
their existing formats.

The accepted residual risk is that a protection check expecting missing IDs
on a qualifying identification track may now see them. The strict layout and
free-map gates limit the affected media; the orchestrator's library-wide A/B
and protection-sentinel checks are the merge evidence. Identification does not
add saves to images whose C-11 layout remains unsupported. Native/ADFFS
routing, install behavior, source images, content identity and store format are
unchanged.
