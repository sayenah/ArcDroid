<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Slice 13 — session-scoped ROM-module unplugging

On RISC OS 3, loaders that write an older ROM-module unplug mask can now boot
through a soft reset. A content session whose persisted CMOS record had these
bytes set recovers on the next launch; its other settings remain in the same
record. ArcDroid acknowledges but does not store guest writes to logical CMOS
bytes 6, 7, 18, 19, 173–176 and 217–219 for RISC OS 3.00, 3.10, 3.11, 3.19
and the test OS. It clears those bytes in persisted records and machine boot
images. The running `*Unplug` command still removes the module immediately,
but the module returns on reset or relaunch and CMOS read-back reports it
plugged. This serves game behavior under P-7; JASPP's ADFFS also prevents
persistent unplugging under its boot-floppy mode, though by a different rule.

An ignored guest write logs one INFO line per content session with the byte,
attempted value and retained value. Healing a persisted record logs
`CMOS: cleared <n> unplug byte(s) from the persisted record`. The unload audio
summary includes bounded locked-write attempt and would-change counters.

RISC OS 2 and Arthur sessions, the RTC, configuration and user bytes, and game
saves retain their existing behavior. The CMOS record format and path do not
change.
