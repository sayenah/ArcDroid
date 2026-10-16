<!-- SPDX-License-Identifier: GPL-2.0-only -->

# RiscPC ordinary-load integration plan (R3d)

Status: **adopted (D-186)**, S65. Fable's adjudication of the S64 R3d design workflow (read-only research), challenged by Astra in two stages on a fresh thread (both ACCEPT WITH CHANGES, every finding incorporated). The body is the workflow's synthesis; the sections **Amendments (incorporated)**, **Astra stage 1** and **Astra stage 2** at the end govern where they differ from it, later sections over earlier ones. Each slice still needs its own work order in `docs/handoff.md` (and, from R3d-3 on, its own Astra stage 2) before implementation.

**State since the draft was written** (the draft read `7b11b47`):
- R3c-2 run G `9a0ef41` (error-watch renewal, Wimp_Poll recovery, error-number class parameter; production zero-only) is on PR #408, CI green; QA re-check, an override-free private rerun and a run H (ordinary error numbers plus deep-review findings) are in flight.
- R3c-3a run B `bdc455f` settles the three gate-freeze conditions (D-185): the helper's CALL is a live Wimp-controller extent; status uses passive observation of a validated 64-byte ARCS block on an ordinary `OS_Byte 0` query; the UpCall guard lives in one retained 256-byte RMA block. Fable's seven-pair and platform legs follow before the bytes freeze.
- Code citations below are by function name where possible; line numbers refer to `7b11b47`.

**Governing law.**

- D-163 and D-164(6): RiscPC stays unreleased until the speed condition is met.
- D-164 defines R3d's scope.
- D-165(1)/(2) and D-166(1)/(2)/(4).
- D-168(5)/(6).
- D-170(3)/(5)/(6)/(7).
- D-172 (first use within 50 s and reopen within 1 s on Odin3 shared storage).
- D-173 (RetroArch legs with the shipping core on ordinary loads).
- D-177(7), D-180, D-182 (parallel workstreams, Odin3 used serially), D-184, D-185.
- PRD FR-10a, FR-16, FR-17, FR-22, FR-43, FR-46, FR-62.
- `docs/riscpc.md` §4.1, 4.6, 4.8, 4.11, 4.12.
- Protocol §10/§11 (no private content in CI, artifacts or logs).

---

## 1. Summary

R3d has one governance and probe step plus six implementation slices.

| ID | Slice | Owner | Can start | Merge after |
|---|---|---|---|---|
| R3d-0 | Decisions, E0 probes, Astra stages 1–2 | Fable | now | — |
| R3d-1 | Session identity, switching hygiene, LOW-4, session-ID source, build identity | Sol | after R3d-0 adjudication; stacked on R3c-2 | R3c-2 |
| R3d-2 | Firmware locator, D-166(4), R3a Lows | Sol | after R3d-0 adjudication; base `main` | — |
| R3d-3 | Production preparation host, no shipping ordinary-load caller yet | Sol | once R3d-1's interface is fixed | R3d-1; R3c-3a only for rebase order (§3), **not** the gate freeze |
| R3d-4 | Release gate and gated ordinary-load route (launch delivery unavailable) | Sol | once R3d-2/R3d-3 interfaces are fixed | R3d-1, R3d-2, R3d-3, **R3c-3a merged with the gate frozen** |
| R3d-5 | Launch wiring on ordinary loads | Sol | after R3c-3b part 2 merges and is promoted | R3d-4, R3c-3b part 2 |
| R3d-6 | Manual RiscPC choice, option meaning per session kind | Sol | after R3d-4 | R3d-4 |

**Milestones.**

- **M1, after R3d-4 and Fable's D-173 legs.** With the gate open, the shipping-configured core runs J1–J13 (§4) on ordinary loads on Windows and Android RetroArch. The guest reaches its desktop and the player starts the game by hand. With the gate closed (the default, and the only state ever uploaded or packaged), every load behaves byte-for-byte as it does today.
- **M2, after R3d-5.** Adds at-most-once automatic launch.
- **R3d closes** after R3d-5 and R3d-6 and the close checkpoint (§6.7). RiscPC then waits only on item 6b (D-163), R4 and item 5.

**Sequencing change (to record in R3d-0).** The handoff currently says "R3d design starts once R3c-3b's controller model is fixed". Only R3d-5 depends on that model. R3d-1 to R3d-4 meet R3c-3b only through the ownership split in §3, which R3d-0 freezes now. This is a decision within Fable's authority, not a Founder gate.

---

## 2. Dependency graph and parallel lanes

```
            R3c-2 (run G → QA → merge) ─┐
R3d-0 ──┬── R3d-1 ──────────────────────┴──┬── R3d-3 ──┐
        │                                  │           │
        │   R3c-3a (run B → freeze → merge) ───────────┼──┐
        └── R3d-2 ─────────────────────────────────────┴──┴── R3d-4 ──┬── R3d-6
                                                                      │
            R3c-3b part 2 (after run B) ──────────────────────────────┴── R3d-5
            6b (P0i / Phase 0 / 1a): independent; shares only the Odin3 and the Mac A/B slot
```

- **Lane A (now).** R3d-0, R3d-1 and R3d-2 run alongside R3c-2 run G, R3c-3a run B, the R3c-3b part 2 design and 6b.
  - R3d-1 touches the identity-predicate sites, the dispatcher, the persistence guards, callbacks, and the RPC refusal op.
  - R3d-2 touches only `core/rpc_selection.*`, `libretro/rpc_inventory.*` and tests.
- **Lane B.** R3d-3 runs alongside R3c-3b part 2's implementation. They meet only at the reset interface (§3).
- **Lane C.** R3d-4 needs nothing from part 2, because launch delivery is "unavailable" on M1.
- **Lane D.** R3d-5 and R3d-6 run in parallel.
- **Odin3 and Mac.** All R3d Odin3 legs (E0, R3d-2 cost, R3d-3 pre-qualification, R3d-4 D-173, R3d-5) are booked serially against 6b profiling and the R-H runs (D-182). Fable's Mac library A/B runs are serial too.

---

## 3. Ownership split with R3c-3b part 2 and R3c-3a (frozen in R3d-0, given to every Sol)

| Area | Owner | Obligation on the other slice |
|---|---|---|
| Whether the frontend routes **every** RPC reset (pending or RUNNING) to `arc_riscpc_preparation_reset` | **R3d-3** | Part 2 does not edit the `retro_reset` dispatch. |
| What preparation reset does inside (no-op helper replacement before any resumed instruction, resume barrier, attempt interrupt) | **R3c-3b part 2** | R3d never changes the body of the RUNNING reset in `rpc_preparation.c`. |
| Cancellation policy at automatic insertion, the disk-control callbacks, keyboard and polled input (`arc_disc_*` insert path, the disk-control setters, the keyboard callback, `arc_poll_inputs`) | **R3c-3b part 2** | R3d-1 changes only the identity predicate on nearby lines, never the cancellation policy. |
| Plan, attempt and action fields on `ArcRpcPreparationRequest`, the "no plan" (delivery unavailable) value, controller, transport, record publication | **R3c-3b part 2** | R3d-4 builds requests with "no plan". R3d-5 fills them in. |
| Process-unique session-ID source | **R3d-1** (provider) | Part 2 uses it through a stated interface and injects IDs in its tests until R3d-1 merges. |
| `arc_load_game` routing, the `retro_run` pending branch, `arc_release_session`, `retro_unload_game`, session kind, AV policy, notice host | **R3d** | Part 2's frontend tests drive these through the R3d-3 host once it merges, or through the plan core before then. |
| Owner of the per-session `ArcRpcPreparation` object | **R3d-3** (frontend) | Part 2 places the attempt object under that owner. |
| Helper generation, gate bytes, transport replacing the authored UpCalls | **R3c-3a run B** | R3d-3 and R3d-4 use the frozen interface and never edit START's helper logic. |

**Rebase rule.** Whichever slice merges second rebases. R3c-3a run A already changes `rpc_preparation.c/.h` and `tests/arc_test_plan.c`, and run B may change more. R3d-3's promotion therefore normally rebases onto R3c-3a. The freeze itself is a merge condition only for R3d-4, because only the ordinary route populates stores. A conflict outside this table is a handoff ambiguity that goes back to Fable. Sol never settles it alone.

**Three different epochs.** Every interface names these separately:

- the **RTC epoch**, `ArcRpcPreparationRequest.epoch`, captured once at load (D-170(3));
- the **attempt invalidation epoch**, from the R3c-3b attempt module;
- the **observer epoch**, from R3c-1/R3c-2.

---

## 4. Player journeys: the observable contract

The slice ACs cite these journeys. They assume a gate-open build unless stated otherwise. `<system>` and `<save>` are RetroArch's System/BIOS and Saves directories.

| ID | Journey | What must be observable |
|---|---|---|
| J1 | First use, with firmware and both boot archives present | Black, silent frames with no freeze. A readable status that moves through hard-disc check, boot files, first-use settings, hard-disc preparation and "Starting RiscPC". Then the guest, with no OSD left behind. On the Odin3, 50 s or less (D-172). |
| J2 | Reopen | "Starting RiscPC", then the guest, within 1 s on Odin3 shared storage. **Works with both boot archives removed, with zero package I/O (counted).** |
| J3 | Selection failure (missing or unusable firmware, unqualified pair, unsupported requirement, conflicting discs, unreadable folder) | Class A refusal before any guest runs. The notice names the reason, the needed family or families, `<system>/arcdroid/firmware/` and the manual Archimedes alternative. Never a substitute OS. No `hd-riscpc`/`cmos-riscpc` entry is created. |
| J4 | Boot archive(s) missing on first use | Class A refusal naming exactly `HardDisc4.zip` and/or `PlingSystem.zip` and `<system>/arcdroid/riscpc/boot/`. **No `hd-riscpc/<id>/` is left behind.** After the file is added, the next load is a normal J1. |
| J5 | First use interrupted (close content, Android force-stop, Windows process kill), at validation, generation and population | The next load recovers under "Checking the RiscPC hard disc" and completes as J1 or J2. The strict checker passes after first use and the guest-tolerant checker after play. |
| J6 | Archimedes → RiscPC → Archimedes → RiscPC in one RetroArch process | Each session looks, sounds and responds as it would after a fresh start. No status, hint, held input, geometry or fault carries over in either direction. One process, proved by PID or a log field, never by the script's relaunch fallback. |
| J7 | Disc control and prompts on RiscPC | Disc Control lists exactly the set's discs and never the helper. A swap reaches drive 0. Promoted R3c-2 cells behave as qualified. A serious error box shows no answer hint. |
| J8 | RetroArch Restart during preparation and during play | No crash, no double start, no store damage. The status stays visible during preparation. |
| J9 | Option changes on a RiscPC title | An explicit non-Auto Archimedes Machine, CPU or OS keeps the title on Arculator with a truthful notice. A media option changed mid-session takes effect at the next Restart or load, and a notice says so. Archimedes-only options have no effect, or their effect is explained. |
| J10 | Gate-closed build | Exactly today's behaviour, including today's notice text. |
| J11 | One set loaded loose, zipped, as m3u or as a folder set | The stores follow the identity rule recorded in R3d-0 (§5 item 2). Any difference from D-029/D-054 is stated to the player per FR-10a. |
| J12 | Failure found during stepped preparation (damaged store, invalid package, generation failure) | Class B: frames keep presenting, a retained terminal message is shown, and it clears on close or switch. |
| J13 | Pause, menu, fast-forward or backgrounding during first use | Preparation follows `retro_run`. No failure, no store damage, no audio pops. J1 completes. |

**Failure classes.**

- **Class A:** refused at load. `retro_load_game` returns false after a notice, the same convention as Arculator. It applies to everything that can be decided at load: selection reasons, unreadable firmware, missing archives when no store exists, and a non-native save directory.
- **Class B:** a held terminal status. It applies only to failures found during stepped preparation, as D-168(2) already requires.

---

## 5. R3d-0: governance, decisions and probes (Fable, no code)

### 5.1 Decisions to record (D-186 candidate and following)

1. **Release gate.** The decision is in §6.0.
2. **Content identity for RiscPC stores, CMOS and boot records.** Keep D-029/D-054 unchanged:
   - a loose image and the same image zipped share one identity, which satisfies FR-10a and FR-43;
   - an m3u playlist uses its own domain hash;
   - a folder set uses the selected disc.

   Record in `docs/riscpc-hd-store.md` that a playlist and a loose disc 1 create separate 126 MiB stores. Player-ux's proposal, a set-level "first disc in canonical order" identity for RPC only, goes to Astra (§8). The default stays D-029/D-054 because it adds no new identity domain and keeps RPC and Arculator consistent.
3. **Max geometry.** Proposed: unify the advertised maximum at 4096×2048 for both session kinds, so it never depends on history. E0 P2 informs this and Astra challenges it.
4. **AV contract.** For an RPC session, `retro_get_system_av_info` at load announces the pending cadence (640×480 at 60 Hz). The only `SET_SYSTEM_AV_INFO` allowed is one at the first guest field, and only if the values differ.
5. **Ownership split (§3) and the sequencing change (§1).**
6. **The notice catalogue and class A/B rule (§7), frozen with exact texts.**
7. **Oracles and pass rules (§9), frozen before any result is accepted.**
8. **Handoff cleanup.**
   - Strike the stale "plan cannot seed CMOS with persist" carry. It closed in R3b-2a (`tests/rpc_cmos_seed_plan_test.py`).
   - Move to item 5: the RetroArch GET_STATUS crash without `.info`, the Android backup stall, the `.info` firmware list, systemname, and the RPCEmu credit in `authors`.
   - The import-guard Low stays unscheduled.
   - Before any work order cites them, Fable confirms the wording of R3b-2b QA S-4 and the R3a re-check Lows from `.work/s62/qa-r3b2b/QA-REPORT.md` and the R3a QA report.

### 5.2 E0 probes

E0 uses only existing binaries: the plan core with its own `.info` and library name, and the shipping and test cores. The Founder's everyday core is never touched.

| Probe | What it settles | Method |
|---|---|---|
| **P1 Notice visibility** | Whether a STATUS/OSD `SET_MESSAGE_EXT` renders with widgets on and off, whether the `SET_MESSAGE` fallback shows, whether terminal messages persist, how long texts wrap on the Odin3 screen, and whether RetroArch logs any of it. | Plan-core first use. Odin3: RetroArch 1.22.2, default driver, timed screencaps. Windows: Founder's box (D-148), GPU screenshot or desktop capture, whichever the scheduled-task driver supports. Check the RetroArch 1.22.2 source as a primary source. Check whether `video_gpu_screenshot` is the real key and the script's `screenshot_gpu` is not. |
| **P2 AV churn** | The size of the presentation gap at the 60 Hz → guest-rate `SET_SYSTEM_AV_INFO`, and the effect of a 2048 vs 4096 max-geometry mismatch. | Odin3 `screenrecord` or SurfaceFlinger latency. Windows frame-time capture. |
| **P3 Library lifetime** | Whether RetroArch unloads the core between two `LOAD_CONTENT`s (Windows `FreeLibrary`, Android `dlclose`) and between deinit and init. | Init and deinit log counters in the test core, plus the RetroArch source. |
| **P4 In-process switch baseline** | Reproduce the stale identity and geometry on a real frontend: Arculator → `.arcplan` RPC → an Arculator title that enters the chooser, via `LOAD_CONTENT` on UDP 55355, with the relaunch fallback refused. | This becomes R3d-1's regression baseline. |
| **P5 Census of flagged titles** | Every library title that raises today's RiscPC notice: whether it reaches gameplay on Arculator, and whether any needs floppy drive 1. | Existing library A/B kit and logs. Evidence for F-2 and for the drive-1 invariant. |
| **P6 Directories** | The Odin3 RetroArch system and save paths (native, no SAF), the Founder's play paths to avoid, and an isolated `CONFIGFILE` layout under `/storage/emulated/0/arcdroid-r3d/`. | Device inspection. |
| **P7 Steering the pairs** | Whether authored flag-bearing JFD carriers (no game content) plus per-leg firmware folders make Auto select each of the seven pairs. The D-173 legs need this because manual RPC options arrive only in R3d-6. | Pure selection API on the Mac. |
| **P8 S-4 cost** | The cost of a live CMOS publication triggered by a guest CMOS write, on two pairs, with RetroArch flush, against the D-152/D-160/D-161 live budgets. | Odin3 runner. The result decides whether R3d-3 steps the publication or records acceptance. |
| **P9 Repository and artifact visibility** | Whether the GitHub repository and its CI artifacts are public. If they are, G-1 below is a hard requirement. | `gh` read-only. |

**E0 rules.**

- Findings and ledger rows go in `.work/s64/r3d-e0/`.
- No private bytes or screenshots leave `.work/`.
- An Odin3 RetroArch instance this session did not start blocks the probe until the Founder closes it.

### 5.3 Astra

- Stage 1 challenges this plan.
- Stage 2 challenges the reconciled plan plus the R3d-1 and R3d-2 work orders.
- Later work orders each get their own stage 2.
- Every material finding is adjudicated.

---

## 6. Slices

### 6.0 Release gate (D-163): defined once, used from R3d-3 to R3d-6

**Mechanism.** A single compile-time route gate, closed by default. It is implemented in R3d-4 and named by Sol.

The gate controls exactly one decision: whether an ordinary load takes the RiscPC route. That covers both a SELECTED result and a class A refusal for an RPC-flagged load. Everything else is compiled in both states: the host, notices, pending frames, maintenance and the locator. The two binaries differ only at the routing decision and in the gate-open-only notice and option texts.

**What "the shipping core" means for D-173 (Fable's ruling, recorded with the decision).** The gate-open build of the shipping configuration:

- hidden visibility and the shipping export list;
- no `ARCDROID_TEST_*` definitions and no test seams;
- the shipping filename and `.info`;
- an unchanged `library_name`.

When D-163 is satisfied, the release changes the default and nothing else, so the D-173 evidence carries over. Test-DLL legs are always labelled test-core evidence and never count as D-173 evidence.

**Rejected alternatives.**

- **A hidden or experimental core option.** Players can reach it through the options menu and per-game `.opt` files, and `SET_CORE_OPTIONS_DISPLAY` hides options without enforcing anything.
- **A file-presence or environment switch.** It can be discovered, and a guard cannot check it as a build fact.
- **"Main is simply unreleased".** Any main build, including side-loads, would route to RPC.

**Required properties.**

- **G-1.** The default configuration, every package recipe and every uploaded CI artifact are gate-closed. A CI guard fails if any uploaded or packaged shipping binary is gate-open. This is a hard requirement because `retroarch-package` is uploaded (`ci.yml:223-226`) and P9 checks whether it is public.
- **G-2.** Gate-closed, every load is byte-identical to the pre-R3d base: route, results, notices (today's "flagged for RiscPC-class machines … booting anyway" text included), option lists and counts, `.info` and persisted bytes. The only allowed delta is R3d-1's build-identity log line.
  - **Resolved conflict:** the gate-closed notice keeps today's text verbatim. In a gate-closed build the claim is true, keeping it avoids a D-165(1) delta, and byte identity stays a clean oracle. Rewording is an Astra question.
- **G-3.** One `retro_init` log line gives the gate state and build identity (version and commit, deterministic, no `__DATE__`). It is the Odin3 install proof, because the installed `.so` cannot be hashed.
- **G-4.** CI builds the gate-open shipping configuration on host, MinGW and Android. It runs the export guard, the shipping-I/O guard and the flag-free harness against it, and never uploads it.
- **G-5.** Test cores may enable the route for authored legs. That evidence is labelled test-core evidence.

**Gate-open selection failures.** Class A refusal with the catalogue notice (§7), in D-165(2)'s reason order.

- No Arculator fallback (riscpc.md §4.11, FR-16, FR-17).
- The notice offers the manual Archimedes override, which item 12 requires.
- An explicit non-Auto Archimedes Machine, CPU or OS option keeps the title on Arculator with notice N-9. LEGACY is unchanged in both gate states.
- This creates a real tension with item 12's "existing working content never regresses". The E0 P5 census and the R3d-4 gate-open A/B decide whether it becomes Founder decision F-2 (§11).

---

### 6.1 R3d-1: session identity and switching hygiene

**Objective.** Tie backend identity to the current session, not to the backend that started last. That makes switching deterministic and persistence-safe. R3d-1 also gives RPC a working refusal path (LOW-4) and provides the session-ID source and the build-identity line. It adds no RPC route and makes no Arculator behaviour change.

**Base.** Stacked on R3c-2 and rebased after R3c-2 merges.

**Scope.**

1. **Session kind owned by the frontend:** empty, Arculator or RiscPC. It is set when a load commits to a route and cleared in `arc_release_session`. All 16 identity sites listed in the basis convert to session kind or to `ArcEngineCapabilities`. The persistence check in `rpc_preparation.c:478` converts if it is a policy branch.
2. **Neutral dispatcher after stop.** After `arc_engine_stop`, the dispatcher reports no RPC identity, fault or fault text until the next start, whether by restoring the baseline operations or by an equivalent query. This must keep the property in the engine comment at `arcdroid_engine.c:36-38`: observer snapshots and failed-start cleanup behave as at the baseline.
3. **Persistence guard.** Arculator CMOS persistence (`arc_persist_cmos`, `arc_sync_cmos`), boot-seed persistence and the Arculator HD, boot-record and outcome writes do nothing unless the session kind is Arculator. This holds on every caller (`retro_unload_game`, `retro_deinit`, `arc_load_game`). RPC persistence goes only through preparation persist and free.
4. **AV by session kind.** The R3d-0 max geometry applies. Base geometry and the fps baseline are set again at every load.
5. **Callbacks.** The log and ADF-refusal callbacks are bound on every backend, or rebound on each switch, and cleared on all backends at deinit. Correctness must not depend on which backend was current at `retro_init`.
6. **LOW-4.** The RPC backend's refusal-callback op is implemented. Raw refusals (write-protected, deleted data, format mismatch) and raw commit or flush failures that latch write protect produce the same refusal record and player notice as `arc_adf_log_refusal`.
7. **Write observer.** The `arc_io` write observer is installed for RPC sessions, so guest writes invalidate the disc-name and identity caches. Its cost on hard-disc journal writes is measured. If the cost argues against installing it, the reason is recorded.
8. **Session-ID source.** It returns nonzero values that never repeat within a process across load, unload, reset and init/deinit, and across a library reload if P3 shows reloads happen. It can be injected in tests without test flags.
9. **No reliance on static initialisers for switch-relevant state.** Function-local statics keyed on `arc_engine_session_generation()` (always 0 on Arculator) are keyed to the session instead. Retained notices are cleared on unload, deinit and switch.
10. **Build-identity log line (G-3)** in all builds. The plan-core and pre-qualification legs use it from this slice on.

**Non-goals.**

- No RPC route, gate, notice text, option or `.info` change.
- No cancellation-policy change (§3).
- No `engine/rpcemu/` change.

**Acceptance criteria.**

- **AC-1 (source guard).** A source guard admits only the listed session-kind and capability queries in `libretro/`. Each of the 16 converted sites has a mutant: reverting any one of them to the last-started-backend query fails a test.
- **AC-2 (J6, D-170(7)).** Take an RPC session that is running, has failed with a retained terminal status, is mid-generation, has faulted, or was unloaded mid-preparation. A following Arculator load (immediate construction, **chooser state**, or a failed load) shows:
  - Arculator max geometry and AV;
  - no RPC status, fallback text or fault;
  - Arculator media acceptance on disk control;
  - Arculator disc and input arbitration;
  - no held keys, pad levels, OSK state, error-hint gates or disc-notice entries.

  The reverse direction is tested too.
- **AC-3.** No Arculator CMOS, seed, HD, boot-record or outcome byte is written during or after an RPC session. A save-tree byte comparison covers every unload path, including deinit without unload. A planted Arculator CMOS path across a switch stays unchanged.
- **AC-4.** Callback binding is correct whatever the order of init, deinit and switches, including repeated init/deinit without unloading the library.
- **AC-5 (LOW-4).** Every raw refusal class and every raw store-failure latch on RPC produces exactly one refusal record and one notice. Arculator refusal behaviour is byte-identical.
- **AC-6.** A guest rename or format of a set disc on RPC changes what automatic disc replacement offers. The observer's cost per journal write is recorded.
- **AC-7.** Session IDs are never zero and never repeat across 10,000 load, unload, reset and init cycles, or across the library-reload case from P3.
- **AC-8 (FR-62).** Over 200 Arculator↔RPC cycles (plan-core RPC sessions), heap census, file handles and the RPC allocation counter return to baseline.
- **AC-9.** Host, sanitizer, MinGW and Android builds pass, along with all guards and `rpc-session-switch`. Existing assertions whose meaning changes (`rpc_orchestration_test`'s `!arc_engine_is_rpc()`, `rpc_cmos_plan_test` cross) are updated deliberately, with written reasons.

**Deterministic tests.**

- A switching harness drives the plan core and the flag-free build only through `retro_*`. It covers Arculator→RPC→Arculator, RPC→Arculator→RPC, chooser after RPC, failed RPC then Arculator, and repeated cycles. Each is compared against a fresh-process Arculator trace covering AV info, geometry, callbacks, notices, input, disk control, options and CMOS paths.
- `rpc_cmos_plan_test` cross gains chooser, failed, faulted and mid-generation variants.
- An init/deinit cycle test.

**Private legs.**

- **Mac:** the library A/B in both package states against the R3c-2 merge base. Only known variance (105, 133, 205) may differ.
- **Odin3 and Windows:** repeat E0 P4 with the R3d-1 plan core. The stale-geometry reproduction must be gone, with a single process proven.

**QA focus.**

- Completeness of the site census (grep plus mutants).
- That stop-time neutrality does not change failed-start cleanup or the observer snapshots relied on at `arcdroid_engine.c:36-38`.
- Persistence guards on every Arculator write path.
- Callback arming after an RPC-current `retro_init`.
- Session-ID uniqueness under reload.

---

### 6.2 R3d-2: firmware locator, D-166(4) and R3a Lows

#### R3d-2 implementation contract (before code)

The pinned comparison base is `be44b0afd1bbf23e006a52ab6ca07a8c61704e66`
(Fable's dispatch supersedes the older AC-5 hash in the handoff).
The shipping scan and locator use only `arc_io`; a narrow metadata primitive
preserves VFS validity, directory type and size rather than collapsing them
through `arc_io_file_size`. No callback or route outside this slice changes.

Metadata outcomes: invalid root metadata (including a missing path) is
**unreadable-inventory**, because VFS validity flags cannot establish absence
versus inaccessibility. A valid non-directory root is an established empty
inventory (**missing-firmware**). Missing required callbacks, invalid candidate
metadata/size, failed directory open, invalid entry name and failed directory
close mark VFS uncertainty. Without a usable candidate this is unreadable;
with one it preserves D-165's selection and `inventory=incomplete`. Boolean
readdir EOF cannot report silent truncation; no detection is claimed. Only a
stat-confirmed regular file of a catalogue size is opened. An unrelated
unreadable file with valid, non-catalogue size therefore stays missing-firmware.

All declared limits invalidate the entire inventory: 1024 directory entries
(including dot entries), 64 MiB of attempted candidate bytes, eight nested
directory levels, 4095 path bytes excluding NUL, and 4 MiB per image. A limit
hit clears every recognition and locator, and produces **incomplete-scan**
where selection requires the inventory, regardless of enumeration or failure
order. LEGACY, conflicting discs, unsupported requirements and unqualified
pairs retain precedence because they are established without inventory.
VFS/resource failures alone do not trigger that refusal. The diagnostic stays
generic and deterministic; eventual player text must name these scan limits
and advise removing unrelated files or reducing nesting/path length.

Each recognised OS family owns a fixed-size path and a copied fingerprint
(size, CRC-32, SHA-1), with no borrowed catalogue strings and no owned firmware
buffer. Successful copies choose the bytewise-smallest full path (`strcmp`),
independent of directory order, spelling conventions or file extension.
Only a successfully closed file supplies recognition. A locator read allocates
at most 4 MiB, re-stats, checks opened length, reads the exact image, checks
length again, verifies all three fingerprint fields and requires successful
close. On failure it frees its temporary buffer and returns null/zero outputs;
it never searches another copy or lower-ranked pair. On success the caller
owns and frees the returned buffer. Replacement with byte-identical content
is interchangeable; identity means the fingerprint, not an inode. Concurrent
mutation after verification is outside this synchronous snapshot contract.

Enumerated AC-5 diagnostic deltas (no other ordinary-load changes):
1. A scan that did not run says `inventory=skipped`, formerly `complete`.
2. A valid unrelated non-catalogue-size file is not opened by this scanner;
   its open failure no longer changes missing-firmware to unreadable-inventory.
3. A valid non-directory root becomes empty/missing-firmware. Invalid root
   metadata remains uncertainty/unreadable; absence cannot be proved by VFS.
   Missing stat and candidate-stat/size failures now report uncertainty;
   explicitly reported directory failures retain their former semantics.
4. Any declared limit now says `reason=incomplete-scan model=none os=none
   inventory=incomplete`, discarding prefix recognitions. The independently
   established reasons listed above retain precedence.

The comparison projection retains every other log and notice byte, load
result, media compatibility/identity, and persisted file byte. The authored
ordinary-load harness counts firmware directory opens, file opens and metadata
calls before the diagnostic:
non-RPC loads must remain zero. `diagnostic-only` remains until R3d-4.
The public scanner wrapper substitutes a 4 KiB byte budget (with a compile-time
assertion pinning production's 64 MiB) to exercise the same exhaustion logic
without repeatedly hashing hundreds of MiB under sanitizers and mutation.
The former allocation-failure-file-leak mutant is equivalent after this change:
image allocation precedes opening, so that failure owns no handle to leak;
read/close cleanup and zero outstanding image allocations are tested directly.

**Objective.** Production can obtain the selected firmware's bytes by fingerprint, with bounded, order-independent I/O. The inventory reports what is missing truthfully. There is no player-visible change.

**Base.** `main`. Independent of everything in flight.

**Scope.**

1. **Locator.** The inventory keeps a bounded locator per recognised family. No firmware buffer survives the scan. A bounded read returns the selected image after re-checking size, CRC-32 and SHA-1, and fails cleanly if the file changed after the scan. Byte-identical duplicates are interchangeable, with a deterministic tie-break that does not depend on filename (D-164(4)).
2. **D-166(4).**
   - **(a) Stat before open.** Only files whose size matches the catalogue are opened. An unrelated unreadable file never turns missing-firmware into unreadable-inventory.
   - **(b) Missing folder.** A missing firmware folder counts as empty and gives missing-firmware.
   - **(c) Budget exhaustion.** When the entry or byte budget runs out, the result is the same for any enumeration order of the same tree, and the inventory is marked incomplete.
3. **Skipped state.** The inventory gets an explicit "skipped" state, and the diagnostic line says `skipped` when no scan ran. Dropping "diagnostic-only" waits for R3d-4.
4. **R3a Lows.**
   - A `resource_failure` policy mutant or assertion.
   - The equivalent OS-range mutant is either killed with an authored out-of-range catalogue or removed with a recorded rationale.
   - The Python `assert`s in `tests/rpc_selection_mutants.py` become explicit checks.

**Non-goals.** No route change. No change to the Arculator firmware walk or its diagnostics. No options.

**Constraints and interfaces.**

- `libretro/rpc_inventory.*` and `core/rpc_selection.*` only, plus tests.
- I/O goes only through `arc_io`, under the shipping-I/O guard.
- The four committed fingerprint tuples are unchanged (D-090).
- D-166(1) holds: no scan unless the notice predicate holds.
- No firmware path or bytes in logs at INFO or above.

**Acceptance criteria.**

- **AC-1.** The locator returns exact bytes for each authored fingerprint and refuses a file that was changed, truncated or replaced. No buffer remains after the scan.
- **AC-2.** The D-166(4) matrix:
  - unrelated unreadable file gives missing-firmware;
  - missing folder gives missing-firmware;
  - an unreadable catalogue-size candidate gives unreadable-inventory, or selection with an incompleteness flag if another candidate exists;
  - permuted directories under budget exhaustion give identical results.
- **AC-3.** Duplicates, renames, nesting and corruption keep D-165(3)'s scanner behaviour.
- **AC-4.** All mutants are killed or recorded as equivalent with a reason, and the script behaves the same under `python -O`.
- **AC-5.** Ordinary-load output is byte-identical apart from the `skipped`/`complete` wording, and non-RPC loads do no extra I/O (I/O-count harness).

**Tests.** Mock-VFS unit tests for metadata failure, permutation and budget exhaustion. The mutation script runs in CTest.

**Private legs.**

- **Mac:** locator reads on the four real dumps, renamed and nested; a package-present A/B.
- **Odin3:** scan plus locator-read cost on FUSE shared storage (the Mac warm baseline is 75.8 ms). R3d-4 needs this to budget load time.

**QA focus.**

- Order independence under exhaustion.
- That no path opens files that are not candidates.
- Stat semantics on Android FUSE.
- Change detection between scan and read.

---

### 6.3 R3d-3: production preparation host, with no ordinary-load caller yet

**Objective.** Move every frontend piece RPC preparation needs out of `ARCDROID_TEST_PLANS`/`ARCDROID_TEST_FAULTS` and `tests/arc_test_plan.c` into one production frontend module that owns the per-session `ArcRpcPreparation`. The plan core becomes a thin client that keeps its low-level route (D-168(6), D-170(6)).

The module also adds checkable channels for notice delivery and presentation, applies the R3d-0 AV contract, and fixes the notice defects. Ordinary loads still never reach it, which keeps this a promotion with a strong oracle.

**Base.** `main` after R3c-2 and R3d-1. It does **not** depend on the R3c-3a freeze, because no ordinary load populates a store until R3d-4. It rebases onto R3c-3a per §3, since both edit `rpc_preparation.*` and `tests/arc_test_plan.c`.

**Scope.**

1. **Host module** in `libretro/`. Callbacks: clock, notice, log, attach, started, diagnostics and attached.
   - **Attach** replaces `arc_rpc_plan_media`. It does not prepare media again, keeps the backing until engine stop, and attaches drive 0 natively, never through ADFFS.
   - R3c-2 boundary ownership (D-177(7)) is preserved.
   - The plan core's low-level entry, authored ROM, seed, factory and import modes, catalogue override and scripted input stay test-only.
2. **Pending frames.** While preparation is pending:
   - one black frame and the stated number of silent samples per `retro_run`;
   - input isolated;
   - the cadence announced at load per the AV contract;
   - pending checked before the active-RPC route.
3. **Live maintenance.** Exactly one `arc_riscpc_preparation_maintenance` per running frame, and none while pending or generating (D-170(1)). S-4 is resolved per E0 P8: either a stepped live publication or a recorded acceptance with numbers.
4. **Reset.** Every RPC reset, pending or RUNNING, routes to `arc_riscpc_preparation_reset` (§3). The frontend clears what the Arculator branch clears: input, disc state, disc-cache invalidation and the notice ring. Launch-outcome cancellation belongs to part 2.
5. **Option freeze.** The media-option freeze (today `arc_rpc_keep_media_options`) becomes production behaviour. It covers disk writes, JFD tab, short tail and JFD omitted until Restart or reload, with the N-11 notice. The reset re-staging policy is defined.
6. **Teardown.** Preparation free (persist the latest snapshot, then detach) runs before engine stop and before any media is freed, on every path, including a failed attachment.
7. **Notice host.** STATUS ext, or the type P1 chooses, with fallback refresh, terminal lifetime, and clearing on reset, unload and switch. Three fixes:
   - **N-8, the revision-1 advisory.** It is overwritten in the same frame because RECOVER emits it and `refresh(p,false)` after the unit re-emits the new status. It gets a minimum display time (frozen in R3d-0) and is logged.
   - **Wording for a receipted store that regenerates.** When a receipted store generates CMOS for a new pair key, the player sees the first-use settings text, not "Starting RiscPC" (`status()`'s receipted short-circuit).
   - **Startup status hold.** The startup status is held after RUNNING for a bounded emulated time frozen in the design note. This covers the 3.50 ARM610 blank interval without any heuristic on frame content. It is a deliberate change: `refresh()` returns early at RUNNING and START clears the status, so the hold lives in the host and leaves the module's terminal semantics alone. R3d-5 replaces it with the transport's desktop-handoff signal. The root cause goes to R4.
8. **Verifiable channels** that ship permanently and carry no private data:
   - one log line per status transition and per terminal text;
   - one summary line at RUNNING, FAILED or cancel (§9).
9. **RPC load negotiation** (used by the plan route now and by R3d-4 later):
   - pixel format;
   - controller info and input descriptors with RPC labels and no A3010 joystick (riscpc.md §4.8);
   - keyboard callback;
   - OSK and input-sampling reset;
   - AV baseline, frame counters and fault flags.
10. **Per-frame parity on RPC.** The OSK activation state machine and its interruption notice, the input-overflow notice, and pad, keyboard and pointer behaviour match Arculator. Any intentional difference gets a test.
11. **Guard updates, each justified in the PR:**
    - `rpc-frontend-boundary-guard`;
    - `backend-symbols.json` policy;
    - the `check-shipping-io` source list;
    - `rpc-shipping-route`, which still asserts that the shipping core has no test entry and that ordinary loads do not route.

**Non-goals.**

- No ordinary route and no gate.
- No plan or attempt fields.
- No change to the RUNNING reset internals.
- No new options.
- No changes to the preparation budgets or the D-168 order.

**Acceptance criteria.**

- **AC-1.** The flag-free harness drives the host through real `retro_*` entry points with an authored-fingerprint request. It covers J1, J2 and J12, plus reset, unload and deinit in every state (VERIFY through RUNNING, generation, FAILED), and then an Arculator load (D-168(6)).
- **AC-2.** While pending, every `retro_run` produces exactly one video and one audio callback. There is no `SET_SYSTEM_AV_INFO` before the first guest field, and at most one at that field.
- **AC-3.** The notice sequence, the log lines and the summary line match the expected texts in every state:
  - a terminal error takes precedence and stops refresh;
  - the revision-1 advisory is visible for its minimum time and logged;
  - regeneration on a receipted store shows the first-use text;
  - reset, unload and switch clear the notice.
- **AC-4.** Plan-core goldens, runner outputs and the CI R2c AC-9 leg are identical except for the listed intended deltas (new log lines, AV announcements), compared against pinned output from the merge base. The plan core holds no host-adapter code beyond its low-level modes.
- **AC-5.** Teardown order is proven under sanitizer: no media is freed before detach, preparation is freed before engine stop, and nothing is left after a failed attachment.
- **AC-6.** One maintenance call per running frame. S-4 is resolved with evidence.
- **AC-7.** OSK, overflow and input parity hold, or each documented difference has a test.
- **AC-8.** A shipping-core ordinary load of RPC-flagged content behaves byte-for-byte as at the R3d-1 base.
- **AC-9.** All builds pass, the guards pass, and the library A/B passes in both package states.

**Private legs: pre-qualification, which takes D-173 risk out before the route exists.**

- **Odin3.** Plan core in RetroArch with an isolated `CONFIGFILE` (P6). All seven pairs using the S62 kit, with stores populated after the freeze where R3c-3a has merged and disposable otherwise. Covers J1 and J2 with screencaps showing the notices, presentation within §9 according to the summary line, and D-172's targets.
- **Windows.** At least 3.71 SA-110, 3.50 ARM610 and 3.60 ARM7500.
- **Mac.** Runner and harness on all seven pairs, with the summary-line numbers compared against R3b-2b runner evidence.

**QA focus.**

- Semantic drift in the promotion: diff the old glue against the new module.
- Release order on every path.
- Completeness of reset routing, and that the RUNNING internals are untouched.
- AV churn.
- Test seams absent from shipping symbols.
- No private content in the new log lines.

---

### 6.4 R3d-4: release gate and gated ordinary-load route (M1)

**Objective.** With the gate open, an ordinary load of RPC-flagged content that R3a selects starts RiscPC preparation in the shipping configuration and delivers J1–J13 (launch delivery unavailable; manual input). With the gate closed, J10 holds. This slice carries the D-173 legs.

**Base and merge conditions.**

- R3d-1, R3d-2 and R3d-3 merged.
- **R3c-3a merged with the gate bytes frozen** (D-185).
- Not R3c-3b part 2.
- F-2 resolved if it was triggered (§11).

**Scope.**

1. **The §6.0 gate** with G-1 to G-5. CI builds both states.
2. **Load order.** The RiscPC decision happens after media preparation and option parsing, and before every Arculator-only step:
   - the HD-media machine check;
   - `arc_options_resolve` against an Arculator machine;
   - the legacy firmware walk and its nearest-size and suspicious-size diagnostics;
   - the Arculator launch plan, CMOS path and seed;
   - the Arculator JFD refresh, ADFFS, A3020 HD reconciliation, the chooser and construction.

   Routing:
   - LEGACY or gate-closed: today's steps, in today's order, with today's output;
   - SELECTED: the RPC route;
   - any other reason: class A.

   An explicit non-Auto Archimedes Machine, CPU or OS keeps the title on Arculator with N-9. RPC-routed loads never run the legacy walk, so they never hash a file twice, never show "Missing RISC OS 3.11" and never show the 2 MiB size message.
3. **Resolved request.**
   - Selection SELECTED.
   - Firmware bytes and catalogue fingerprint from the R3d-2 locator.
   - `key.model = selection.model − 1`.
   - Layout by OS (3.50→1, 3.60→2, 3.7x→3).
   - `defaults` false.
   - Content key from `arc_content_digest` under the R3d-0 identity rule.
   - Save and system paths.
   - The RTC epoch captured once and injectable in tests.
   - Drive 0 from the prepared entry (native JFD, never ADFFS).
   - `hard_disc` and `populate` true.
   - No helper override, so revision-2 stores get R3c-3a's generated no-op helper.
   - "No plan".
4. **Notices (§7).**
   - The false notice is absent on RPC-routed loads.
   - Class A notices N-1 to N-6.
   - The boot-archive check runs before the store directory is created (J4).
   - With the gate open, a one-time N-12 says automatic launch is not yet available. R3d-5 removes it.
   - The selection log line drops "diagnostic-only" when the gate is open.
5. **Options on RPC (minimal, for M1).**
   - Hard Disc is ignored and IDE is always attached. This closes the R2a absent-IDE 60 s item.
   - ADFFS, JFD boot and the A3010 joystick do not apply.
   - Launch, Chooser and RAM disc are ignored while launch delivery is unavailable.
   - One log line lists the ignored options.
   - Disk writes, short tail and omitted sectors keep their meaning under the R3d-3 freeze.
   - Gate-closed option lists are byte-identical.
6. **Drive-1 invariant.** On ordinary loads, drive 1 only ever holds the generated helper. Disk control and automation address drive 0 only, and the helper is never a candidate (re-run R3c-3a AC-4). This closes R2b LOW-3 and the drive-1 `.jfd` item by invariant plus a test. Either reopens if any path exposes drive 1, and P5 records whether any title needs it.
7. **CI reachability with authored inputs.** A test-build-only mechanism lets RetroArch reach the ordinary route with an authored catalogue, firmware and boot packages, because RetroArch cannot call `arc_libretro_test_rpc_catalogue`. The mechanism is fixed in the design note after Astra, which also weighs link-time substitution of the catalogue translation unit for the flag-free harness (§8). `rpc-shipping-route` and the export guard prove it is absent from both shipping states.
8. **Pre-freeze stores.** Before the post-freeze D-173 legs, Fable deletes every private store populated before the freeze from all leg directories and records the deletion in the ledger. The receipt carries no hash of the gate bytes. Whether it should is raised with R3c-3a's owner; R3d does not change it.
9. **Docs.** Update the statements that "shipping routes reject RiscPC" to say "gate-closed builds": `riscpc-hd-store.md` (:747, :901, :989), `riscpc-session-preparation.md:3`, `riscpc-launch-gate.md:4`, `riscpc-launch-plan.md:4`. Record the content-identity note.

**Non-goals.**

- Automatic launch.
- Manual RPC options and per-kind visibility.
- `.info` changes (item 5; unchanged in both states).
- Opening the gate.
- Speed (6b).
- Storage-management UI.

**Acceptance criteria.**

- **AC-1 (J10, G-1, G-2).** Gate closed:
  - the library A/B in both package states and the D-165(3) scenario set are byte-identical to the R3d-3 base apart from the G-3 line;
  - the shipping DLL in Windows CI RetroArch boots an authored RPC-flagged JFD on Arculator with today's text;
  - the G-1 guard passes.
- **AC-2 (gate-open Archimedes).** Archimedes content is identical to gate-closed.
- **AC-3 (J1, J2 through `retro_load_game`, flag-free harness and test core).**
  - Each SELECTED pair reaches RUNNING through `arc_riscpc_preparation_begin`.
  - No Arculator CMOS, launch, ADFFS or chooser state is created.
  - The legacy walk does not run.
  - A firmware folder holding only RiscPC ROMs succeeds.
  - Reopen with both archives removed does zero package I/O.
- **AC-4 (J3, J4).** Each failure reason gives a class A refusal with its exact notice and no Arculator start. This is tested across:
  - loose, zip and playlist sets;
  - conflicting later discs;
  - mixed raw and JFD;
  - permuted and duplicate inventories;
  - a missing folder;
  - budget exhaustion;
  - an unrelated unreadable file;
  - malformed firmware.

  A missing archive names the file and leaves no store directory. A file swapped between scan and read fails VERIFY with a notice.
- **AC-5 (J6).** Through real entry points in one process: Arculator→RPC→Arculator→RPC, including a chooser-state Arculator session, a refused RPC load followed by Arculator, and unload during each preparation state. Each matches a fresh-process trace. Session IDs are fresh, and FR-62 holds over 200 cycles.
- **AC-6 (J7, J9).**
  - The drive-1 invariant and helper exclusion hold on every disk-control path.
  - A manual swap reaches drive 0.
  - Promoted R3c-2 cells behave as qualified.
  - N-9 and N-11 appear when they should.
  - Content keys are equal for a single image and its zip; playlists and folder sets follow the documented rule.
- **AC-7 (CI test core, authored, labelled test-core evidence).** A Windows RetroArch leg: an ordinary-load first use, then a fresh-process reopen, then `LOAD_CONTENT` in one process to Arculator with the relaunch fallback refused. It asserts:
  - status-transition log lines;
  - black pending frames, then guest frames after START;
  - summary-line bounds;
  - Arculator screenshots equal to the baseline after the switch.

  A `windows_load_harness` RPC mode asserts the exact `SET_MESSAGE_EXT` sequence, one video and one audio callback per pending `retro_run`, the reset and unload matrix, and D-170(7).
- **AC-8 (gate-open shipping DLL with no ROMs, CI).** RetroArch shows the J3 missing-firmware and J4 missing-archive outcomes.
- **AC-9 (G-4).** The gate-open shipping configuration builds on host, MinGW and Android. Export, shipping-I/O and symbol guards and the flag-free harness are green, and the test seam is absent in both states.
- **AC-10 (D-173, Fable private).** The legs below pass against §9's frozen pass rules.
- **AC-11 (cost).**
  - S-4 is confirmed under live play on the Odin3 under RetroArch.
  - The ordinary route's hot workload stays within D-180's 3 % of the plan route, in paired alternating ABBA runs.
  - Load time includes R3d-2's measured scan and locator cost.

**Private legs (the D-173 evidence).**

- **Odin3** (RetroArch 1.22.2, Android 15).
  - Setup:
    - the gate-open shipping arm64 core installed through Install or Restore a Core, with the repo `.info` pushed;
    - the isolated `CONFIGFILE` and directories from P6;
    - the build proven by the G-3 line.
  - All seven pairs, steered by P7 carriers and folders:
    - J1 (50 s or less) and J2 (1 s or less) with timed screencaps showing the notices and the summary line;
    - J3 and J4;
    - J5 at three points on at least two pairs;
    - J6 in one process;
    - J7, J8 and J13;
    - a revision-1 store;
    - two real RiscPC titles for realism.
  - Afterwards the gate-closed main core is restored per F-1 and recorded in the ledger. These legs are serial with 6b and R-H.
- **Windows** (Founder's box, D-148, RetroArch 1.22.2, gate-open MinGW DLL, private files staged only there).
  - J1 and J2 on all seven pairs, unless Astra and R3d-0 accept a stated subset. The minimum is 3.71 SA-110, 3.71 ARM710 and 3.50 ARM610.
  - J4, J5 (kill and QUIT), J6 and J8.
  - Visibility by the method P1 verified.
  - Times are recorded; a Windows target is proposed in the build log (D-172 is Odin3-only).
  - Nothing but numbers leaves the box.
- **Mac.** The library A/B in both package states × both gate states.
  - Gate closed: must equal the base.
  - Gate open: each RPC-flagged title is classified as routed, refused (with reason) or unchanged, and each is adjudicated. Any title that refuses but reaches gameplay on Arculator today triggers F-2.
- **HD-store platform obligations** (`riscpc-hd-store.md:802-803`, `:905-911`): re-run on the shipping path on both platforms.

**Licensing.**

- CI uses authored ROMs and the authored boot packages already in the tree.
- Authored carriers contain no game content.
- No private bytes, screenshots or logs leave the ledger.
- No gate-open binary appears in any artifact.

**QA focus.**

- Gate soundness: can any runtime input reach RPC when the gate is closed?
- Gate-closed byte identity.
- Class A before execution for every reason.
- No Arculator-only refusal before routing on RPC loads, and none skipped on Arculator loads.
- The drive-1 invariant.
- That each CI test proves what its label claims.
- No private content in logs.

---

### 6.5 R3d-5: launch wiring on ordinary loads (M2)

**Objective.** On gate-open ordinary loads, deliver the chosen content at most once per explicit attempt through R3c-3a's gate and R3c-3b's controller. Every unavailable or ambiguous case gets a notice. R3d-5 removes N-12 and replaces R3d-3's bounded status hold with the desktop-handoff signal.

**Base.** R3d-4 and R3c-3b part 2 merged with its promotion run. If part 2 leaves the outcome and boot-watch cells disabled, delivery-only operation is stated explicitly, and anything that depends on boot records degrades cleanly.

**Scope.**

1. **Plan inputs.** Drive-0 structural discovery at load, before preparation, builds plan v2 (BOOT, APPLICATION with an index or an automatic unambiguous choice, PRODUCT), the media digest, and the attempt, using part 2's interface and R3d-1's session ID.
2. **Explicit attempts (proposed, for Astra).** Each `retro_load_game` and each chooser confirmation is one explicit attempt. Restart, fault and unload interrupt the attempt and never relaunch it. The player gets N-10 and manual input. Swaps follow part 2's policy.
3. **Option mapping.**
   - Launch Off, Auto, App1–8 and Ask, the Launch Chooser and the RAM disc map to plan v2's explicit index and optional RAMFS.
   - An ambiguous discovery with Ask, or with no remembered answer, shows a RiscPC chooser before preparation. That uses drive-0 discovery only.
   - PRODUCT is offered only when a verified product exists from an earlier session, because product presence is known only after RECOVER.
4. **Boot records.** The on-disk location (separate from `<save>/arcdroid/launch/`), the canonical configuration digest, and what an ordinary load does with a record (skip relaunch, choose an action, or diagnostics only) are fixed in the design note. Records are never required. Discarding them on an identity refusal is integration-tested.
5. **Manual-only notices** for a revision-1 store, a rejected plan, unanswered ambiguity, and delivery that is unavailable or unknown.

**Non-goals.**

- Changes to part 2's state machines, transport or profiles.
- Changes to Arculator launch behaviour or cancellation.

**Acceptance criteria.**

- **AC-1.** Exactly one token per explicit attempt. Reset, swap rollback, fault and unload at each of part 2's points never cause a second automated execution, tested through the shipping frontend route. A reopen launches once.
- **AC-2.** The chooser appears before any preparation status. A remembered choice skips the chooser next time. Off and App-N mappings hold. Arculator launch is byte-identical (A/B).
- **AC-3.** No record comes from an unestablished attempt, and discard on identity refusal works. With the cells disabled by override, behaviour equals R3d-4 plus a dispatch with an unknown outcome, and no record is created.
- **AC-4.** Builds, guards and gate-closed identity are re-checked.

**Private legs.**

- **Odin3 and Windows, gate-open shipping core, all seven pairs on the Odin3:**
  - returning commands, desktop applications, non-returning titles and prompts;
  - Restart mid-attempt;
  - the 084 install from an ordinary load, through automatic exchange and product verification, then a later load offering the product.
- **Odin3 R-H:** within 3 % with no launch phase active (D-180), and launch-phase cost within part 2's ceilings, in paired alternating runs.
- **Mac:** the library A/B in both package states.

**QA focus.**

- At-most-once through RetroArch's own Restart and close.
- Chooser timing relative to preparation.
- No relaunch after an interruption.
- The three epochs kept distinct.

---

### 6.6 R3d-6: manual RiscPC choice and option meaning per session kind

**Objective.** Meet riscpc.md §4.12 ("a manual RiscPC option is offered"), D-165(1) and D-166(2), and make every option's meaning on RPC explicit without breaking per-game option files.

**Base.** R3d-4. Runs in parallel with R3d-5.

**Scope.**

1. **Manual RiscPC model and OS options**, present only in gate-open builds. They are per-axis overrides and never bypass recognition or qualification. A manual choice of a pair whose firmware is not installed gives J3.
2. **Meaning or visibility for each existing option per session kind** (Machine, CPU, Memory, OS, Joystick, Hard Disc, ADFFS, JFD Boot, Disk Writes, Short tail, Omitted sectors, Disc Requests). Visibility uses `SET_CORE_OPTIONS_DISPLAY` or the update-display callback if P1 and Astra show RetroArch handles it well.
3. **`.opt` compatibility.** Existing keys and values are stable (FR-22, D-031). Unknown or stale RiscPC values give a notice, not a hard load failure; today `arc_read_options` refuses unknown values. The static option-count asserts are updated deliberately.
4. **Notice when a manual choice changes the pair of a title that already has a store.** The hard disc is kept, a new CMOS record is created, and boot records are invalidated (D-164(2)).

**Acceptance criteria.**

- **AC-1.** Gate-closed option tables and counts are byte-identical.
- **AC-2.** The override matrix follows D-166(2). Recognition and qualification are never bypassed.
- **AC-3.** A per-game `.opt` written by either gate state loads in the other without failure.
- **AC-4.** The meaning and visibility matrix is tested in a switching sequence.
- **AC-5.** Arculator A/B is unchanged.

**Private legs.** One manual-override leg each on the Odin3 and Windows, visible in the RetroArch menu, plus a gate-closed Mac A/B.

**QA focus.**

- `.opt` compatibility in both directions.
- That the pinned enum values serialised into records are unchanged.
- Override precedence.

---

### 6.7 R3d close (Fable)

- The library A/B in both package states × both gate states on the final head.
- Seven-pair J1–J13 on the Odin3 and Windows.
- The S-4 verdict.
- FR-62 cycles.
- A docs sweep.
- R3 is declared complete. RiscPC stays gate-closed until 6b meets D-163 and the Founder confirms.

---

## 7. Notice catalogue (texts frozen in R3d-0)

With the gate closed, every text is byte-identical to the base.

| ID | When | Class | Proposed text (gate open) |
|---|---|---|---|
| N-1 | Missing firmware | A | "This title needs a RiscPC with RISC OS {families}. Add that firmware to <system>/arcdroid/firmware/, or choose an Archimedes in core options to try it anyway." |
| N-2 | Unqualified pair | A | "This title needs {CPU} with RISC OS {families}, which ArcDroid does not support yet. You can choose an Archimedes in core options to try it anyway." |
| N-3 | Unsupported requirement | A | "This title asks for hardware ArcDroid does not emulate ({summary})." |
| N-4 | Conflicting discs | A | "The discs in this set ask for different machines. Load them separately, or choose a machine in core options." |
| N-5 | Firmware folder unreadable | A | "ArcDroid cannot read <system>/arcdroid/firmware/. Check that it is an ordinary folder and readable." |
| N-6 | Boot archive(s) missing | A | "First use of a RiscPC title needs {HardDisc4.zip and/or PlingSystem.zip} in <system>/arcdroid/riscpc/boot/. Nothing was changed." |
| N-7 | Progress | — | Today's five statuses. The first-use text is also shown for regeneration on a receipted store. Optionally "(first time only)" on the two first-use statuses. |
| N-8 | Revision-1 store | advisory | Today's text, with a minimum display time and a log line. |
| N-9 | Explicit Archimedes option on an RPC-flagged title | advisory | "This title is flagged for RiscPC-class machines; starting on {machine} because it is selected in core options." |
| N-10 | Launch interrupted (R3d-5) | advisory | "Launch was interrupted; the RiscPC is under your control. Restart the content to launch again." |
| N-11 | Media option changed mid-session | advisory | "{option} takes effect after Restart." |
| N-12 | M1, launch not yet available (R3d-4; removed by R3d-5) | advisory | "Automatic launch is not available yet; the RiscPC desktop is ready for you." |
| — | Stepped failure | B | The existing terminal texts, retained while frames present, cleared on close or switch. |

---

## 8. Open questions for Astra

1. Is a compile-time route gate with one constant a faithful reading of D-173's "shipping core"? What guard best proves G-1 for uploaded and packaged artifacts?
2. Gate-closed notice: keep today's text verbatim (this plan) or reword it truthfully ("this build does not offer RiscPC machines") as an accepted D-165(1) delta?
3. Gate-open selection failure: class A refusal plus the manual override (this plan, riscpc.md §4.11/§4.12, FR-16) against Arculator fallback for titles that work today. Is a per-title outcome from the P5 census the right input?
4. Unify max geometry at 4096×2048, or keep values per kind with a `SET_SYSTEM_AV_INFO` at the session boundary? Does P2's measured gap change the answer?
5. Content identity: D-029/D-054 as they are, with the m3u and loose split documented (this plan), or a set-level "first disc in canonical order" identity for RPC only, before any public RPC store exists?
6. CI reachability: link-time substitution of the catalogue translation unit (flag-free, closer to shipping) against a `TEST_FAULTS`-only seam, such as an authored catalogue compiled in or read from the system directory in test builds only.
7. Session-ID uniqueness under library reload: is a process-static counter enough, or should the source be seeded (for example from a monotonic clock), given P3?
8. Dispatcher neutrality at stop: restore the baseline operations pointer, or add an explicit identity query and leave the pointer alone? Which one better preserves the `arcdroid_engine.c:36-38` invariants?
9. Should the `arc_io` write observer run on RPC, given its cost on hard-disc journal writes?
10. The explicit-attempt definition, and the chooser running before preparation on drive-0 discovery while PRODUCT waits for a post-RECOVER store.
11. A bounded post-RUNNING status hold in the host (R3d-3) against waiting for the R3d-5 handoff signal. Is the hold change to `refresh()` semantics acceptable?
12. Windows coverage: all seven pairs, or the stated three-pair minimum?
13. Do the numbers in the §9 pass rules hold up against P2 and the R3b-2b runner evidence?
14. Should the store receipt carry a gate-bytes digest? This would be raised with R3c-3a, not done in R3d.

---

## 9. Oracles and pass rules (frozen in R3d-0 before any result is accepted)

- **Notice delivered.** The core logs each status transition and terminal text once. This is the CI-checkable channel; today progress text is not logged.
- **Notice visible.** Timed screencaps on the Odin3, and GPU screenshot or desktop capture on Windows, show each status readable at default RetroArch settings. If P1 finds that STATUS does not render without widgets, the host uses a type that does, recorded as a decision.
- **Frames presenting.** The summary line at RUNNING, FAILED or cancel records:
  - frames;
  - elapsed host time;
  - maximum core frame time;
  - maximum and p99 host interval between `retro_run` calls;
  - the count of intervals over 2 T;
  - the D-162 and D-169 exception counts;
  - AV changes issued;
  - the final state.

  Proposed pass rule: no host interval over 2 T outside D-169's accepted isolated spikes and D-162's install-checkpoint frames, which are counted and reported; p99 at most 1.25 T; exactly one AV change, at guest start. The Odin3 screenrecord cadence corroborates it.
- **Single process.** A PID or log field is unchanged across the switch sequence.
- **Build identity.** The G-3 line.
- **Odin3 budgets.** D-172: first use 50 s or less, reopen 1 s or less, offline preparation at most 0.80 T per frame, generation at most 4 T per frame.

---

## 10. Carried items: disposition

| Item | Source | Disposition |
|---|---|---|
| False "not emulated; booting anyway" notice | R2c | Gate closed: verbatim (G-2). Gate open: absent on routed loads, N-1 to N-6 and N-9 elsewhere (R3d-4). |
| Plan cannot seed CMOS with persist | R2c | Already closed in R3b-2a; struck in R3d-0. |
| Drive 1 refuses `.jfd`; LOW-3 shared staging | R2c, R2b | Closed by the R3d-4 drive-1 invariant plus a test. Reopens if drive 1 is ever exposed. |
| About 18 s blank screen on 3.50 ARM610 | R2c | R3d-3 bounded status hold; R3d-5 handoff signal; root cause to R4. |
| First-use generation notice | R2a, R2c, D-173 | R3d-3 (channel and wording fix); proven visible in R3d-4's D-173 legs. |
| GET_STATUS without `.info`; Android backup stall | R2c | Item 5 (R3d-0). |
| Import guard not tied to file content | R2c | Unscheduled Low; no slice touches `engine/rpcemu/`. |
| LOW-4: raw store failures silent | R2b | R3d-1 AC-5. |
| R3a Lows (range mutant, `resource_failure`, Python asserts, `skipped` wording) | R3a | R3d-2. |
| D-166(4) (stat before open, missing folder, order dependence) | R3a | R3d-2 AC-2. |
| S-4: synchronous live CMOS persistence | R3b-2b | E0 P8, then R3d-3 AC-6, confirmed in R3d-4 AC-11. |
| Absent-IDE 60 s delay | R2a | Closed by IDE always attached on RPC (R3d-4). |
| Revision-1 advisory overwritten in the same frame | New (verified in the design workflow) | R3d-3 scope 7. |
| Stale identity, CMOS zero-write, callbacks, write observer | New (area maps) | R3d-1. |
| 2 MiB 3.50 suspicious-size message | New | Never shown on routed loads (R3d-4). The Archimedes-content case stays as it is under D-165(1) unless Astra argues otherwise. |
| Q-3 test comment | Earlier slice | The next slice that touches `tests/rpc_cpu_test.c`. |

---

## 11. Founder decisions

None is needed now. The gate implements D-163 without relaxing it, and D-173's interpretation is Fable's call. Two conditional items go to the handoff's **Waiting on the Founder** list only if their trigger fires, and each parks only the workstream named:

- **F-1: everyday Odin3 core.** By default, Fable installs gate-open builds only for legs, in isolated directories, and restores the gate-closed main core afterwards, recording it in the ledger. The question is raised only if the Founder objects or prefers a second RetroArch install. It parks nothing.
- **F-2: refusal against "never regresses".** It triggers only if E0 P5 or the R3d-4 gate-open A/B finds an RPC-flagged title that reaches gameplay on Arculator today but would refuse with the gate open (riscpc.md §4.11/§4.12 against item 12's "existing working content never regresses"). The question is whether N-1's manual Archimedes route is enough, or whether such titles keep a fallback. It parks only R3d-4's merge, not its implementation.

Any proposal to let players opt in early (an experimental option) would relax D-163 and would be a Founder gate. This plan proposes none.

---

## 12. Workflow, licensing and evidence rules (every slice)

- **Workflow.** Fable work order → Astra stage 2 → handoff `Stage: IMPLEMENTATION`, `Owner: Sol` with an exact `Next` → Fable reviews the diff and evidence → Opus QA in a fresh clone (`arcdroid-opus-qa`, effort 15 quoted verbatim) → adjudication → §12.11 merge.
- **Licensing and provenance.**
  - No ROM, boot-archive or title bytes, private screenshots or private logs in commits, CI or artifacts (protocol §10/§11, D-134, D-164(4)).
  - Only the four committed fingerprint tuples (D-090).
  - Authored carriers and packages only in CI.
  - No `engine/rpcemu/` change is planned; any change there must be a marked patch with `UPSTREAM.md` and `manifest.json` and must pass the import guard.
  - `THIRD_PARTY_NOTICES.md` already covers RPCEmu; the `.info` credit is an item 5 task.
- **Evidence matrix.**

| Slice | CI (deterministic) | Mac (Fable) | Odin3 (Fable) | Windows (Fable) |
|---|---|---|---|---|
| R3d-0 | — | P5, P7 | P1, P2, P3, P4, P6, P8 | P1, P2, P3, P4 |
| R3d-1 | Switching matrix, per-site mutants, persistence guard, LOW-4, session ID, FR-62 | A/B both package states | P4 re-run (single process) | P4 re-run |
| R3d-2 | Inventory matrix, mutants, I/O counts | Real-dump locator; A/B | Scan and locator cost on FUSE | — |
| R3d-3 | Harness lifecycle, frame and AV counts, notices, goldens, guards | A/B; seven-pair runner | Seven-pair plan-core pre-qualification | Three-pair pre-qualification |
| R3d-4 | Gate-closed identity, G-1 guard, gate-open harness, test-core RetroArch leg, harness RPC mode, gate-open builds | A/B both package states × both gate states | **D-173**: seven pairs, J1–J13, D-172, S-4, R-H | **D-173**: J1/J2 pairs, J4–J6, J8 |
| R3d-5 | Attempt matrix through the frontend | A/B | Seven-pair launch, 084, R-H | Launch legs |
| R3d-6 | Option tables, `.opt` round trips | Gate-closed A/B | Manual-override leg | Manual-override leg |

---

## Amendments (incorporated)

Each item comes from the completeness critic and was checked against the cited code by the critic; Fable adopts all of them. They amend the sections named.

1. **§9 pass rules by phase.** Offline preparation: core frame time ≤ 0.80 T plus D-169 spikes. Generation: ≤ 4 T per frame (at least 15 fps during the first-use settings status). Live session: the D-152/D-160/D-161 budgets. The summary line reports max, p99 and over-threshold counts per phase. Astra Q13 also asks whether 15 fps during generation is "continuous presentation" under D-173.
2. **AV changes.** §5.1(4) and §9 count only frontend-induced AV changes: one at the pending → guest transition, none between pending phases. Guest-driven refresh changes (`arc_rpc_present_run`, riscpc.md §4.7) are allowed and listed; P2 measures each one's gap. R3d-3 AC-2 is read this way.
3. **Notice severities.** R3d-3 scope 7 changes the host notice contract to three module-set severities: progress status, timed advisory, terminal. Never inferred from text or test-plan state. N-8 follows from the advisory class instead of a special minimum time. Running-phase store advisories get their own AC-3 case.
4. **Unload persistence failure.** Always logged; the store or CMOS layer records it and the next load of that title shows a timed advisory. An AC injects the failure and asserts both (D-170(5)).
5. **Gate state of test cores.** G-5 uses either a separate gate-open test target or a runtime route override that exists only in `ARCDROID_TEST_FAULTS` builds and defaults to closed. Existing test-core legs keep the closed state. G-1 covers every uploaded core whose ordinary route can open; no gate-open core under the shipping filename enters `retroarch-package`. The plan core stays reachable only through `.arcplan`, under its own name and `.info`.
6. **RTC epoch source.** Host wall clock at load, local time like Arculator, captured once, through an injectable clock without test flags. AC (R3d-4): guest clock within 1 s of host time at load, stable across reset, never re-read; determinism tests inject the clock.
7. **Kill during play.** New J14: guest CMOS change plus HD guest writes, then Android force-stop or Windows kill mid-play; the next load recovers the latest published state. If S-4 is stepped, R3d-3 AC-6 bounds the loss window (N frames, frozen in R3d-0) and tests a kill inside it. D-173 Odin3 legs include J14 on two pairs.
8. **Runtime fault during play.** New J15: a contained RPC fault gives a retained class B terminal status, frames and silence keep presenting, CMOS and HD are persisted or their failure is reported, unload and the next Arculator load are clean. Tested with the harness fault seam and a test-core RetroArch leg.
9. **Save continuity across an engine change.** R3d-0 records, R3d-4 tests: staged and overlay media are engine-agnostic shared state; Arculator CMOS, launch and boot-record files are neither read, migrated nor deleted by RPC sessions; N-9's Arculator route still finds them. N-6 drops "Nothing was changed" unless the archive check moves before media staging.
10. **Auto pair drift when firmware is added.** R3d-0 chooses between (a) Auto pinned to the pair recorded beside the store, with a notice offering the new pair, and (b) accepted drift with a one-time advisory. R3d-4 AC: add a higher-ranked firmware after first use.
11. **Concurrent writers.** R3d-0 chooses between an ownership/lock marker with stale-lock recovery and a tested detection that refuses the second opener (class A). R3d-4 AC with two processes. P3 also checks whether run-ahead's second instance loads content on a core with `serialize_size=0`.
12. **Selection options mid-session.** Machine, CPU, OS and manual RiscPC options take effect only on reload ("…after reloading the content"); media options after Restart; Restart never switches engines. J9 and R3d-6 AC-4 test both groups.
13. **Manual options and D-166(1).** R3d-6 scope 1 amends D-166(1): the scan also runs when any manual RiscPC axis is set. AC covers raw ADF/ADL and legacy-header JFDs with manual choices; the I/O-count harness proves no scan without one.
14. **Single-process proof.** E0 P4 first checks the RetroArch 1.22.2 source and a CI log for `LOAD_CONTENT`, then names the Odin3 mechanism (menu Load Content by pad `sendevent`, or a network command) and its oracle (`pidof` unchanged plus the G-3 init counter). Windows uses the menu route through the scheduled-task driver if no network route exists.
15. **Failed-load notice visibility.** New probe P1b on the Odin3 and Windows. If RetroArch drops the message on a failed load, R3d-0 records a fallback (for example a minimal black, silent session with a terminal status), and the load-failure convention becomes a decision.
16. **Gate-open A/B sizing.** The R3d-4 Mac A/B fixes the system directory per gate state (real firmware plus both archives), a per-title budget covering first use plus reopen, a fresh save tree per run with stores deleted afterwards, classification cold and receipted, and F-2 triggers only from the firmware-present run.
17. **Gate-closed baseline chain.** G-2 lists the cumulative allowed gate-closed deltas: R3d-1 the G-3 line, R3d-2 the `skipped`/`complete` wording, nothing else. Every slice compares against the pre-R3d pinned base minus that list.
18. **Windows timing.** Declared informational in R3d-0: D-173 on Windows passes on notice visibility and cadence; times are recorded. (D-172 stays Odin3-only.)
19. **Disk control while pending.** Eject, insert and index changes in each pending state: refused with no side effects (no change to part 2's cancellation policy). Image append/replace on RPC and Arculator: media check, content identity unchanged, never an engine switch (riscpc.md §4.12). J7 and R3d-3 AC-1 cover it.
20. **Archimedes loads in gate-open builds.** R3d-2's Odin3 leg also measures Archimedes load time with the four RiscPC ROMs present. In gate-open builds only, R3d-4 excludes files the RiscPC catalogue recognises from the suspicious-size diagnostic; gate-closed stays byte-identical (G-2). Astra is asked whether D-165(1) permits that.
21. **Founder's play directory.** D-173 legs use an isolated `CONFIGFILE` with `system_directory` and `savefile_directory` under `/storage/emulated/0/arcdroid-r3d/`. Nothing is written to `/storage/emulated/0/BIOS`. The ledger records the restore of the Founder's core and the G-3 line read back afterwards.
22. **Licensing of uploaded binaries (FR-67).** If P9 shows CI artifacts are public, R3d-4's G-1 guard also requires the notices and licence files in the package; otherwise it is recorded as an item 5 obligation in R3d-0.
23. **Store identity and path spelling (R3d-5).** Either canonicalise the path namespace before hashing, or state and test that a different spelling only invalidates records (no record) and never refuses the store.

---

## Astra stage 1 (S65, ACCEPT WITH CHANGES): adjudication

Astra's report is `.work/s65/astra/report1.md` (fresh R3d thread). All nine findings are incorporated; where they conflict with the body or the amendments above, these govern.

**A-1 Artifact boundary (finding 1).** Uploads come only from an explicit allowlisted staging directory, filled after a successful binary classification step; every upload, including the `if: always()` diagnostics upload, depends on that step. Any ordinary-route-capable core is rejected whatever its filename or default override value. Gate-open Windows RetroArch inputs are built inside the consuming job and never uploaded. Negative guard tests: renamed binaries, runtime overrides, failed builds, stale output directories. G-3's line also names the build target and configuration and any test or catalogue substitution. The gate controls project builds and distribution; it cannot stop someone editing source or redistributing a private qualification binary, and the plan says so.

**A-2 Regression trigger (finding 2).** Amendment 16's restriction is withdrawn. F-2 triggers on any demonstrated loss of existing gameplay: absent-resource configurations (no RiscPC firmware or boot archives) and RPC selections that then fail. The A/B compares identical resource configurations before and after, then separately tests fully provisioned RPC setups. Class A refusal stays the default; the census is evidence for adjudication, never a shipped exception table, and the manual Archimedes override is a recovery path, not proof of no regression.

**A-3 Reset transaction (finding 3; corrected by stage 2), frozen now.** An RPC reset has two branches, owned by the preparation module.
- **Pending or FAILED preparation:** invalidate any attempt, stop construction or generation, release offline ownership, keep the reset's launch-suppression state, and restart VERIFY/recovery. Nothing is attached until the normal START. Generation stays isolated from content, helper and IDE.
- **RUNNING:** one ordered transaction that keeps volatile CMOS and the HD:
  1. interrupt the attempt and close the execution barrier (R3c-3b part 2; in M1, with no plan, a preparation-owned execution barrier that needs no attempt object);
  2. detach the affected backing (preparation);
  3. re-stage drive 0 only, through a frontend host callback that never touches drive 1 (R3d-3 promotes it; the plan core supplies it before then);
  4. restore drive 1 under the admission policy: the preparation-owned no-op helper only where START would generate one (a revision-2 populated store with no supplied helper and no explicit override); revision-1 and helper-free sessions get no helper because of a Restart; an explicit drive-1 image is re-attached with unchanged identity, content and attachment choice, under the reset-time write policy; an explicit empty drive 1 stays empty;
  5. reset and rebind observers and input (frontend callback);
  6. resume only on complete success; any failure leaves a stopped session with a terminal status, never a resumed instruction, no dangling backing, published storage preserved, and an explicit outcome for the latest CMOS snapshot.
- Failure injection at each callback and attachment boundary tests those properties.
The frontend's `retro_reset` dispatch routes every RPC reset to the preparation module (R3d-3). **Disk-control setters:** R3d-3 owns the early pending-state rejection (admission checked before any observer or cancellation state is cleared, so a rejection has no side effects) and its tests; R3c-3b part 2 owns cancellation semantics after admission.

**A-4 Scanner semantics (finding 4), frozen for R3d-2.** Global entry or byte budget exhaustion gives one deterministic incomplete-scan result that refuses selection; prefix-dependent recognitions are discarded. D-165's partial-VFS-failure-with-a-candidate behaviour stays separate. "Missing" versus "inaccessible" is distinguished only as far as the VFS can establish it: a path that does not stat as valid is missing (empty, missing-firmware); a path that stats as a directory but cannot be opened or listed is unreadable. The locator owns no buffer after the scan, has a path-length limit, handles read and close failure, and refuses (class A, no fallback to a lower-ranked pair) when the selected file changed. Selection is fingerprint-based and filename-independent; among byte-identical copies the canonical path is the bytewise-smallest. R3d-2 may touch the frontend call site that creates the skipped inventory.

**A-5 Concurrent-writer ownership (finding 5).** A named production prerequisite before R3d-4, owned by R3d-3: an actual atomic acquisition mechanism (candidate: VFS directory creation, which fails if the directory exists; R3d-0 verifies it on Windows, Android FUSE and the Mac), alias handling, crash recovery of stale ownership and a failure policy. It covers the HD store, per-content CMOS and shared-default publication, including the fixed `.tmp` publication path. Tests: simultaneous open of the same title and of two titles generating the same defaults, with process kills around acquisition, publication and recovery. A check-then-write marker is not enough.

**A-6 Baseline projection (finding 6).** R3d-0 freezes an exact comparison projection and an enumerated list of semantic deltas per slice: R3d-1 the G-3 line and the corrected plan-core switching traces; R3d-2 the `skipped`/`complete` wording and the D-166(4) reason changes (a missing folder gives missing-firmware instead of unreadable-inventory; budget exhaustion gives the incomplete-scan result). Persisted bytes stay strictly equal wherever no persistence change is authorized. Amendment 20 is withdrawn: gate-open Archimedes loads stay identical to gate-closed, so R3d-4 AC-2 holds. Amendment 12's reload-only rule applies to RPC sessions only; Arculator Restart keeps reading Machine, CPU and OS as today.

**A-7 Timing and visibility (finding 7).** R3d-0 freezes separate metrics: preparation work per call, complete frontend-call duration, callback cadence and actual presentation, each with phase-specific limits, the value of T, the percentile method, exception attribution and treatment of pause, menu and fast-forward. A post-first-field observation records the AV transition. D-172's endpoints are named separately: load entry, first pending presentation, preparation RUNNING, desktop readiness; scan and locator work count in the ordinary-load budget; "reopen within 1 s" is not reinterpreted as desktop-ready. Visibility is required for sustained statuses, not every transient state. E0 informs the thresholds; qualification runs only against the frozen rules.

**A-8 Reachable CI paths (finding 8).** Four evidence classes, each labelled: flag-free production-module tests before routing (R3d-3, calling the module, not an ordinary route); plan-core frontend lifecycle tests before R3d-4; authored-catalogue ordinary-route integration tests after R3d-4, using link-time substitution at a narrow catalogue boundary without duplicating selection logic; and genuine shipping-configuration private D-173 legs. The no-ROM shipping test proves J3 only; J4 needs recognised authored firmware in the substituted build, or real firmware privately.

**A-9 Source delivery (finding 9).** Each distributed binary package identifies its exact source revision and provides the corresponding source and build scripts through an accepted mechanism, with the notices. This applies to diagnostics executables and test cores. P9 (S65): the repository is private. Privacy alone does not settle distribution: R3d-0 records the actual artifact audience and delivery arrangement (today the Founder only). The notices and source mechanism attach to the first qualifying distribution, including test and diagnostics binaries, as an item 5 release prerequisite; it does not delay R3d-1 or R3d-2.

**Answers to §8.** Q1 accept (requalify the eventual release head). Q2 keep today's text. Q3 accept with A-2. Q4 stable 4096×2048 maximum, base geometry and rate reset per load, full AV updates for timing changes. Q5 keep D-029/D-054 and tell players about the playlist/loose distinction. Q6 link-time catalogue substitution (A-8). Q7 see R3d-1 below. Q8 explicit lifecycle identity with the operations pointer kept valid, masking stopped-session identity and fault queries without losing failed-start diagnostics before their caller collects them. Q9 install the write observer, filtering irrelevant paths if journal traffic is costly. Q10 load and chooser confirmation are attempt boundaries; PRODUCT is revalidated against the current recovered store before it is offered or executed; N-10 says "close and reload the content". Q11 accept a bounded presentation-only hold that never claims desktop readiness or gate execution, yields to terminal messages and expires on its own. Q12 three Windows pairs for pre-qualification, all seven at the final checkpoint. Q13 phase-specific allowances (A-7). Q14 no gate digest in the receipt.

**Session-ID lifetime (Q7), frozen for R3d-1.** RetroArch may unload the core library between contents, so the contract is the narrower one: IDs are nonzero and unique within one library load, from a 64-bit counter; on exhaustion new sessions are refused. R3d-1 proves no session ID, attempt token or epoch derived from it is persisted or otherwise survives library unload (source guard plus tests), so no stale token can meet a reused ID. E0 P3 records whether reloads happen; the contract holds either way.

## Astra stage 2 (S65, ACCEPT WITH CHANGES): adjudication

Report `.work/s65/astra/report2.md`. All eight findings incorporated: A-3 is corrected above (two reset branches; helper restoration under the admission policy; R3d-3 owns pending setter rejection; M1 execution barrier without an attempt object). A-9 is corrected above (distribution decided by audience, not privacy). The R3d-1 and R3d-2 work orders in the handoff carry the rest: the scanner's metadata outcomes and refusal precedence, LOW-4's separate RPC notice obligation, reachable switching evidence, behavioural site mutants and capability queries outside a running session, session-ID allocation and the part 2 integration AC, and pinned branch bases with a run-H boundary.

**R3d-2 QA adjudication (S65).** (1) A-4/A-6 are amended: a firmware root whose metadata is invalid (which is what RetroArch reports for a folder that does not exist) gives the uncertainty-aware "missing or unreadable" outcome, not missing-firmware; there is no missing-folder delta. R3d-4's notice for that outcome covers both causes: "ArcDroid cannot find or read <system>/arcdroid/firmware/. Create that folder and add your RiscPC firmware, or check that it is readable." (2) When the inventory's resource-failure flag is set, R3d-4's notice says ArcDroid ran out of memory while checking firmware, never "add firmware". (3) R3d-4 never branches on the `vfs_failure` or `resource_failure` flags once `bounded` is set (they are order-dependent then). (4) R3d-3 and R3d-4 add no second large by-value inventory to the load path (the scan already peaks near 110 KB of stack).
