<!-- SPDX-License-Identifier: GPL-2.0-only -->

# SS-1 compatibility and frontend settings

Run H is not acceptance-complete: its 16 MiB session-bound target and default
20 MiB rewind-buffer gate remain unmet. The copied-module isolation proof also
does not cover Android API 21–22 loaders that alias the copy to the primary. The authored read-only ADF bound is
143,758,088 bytes. Do not interpret native functional tests as approval to ship.

Save states, rewind and run-ahead require Android 6.0 (API 23) or newer.
The intended API-21/22 runtime disable is blocked: RetroArch 1.22.2 cheat
routes can create a secondary even after size zero. This unapproved run-H
implementation must not be released as enforcing that requirement.

Arculator save/load and single-instance run-ahead use a fixed session buffer.
RiscPC states remain unavailable. The launch chooser reports the admitted bound
but refuses save/load until guest execution begins. A larger replacement disc
keeps running while state operations refuse with a timed notice.

States require the same core build, platform/ABI, machine, firmware, ordered
media and options. Cross-version migration is unsupported. Mismatches preserve
the running session. The [state design](arculator-save-states.md) describes the
format, capacity, validation and current performance limitation.

Distinct copied secondary instances are inert before initialization and frontend
registrations, including RiscPC. Size queries never request a savestate context
that could create a secondary. Preemptive Frames is unsupported: its first save
refuses, invoking RetroArch 1.22.2's preempt error cleanup. Single-instance fake
frames stage writes; original polling publishes final effects before the next
real frame, unload or deinit. A publication failure blocks later mutations and
shows one warning.

A state cannot roll persisted discs or CMOS back. Loading or rewinding older
than a committed store frontier refuses. Raw media without state history keep
ordinary sector IO until their first-ever snapshot establishes a durable
checkpoint. Stores with existing state history keep protection on later opens.
Capture emits dirty sectors against immutable media identities. Preserve the
save directory, including CMOS/raw frontier sidecars under
arcdroid/state-stores/ and immutable state-base.adf files. Original media retain
stable identities across acknowledged writes; external edits establish a newer
frontier and identity.

The original full-buffer save/load/save byte identity remains required; output
spare capacity is zeroed. The Founder permits this clearing under R-SS4d. The remaining bound gate
is independent of exact-payload work.
