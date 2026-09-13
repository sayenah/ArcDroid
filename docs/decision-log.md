# ArcDroid Decision Log

Record only durable decisions that constrain future work.

Format: `YYYY-MM-DD | D-### | Decision | Why (one sentence) | Source/PR`

2026-09-13 | D-001 | ArcDroid will target a high-quality Acorn Archimedes emulation experience on Android; RetroArch/libretro integration is strongly preferred, and MAME is not the target emulator. | Establishes the product direction while leaving the exact emulator engine and integration architecture open to evidence-driven design. | Founder; `docs/protocol.md`
2026-09-13 | D-002 | Claude Fable 5 is Architect/Orchestrator, GPT-6 Astra is read-only Co-Architect/Architecture Challenger, GPT-5.6 Sol is Implementer, and Claude Opus 5 is independent QA; Fable and Astra do not write implementation code or tests. | Keeps architecture, implementation, and QA authority deliberately separated in the development harness. | Founder; `CLAUDE.md`; `AGENTS.md`; `docs/protocol.md`
2026-09-13 | D-003 | No emulator engine is selected yet; Arculator, CLK, and other technically and legally suitable candidates must be evaluated from current evidence before architecture is locked. | Avoids turning early research or chat conclusions into an unverified long-term dependency decision. | Founder; `docs/protocol.md`
2026-09-13 | D-004 | The initial `docs/PRD.md` will be produced from the current Founder brainstorming and becomes product law only after explicit Founder approval; the current bootstrap `docs/handoff.md` will then be replaced by a detailed development handoff. | Ensures the development harness starts from settled product intent and an explicit baton rather than conversational memory. | Founder instruction, 2026-09-13
