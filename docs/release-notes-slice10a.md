# Slice 10a — native JFD disc writes

RW-flagged extended JFD images can now keep native-path sector writes in the
save directory. ArcDroid never changes the source JFD or its archive. Saved
changes are shared by identical decoded JFD images across supported packaging.

Each sector is committed before the controller reports success. A process
interruption can leave a multi-sector filesystem update between sectors; the
guarantee is per-sector atomicity under process kill, not power-loss durability.
An interruption immediately after the commit rename can leave a saved sector
whose success was not yet reported to the guest.

Legacy, write-protected and unflagged JFDs remain read-only. FORMAT and WRITE
TRACK remain unsupported. `Disk Writes: Off` shows the original disc and hides
saved changes until writes are enabled on a reset or reload. A saved native JFD
continues to boot natively if an ADFFS package is added later.

RW-flagged images matching the bounded P-DD5 profile can create missing 1024-byte DD
sectors (IDs 0–4, 80 cylinders × 2 sides) when the complete track-side
placement is schedulable. On a writable native mount, an omitted eligible
sector reads as 1024 zero bytes with good CRC, and appears in READ ID/ADDRESS
scans, only while the disc's valid saved free-space map assigns at least one
byte of that sector to an object. This applies immediately if the base map
already allocates it, and after a committed map write if a guest allocates it.
Free omitted sectors retain Record Not Found. Reset, reinsertion and fresh
load derive the same view from the saved map; reads create no records.
A write to an omitted ID commits that sector and its data together; it then
appears in reads, address scans and native launch
discovery, including after a process restart. Captures with omissions that
cannot safely be created are protected up front with `layout-unsupported`.
Irregular track-sides remain non-creating; the ADFS free-space map must prove
that missing standard IDs there are outside allocatable space.
The zero fill does not apply with Disk Writes Off, write protection, an
unsupported layout, an invalid or missing map, a damaged saved slot, or an
ADFFS/install boot path.
Out-of-profile and irregular-track targets retain Record Not Found behavior.
An absent free sector probed by a guest READ SECTOR stays unformatted for the
rest of that mounted session. If a guest writes data there first, that data
write is refused; if it writes an allocating map first, the map write is
refused. Other writes remain possible. These probe and veto rules require a
valid base map and at least one valid current map copy; without one, the disc
uses the earlier omitted-sector write behavior.
The probe marks clear on reset or reinsertion. A guest that allocates a
protection slot before probing it in the same session can still break that
protection; no such workflow was observed. A sector probed by a scan cannot
be claimed until reset. FileCore's in-memory map after a refused map write
has not yet been verified against real-title recovery behavior.

A bad ID CRC reports a header CRC error. A bad data CRC, slow sector,
duplicate ID, shared payload, or 82C711 size mismatch reports a data CRC
error without changing the disc.
Deleted-data writes and FORMAT/WRITE TRACK report write protection.
