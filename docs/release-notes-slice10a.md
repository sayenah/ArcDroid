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

An omitted sector ID reports Record Not Found; a bad ID CRC reports a header
CRC error. A bad data CRC, slow sector, duplicate ID, shared payload, or
82C711 size mismatch reports a data CRC error without changing the disc.
Deleted-data writes and FORMAT/WRITE TRACK report write protection.
