# ArcDroid Handoff

**Stage:** DISCOVERY  
**Owner:** Founder  
**Next:** Review and continue refining the draft `docs/PRD.md` on branch `docs/initial-prd`. Continue the current Founder/ChatGPT brainstorming session as needed. No implementation is authorized. After the Founder explicitly approves the initial PRD, merge the PRD change and replace this discovery baton with the detailed architecture/development handoff for Fable.

## Objective

Finalize the product requirements and architecture-ready intent for ArcDroid: a **RetroArch/libretro core for playing Acorn Archimedes games with a game-first, select-and-play experience**, with Android as the primary target.

The current PRD draft captures the product definition reached so far but is not product law until explicit Founder approval.

## Verified current state

- Repository: `sayenah/ArcDroid`, private, default branch `main`.
- Governance bootstrap exists at repository root: `CLAUDE.md` and `AGENTS.md`.
- Binding project protocol exists at `docs/protocol.md`.
- `docs/decision-log.md` and `docs/build-log.md` are initialized on `main`.
- Draft `docs/PRD.md` exists on branch `docs/initial-prd` and awaits Founder approval.
- Product direction captured by the draft includes: libretro/RetroArch core; Android primary target; game-first direct media launch; loose-content support without a software-list gate; automatic machine/firmware selection; user-supplied game-relevant firmware; controller-first operation; and a long-term Arculator-class game-compatibility target.
- No emulation engine has been selected. Arculator, CLK, and other technically and legally suitable candidates remain architecture candidates.
- No implementation is authorized and no implementation branch is active.

## Current discovery scope

Use the remaining Founder brainstorming and PRD review to settle or intentionally leave architecture-dependent the requirements that materially affect the first development handoff, including:

- final Founder approval of the product definition and principles;
- v1 compatibility corpus and milestone scope;
- first-release media-format scope versus long-term parity scope;
- firmware setup and user-facing expectations;
- input and per-game override expectations;
- save-state expectations, subject to architecture feasibility;
- Android ABI/API/device support and measurable performance targets;
- initial distribution path;
- any product requirement that must constrain emulation-engine selection.

The engine choice, integration architecture, exact firmware fingerprinting implementation, build system, and code structure remain architecture decisions for Fable + Astra after the PRD is approved.

## Non-goals during discovery

- Writing production code, tests, CI, Android/native glue, or libretro implementation.
- Selecting an emulator engine solely because it appeared promising in an earlier chat.
- Treating the draft PRD as approved product law before Founder approval.
- Treating unverified web claims or model memory as architecture facts.
- Committing commercial ROMs, disk images, RISC OS/Arthur firmware, manuals, credentials, or other non-redistributable content.
- Authorizing Sol or Opus before the handoff advances to the appropriate protocol stage.

## Governing constraints

1. The Founder has final product and material scope authority.
2. Fable is the accountable Architect/Orchestrator and writes no implementation code or tests.
3. Astra is the independent read-only Co-Architect/Architecture Challenger and writes no implementation code or tests.
4. Sol may implement only when a future handoff says `Stage: IMPLEMENTATION`, `Owner: Sol`, with an exact `Next` action.
5. Opus may perform QA only when a future handoff says `Stage: QA`, `Owner: Opus`, with an exact `Next` action.
6. Material external premises—upstream status, emulator capabilities, licenses, libretro behavior, Android/NDK behavior, and tooling—must be verified from current primary sources before becoming design premises.
7. Third-party source may not be incorporated until provenance and licensing obligations are verified and accepted.
8. Copyrighted firmware and commercial game/media content must remain outside the repository and public CI unless redistribution rights are established.

## Discovery completion criteria

- **AC-1:** The initial `docs/PRD.md` reflects the product requirements settled with the Founder and receives explicit Founder approval before it is treated as product law.
- **AC-2:** The draft PRD is merged only after that approval.
- **AC-3:** This discovery handoff is then replaced by a detailed architecture/development handoff that a fresh Fable session can execute without chat history.
- **AC-4:** The final handoff contains exact `Stage`, `Owner`, `Next`, objective, scope, non-goals, verified state, architecture constraints/questions, numbered acceptance criteria, evidence, risks, and QA focus.
- **AC-5:** Durable decisions reached during brainstorming are recorded tersely in `docs/decision-log.md`; temporary discussion and rejected ideas are not logged unless their rejection materially constrains future work.
- **AC-6:** External facts materially influencing the initial architecture are reverified from current primary sources and cited where the decision depends on them.
- **AC-7:** No implementation is authorized until the approved PRD and detailed handoff make the next architecture/implementation step explicit under `docs/protocol.md`.

## Known open decisions / blockers

- Founder review/approval of the initial PRD is pending.
- The detailed post-brainstorm architecture handoff has not yet been written.
- Emulation engine and integration architecture remain undecided.
- v1 compatibility corpus, device/performance gates, exact media scope, and release/distribution sequence remain to be finalized or delegated to architecture with explicit Founder gates.

## Evidence required to close discovery

- Explicit Founder approval of the initial PRD.
- Repository verification of the approved PRD and finalized canonical documents.
- Primary-source citations for material external architecture premises.
- A final detailed handoff whose `Stage`, `Owner`, and `Next` accurately identify the first development-harness action.

## QA focus

No implementation exists yet, so Opus QA is not active. When development begins, QA should emphasize emulator correctness and regressions, Android/libretro lifecycle behavior, firmware selection, loose-content launch, unknown-content fallback, input/media handling, timing/audio/video stability, crash/resource safety, licensing/provenance, and protection against accidental inclusion of non-redistributable assets.
