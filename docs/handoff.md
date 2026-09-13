# ArcDroid Handoff

**Stage:** DISCOVERY  
**Owner:** Founder  
**Next:** Continue the current Founder/ChatGPT brainstorming session. Before ArcDroid is handed to the VS Code development harness, replace this bootstrap baton with a detailed development handoff and add a Founder-approved `docs/PRD.md`. No implementation is authorized by this handoff.

## Objective

Define the product requirements and architecture-ready intent for ArcDroid: a high-quality Acorn Archimedes emulation experience on Android, with RetroArch/libretro integration strongly preferred and MAME not treated as the target emulator.

This handoff is intentionally temporary. It exists so any model opening the repository during discovery understands that product definition is still in progress and must not infer missing requirements from chat history or begin implementation.

## Verified current state

- Repository: `sayenah/ArcDroid`, private, default branch `main`.
- Governance bootstrap exists at repository root: `CLAUDE.md` and `AGENTS.md`.
- Binding project protocol exists at `docs/protocol.md`.
- `docs/decision-log.md` and `docs/build-log.md` are initialized.
- `docs/PRD.md` does not yet exist and no product requirements document is Founder-approved yet.
- No emulator engine or integration architecture has been selected. Arculator and CLK are candidates, not decisions.
- No implementation is authorized and no implementation branch/PR is active.

## Current discovery scope

Use the ongoing Founder brainstorming to settle, at minimum, the requirements that materially affect product architecture and implementation:

- target Android and RetroArch/libretro user experience;
- whether ArcDroid is a libretro core, Android application, shared engine plus multiple frontends, or another evidence-supported architecture;
- upstream emulator candidate selection criteria and acceptable fork/wrapper strategy;
- supported Archimedes machine models, firmware expectations, disk/media formats, disk swapping, and persistent state;
- controller, keyboard, mouse, touch, and frontend remapping behavior;
- video, audio, timing, frame pacing, latency, aspect-ratio, and performance expectations;
- save-state and configuration expectations where technically viable;
- Android ABI/API/toolchain, packaging, distribution, and release expectations;
- licensing, provenance, source-distribution, attribution, and firmware/ROM boundaries;
- compatibility-testing strategy and the representative game/software corpus to be tested locally without committing copyrighted content;
- success criteria for a first usable release and later compatibility expansion.

## Non-goals during this bootstrap discovery stage

- Writing production code, tests, CI, Android/native glue, or libretro implementation.
- Selecting an emulator engine solely because it appeared promising in an earlier chat.
- Treating unverified web claims or model memory as architecture facts.
- Committing commercial ROMs, disk images, RISC OS firmware, manuals, credentials, or other non-redistributable content.
- Authorizing Sol or Opus before the handoff advances to the appropriate protocol stage.

## Governing constraints

1. The Founder has final product and material scope authority.
2. Fable is the accountable Architect/Orchestrator and writes no implementation code or tests.
3. Astra is the independent read-only Co-Architect/Architecture Challenger and writes no implementation code or tests.
4. Sol is the implementation model and may act only when a future handoff says `Stage: IMPLEMENTATION`, `Owner: Sol`, with an exact `Next` action.
5. Opus is independent QA and may act only when a future handoff says `Stage: QA`, `Owner: Opus`, with an exact `Next` action.
6. Material external premises—upstream status, emulator capabilities, licenses, libretro behavior, Android/NDK behavior, and tooling—must be verified from current primary sources before they become design premises.
7. Third-party source may not be incorporated until provenance and licensing obligations are verified and accepted.
8. Copyrighted firmware and commercial game/media content must remain outside the repository and CI unless explicit redistribution rights are established.

## Discovery completion criteria

- **AC-1:** A complete initial `docs/PRD.md` reflects the product requirements settled with the Founder and receives explicit Founder approval before it is treated as product law.
- **AC-2:** This bootstrap handoff is replaced with a detailed architecture/development handoff that a fresh Fable session can execute without chat history.
- **AC-3:** The final handoff contains the exact next stage, owner, action, objective, scope, non-goals, verified relevant state, architecture constraints/questions, numbered acceptance criteria, required evidence, risks, and appropriate QA focus.
- **AC-4:** Durable decisions reached during brainstorming are recorded tersely in `docs/decision-log.md`; temporary discussion and rejected ideas are not logged unless their rejection materially constrains future work.
- **AC-5:** Any external facts that materially influence the initial architecture are reverified from current primary sources and cited where the resulting decision depends on them.
- **AC-6:** No implementation is authorized until the PRD and detailed handoff make the next architecture/implementation step explicit under `docs/protocol.md`.

## Known open decisions / blockers

- Product requirements are still being brainstormed with the Founder.
- The initial PRD is not yet written or approved.
- Emulator engine and integration architecture remain undecided.
- Target compatibility corpus, release scope, and several platform behaviors remain to be defined.

## Evidence required to close discovery

- Explicit Founder approval of the initial PRD.
- Repository verification of the finalized canonical documents.
- Primary-source citations for material external architecture premises.
- A final detailed handoff whose `Stage`, `Owner`, and `Next` accurately identify the first development-harness action.

## QA focus

No implementation exists yet, so Opus QA is not active. When development begins, QA should eventually emphasize emulator correctness, regressions, Android/libretro lifecycle behavior, input/media handling, timing/audio/video stability, crash/resource safety, licensing/provenance, and protection against accidental inclusion of non-redistributable assets.
