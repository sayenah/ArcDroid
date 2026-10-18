# 6b Track M: slice M2b (lockstep emitter, build class, two-process driver; format v1)

<!-- SPDX-License-Identifier: GPL-2.0-only -->

## Governing references
- **Decisions:**
  - D-142(5), D-146, D-180, D-182, D-188, D-191, D-195 (R-b, R-c, cross-check naming).
  - D-198 (`docs/decision-log.md:211`): M2b is dispatched after 1a-0 and before the L1/S1 work orders, merges before F1, and freezes format v1.
  - D-199 (`e1deb41:docs/decision-log.md:212`): L1 merges first, then S1; B3 is dropped. Its AC-9 caveats are in D-199(4).
- **`docs/riscpc-6b-m2a.md`:**
  - the M2b outline at `:305-334`;
  - amendments A-1..A-34 at `:345-386`, which bind this order, especially A-2, A-4, A-6, A-7, A-11, A-12, A-25..A-31 and A-32..A-34;
  - R-M2-3, R-M2-4 and R-M2-7 at `:39-77`.
- **`docs/riscpc-6b-1a0.md`:** R-e0-1..R-e0-4 (`:10-13`), the AC-5 object-identity method (`:39`), and AC-8, the frozen benefit rule (`:42-49`).
- **`docs/riscpc-6b-plan.md`:** CG-1..CG-10 (`:164-173`), the baselines (`:153-158`), and Track M AC-H1..H4 (`:258-261`).
- **`docs/riscpc-6b-phase1a.md`:** the L1 and S1 rows (`:41-42`).
- **`docs/riscpc-translator-invariants.md`:** phases §2.1 (`:228-235`), §2.2 (`:237-299`), exits, G-1..G-14 and Q-1..Q-11.
- **M2a deliverables:** the design note, schema, comparator, stream writer, schedule reference, corpus and mutants (`docs/riscpc-lockstep-harness.md`, `tests/rpc_lockstep_schema.json`, `tests/rpc_lockstep_reviewed_policy.json`, `tools/rpc_lockstep_compare.py`, `tests/rpc_lockstep_stream.py`). Before M2a merges they are cited at `origin/slice/6b-m2a-lockstep` `165efa4`.

**Basis.**
- Code citations are at `b57f772`. Between `b57f772` and `e1deb41` only `docs/decision-log.md` and `docs/handoff.md` differ.
- M2a's files are cited at `165efa4`, which predates 1a-0. Its schema is grounded at `bb5008f`.

## Branch and base
- **Branch:** `slice/6b-m2b-emitter`, from `main` **after M2a merges** (precondition P-1). PR #469 (1a-0) is already merged at `b57f772`.
- **Merge order:** M2b merges before L1, S1 and F1 (R-M2b-1).
- **What changes:** M2b changes no guest behaviour, timing, shipping output or libretro surface. Every hook compiles to nothing outside the new lockstep class.

**Precondition P-1 (stop if it fails).**
- M2a must be on `main` and its inventory test green.
- At `b57f772`, the M2a schema classifies none of 1a-0's new statics:
  - `rpc_mem_timing.c:59-61,87,95-96`;
  - `arm.c:2017-2019`;
  - `rpc_session.c:91`;
  - `rpc_crosscheck.c:5-6`, in the cross-check build only.
- If M2a merges without classifying them, scope item 1 does it under R-M2b-15. Sol does not start hook work until the inventory passes on the base.

## Rulings needed (each with a proposed ruling)

- **R-M2b-1 Order and baselines.**
  - Proposed: M2b merges before L1. L1 and S1 may be developed in parallel (D-199), but each rebases onto M2b and merges after it in the order L1, then S1.
  - L1 and S1 add "M2 self-consistency passes" and "M2 lockstep against the immediate predecessor" to their ACs (A-15, A-31).
  - D-195's post-P0i anchor is unchanged. No emitter is back-ported (A-31).

- **R-M2b-2 Exact P1 points (the A-6 projection ruling; this fixes format v1).** Each site kind has exactly one point. P1 sequence numbers count from 0 at each P0(k), in program order.
  - **Data access (sites 2 and 3).**
    - Point: entry of each of the four accessors (`mem.c:704,713,722,731`), **before** `translateaddress`. Each accessor call emits one P1.
    - Consequences: an aborted access still has its P1 (the abort early-return is at `:707/716/725/734`), and descriptor-walk MMIO events (`cp15.c:405,411`) carry that P1's identity.
    - LDM/STM (`arm_common.c:271,351,428,494`), SWP (`arm.c:904,909,965,969`), LDRT/STRT (`mem.h:120-182`) and LDRH-class instructions (`arm_common.c:57-221`) get **no** extra hook; they reach the accessors through `mem.h:66-112`.
    - `arc_rpc_mem_access` (`rpc_mem_timing.c:498`) gets **no** hook, because `fetch_slow` also calls it (`:481`).
  - **Fetch continuation (site 1).**
    - Point: immediately after `arc_rpc_mem_opcode(opcode)` (`arm.c:611`), before the profile lines and before `arc_rpc_observe_fetched` (`:616`).
    - Fetch-timing work inside `getpccache` (`cp15.c:471-488`, which leads to `arc_rpc_mem_fetch` at `rpc_mem_timing.h:147-158`, then to `fetch_slow` at `rpc_mem_timing.c:475-493`) is **not** a separate P1. Its effects (`fetch_hits`, requests, attributes) are compared in this record.
    - A prefetch abort (`arm.c:603-608`) emits no fetch P1.
  - **SWI (site 4).**
    - Point: in the loop body at the SWI case, before any observer or `opSWI` action. That is before `:1910` in the cheap copy and before `opSWI` at `:1914`.
    - **Not** inside `opSWI` (`arm_common.c:569`), because the cheap copy (`arm.c:1911`) and the observed copy (`arm_common.c:579-585`) can consume the SWI without calling it.
    - This amends A-2 for SWI: the SWI P1 is the semantic "SWI instruction reached with its condition passed".
  - **CP15 (sites 5 and 6).**
    - Point: entry of `cp15_read` (`cp15.c:300`) and `cp15_write` (`:163`).
    - Emitted only while an attempt is open (between P0 and P2). A call outside an attempt, such as the test-rate rebind at `rpc_session.c:640`, emits nothing and increments a host-only diagnostic count.
  - **Exception (site 7).**
    - Point: entry of `exception()` (`arm.c:505`), before `arc_rpc_mem_branch` (`:508`).
    - Nesting order follows program order. A SWI gives SWI P1 (n), then exception P1 (n+1). A data abort gives its accessor P1s, then the exception P1 from `:1944`.
  - **Mode transition (site 8).**
    - Point: at both §6 points in **all three** copies. These are after the body (`:1929`; the existing checks are at `:1931`/`:1933`) and after `instruction_done` (`:1956`; existing checks at `:1958`/`:1960`).
    - Emitted only when `arm.mode` differs from the mode recorded at the previous P0 or mode P1 of the same attempt. The unobserved copy gains this check only inside the lockstep macro.
    - `updatemode()` (`arm.c:89`) is not a P1 point.

- **R-M2b-3 P0, P2 and P3 placement.**
  - **P0(k): at the loop top, before `:590`.** That is before `arc_rpc_observe_begin` (`:592`), the integer-attempt diagnostic (`:595`) and the profile lines (`:598-599`).
    - Reason: P0 is the resumption point, and a resume enters at the loop top. So observer-begin for k belongs to k.
    - §2.1's "`arm.c:600`" (`docs/riscpc-translator-invariants.md:232`) is restated to match.
    - The first P0 of a batch follows `arc_rpc_mem_batch_begin` (`arm.c:583`). The cheap copy's `observe_batch_begin` (`:581`) also precedes it.
  - **P2(k): immediately after `arc_rpc_mem_instruction_end(PC)`** (`arm.c:1963`), before `ARC_RPC_PROFILE(end)` (`:1965`) and the continuation (`:1966`).
  - **P3: once, in the common wrapper `arc_rpc_arm_exec_batch`, after `arc_rpc_prefix_capture()`** (`arm.c:2076`) and before it returns.
    - Reason: the driver resumes from post-wrapper state, which includes `arc_rpc_mem_endpoint_return`'s changes to `test_stop` and `endpoint_complete` (`:2075`).
    - This also covers the NULL-batch path (`rpc_session.c:462`).
    - When endpoint admission returns 0 (`arm.c:2062-2063`), no attempt runs: no P0 or P3 is emitted and k does not change.

- **R-M2b-4 Source of k.**
  - Proposed: an emitter-owned attempt counter. It increments when an instruction attempt begins, never per emitted record (R-M2-3), and resets at every epoch bump.
  - It is **not** `arc_rpc_mem_fast.instructions`, which advances only when timing is enabled and resets at bind.
  - The A-4 mutant (a guard miss that bumps `instructions`) must remain visible as a field divergence, not as a key shift.

- **R-M2b-5 Epoch bump sites.**
  - Proposed: one lockstep-only epoch call at each G-13 entry point in `rpc_session.c`, never in `arc_rpc_mem_bind`. Bind sits in L1/S1's file, and some flows bind twice.
  - The G-13 entry points are:
    - start success (`:283`);
    - failed start (`:231-234`, `:261-262`);
    - reset (`:321-343`);
    - stop (`:153-178`), which also covers the Arculator↔RPC switch through `.stop` (`:560`);
    - test-rate rebind (`:633-642`).
  - The R3d reset/rebind site is unverified at this head and goes to M2c with the G-13 ROMs (A-20).

- **R-M2b-6 Fatal record (Q-9, A-11, A-32, A-33).**
  - **Where it is emitted:** at `fatal()` after the text is formatted (`rpc_session.c:121`) and before `arc_rpc_fault` is set (`:123`). Observer retire (`:125-126`), sound purge and the deadline clear (`:132`) have not yet run at that point.
  - **v1 payload:** the key, a reason class (a truncated SHA-256 of the format literal) and a text hash (a truncated SHA-256 of the formatted text). This needs no enum and no edits to the many callers.
  - **The record is followed by:**
    - every RAM/VRAM page hash;
    - the committed write and MMIO events up to the fatal;
    - an End frame.
  - **Key:**
    - Inside an attempt: the open key, with phase P1 if inside a helper, otherwise the last phase emitted.
    - Outside an attempt, meaning a driver fatal (`rpc_session.c:418,422,430,448,455,469`) or a fatal before P0 (`arm.c:577`, `rpc_mem_timing.c:375`): a new **phase 4, "driver"**, with k equal to the next attempt ordinal and site 0.
  - **Executed count at a fatal:** B + attempts completed (reached P2) in the current batch (R-M2b-11).

- **R-M2b-7 Hashing location.**
  - Proposed: hash in the engine. Add `arc_sha256_*` (`core/arcdroid_core.h:267-272`) to `rpc_services` in `engine/backend-symbols.json`.
  - Reason: at about 915 fields × 8 bytes per P0/P2, raw detail bytes cannot cross the sink every instruction.
  - Only compact, event, page, fatal and End frames, plus detail frames inside a requested detail range, cross the sink.

- **R-M2b-8 Page checkpoints and write observation (A-28, Q-g).**
  - **Dirty tracking:** lockstep-only dirty-page bits are set at the existing write points beside `ARC_RPC_PROFILE(store(...))` in `mem_phys_write32` (`mem.c:447,512,524`) and `mem_phys_write8` (`:584,651,666`).
  - **Page frames:**
    - at every P3 and frame end: hashes of the pages dirtied since the previous page checkpoint;
    - at an epoch change, a fatal and the end of the stream: hashes of every page.
    - This is a v1 format change.
  - **Full-audit mode:**
    - keeps a shadow copy of RAM and VRAM;
    - at every P3, compares the whole of guest memory with the shadow plus the logged writes since the last P3;
    - any byte that differs without a logged write fails as "unlogged write", naming the page;
    - in CI, runs only on directed ROMs at the smallest RAM configuration the plan core accepts, which Sol states with a citation, under the AC-15 cost bound.
  - A write-and-restore between P3s is not claimed to be detected (A-28).
  - The wholesale writers, reset (`mem.c:162-164`) and ROM load (`:69-76`), occur only at epoch points and are covered by the full-page checkpoint.

- **R-M2b-9 Sink, runner and driver.**
  - **Engine interface:**
    - A runner-owned write callback plus an opaque pointer, registered through a `RETRO_API` test function. It is exported by `dllexport`, as the endpoint and prefix APIs already are (`rpc_session.h:25-60`), so no `.def` line is added.
    - A status getter reports sink errors and emitter errors (unmapped pointer, unclassified state, unlogged write).
    - Capability bit `ARC_RPC_TEST_LOCKSTEP = 32u`, after the flags at `rpc_session.h:34-35`.
    - Refusing stubs under `TEST_FAULTS || TEST_API` in every other test core, using the idiom at `rpc_session.c:574-663` and refusing through `arc_rpc_test_refuse` (`:103-104`).
  - **Runner (`tools/rpc_runner.c`):**
    - The stream goes to a **path**, never to stdout (`:185`, `:457-486`).
    - New flags: density (baseline, every-instruction, full-audit), P1 on/off, detail key range, and stop-after key. With stop-after, the runner ends after the frame that contains that key; there is no new in-engine stop.
    - Lockstep support is detected at runtime from the capability bit (`:299-306`).
    - The sink status is polled where the cross-check status is polled (`:404-405`), and the runner exits non-zero on error.
  - **Driver:** a new `tools/rpc_lockstep_run.py`.
    - It runs two runner processes and feeds their streams into the M2a comparator. It uses FIFOs on POSIX and bounded temporary files on Windows.
    - On a divergence it reruns both with the comparator's detail range (harness `:122-126`). If the divergence does not reproduce, the verdict is "non-deterministic", which fails.

- **R-M2b-10 Android.**
  - Proposed: the lockstep core is not built for Android. It is excluded the same way the Android-specific block at `CMakeLists.txt:2604-2607` is scoped.
  - M2c's legs are Linux x64, Windows and host-arm64 (D-198).
  - Android object identity of the shipping and measurement cores is still required (AC-3).

- **R-M2b-11 Driver-local mirrors and the executed count (A-29, A-33, Q-j).**
  - **Mirrors:** lockstep-only static mirrors of `limit`, `video_start`, `event_work` and `instruction_work` (`rpc_session.c:405-410`). They are updated where the locals change (`:418,:431-439,:455,:462`) and classed as **live-control mirrors**, compared through the A-29 phase projection, never excluded.
  - **Counters kept by the emitter:**
    - B = `arc_rpc_executed` at batch entry;
    - C = attempts in this batch that have reached P2.
  - **Projected `arc_rpc_executed`:**
    - at P0(k) and P1(k): B + C, where C excludes k;
    - at P2(k): B + C, where C includes k;
    - at P3: the raw value after `:439` or `:462`;
    - on the NULL batch, `instruction_work` is projected as already booked (`:462`);
    - at a fatal: B + C, with k incomplete. Raw `arc_rpc_executed` (static `:53`) is diagnostic after a fatal (Q-9).

- **R-M2b-12 Build-class combinations.**
  - `ARCDROID_RPC_LOCKSTEP` is `#error` if any of the following holds:
    - `ARCDROID_RPC_MEASUREMENT` is defined;
    - `ARCDROID_TEST_FAULTS` is absent;
    - `ARCDROID_RPC_PROFILE` is defined (the `*_profile_body` renames, grounding §2);
    - `ARCDROID_RPC_CROSSCHECK` is defined.
  - The guard sits in `engine/rpc/rpc_namespace.h` **before** the split block that `#undef`s `TEST_FAULTS` (`:8-14`), following the shape of `rpc_profile.h:7-10`. The off form is `((void)0)` (`rpc_profile.h:59-61`).

- **R-M2b-13 Field names (A-19, Q-k).** The schema's `<tu>.<symbol>` names stand. 1a-0 has no field vocabulary beyond the test-only `crosscheck_field("issue_ticks", …)`.

- **R-M2b-14 S1's `finish_ns` (cross-slice, H-2).**
  - Proposed for the S1 order: `finish_ns` is a separate host-only static outside `m`, excluded, and reset at every G-13 bind before the early return (§2.2 `docs/riscpc-translator-invariants.md:290-296`). The draft placement "in `m`" (`docs/riscpc-6b-phase1a.md:42`) is withdrawn.
  - The same rule applies to L1's hint and key mirror.

- **R-M2b-15 1a-0 statics (needed if M2a has not classified them).**
  - Exercise statics and `exercise_batch` (`rpc_mem_timing.c:59-61`): host-only, excluded (R-e0-2).
  - Endpoint and measurement statics (`:87,:95-96`): **measurement** class (compared).
  - Prefix statics (`arm.c:2017-2019`): diagnostic, excluded (§2.2 prefix clause).
  - `arc_rpc_refused` (`rpc_session.c:91`): host-only test bookkeeping, excluded.
  - Cross-check statics (`rpc_crosscheck.c:5-6`): host-only, excluded (R-e0-2).
  - Both schema files are updated together under the two-file review rule.

## Objective
Deliver the lockstep emitter, which is the H-1 stream producer for the plan-core interpreter. It covers:
- P0, P1, P2 and P3 at the ruled points in all three loop copies;
- per-TU serialisers covering every §2.2 field and the M2a inventory;
- the non-mutating P0–P3 projection;
- one event chronology for RAM writes and MMIO;
- page checkpoints, with an independent write audit;
- fatal records and epochs.

It is built in a non-installed `arcdroid_rpclockstep_libretro` core with a runner-owned sink and a two-process driver into M2a's comparator. The format is frozen at v1.

## Scope
1. **Re-ground (A-6, P-2, P-3).**
   - Re-ground §2.2, the schema, the inventory and every citation at the base.
   - Classify the 1a-0 statics (R-M2b-15).
   - Refresh the stale citations:
     - §1.6 at `docs/riscpc-translator-invariants.md:171-182`: `fatal :117-134`, generation `:283`, budgets `:333/:344`, overflow `:211/:217`;
     - §2.1's P0 text (R-M2b-3);
     - the Phase 1a citation table at `docs/riscpc-6b-phase1a.md:247-248`;
     - the M2a order's line numbers listed in grounding P-2.
   - Restate `docs/riscpc-translator-invariants.md:269-275` against the macro split (A-18).
2. **Build class.**
   - Add the `ARCDROID_RPC_LOCKSTEP` option (`CMakeLists.txt:10-11`) with the R-M2b-12 guards.
   - Add `arcdroid_add_libretro(arcdroid_rpclockstep_libretro FALSE arcdroid_core_test)` inside the non-installed block near `:2575-2646`, with `TEST_FAULTS`, `TEST_PLANS` and `RPC_LOCKSTEP` defined and a new `engine/rpc/rpc_lockstep.c` added to its `_rpc` target.
   - Exclude the core from Android.
   - Add a lockstep arm to the build-identity string (`libretro/arcdroid_libretro.c:6533-6539`).
   - Add a lockstep row to the capability matrix in `docs/riscpc-translator.md:395-405`.
3. **Hooks.** P0, P1, P2 and P3 exactly as R-M2b-2 and R-M2b-3 rule.
   - Body hooks are written once in `arm.c:565-1981` and land in all three instantiations (`:1986-2011`).
   - P3 goes in the wrapper (`:2057-2079`).
   - Helper hooks go in `mem.c`, `cp15.c` and `exception()`.
   - Each hook is a macro that compiles to `((void)0)` outside the class.
4. **Serialisers.**
   - Each owning TU serialises its own statics field by field, in schema order, under the lockstep macro. The location of every field is listed in grounding §9.
   - Pointers are written as (allocation id, offset) or as an index. An unmapped pointer is an emitter error, never a raw address. The pointers are those in grounding H-9, and the `ramb*` aliases share their word allocations.
   - Conditional fields follow the schema's phase and mode axes (A-10, A-25).
   - Device TUs (`iomd.c`, `vidc20.c`, `fdc.c`, `keyboard.c`, `i8042.c`, `superio.c`, `cmos.c`, `ide.c`) and project TUs (`rpc_audio.c`, `rpc_input.c`, `rpc_floppy.c`, `rpc_rtc.c`, `rpc_observer.c`, `rpc_observers.c`) are covered, including reachable storage (A-29).
   - Immutable tables (`arm.c:58,211,213`; `rpc_mem_timing.c:725,734`) are serialised once per epoch as identity.
5. **Projection.** Written as specified prose in `docs/riscpc-lockstep-harness.md` before code. Astra stage 2 reviews it before dispatch.
   - At P0: `issue_*` follows the flush formulas (`rpc_mem_timing.c:656-657`) computed on a copy, never through `batch_flush` (`:644`) or `next_base` (`:244`).
   - At P2 in integer mode, `cpu_due_ticks`, `due_floor` and `due_fraction` are computed from `cpu_due_ns` and base. They are stale at `rpc_mem_timing.h:133-137`.
   - At P2 with requests pending, `cpu_due_*` is excluded.
   - At P3: the raw state.
   - Driver mirrors follow R-M2b-11.
   - Overflow is handled by non-faulting copies of `add()` and `ticks()` (`:209,:214`); the projection never calls `fatal`.
6. **Events.**
   - RAM writes are logged at the six write points (R-M2b-8).
   - MMIO is logged at physical dispatch:
     - reads: `mem.c:215,225,230,233,249,332,338,345,350,353,369`;
     - writes: `mem.c:457,467,477,481,504,594,603,608,618,622,640`.
   - Each event carries the agent tag. Walk reads (`cp15.c:405,411`) are tagged agent 1. Agents 2 and 3 are reported as structural zeros at this head, with the reason "DMA only reads" (invariants `:204`).
   - Events share one sequence per epoch and carry the identity required by A-27.
7. **Pages and audit** (R-M2b-8). **Fatal** (R-M2b-6). **Epochs** (R-M2b-5). **k** (R-M2b-4).
8. **Runner and driver** (R-M2b-9).
   - Extend the existing 1a-0 flags (`rpc_runner.c:203,244-256,336-342`) only as far as the driver needs them.
   - Add the new LOADs and the new DLL to `tests/rpc_windows_exports_test.py:11-13`.
9. **Format v1.** One bump, after which the format is frozen.
   - Fixed P1 points.
   - Phase 4, "driver".
   - The v1 fatal payload.
   - Dirty-page checkpoint semantics.
   - Header flags for P1-off and full-audit.
   - The design note, writer (`tests/rpc_lockstep_stream.py`), schema, reviewed policy and comparator are updated together. v0 streams are refused.
10. **Tests and mutants.** Committed per the ACs, with mutant anchors ported (H-5) the way `tests/rpc_lockstep_port_1a0.json` ports them.
11. **Docs.**
    - Design note: the P1 table, projection, sink and driver, and cost bounds.
    - §2.1 and §2.2 restatements.
    - `tools/rpc-r1-runbook.md`: lockstep commands.

## Non-goals
- Any fast path (L1, S1, A1, F1, D1, D2) and any change to `batch_next`, `batch_service`, `lookup()` or `commands()`.
- Shipping or measurement behaviour.
- Any `.info` change.
- G-13 lifecycle ROMs, reload after a fatal, cross-platform CI, and fatal-unwind cleanup under allocation failure (all M2c; A-20, A-24).
- Block-key, VA-key and G-10 mutants (owned by B-0 onward; A-30).
- The Android lockstep core.
- Tiny-cache and code-cache-cap modes.

## Architectural constraints and affected interfaces
- **Compile-out.**
  - Every hook, mirror, serialiser and sink exists only under `ARCDROID_RPC_LOCKSTEP`.
  - Outside that class, the only addition is the refusing stub in `rpc_session.c` under `TEST_FAULTS || TEST_API`.
  - Shipping objects stay byte-identical (AC-3).
- **Namespace and guards.**
  - Every new extern is `arc_rpc_`-prefixed, declared in `rpc_namespace.h`, and accounted for in `engine/backend-symbols.json`, as `tools/check-backend-symbols.py` requires.
  - The engine does no stdio.
  - A new header joins `PROJECT_HEADERS` (`tools/check-rpcemu-import.py:28`) and must pass `rpc-include-closure` (`CMakeLists.txt:452`).
- **§2.2 classes.**
  - Every new static is classed in both schema files:
    - mirrors: live-control;
    - emitter bookkeeping, dirty bits and shadow: host-only, excluded, reset at every epoch;
    - P1-outside-attempt count: diagnostic.
  - No masking of the raw inspector snapshot (`cp15.c:548-557`) or of `rpc_mem_timing.c:1015-1020`.
- **Provenance.**
  - Every edited `engine/rpcemu/` TU (`arm.c`, `mem.c`, `cp15.c`, and the device TUs) gets a dated ArcDroid modification notice within its first 2048 bytes, plus a new patch record and lineage entry in `engine/rpcemu/manifest.json` (`check-rpcemu-import.py:152-182`).
  - New files carry `GPL-2.0-only`.
  - No ROM, title or trace bytes are committed or logged. Private runs use the hash-only output (A-23).
- **Shipping identity.**
  - Only `arcdroid_libretro` is installed (`CMakeLists.txt:305,316-317`).
  - The lockstep core is non-installed, and Windows test DLLs keep the shared `tests/rpc_test_exports.def` (`:283-287`) unchanged.
- **Interfaces touched:**
  - `arm.c` loop body and wrapper; `mem.c` accessors and `mem_phys_*`; `cp15.c` (`cp15_read`/`cp15_write`, `checked_translation` walk reads);
  - `rpc_session.c` (fatal, G-13 sites, driver mirrors, stubs, capability bit);
  - read-only serialiser access to `rpc_mem_timing.c`. M2b adds no logic to the timing module beyond serialisers and projection copies; this is H-1 hygiene for L1 and S1.

## Acceptance criteria
- **AC-1 Re-ground and inventory.**
  - The inventory passes on the branch, and an added unclassified static fails it (A-3).
  - Every 1a-0 static is classed per R-M2b-15.
  - The citation refreshes from scope item 1 are present. Fable verifies a sample.
- **AC-2 Build class and refusal.**
  - Committed negative-compile tests show `#error` for each R-M2b-12 combination.
  - Every other test core (`arcdroid_libretro_test`, rpcplan, rpcmeasure) reports bit 32 absent and refuses the sink explicitly; the runner prints the refusal and exits non-zero.
  - The shipping core has no lockstep symbol (symbol guard).
  - `rpc-windows-test-exports` covers the new LOADs and the new DLL.
- **AC-3 Zero footprint (1a-0 AC-5 method).**
  - (a) Every shipping object's code sections are byte-identical to `b57f772`'s, on host, MinGW and Android.
  - (b) 1a-0's committed measurement-versus-shipping script passes unchanged, within D-199(4)'s caveats.
  - (c) In the plan and ordinary test cores, every object except `rpc_session.o` has identical code sections, and `rpc_session.o` differs only by the enumerated stub and capability bit.
- **AC-4 State accounting.**
  - A lockstep-class `offsetof`/`sizeof` test shows every member of every serialised struct classified.
  - The fixture field layout covers all bytes except padding, including the class-dependent `test_stop`/`test_measure` (`rpc_mem_timing.h:22-24`).
  - Mutants killed:
    - a member added to `m` (the `finish_ns` shape);
    - a member dropped from a serialiser;
    - a pointer hashed as an address.
  - An **allocation-shift run**, using lockstep-only padding that moves every allocation, gives an identical stream.
- **AC-5 P1 density and points.**
  - Per model on the M2a corpus, realised P1 counts per site agree with independent counts that run no emitter:
    - fetch P1 = attempts − prefetch aborts;
    - SWI P1 includes observer-consumed SWIs (cheap and observed copies);
    - data P1 = accessor calls, including aborted ones;
    - CP15 P1 excludes calls outside an attempt (`rpc_session.c:640`);
    - mode-transition P1 is identical across the three copies.
  - P0, P2, P3, event, page and fatal records are identical with P1 on and off.
  - Mutants killed:
    - SWI hooked only in `opSWI`;
    - an extra P1 in LDM/STM (double count);
    - a P1 in `arc_rpc_mem_access`;
    - `cp15_read` emitting outside an attempt;
    - an inlined helper with no P1 (A-2);
    - a P1-transient mutant, killed with P1 on and escaping with it off.
- **AC-6 Copies and configurations (A-7, A-25).**
  - Observer off, unmatched, trace and active give identical key sets, and identical records under mode (c), across the three copies.
  - Trace on and off give identical streams under mode (d).
  - The G-1 cheap-copy truncation ROM (`arm.c:1912`) passes.
- **AC-7 Projection.**
  - P0 projected at quota 200 equals P0 materialised at quota 1 under mode (b).
  - P2 integer-mode `cpu_due` is computed, and a stale-representation mutant is killed.
  - A lockstep self-check memcmps the raw inspector and observation snapshots before and after every projection, with zero differences.
  - Mutants killed:
    - a digest that flushes state (calls `batch_flush`);
    - projection through `next_base`;
    - a projection that faults on overflow;
    - the final-instruction overflow case.
- **AC-8 Self-consistency (AC-H1 in full, Q-7).**
  - Two processes give identical streams over the full corpus: the CI seed subset in CI, and the local-full subset in Sol's report.
  - Quotas 1, 7 and 200 agree under mode (b).
  - Every disagreement goes to Fable as an oracle finding. No item is excluded without a ruling.
- **AC-9 Events, pages and write audit.**
  - Directed items show:
    - walk MMIO tagged agent 1;
    - a RAM/MMIO cross-order swap and a time shift detected (A-27 on real streams);
    - write-buffer-at-capacity, timing-register write with `m.count > 0`, and stale fetch after SMC proven from field values (A-8).
  - Full-audit passes on the directed set.
  - Mutants killed:
    - a store that bypasses the funnel (unlogged write);
    - a dropped dirty bit;
    - a walk tagged as CPU.
- **AC-10 Fatal (A-11, A-32, A-33).**
  - M2a's directed fatal ROMs produce records at the ruled keys: bad mode (`arm.c:177`), the request and packet budgets (`rpc_mem_timing.c:333,344`), and the instruction budget (`rpc_session.c:430`).
  - The phase-4 driver key is used for the outside-attempt fatals.
  - Each record carries the page hashes and the committed logs.
  - The "awaiting emitter" RAM expectations become **demonstrated**.
  - A mutant that emits after observer retire is killed.
- **AC-11 Epochs and k.**
  - Each G-13 site bumps the epoch exactly once, with k and the event sequence reset.
  - A double bind does not double-bump.
  - Mutants killed:
    - a missing bump at reset;
    - k taken from `fast.instructions`;
    - a guard miss that applies `instructions++` or a TLB fill (A-4);
    - P3 emitted on zero admission.
- **AC-12 Runner and driver.**
  - Runner stdout is byte-identical with the stream on and off.
  - A localisation rerun reproduces an injected divergence and names the earliest (key, field). Injected non-determinism yields "non-deterministic", which fails.
  - A sink error exits non-zero.
- **AC-13 Kill matrix (A-30).** The following become **demonstrated**:
  - the four AC-H2 mutants (`docs/riscpc-6b-plan.md:259`);
  - Astra 1–3: replay after a helper, repeated exception handling, demand before a buffered store, queued video at grant end, delayed video completion, one-instruction quota;
  - M2a's digest-only list, except the block-key, VA-key and G-10 mutants, which stay **awaiting candidate mechanism**.
  - Any mutant killed by only one item is flagged.
- **AC-14 Existing suites and guards.**
  - Passing: `rpc-qa-mutations`, `rpc-observation-mutants`, `rpc-observers-mutants`, `rpc-measure-mutants`, the profile mutants, M2a's suites (anchors ported, each matching once), all 25 goldens, the calibrations, and `rpc-observation-plan`.
  - Passing guards: import, include-closure, symbol, frontend-boundary and export.
- **AC-15 CI cost (W4 rule).**
  - Every new test has an explicit timeout, and the worst host and Windows durations are at most 60 % of it.
  - Dense SHA-256 per group (about 915 fields), dirty-page hashing and full-audit memcmp are each bounded and stated per item.
  - Mutant tests are `RUN_SERIAL`.
- **AC-16 Provenance and public text.** Notices, manifest lineage, licence, asset and public-text guards all pass. No generated byte is committed.

**Obligations this order places on L1 and S1** (they go into their work orders; they are not M2b ACs):
- the AC-8 frozen benefit rule (`docs/riscpc-6b-1a0.md:42-49`) as the benefit decision, on Fable's private Odin3 and Windows legs;
- D-180 within 3 % on hot and 031, with observers genuinely off (D-199(1));
- CG-1..CG-10 as they apply in Phase 1a;
- the retained reference path kept as the oracle and as the cross-check comparator;
- M2 self-consistency, plus M2 lockstep against the immediate predecessor (A-31);
- new host-only state classified in both schema files and kept outside `m` (R-M2b-14).

## Evidence
- **Sol**, in a `SOL-REPORT-6B-M2B.md` at the clone root (not committed):
  - commands and results for AC-1..AC-16;
  - AC-3 identity output for host, MinGW and Android;
  - the per-model P1 count table with its independent counts;
  - the kill matrix with evidence levels;
  - the projection specification as committed;
  - CTest durations;
  - per-seed stream hashes from host and MinGW.
- **Fable, private:**
  - Astra stage 2 on the R-M2b-2/R-M2b-3 points and the projection, before dispatch;
  - the lockstep suite on Mac arm64 (an early AC-H3 leg);
  - Windows durations checked against W4;
  - hash-only self-consistency on a private-title prefix (A-23);
  - adjudication of AC-8 oracle findings.
- **No Odin3 performance legs.** AC-3 is what keeps D-180 unaffected.

## Risks and QA focus
- **A hook that does not compile out:** shipping or measurement codegen drift in all three loops, which would make D-180 apply to this slice.
- **P1 points that differ by copy:** SWI, the mode check in the unobserved copy, or `observe_begin` ordering at P0.
- **A projection that mutates state or calls `fatal`.**
- **Unclassified struct members or pointers serialised as addresses.**
- **Epoch bumped twice or missed:** double bind, failed start, or the switch path.
- **Wrapper-level P3 ordering against 1a-0's endpoint and prefix hooks.**
- **The fatal record emitted after retire or deadline clear.**
- **Shadow audit and page-hash cost blowing the CI timeouts.**
- **A sink leak to stdout.**
- **Merge conflicts with L1/S1 in `rpc_mem_timing.c`,** for which M2b should add only serialisers.
- **Mutant anchor drift.**
- **Leakage of private names in streams or logs.**

## Completeness critic (S67 design workflow): binding corrections

Every finding below is incorporated as a binding correction and overrides conflicting text above. Where a finding offers alternatives, Fable's choice in the next section governs.

**HIGH**

H1. **The data P1 at accessor entry breaks M2a's comparator ordering and §2.1's P1 definition.**
- The comparator orders events (rank 0) and pages (rank 1) before the checkpoint that covers them, at the same key. It rejects any record whose order regresses (`165efa4:tools/rpc_lockstep_compare.py:202-209,329-331`).
- If the P1 record is emitted at `readmemfl`/`writememfl` entry (`mem.c:704,722`), the access's own RAM and MMIO events, and any walk MMIO from `translateaddress`, are emitted after it with the same (k, 1, site, seq). That fails with "record order regressed".
- §2.1's P1 row (`docs/riscpc-translator-invariants.md:236`) also requires "`arc_rpc_attributes` as the current access's translation left it". That is post-translation state.
- **Correction:**
  - Allocate the P1 sequence at accessor entry, so walk and access events carry it.
  - Emit the P1 record at every accessor exit: after the physical access, and also on the abort early-return at `:707/716/725/734`.
  - Restate "before `translateaddress`" accordingly.
  - Add a comparator-order test to AC-5.

H2. **The fatal key contradicts AC-5 and the comparator.**
- "Phase P1 if inside a helper, otherwise the last phase emitted" cannot be computed, because there are no helper-exit hooks. With P1 off it gives a different key, which breaks AC-5's "fatal identical with P1 on/off".
- A fatal reusing the last P1 key has the same order as that checkpoint. Kind 5 is not allowed to repeat an order (`compare.py:331`), and its pages (rank 1) would regress behind it.
- The order says pages and events *follow* the fatal frame. The comparator rejects any record after kind 5 except End (`:276`, `:318-319`), and the harness says "logs and page hashes precede it" (`165efa4:docs/riscpc-lockstep-harness.md:111`).
- v0's fatal payload is 8 bytes (`compare.py:317`). The proposed reason-plus-text-hash payload is a format change that scope item 9 does not list.
- **Correction:**
  - Give the fatal its own key: a dedicated fatal phase with k = the open attempt (or the next attempt outside one), and site and seq taken from always-maintained counters.
  - Emit the full page frames before the fatal frame, then End. Events are already in the stream; do not re-emit them.
  - List the payload width change in v1.
  - State that P1 sequence counters and event identity are maintained even when P1 emission is off.

H3. **A P3 in the wrapper cannot see "the raw `arc_rpc_executed` after `:439`/`:462`".**
- The wrapper returns before the driver runs `instruction_work += executed; arc_rpc_executed += executed` (`rpc_session.c:438-439`, `:462`). So at a wrapper P3 the raw value is still B.
- `instruction_work` likewise differs between the two paths: it is booked before the NULL call (`:462`) and after the ordinary call (`:439`).
- **Correction:**
  - Define the P3 values as projections: executed = B + `linecyc`; `instruction_work` = mirror at entry + `linecyc` (ordinary path), or the booked value (NULL path, including zero admission).
  - State the k carried in the P3 key.
  - Restate §2.1's P3 row (`invariants:238`, "after final flush… resumer `rpc_session.c:411`") along with P0.

H4. **AC-6 and the AC-5 "identical across copies" claim are false as written.**
- The cheap copy truncates the batch after a SWI (`arm.c:1912`) and the others do not. P3 keys, and P3 page frames, therefore differ between copies, and between trace on and off. The comparator skips P3 only in cross-quota mode (`compare.py:377-380`, `optional() :257-259`).
- An active owner that completes a SWI (`rpc_observer.c:452-458`; `arm_common.c:583`; `arm.c:1911`) suppresses the exception. That removes the exception and mode P1s and changes guest execution entirely.
- **Correction:**
  - Rule that modes (c) and (d) treat P3 detail, compact and page records as optional, as cross-quota does. This is a v1 comparator change.
  - Restrict the "active" configuration to owners that never complete. Alternatively, list completion as an intended divergence with a directed item.

H5. **No mechanism carries the schema's mode and phase rules into C, but compact hashes are computed in the engine "after projection".**
- The comparator cannot re-project compact groups.
- The header's mode must match the comparison mode (`compare.py:226`). The schema SHA-256 must match the engine's (`:225`).
- Detail frames must follow schema order across TUs.
- The runner flags in R-M2b-9 include no comparison-mode flag.
- **Correction:**
  - Generate the C field tables, conditions and mode exclusions from `tests/rpc_lockstep_schema.json` at build time. Embed its hash and fail the build on drift.
  - Add a `--lockstep-mode` flag, a baseline/candidate role flag (`compare.py:362`; "baseline must be dense" `:239`), and the plan, schedule and configuration hash inputs.
  - Add an AC: a schema edit without regeneration fails.

H6. **The v0 header declares the P0/P2 counts up front (`compare.py:224,271`), so it cannot be written to a FIFO.**
- R-M2b-9 streams over FIFOs, and the counts are known only at the end of the run.
- **Correction:** move the declared counts and omissions to the End frame in v1, and list this in scope item 9. Alternatively, drop FIFOs and use seekable files everywhere.

H7. **The MMIO event list is incomplete.**
- Writes omitted:
  - `iomd_mouse_buttons_write` (`mem.c:437,571`);
  - `writeidew` (`:464,:491`);
  - `writevidc20` (`:487`), which is every VIDC20 write;
  - `writeide` (`:627`);
  - the absorbing Econet range (`:470-472,:611`).
- Reads omitted: `iomd_mouse_buttons_read` (`:219,:336`), `readidew` (`:223,:237`), `readide` (`:357`), and the float range at `:339-341`.
- Unmapped I/O that falls through to `break` or `return 0` is also unlogged.
- **Correction:** log once per `mem_phys_*` call using the existing I/O predicate (`mem.c:284-285,547-548,692-693`, the profile-wrapper shape), as a lockstep-only wrapper. Log reads after their value returns. Add an AC that cross-checks MMIO event counts against the profile build's `mmio_reads`/`mmio_writes`.

H8. **No AC shows that the lockstep class leaves guest behaviour unchanged.**
- AC-12 compares stream on and off only inside the lockstep core. Hook side effects that run whenever the class is compiled escape it. Examples: a serialiser calling a stateful getter, a translation that fills the TLB, a projection that touches the scheduler.
- AC-7's memcmp covers only the inspector and timing snapshots (`cp15.c:548-557`, `rpc_mem_timing.c:1015-1020`). These omit the scheduler, devices, observers, the ARM state and RAM.
- **Correction:**
  - Add an AC that the lockstep core, with the sink attached in every density, gives runner stdout byte-identical to the plan core over all goldens and the M2a corpus.
  - Widen the AC-7 self-check to a full serialised-state digest, plus the RAM dirty set, taken before and after every projection.

**MEDIUM**

M1. **The P0 projection is under-specified for integer mode.**
- §2.1 requires `cpu_due_*` = issue at P0 (`invariants:235`).
- In integer mode, `batch_next` updates only `now_ns` (`rpc_mem_timing.h:179-180`). `cpu_due_ticks`, `due_floor` and `due_fraction` stay stale at P0 and P1 as well as at P2. At quota 1 they are materialised by flush and `next_base` (`.c:655-658,252-255`) and then by `mem_ready` (`:893-895`).
- Scope item 5 projects only `issue_*` at P0.
- **Correction:** specify all of `issue_*`, `cpu_due_*` and `due_*` at P0 and P1, and `issue_*` at P2, in integer mode. Never run `flush`'s exercise side effect (`:648-653`). Add a mutant that projects `cpu_due` stale at P0.

M2. **Event `time_ticks` (A-27) has no integer-mode rule.**
- CPU-agent RAM and MMIO events inside an integer batch would carry a stale `issue_ticks`.
- Quota 1 and quota 200 would then disagree under mode (b).
- **Correction:** CPU events carry the projected issue time of k, computed by the same pure function as P0. Add a cross-quota event-time test.

M3. **The fetch P1 is asymmetric and leaves walk events unassigned.**
- With the fetch P1 after `getpccache`, fetch-walk MMIO (`cp15.c:405,411` via `getpccache`) occurs before the P1 and is not attributed to any P1.
- On a prefetch abort no fetch P1 exists, so the sequence number the exception P1 takes is undefined.
- **Correction:**
  - Allocate a fetch sequence before `getpccache` and attach walk events to it.
  - Emit the fetch P1 record after opcode capture.
  - On a prefetch abort, either emit a fetch P1 that records the abort, or state that the sequence is consumed and skipped.
  - Restate AC-5's fetch count formula to match.

M4. **Epoch sites double-bump.**
- `arc_rpc_stop` runs inside `start_begin` (`rpc_session.c:221`) and inside both failed-start landings (`:232,:262`).
- A successful start therefore bumps at stop and again at `:283`. A failed start bumps up to three times.
- Reset runs `mem_reset` (`:335`) before `reset_devices`, so a bump at entry would checkpoint pre-reset memory.
- Before the first success `arc_rpc_generation` is 0, and the comparator requires generation > 0 (`compare.py:327`).
- **Correction:** set an idempotent "epoch pending" latch at any G-13 site and consume it once, after the flow completes, before the next emission. Define keys for generation 0. AC-11 should test per *flow* (start, failed start, reset, switch, rate), not per site.

M5. **The fatal hook runs on every `fatal()` in the class, including start and allocation paths.**
- `memory_prepare` fatals (`mem.c:124-130`) leave RAM missing or uninitialised (the memsets are phases 4-6). Page-hashing it is undefined behaviour and nondeterministic.
- A fatal raised while a frame is partly written corrupts the stream.
- An End frame at the fatal plus another at teardown would duplicate End.
- **Correction:**
  - Hash pages only once memory is prepared; otherwise record them as absent.
  - Add a re-entrancy guard: emitter errors set a status and never call `fatal`.
  - Assemble each frame whole before it reaches the sink.
  - After End the sink is closed, with exactly one End.

M6. **Dirty-only page frames break cross-quota comparison and weaken A-12.**
- With dirty sets "since the previous page checkpoint", frame-end frames depend on where P3 checkpoints fell, so they differ across quotas. The comparator skips only P3 pages.
- A-12 requires full page hashes at every P3 for directed items (`riscpc-6b-m2a.md:356`).
- **Correction:**
  - Keep separate dirty sets per checkpoint class: one for P3, one reset at frame end.
  - Directed items run full-page checkpoints at P3.
  - Name the frame-end hook site, `arc_rpc_frame`/`run_frame`.

M7. **R-M2b-15 misclassifies `rpc_mem_timing.c:87`.**
- `interval_exercise_start` and `interval_exercise_end` are exercise snapshots. §2.2 excludes them (`invariants:291`, "interval exercise snapshots"), and R-e0-2 makes them host-only.
- **Correction:** classify `:87` as host-only and excluded. Only `:84-86,:95-96` are measurement state.

M8. **The `.def` choice contradicts binding A-18 without amending it.**
- A-18 requires the sink API to be exported through `tests/rpc_test_exports.def` and checked (`riscpc-6b-m2a.md:362`). R-M2b-9 uses dllexport only and keeps `.def` unchanged.
- **Correction:** either add the names to `.def`, with refusing stubs in every test DLL, or record an explicit amendment of A-18.

M9. **AC-3 omits the measurement core.**
- D-199(3) says the AC-8 A/A band is still being collected on the measurement core. L1/S1's frozen benefit rule depends on that core being unchanged.
- AC-3(b) checks only the named hot functions.
- **Correction:** add AC-3(d): every measurement-core object except `rpc_session.o` (the enumerated stub) has identical code sections to the base. Compare against the M2b merge-base rather than `b57f772`, since M2a and docs merges intervene.

M10. **AC-5's "independent counts that run no emitter" have no named instrument.**
- Profile counts `reads`/`writes` only after the abort check (`mem.c:708,726`). It has no SWI, CP15 or mode counters (`rpc_profile.h:27-31`), and lockstep plus profile is `#error`.
- **Correction:** for each site, name the source: profile `attempts`/`fetched`/exceptions on the plan core, or the authored corpus items' statically stated counts (SWIs, aborted LDMs, MRC/MCR, mode changes). Give a stated expected value per item.

M11. **The cost of dense hashing is unbounded at design time.**
- Every P0, P1 and P2 hashes about 915 fields plus reachable storage. That includes the VIDC presentation buffer (`vidc20.c:241`, `ARC_RPC_VIDEO_LIMIT²×4` bytes) and `cpu_delta[1000]`.
- An LDM gives up to 16 P1s.
- **Correction:**
  - Class large reachable buffers as output-compared at frame end and epochs only.
  - Cache per-group hashes, invalidated by dirty or generation counters.
  - State the bytes hashed per record and per instruction in the design note before AC-15 timeouts are set.

M12. **AC-13 and the ACs drop two items from the M2b outline.**
- "Field sensitivity" (`riscpc-6b-m2a.md:321`) is missing. Every schema field must be perturbable in the emitter and be detected; AC-4 has only one serialiser-drop mutant.
- "H-2 checked against M2a's vectors" (`:318`) is missing.
- **Correction:** add a generated per-field sensitivity AC, and either an H-2 vector AC or an explicit deferral ruling.

M13. **There is a cross-slice collision with S1.**
- S1 moves the `.h:171-173` flush into service. That changes the raw integer-mode state at P0 and P2, which M2b's projection is written against.
- **Correction:** add to S1's obligations: re-verify AC-7 (projection equals materialised at quotas 1/7/200, plus the stale-representation mutants) after moving the flush, and update the projection prose in the same PR.

M14. **Emitter allocations perturb test semantics.**
- Shadow, dirty bitmap and allocation-shift padding made through `arc_rpc_alloc` (`rpc_session.c:108-115`) shift `arc_rpc_allocations`. That moves the `--fail-alloc` stage numbering (A-20) and the snapshot allocation counts.
- **Correction:** emitter-owned memory bypasses the counted allocator. Padding is added inside `arc_rpc_alloc` without changing either counter. Add a test that the stage numbering is unchanged.

**LOW**

L1. "Smallest RAM configuration the plan core accepts" does not exist. RAM is fixed at 64 MiB (`rpc_session.c:238`; `mem.c:156-163`), and VRAM is 2 MiB per model. Bound the full audit at 66 MiB per P3.

L2. Adding `arc_sha256_*` to the single `rpc_services` list (`engine/backend-symbols.json`) also allows it in shipping objects. Add a shipping-only assertion that no `arc_sha256` reference exists, or hash inside `rpc_lockstep.c` only.

L3. The order's "declared in `rpc_namespace.h`" is wrong. Per `rpc_namespace.h:16-21`, new `arc_rpc_` names are declared in the `rpc_*.h` headers and registered in `backend-symbols.json`, with no alias.

L4. The P1 inside LDRT/STRT sees the transient `memmode=0` (`mem.h:120-182`). Rule whether P1 records the raw or the restored value, since compiled candidates must reproduce it.

L5. The mode-transition P1 is not ordered relative to `arc_rpc_observe_mode` (`arm.c:1931,1958`), which mutates observer state. State the order.

L6. The driver mirrors omit `completed` and `stop_at_field`, which force quota 1 and break the loop (`rpc_session.c:415-421,436`). Mirror them as live-control.

L7. When the comparator returns early, the runners block on full FIFOs. The driver must close the read ends and terminate only the processes it started, with a recorded PID (CLAUDE.md process ownership).

L8. R-M2b-10's "scoped like `:2604-2607`" is wrong, because that block renames output and excludes nothing. Use `if(NOT ANDROID)`.

L9. P-1's fallback ("if M2a merges without classifying…") implies a red `main`. Make the classification an M2a rebase gate, and keep R-M2b-15 as the ruling handed to M2a.

L10. Stream files from private titles contain event values. Keep them in private scratch, delete them after the run, and never upload a non-authored stream as a CI artifact (A-23, CG-9).

L11. The reason hash of a format literal cannot tell apart identical literals at different sites (`rpc_session.c:418` vs `:455`). Accept this explicitly, or add a lockstep-only site tag at driver call sites.

## Fable's choices on the critic's alternatives (S67)

- **H1:** allocate the data-access P1 sequence at accessor entry (so walk and access events carry it) and emit the P1 record at every accessor exit, including each abort early-return.
- **H2:** a dedicated fatal phase; full page frames precede the fatal frame, then End; the payload width change is part of v1; P1 sequence counters and event identity are maintained when P1 emission is off.
- **H4:** both: equality configurations use only owners that never complete a SWI, and a directed item records completion as an intended divergence; comparison modes (c) and (d) treat P3 detail, compact and page records as optional (a v1 comparator change, with its own comparator tests).
- **H5:** generate the C field tables, conditions and mode exclusions from `tests/rpc_lockstep_schema.json` at build time, embed its hash, and fail the build on drift; add `--lockstep-mode`, a baseline/candidate role flag and the plan, schedule and configuration hash inputs.
- **H6:** move the declared P0/P2 counts and omissions to the End frame in v1; keep FIFOs.
- **H7:** log MMIO once per `mem_phys_*` I/O call through a lockstep-only wrapper on the existing I/O predicate; cross-check event counts against the profile build's MMIO counters.
- **M8:** keep A-18: add the sink API names to `tests/rpc_test_exports.def` with refusing stubs in every test DLL.
- **M12:** add a generated per-field sensitivity AC; H-2 consumption is deferred to B-0 (no block exits exist before it, A-1), recorded as an explicit deferral.
- **M13:** S1's order carries the obligation to re-verify M2b's projection after moving the integer flush (and update the projection prose in the same PR).
- **L9:** the classification of 1a-0's statics is an M2a gate (M2a run E), not an M2b fallback.
- **Ordering:** M2b is dispatched before L1 and S1 (D-198(1)); L1 and S1 may be developed in parallel after M2b is dispatched, and both merge after M2b, L1 first.

## Astra challenge (S67, ACCEPT WITH CHANGES): Fable's adjudication

6b thread `01a13e62-8e2e-7f10-b35a-de57821ce0e7`, S67 challenge of M2b, L1 and S1 together (report private). All findings that concern M2b are incorporated and override earlier text.

- **AS-1 (Astra 1) Entry and exit checkpoints at data accesses.** The data-access P1 keeps an **entry** checkpoint (canonical state before the helper, E3) and an **exit** checkpoint (after the physical access or the abort return); the ordering contract distinguishes entry, the access's walk and RAM/MMIO events, and exit at the same (k, site, sequence). `arc_rpc_attributes` is specified at each point. Mutant: an entry-state transient that the helper's own translation or timing work repairs before exit is killed by the entry checkpoint.
- **AS-2 (Astra 2) Observer projection table before freezing v1.** M2b completes the schema's observer projections (diagnostic admission, batch representation, semantic instruction identity, live origin and watch state) for comparison modes (b) cross-quota and (d) trace on/off, keeping strict mode complete, with concrete vectors (cheap against observed, quota 1 against 200, trace on against off) committed before the C tables are generated; schema changes go through the reviewed policy.
- **AS-3 (Astra 3) Page checkpoint identity.** Page checkpoints carry an explicit boundary kind (P3, frame end, epoch retirement, post-transition, fatal) and an occurrence identity independent of instruction attempts; the ordering of all kinds is defined; the epoch latch's consumption is specified for successful, failed and nested lifecycle flows and for termination without a further P0. Tests: consecutive idle frames, reset without execution, failed start.
- **AS-4 (Astra 4) Online span declarations.** Sparse and compiled spans are declared online in the stream (open and close records with bounded state) with final accounting in End; the header's 256-span cap is removed. Test: more than 256 alternating spans over a FIFO. Full-audit fetch requirements and mandatory helper checkpoints stay enforceable.
- **AS-5 (Astra 5) Reproduction across encodings.** A compact mismatch is reproduced by recomputing the original compact group hashes from the detail rerun, verifying the original divergence and matching prefix, then reporting the earliest differing leaf. Tests: compact to detail success, changed reproduction, earlier divergence.
- **AS-6 (Astra 6) Compatibility hash and runner/core pairs.** The stream header carries a wire/projection compatibility hash computed from the compared field layout and rules only; inventory and provenance metadata are hashed and reviewed separately (inventory review stays enforced by its own test). The differential driver takes two explicit runner/core pairs. (The ABI handshake for structure-copy APIs is L1's, AS-L1-6.)

## Fable's rulings on Sol's dispatch stop (S67)

Sol stopped run A before implementation on two conflicts. Both rulings below bind and override earlier text.

- **R-M2b-S1 Measurement-core identity.** The dispatch's "the default measurement core is byte-identical to `main`" is withdrawn; the order governs. The shipping core has no lockstep symbol and its objects are unchanged (AC-2, AC-3). The measurement core carries the new refusing stubs and the `.def` exports in every test DLL (R-M2b-9 with Fable's M8 choice). Its identity check is critic M9's AC-3(d): every measurement-core object except `rpc_session.o`, the enumerated stub, has code sections identical to the M2b merge-base, and 1a-0's AC-5(b) holds on the named hot objects. `rpc_session.o` differs only by the enumerated refusing stubs; AC-3 states that difference explicitly.
- **R-M2b-S2 Budget fatals in AC-10.** M2a established (`tests/rpc-lockstep-expectations.md`) that the request and packet budgets (`rpc_mem_timing.c:333,344`) are internal invariants that no valid bounded guest can overflow, and that the driver instruction cap (`rpc_session.c:430`) cannot be exceeded within one advance; it delivered no directed ROMs for them. AC-10 is amended:
  - **Bad mode** (`arm.c:177`) stays a guest-reachable directed ROM, with its record at the ruled key, page hashes, committed logs and the "awaiting emitter" RAM expectations demonstrated.
  - **The three budget fatals** are demonstrated by a **test-only injection that exists only in the lockstep core class**: it forces the existing check's failure path at a chosen key without changing any budget, constant or condition used by any other build. The fatal records at those sites carry the same fatal-phase content as bad mode (page frames, committed logs, End). The evidence is labelled **injected**, never guest-reachable, in the report, the expectations file and the kill matrix.
  - AC-3 proves the injection is absent from the shipping core, the measurement core and every other test core.
  - The clock-time overflow path stays an unexercised, reported limit, as M2a recorded.

## Fable's rulings on run B's cross-quota oracle findings (S67)

Run B's authored point ROM shows three quota 1 against quota 200 differences in comparison mode (b). Each is ruled below; each applies to mode (b) only, strict mode keeps raw comparison, each is pinned with its reason in `tests/rpc_lockstep_reviewed_policy.json`, and each carries a sensitivity test showing that a genuine divergence in the compared or projected value still fails.

- **R-M2b-O1 `rpc_session.arc_rpc_test_quota`:** the requested quota is the independent variable of mode (b), a test configuration value and not guest state. Excluded in mode (b).
- **R-M2b-O2 `memory.active_kind`:** its value carries no meaning while `memory.active_valid` is false. In mode (b) it is compared only while `active_valid` is true; `active_valid` itself stays compared.
- **R-M2b-O3 `arm.inscount`:** in the RPC engine it is only incremented at batch return and reset at start (`engine/rpcemu/src/arm.c:279,2007`); nothing reads it, so it cannot affect the guest, and its value at a point depends on batch structure. Mode (b) compares a defined projection of it (stated in the harness note) instead of the raw per-point value.

Also ruled: the two standalone component goldens (CPU and CMOS executables) have no core sink surface and are recorded as component-only coverage; Windows runtime and W4 timeouts are taken from the PR's CI durations by W4's rule (at least twice the worst observed, whole minutes) in a follow-up run if CI shows them short.

## Fable's rulings after the first CI run (S67)

CI at `6dd297f` (PR #506) was green except `windows`, where `rpc-lockstep-inventory` failed with "inventory metadata changed without independent review". Fable traced it: `tests/rpc_lockstep_inventory.py` reads the schema and policy with the platform default encoding; run B added non-ASCII text to the schema, so on Windows (cp1252) the reviewed-inventory hash differs, while under UTF-8 it matches. The run also showed that CI never builds the lockstep class, so none of M2b's emitter tests ran in CI.

- **R-M2b-C1 Encoding.** Every lockstep tool and test reads and writes its text files (schema, policy, inventory, expectations, streams' text metadata) as UTF-8 explicitly; a regression test runs the inventory and schema checks under a simulated cp1252 default.
- **R-M2b-C2 Host CI.** The `host` CI job (Linux) builds the lockstep class and runs M2b's CI subset: the directed corpus, the CI seed subset (AC-8), full-audit on directed ROMs at the smallest RAM configuration (scope item), the emitter, layout, field, codegen, API, driver and consistency tests and the emitter mutants, with timeouts by W4's rule set from that job's measured durations (worst at most 60 % of the timeout, AC-15). The 75 golden-density checks and the local-full items remain local evidence in the report.
- **R-M2b-C3 Windows.** Lockstep execution in Windows CI, and AC-15's Windows half, belong to M2c ("cross-platform CI", non-goals). M2b keeps the MinGW compile, layout and export proofs; the existing M2a lockstep tests keep running on Windows.

## Fable's adjudication of Opus QA (S67)

Opus QA (effort 15, `b346d29`): **FAIL** on H-1. Adjudication:

- **H-1 (defect, validated):** the emitter's per-owner state cache is refreshed only when a function inside the owning translation unit marks it dirty, but other units write IOMD registers directly (mouse position in `keyboard.c`, video cursor in `vidc20.c`, sound status in `rpc_audio.c`, interrupt bits from the FDC, IDE and SuperIO), so the stream can record stale values (QA: `iomd.iomd.mousex` 0 against 12 at key (1,2,7389,0) on `input-0`, cache on against off). **Ruling R-M2b-Q1:** every write that changes emitted state marks its owner dirty wherever it occurs; a committed check runs representative items (including the input, video, audio, floppy, hard-disc and SuperIO paths) with the cache enabled and disabled and requires identical streams; a mutant that removes one cross-unit dirty mark must be killed.
- **M-1, M-2, M-3 (test gaps, validated):** add tests that kill QA's eight surviving v1 comparator mutants (unmatched exit, event outside an open access, missing page-boundary marker, rerun matching-prefix check, compact-hash recomputation of excluded fields, fatal text hash, the two span checks), exercise optional P3 records in modes (c) and (d), and exercise the NULL-batch projection with a directed item.
- **L-1:** add a directed outside-attempt (phase-4) event if it is reachable at this head; otherwise record why and leave it to M2c's G-13 lifecycle ROMs.
- **L-2:** set `rpc-lockstep-emitter-reach` by W4's rule from its CI duration (259 s → at least 540 s).
- **L-3 (clarification of R-M2b-C3):** "MinGW compile, layout and export proofs" are Sol's local evidence; Windows CI builds no lockstep core until M2c.
- **L-5 (defect, validated):** the runner's `baseline` density and `baseline` role must not share a flag; the result may not depend on argument order.
- L-4 needs no action. The Mac-only `i8042` inventory failure is pre-existing; R3c-3b's mapping on `main` covers it once merged.

M2b also merges `main` (R3c-3b's lockstep schema and observer changes): conflicts in `engine/rpc/rpc_observers.c`, `engine/rpcemu/manifest.json` and `tests/rpc_lockstep_schema.json` are resolved keeping both sides, and R3c-3b's new observer state is emitted and classified as M2a requires.
