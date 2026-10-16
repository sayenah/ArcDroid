<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Slice 28 — guarded writes on old-map JFD discs

RW-flagged D-format JFD discs can now save changes when their only layout
obstacle is an irregular protection area in free space. This also covers old
maps that declare extra space beyond the physical disc, provided that extra
space is entirely free. New-map E/F images and other unsupported layouts
keep their existing write protection.

ArcDroid preserves captured protection sectors and never creates sectors in
an unavailable protection area or beyond the filesystem's physical capacity.
If a save needs that space, the game receives a disc error (typically
“Disc error 10”). Accepted map writes never increase unavailable allocation.
An object already on a protection track stays as it is; modifying it may fail
with a disc error. Map updates that add unavailable allocation are refused before
they are saved. Other writes remain available after a refusal.

On reload, `guard-violated` means the saved allocation map is invalid, changes
the declared disc size, claims beyond-disc space, or allocates unavailable
units the original map did not. ArcDroid disables writes
and inferred blank sectors, preserves every save-store file, and continues to
show saved captured replacements and created sectors. `Disk Writes: Off`
hides saved changes and restores the original disc presentation.

The source image is never modified. Saved changes use the existing version 2
store. As in Slice 10a, persistence is atomic per sector under process kill;
there is no filesystem-transaction or power-loss durability guarantee. A
multi-sector operation can stop between committed sectors.
