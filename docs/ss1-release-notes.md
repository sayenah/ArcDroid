<!-- SPDX-License-Identifier: GPL-2.0-only -->

# SS-1 compatibility and frontend settings

Run K implements D-215: guest state remains exact and host picture histories
are rebuilt after load. SS-1b run B retains SS-1's reservation for all 32 media
slots: measured host arm64 floppy-session bounds are 7,356,582–9,020,582 bytes across the
four 4 MiB profiles, including writable media. The conservative all-floppy bound
is 16,638,330 bytes, below 16 MiB and small enough for RetroArch 1.22.2's default
20,000,000-byte rewind-buffer initialization.
Native hard-disc sessions reserve a larger buffer and require a larger rewind
setting. Local tests are evidence, not approval to ship.
Run L integrates R3d-4's R-1a lifecycle guard in both gate states and every
Archimedes load. It refuses loads over a live session and ignores nested
secondary lifecycle calls, including aliased legacy Android modules. R-SS3g
replaces the proposed Android version restriction; no API-level switch is used.

Arculator save/load and single-instance run-ahead use a fixed session buffer.
RiscPC states remain unavailable. The launch chooser reports the admitted bound
but refuses save/load until guest execution begins. Load New Disc may append
slots up to the 32-disc limit without changing the fixed buffer. Save/load,
rewind and single-instance run-ahead remain available when the appended image
fits the session's media envelope. Save states and rewind history from before
"Load New Disc" do not load after it, because the ordered media set has changed
(the existing SS-1 behavior). A replacement or appended image exceeding that
envelope keeps running while state operations refuse with a timed notice. Save-
and load-side notices are deduplicated per cause, including while rewind is held. A successful
load rearms the media, pending-change and mismatching-state notices, so a later
mismatching manual load gets its notice and log reason. Repeated failures while
rewind is held give one notice until a successful load. Other refusal latches
reset with a new content session.

States require the same core build, platform/ABI, machine, firmware, ordered
media and options. Cross-version migration is unsupported. Mismatches preserve
the running session. The [state design](arculator-save-states.md) describes the
format, capacity, validation and the Run K representation.

Distinct copied secondary instances are inert before initialization and frontend
registrations, including RiscPC. Size queries never request a savestate context
that could create a secondary. Preemptive Frames is unsupported: its first save
refuses, invoking RetroArch 1.22.2's preempt error cleanup. Single-instance fake
frames stage writes. Real frames publish eagerly and finish their storage effects
before the outer frame call returns, including early returns. Speculative
provenance survives restoration of the original callbacks until a successful
load discards those effects. Failed rollback stops guest execution and publication
until reset or reload. Unload never publishes effects retained as speculative.
A save requested before speculative rollback also refuses, even after the
original input callbacks have returned. An exceptional capture with more than
65,536 encoded bytes of staged operations
refuses before changing the caller's buffer or stores. A publication failure
blocks later mutations and shows one warning.

A state cannot roll persisted discs or CMOS back. Loading or rewinding older
than a committed store frontier refuses. Raw media without state history keep
ordinary sector IO until their first-ever snapshot establishes a durable
checkpoint. Stores with existing state history keep stale-state protection on later opens: the first new native write invalidates the old checkpoint once, and a new capture seals the next checkpoint. Captured writes use sector-sized redo records.
Capture emits dirty sectors against immutable media identities. Preserve the
save directory, including CMOS/raw frontier sidecars under
arcdroid/state-stores/ and immutable state-base.adf files. Original media retain
stable identities across acknowledged writes; external edits establish a newer
frontier and identity.

CPU, memory, audio, storage and every guest-visible byte remain exact after load.
Historical frontend geometry, keyboard overlay and input state remain exact too.
Presented pixels may differ in host picture content until scanout rewrites it,
including both interlace parities and retained tails; no one-field convergence
limit is promised. The original full-buffer save/load/save byte identity remains
required; output spare capacity is zeroed (R-SS4d).

Distinct copies are identified when the frontend supplies GET_LIBRETRO_PATH.
Legitimate content switches unload first; outgoing CMOS publication is preserved.

A state taken before the session’s first frame cannot be loaded; rewinding to the very start of a session is refused with a notice.
