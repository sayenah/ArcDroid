# ArcDroid Handoff

**Stage:** ARCHITECTURE  
**Owner:** Fable  
**Next:** Starting from current `main`, perform the initial ArcDroid architecture phase against the Founder-approved PRD, including the 2026-09-13 Windows/Android platform amendment. Reverify current upstream, libretro, Windows, and Android facts from primary sources; independently evaluate viable Archimedes emulation engines and integration strategies; obtain and adjudicate Astra's architecture challenge; then produce the reconciled architecture and first implementation-ready capability-slice handoff. **Do not authorize Sol until every `AC-ARCH-*` criterion below is satisfied and material Founder gates are resolved.**

## Objective

Translate the Founder-approved ArcDroid PRD into an implementation-ready architecture for a **RetroArch/libretro core that plays Acorn Archimedes games with a game-first, select-and-play experience on both Windows and Android**.

Windows and Android are first-class shipping targets. Android additionally requires real-device/mobile lifecycle and performance validation; Windows is not merely a host-side development build.

The architecture must preserve the central product distinction: ArcDroid may internally reproduce all machine, firmware, OS, media, input, and timing complexity required for compatibility, but normal users should launch game content rather than operate an emulated Archimedes computer.

## Governing product decisions

`docs/PRD.md` is Founder-approved product law. Architecture must preserve these settled requirements:

- ArcDroid's primary product is a **libretro/RetroArch core**.
- **Windows and Android are both first-class shipping platforms.**
- Android requires representative real-device validation; Windows requires representative native RetroArch validation.
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
- The PRD intentionally **does not select the emulation engine**.

## Verified repository state at handoff

- Repository: `sayenah/ArcDroid`, private; default branch `main`.
- Root boot/configuration files: `CLAUDE.md` and `AGENTS.md`.
- Binding governance: `docs/protocol.md`.
- `docs/PRD.md` is Founder-approved product law and has been amended to make Windows and Android first-class targets.
- `docs/decision-log.md` records platform decision D-006.
- No production emulator/libretro implementation exists yet.
- No emulation engine has been selected or vendored.
- No implementation work is authorized by this handoff.
- Arculator and CLK/Clock Signal are serious research leads, not accepted premises; reverify current source, capabilities, licensing, portability, and maintainability.

## Required architecture work

### A. Candidate engine and provenance evaluation

Evaluate Arculator, CLK, and any other materially credible candidate against the PRD. Verify from current primary sources and source inspection where necessary:

- exact upstream source/revision and license;
- redistribution/source-disclosure obligations;
- game-relevant Archimedes machine, CPU, memory, controller, firmware, and media breadth;
- protected/metadata-rich disk and CD/non-floppy feasibility;
- timing, audio, video, input, lifecycle, persistent state, and save-state feasibility;
- separation from desktop GUI/framework dependencies;
- **native Windows build feasibility and native Android/NDK build feasibility**;
- code quality, maintainability, testability, and upstream synchronization cost;
- credible path toward Arculator-class game compatibility.

Do not equate reusing Arculator with automatic parity, and do not reject another candidate without evidence.

### B. Libretro boundary

Define lifecycle and ownership across init/load/run/reset/unload/deinit; direct loose-media loading; core options with `Auto` defaults; video/timing; audio; RetroPad/keyboard/mouse/pointer; disk control; system/save/content paths; messages/errors; and save-state policy.

The libretro-facing contract should be common across Windows and Android except where a verified platform constraint requires an explicit adapter.

### C. Game-launch orchestration

Define the path from selected content to gameplay:

1. media recognition/inspection;
2. known compatibility-profile lookup;
3. firmware inventory/fingerprint matching;
4. machine/hardware profile selection;
5. media mount/disk-set construction;
6. safe deterministic boot/launch automation;
7. controller/input-profile selection;
8. generic fallback for unrecognized supported content;
9. advanced user overrides for bad detection or unusual titles.

Compatibility metadata optimizes behavior; it must never become permission to launch.

### D. Firmware model

Define firmware discovery, filename-independent identification where reliable, validation/fingerprints, machine/OS mapping, preferred and verified-compatible fallbacks, missing-firmware diagnostics, and troubleshooting visibility. The same firmware model should behave predictably across Windows and Android RetroArch system directories.

Do not commit or redistribute copyrighted firmware unless rights are separately established.

### E. Media and compatibility model

Define the first milestone media subset while preserving expansion toward the full game-relevant Arculator envelope. Analyze normal floppy images, multi-disk titles, protected/metadata-rich formats, and CD/non-floppy content to the extent relevant to engine choice. Define only the compatibility-profile contract needed for the first slices; do not overdesign a database.

### F. Windows and Android build/runtime architecture

Define and verify:

- initial Windows architecture/toolchain and native RetroArch core build/loading path;
- initial Android ABI/API/NDK/toolchain strategy and RetroArch core build/loading path;
- shared portable core/emulation layer versus narrowly scoped platform adapters;
- native dependencies and their distribution/licensing implications on both platforms;
- deterministic storage/path behavior on both platforms;
- host-side test loop and native Windows smoke-test loop;
- Android physical-device smoke-test loop;
- representative Windows and Android performance/stability matrices.

Do not import an upstream desktop GUI stack merely because the upstream emulator uses one.

### G. Testing and compatibility evidence

Design evidence that does not require copyrighted assets in git/public CI:

- deterministic host/unit/integration tests for engine/core contracts;
- redistributable/synthetic media and firmware substitutes where possible;
- local private game/firmware corpus for compatibility evidence;
- native Windows RetroArch smoke tests;
- Android RetroArch physical-device smoke tests;
- repeated load/unload/resource lifecycle testing on both platforms;
- timing/audio/video regression evidence;
- malformed/unsupported-media failure tests;
- compatibility regression tracking once titles are accepted as working.

## Architecture non-goals

- Fable and Astra do not write production implementation, tests, CI, libretro glue, platform code, or emulator modifications.
- Do not select an engine from earlier chat conclusions or popularity.
- Do not inherit Arculator's UX or an upstream desktop GUI as ArcDroid product architecture.
- Do not recreate MAME software-list gating.
- Do not require full non-game Archimedes workstation/peripheral emulation merely for completeness.
- Do not promise save states until complete state serialization is verified.
- Do not add cloud/account/online launch dependencies; ArcDroid must work offline with installed firmware/content.
- Do not create new permanent design documents without Founder authorization; keep the current baton here and durable decisions in `docs/decision-log.md`.

## Founder gates during architecture

Return to the Founder before implementation if evidence requires:

- changing a settled Section 28 PRD product decision;
- dropping either Windows or Android as a first-class shipping target;
- abandoning RetroArch/libretro as the primary product;
- materially weakening Arculator-class game compatibility;
- accepting unresolved licensing/provenance risk;
- choosing a materially different distribution model or effectively irreversible dependency with meaningful product/maintenance tradeoffs;
- setting a v1 compatibility/platform/performance threshold that is a material product tradeoff rather than routine implementation detail.

## Astra challenge requirement

Before Sol authorization:

1. Give Astra the objective, PRD/protocol constraints, verified upstream evidence, and candidate facts without Fable's preferred conclusion when practical.
2. Ask for material disagreements, missing constraints, licensing/provenance issues, failure modes, unjustified complexity, and stronger alternatives.
3. Then provide Fable's candidate architecture for comparison.
4. Fable adjudicates every material finding as incorporated, rejected with rationale, or escalated to Founder.
5. Unresolved material disagreement blocks implementation.

Astra remains read-only and must not edit project files or authorize Sol.

## Architecture acceptance criteria

- **AC-ARCH-1 — Engine selection:** A specific engine/source strategy is chosen from current evidence, with serious alternatives compared against PRD requirements.
- **AC-ARCH-2 — Licensing/provenance:** Exact upstream provenance and material dependency obligations are verified.
- **AC-ARCH-3 — Libretro feasibility:** Lifecycle, content loading, options, video, audio, input, disk control, storage, messaging, teardown, and headless operation are defined without an upstream GUI dependency.
- **AC-ARCH-4 — Windows feasibility:** The selected engine/dependencies have a verified native Windows RetroArch build/runtime path for the initial Windows target.
- **AC-ARCH-5 — Android feasibility:** The selected engine/dependencies have a verified Android/NDK RetroArch build/runtime path for the initial Android ABI/API strategy.
- **AC-ARCH-6 — Shared-platform architecture:** Windows and Android use one coherent emulation/core architecture with platform-specific code limited to justified boundaries.
- **AC-ARCH-7 — Compatibility path:** The architecture has a credible path toward the game library Arculator can play.
- **AC-ARCH-8 — Firmware model:** Discovery, validation, machine/OS mapping, auto-selection, fallback, storage, and missing-firmware diagnostics are defined without unauthorized redistribution.
- **AC-ARCH-9 — Direct launch:** Frontend-selected media can become a mounted/configured machine and gameplay with safe boot automation for known titles.
- **AC-ARCH-10 — Unknown-content fallback:** Supported but unrecognized media has a genuine generic launch path.
- **AC-ARCH-11 — Input model:** RetroPad, keyboard, mouse/pointer, title defaults, joystick translation, ports, remapping, and overrides have coherent ownership.
- **AC-ARCH-12 — Media model:** The first milestone media subset is defined with a credible path to multi-disk, protected/metadata-rich, and non-floppy game media.
- **AC-ARCH-13 — Timing/video/audio:** Engine-to-libretro timing, video, audio, frame pacing, and synchronization are understood for both target platforms.
- **AC-ARCH-14 — Persistence/lifecycle:** Save/persistent paths, writable media/state, reset, repeated load/unload, resource ownership, and save-state feasibility are resolved for Windows and Android.
- **AC-ARCH-15 — Test strategy:** Automated tests, legal test assets, private compatibility evidence, native Windows validation, Android device validation, and regression tracking are defined.
- **AC-ARCH-16 — First slice:** Fable identifies the smallest end-to-end slice that proves the architecture on both Windows and Android without prematurely building the whole parity layer.
- **AC-ARCH-17 — Astra reconciliation:** Astra's independent challenge is completed and every material finding is adjudicated.
- **AC-ARCH-18 — Implementation baton:** `docs/handoff.md` is replaced by an implementation-ready work order with `Stage: IMPLEMENTATION`, `Owner: Sol`, exact `Next`, branch/PR, scope/non-goals, numbered ACs, deterministic evidence, licensing/provenance constraints, and QA focus.

## Required evidence before Sol authorization

Preserve enough evidence in the handoff, decision log, and/or PR for a fresh session to recover the conclusion without chat history:

- primary-source links/revisions for selected and rejected serious engines;
- verified license/provenance conclusion;
- concrete Windows/libretro feasibility evidence;
- concrete Android/libretro feasibility evidence;
- architecture mapping to PRD Section 30;
- Astra findings and Fable adjudication;
- exact first implementation slice and machine-verifiable evidence required from Sol.

## QA focus for the first implementation slice

When the baton advances to Opus, QA should focus on the actual slice plus the risk it proves, especially direct loose-content launch, lifecycle/repeated load-unload, firmware discovery/absence, input delivery, timing/audio/video stability, malformed content handling, no copyrighted/GUI/MAME/network dependency, and **equivalent core behavior on Windows and Android except for documented platform constraints**.

## Current blockers

No product blocker remains. Current blockers are architectural and owned by Fable:

- upstream engine/source strategy is not selected;
- license/provenance implications require verification;
- libretro boundaries must be reconciled with the selected engine;
- both Windows and Android build/runtime strategies must be proven;
- first media/compatibility slice and evidence strategy must be selected;
- Astra challenge and reconciliation are required before implementation.

**Sol is not authorized to implement under this handoff.**
