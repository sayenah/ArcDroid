# Slice 16: native JFD track-side quarantine

ArcDroid now opens a structurally valid JFD even when a track-side contains
sectors the native floppy model cannot represent. It hides that whole side
from native reads, IDs, writes and sector creation. Other sides remain usable,
and ADFFS receives the original decoded image bytes. A single warning in the
core log identifies each affected image and its first quarantine reason.

This is a fidelity limit: hidden sectors are not claimed to be absent from
the source. Large sectors, octal density, DiscOp 3 whole-track data, mixed
timing, repeated untimed ID/density pairs and schedules longer than a
revolution are not modelled.
Structural damage still rejects the image. The native write-layout gate can
protect the entire image when it cannot prove that hiding a side leaves all
free space safe. There are no new options or frontend notices.
