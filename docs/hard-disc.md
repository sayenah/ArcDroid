# Native hard-disc applications

On an A3020 native session with `Hard Disc: Auto` and `Launch: Auto`, ArcDroid
can use the content's persisted hard disc at `ADFS::4`. When an application's
launch from a write-protected floppy ends after a refused write, ArcDroid
restarts with a project-authored guest installer. The installer creates
`ADFS::4.$.ArcDroid`, copies the application there and launches that copy.
`ArcInstall` and `ArcMark` register as Wimp tasks, then launch their hard-disc
targets with `Wimp_StartTask` using the exact `Run <path>.!Run` command. They
close down their Wimp tasks after the child ends or becomes a desktop task.
If that launch cleanly produces exactly one other runnable application in the
owned directory, ArcDroid records the produced application and launches it.
Later loads use the recorded application directly from the hard disc.
When the launch chooser is set to Ask, its **Installed** row names the
validated product; a remembered installed choice uses `.choice` v4 with the
product and attempt bound to the current discovery.
Choosing a floppy row, or remembering a floppy `.choice`, runs that floppy
application. Automatic decisions may use the installed product instead.

The guest installer writes only inside `ADFS::4.$.ArcDroid`. The host reads
FileCore structures to validate the installation; it never writes FileCore
structures. The helper disc is still drive 1, and the hard disc is drive 4.
The reader accepts FileCore's updated disc ID in the map record after a guest
write while checking the remaining disc-record fields and both map copies.
The install and product recovery helpers are published as `.install.adl` and
`.product.adl` alongside the ordinary launch helper. Neither recovery helper
carries a RAM disc.
There is no shipped application profile or title-specific install recipe.

The owned directory contains a copy of the source application, any application
it produced, and `Installed`, a bounded text record. Its CR-separated fields
start with `ARCINST1` and include `revision=1`, `state`, `attempt`, `source`
(the full floppy path), `media` (the effective source-view digest),
`fingerprint` (the discovered candidate set), `content` (the content identity),
and `os` (the OS class). A completed record also names `product`; a failed
record includes a bounded `error`. A guest program publishes a replacement
by writing and closing `Installed~`, removing `Installed`, then renaming
`Installed~` to `Installed`. A directory caught between remove and rename is
not considered an eligible installation.

`pending` means the copy was not confirmed. `started` means the one automatic
run of the copy was consumed, even if extraction later failed or no runnable
product appeared. Any player input during the unattended first run cancels
the automatic steps. An interrupted or cancelled install remains `started`
and is terminal until the directory is moved aside. A frontend reset may arm
a new launch attempt, but install and product recovery still run at most once
per session. `completed` launches only when the named product still
exists and the record matches the current source, media, candidate fingerprint,
content and OS. `failed`, `foreign`, `unowned` and `indeterminate` never
substitute a hard-disc application. ArcDroid continues the normal floppy
launch and shows a notice once per session. To try installation again, move
`ADFS::4.$.ArcDroid` aside on the guest hard disc first; preserve it if it
contains saves. ArcDroid does not automatically erase or retry it. A
read-only store can launch a validated completed product but cannot begin an
installation.

At the shipping `Disk Writes: Shadow` setting, a staged ADF opens writable,
so its write-protect refusal callback has no event to report. With `Disk
Writes: Off`, the ADF callback can report a refusal, but the hard-disc store
is read-only and automatic installation cannot arm.

`Launch: Off`, explicit `Application N`, ADFFS sessions, multi-image sets,
machines without the attached IDE disc, `Hard Disc: Off`, and unsupported OS
classes keep their existing routes. The mechanism does not copy whole discs,
run user-placed applications elsewhere on the hard disc, or handle applications
that require external System resources or their own disc installer structure.
