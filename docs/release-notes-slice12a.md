# Slice 12a — launch discovery

ArcDroid now launches a discovered application when a disc has a boot option but no root `!Boot` object. A root `!Boot` file or directory still gives the disc control of its own boot sequence.

When the root contains no `!` directory, ArcDroid searches the immediate children of its ordinary folders for applications. The chooser displays these as `Folder.!App`. It searches one folder level only; a root `!` directory keeps root-only discovery.

The helper addresses a folder literally when every name byte is safe in a RISC OS command, including extended bytes, and the name identifies exactly one root object, including files. Folders whose names RISC OS cannot address on a command line are listed but cannot be launched automatically or offered as verified chooser targets. The same applies to ambiguous names.

Discovery reads at most one root, 77 parent directories and 77 application directories, each at most 2 KiB. It keeps at most 77 candidates. An unreadable parent or a truncated candidate scan suppresses automatic selection and reports inspection incomplete. Verified applications remain selectable; an explicit or remembered launch from an incomplete scan shows a warning. Names, records, scripts and diagnostics are bounded. These rules use disc structure only and contain no title-specific knowledge.
