# Slice 26 — Disc requests

ArcDroid can insert a disc from the loaded multi-image set when FileCore asks for that disc by name, or when the same fully qualified floppy object lookup fails twice and the object exists on exactly one other image. A successful swap shows `Disc N inserted`.

The **Disc Requests** core option defaults to **Insert automatically**. **Hint only** leaves the media mounted and shows `Insert disc N with Disk Control`; **Off** disables request handling. Manual Disk Control remains available.

This first version admits only `adfs::0.$...` and paths qualified by the inserted disc's FileCore name. Unqualified paths, `:0.` paths, variables and search paths are outside its evidence rule. Tasks that call Wimp polling between lookups do not accumulate a request. A title that checks once per acknowledgement needs one further acknowledgement before automatic insertion. The prompt classes represented by library indices 68, 69 and 49 are not covered by these signals. The ADFFS path does not use automatic disc requests.
