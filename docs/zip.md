# Loading game media from zip files

ArcDroid can load `.zip` files containing ADF, ADL, or JFD disc images. A zip
with one usable image behaves like loading that image directly. With several
images, ArcDroid chooses the starting disc from the member and zip names, then
exposes the rest through RetroArch's disc controls. The first rule below that
matches wins:

1. A member explicitly named as disk/disc 1.
2. A member whose title matches the zip's title. For example, `Title.jfd` is
   chosen from `Title.jfd.zip`, and `Title.adf` is chosen from `Title.zip`.
3. A set stored as `Game.adf/Game.adf` when every supported image is directly
   inside that same `Game.adf` directory.
4. Otherwise, the first member in natural full-path order.

1600 K F-format ADF images are supported on New-IO machines.

Within a rule, natural full-path order breaks ties. Every image after the
chosen one also stays in natural full-path order, so `Disc2.adf` comes before
`Disc10.adf`. An explicit disk/disc 1 name takes precedence over a zip-title
match.

Disk 1 recognition is ASCII case-insensitive and accepts `Disc1`, `(Disk 01)`,
`disc-1`, and `Disk 1 of 3`. The word `disk` or `disc`, its complete decimal
number, and surrounding text must be separated by the start/end of the title
or by spaces, tabs, `.`, `_`, `-`, parentheses, or square brackets. Names such
as `Disk10`, `Disk1a`, and `HardDisk1` do not mean disk 1. A title containing
both disk 1 and another disk number is not treated as disk 1.

Unsupported files, hidden path components, `__MACOSX` content, directories,
links, and unusable disc images are skipped. ArcDroid reports skipped disc
images; a zip with no usable supported image is refused. Classic stored and
deflated zip members are supported. Zip64, encrypted archives, other
compression methods, and other archive formats are not supported.

For zip entries made on Unix, a mode with no file-type bits is treated as a
regular file. Entries explicitly typed as directories, links, sockets, FIFOs,
or devices are not game media and are skipped.

The limits are 32 MiB per archive, 1,024 entries, 8 MiB per uncompressed
member, 32 supported disc images, and 256 MiB of prepared logical media.
ArcDroid validates the whole zip structure before extracting any member.

ArcDroid never modifies the zip. With `Disk Writes` set to `Original`, zip
members use ArcDroid's shadow copy instead. `Off` remains read-only.

RetroArch may pass either `game.zip` or `game.zip#Disc1.adf`. The member form
validates that the named member is a supported image, but it does not select a
different starting disc: image 1 and its identity still govern the set.
RetroArch names its own per-game overrides, remaps, and options from the zip
stem for a bare zip and from the member stem for the member form. Name the zip
like the image, or use the member form, when keeping those frontend-side
settings matters.

When scanning content in RetroArch, leave `Scan Inside Archives` off to create
one playlist entry per zip. With it on, RetroArch creates one entry per member;
entries naming qualifying image members boot ArcDroid's selected image 1 for
the zip. An entry naming something ArcDroid never treats as an image (hidden or
`__MACOSX` entries, links, directories, or unsupported extensions) is refused.
If a named image fails preparation but another image survives, the set still
boots its selected image 1 with a `Zip member omitted` notice.

ArcDroid keeps CMOS only in its write-through file under the configured saves
directory. `.srm` files created by earlier ArcDroid builds are inert and may be
deleted.

The disc that actually starts supplies the game's ArcDroid identity used for
CMOS, launch choices, and title profiles. Renaming a zip can therefore change
the selected disc through the zip-title rule and change that identity. A set
first used with the older natural-order behavior may likewise acquire the new
starting disc's identity after upgrading. ArcDroid does not migrate the old
identity automatically because it may legitimately belong to another set;
the new identity is the same one used when that starting disc is loaded
unzipped.

Playlist (`.m3u`) entries cannot point to zip files or zip members. Load the
zip itself instead.
