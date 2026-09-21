# Using a user-supplied ADFFS package

ArcDroid can use ADFFS to boot an eligible, single-disc JASPP JFD image. Put
the ADFFS zip, unchanged, in the frontend system directory at
`arcdroid/adffs/`. If several zip files are present, ArcDroid uses the
byte-wise lowest filename whose package validates and records that filename in
the frontend log.

ArcDroid is not affiliated with JASPP and does not include or distribute
ADFFS. Obtain information from the
[JASPP forum thread](https://forums.jaspp.org.uk/forum/viewtopic.php?t=722).

`JFD Boot` defaults to `Auto`; choose `Native` to keep the existing JFD boot
path even when a usable package is installed. `ADFFS Package` defaults to
`Verified only`; `Allow untested` permits package versions whose runtime files
do not match ArcDroid's known profile.

The ADFFS path currently supports only one JFD image whose header describes a
single disc. Playlists, folder sets, and multi-disc titles use the native path.
The two guest discs used for an ADFFS session are memory-backed and read-only:
changes to the mounted image are not written back, regardless of the `Disk
Writes` option. At boot, ArcDroid's project-authored launcher copies only the
package files needed by the selected title to a temporary guest RAM disc. This
lets ADFFS create its working script without modifying the package or writing
anything to host storage.

If the package is absent or unusable, ArcDroid keeps the native boot path and
shows a short notice when user action may help. The two new options and these
notices are part of the Slice 6a release notes.
