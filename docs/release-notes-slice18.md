<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Slice 18 — CMOS Boot flag and power-on Shift

RISC OS 3.00, 3.10, 3.11 and 3.19 now start with the guest's CMOS Boot flag intact. When that flag is clear, ArcDroid holds left Shift from machine construction so RISC OS performs its normal Shift-Break boot. The core releases Shift after one second of emulated time or the guest's first `OS_FSControl 15`, whichever happens first. The same behavior applies to ordinary, helper, ADFFS, install, reset and recovery constructions.

The per-content CMOS record now stores the Boot flag clear. Old records with the forced flag are healed on load; a guest may still set it and reset within the running machine. A loader that used a persisted Boot phase marker across a process restart will repeat its first pass. A real user Shift held at power-on now composes with ArcDroid's Shift and no longer suppresses auto-boot. The Launch options remain the way to control automatic launching.

Remembered boot observations use compatibility revision 2. Revision 1 records are discarded and regenerated against the constructed starting CMOS. No firmware, game assets or per-title rules are included in this change.

Frontend input cleanup now releases a held key or button through the normal guest input queue when a chooser opens, confirms, continues or cannot publish its choice. A key-down already queued for the running guest is retained. This also applies to sessions outside RISC OS 3 and avoids a guest key remaining stuck after a chooser transition.
