<!-- SPDX-License-Identifier: GPL-2.0-only -->

# SS-1 compatibility and frontend settings

Arculator sessions support complete save/load, rewind and single-instance
run-ahead through RetroArch 1.22.2. RiscPC save states remain unavailable.

States require the same core build, platform/ABI, machine, firmware, ordered
media and options. Cross-version migration is unsupported. Mismatches refuse
with a timed notice and preserve the running session.

The frontend buffer size is fixed for the session in every context, including
save files. The payload uses retained raster spans and changed media sectors;
unused buffer capacity is deterministic. Rewind memory must exceed the reported
state size. Exact capacities, payloads and per-profile costs are in
[SOL-REPORT-SS1.md](../SOL-REPORT-SS1.md).

A state cannot roll persisted discs or CMOS back. Loading or rewinding older
than a committed store frontier refuses. Real guest writes remain eager;
speculative writes stay in memory until proven real and disappear on restore.
Second-instance run-ahead refuses. A frontend without the savestate-context
capability receives the incomplete-serialization quirk; ordinary save/load
remains available. Speculative operation ownership is bounded at 32 MiB;
exhaustion fails safely without native speculative writes.

Preserve the save directory, including CMOS/raw intent/applied sidecars under
`arcdroid/state-stores/` and immutable raw-media `state-base.adf` files. Original
media retain a stable identity across their own acknowledged guest writes;
external edits establish a new frontier and identity.
