<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Slice 19 — native-writable JFD boot

With `JFD Boot: Auto`, a single-image JFD for disc 1 of 1 now boots natively
when its extended header permits writing, its native write layout is supported,
and its header OS mask is unspecified or includes RISC OS 3.1. This applies
even when an ADFFS package is installed, allowing the game's disc changes to
persist in ArcDroid's native JFD store. The former fixed list of native-routed
images is removed. Images whose OS mask excludes RISC OS 3.1, whose header
prohibits writing, or whose native write layout is unsupported retain the ADFFS
path when eligible. Install sessions remain on their existing path.

The rule applies regardless of `Disk Writes`; with `Disk Writes: Off`, a
qualifying image boots natively write-protected and a loader that must write at
boot may fail, so choose `ADFFS preferred` and follow the recovery procedure
below.

A native launch can show the game's own original-disc acknowledgement with
`Cancel` selected by default. Choose `OK` with the pointer; Return or pad B can
cancel and leave the desktop. ArcDroid does not confirm the game's dialog for
the player.

To return an automatically native-writable image to ADFFS, select `ADFFS
preferred` and reset or reload the content. Existing native changes still take
precedence (`native-saves`), including changes written during boot before any
deliberate save; `Disk Writes: Off` does not bypass that check. If it still
boots natively, unload the content, move that image's
`<save>/arcdroid/jfd/<sha1>.*` overlay files aside (or choose an empty frontend
save directory), then reload with `ADFFS preferred`. Keep the moved files as a
backup. ADFFS does not see the native changes in those files.
