<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Archimedes calendar year

Archimedes sessions now start with the host's current calendar year. Reset
keeps the session year; a year changed in the guest is replaced on the next
load.

## Upgrading

The first load of RISC OS 2/3 content after upgrading discards its previously
remembered boot result once. That launch behaves like first use, and ArcDroid
can learn the boot result again. Remembered application chooser picks are
unaffected.
