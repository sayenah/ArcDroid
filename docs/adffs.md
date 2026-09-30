# Using a user-supplied ADFFS package

ArcDroid can use ADFFS to boot an eligible JASPP JFD image. Put
the ADFFS zip, unchanged, in the frontend system directory at
`arcdroid/adffs/`. The `.zip` extension is matched without regard to case. If
several zip files are present, ArcDroid uses the byte-wise lowest filename
whose package validates and records that filename in the frontend log.

ArcDroid is not affiliated with JASPP and does not include or distribute
ADFFS. Obtain information from the
[JASPP forum thread](https://forums.jaspp.org.uk/forum/viewtopic.php?t=722).

`JFD Boot` defaults to `Auto`. A single-image JFD for disc 1 of 1 boots
natively when its extended header permits writing, its native write layout is
supported, and its header OS mask is unspecified or includes RISC OS 3.1.
This keeps disc changes in ArcDroid's persistent JFD store even when an ADFFS
package is installed. An OS mask that excludes RISC OS 3.1, a protected image,
or an unsupported write layout retains the ADFFS path when eligible.
The rule applies regardless of `Disk Writes`; with `Disk Writes: Off`, a
qualifying image boots natively write-protected and a loader that must write at
boot may fail, so choose `ADFFS preferred` and follow the recovery procedure
below.

Choose `Native` to force the native path, or `ADFFS preferred` to bypass the
automatic native-writable rule. An existing native save still takes precedence
under `ADFFS preferred`, so saved changes never silently disappear.
`ADFFS Package` defaults to `Verified only`; `Allow untested` permits package
versions whose runtime files do not match ArcDroid's known profile.

To return an automatically native-writable image to ADFFS, select `ADFFS
preferred` and reset or reload the content. Existing native changes still take
precedence (`native-saves`), including changes written during boot before any
deliberate save; `Disk Writes: Off` does not bypass that check. If it still
boots natively, unload the content, move that image's
`<save>/arcdroid/jfd/<sha1>.*` overlay files aside (or choose an empty frontend
save directory), then reload with `ADFFS preferred`. Keep the moved files as a
backup. ADFFS does not see the native changes in those files.

On a disc with two launchable applications, ArcDroid's launch chooser makes a
one-time application pick. Its `Launch Chooser` option can show the choice
again later.

The ordinary ADFFS path supports one JFD image whose header describes a single
disc. It works on both WD1772 and New-IO machines. A zip containing one eligible
JFD image is eligible for the same path. Multi-image sets use the native path.
A single-image high-density JFD on an IDE-capable A3020 takes the native hard-disc
route under `JFD Boot: Auto`; `ADFFS preferred` bypasses that route.
For applications that need to install beside themselves on that disc, see
[Native hard-disc applications](hard-disc.md).

The two guest discs used for an ADFFS session are memory-backed and read-only:
changes to the mounted image are not written back, regardless of the `Disk
Writes` option. At boot, ArcDroid's project-authored launcher copies only the
package files needed by the selected title to a temporary guest RAM disc. This
lets ADFFS create its working script without modifying the package or writing
anything to host storage. References on Obey comment lines (whose first
nonblank character is `|`) do not add files to that per-title set.

If the package is absent or unusable, ArcDroid keeps the native boot path and
shows a short notice when user action may help.
