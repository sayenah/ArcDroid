# Slice 15 — short raw floppy images

An accepted short raw image is treated as a prefix of its declared disc. With
the default **Short image tail: Missing** option (`missing` value), sectors
beyond the source return a disc error. Present sectors retain their source bytes,
and writes to them continue to use the selected Disk Writes mode.

**Short image tail: Zero-filled** (`zero-filled` value) exposes the complete
staged image, including any bytes previously written to its tail. Existing
full-size shadows are kept; switching back to Missing hides those bytes
without truncating or erasing them.
The source image is unchanged in either mode.

The raw reader refuses FORMAT when a track-side includes an absent sector.
The upstream 82C711 controller currently completes FORMAT without calling
that reader, so this refusal is not exposed on A3020. Short images
beyond the existing tolerance are accepted only for standard 800 K D and
single-zone E formats whose source map proves the entire missing area free.
The missing bytes are unknown; ArcDroid makes no claim that the original disc
tail was blank or unformatted.

Discovery can classify an application when its directory is present but its
`!Run` payload lies in the missing extent. A guest read of that payload then
fails with a disc error. The new media stamp invalidates remembered Slice 14
boot observations once; affected self-booting titles re-learn after one
detection.
