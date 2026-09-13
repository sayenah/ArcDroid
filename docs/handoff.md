# ArcDroid Handoff

**Stage:** ARCHITECTURE  
**Owner:** Fable  
**Next:** Starting from the merged `main`, perform the initial ArcDroid architecture phase. Reverify current upstream/libretro/Android facts from primary sources; independently evaluate viable Archimedes emulation engines and integration strategies against the approved PRD; obtain and adjudicate Astra's architecture challenge; then produce the reconciled architecture and the first implementation-ready capability-slice handoff. **Do not authorize Sol until every `AC-ARCH-*` criterion below is satisfied and material Founder gates are resolved.**

## Objective

Translate the Founder-approved ArcDroid PRD into an implementation-ready architecture for a **RetroArch/libretro core that plays Acorn Archimedes games with a game-first, select-and-play experience on Android**.

The architecture must preserve the PRD's central product distinction: ArcDroid may internally reproduce all machine, firmware, OS, media, input, and timing complexity required for compatibility, but normal users should launch game content rather than operate an emulated Archimedes computer.

## Governing product decisions

`docs/PRD.md` is Founder-approved product law. In particular, architecture must preserve these settled requirements rather than reopening them for convenience:

- ArcDroid's primary product is a **libretro/RetroArch core**.
- **Android is the primary user platform** and requires real device/runtime validation.
- Normal UX is **Select game → Play**.
- Supported loose content launches directly; no MAME-style ROM-set/software-list gate may determine whether content is attempted.
- Unknown content in a supported media format must receive a real generic launch attempt rather than database denial.
- Machine model, firmware/OS generation, disk/controller details, joystick interface, and boot flow should be selected/automated whenever reliable.
- User-supplied game-relevant firmware must be supported broadly enough to pursue the full compatibility envelope and should be recognized/selected automatically after setup.
- RetroPad is the default gaming abstraction, with keyboard and mouse/pointer support where games require them; RetroArch remapping must remain usable.
- RetroArch-native mechanisms should be used for frontend integration such as disk control, core options, saves, messages, and input where appropriate.
- The long-term compatibility target is **the game library Arculator can play**, not every non-game workstation peripheral or Arculator's UI.
- Game-relevant media fidelity, including unusual/protected media where required for compatibility, must remain architecturally possible.
- No unauthorized commercial game content, RISC OS/Arthur firmware, or other non-redistributable assets may be shipped or committed.
- The PRD intentionally **does not select the emulation engine**. That is the primary architecture decision for this stage.

## Verified repository state at handoff

- Repository: `sayenah/ArcDroid`, private; default branch `main`.
- Root model boot/configuration files exist: `CLAUDE.md` and `AGENTS.md`.
- Binding governance exists at `docs/protocol.md`.
- `docs/PRD.md` has been explicitly approved by the Founder on 2026-09-13.
- `docs/decision-log.md` and `docs/build-log.md` exist.
- No production emulator/libretro implementation exists yet.
- No emulation engine has been selected or vendored.
- No implementation work is authorized by this handoff.
- Previous brainstorming identified **Arculator** and **CLK/Clock Signal** as serious candidates and suggested that other suitable engines should not be excluded. Treat those as research leads, not accepted premises; reverify current source, capabilities, licensing, and portability yourself.

## Required architecture work

### A. Candidate engine and provenance evaluation

Evaluate Arculator, CLK, and any other materially credible candidate against the approved PRD. For each serious candidate, verify from current primary sources and source inspection where necessary:

- exact upstream repository/source and active version/revision under consideration;
- license and redistribution/source-disclosure obligations;
- machine/CPU/MEMC/VIDC/IOMD-or-relevant hardware breadth needed for Archimedes games;
- Arthur/RISC OS and machine-specific firmware support relevant to games;
- floppy/media format breadth, protected-media fidelity, CD/game-media capability, and multi-disk feasibility;
- input architecture and ability to support joystick interfaces, RetroPad mapping, keyboard, and mouse;
- timing, audio, video, frame pacing, and synchronization model;
- state ownership, reset/load/unload behavior, persistent writes, and save-state feasibility;
- dependency on desktop GUI frameworks versus separable headless/emulation layers;
- build-system and Android/NDK portability;
- code quality, maintainability, testability, and upstream synchronization cost;
- practical path to long-term Arculator-class **game** compatibility.

Do not equate "reusing Arculator" with automatic compatibility parity; verify the relevant engine behavior. Do not reject another candidate merely because it currently exposes fewer desktop features if its architecture can credibly satisfy the game-focused PRD.

### B. Libretro boundary

Define the libretro-facing architecture, including at minimum:

- core lifecycle and ownership across init/load/run/reset/unload/deinit;
- content path/data handling and direct loose-media launch;
- environment/core-option contract and default `Auto` philosophy;
- video geometry/pixel output and timing changes;
- audio production/synchronization strategy;
- RetroPad, keyboard, mouse/pointer, controller-port, and remapping behavior;
- disk-control interface for multi-disk/media workflows;
- system/save/content directory use and deterministic storage paths;
- frontend-visible error/message behavior for missing firmware and unsupported/corrupt media;
- serialization/save-state policy based on verified engine feasibility rather than wishful compatibility.

### C. Game-launch orchestration

Define how a content selection becomes a running game without making the user operate the Archimedes:

1. media recognition/inspection;
2. compatibility-profile lookup when known;
3. firmware inventory and content-fingerprint matching;
4. machine/hardware profile selection;
5. media mount/disk-set construction;
6. deterministic boot/launch automation where safe;
7. controller/input-profile selection;
8. generic fallback for unrecognized supported content;
9. manual advanced overrides for bad detection or unusual titles.

Compatibility metadata must optimize this flow, not gate permission to launch.

### D. Firmware model

Define a legally safe, RetroArch-native model that supports the game-relevant firmware breadth required by the PRD. The architecture must specify:

- firmware location/discovery;
- filename-independent identification where technically reliable;
- fingerprints/validation and handling of alternate legitimate representations;
- machine/OS compatibility mapping;
- preferred versus verified-compatible fallback behavior;
- user-facing missing-firmware diagnostics;
- how firmware inventory is exposed for troubleshooting without turning setup into a machine-emulator UI.

Do not commit or redistribute copyrighted firmware unless rights are separately established.

### E. Media and compatibility model

Determine the first milestone media subset while preserving a credible expansion path toward the full game-relevant Arculator envelope. Explicitly analyze normal floppy images, multi-disk titles, protected/metadata-rich disk formats, and CD/non-floppy game content to the extent relevant to engine selection.

Define how compatibility profiles are represented and versioned at the contract level, including deterministic title/media identification, known-safe fallbacks, input defaults, boot automation, and user overrides. Do not overdesign a database before the first slice needs it.

### F. Android/build architecture

Verify and define:

- supported initial Android ABI strategy and current RetroArch practical target;
- Android API/NDK/toolchain assumptions from current first-party documentation and live environment;
- native dependencies and whether they are acceptable for distribution;
- host-side build/test path for rapid deterministic testing;
- Android core packaging/loading approach;
- expected developer/device smoke-test loop;
- performance-risk areas and representative-device validation plan.

Do not import a desktop GUI stack merely because an upstream emulator uses one.

### G. Testing and compatibility evidence

Design an evidence strategy that does not require copyrighted assets in git or public CI:

- host/unit/integration tests for deterministic engine/core contracts;
- synthetic or redistributable media/firmware substitutes where possible;
- local private test corpus for commercial compatibility evidence without redistributing content;
- Android/RetroArch device-level smoke tests;
- repeated load/unload/resource-lifecycle tests;
- timing/audio/video regression evidence;
- malformed/unsupported-media failure tests;
- compatibility regression tracking once titles are accepted as working.

Propose, but do not silently freeze, the first representative private/local game corpus and device matrix. Escalate any material Founder choice.

## Architecture non-goals

- Do not write production implementation, tests, CI, libretro glue, Android/NDK code, or emulator modifications; Fable and Astra are non-implementing roles.
- Do not select an engine based only on earlier chat conclusions or popularity.
- Do not inherit Arculator's UX or any upstream desktop GUI as product architecture.
- Do not recreate MAME's software-list gating.
- Do not require full non-game Archimedes peripheral/workstation emulation merely for historical completeness.
- Do not promise save states until complete machine serialization has been verified feasible.
- Do not design speculative cloud services, accounts, remote compatibility dependencies, or online launch requirements; ArcDroid must work offline once installed with user content/firmware.
- Do not create new permanent design documents without Founder authorization; keep the reconciled current baton in `docs/handoff.md` and durable choices in `docs/decision-log.md`.

## Founder gates during architecture

Return to the Founder before implementation if evidence requires any of the following:

- changing a settled Section 28 PRD product decision;
- abandoning RetroArch/libretro as the primary product or Android as the primary user platform;
- materially weakening the Arculator-class game-compatibility target;
- accepting unresolved licensing/provenance risk;
- selecting a distribution model that materially changes user setup or source/redistribution obligations;
- choosing a materially irreversible dependency where credible alternatives create substantially different product/maintenance outcomes;
- defining a v1 compatibility/device/release threshold that represents a material product tradeoff rather than an implementation detail.

Routine, reversible architecture choices consistent with the PRD do not need repeated Founder confirmation.

## Astra challenge requirement

This architecture is non-trivial and high-risk. Before Sol can be authorized:

1. Give Astra the approved objective, relevant PRD/protocol constraints, verified upstream evidence, and candidate facts without presenting Fable's preferred conclusion when practical.
2. Ask Astra for material disagreements, missing constraints, licensing/provenance concerns, failure modes, unjustified complexity, and stronger alternatives.
3. Then provide Fable's candidate architecture and request comparison against Astra's independent assessment.
4. Fable must adjudicate each material Astra finding as incorporated, rejected with rationale, or escalated to Founder.
5. Unresolved material disagreement blocks implementation.

Astra remains read-only and must not edit the repository or authorize Sol.

## Architecture acceptance criteria

- **AC-ARCH-1 — Engine selection:** A specific engine/source strategy is selected from current evidence, with serious alternatives compared and the choice justified against PRD requirements rather than UI preference alone.
- **AC-ARCH-2 — Licensing/provenance:** Exact upstream provenance and license obligations for the selected engine and material dependencies are verified; distribution/source obligations and forbidden asset classes are understood.
- **AC-ARCH-3 — Libretro feasibility:** The architecture defines a headless core boundary and maps lifecycle, content loading, options, video, audio, input, disk control, storage, messaging, and teardown without depending on an upstream desktop GUI.
- **AC-ARCH-4 — Android feasibility:** The selected engine/dependencies have a verified build/runtime path for the chosen initial Android ABI/toolchain strategy, with no unresolved desktop-only blocker.
- **AC-ARCH-5 — Compatibility path:** The engine/architecture has a credible expansion path toward the game library Arculator can play, including the required machine/firmware/media breadth or a justified plan to close specific gaps.
- **AC-ARCH-6 — Firmware model:** Discovery, fingerprint/validation, machine/OS mapping, automatic selection, verified-compatible fallback, storage, and missing-firmware diagnostics are defined without unauthorized redistribution.
- **AC-ARCH-7 — Direct launch:** The architecture defines the end-to-end path from frontend-selected media to mounted/configured machine and gameplay, with deterministic boot automation for known titles where safe.
- **AC-ARCH-8 — Unknown-content fallback:** Supported but unrecognized content has a genuine generic launch path that does not depend on database membership.
- **AC-ARCH-9 — Input model:** RetroPad, keyboard, mouse/pointer, title defaults, joystick-interface translation, controller ports, and user remapping/overrides have a coherent ownership model.
- **AC-ARCH-10 — Media model:** The first milestone media subset is defined and the architecture preserves a path to multi-disk, protected/metadata-rich formats, and non-floppy game media required by long-term compatibility.
- **AC-ARCH-11 — Timing/video/audio:** The engine-to-libretro timing, video, audio, frame-pacing, and synchronization model is understood well enough to implement without speculative timing hacks.
- **AC-ARCH-12 — Persistence/lifecycle:** Save/persistent paths, writable media/state, reset, repeated load/unload, resource ownership, and save-state feasibility are explicitly resolved at architecture level.
- **AC-ARCH-13 — Test strategy:** Deterministic automated tests, legal test assets, private compatibility evidence, Android/RetroArch smoke testing, and regression tracking are defined sufficiently for the first slice.
- **AC-ARCH-14 — First slice:** Fable identifies one smallest end-to-end capability slice that proves the architecture on real libretro/Android behavior without prematurely building the entire parity layer.
- **AC-ARCH-15 — Astra reconciliation:** Astra's independent challenge is completed and every material finding is adjudicated; no unresolved material disagreement remains.
- **AC-ARCH-16 — Implementation baton:** `docs/handoff.md` is replaced with an implementation-ready work order containing `Stage: IMPLEMENTATION`, `Owner: Sol`, exact `Next`, branch/PR, scope/non-goals, architecture constraints, numbered ACs, deterministic evidence, licensing/provenance constraints, and QA focus.

## Required evidence before Sol authorization

Fable's architecture conclusion must be recoverable from repository state and cited evidence without relying on this conversation. At minimum, preserve in the handoff/decision log/PR as appropriate:

- primary-source links/revisions for selected and rejected serious upstream candidates;
- verified license/provenance conclusion;
- concrete Android/libretro feasibility evidence or source-level proof sufficient for the first slice;
- architecture mapping for the PRD acceptance criteria in Section 30;
- Astra findings and Fable adjudication of material items;
- exact first implementation slice and machine-verifiable evidence required from Sol.

## QA focus for the first implementation slice

When the baton eventually advances to Opus, initial QA should focus on the actual slice plus the architectural risk it is intended to prove, especially:

- direct loose-content launch rather than machine-first startup;
- clean libretro lifecycle and repeated load/unload;
- correct firmware discovery/absence behavior for the slice;
- controller/input delivery;
- stable audio/video/timing on the targeted environment;
- failure handling for malformed/unsupported content;
- no hidden dependency on copyrighted assets, desktop GUI state, MAME software lists, or network services;
- no scope creep that hard-codes a single title in place of general architecture.

## Current blockers

No product blocker remains: the PRD is Founder-approved. The current blockers are architectural and intentionally owned by Fable:

- upstream engine/source strategy is not yet selected;
- license/provenance implications must be verified;
- libretro and Android integration boundaries must be reconciled with the selected engine;
- first media/compatibility slice and evidence strategy must be selected;
- Astra challenge and reconciliation are required before implementation.

**Sol is not authorized to implement under this handoff.**