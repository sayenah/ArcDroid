<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Slice 22 — ADFFS install path retired

ArcDroid no longer uses per-title ADFFS install recipes, install launchers,
install carriers, or the install variant chooser. Ordinary single-disc ADFFS
boots remain available. `Install Saves` has been removed; old global and
per-game values are ignored.

Old installed games and saves at `<save>/arcdroid/install/…` remain on disk
untouched, but ArcDroid no longer opens them. Keep that directory as a backup
or delete it manually when you no longer need it. ArcDroid does not migrate
these saves to the native hard disc.

Multi-disc JFD sets that formerly used the install path now boot natively,
even with the ADFFS package present. The two formerly installed multi-disc
cases reach their own disc prompt; swap discs through RetroArch disk control.
New saves reside in the writable data disc's JFD store. A reset retains the
currently selected disc index, so reselect disc 1 before resetting if the
game expects to start there.

`ADFFS preferred` no longer restores the install path. A formerly installed
single-disc high-density case then reaches plain ADFFS, whose script cannot
run it; use `JFD Boot: Auto` for the native A3020 hard-disc route.
