# Loading game media from zip files

ArcDroid can load `.zip` files containing ADF, ADL, or JFD disc images. A zip
with one usable image behaves like loading that image directly. With several
images, ArcDroid starts from the first and exposes the rest through RetroArch's
disc controls. Images are ordered by their full member paths using natural
number ordering, so `Disc2.adf` comes before `Disc10.adf`.

Unsupported files, hidden path components, `__MACOSX` content, directories,
links, and unusable disc images are skipped. ArcDroid reports skipped disc
images; a zip with no usable supported image is refused. Classic stored and
deflated zip members are supported. Zip64, encrypted archives, other
compression methods, and other archive formats are not supported.

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

Playlist (`.m3u`) entries cannot point to zip files or zip members. Load the
zip itself instead.
