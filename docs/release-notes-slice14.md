# Slice 14 — boot-outcome fallback

With **Launch: Auto**, a disc whose own startup leaves RISC OS waiting at the
`*` command prompt can start its application through ArcDroid's existing launch
helper. If several verified applications are available, a chooser appears after
the failed boot. The original error screen may remain visible for a few seconds.
Using the controls before the prompt is confirmed cancels the automatic launch.
After ArcDroid has seen the prompt and offered the chooser or launched a helper,
a validated local boot observation lets later loads and resets skip the failed
boot until the disc contents, firmware, machine, CMOS settings or boot options
change. A game that saves to its own disc will usually show the failed boot
again after saving.

The core observes the operating system's command-line wait through ARM SWI
execution. It does not inspect the disc's boot program or interpret screen text.
Recovery discovers applications from the current disc after the boot, including
guest disc writes. The observation records only that the operating system
reached the prompt, not that an application works. It is discarded when the
effective media, firmware, starting CMOS, machine or boot options change.
The `Launch:` log retains its existing fields and adds
`phase=` and `boot-watch=`; recovery lines also include `recovery=`. When no
verified application can be selected, the guest remains at the prompt and one
notice says: “Disc startup stopped. No application could be selected
automatically.”

Recovery is attempted once per content load or frontend reset, within the
first 40 seconds of emulated machine time. BASIC's own `>` prompt and a game's
own key prompt are outside this rule. A program that exits to the operating
system's command line without user input in that interval is indistinguishable
from a failed boot and may also trigger recovery once.

Discs whose own boot takes over or reaches the desktop continue normally.
**Launch: Off**, an explicit application choice, ADFFS and install paths retain
their existing launch behavior.
