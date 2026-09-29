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
or an unsupported write layout retains the ADFFS path when eligible. Choose
`Native` to force the native path, or `ADFFS preferred` to bypass the automatic
native-writable rule. An existing native save still takes precedence under
`ADFFS preferred`, so saved changes never silently disappear. `ADFFS Package`
defaults to `Verified only`; `Allow untested` permits package versions whose
runtime files do not match ArcDroid's known profile.

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
JFD image is eligible for the same path. Other multi-image sets use the native
path unless they match a verified install recipe.

Exact disc sets for E-Type and E-Type Compendium, and the single-disc Premier
Manager [SA] image, can be installed automatically under the verified package
and RISC OS 3.11. Put all required discs in one zip, playlist, or folder set.
The Compendium chooser offers each game whose required disc is present. The
install first uses a guest RAM disc. With `Install Saves: Persistent` (the
default), ArcDroid copies the completed installation to a blank A3020 hard
disc in the frontend's native save directory. After checking the installation,
ArcDroid restarts the guest into the installed game during the same session.
An in-game save is retained across restarts. `Machine: Auto` selects A3020
for an eligible install. Existing saved installations also require A3020;
choose `Machine: Auto` when another machine was selected explicitly.

The store is at `<save>/arcdroid/install/<storage-id>/`. Keep or back up this
directory with the rest of the frontend's saves; its disc, journal and manifest
files belong together.

`Install Saves: Temporary` uses the RAM installation path. **Saves made there
disappear on reset or exit.** ArcDroid shows this warning only for RAM install
boots. `Disk Writes: Off` uses an existing saved installation read-only. If
there is no saved installation, it uses the RAM path and shows the warning.
When a saved installation exists but the save directory is unwritable, it runs
read-only and shows a notice. Creating a persistent installation needs a
native writable save directory; choose a native directory or select
`Install Saves: Temporary` if it is unavailable. ArcDroid refuses a damaged
or unsupported saved installation with an error and preserves its files so a
backup can be restored. A different revision of any image or built-in script
stays on the native path.

ArcDroid's messages identify these outcomes:

- **Saved installation is damaged:** Preserve the install directory and restore
  a backup. ArcDroid does not repair or overwrite it.
- **Unable to create the install disc:** Check free space and save-directory
  write access, or choose `Install Saves: Temporary`.
- **Install disc save limit reached:** This session has reached the 8 MiB
  journal cap; further saves cannot be written until a later load compacts it.
- **Install disc storage failed:** A write failed; further saves cannot be
  written in this session.
- **Install disc is read-only:** The saved game can run, but saves cannot be
  changed in this session. This also follows a failed journal-tail truncation.
- **This title's saved installation runs on the A3020:** Set `Machine: Auto`.
- **Persistent install saves require Machine: Auto and a native writable save
  directory:** Choose those settings, or use `Install Saves: Temporary` for a
  RAM install.

The two guest discs used for an ADFFS session are memory-backed and read-only:
changes to the mounted image are not written back, regardless of the `Disk
Writes` option. At boot, ArcDroid's project-authored launcher copies only the
package files needed by the selected title to a temporary guest RAM disc. This
lets ADFFS create its working script without modifying the package or writing
anything to host storage. References on Obey comment lines (whose first
nonblank character is `|`) do not add files to that per-title set.

If the package is absent or unusable, ArcDroid keeps the native boot path and
shows a short notice when user action may help.
