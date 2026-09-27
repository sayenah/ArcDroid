# Slice 12a — launch discovery

ArcDroid now launches a discovered application when a disc has a boot option but no root `!Boot` object. A root `!Boot` file or directory still gives the disc control of its own boot sequence.

When the root contains no `!` directory, ArcDroid searches the immediate children of its ordinary folders for applications. The chooser displays these as `Folder.!App`. It searches one folder level only; a root `!` directory keeps root-only discovery.

The helper addresses a folder literally when its name is safe in a RISC OS command. Otherwise it uses the longest safe ASCII prefix followed by `*`, provided that pattern identifies exactly one root object, including files. An unsafe or ambiguous address is never launched automatically or offered as a verified chooser target.

Discovery reads at most one root, 77 parent directories and 77 application directories, each at most 2 KiB. It keeps at most 77 candidates. An unreadable parent or a truncated candidate scan suppresses automatic selection and reports inspection incomplete. Verified applications remain selectable; an explicit or remembered launch from an incomplete scan shows a warning. Names, records, scripts and diagnostics are bounded. These rules use disc structure only and contain no title-specific knowledge.
