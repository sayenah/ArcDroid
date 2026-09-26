# Slice 10b — persisted install hard disc

Exact-profile E-Type, E-Type Compendium and Premier Manager install recipes
can publish to a project-formatted A3020 hard disc. The source package, support
disc and supplied JFD images remain memory-backed. The saved installation
gets a content-bound storage ID and a versioned, checksummed manifest. A
candidate is published only after ArcDroid checks the FileCore map,
directories, marker, generated run file, embedded JFD identity and expected
application tree.

Each IDE WRITE SECTOR(S) command is buffered in full and appended as one
checksummed journal record before guest completion. A failed append refuses
further writes for the session. Journal replay rejects malformed complete
records; an incomplete final append is truncated when writable and ignored
in memory when read-only. A failed truncation runs read-only for that session.
A journal over 1 MiB is offered compaction at load; a failed attempt keeps the
current committed generation writable. The in-session journal limit is 8 MiB.
Read-only sessions do not truncate, compact, clean up or otherwise write to the
host store.

The guarantee is process-kill atomicity for each IDE command. It does not
claim power-loss durability. A save spanning several commands is not atomic
as a group. The store does not lock across processes; do not run two writing
sessions against the same installation at once. Save states are unsupported.
Windows CI does not terminate RetroArch between `manifest-publish-start` and
the manifest rename; mock-VFS interruption tests cover that window.

The selected machine is A3020 when `Machine: Auto` and a persistent install
is eligible. `Install Saves: Temporary` keeps the previous RAM install flow.
`Disk Writes: Off` runs a saved installation read-only, or uses that RAM flow
when no saved installation exists. An unreadable, malformed or unsupported
saved installation is left intact for recovery.
