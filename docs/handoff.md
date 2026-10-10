# ArcDroid Handoff

**Stage:** IMPLEMENTATION — roadmap item 6, RiscPC **R2c** (IDE and a scalable hard disc), run B (implementation of items 9–12)
**Owner:** Sol (run B), supervised by Fable
**Next:** supervise Sol R2c run B4 (the recovery run) to completion, then Fable reviews it and runs AC-6, AC-8 and AC-9 privately, before Opus QA.
- **Running:** codex PID 74871, started 2026-10-10 00:05 CDT, resuming Sol thread `01a120ab-7bf4-7843-a879-6fab6d9b3736`.
  - Clone: `.work/s60/sol2c/clone` on `slice/r2c-ide` at `7e55526` (run A plus `main` through `ff2b5f0`; draft PR #337).
  - Prompt `.work/s60/sol2c/promptB4.md`; stream `streamB4.jsonl`; final message `finalB4.md`; report `SOL-REPORT-R2C-B.md` in the clone.
  - Watchdog: detached `wait-run.sh`, PID 75829, writing its one line to `.work/s60/watch-solB4.txt`.
- **Why a recovery run:** the Mac rebooted at about 12:30 CDT on 2026-10-09 while S59's Sol run B3 was in its final verification. The boot wiped `/private/tmp`, taking the uncommitted run B clone and every Fable scratchpad from S55 to S59. Nothing committed was lost. Sol's thread log survived, so run B4 rebuilds its tree from `.evidence/sol-run-b-replay.md` (its own 258 recorded calls) and then re-runs all verification. Earlier results do not count.
- The pristine `rpcemu-0.9.5.tar.gz` was downloaded again from upstream; its SHA-256 matches the pin `4e4624641cd1a83af275bd6da8765d4a0e5e0c83f008f059014cc24c5c33d59e` in `engine/rpcemu/UPSTREAM.md`. Astra's store report was recovered from its thread log.
- **Private work now lives in `.work/s<n>/`** inside this checkout (git-excluded through `.git/info/exclude`), not in `/private/tmp`, so a reboot cannot wipe it (D-149).

**R2c run A outcome (S59).**
- **Sol run A** (fresh thread above, 1,831 s) is committed verbatim as `b4bc633`:
  - `docs/riscpc-hd-store.md`, the item-9 store contract. It has 256 KiB chunks (504 for 126 MiB), a checkpoint armed at 1 MiB of journal, both journals together ≤ 8 MiB, store memory ≤ 4.9 MB, and an incremental ROTATE → MATERIALIZE → MANIFEST → PUBLISH → RETIRE → GC cycle, one step per `retro_run`. The RPC formatter uses five zones with 8 KiB per map bit, and the A3020 output is unchanged.
  - QA N1 in `tests/rpc_fdc_test.c`. Its mutant (an unconditional IRQ on cancel) is killed.
- **Astra** (thread `01a120c8-fcb0-7523-981d-5fafe50f01d7`) returned ACCEPT WITH CHANGES:
  - HIGH: population could drop acknowledged writes that had not yet been checkpointed;
  - HIGH: a POPULATION header could not explain a leftover source journal after a kill;
  - MEDIUM: multi-megabyte work in one tick;
  - MEDIUM: the prepared-disc key was undefined.
- **Fable accepted the contract with D-145 (six binding amendments):** the four Astra changes; A1, which drops the per-command record readback (the A3020 store's proven pattern); and A2, which keeps full SHA-256 per cache miss but allows faster hashing.
- **Run B so far:**
  - **First stop (correct):** the prompt wrongly asked for a tenth scheduler event. Ruling: activate the existing reserved IDE slot; the count stays at nine.
  - **Second stop (correct, with an executable probe):** the VFS `readdir` cannot report errors, so listing-based GC could exceed the disc bound after a kill. **Ruling R2c-1 (D-147):** RETIRE removes garbage before removal; debris is found by fixed paths; listing is advisory only.
  - **Run B3** then reached its final verification, and was lost in the reboot (see Next). Run B4 is the recovery.
- **Contract honesty, accepted:**
  - the frontend VFS gives no power-loss durability and no bounded wall-clock latency;
  - "no stall depends on the host" means guest-visible timing is virtual and identical however slow the host is;
  - AC-9 measures whether host I/O fits the frame budget.

**R2b outcome (merged S58).** PR #331 merged as `f3424fa` (head `a8dc85c`); D-139, D-143, D-144.
- **What it adds:**
  - an instance-based two-drive model (`engine/rpc/rpc_floppy.*`) on the unchanged media core;
  - the full FDC37C665GT controller (`engine/rpcemu/src/fdc.c`, patch `devices-r2b-floppy`);
  - plan v2 `media`, `drive1` and `disk_control` keys.
- **Sol runs:**
  - A `23a5c0e`: items 5–8;
  - B `6129e8f`: R2b-1;
  - C `a8dc85c`: QA remediation and R2b-2.
- **Rulings:**
  - **R2b-1 (D-143):** raw FORMAT accepts a permutation of the track's ID set, because RISC OS ADFS formats with a track skew.
  - **R2b-2 (D-144):** Recalibrate issues at most 79 pulses. A present drive, even with no disc, reaches TRK0; only an absent drive (units 2 and 3) gets EC.
- **Opus QA (effort 15):**
  - FAIL: MEDIUM-1, a media change killed an in-flight seek; LOW-2, the IRQ dropped on cancel; 19 surviving mutants.
  - Re-check after run C: **PASS WITH LOWS**. P1 and P2 fixed, the R2b-2 boundary exact, every former survivor killed, Q19/Q33/Q04/Q30 equivalent.
- **CI:** green on all five jobs at `a8dc85c`, including native Windows RetroArch.
- **Fable, private:**
  - **AC-8 R2b PASS** at `6129e8f` and again at `a8dc85c`, on 3.50 ARM610, 3.60 ARM7500 and 3.71 SA-110 (authored distinct blank 800K raw discs, Shadow writes): guest `*Format` of both drives (160 tracks), `*Save`/`*CDir`/`*Cat` per drive, a disk-control swap of drive 0 to a third disc plus format, a swap back with both drives intact, and staged images byte-checked per disc.
  - **JFD store persistence PASS** (3.71 SA-110, title 146 with the tab *Off*): `*Save` in session 1, still listed in a fresh process.
  - **AC-6 44-title A/B PASS** (`main` against `6129e8f`, both package states): 42 of 44 identical, with only 105 and 205 differing, both known variance. Run C changed only the FDC.
  - **Two-floppy diagnostics:** no anomaly reproduced (helper absent, or accessed after drive 0). DOR/TC numeric traces were not captured, because there was nothing to diagnose. The catalogue-before-install case is replayed in R2c.

**Carried from R2b:**
- **To R3 (LOW-3):** byte-identical media in both drives share one staged file (Shadow staging is keyed by source SHA-1; Original mode uses the source file), so a write through one drive changes the other medium with no disc-change signal. Stage per drive or slot, or refuse identical sources, before drive 1 is reachable outside the plan core.
- **To R3 (LOW-4):** on RPC, raw store failures latch write protect with no notice, and raw refusals never reach `arc_adf_log_refusal`. The player needs a notice before the RPC path ships.
- **N1:** done in R2c run A.
- **Harness lesson for private legs (`s58/ac8/leg.py`, `legfull.sh`):** the runner VFS refuses writes outside `--save`, so use `--disk-writes shadow` or keep media inside the save directory, and give every blank disc distinct bytes.

- **Parallel workstream (D-141, D-142), item 6b translator evaluation:** the plan is in `docs/riscpc-translator.md`.
  - **Phases:**
    - Phase 0 now, evidence only;
    - Phase 1 after R4 and item 5: profile-led exact interpreter optimisations, then a minimal block/exit foundation;
    - Phase 2: a guarded arm64 fast path;
    - Phase 3: a rate change, on a Founder decision.
  - **S58 Astra challenge done:** ACCEPT WITH CHANGES, all nine incorporated (D-142). Thread `01a11e3a-d351-75a1-8424-b370d8b9f354`.
  - **S59 Phase-0 progress** (details in `docs/riscpc-translator.md`, *Phase 0 evidence*):
    - **Executable-memory probe: PASS** inside RetroArch on the Odin3. Dual RW/RX mapping is allowed, and code replacement, the helper ABI, the `longjmp` fault unwind with x18 intact, and teardown all work.
    - **First Odin3 profile done.** Fetch translation is a third of cache-hot cost. Warm load/store code reaches p95 1.83 T at 100 MIPS. The timing model dominates memory-bound code. Devices cost about 0.04 T.
    - **Invariants accepted as the Phase-0 basis (D-146)**, in `docs/riscpc-translator-invariants.md`, after an Astra check: ACCEPT WITH CHANGES, three HIGH and four MEDIUM, all incorporated. Rulings are Q-1 to Q-11.
  - **Q-3 oracle fix (D-146), queued after R2c merges and before R3.** The R2a fetch memo is keyed by virtual line only and can outlive a change of eligibility after a translation-cache re-walk. The work order is a small corrective slice: a targeted reproduction plus the smallest fix, with no blanket golden regeneration, and its own Fable review and QA.
  - **Windows profile done (S60, D-148).** It is in `docs/riscpc-translator.md`, *Interpreter profile on Windows*. The 7800X3D costs 0–23 % less per instruction than the Odin3; warm load/store code still misses real time (p95 1.60 T), fetch translation is again about a third of cache-hot cost, and traces are byte-identical to the Odin3. Private files are in `.work/s60/prof/` (`winsample.py`, `winrun.py`, `symbolise.py`, `winout/`); the copies on the Windows box are in `C:\Users\sinmu\arcdroid-s60prof\`.
  - **Windows executable-memory probe: PASS (S60),** after the Founder switched Smart App Control off on the box (its first attempt had been blocked). Dual RW/RX views work. A CRT `longjmp` through an emitted frame needs unwind data registered with `RtlAddFunctionTable`; without it the process dies with `0xC00000FF`. Details are in `docs/riscpc-translator.md`; the source is `.work/s60/xprobe/`. **The Windows box no longer has Smart App Control on,** so it no longer represents players who do.
  - **Remaining Phase-0 steps for Fable:**
    - representative gameplay (after R2c);
    - block-shape and churn instrumentation;
    - the Q-1 design objective, put to the Founder at the end of Phase 0.
  - **Founder questions:** Q1 (design objective, with Q2) at the end of Phase 0; Q3 (fork copying) before any copied helper; Q4 at qualification. None is needed now.
- **R2a merged** as `6ee6460` (PR #317, head `880e528`; D-139, D-140). Outcome below.

**R2a outcome (merged S57).** Sol runs A–G:
- A: the timing contract `docs/riscpc-timing.md`;
- B: the memory-system timing model;
- C: RiscPC CMOS records and generated defaults;
- D: Delete held to 45 s, 8-frame generation, optimisation;
- E, F, G: QA remediation and Windows CI.

Rulings, recorded in D-140:
- G7, dirty ROM castouts;
- R-D1, the Delete schedule;
- R-H, the Odin3 host-cost gate relative to `main`;
- ROMCR reset;
- a fixed generation epoch, with procedure revision 3.

**R2a evidence:**
- **Calibrations:** 4,674 independent exact timing calibrations. Every QA mutant is killed; V6 is accepted as equivalent.
- **Opus QA (effort 15):** FAIL (HIGH-1, CMOS torn candidate) → FAIL (MEDIUM-5, test-only) → **PASS WITH LOWS** at `9dc72e8`. Run G after that was test-only: Windows test exports and paths, plus geometry probes.
- **CI:** green on all five jobs at `880e528`, including windows-retroarch. The windows job was re-run once and passed.
- **Fable, private:**
  - R1-1 is resolved: generated defaults are full factory resets on all seven pairs at production rates.
  - The Odin3 3.71 default is byte-identical to macOS; first-launch generation takes about 19–20 s on the Odin3.
  - `*Configure` survives a fresh process.
  - The 44-title Archimedes A/B in both package states matches `main` apart from known variance.
  - Odin3: hot workload p95 0.99–1.00 T against `main` 1.10 T; AC-8 0.169 T.

**Carried from R2a:**
- `rpc-cpu-trace` produced empty output once on Windows CI at `5648bd4`, then passed twice on identical code. An intermittent fault; investigate if it recurs.
- QA LOW-A: a transient directory-listing failure in the CMOS reconcile path latches refused for that session. It is conservative and clears on reload.
- R3 must show a notice during first-launch default generation (about 20 s on the Odin3).
- After the absent-IDE startup dialog, the desktop takes about 60 s to appear. This is identical on R1 and goes away once R2c's IDE exists.
- AC-10, the end-of-R2 gameplay speed gate, is still owed after R2c.

**R2 work order** (below): R2a and R2b done; **R2c next**.

Slice order (D-133): R1–R4 → item 5 release baseline → item 6b faster CPU translator → item 8 save states. Carried: the Arculator VIDC-register reset follow-up (below); follow-up Lows; Slice 23 residuals; physical-stick feel on the Odin3 (Founder check).

**Item 5 grounding (S55, paused at a safe point: nothing committed, no branch).** `main` `c725bd6`: CI builds Android arm64 only, which is the whole Android target (D-135: no armeabi-v7a); the `.info` is copied into the build tree (`configure_file`) and installed by `install(FILES …)` but no CI artifact bundles core + `.info` + `COPYING` + `THIRD_PARTY_NOTICES.md`; the version lives in three places (`project(… VERSION 0.1.0)`, `ARCDROID_VERSION` fallback in `libretro/arcdroid_libretro.c:28`, `display_version` in `arcdroid_libretro.info`). (armeabi-v7a was found to build unmodified, but is out of scope by D-135.) Android devices: the Odin3 (`arm64-v8a` only) and a second Snapdragon 8 Gen 2 device (Founder S55). **Windows signing risk (S60):** Smart App Control, on by default for many Windows 11 installs, blocked one unsigned ArcDroid-built probe executable on the Founder's box while allowing the unsigned runner and plan core. Item 5 must decide whether an unsigned `arcdroid_libretro.dll` is acceptable, or whether it needs signing or a documented workaround. No shipping source uses `__FILE__`/`__DATE__`/`__TIME__` or a runtime `assert` (the Slice 8 note on embedded paths is unverified — check with a two-path build comparison). The GPL corresponding-source mechanism for distributed binaries (D-016) stays tied to publication (D-100). Scratch: S55 scratchpad `s55/` (`a/`, `second-checkout-b/` clones, `build-android.sh`).

## Waiting on the Founder (protocol §18, D-124)
- None.

**Answered S60:** SSH to the Founder's Windows box (`sinmu@192.168.1.66`) is allowed for the item 6b Windows profile (D-148). It may also serve R2c AC-9's Windows native-RetroArch leg.

**S60 (Claude Opus 5.5 effort 15, window `arcdroid-db`; started by the Founder by hand after the reboot).**
- Found the loss described under Next, re-created the clone and evidence, and dispatched Sol run B4.
- The S59 window crashed before closing, so its name was never recorded. Its work is recorded in the R2c run A outcome above and in D-145 to D-147.
- Fable private scratchpads from S55 to S59, which earlier entries below cite, no longer exist. Their results are recorded in these entries and in the decision log; only the harness scripts are gone. The Odin3 tooling under `/data/local/tmp/` is unaffected.

## Answered S55 (D-131…D-135; full option texts in git history, `docs/handoff.md` at `7a32f93`)
- RiscPC go/no-go → **GO** at a 100 MIPS Odin3 StrongARM budget (D-131); boot files are never shipped or fetched — user-supplied in RetroArch's system folder (D-134).
- Write-protect tab → **per-game option** (D-132) → Slice 30, merged S55.
- Faster CPU translator for RiscPC joins the roadmap as item 6b (D-133). Android is arm64-v8a only (D-135).

**Slice 30 work order:** merged; in git history (`docs/handoff.md` at `7a32f93`).

**S58 (Claude Opus 5.5 effort 15, window `arcdroid-99`; baton by relay from `arcdroid-ba`).**
- **Work done:**
  - took over Sol R2b run A (PID 53166) from S57 and supervised it to completion;
  - **R2b reviewed, privately verified, QA'd and merged** (outcome above; D-143, D-144);
  - the item 6b Phase-0 Astra challenge: ACCEPT WITH CHANGES, all nine incorporated (D-142); Astra thread `01a11e3a-d351-75a1-8424-b370d8b9f354`.
- **Founder (S58):** after an auto-mode classifier block on a self-merge, the Founder said in the window, "Yes yes please merge." Governance and implementation merges continued.
- **Nothing left running.** No Sol, Astra, QA, runner or watchdog process started by S58 is running.
- **Private evidence:** S58 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/96a9a3c8-cfc4-45af-b638-d566632c35c9/scratchpad/s58/`, indexed by `LEDGER.md`:
  - `astra/`: brief, stream and report for the 6b challenge;
  - `r2b/`: Fable's slice build;
  - `probe/`: a `/* PROBE */` format-tuple logger, never a Sol source;
  - `ac8/`: `leg.py`, `legfull.sh`, the authored blank discs `media/u*.adf`, and the legs `legA2`, `leg350`, `leg360`, `c350`, `c360`, `c371`, `jfd1`, `jfd2`, `cfg350`, `cfg360` (their saves have Floppies 2 persisted);
  - `ab/`: `legs58.sh`, `ab58.py`, `cmp58.py` and the legs;
  - `main/`: the baseline clone;
  - `qa/`: the QA clone, probes, mutants and logs.
- **Sol files** are in the S57 scratchpad `s57/sol2b/`: prompts A, B, C and C2, streams, final messages, and the clone with `SOL-REPORT-R2B-{A,B,C}.md`.

**S57 (Claude Opus 5.5 effort 15, window `arcdroid-ba`; baton by relay from `arcdroid-25`; closed on the Founder's instruction because context was nearly full).**
- **Work done:**
  - R2 designed (D-139);
  - **R2a designed, implemented, QA'd and merged** as `6ee6460` (D-140);
  - Founder project isolation applied (`arcdroid-opus-qa`, isolation rule, local deny list);
  - 6b evaluation done (D-141, `docs/riscpc-translator.md`);
  - R2b run A dispatched.
- **Left running:** Sol R2b run A, PID 53166, recorded above. The S57 watchdog on it ends with this window. Nothing else S57 started is running: the Opus QA and research agents have finished, and on the Odin3 neither the runner nor RetroArch is running.
- **Odin3 state (S57):** `/data/local/tmp/arcdroid-r2a/` and `/data/local/tmp/arcdroid-r2d/` hold the run-B and run-D arm64 runners, plan cores and workloads (`gates.sh`, `compare.sh`, `defgen.sh`). The installed RetroArch cores are unchanged since S56.
- **Earlier S57 work:** R2 designed (D-139). R1-1 diagnosed to its cause (missing CPU memory-transfer cost; see the R2 work order). **Astra** thread `01a11795-811f-7ff2-9065-4200a62d4a1a`:
- stage 1 gave an independent design that agreed on all main lines: IDE required; an RPC-side drive model over the engine-neutral core; a memory-cost fix rather than a keyboard change; a store redesign;
- stage 2 returned PROCEED WITH CHANGES. All seven changes were incorporated:
  1. a worked timing contract before code, with timing-only cache tags preferred and guest progress measured;
  2. correct TC and short-write semantics;
  3. a required-mask + stability + checksum completion oracle in place of the 216/240 threshold;
  4. the CMOS key uses the exact firmware SHA-256, with normative default-cache invalidation;
  5. the store state machine and concrete bounds frozen before code;
  6. the plan route arriving incrementally per PR, with the catalogue-before-install replay moved to R2c;
  7. AC reconciliation (Arculator vs RPC goldens; the 44-title A/B per PR gate; a frozen ATA matrix; Odin3 limits; the end-of-R2 speed gate).

Private evidence: S57 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/bd202a6a-899b-41f1-bbdd-15986eaffba2/scratchpad/s57/` (`LEDGER.md` index):
- `research-*.md` — research notes;
- `r11sweep.log` and `r11/` — the rate/model sweep;
- `probe/` — the scratch `/* PROBE */` build, never a Sol source; runs in `r11p/`, `r11q/` and `r11cf/` (the counterfactual);
- `dis.sh` — the private disassembly helper;
- `astra/` — briefs and reports;
- `workorder-r2.md` — drafts.

**S56 (Claude Opus 5.5 effort 15, window `arcdroid-25`; baton by relay from `arcdroid-7e`).** **RiscPC R1 designed, implemented, QA'd and merged as `7244f9c`** (outcome above; D-137, D-138).

**Machine and device notes:**
- Machine load reached about 130 from other projects, so Sol's full suites took 5 h or more.
- Watchdogs ran detached via `detach.py` with a Monitor relay, because tool commands are capped at 2 h.
- Odin3 tooling in `/data/local/tmp/arcdroid-r1/`:
  - `ra.sh` drives RetroArch: start, screenshot, gamepad button, quit;
  - `flick.sh` scrolls menus;
  - `devstate.sh` reads thermal and frequency state;
  - `ac5.sh` runs the AC-5 plan set.
- The plan core is installed in RetroArch through `RetroArch/downloads`. The gamepad is `/dev/input/event8` (A = Return in RISC OS). Private files: seed at `BIOS/arcdroid/rpc-r1/`, plan at `RetroArch/rpc-r1/`.

**Design (D-137):** Astra thread `01a11216-5d14-7d60-be26-243b187b2015` stage 1 (independent design: dormant integrated backend, smaller allowlist, provisional 20/35/35 rates, Odin3 screening in R1) and stage 2 PROCEED WITH CHANGES — seven changes, all incorporated: (1) remove `disc*.h`/`hostfs.h`/`network.h` includes with their loaders/interception, plus a closure check; (2) clipping is presentation-only, separated from DMA/`VIDCUR`/flyback progression, bound kept at the trial's 2048×2048; (3) ROM 2/4/8 MiB with defined aperture mirroring, 6 MiB deferred; (4) CMOS seed/factory-capture plan modes, reset keeps volatile CMOS, test-core plan file usable from native RetroArch, architectural aborts keep the session running; (5) exact RPCEmu audio production/drain contract with generated/accepted/discarded counts, Arculator audio untouched, defined post-fault output; (6) AC-8 as an absolute budget on a predeclared authored workload (the trial's sustained workloads were private game recipes, so no reproducible relative baseline exists); (7) RiscPC models in a private backend registry, shipping machine registry unchanged. No Founder decision needed. Private evidence: S56 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/e343526c-1407-4062-a8fc-3374fe5426a8/scratchpad/s56/` Its contents:
- `LEDGER.md`, the index;
- `astra/`: briefs, reports, streams, and a clone with `.evidence/`;
- `workorder-r1.md` drafts;
- `sol/`: prompts A–H, reports `SOL-REPORT-R1-*.md`, streams, the clone, and the tarball in `upstream/`;
- `fable/`:
  - the `r1` build checkout;
  - `ac5/` for AC-5;
  - `ac7/` for AC-7: plans, seeds, smoke images, long-boot frames;
  - `ac8*` for AC-8, including the CSVs and device state;
  - `odin/` for the RetroArch screens;
  - `linux/`, the Docker GCC reproduction;
  - `prof-sample.txt`;
- `ab/`, the AC-3 A/B legs and comparisons;
- `qa/`, the Opus QA clone, reproductions and mutants.

No process from this session is left running.

**S55 outcome (Claude Opus 5.5 effort 15, window `arcdroid-7e`; baton by relay from `arcdroid-df`). Slice 30 merged as `ff048e4`** (PR #307, head `018a05f`; D-132, D-136): per-game core option `arcdroid_jfd_write_protect` ("Write-protect Tab": *As imaged* default / *Off*). One rule `arc_jfd_write_authorized` (Off OR extended RW/not-WP) with the session policy passed explicitly to every header-authorization site (store, `core/jfd_layout.c` guarded admission, D-102 routing, file-data class, refresh notices, temporary decodes, disk control, recovery); header, identities and store keys unchanged; under *As imaged* a store of a header-unauthorized image is hidden (read-only detection, one notice), never deleted; boot observation revision 4 stamps the setting. Release note `docs/release-notes-slice30.md`. **Sol** (GPT-6.1 Sol, thread `01a1103a-9d72-73d3-9ad3-5d7360357a4e`): run 1 `64a37a5` implementation; runs 2–4 test/doc-only (`afcfb2e` Windows test-hook exports, `5751de4` Windows reset descriptor phase + no NULL `memcpy`, `018a05f` QA test gaps TG-1…TG-4 + `docs/adffs.md` wording + reset-scoped Windows relaxation). **Astra** thread `01a11032-bd6d-7600-ab82-00fef64d1785` stages 1–2 (PROCEED WITH CHANGES, all incorporated). **Fable ruling:** As imaged log lines stay byte-identical; extended `JFD writes:` fields under Off only. **Opus QA** (effort 15, ArcDroid QA agent): PASS WITH LOWS (no defects; mutants M1–M4, M6–M8 killed; M5, M9–M12 survived) → run 4 → re-check PASS WITH LOWS (all five killed; no production change). **CI** green on all five jobs (PR event) at `018a05f`. **AC-12 PASS** (private, RISC OS 3.11; fresh data directories per run; legs vs `main` code): default no-package 218 zips — 214 frame-identical, differences 105/133/164/205 all run-to-run variance (133 newly shown variable: two `main` runs differ at t060/t090); default with package — 215 identical, 105/133/164 variance; *Off* with package — 101/111/136/146 `native-writable reason=tab-off`, utility reconfigures (CMOS writes 513–515), t150 frames identical to S54's verified title screens; 103 at *Off* stops at its own "MUST be write protected" (identical to S54); *Off* forced on the S54 104-title cohort (no package) — frame differences exactly 019/080/101/103/111/136/146/164/191 and 103/104 titles frame-identical to S54's forced-writable leg (164 variance), 0 error changes; legacy-header 010 (chooser) and 081 (both images) unchanged frames under *Off*, admitted writable, no writes, no corruption. **AC-13 Odin3 PASS** (arm64 `64a37a5`, production-identical to the merge; see the S55 device record in git history at `7a32f93`): 146 per-game *Off* → native-writable → title → gameplay; relaunch reloads the store (`generation=9 records=4`); 103 at default unchanged; 0 `[ERROR]`, peak PSS 175–189 MB. **Item 5** grounded and paused (above). Private evidence: S55 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/23f7c4c9-86e2-43f9-878a-2538fbe3ca51/scratchpad/s55/` (`astra/` briefs/reports; `sol30/` prompts 1–4, reports, streams, CI logs; `ac30/` `legs.sh`, `ab55.py`, `cmp55.py`, `vs54.py`, legs `base-nopkg`/`s30-nopkg`/`base-pkg`/`s30-pkg`/`off-nopkg`/`off-pkg`/`off-ten`/`base-133r`; `qa30/` QA clone + `mutate.py`; `a/` = `main` code build; Odin3 screenshots `d146*`, `d103a*`).

**Unit P state (S52).** Branch `probe/riscpc-trial` (never merged), head `5ff5481` after Sol runs 1–14 (`29fdf4f`…`5ff5481`); private report `scratchpad/solP/clone/SOL-REPORT-P.md` (+ archived per-run reports under `probe/riscpc/run/`), runbook `probe/riscpc/RUNBOOK.md`. Fable rulings, all general: R3-1 factory-reset CMOS; R4-1 project-authored multisync MDF + boot step; R5-1 RISC OS Open HardDisc4 + PlingSystem resources (private); R6-1/R7-1 keep ROM modules, run none of RISC OS Open's boot scripts — ArcDroid sets the standard boot variables/paths; R8-1 per-drive media ownership; R9-1/R10-1 single floppy drive, boot from the standard IDE disc (residual risk: unexplained two-floppy FileCore interaction after cataloguing drive 1); R11-1 boot the Resources applications via Filer_Boot; D-127 line-accurate scanout. Odin3 device evidence: `scratchpad/device-p12/` (RetroArch runs `f031-100`, `f074-35`, headless `h031-100`, `h110-100`, thermal `th1–3`); device layout `BIOS/arcdroid/firmware/ROM371`, `BIOS/arcdroid/rpcprobe/trial-{a,b,d}`, content `RetroArch/rpcprobe-content/`, probe core installed as `arcdroid_rpcprobe_libretro_android.so`, runner `/data/local/tmp/s52.sh`, headless kit `/data/local/tmp/rpcprobe-p14/`.

## R2 work order — RiscPC media, IDE, CMOS and memory timing (S57; D-139)

**Objective.** Give the dormant RiscPC backend (D-137) what a RiscPC-only game needs before R3 can launch it:
- **CMOS** that persists per game, with defaults generated by the user's own firmware.
- **Two floppy drives** backed by ArcDroid's existing media rules (JFD, ADF/ADL, helper), through a complete 82077-class controller.
- **A persisted IDE hard disc** large enough for the standard boot resources plus a game install.
- **The memory-timing correction** that residual R1-1 exposed.

Governing: `docs/riscpc.md` §§4.4, 4.6, 4.9, 4.11, 6; D-063, D-075, D-076, D-090, D-097, D-106, D-107, D-120, D-121, D-131, D-134, D-136, D-137, D-138, D-139; PRD FR-8, FR-10, FR-22, FR-62–FR-68; protocol §10.

**Player-visible outcome: none.** R2 stays dormant. Every new path is reached only through the plan core (`arcdroid_rpcplan_libretro`) and `tools/rpc_runner.c`. Shipping routes, options, machine names and `.info` are unchanged, and Archimedes behaviour is byte-for-byte unchanged.

**Delivery: three sequential PRs on this one design.** Each PR gets its own Sol runs, Fable review and Opus QA, and each starts from the merged previous one. R2 is complete when R2c merges and the end-of-R2 gate (AC-10) passes.
- **R2a — memory-system timing and CMOS.** Branch `slice/r2a-timing-cmos`.
- **R2b — floppy drives and full controller.** Branch `slice/r2b-floppy`.
- **R2c — IDE and a scalable hard disc.** Branch `slice/r2c-ide`.

### Verified state (S57, `main` `a3443c9`)

**RPC backend today**
- RPC media and CMOS backend ops are stubs (`engine/rpc/rpc_session.c` ~409–452; `.cmos` returns zeros), and `arc_rpc_floppy_request` always returns `NO_MEDIA`.
- FDC commands are as in QA L5 (*R1 outcome* above).
- Two R1 FDC hazards would matter once media works:
  - `fdc_request()` rebuilds CHRN from the original parameters even after `fdc.sector++`;
  - data commands take the physical side from the requested H (`engine/rpcemu/src/fdc.c:146-158, 242-260, 559-601`).
- IDE is absent (D-138(1)), and `ARC_RPC_EVENT_IDE` is reserved.
- The scheduler has nine events; `ArcRpcTestSnapshot.events[9]` and the runner hard-code that count.
- CMOS lives in session memory only. The factory-capture success test is "≥ 1 guest write", which a failed reset passes.
- The runner's VFS is read-only.

**Arculator media side**
- The engine-neutral media policy lives in `core/` and `libretro/`.
- Arculator's drive side (`disc.c`, `disc_adf.c`, `disc_jfd.c`) is tied to Arculator's controller table, timer and IOC:
  - JFD is a drive-0 singleton;
  - raw transfers share global state;
  - raw writes ignore requested size and the deleted flag.

**Hard-disc store**
- `libretro/install_store.c` holds the whole 15.75 MiB A3020 image in memory, hashes it at open, and reads it all back at compaction.
- It compacts only at open, and it refuses in-session writes once the journal reaches 8 MiB.

**CMOS records**
- `ARCCMOS2` records have 8-bit machine/OS tags and no payload checksum.
- There is no RiscPC machine and no RISC OS 3.5+ class.

**What the titles need (trial)**
- 031, 074 and 084 install onto the hard disc and run from it; 074 cannot run from floppy at all.
- The trial needed a 126 MiB disc. Its 16 MiB and 63 MiB discs left about 2 MiB free after the standard boot resources.

**R1-1 diagnosis (S57; private firmware; a scratch `/* PROBE */` build, never a Sol source)**
- **Where it fails:**
  - the factory reset fails at 100 MIPS on all seven pairs (3.71 SA-110/ARM710/ARM7500, 3.70 SA-110, 3.60 ARM710/ARM7500, 3.50 ARM610);
  - it also fails on SA-110 at 40, 60 and 80 MIPS;
  - it succeeds at 35 MIPS and below.
- **Why:** after the keyboard reset command and its ACK, the firmware:
  - clears memory with `TEQ; STMNE r10!,{8 regs}; BNE` at ROM offset 0x1541c — about 10.93 M instructions with no I/O;
  - then reads CMOS over IIC for about 224 ms, paced by IOMD timers;
  - then decides.

  The decision window W(rate) = 224 ms + 10.93 M / rate fits 20, 40, 60 and 100 MIPS to within 1 ms.
- **Keyboard timing is not the fault:** BAT arrives 500 ms after the ACK, within the HT82K629B datasheet's 300–500 ms.
- **Counterfactual:** charging 240 ns per 8-word store in that loop alone gives a full reset at 100 MIPS on SA-110 and ARM710 (554 writes, all 240 payload addresses).
- **Root cause:** CPU memory transfers have no cost.

### R2a — memory-system timing and CMOS

**1. Timing contract first (run A, no product code).** Sol writes `docs/riscpc-timing.md`: a worked timing contract for each model.

- **Bus time** for CPU transfers to DRAM, VRAM, ROM and I/O space:
  - sequential and non-sequential per transferred word;
  - sharing arbitration with the existing video-DMA hold (`arc_rpc_video_bus`).
- **Write buffer:** capacity and entry width, drain at bus timing, and a CPU stall when the buffer is full.
- **Caches:**
  - ARM610/ARM710 have write-through caches and ARM7500 its integrated cache. The SA-110 has a write-back D-cache with an 8-entry write buffer, in the Risc PC's standard bus mode with bursts of at most 4 words.
  - Preferred: timing-only cache tags (presence and replacement, no data). They decide store hits vs bus traffic, read misses and line fills, dirty evictions, and the instruction-cache rule for ROM.
  - The contract must estimate the tags' host cost against the D-131 budget. If Fable rules the tags unaffordable, use a declared bounded approximation instead, and narrow AC-1 to it.
- **SA-110 reset clock:** DCLK runs from MCLK until clock switching is enabled (SA-110 TRM §8.2).
- **Sources:** every constant cites a primary source (CPU datasheets/TRMs, the IOMD functional specification, Risc PC board configuration) and shows its derivation.
- **Calibration workloads:** expected times come from the documentation, never from the implementation. Workloads:
  - ALU, branches, and LDM/STM lengths;
  - streaming and random memory;
  - cache and write-buffer control;
  - ROM and I/O;
  - DMA contention;
  - memory size;
  - one hot instruction-throughput case.
- **Firmware reset is validation only,** never the fitting target. BAT timing is not changed.

Fable accepts the contract, after a short Astra check, before run B implements it. Then:
- **Rates:** D-137 rates are ceilings (SA-110 100 accepted; 20/35/35 provisional). Stalls lower achieved throughput; rates are never lowered, and there is no capture-only mode.
- **Determinism:** the model is integer and deterministic, with no host-pacing input.

**2. CMOS records.** A new versioned RiscPC envelope that is never read as `ARCCMOS2`. Arculator's format and paths are unchanged.

| Part | Content |
|---|---|
| Contents | 240-byte payload (PCF8583 bytes 16..255), content identity, private model id, firmware SHA-256, hardware/layout revision, generation, integrity checksum |
| Key | content × model × **exact firmware SHA-256**. Grouping firmware versions needs separate layout evidence and is not part of R2 |
| Location | `<save>/arcdroid/cmos-riscpc/`, a name no Arculator key can produce |

- **Publication:** verified candidate publication into recoverable slots.
- **When to persist:**
  - eagerly, outside guest execution, at least once per `retro_run` when changed;
  - at unload;
  - reset keeps live CMOS.
- **Refusals:** corrupt, foreign-version and wrong-key records are refused visibly and never overwritten silently.
- **No normalisation:** an interrupted guest checksum update is never repaired. Launch-owned bytes (Boot, drive counts, unplug) belong to R3.

**3. Generated defaults.** A missing record is seeded from the factory default for (firmware SHA-256, model, generation-procedure revision, timing-model revision). Changing the timing model, the reset procedure or the relevant machine configuration bumps a revision, which invalidates the cached default.

- **Generation:**
  - an isolated, media-free session: zero CMOS, an internally held Delete with a fixed schedule, pinned RTC, production rate;
  - it runs incrementally, in bounded slices, through ordinary frontend calls;
  - then the session is destroyed and the requested session starts clean.
- **Completion oracle.** At a fixed capture deadline, all three hold:
  - (a) every address in the layout family's **required mask** has been written. The mask is 240/240 unless a firmware demonstrably never writes an address during its held-Delete reset: identical write masks at 20 and 35 MIPS and at the production rate, over an extended horizon. Today that covers 3.50's 194/195. The masks and their evidence are recorded.
  - (b) no payload write occurred during a stated stability interval before the deadline;
  - (c) the RISC OS checksum (`core/cmos_defaults.c` rule) is valid.
- **Negative tests:** the 14-write case; no Delete; an interrupted reset; a valid checksum with incomplete coverage.
- **Cache:** under `<save>/arcdroid/cmos-riscpc/defaults/`, with a provenance record: firmware digest, model, rate, revisions, reset schedule, RTC, completion evidence, payload digest. Never shipped, logged or uploaded (D-134).
- **On failure:** no default, an explicit error, and no session.

**4. Plan route, part 1.** Plan v2 adds content identity, a writable save directory, and CMOS persistence fields. The runner gets a writable VFS for the save directory only. Version-1 plans still work.

### R2b — floppy drives and full controller (L5)

**5. Drive model (§4.6 as reconciled by D-139).** An ArcDroid-owned, instance-based RPC drive model sitting on the engine-neutral media core.

- **The media core stays authoritative:**
  - `arc_jfd_effective_sector`, preparation, quarantine, identification and omitted file-data;
  - the D-120/D-121 guards, the store, the D-136 policy, and Disk Writes Off/Shadow/Original staging.
- **Arculator's drive side is not modified.** Small shared primitives may be extracted only if Arculator stays byte-identical.
- **State:** each drive has its own motor, rotation, head position, change latch and mounted-image ownership; there is one controller transfer state.
- **Media:** drive 0 takes JFD or raw ADF/ADL; drive 1 takes the raw helper through the same mechanics. Raw prefix limits are kept.
- **Timing — integer virtual deadlines:**
  - rotation: JFD's accepted 200 ms revolution and its descriptor positions; raw layouts from validated geometry;
  - index;
  - byte time per FM/MFM and data rate;
  - head load, and step/settle from SPECIFY.

  Host storage latency never advances guest time. Deadlines are multiplexed behind the existing FDC slot with a documented internal tie order. A new slot is allowed only with a deliberate snapshot/runner format revision.
- **Typed interface:**
  - physical drive, physical cylinder and selected head are kept separate from the requested CHRN;
  - explicit encoding, rate, modifiers and parameters;
  - typed ID, read-byte, write-byte-available, index and terminal-status events;
  - no size sentinels or side channels;
  - session, mount and command generations: every callback validates its generation, and generations are invalidated before media is released.
- **Fix both R1 hazards.**
- **Writes and termination:**
  - A complete sector commits exactly once, after FIFO consumption and validation. This includes a sector whose last byte arrives with TC.
  - JFD: an incomplete sector (early TC, overrun, underrun, reset, eject) is discarded. This is the D-075 store guarantee and a documented fidelity limit; the guest sees the datasheet's result status.
  - Raw ADF/ADL: an early TC completes the sector as the FDC37C665GT datasheet specifies, then commits.
  - N/DTL that differs from the media's sector size follows the existing size-mismatch handling.
  - Previously committed sectors are always kept.
  - Unchanged guest-visible results: veto → data CRC error, store failure → write protect.
  - A synchronous refusal ends the command before DMA.
- **Reset, eject, replacement, failed insertion and teardown:**
  - discard incomplete transfers, lower outstanding DMA, and prevent stale completion;
  - a replacement is validated before ownership is retired;
  - a failed insertion leaves an explicit empty drive.
- **Disk control:** libretro disk control works for RPC sessions in the plan core.

**6. Controller (L5).** The SMSC FDC37C665GT command tables are the authority.
- **Commands:**
  - setup and status: SPECIFY, SENSE INTERRUPT, SENSE DRIVE STATUS, RECALIBRATE, SEEK and RELATIVE SEEK;
  - configuration: CONFIGURE (with effect), VERSION (`0x90`), DUMPREG, LOCK and PERPENDICULAR MODE;
  - data: READ DATA, WRITE DATA, READ DELETED DATA and WRITE DELETED DATA;
  - VERIFY, which checks every selected sector without host transfer;
  - READ ID (FM/MFM);
  - READ TRACK, which transfers sector data in rotational order, not a fabricated flux stream;
  - FORMAT TRACK, with N/SC/GPL/fill and incoming CHRN tuples.
- **Fields and termination:** MT, MFM and SK; EOT, GPL and N/DTL; FIFO and non-DMA modes; implied seek; and the documented result CHRN/status transitions.
- **Invalid by datasheet:** SCAN and reserved encodings stay invalid.
- **Valid command vs representable media operation:**
  - JFD FORMAT and deleted-data writes keep today's refusal results;
  - raw FORMAT succeeds only for an exactly representable layout, with the supplied fill byte, and is never normalised;
  - no format invents deleted marks: media without them read as normal data, with CM/SK semantics.

**7. Two drives.** Both drives are always available; nothing restricts a session to one drive.
- **Synthetic-ROM tests** prove controller and drive isolation, including deliberate DOR/command-unit mismatches.
- **Fable's private R2b reproduction:**
  - authored FileCore volumes with replacement media, under private firmware;
  - helper absent, present but untouched, accessed first, and accessed later;
  - volume identity, geometry and repeated insertion varied independently;
  - bounded numeric traces of DOR selection, unit/HDS, physical/requested CHRN, change latch, index phase, bytes serviced, TC, IRQ/FIQ and results.
- **The original catalogue-before-install case** needs IDE, so it is replayed in R2c.
- **Outcome:** any controller or bridge defect is fixed before R2 completes. Anything else stays an open residual, with evidence and a next diagnostic. Two-drive compatibility is never claimed unproven.

**8. Plan route, part 2.** Media sets, drive-1 media and timed disk-control operations, all through the shipping adapters and stores.

### R2c — IDE and a scalable hard disc

**9. Store state machine first (run A, no product code).** Sol writes `docs/riscpc-hd-store.md`. Fable accepts it, after a short Astra check, before run B implements it. It must define:
- **Holes:** only manifest-declared holes read as zero. A missing referenced chunk is corruption.
- **Chunk verification:** a chunk is verified before its contents are used, including partial-write reconstruction during a checkpoint.
- **Manifest binding:** each manifest is bound to an exact journal generation and committed-sequence boundary. Writes acknowledged during a checkpoint are retained and replayed.
- **Garbage collection:** follows references from every retained recovery root, including journals. Unchanged chunks referenced by the new manifest are never deleted.
- **Failure behaviour:** when a checkpoint cannot finish before the journal cap, on full storage, and on publication failure. The guest sees an ATA error, the disc latches read-only and a visible notice appears; no stall depends on the host.
- **Fault-injection outcomes** for every boundary.
- **Concrete bounds for the 126 MiB geometry:**

  | Item | Bound |
  |---|---|
  | Chunk size | ≤ 1 MiB |
  | Resident cache | ≤ 8 MiB |
  | Journal | ≤ 8 MiB, checkpoint triggered at ≤ 4 MiB |
  | Files | ≤ 2 manifests + ≤ 2 journals + chunk files |
  | Peak temporary disk | one chunk plus one manifest per step |
  | Checkpoint work per `retro_run` | ≤ 1 chunk step |
  | Disc size | 126 MiB |

**10. IDE controller.** Pristine import of RPCEmu 0.9.5 `src/ide.c` (manifest, notices, import checker), then marked patches:
- **Device:** single ATA master only — no ATAPI, CD, slave or file-backed loading.
- **Wiring:** `ArcIdeBackend`; `ARC_RPC_EVENT_IDE` timing in virtual time; IRQ on IOMD IRQB bit 1, honouring nIEN.
- **Write semantics:** the Arculator rewrite's transaction latch. A whole multi-sector write commits in one backend write. A failed commit latches read-only and returns an ATA error.
- **Frozen command matrix:**
  - Implemented: RECALIBRATE (1x), READ SECTORS (20/21), WRITE SECTORS (30/31), READ VERIFY (40/41), SEEK (7x), INITIALIZE DEVICE PARAMETERS (91), IDENTIFY DEVICE (EC), power commands E0–E3/E5 (correct status, no effect).
  - Everything else: ABRT.
  - IDENTIFY: 256 cylinders, 16 heads, 63 sectors, current-geometry words, LBA capacity words, ArcDroid-authored model/serial strings, PIO only.
  - Adding a command needs RISC OS evidence and Fable's approval.
- **Bounds and cancellation:** commands are bounded to 256 sectors. CHS/LBA limits, 16- vs 8-bit transfers, status vs alternate-status acknowledgement, reset and incomplete-command cancellation all behave correctly. Faults are contained.
- **No disc attached:** the D-138(1) contract applies.

**11. Hard disc.**
- **Location and identity:** `<save>/arcdroid/hd-riscpc/<content_id>/`, with explicit geometry and format revisions. Content identity is frozen across swaps and disk-control edits. The disc is shared across RiscPC CPU/firmware selections.
- **Format:** 63×16×256 (126 MiB), one general size, made by ArcDroid's formatter. The formatter is parameterised correctly (maps, root, checksums, IDENTIFY capacity), and A3020 output is unchanged.
- **Store:** the chunked sparse store from item 9, plus the existing CRC-framed per-command journal with an in-memory index.
- **Recovery:** a torn tail is recovered. A damaged disc is reported and never silently reformatted.
- **A3020:** the A3020 store and its files are untouched and never migrated.
- **Population hooks:** R2 publishes an authored blank filesystem and provides transactional population hooks — populate a private candidate, publish only when complete. R3 validates user-supplied boot resources under a reserved `<system>/arcdroid/riscpc/boot/` and populates the disc through them.

**12. Plan route, part 3.** Hard-disc fields, plus a test-only key that attaches a privately prepared disc, for Fable's evidence.

### Non-goals
- **R3:** shipping reachability, firmware inventory, Auto, launch automation, observers, boot-resource population and the missing-files notice.
- **R4:** gameplay qualification, rate qualification, the 218-title A/B and release notes.
- **Not planned:** save states, the faster CPU translator, RISC OS 5, podules and CD-ROM.

### Acceptance criteria

**AC-1 Timing (R2a)**
- Authored workloads match the documentation-derived expectations in `docs/riscpc-timing.md`.
- The hot-throughput case keeps the 100 MIPS ceiling.
- Private, by Fable: the factory reset succeeds at production rates on all seven pairs with BAT unchanged.
- R1 RPC goldens change only where costs apply, and each delta is explained against independent expectations.
- Odin3 at 100 MIPS on the R1 AC-8 workload, measuring both host frame cost and guest progress: p95 ≤ 0.80 T, and no frame over T.

**AC-2 CMOS (R2a)**
- Record round trip, key isolation and integrity.
- Fault injection at every write, flush, rename and publication step.
- Kill safety.
- Foreign and corrupt records refused.
- No cross-reading between Arculator and RPC records.
- Defaults: generation, cache reuse and invalidation, provenance contents, and the oracle's negative cases.
- Incremental generation slices.
- Private:
  - `*Configure` changes survive a fresh process on each pair;
  - each captured default restarts cleanly;
  - default generation wall time is measured on the Odin3.

**AC-3 Controller (R2b)**
- Synthetic-ROM, guest-driven tests through SuperIO, IOMD and FIQ DMA, with independently stated expectations.
- Coverage:
  - every item-6 command and field;
  - MT/EOT and the end-of-cylinder table;
  - FM/MFM, CRC errors, absent sectors, empty drives, write protect and deleted marks;
  - TC on the final byte, TC one byte early, queued FIFO bytes, and multi-sector termination;
  - both R1 hazards.

**AC-4 Media (R2b)**
- Authored JFD/ADF/ADL/helper fixtures covering:
  - identification, quarantine, omitted IDs and data, creation and D-121 guards;
  - every write policy and the store-failure mapping;
  - no partial commits;
  - cancellation on both drives.
- Differential traces against the Arculator side for presentation and committed results. Controller timing does not have to match.
- Store files written by one backend are readable by the other.

**AC-5 IDE and store (R2c)**
- Full-capacity addressing.
- Installer-like writes far beyond 8 MiB with no reopen.
- Checkpoint and recovery under fault injection at every boundary.
- The frozen command matrix, plus storage failures.
- The item-9 bounds, measured.
- A3020 store regression identical.

**AC-6 Arculator unchanged (each PR)**
- Every Arculator test and golden is unchanged. RPC goldens change only under AC-1.
- Fable's private A/B on the R1 44-title set, in both package states, at each PR gate: identical apart from known variance.

**AC-7 Isolation and determinism (each PR)**
- Shipping routes reject RPC plans, and capabilities, namespace and exports are correct.
- Arculator→RPC→Arculator switching, and failed start, reset and unload, all work.
- Goldens and audio counts are identical across pacing, Linux, Windows, macOS and the Odin3 runner.

**AC-8 Private RISC OS (Fable)**
- R2b, on 3.50–3.71:
  - ADFS read, write and format on authored discs in both drives, with swaps;
  - JFD store persistence across reload;
  - two-floppy diagnostics.
- R2c:
  - hard-disc `*CDir`/`*Save` persist across a fresh process;
  - 031, 074 and 084 install from their floppies onto the R2c disc, with a privately prepared boot environment, and start cold from it;
  - the original catalogue-before-install case is replayed.

**AC-9 Devices (Fable, R2c)**
- Native RetroArch media operations, save/reload and process termination on Windows and the Odin3.
- Odin3 pass limits:
  - peak PSS ≤ 240 MB during mount, checkpoint and reload;
  - store open ≤ 1 s;
  - AC-1's frame budget holds during an install that checkpoints;
  - no growth over 20 repeated sessions.

**AC-10 End-of-R2 speed gate (Fable)**
- Odin3 at 100 MIPS: guest progress and frame rate of the trial's private 031 and 110 gameplay workloads, compared with the trial's record (031 ≈ 27 fps).
- A drop of more than 10 %, or a visible or audible regression, goes to the Founder before R3.

### Evidence Sol must provide
- CTest names for AC-1–AC-5 and AC-7, wired into CI (sanitizer selection included).
- The two contract documents.
- Every new or revised golden, with its rationale.
- Fault-injection matrices.
- Android arm64 test-core and runner build commands.
- A report listing each source behind each timing constant, each patch with its reason, and the bounds chosen.

### Risks and QA focus
- Cost constants without primary sources, or fitted to R1-1.
- Trace churn hiding regressions.
- Any change to Arculator.
- Partial or duplicate commits, and stale callbacks across swap or reset.
- Store recovery and Android storage.
- CMOS key collisions.
- The dormant boundary.
- Provenance of the imported `ide.c`.

**Roles.** Sol: items 1–12 and the evidence above, one PR per part. Fable: contract acceptance, review, AC-1/2 private legs, AC-6, AC-8, AC-9 and AC-10. Opus QA on each PR after Fable's review.

## R1 outcome (S56) — merged as `7244f9c` (PR #312, head `b084890`; D-137, D-138)
The R1 work order and implementation record are in git history (`docs/handoff.md` at `07a6ebc`).

**What R1 delivered.** RPCEmu 0.9.5's interpreter is a contained, deterministic, **dormant** second backend in the one core. No shipping route, option, machine name or `.info` change reaches it; only the test core (plan files, `.arcplan`, test-only `.info`) and the headless runner `tools/rpc_runner.c` can start it (runbook `tools/rpc-r1-runbook.md`).
- **Import and isolation:**
  - pristine allowlisted import `fe96e41`, 28 files byte-identical to the tarball, with no header-less upstream file;
  - a private dispatcher with Arculator moved behind it mechanically;
  - the `arc_rpc_` namespace, enforced by an object-level Mach-O/COFF/ELF guard, plus exact export names;
  - containment of every fatal, exit and assert path.
- **Emulation:**
  - an integer virtual-time scheduler, at SA-110 100 MIPS and ARM610 20 / ARM710 35 / ARM7500 35 (provisional);
  - the MMU permission correction;
  - line-accurate VIDC20 scanout on both VRAM and DRAM paths;
  - exact audio accounting;
  - a PS/2 keyboard, plus a quadrature mouse on Risc PC and a PS/2 mouse on A7000;
  - ROMs of 2/4/8 MiB, and CMOS from a seed or a factory capture;
  - SMC/82077 empty-drive floppy semantics, and an absent IDE that times out as on hardware.
- **Sol runs:** A to H, 14 commits. QA findings, in order:
  - **AC-8 failure:** the Odin3 measured 1.42 T. Scheduler and MMU overhead were found and fixed (run F).
  - **Floppy controller:** SMC empty-drive semantics corrected (run F8).
  - **VRAM hardware scroll:** VIDCR readback corrected (run F9). Opus QA then showed the A7000 readback differs (run G).
  - **Keyboard:** a reset-stall regression fixed (run G).
- **Evidence:**
  - CI green on all six jobs.
  - Opus QA (effort 15): FAIL → PASS WITH LOWS → **PASS** at `b084890`.
  - **AC-3:** 44 library titles in both package states were identical to `main`, apart from known run-to-run variance (105/205).
  - **AC-5:** 36 authored plans were byte-identical on macOS (two pacings) and the Odin3.
  - **AC-7:**
    - RISC OS 3.50, 3.60, 3.70 and 3.71 reach the Desktop on all four models. A no-hard-disc startup message appears ("Disc error 08 at :4"); Escape, the mouse and the F12 command line all work.
    - On the Odin3, RetroArch 1.22.2 installed and loaded the plan core. Gamepad input reached the guest, and the core reloaded cleanly with 0 `[ERROR]` and PSS 170–180 MB.
    - Not run: native RetroArch on macOS (no RetroArch installed) and on Windows (the RPC core was exercised by CTest only).
  - **AC-8:** on the Odin3 at 100 MIPS, p95 0.70 T and p99.9 0.76 T, with no frame over T.

**Carried from R1 (R2 and follow-ups):**
- **R1-1 (required for R2):** on SA-110 at 100 MIPS the firmware misses the held-Delete factory reset (14 incidental writes). ARM610/710/7500 at their rates, and SA-110 at 20 MIPS, reset fully. A real StrongARM resets, so this is a timing-fidelity gap; it must be resolved without a silent capture-only rate.
- **QA L5 (R2):** the FDC interface lacks EOT/GPL/DTL/MT/SK and a data/status channel. MT/SK variants, VERSION, PERPENDICULAR, LOCK and READ TRACK are invalid. FORMAT field mapping and the FM READ ID path need R2's media bridge.
- **Trial residual:** the two-floppy FileCore interaction is unexplained. It must not become a single-drive restriction by default.
- **Arculator follow-up (pre-existing):** programmed VIDC registers persist across machine recreation, so the first frames after `retro_reset` differ from a fresh load. This is pinned by the characterization golden `tests/fixtures/arculator-reset-poweron.trace`, which changes intentionally when this is fixed.
- **Pre-existing:** after a killed run, `libretro-harness` leaves its data directory dirty, and a rerun aborts until the directory is removed.

**Unit P work order:** closed (D-131); in git history (`docs/handoff.md` at `cd32872`).

**Unit P progress (S52, private evidence `scratchpad/solP/clone/SOL-REPORT-P.md` and `probe/riscpc/run/`).** Mac, RISC OS 3.71: AC-5 PASS (seven firmware/CPU pairs to the desktop, keyboard and per-model mouse); host CTest 3/3 also under ASan/UBSan (473 mechanism, 47 controller checks); authored 600 s replay identical across pacing; **031 installs onto the scratch IDE and reaches active racing**; **110 reaches gameplay** (a transient HUD distortion during movement is unclassified); 074/084 install their discs but their shared support installer needs the standard `!Boot`. Fable rulings (general, never title-specific): **R3-1** every session's cold CMOS is the firmware's own Delete-power-on factory reset (all-zero CMOS was unrepresentative); **R4-1** a project-authored multisync monitor definition file (from Acorn/VESA published timings) loaded by a project-authored boot step — without an MDF RISC OS 3.5+ refuses every mode selector; **R5-1** the standard boot sequence from RISC OS Open's Apache-2.0 HardDisc4 (sha256 `28613a8e…`) plus its `!System` resources (PlingSystem, sha256 `0259063d…`), merged as RISC OS Open documents for pre-RISC OS 5 machines — private to the trial; whether ArcDroid may ship or fetch them was answered by D-134: never — user-supplied in the system folder. No AC-7 calibration, cross-platform build or AC-8 tooling yet.
**Slice 29 work order:** merged; in git history (`docs/handoff.md` at `bc5e58b`).

**Item 7 outcome (S53 D-128, S54 D-130; private evidence below).** S53: memory pressure refuted; 168/169 fixed by Slice 29. **S54:** (1) 146-class (101/111/136/146): the RISC OS 3.11 screen size is raw CMOS `&CF` (the firmware's own `*Configure ScreenSize 20` writes `&14`, checksum `&3F` +`&14`; ArcDroid's shipped CMOS holds 0), but presetting it changes nothing: natively the four stop at their publisher's configuration utility because its first step (`*opt 4 0`) is refused (`JFD write refused … reason=wp-flag`); writable, it saves the old CMOS to the disc, `*Configure`s drive/font/sprite/system/RMA/screen/RAM-disc sizes and Boot, writes a user-CMOS marker and resets, and all four reach title/attract natively with no input (relaunch: the utility repeats its automatic reconfiguration because D-101 clears the persisted Boot bit; still no input). Under ADFFS, 101/111/136 play; 146 crashes because the desktop start-up shrinks screen memory to the mode minimum (`OS_ChangeDynamicArea 2, -&4000000` from ROM) and its JASPP script remaps video memory only on RISC OS 3.5+, so its loader loads to address 0 and overwrites the vectors. (2) Library A/B (native, no package, forced writability vs today, the 104 titles with write-protect-flagged or unflagged JFDs, 150 s): 95 frame-identical, 101/111/136/146 gain, **103 regresses** (its loader demands a write-protected disc), 019/080/191 neutral, 164 known variance; no non-writing title changed, so no title reads the drive's write-protect status otherwise. A blanket rule is rejected; the general alternative is the Founder's choice (waiting entry). (3) Lead 79: the package's script for 79 passes the protection key with `3F` where the game's own `!Run` passes `A0 A0` (5 occurrences); a private package copy with those bytes corrected decrypts all stages and reaches the attract screen — a package data defect, not emulation. (4) 48-title ADFFS-vs-native contact sheets: ADFFS ahead for 15 titles, native ahead only for 79 and 122 — ADFFS stays the right default route. Remaining item-7 residue: 123/158/176 (empty desktop; icon-bar class, tabled D-085), 75 (Wimp box dismissed, unexamined). Private evidence: S54 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/4944dc22-5510-4073-be08-0f54041830ff/scratchpad/item7/` (`LEDGER.md` index; `probe/` = `main` 94a519e + `/* PROBE */` diff: S53 SWI/MEMC logging, `PROBE_CMOS` raw preset, `PROBE_FORCE_RW`, PC sampler/exception log, `PROBE_PKG_UNTESTED` — never a Sol source; legs `base`/`ss20`/`nbase`/`nss20`/`full146`/`trace146`/`key`/`rw`/`fix79`/`fix79b`/`lib-base`/`lib-rw`; `pkgfix/` private corrected package copy, `obeyx/` extracted script; `png/` sheets `an1`/`an2`/`abrw`).

**Slice 29 evidence (S53).** Sol run 1 (thread `01a10ee3-b391-7ad3-b1ba-3f2e0523dda5`, 88 min) committed verbatim as `60129a7` (PR #300): one launcher line `SetEval LCDGM$Status 0` after `SetVars`, contract comment, exact-text/SHA goldens and a new ordering stage in `adffs-package-media`, release note; launcher 1,071 → 1,094 bytes, RAM-disc allocation unchanged (2,048-byte rounding); Sol: host CTest 188/188, sanitizer CTest 183/183, direct sanitizer `libretro-harness` pass. Fable review: diff minimal and in scope; no package text (D-049). **AC-3 PASS** (private, RISC OS 3.11 + ADFFS 2.88, host build of `60129a7`, all 48 ADFFS-route titles vs the Slice 28 baseline): 25 frame-identical, 23 timing/animation-phase differences reviewed on contact sheets — every title reaches the same t150 state except 168/169, which move from the "Unknown operand" box to their attract loop; 0 `[ERROR]`. **AC-6 PASS** (Odin3, arm64 of `60129a7` sha256 `64fe572a…`, sideloaded through the menu with the repo `.info`, "Core installation complete"): 168 `JFD boot: path=adffs` → high-score attract and title screen, no error box, 0 `[ERROR]`, peak PSS 243 MB; RetroArch closed. CI on the PR event: android-arm64, windows, host, windows-retroarch pass; host-sanitizers pending at this baton. Private evidence: S53 scratchpad `sol29/` (prompt, stream, `SOL-REPORT-29.md`, `final1.md`), `ac29/` (slice-head host + arm64 builds), `item7/ac3/` + contact sheets `b3{a,b}.jpg` vs `a3{a,b}.jpg`, `odin/` (`d168a-*`); device runner `/data/local/tmp/s53.sh`, evidence `/data/local/tmp/s53/`.

**S55 (Claude Opus 5.5 effort 15, window `arcdroid-7e`; baton by relay from `arcdroid-df`).** Founder answered both S54 gates (D-131, D-132), added the faster CPU translator (D-133), ruled no shipped guest files (D-134) and arm64-only Android (D-135); Slice 30 designed, implemented, QA'd and merged (outcome above); item 5 grounded and paused.

**S54 (Claude Opus 5.5 effort 15, window `arcdroid-df`; baton by relay from `ArcDroid-52`).** Item 7 leads 146 and 79 investigated to their causes (outcome above, D-130); write-protect tab question opened to the Founder; no code changed. Nothing left running (the full-library legs finished; an earlier full-length leg was stopped by this session).

**S53 (Claude Opus 5.5 effort 15; baton by relay from `arcdroid-1b`).** RiscPC go/no-go still unanswered (stays waiting). Item 7 measured (outcome above); Slice 29 designed (D-128). No Astra challenge for Slice 29: a one-variable launcher parity with a full 48-title A/B already run is trivial and low-risk under protocol §8 (rationale in D-128). Founder answered the supervision question with option A and authorised the settings amendment (D-129): `.claude/settings.local.json` now allows `~/dev/agent-tools/wait-run.sh`. Private evidence: S53 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/969fce33-841e-4b4c-8572-ccc466dbf3fd/scratchpad/item7/` (`probe/` = `main` + `/* PROBE */` SWI/MEMC logging and the one-line launcher change, never a Sol source; legs `pkg`, `nopkg` (four titles, unfixed), `fix1` (168/169 fixed), `fix48` (48 ADFFS titles fixed); `ab53.py`, `cmp53.py`, `jfdvars.py`; contact sheets `base{1,2}.jpg` vs `fix{1,2}s.jpg`; `pkgx/` = extracted private package, read-only).

**S52 (Claude Opus 5.5 effort 15, window `arcdroid-1b`; Founder in this window).** Merged PR #291 (Founder's D-124/D-125 governance, left open). Founder answered the RiscPC gate (D-126). Firmware 3.50/3.60/3.70/3.71 verified privately against MAME's chip SHA-1s. Trial work order written and challenged (Astra thread `01a0ffdf-…`, stage 3 PROCEED WITH CHANGES, incorporated). Private evidence: S51/S52 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/2abcd450-479a-4e2b-b80d-4720072de552/scratchpad/` (`riscpc/workorder-p.md` draft, `astra/brief3.md`/`report3.md`, `private/games/<index>/discN.jfd`).

**S51 outcome (Claude Opus 5.5 effort 15; Founder: "pickup from handoff, odin 3 is connected").** **Slice 28 AC-6 (Odin3) PASS:** arm64 of `3dfe5dd` (sha256 `7dd684e9…`, production equal to `main`) installed through the menu with the repo `.info`. 67: chooser → pad A → title → its own "insert levels disc into drive 0" screen → disc 2 by disk control → a key → its menu → `1` → Return → Stage A gameplay; first load `guard=old-map unavailable=10 tail=0 generation=0`, store created during play; force-stop; relaunch `generation=5 records=3` (chooser remembered), played to Stage A again; a third launch reads `generation=10` (the second run's writes persisted too); no guard refusal; 3 runs 0 `[ERROR]`, peak PSS 178–187 MB. (Per-commit `JFD write:` lines exist only in fault-test builds; the device proof is the generation counter.) **Next workstream chosen (D-122):** item 6 — the RiscPC-only sets are the largest group of library titles that do not play at all; item 5 makes no new title play and its buildbot submission needs publication (D-100: private); item 7 follows. **RiscPC evaluation (D-123):** RPCEmu 0.9.5 provisional, conditional on a feasibility probe (thresholds in `docs/riscpc.md` §5); Astra thread `01a0ffdf-ee7a-7ea2-b5b7-b2be92b53e70` stages 1–2 (PROCEED WITH CHANGES, all incorporated). Private evidence: S51 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/2abcd450-479a-4e2b-b80d-4720072de552/scratchpad/` (`LEDGER.md` index; `odin/` Slice 28 AC-6 screenshots `d67a.*`, `d67b.*`, logs `d67a/b/c.log`; `riscpc/rpcemu-audit.md`, `riscpc/alternatives.md`, `riscpc/rpcemu/` (0.9.5 tarball + extracted source, headless build), `riscpc/alt/` (alternative clones), `riscpc/probe/` (throughput probe: `probe_main.c`, `mkrom.py`, `build.sh`; never a Sol source), `riscpc/candidate.md`; `astra/` briefs/reports 1–2 and clone with `.evidence/`). Odin3 probe binary at `/data/local/tmp/s51/rpcprobe/`.

**S50 outcome (Claude Opus 5.5 effort 15, window `arcdroid-d4`, baton via the `ArcDroid-49` relay). Slice 28 merged as `7c2671f`** (PR #284, head `3dfe5dd`; Sol runs `e381b3f`, `629de12` (A1), `3dfe5dd` (QA tests/wording); governance PRs #283 design, #285 A1, #286 QA baton, #287 remediation baton). RW-flagged **old-map (format D)** JFDs whose end-of-disc protection track lies in free space — or whose map declares more than the 3,200-unit D capacity — are **guarded-writable**: unavailable set U (non-writable P-DD5 slots of irregular track-sides), declared tail T; admission needs an old map, P-DD5 profile and placement, T free (U may already be allocated, A1); a fail-closed map-write guard on exact 256-byte units refuses any growth of unavailable allocation (A(pending) ∩ U ⊆ A(current) ∩ U), any T allocation, size change or invalid map; reload checks A(selected) ∩ U ⊆ A(base) ∩ U; `guard-violated` keeps every store file. Beneficiaries: **67 now plays** (it exited after its title because its start-up writes were refused); 100 and 149 become writable (149 has a save directory). Also S50: **item 4 closed for now (D-118)**; **D-105 follow-up deferred (D-119)**; 159's "blank setup on the device" retired (host identical). **Evidence:** Astra (thread `01a0fc9d-d998-7cf2-a8d5-b48f532cf30e`) stages 1–3 PROCEED WITH CHANGES (all incorporated); Fable host CTest 188/188 (`e381b3f`) and focused 14/14; **AC-3 PASS** (private, real RISC OS 3.10/3.11/3.19: 67 guarded `unavailable=10 tail=0`, start-up writes, disc 2, gameplay; `*Save`/`*CDir` onto the protection track refused at the data write; an **open-file extension writes the map first** and is refused at the map write; guest `ADFS_DiscOp` map writes refused (`guard-unavailable`; 100/149 `guard-tail`); `*Copy`, rename, delete, `*Compact` succeed; continued saves succeed; `*CheckMap` consistent immediately and after remount); **AC-4 PASS** (218-zip legs with and without the package vs `40d156b`: 211 identical, only 67/100/149 change write state, 4/105/164/205 known t150 variance, 0 `[ERROR]`); **Opus QA** (effort 15, ArcDroid QA agent): PASS WITH LOWS (no defect; 20 000-step random-walk model 0 mismatches; 1 922 non-guarded loads identical to `main`; 29/38 mutants killed) → run 3 → re-check **PASS** (all 5 surviving mutants killed; note: the re-check ran while the baton still read IMPLEMENTATION/Sol — it was the planned Next and changed no state); CI green on all five jobs at `3dfe5dd` (PR event; slice branches run no push event). **L-2 ruling (recorded):** C-1(c) governs — a captured sector carrying a P-DD5 ID that is not an eligible standard sector (e.g. density 1) on an irregular track-side belongs to U and its write is refused; non-P-DD5 IDs keep D-075. Private evidence: S50 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/1111d17d-e01b-48e5-a74b-24c2b76fa434/scratchpad/s50/` (`LEDGER.md` index; `astra/` both threads; `sol28/` prompts, `SOL-REPORT-{1,2,3}.md`, streams; `qa28/clone` QA tree; `ab/` A/B legs `base-{nopkg,pkg}`, `s28-{nopkg,pkg}` with `ab50.py`/`cmp50.py`; `p/ac2-*` AC-3 runs; `ac28/` = slice head + private harness verbs; `android28/` arm64 build; `probe/` research probes, never a Sol source; `odin/`).

**Slice 28 work order:** merged; in git history (`docs/handoff.md` at `9fa3944`, A1 at `c75f2e5`).
**Slice 27 work order:** merged; in git history (`docs/handoff.md` at `6c4320f`; QA baton `b09b59d`).

## Session history (S45–S49, condensed; details in `docs/build-log.md` and git history)
- **S49** (Claude Opus 5.5, window `ArcDroid-49`): Slice 27 merged as `28dcd8d` (PR #280, D-117: the left analog stick acts as the D-pad; AC-12 Odin3 PASS). Private evidence: S49 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/3cdb83d2-9641-410a-bedf-da1b443bef05/scratchpad/s49/` (`LEDGER.md`).
- **S48** (Claude Opus 5.5): Slice 26 merged as `f472bb0` (PR #274, D-114; AC-8 Odin3 PASS); Slice 25 AC-9 Odin3 PASS; Founder D-112 (left stick = D-pad) and D-113. Private evidence: S48 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/91d86771-bb37-4ce9-88f2-9c2f7abafca4/scratchpad/s48/` (`LEDGER.md`).
- **S47** (Claude Opus 5.5): Slice 24 merged as `77b9c64` (D-110, item 2 closed) and Slice 25 as `20d0154` (D-111). Evidence: S47 scratchpad `5eeddff6-…/scratchpad/s47/`.
- **S46** (Claude Fable 5.1): Slice 23 merged as `db2db46` (D-109). Evidence: S46 scratchpad `4f0c94d7-…/scratchpad/s46/`.
- **S45** (Claude Fable 5.1, window `ArcDroid-45`): Slice 22 merged as `2018bb7` (D-108). Evidence: S45 scratchpad `829d655e-…/scratchpad/s45/`.

**S48 D-105 research (private; indices only; needed for the D-105 follow-up).** Probe clone of `main` `ce781af` (S48 scratchpad `s48/d105/probe`, never a Sol source). While a disc prompt waits no title polls the drive. Signal **A** — FileCore `UpCall 2` MediaNotKnown naming the disc (20 `…2`, 65, 96, 200; the OS prints/raises "Please insert disc '<name>'" each second until it appears). Signal **B** — a user-mode lookup (`OS_File` 5/17/255…, `OS_Find`) on drive 0 that fails on the inserted disc while the object exists on another image: polling titles 26, 81, 89, 115, 173 (116–5 083 lookups), acknowledgement titles 68/69 (`$.TRACKS.1…5` per OK click), 30, 23 (own error box re-raised per OK), 12, 36, 42, 49; 71 likely. Validation: inserting the requested image alone unblocks 26/81/89/173 (89 and 81 reach gameplay); 115 then asks for disc 3 the same way; 68 and 20 proceed on their next acknowledgement. False-positive risk: 97 and 118 make one cross-disc lookup each in normal play. Of the 65 multi-image sets, 43 showed no request within 240 s of light input (requests may come later in play). Founder D-113: insert automatically with a notice; option for hint-only/off. Slice 26 covers fully qualified names; unqualified `$.`, `:0.` and variable-relative names (68/69's `$.TRACKS.N`, 49's `:0.…`, 12/36/71) need authenticated filesystem/media binding.

**Slice 26 work order:** merged; in git history (`docs/handoff.md` at `933c28f`; QA baton `315fd66`).
**Slice 25 work order:** merged; in git history (`docs/handoff.md` at `023bd91`; QA baton `5c3724a`).
**Slice 24 work order:** merged; in git history (`docs/handoff.md` at `31ea908`; QA baton `56e583e`).
**Slice 23 work order:** merged; in git history (`docs/handoff.md` at `9faf773`, run-3/run-4 amendments at `5f1cca9`/`68c176e`).
**Slice 22 work order:** merged; in git history (`docs/handoff.md` at `339173f`).

## Carried context (S43 and earlier)

**S43 outcome (Claude Fable 5.1, then Claude Opus 5.5 from the same window `arcdroid-6c`; baton via the S42 relay).** **Slice 21 merged** as `7978585` (PR #240, head `9b49726`; D-107): every native session on the A3020 (the only machine ArcDroid attaches IDE to — one descriptor flag for adapter and engine) gets a persisted writable hard disc at `ADFS::4` from `<save>/arcdroid/hd/<content_id>/`, created, hash-verified and published before any guest execution; option `arcdroid_hard_disc` (auto/off, attachment only); visible failure matrix, never a temporary disc or a reroute; CMOS logical 189 attachment-derived; boot observations revision 3 stamp the effective disc; with the package present a single-image, disc-1-of-1, high-density JFD on an IDE-attachable machine boots natively (`reason=hard-disc`) before any recipe, package or install-store access. Beneficiaries: 159 plays natively with saves on the hard disc for package owners (host: desktop save → fresh process "Game loaded"); 66/117/156 now reach their own screens natively (66 needs the later self-extract slice; 117/156 are RiscPC-only). Sol run 3's test exposed a real defect before merge (the route helper read freed JFD disc fields in the install-machine preflight, so a title with an old READY install store was promoted as `install-disc` after package/store access) — fixed and proven on host firmware and on the Odin3 (which holds old install stores). **Evidence:** Astra S43 stages 1–2 (PROCEED WITH CHANGES; all incorporated). Opus QA (effort 15): run 1 FAIL (narrow: test gaps — readback outcome, preflight exclusion) → Sol run 3 → re-check FAIL only on a known test-table count → Sol run 4 (test-only) → CI green on the PR event at `9b49726` including an executed `windows-retroarch` (AC-6 guest round trip). AC-8 private (real RISC OS 3.11): 159 save/reload, populated disc (`!Boot` + app) leaves the floppy boot frame byte-identical and the app runs, `ADFFS preferred` bypass, `Hard Disc: Off` independence, host peak RSS within bounds (off 99.5 / cold 132.0 / warm 115.5 MB), existing install store byte-identical; 218-zip A/B vs `main` `dadef64` with and without the package: 200/218 identical each, differences only the predicted ones (known variance 4/105/164/205; hd-media natives icon/phase; with the package the routes of 66/117/156/159), 0 `[ERROR]`. AC-9 Odin3 (arm64 `85023f7`, production-identical to the merge): 159 `hd-media` + `hard-disc` despite the device's old install store, disc created, persistence proven by a `*Cdir` survivor across force-stop (the game's own save is not exercisable on the device: its setup screen renders blank — known since 10b), 84 attached (`multi-image`), 11 unaffected, Hard Disc Off via Quick Menu, 6 runs 0 `[ERROR]`, peak PSS 183–213 MB, install stores byte-identical. Private evidence: S43 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/176c646f-044b-4cfa-be74-8993f314e00d/scratchpad/s21/` (`LEDGER.md` index; `workorder-v3.md`; `astra/`; `sol/` prompts and reports 1–4; `qa/` adjudication and QA drivers; `ab/` legs `base-pkg`, `base-nopkg`, `s21-pkg`, `s21-nopkg` with `ab43.py` (`HARNESS_EXTRA`) and `cmp43.py`; `work/` host runs incl. `run43.sh`; `odin/` device evidence).

**Test-harness note (Slice 21):** `tests/libretro_harness.c` defaults `arcdroid_hard_disc` to `off` and `arcdroid_install_saves` to temporary; pass `--boot-hard-disc-auto` (and `--install-saves persistent` to reach the install-machine preflight) for runs that should match the shipping defaults.

**S42 outcome (Claude Opus 5.5, baton via the S41 relay).** **Combined Odin3 pass for Slices 18/19 (D-104): PASS** (62 helper and "ODN 150" save through `native-writable`, store generation 8 → 16, visible after reset and fresh launch; 127 self-boot; 185 native two-pass loader; 34/174 play natively; 154 OK by tap; 0 `[ERROR]`; 174's save not exercised on the device — same path proven by 62). **Founder D-105:** roadmap item 2b goes **native-first** (Option A) with friction refinements for item 3. **Slice 20 merged** as `c9df9ef` (PR #237; D-106): on the native path, omitted standard sectors that the disc's own validated FileCore map and directory tree prove to hold file or directory data read as zeros (non-writable D/E images; option `arcdroid_jfd_omitted` `file-data`/`strict`). Result: 68 and 69 boot natively to their own "insert TRACKS disc" prompt; disc swap through disk control + OK → play; high-score saves persist in the RW data disc's store (host and Odin3); 200 passes its former "Disc error 18" to its own prompts and game window; 209 boots under `Disk Writes: Off`. With the package present 68/69/159 still take today's install path (unchanged until the install path is deleted).

**Private evidence (never commit):** S42 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/8eec8944-15ed-4974-8c1f-779dc3da748a/scratchpad/` — `s2b/LEDGER.md` (the index), `s2b/run.sh` (env `ADFFS`, `EXTRA`, `ROM`, `CLONE`, `BUILD`), `s2b/probe` (main `56382d2` + harness patch `s2b/probe-harness.patch`), `s2b/probe20` / `probe20f` (Slice 20 `e08274f` / `74800ec` + patch), `s2b/work/` (`g2`–`g8` agent evidence; `g3/probe` = PROBE_HD, `g4/probe` = R1 probe, `g7/probe20s` = option flag, `g8/materialize.py`), `s2b/ab/` (`ab42.py` with the NOPKG fix, `cmpn.py`; legs `n-base`/`n-s20` no package; `g6/base-nopkg`/`g6/s20-nopkg` are **package-present** legs despite their names), `astra/` (thread `01a0ebbf-4d89-7511-aa7d-f37a2d6bce73`, stages 1–2), `sol/` (prompts 1–6, reports; Sol thread `01a0ebf4-b20f-7092-a84b-5ec13c84145b`), `qa/adv/` (QA fixtures, mutation drivers and results), `odin/pass/` (D-104 pass), `odin/ac8/` (Slice 20 AC-8), `odin/clone20` (arm64 `7821801`).

**S41 outcome (Claude Opus 5.5, baton via the S40 relay). Roadmap item 2a is closed in code.** Merged **Slice 18 (D-101)** as `63fd55f`: the CMOS Boot flag is no longer forced; every RISC OS 3 construction whose starting CMOS says NoBoot holds left Shift through an internal input source from power-on until 1 s of emulated time or the first `OS_FSControl 15`; the Boot bit is launch-owned in persisted records (old records heal); boot-observation revision 2 → two-pass "Shift-Break" loaders run their configuration pass (99, 185 play natively). Merged **Slice 19 (D-102)** as `9332d53`: with `JFD Boot: Auto` and the package present, a single-disc JFD that is structurally native-writable (extended header, RW set, WP clear, D-076 layout plan) and whose header OS mask is zero or includes RISC OS 3.1 boots natively (`reason=native-writable`); **the ten-entry native-route table is deleted** (D-084 retired). Result on the library (package present): the ten former table titles take the rule with unchanged frames; 99 124 154 161 167 174 181 185 201 move from ADFFS to native; 32 53 86 (broken under ADFFS) become reachable; 14 54 134 stay on ADFFS; 112 fails on both paths. **Founder decisions S41:** D-103 (154 runs natively with its own Cancel-default acknowledgement box for now; controller answers for RISC OS error boxes join roadmap item 3); D-104 (Slices 18 and 19 merged without individual Odin3 legs; one combined pass before the next slice).

**Item 2a evidence (private, S41; real RISC OS, verified ADFFS 2.88; indices only).** Survey of the 30 single-JFD RW titles and diagnosis by class (ledger `s2a/LEDGER.md`); Slice 18 AC-6: 99 on 3.10/3.11/3.19 and 185 on 3.00–3.19 and on A3020/A3010, first/second launch, reset, termination between loader passes, healed records, observer cancellation, install lifecycle (68) identical to `main`, 218-zip A/B and a no-package RW leg — no regression. Slice 19 AC-4: visible save round-trips on the final build with the package present (save → in-session reset → fresh reload shows the entry): 34 `00150 QAZ` and 102 `QAZ 4340` by real play; 124, 161, 174, 181 with a RAM poke only for the qualifying score (entry and save by the game); the S33 AC-4 scripts reproduce the store counts of 33 35 45 62 82 98 128 153; 99 185 154 167 reach play; 218-zip A/B vs Slice 18: only the predicted route changes, frames of the former table titles identical, every moved title equal or better (154/167 stop at their own boxes, 201 waits at its instructions for Space) — no regression. Opus QA (effort 15): Slice 18 PASS WITH LOWS → run 3 → PASS; Slice 19 PASS WITH LOWS → run 2 → PASS.

**Item 2a residue (not scheduled; general machinery only):** 54 and 14 are RISC OS 2-only by header (ArcDroid performs no launch on RISC OS 2; 54 plays on 2.01 when booted by hand; 14's own `!Boot` is damaged); 134 is the only old-map L-format JFD (ADFS reads logical track n at cylinder n head 0 while the native model maps index = 2·cyl + head; also needs a system sprite area; write-protected by the D-076 gate); 14 and 67: an irregular protection track in free space write-protects the image (D-076 gate); 32: native attract, no credit registers (possible protection gap); 53: store round-trip proven, visible restore unproven; 185 has its own save/load prompt (unexercised save candidate); 112: code protection aborts on both paths (CPU fidelity?). S41 follow-up Low: S-4 align `tests/power_on_guest_assert.py`'s strict `down < release < up` with the per-frame rule.

**Private evidence (never commit):** S41 scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/89cd7821-0625-4311-92cc-6f54d27495f8/scratchpad/` — `s2a/LEDGER.md` (the index), `s2a/run.sh` (env `ADFFS`, `EXTRA`, `ROM`, `CLONE`, `BUILD`), `s2a/probe` (main `eb10158` + probe harness incl. `type`), `probe-nb` (Boot-flag probe), `probe18`, `probe19` (slice heads + probe harness), `s2a/obey/` (JASPP scripts), `s2a/work/` (`g1`–`g6` agent evidence, `ac6/`, `ac19/`, `fsc/`), `s2a/ab/` (`ab41.py`, `cmp.py`, `pairs*.py`; legs `nb`, `nb2`, `s18`, `s19`, `rwmain`, `rws18`), `sol/` and `sol19/` (prompts, reports), `qa/`, `qa19/` (QA mutation drivers and logs), `astra/` (thread `01a0e96f-5261-7ec2-9910-67cf2df3da2d`, stages 1–4), `odin/clone` (arm64 build of `3b49b22`; rebuild from `9332d53` for the combined pass).

**Broken-launch cluster after 12a (Founder S33: roadmap item 1, generic mechanisms only, no per-game hacks):**
1. **Broken-launch cluster classes (diagnosed S33; private ledger `p33/launch/LEDGER.md`, proofs by guest-side experiments on private copies):** **G1** self-boot without a root `!Boot` (22, 133) and **G2** applications one directory down (41, 44, 56) → **Slice 12a (merged S35)**; **G3** JFD parser rejects a whole image for one unmodellable protection track-side (81; a class of one) → **Slice 16 (D-097; merged S40 as `21bb3b7`)**; **G4** no RAM disc for CFS-compressed apps (11, 16; RamFsSize 0; partial gain only — ends at a Filer window); **G5** JASPP captures omit free-space sector IDs on track 0 side 0 → ADFS miscounts sectors per track → "Broken directory"/"Disc not understood" (DD 30, 48, 67, 77, 100, 149; quad 78, 84 and the eleven single-image quads natively) → **Slice 12b (merged S36, D-089)**; 78 then needs `System:` (separate class), 84 is an HD-install title; **G6** a shared 1991 loader writes a RISC OS 2-numbered CMOS unplug mask and soft-resets → RISC OS 3 unplugs FileCore/ADFS/BASIC-class modules and ArcDroid persisted the poisoned CMOS per title (32, 38; 148/193/210/211 write the same bytes harmlessly) → **Slice 13 (D-091; merged S38 as `708a06c`)**; **G7** Arthur-only `!Boot` beside a RISC OS app (125, 197, static 119; the boot ends at the RISC OS `*` prompt — the same outcome as 47/48 (RMA exhaustion), 24/99/114/143/185 natively and 68/69 without a package) → **Slice 14 (D-092/D-093/D-095; merged S38)**; **G9** (found S36 by 12b's A/B) quad titles that, once identified, stop at "System resources cannot be found" or a missing `LoadModeFile` (78, 90, 110, 116, 156); **S40:** 90, 110, 116, 156 are RiscPC/StrongARM re-releases of playable originals (89, 109, 115, 155) → RiscPC workstream (D-063, D-085); 78 is an installer → item 2b. Also found: RW-flagged discs the D-076 layout gate write-protects refuse the game's own start-up writes (67 exits after its title) — a writability class for later. **G8** short raw images zero-padded (127, 190, 186, 72; short images 73, 104, 142, 177, 178, 204, 205) → **Slice 15 (D-096; merged S39 as `5e0967c`)**. Out of scope: 31 (HD install + Toolbox modules not in ROM + 6 MB RAM → RiscPC/HD-install workstream); 38's disc-sticker protection.
2. *(Withdrawn by D-090: evidence to add idx 124, 161, 174, 181 to the native-route table would extend per-game data; those titles are served by roadmap item 2.)*
3. Private diagnoses (evidence for general fixes, D-090): Premier Manager [SA] blank setup menu on the device (colour depth); idx 54 native "Not enough memory in module area"; idx 185 native hang; Exodus, The Time Machine.
**Tabled / deprioritised (Founder S33, D-085):** the icon-bar auto-start helper ("whack-a-mole"; if revived: a core option, default On only if it works 100 %, no per-game customisation; probe private in `p33/clone10d` and `p33/iconbar/`). Slice 10c (SpeedBall 2 [RPC]; the RPC editions duplicate playable originals; S32 probe `p10c/FINDINGS.md`, S34 summary in the S34 scratchpad `boot-and-10c-summary.md`).

**Founder mandate (S21):** make these Archimedes games accessible and easy to play without any unnecessary hurdles. **Founder vision (S27b, D-073):** every game in the library working, with saves, pick-up-and-play with minimal fuss; slice order is the orchestrator's to choose. **Founder S33/S36 (PRD P-4, D-090):** one general machine — generic per-class mechanisms, never per-game data or fixes. Judge every slice and gate against these.

## Recently closed
- **Slice 30** merged S55 as `ff048e4` (PR #307, head `018a05f`; Sol runs 1–4; D-132, D-136): per-game "Write-protect Tab" option; 101/111/136/146 play natively with *Off* (146 previously played nowhere); default byte-for-byte unchanged. Opus QA PASS WITH LOWS → run 4 → re-check PASS WITH LOWS; CI green on all five jobs; AC-12 private and AC-13 Odin3 PASS (see S55 outcome). Release note `docs/release-notes-slice30.md`.
- **Slice 29** merged S53 as `215cf7f` (PR #300, head `60129a7`; Sol run 1; D-128, D-129): the ADFFS launcher sets `LCDGM$Status` = 0 after `SetVars` and before the first `ADFMount`, matching the package loader; 168 and 169 reach their attract loop under ADFFS. Opus QA (effort 15, ArcDroid QA agent) PASS (no defects; 8/8 ordering mutants killed; host CTest 188/188); CI green on all five jobs (PR event); AC-3 private (48 ADFFS-route titles: only 168/169 change state, 0 `[ERROR]`) and AC-6 Odin3 PASS (see Slice 29 evidence). Release note `docs/release-notes-slice29.md`.
- **Slice 28** merged S50 as `7c2671f` (PR #284, head `3dfe5dd`; Sol runs `e381b3f`, `629de12`, `3dfe5dd`; D-120, D-121): guarded-writable old-map JFDs (see S50 outcome); AC-6 Odin3 owed. Work order in git history (`docs/handoff.md` at `9fa3944`).
- **Slice 27** merged S49 as `28dcd8d` (PR #280, head `9e1ebba`; Sol runs `a0ec851`, `9e1ebba`; D-117): the left analog stick acts as the D-pad (see S49 outcome). Work order in git history (`docs/handoff.md` at `6c4320f`; QA baton `b09b59d`).
- **Slice 23** merged S46 as `db2db46` (PR #253, head `714d8a8`; Sol runs 1–5; D-109): installed applications on the native hard disc (see S46 outcome). Work order in git history (`docs/handoff.md` at `9faf773`, amendments at `1c4788c`/`5f1cca9`).
- **Slice 22** merged S45 as `2018bb7` (PR #244, head `7b0f65d`; Sol runs `3c98e00`, `d3e7aa0`, `db19623`, `7b0f65d`; D-108): the ADFFS install path deleted (see S45 outcome). Work order in git history (`docs/handoff.md` at `339173f`).
- **Slice 21** merged S43 as `7978585` (PR #240, head `9b49726`; Sol runs `9e38a82`, `13bcc2c`, `85023f7`, `9b49726`; D-107): persisted hard disc for native sessions (see S43 outcome). Work order in git history (`docs/handoff.md` at `2dfb4fa`).
- **Slice 20** merged S42 as `c9df9ef` (PR #237, head `74800ec`; Sol runs `e08274f`, `dc79c2d`, `7821801`, `06d6f2c`, `8ff31dd`, `74800ec`; D-105, D-106): omitted file-data sectors read as zeros on the native JFD path (see S42 outcome). Astra S42 stages 1–2 PROCEED WITH CHANGES (adjudicated). Opus QA (effort 15) FAIL (narrow: M-1 map agreement compared step starts only; M-2 resolution limit reachable) → run 5 → PASS WITH LOWS → run 6 → PASS (42/50 mutants killed; 8 equivalent). CI green on all five jobs on the PR event at `74800ec`. AC-7 private: 218-zip A/B with and without the package — only 68/69/200 change (the only presented images), known variance 4/105/164/205; focused 68/69 saves, 200, 40/53/85 unchanged, 100/209, `strict`, package install unchanged. AC-8 Odin3 (arm64 `7821801`): 68 native — prompt, disk-control swap, touch OK, race, saves 15600 and 1476 visible after fresh relaunches. Work order in git history (`docs/handoff.md` at `cf1a3d4`).
- **Combined Odin3 pass for Slices 18/19** (D-104) PASS in S42 (see S42 outcome).
- **Slice 19** merged S41 as `9332d53` (PR #234, head `20e15fb`; Sol runs `57899e6`, `b80d56d`; D-102, D-103): native-writable JFD routing; native-route table deleted (see S41 outcome). Opus QA PASS WITH LOWS → PASS (mutants M01/M13/M14 killed by run 2). CI green on the PR event against `main`. AC-4 private complete; AC-5 Odin3 deferred to the combined pass (D-104). Work order in git history (`docs/handoff.md` at `20e15fb`).
- **Slice 18** merged S41 as `63fd55f` (PR #233, head `c11cde3`; Sol runs `8ba798a`, `3b49b22`, `95c1621`; D-101): power-on Shift instead of a forced Boot flag (see S41 outcome). Opus QA PASS WITH LOWS → PASS (TG-1–TG-4 closed; L-1 frontend cleanup releases held keys through the FIFO in every session, documented). CI green. AC-6 private complete; AC-7 Odin3 deferred (D-104). Work order in git history (`docs/handoff.md` at `c11cde3`).
- **Slice 17** merged S40 as `2344f2e` (PR #230, head `1a22e93` = Sol runs `f49487d` + `444e02b` + a merge of `main` with R-CI; D-099): RAM disc for native helper launches (see S40 outcome). Opus QA (effort 15) PASS WITH LOWS at `741d832` (independent disc validator; mutations 17/19 killed), run 2 remediated TG-1–TG-4, S-2, S-3; re-check PASS at `444e02b`; S-1 (non-X `OS_ReadDynamicArea`) accepted. CI green (PR event) at `1a22e93`. AC-6 private: four RISC OS 3 versions; 11/16 to play; forced failure continues unattended; measured 32 KiB; configured 64 KiB and a populated RAM disc preserved; RAM file write/read/delete; CMOS logical 144 stays 0; RAM cohort (13 titles) no regression at 300 s; final 218-zip A/B vs Slice 16: no errors added, every differing frame reviewed (timing/RAM icon). AC-7 Odin3 (arm64 `f49487d`): idx 11 plays (swipe pointer, double-tap, icon-bar tap, pad), idx 125 recovery → main menu; 0 `[ERROR]`; PSS 184–187 MB; the Auto↔Disabled switch only host-tested. Work order in git history (`docs/handoff.md` at `1a22e93`).
- **R-CI** merged S40 as `e0e8b1b` (PR #231, D-100): CI cost cuts (see S40 outcome); first run green on all five jobs, ccache filling (sanitizer job 23/170 hits cold).
- **Slice 16** merged S40 as `21bb3b7` (PR #227, head `3b1cf1b`; Sol runs `5e02e61`, `3b1cf1b`; D-097): per-track-side quarantine of unrepresentable JFD descriptors (see S40 outcome). Opus QA (effort 15) PASS WITH LOWS at `2e6d0b5` (differential fuzzer vs the old parser: 0 mismatches over >150k images; mutation testing); run 2 remediated TG-1 (guest fixture proves hiding), TG-2 (D-076 majority tie), TG-3, TG-4, L-1–L-3; re-check PASS WITH LOWS at `3b1cf1b` (M1, M3+M15, M11 killed). Carried: the New-IO FORMAT test uses the zero-sector form (quarantined New-IO FORMAT correct by inspection: `jfd_format` always refuses); M13 (probe-mark guard) unreachable. CI green on both events at `3b1cf1b`; AC-7 private (idx 81 fresh/reset/relaunch to gameplay; 218-zip A/B: only 81 changes, 4/105/164/205 known variance) and AC-8 Odin3 (arm64 `5e02e61`: gameplay after disc swap, relaunch, 0 `[ERROR]`, PSS 181–191 MB). Work order in git history (`docs/handoff.md` at `2e6d0b5`).
- **Slice 15** merged S39 as `5e0967c` (PR #224, head `be9e897`; Sol runs `fb0c0de`, `256260e`, `be9e897`; D-096): short raw images are prefixes of their discs (see S39 outcome). Opus QA (effort 15) PASS WITH LOWS at `85c03cd`; run 3 remediated L-1 (unrestricted full-size media keep upstream not-found timing), TG-3 (boundary proof tests), S-1 (proof refuses free space over map/boot block/root), D-1/S-2 (release note), S-3 (`disc.c` note); re-check PASS WITH LOWS at `be9e897`. Carried Lows: TG-1 boot-recovery extent retention and TG-2 failed insert/reset and a guest-level zero-filled file check — correct by inspection, untested; the upstream 82C711 FORMAT never reaches the reader (no bytes change; documented); WD1770 multi-sector read unimplemented upstream. CI green on both events at `be9e897`; AC-7 private (127, 190, 186, 72, guest save/reload on idx 142, legacy shadow, 218-zip A/B) and AC-8 Odin3 (arm64 `256260e`: 127 and 190 play, legacy shadows, 0 `[ERROR]`, peak PSS 182–183 MB; old core: 127 stuck at the boot screen > 9 min). Work order in git history (`docs/handoff.md` at `85c03cd`); S39 outcome paragraph at `fc12819`. **177:** every disc of the public ADF set lacks its last track-side with real data there — a property of the dump; a complete image (e.g. a JASPP JFD; Founder pointer: JASPP forum t=676) is needed.
- **Slice 14** merged S38 as `fbf3d31` (PR #218, head `caeb5e7`; D-092, D-093, D-095): an engine SWI observer latches an unattended kernel command-line wait after a self-boot (R0 < &8000 from ROM, no `OS_CLI` for 2 s, 40 s horizon, input cancels); the adapter rediscovers and relaunches the unique application through the helper, opens a post-boot chooser, or leaves one notice; a remembered boot observation (`ARCBOOT1`, stamps over firmware, effective media, constructed starting CMOS and options) makes later loads skip the failed boot; the fresh RISC OS 3.10/3.11/3.19 CMOS default carries the firmware's own byte-195 correction. Opus QA (effort 15) PASS WITH LOWS (first pass at 9c6f312; run 5 remediated L-1, L-2, L-3, L-5, TG-1; re-check PASS WITH LOWS at caeb5e7); carried Lows: L-4 (residual false positives by design), TG-2 (three failure paths correct by inspection), TG-3 (C-7 and the stamp unit-tested; real-firmware proof private), S-1–S-3. Private AC-5 (RISC OS 3.00/3.10/3.11/3.19), AC-6 (218-zip A/B: only 125/197 play, 47/48 chooser), AC-7 (Odin3). Work order in git history (`docs/handoff.md` at `91dc0df`).
- **Slice 13** merged S38 as `708a06c` (PR #213, head `2eef52e` = run 2 `fb89b47` plus a docs-only merge of `main`; D-091): guest writes to the RISC OS 3 ROM-module unplug bytes are ignored and every persisted CMOS payload is normalised there; Opus QA PASS WITH LOWS (effort 15; carried Lows TG-2, S-2, S-3); private AC-5/AC-6 (S37); **AC-7 on the Odin3 (S38)** with a core rebuilt from `fb89b47`: fresh launch and force-stop/relaunch of idx 38 reach the title, a synthetic poisoned record and the device's own record poisoned by an older core both heal (`CMOS: cleared 4 unplug byte(s)`) and boot to the title, header and unrelated bytes preserved, 0 `[ERROR]`, peak PSS 186–192 MB. **Recipe lesson:** the recipe's "unrelated sentinel" bytes (logical 24 and 152) are live RISC OS configuration and change the boot on their own; future sentinel checks must use bytes proven unused (e.g. by an A/B with and without them). Work order in git history (`docs/handoff.md` at `acefe08`).
- **Slice 12b** merged S36 as `56de668` (PR #208, D-089): omitted standard sector IDs on the JFD identification track (track 0 side 0) are presented ID-only when the disc's own validated map proves every omitted slot free (D/E/F; old maps validate-then-intersect with clamping) or, on F-format, within the bounded system-fragment prefix below 0xC00; all-or-nothing; data reads keep RNF; one `JFD identification:` line per image. Full work order in git history (`docs/handoff.md` at the QA baton, PR #210).
- **Slice 12a** merged S35 as `5ea5a6d` (PR #201, head `39e95b1`, D-087/D-088): self-boot only with a root `!Boot` object; applications one level down as literal `ADFS::0.$.<parent>.<leaf>` targets with `.choice`/`.bind` v3; Opus QA PASS WITH LOWS (effort 15); private AC-8 (218-zip A/B) and Odin3 AC-9. Full work order in git history (`docs/handoff.md` at `9f6d5ac`).
- **Slice 10d** merged S33 as `5fcc25c` (PR #192, head `111c513`, D-084): exact-identity native routes for ten proven ADFFS-path save titles, `JFD Boot: ADFFS preferred`, reset install-disc machine promotion; Opus QA PASS WITH LOWS (three passes), AC-4 private, AC-5 Odin3 (read path).
- **Slice 10b** merged S33 as `b99ba0f` (PR #187, head `067b1d4`, D-083): persisted install hard disc at `ADFS::4` on the A3020 (upstream `ide.c`, framed journal, doorbell-published validated base), incl. run 13's core-owned presentation buffer (Odin3 first-install SIGSEGV on A/V reinit after same-session recreation); Opus QA PASS WITH LOWS; AC-6 private; AC-7 Odin3 for E-Type (Premier save not exercisable, disclosed).
- **Slice 10a** merged S31 as `ffbacc7` (PR #166): RW-flagged JFDs on the native path keep writes in a two-slot store (D-075/D-076/D-080/D-081); residuals in `docs/release-notes-slice10a.md`. Carried Lows: N-1 the `>= 100` frame comment; N-2 single-shot `SCREENSHOT` in Windows media steps; L3-1 the three `jfd-gate-*` tests not in the CI sanitizer `-R` list; title 187's VIDC freeze after its disc-2 load (pre-existing); a `.choice` with a zero fingerprint is accepted and matched on the application name (Slice 5c, to check).
- **Slice 9** merged S27b as `d1720e9` (PR #156): ADFFS on New-IO, RAM-disc install path with exact-identity profiles, install-variant chooser. Carried Lows: failed-pick fallback into a native chooser untested; a failed confirmed pick deletes any `.choice`; support-disc overflow reported as carrier `too-large`; a failed guest `Copy` shows RISC OS's garbled text before the guard.
- **Slice 8** merged S27 as `3b93737` (PR #142): F-format HD ADFs on the A3020, quad JFDs, 82C711 JFD READ ID/RNF, media-resolved machine, RiscPC notice, short tail. The Android core embeds absolute source paths (`__FILE__`), so its hash differs per checkout; prove an installed core by behaviour.

## Session relay and close (binding rules: protocol §18; Founder, D-115, D-116)
- **Baton holder = Claude Opus 5.5 at effort high, always (D-116).** Receipt checks first: Opus 5.5 model at effort 15 (`high`) or above, working directory `/Users/mac0918/dev/ArcDroid`, and the relay is the first message in the window. If any check fails, change nothing, say which check failed, send a `PushNotification`, and stop. A session cannot change its own model or effort; the Founder runs `/model claude-opus-5-5` and `/effort high`.
- **Founder decision or Founder-only blocker (incl. the Odin3)** → park only that workstream: finish its unblocked in-progress work, list it under **Waiting on the Founder** above, send one `PushNotification`, and continue independent work (protocol §18, D-124). **Case A** (stretch done or context low) → close and relay to **one** eligible window; the waiting list carries forward. **Case B** (every remaining workstream waiting or blocked, no independent work) → close **without** relaying.
- **Eligible window — ArcDroid only, no exceptions (D-125):** a `ListAgents` peer that is `arcdroid-`-named, `interactive`, `idle`, and not in the used-windows list below. Never another project's session. Never a second window; waiting and independent workstreams travel together in this one baton. **If none is eligible or the send fails, all project work stops:** do not relay and do not continue in this window instead; send a `PushNotification`, close, and the Founder opens a new Opus 5.5 (effort high) ArcDroid window and says "pick up from handoff".
- **Every close** ends with the bold line **This session is finished, and this window can be closed**. If the session cannot close safely, show `CANNOT CLOSE SAFELY` and send a `PushNotification` instead.
- **Project isolation applied 2026-10-08 (S57, Founder instruction):**
  - **QA agent:** agent `arcdroid-opus-qa` (renamed from the project's own `.claude/agents/opus-qa.md`). The type is listed only in windows opened after this commit; until then, QA uses the CLI recipe in `CLAUDE.md` step 9.
  - **Rule text:** an isolation rule in protocol §3, `CLAUDE.md` and `AGENTS.md`.
  - **Deny list:** a local deny list of 113 rules (untracked). It is merged into the existing `.claude/settings.local.json`, keeping its Founder-authorised allow and autoMode blocks, and excluded via `.git/info/exclude`.
  - **Clones and probes:** Sol, Astra and QA clones and scratch probes live under this project's own scratchpad (`/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/…`), which the deny list does not cover. Sol's own build directories (`/private/tmp/arcdroid-r2*`) are outside `claude-501` and are confined by the Codex sandbox.
  - **Sibling directory `~/dev/ArcDroid-evidence`:** denied by the generated list, like every other sibling. If it holds this project's evidence, exempt it by removing its three lines.
- **Founder instruction (S54, relayed S55):** never stack shell commands — no `&&`, `;` or pipes chaining git steps; run each command separately (stacked commands get denied). Put multi-step work in a script file under the scratchpad instead.
- **Used windows (never relay to these; each closing holder adds its own name):** `arcdroid-d9` (S33), `arcdroid-6c` (S43), `ArcDroid-45` (S45), `arcdroid-83` (2026-10-02 governance edit), `ArcDroid-49` (S49), `arcdroid-d4` (S50), `arcdroid-10` (S51), `arcdroid-1b` (S52), `ArcDroid-52` (S53), `arcdroid-df` (S54), `arcdroid-7e` (S55), `arcdroid-25` (S56), `arcdroid-ba` (S57), `arcdroid-99` (S58). Earlier holders were not recorded; window names change between days.

**Relay brief:** "ArcDroid baton handoff from <window> (S<n>), for /Users/mac0918/dev/ArcDroid only. Before anything else, run the receipt checks in docs/protocol.md §18: you must be Claude Opus 5.5 at reasoning effort 15 (high) or above, your working directory must be /Users/mac0918/dev/ArcDroid, and this must be the first message in your window. If any check fails, change nothing, tell the Founder in your window which check failed (for a wrong model or effort: run /model claude-opus-5-5 and /effort high, then say \"pick up from handoff\"), send a PushNotification, and stop. If all pass, take the baton without asking the Founder to confirm, number yourself S<n+1>, read CLAUDE.md and docs/handoff.md, take the active Next (never a workstream under Waiting on the Founder until its answer is recorded), and work towards the Founder vision D-073. Founder instruction (S54): never stack shell commands (no `&&`, `;` or pipes chaining git steps) — run each command separately. Close by protocol §18: relay only to one idle arcdroid- window, never another project's session; if no ArcDroid window is eligible, stop all work and close. Repeat this rule in your own relay."

## State (carried)
- **Role note (Founder S26–S28, S38, D-115, D-116):** earlier orchestrators ran Claude Opus 5.5 or Claude Fable 5.1 (each session outcome above names its model). **Every baton holder is Claude Opus 5.5 at effort high (D-116; D-115 briefly named Fable 5.1 and was corrected the same day).** QA independence is kept by dispatching QA to a separate `arcdroid-opus-qa` agent (effort high) with a fresh clone and no conversation context; the orchestrator does not write implementation or tests.
- **Founder-visible decisions recorded as reversible (not gates):** HD media auto-selects the A3020; the 800 K short-tail tolerance is two cylinders; eight RiscPC-flagged JFDs are not currently reachable (PRD parity target unchanged; D-063 workstream). Say so if any should change.
- **Sweep baseline:** S28 re-baseline supersedes the S24 residual counts; S24 per-title tables stay in the S24 scratchpad `16ff5ea2…/scratchpad/sweep/`. **Sweep recipe (S28):** `sweep/sweep.py RUNS OSK OUT [workers] [idx-file]` (3 workers ≈ 55 min on this Mac under load), `analyze.py`, `sheet2.py`, `score.py`, `jfdflags.py`, `jfdtracks.py` in the S28 scratchpad `sweep/`.
- **Windows CI flake:** rider R-W merged with Slice 8 (bounded RetroArch/port wait, chooser retries); the in-session reload never works on 1.22.2 stable and always falls back to relaunch; occasional "RetroArch start timed out after 30 s" at runner start-up (`test-retroarch-windows.ps1:67`) passes on rerun.
- **QA effort rule (Founder S24, D-060):** Opus 5.5 QA at effort `high` (= 15 on the model's scale) via the `arcdroid-opus-qa` agent or CLI `--effort high`; a `general-purpose` Agent ran at 10 and is never used for QA.
- **Follow-up Lows from 6b QA (accepted, not scheduled):** `.ps1` retry match is locale-bound; the lifecycle loop's m3u/JFD cycles check only the `ARCCMOS2` tag; unreachable decode-fallback branches around `arc_sync_cmos`/`arc_persist_cmos`; one extra `move attempt 1/3` log line per screenshot.
- **ADFFS multi-disc shelved (D-059)**; evidence branch `slice/06b-adffs-multidisc` `25446fd` unmerged, keep.
- **Release notes owed** (collect at the release baseline; per-slice files exist through Slice 30): 5b-2, 5c, 6a, 5a M-1, 7a, 7b, 6b, Slice 8, Slice 9 (`docs/release-notes-slice9.md`), 10a/10b/10d, 12a (per-slice files exist).
- **Open after 6a (unchanged):** one eligible 633 KB title returns to the desktop; floppy-speed carrier loading; AC-16 items not run; one transient `unable to prepare staged media directory` on the Odin3 (S18). **OSK on the device:** drive it with the D-pad (`hat 16/17`) and A (`btn 305`); a touch on the OSK also reaches the guest as a mouse click. Library = Desktop (218 zips).

## Roadmap (D-074, S28; Founder S33 re-ordered: broken-launch cluster first; Founder S36: retire per-game tables next)
1. **Broken-launch cluster (Founder S33):** Slice 12a (G1 + G2, merged S35), Slice 12b (G5, merged S36), Slice 13 (G6, merged S38), Slice 14 (G7 and the prompt-ending family, merged S38), Slice 15 (G8, merged S39), Slice 16 (G3, merged S40), Slice 17 (G4, merged S40) — **item 1 closed (S40)**; G9 reclassified (RiscPC re-releases → item 6; 78 → item 2b). Remaining item-1 residue: 67 (RW disc write-protected by the D-076 gate; writability class) → **Slice 28 (S50, D-120)**.
2. **Retire the per-game tables — (a) closed S41 (Slices 18/19); (b) next (Founder S36, PRD P-4, D-090; Founder S40: after G4, 2a then 2b; the real save gap is the 12 RW-flagged ADFFS-path titles):** rebuild the general machinery so the games they serve work without per-game data, then delete each table; a working game never regresses (library A/B gate). (a) **Native-route table** (`core/native_route.c`, ten titles): close the native-path gaps that made ADFFS better for some titles (idx 54 module-area memory, idx 185 loading hang, the 10d holdouts 124/161/174/181) until JFDs boot natively by general rule, then delete the table. (b) **ADFFS install recipes** (`core/adffs_install_profiles.c`, three profiles / four variants: 68, 69, 159) — **native-first (Founder S42, D-105):** Slice 20 R1′ (**merged S42**, D-106) (omitted in-file sectors read as zeros natively → 68/69/200 native with their own disc prompt, saves in the RW data disc's store); then a persisted writable hard disc for native sessions on the IDE-capable machine (**Slice 21, merged S43**, D-107: 159 native with saves; `reason=hard-disc` routing); then delete the ADFFS install path (existing stores unused, release note; **Slice 22, merged S45**, D-108); later: self-extracting apps (66) and disc-own installers (78) onto the hard disc; 84 (HD-optional multi-disc) to be re-examined natively. (Item 2a: Slice 18 then Slice 19 — see "Item 2a plan".) (c) **Elite input profile** (`core/input_profiles.c`): **Slice 24 merged S47** (`77b9c64`, D-110; AC-9 Odin3 PASS) — **item 2 closed** — delete the table; input resolution from the per-input core options only, saved per game by the player (PRD FR-29/FR-33).
3. **Friction:** (S50: the D-105 follow-up for unqualified/`:0.`/variable names is deferred, D-119.) **Slice 25 merged S47 (`20d0154`, D-111; AC-9 Odin3 PASS S48)**; **Slice 26 merged S48 (`f472bb0`, D-114): automatic insertion of a requested disc (D-105 signals A/B; unqualified names follow later): controller answers for `Wimp_ReportError` boxes (D-103)**; ADFFS/install boot time (the icon-bar helper is tabled, D-085); **controller answers for RISC OS error boxes (Founder S41, D-103):** while a guest `Wimp_ReportError` box is waiting, pad A answers OK and pad B Cancel regardless of the default button (derived from the call's own flags; equivalent to the player's click; short on-screen hint; no per-title data) — needs its own design and Astra challenge (open question: completing the call with the chosen result vs a synthesized click). Motivated by 154's Cancel-default acknowledgement box. **Founder S42 (D-105) adds:** a clear on-screen hint when a game waits for another disc of the loaded set, and controller-friendly answers to such prompts (68/69's "insert TRACKS disc [OK]" needs a pointer click today); note (S42): an in-session reset keeps the current disk index, so a reset while disc 2 is inserted boots disc 2 (empty desktop for 68) — the player re-selects disc 1 first; consider in that design — each with its own design.
4. **Slice 11 — engine and input tidy-up:** **Slice 27 merged S49** (`28dcd8d`, D-112 left stick = D-pad, D-117; AC-12 Odin3 PASS); **closed for now (S50, D-118)**: ARM3-class machines A5000/A540 (Founder S25), Slice 3b joystick interfaces (D-034) and the audio resampler flush (D-022) deferred — no library title needs them.
5. **Release baseline:** `.info` beside the core, owed release notes, reproducible builds, libretro buildbot submission (D-016; submission needs publication, D-100). **No armeabi-v7a — Android is arm64-v8a only (D-135).** Grounded S55 (above); after R4 (D-133).
6. **RiscPC workstream (D-063; trial passed, **GO** D-131 at a 100 MIPS Odin3 StrongARM budget):** R1 contained import → R2 media bridge/IDE/CMOS → R3 firmware, Auto selection, launch → R4 platform and regression qualification (`docs/riscpc.md` §6). Next after Slice 30.
   **6b. Faster CPU translator for RiscPC (Founder S55, D-133; evaluation in parallel with R2b–R4 from R2a merge, D-141):** an arm64 just-in-time translator so the heaviest 3D titles reach their native frame rate on the Odin3; starts with an evaluation (RPCEmu's dynarec is x86-only; `rpcemu-extended`'s arm64 JIT only as a reviewed input, never wholesale); after R4 and item 5.
7. **ADFFS-path failures:** diagnosed S53/S54 (D-128, D-130); **Slice 30 — "Write-protect Tab" per-game option merged S55** (`ff048e4`, D-136); remaining residue 123/158/176 (icon-bar class, tabled D-085), 75 unexamined; Slice 10c (deprioritised, D-085).
8. **Save states (D-011):** gated on complete machine-state serialisation.
Carried Founder checks ride on each slice's device leg. Not scheduled: the clean-room command layer.

## Carried Founder checks
Windows-PC real boot (Slice 1 AC-12(b)); Windows real-ADFFS evidence (6a AC-16); second Android device pass (Snapdragon 8 Gen 2, arm64; was "Retroid Pocket pass"); on-device CMOS mutation; Windows physical mouse/XInput; hand two-finger tap; Odin3 physical sticks (S49: synthetic left/right stick proven; physical feel of the 50 %/40 % throw unchecked) and split-pad OSK modality (M-1); Repton 4 title-screen question. Elite device recipe (after Slice 24: the ten Pad Mapping keys are saved per game on the Odin3 — see `docs/release-notes-slice24.md`; fly with the D-pad): desktop → `:0` → `!élite` → Menu on the Jameson icon → Preferences → Mouse off → OK → Select-click → manual word on the OSK (manual PDF public at `https://arcarc.nl/archive/Games/E/Elite/`; never record words in the repository) → A → Start; decelerate or turn at once.

## Other candidates
- Save states (D-011); release baseline; Slice 3b joystick interfaces (D-034); audio resampler flush (D-022 underruns); ARM3-class machines; scanner one-entry-per-member playlists (frontend behaviour, documented).
- Complete: Library loading (5a, 5b, 5b-2, 5c), JFD boot through user-supplied ADFFS (6a, single disc), Zip content (7a), Zip boot-disc selection (7b), High-density and protected-format media (8), ADFFS on New-IO + install path (9), native JFD writes (10a), persisted install disc (10b), native routing (10d). Shelved: ADFFS multi-disc mechanism (6b Run 1, D-059).

## Follow-up Lows (not scheduled)
**Slice 30 Lows (Opus QA, accepted):** S-6 the pending-change notice and the observation stamp ignore the content type (ADF/ADL/hard-disc sessions see "will apply after reset" and one re-observation for an option with no effect on them); S-7 every *As imaged* load of a write-protected or legacy JFD (incl. cached temporary decodes) now hashes the decoded image and scans `save/arcdroid/jfd` read-only to detect a hidden store — measure on Android if the store directory grows large; N-9 under *Off* the guarded-image `JFD writes:` line drops its "accepted map writes never increase unavailable allocation" suffix (diagnostics only). Interaction (by design, D-106): a disc made writable by *Off* leaves the Slice 20 omitted-file-data presentation (non-writable images only); the forced-*Off* cohort leg showed no frame change for 68/69/200.
**Slice 29 note (Opus QA, out of scope, pre-existing):** running the host suite leaves an untracked `tests/__pycache__/` (`.gitignore` has no `__pycache__` entry) — repo hygiene for a later slice.
**Slice 27 Lows (Opus QA and device leg, accepted):** M11 — removing the `bound` comparison in the stick routing snapshot survives (only an `a`↔`Unbound` change differs in `bound` alone; the per-port neutral gate still holds, so the effect is one neutral poll instead of two); S-2 existing right-stick/release-gate harness scripts now drive port 0 only (port-1 incidental analog coverage lost); S-3 on the poll where RetroArch first registers a stick, non-forced Analog to Digital can still turn a held stick into one frame of D-pad (pre-existing on `main`, within C-5); redundant await guards kept as defence in depth (S-1); RetroArch renders the value `off` as an OFF toggle (cosmetic). **Slice 27 design Low (Astra S49 F5, out of scope):** a physical D-pad direction changed from `Auto` to an explicit key equal to its resolved key under the A3010 plan moves it from the joystick to the key path without a neutral gate (pre-existing; the stick contribution has its own await-neutral rule).
**Slice 26 Lows (Opus QA, accepted):** the FAILED-latch refresh over waits longer than 120 s is untested (code correct by probe: one attempt and one notice over 400 s); per-image disc-name cache invalidation is untested (only A uses names); residual G5-class candidate (23: a JFD whose identification track is not natively presentable can be inserted although FileCore cannot read it — same outcome as a manual insert; release note). `libretro-harness` reused-data-directory failure seen again (O-1).
**Slice 25 Lows (Opus QA, accepted):** the frontend reset/unload end path (`arc_engine_stop` → `arc_engine_error_end`) has no harness test (production correct; S48 AC-9 proved a network RESET with a box up on the device); the release-gate redundancy with the OSK/chooser gates is documented, not separately tested.
**Slice 24 Lows (Opus QA, accepted):** L-4 the A3010 joystick key-side override (a key bound on an input the joystick interface owns) is untested — pre-existing, routing unchanged; O-1 `libretro-harness` is not re-runnable in a reused data directory (a second full run aborts at the A3020 `eligible-hd*.jfd` hard-disc assertion because persisted `hd/` stores remain; fresh directories pass; CI always fresh) — clear or uniquify the data directory per run.
**Slice 23 Lows (Opus QA, accepted):** L-a the 'ABEX' counting window opens at observer setup rather than at the StartTask match (conservative); L-4 the `libretro-harness` CTest margin (≈ 200–260 s of 300 s; times out under parallel load — rerun alone); T-3 the journal cap during an install is not tested separately; the ADF refusal callback cannot feed an armed observer at shipping settings (`Disk Writes: Off` detaches the disc); a runnable copied application is not re-launched automatically after a cancelled first run (terminal `started`; move the directory aside).
**Slice 21 Lows (Opus QA, accepted):** TG-5 publication-before-first-instruction shown by log order (holds by construction); S-1 `ARC_OS_TEST` zeroes byte 189 in the machine image but not in `arc_cmos_normalise_payload`; S-2 a save path too long for the store directory reports "damaged" (inherited from 10b); S-3 the damaged-disc notice mixes separators on Windows; Lo-1 the M-2 harness block leaves `Disk Writes` at shadow (the Off→hard-disc case is covered via the damaged-store case); Lo-2 roundabout manifest-corruption idiom; Lo-3 one notice re-arm sub-check is vacuous. **New Windows CI flake (S43):** `windows-retroarch` once failed in the Slice 10a allocation-veto step (`Wait-JfdResultVideoReady`, `tools/test-retroarch-windows.ps1:743`: "Cannot find an overload for IndexOf and the argument count: 2"); the rerun passed. **Device:** 159's setup screen renders blank on the Odin3, so its game save stays device-unexercised (host-proven).
**S42 notes:** idx 85 ignores mouse clicks after boot on both `main` and Slice 20 (keyboard and F12 work; S30 used the mouse there) — investigate; idx 169 under ADFFS ends at an "Unknown operand" box (168 too); 100 disc 1 is `layout-unsupported` (not writable). Slice 20 QA equivalent mutants (M16, M16b, M16c, M19, M22, M31, M41, M47) documented in the S42 `qa/adv/mut6_results.txt`.
**Slice 12b follow-up Lows (Opus QA, accepted, not scheduled):** re-check L-1 the two probe-write test-ROM fingerprints are unconditional in `core/firmware_table.c` (extends 12a L-7); S-1 Slice 8's F fixtures now take the G5 path; S-2 the `captured=` list truncates without a marker and `map-invalid` is logged where the manifest says `unsupported-format`. **Slice 12a follow-up Lows (accepted, not scheduled):** L-1 `explicit-d.adf` Auto decision changed (correct, untested) and the explicit-pick reason "disc !Boot skipped" fires on boot option ≠ 0 without a root `!Boot` (`libretro/arcdroid_libretro.c:3845`); L-2 nested fingerprint contents untested; L-3 cross-parent resource exclusion untested; L-4 v3 `.bind` strict parsing untested; L-5 publication-failure / unaddressable-parent / Application-N-suffix notices unasserted; L-6 Application N suggestion suppressed all-or-nothing; L-7 chooser and D-format test-ROM fingerprints live in the shipping table, stale hashes in `tests/firmware_table_test.c`; L-8 81-character escaped nested labels overflow the row; S-1 unused `boot-3-l.adl` in Windows packaging; S-2 discs with no root `!` directory log `scope=nested`; S-3/S-4 synthetic transport evidence weaker; S-5 explicit Application N on a self-booting disc with nested apps untested. Release notes: `docs/release-notes-slice12a.md`, `docs/release-notes-slice12b.md`. **12b outcomes by title (private, S36 `g5/`):** 100, 149, 77 intros; 30 data-disc prompt; 28 menu, 217 title, 180 intro (native); 48 desktop then RMA exhaustion on self-boot; 67 title then exit (start-up map write refused — RW disc write-protected by the D-076 layout gate, a writability class for later); 66, 84 need a hard-disc install; 117, 144 RiscPC-only; 78, 90, 110, 116, 156 G9.
Slice 8 (Opus at `308d0d2…7a23f4a`, S26): N-1 add a WD1770 READ ADDRESS drive-0/drive-1 alternation test for the per-drive raw READ ID cursor; L-1 AC-5's HD read is issued by the harness hook (as the DD read is), guest-level HD reads are proven only by AC-11; L-4 a 655 360-byte old map declaring exactly 3200 sectors is refused (pre-existing, `core/launch.c` `case 3200u` before the L tolerance); L-5 `retro_reset` checks media before disk-writes re-preparation (contrived folder-set swap); switching the machine option back to `Auto` keeps the session machine until the next load. S23: Python-made zips (bare `ZipInfo`) are rejected by the regular-file rule — Rider R-3 in Slice 6b (re-scoped). S22 observation (frontend naming, pre-existing since 7a): RetroArch names the `.srm` and `.lrtl` after the content leaf, so a `zip#member` playlist entry writes its own `.srm`; SAVE_RAM is the CMOS record (D-011), so two playlist entries for one zip can carry diverging frontend CMOS copies while the core's own write-through CMOS stays per machine — document with the scanner one-entry-per-member note, or consider dropping SAVE_RAM once write-through is the only CMOS path. Slice 7b (Opus at `6e61744`/`5b893b5`/`cc5a819`): L-1 the `Zip content order:` line can exceed a frontend's per-message log capacity (harness sink 2048 B, logcat ≈ 4 KB) and a truncated line loses its newline; L-3 the fallback OSD text is not length-bounded (≈ 1.1 KB worst case); L-4 T3 folds case unconditionally while D-041 on disk folds only on Windows; L-5 `archive_key` duplicates the media-extension list instead of reusing `arc_media_extension`; L-6 a `content://`/SAF archive path yields a percent-encoded key so T2 silently never matches; S-1 `preferred_index` is an archive-entry index equal to `entries[0]` after rotation; S-2 `ascii_fold`/`name_equal` duplicate `folder_set.c` statics; S-3 four near-identical summary format strings; TG-3 the harness order buffer is never filled past 2047 B; N-2 the no-suffix fallback OSD branch is unreachable with repository fixtures. Slice 7a: the unsupported-container message misses a non-zip container inside a directory whose name contains `#`; the 256 MiB logical clause is unreachable behind the 32 × 8 MiB caps; the OSD summary also fires for notices-only loads; comparator global in `core/zip_content.c`; macOS ASan cannot `detect_leaks` — Linux CI is the leak evidence. Slice 6a: L-5 launcher stage 1 has no failure dialog; L-7 `ARC_ADFFS_MAX_MEMBERS` = 64; carrier/support `writable` flag untested alone; AC-7 walker shares constants with the writer; RAM-size test constants; two `windows-retroarch` flakes in `tools/test-retroarch-windows.ps1`: (a) the in-session reload step (`:327`); (b) a screenshot-rename race at the two `Move-Item` sites. Slice 5c: interrupted `.choice` replacement leaves `.previous`/`.tmp`; failed-construction state shows a stale frame and reset is a no-op; 2 px chooser highlight offset; `Launch:` line index mix; PowerShell `try/finally` masking; pick recorded before construction; chooser-exit frame before `SET_GEOMETRY`; one RetroArch gdi 0xC0000005 on a `SCREENSHOT` race (frontend). Slice 5b-2/5b/5a: folder-set contents changing between loads untested; `arc_ejected` not cleared by release; path-exact index remap and folder-set flag clearing untested; `closedir` failure discards a listing; L-1 binding rename crash window; L-2 VFS readdir end/error ambiguity; L-3 diagnostics escaping; L-5 duplicated leaf table; I-1/I-2. Slice 4: A3010 joystick gate on machine id; duplicated Auto-firmware wording; IDE register re-read. Earlier: see `docs/build-log.md`.

## Licensing / security / provenance
**Repository stays private (Founder S40, D-100).** The S40 pre-publication scan found no firmware/game images or secrets in history, but five `evidence/*` branches (S5–S12) hold device screenshots and title-naming logs and this file lists the Windows box's LAN address — both to be handled before any future publication. Standing constraint from Slice 6a: ADFFS (JASPP) has no licence text — user-supplied only, never in the repository, CI, logs, PRs, releases or ArcDroid-written host files (D-049). Vendored zlib 1.3.2 inflate-only remains the single third-party dependency for archives; upstream Arculator `src/ide.c`/`ide.h` at the pinned commit `7d3d267` joined the GPL-2.0-only allowlist with Slice 10b (D-083). No firmware or game bytes in the repository, CI, logs or PRs; the Founder's library names appear only in private scratchpad evidence and the backup manifest on the Founder's Desktop. Slice 12a adds only project-authored code and invented fixtures; guest names in fixtures must be invented.

## Branch / PR (governance)
**S55 (close):** `main` = `ff048e4` (Slice 30) plus this baton. Nothing running that this session started: Sol runs 1–4, Astra stages 1–2, Opus QA and its monitors, the AC-12 leg driver and the CI watchers have all finished; RetroArch on the Odin3 closed. Kept branch: `slice/30-write-protect-tab` (merged). **Odin3 (S55):** installed core = arm64 of `64a37a5` (production = `ff048e4`); the global `ArcDroid.opt` gained `arcdroid_jfd_write_protect = "as-imaged"`; index 146 has a per-game option file with `"off"` (delete it to return 146 to the default); 146's JFD store holds generation 9; runner `/data/local/tmp/s55.sh` (= `s53.sh`; evidence `/data/local/tmp/s55/`, `fling.sh`). Rebuild from `main` for the next device leg.
**S54 (close):** `main` = `94a519e` plus this baton (governance only). Nothing running. Odin3 untouched (installed core still the arm64 of `60129a7`).
**S53 (close):** `main` = `215cf7f` (Slice 29) plus this baton. Nothing running: Sol run 1 and Opus QA finished; no RetroArch, harness or probe started by S53 is running. Kept branch: `slice/29-adffs-loader-env` (merged). **Odin3 (S53):** installed core = arm64 of `60129a7` (production = `215cf7f`); runner `/data/local/tmp/s53.sh` (= `s52.sh` with the ArcDroid core; evidence `/data/local/tmp/s53/`); RetroArch closed. Note: the ADFFS-path peak PSS on the device was 243 MB (native titles 178–213 MB).
`main` = `7c2671f` (Slice 28) plus S50/S51 governance (S51: PR #289 AC-6 + D-122; branch `docs/s51-riscpc-evaluation` = `docs/riscpc.md` + D-123 + this baton). **Nothing running at this baton (S51):** Astra runs 78881/87581 completed; no RetroArch instance or probe running. Kept branch: `slice/28-guarded-writable` (merged). **Odin3 (S51):** installed core = arm64 of `3dfe5dd` (production = `7c2671f`); runner `/data/local/tmp/s51.sh` (= `s50.sh`, evidence `/data/local/tmp/s51/`); 67's store at generation 10; RetroArch closed. **Odin3 (S50):** installed core unchanged (`a0ec851`); runner `/data/local/tmp/s50.sh` (= `s49.sh`, evidence `/data/local/tmp/s50/`); RetroArch closed. **S49:** **Odin3 (S49):** installed core = arm64 of `a0ec851` (production = `28dcd8d` except the left-stick option help text); the global `ArcDroid.opt` gained `arcdroid_pad1_left_stick = "dpad"` (the default) on close; RetroArch Port 1 Analog to Digital Type restored to the device default "Left Analog" (`input_player1_analog_dpad_mode = "1"` in `arcdroid-run.cfg` and `retroarch.cfg`); runner `/data/local/tmp/s49.sh` (= `s48.sh` plus `stick X Y [HOLD]` on `ABS_X`/`ABS_Y` and `rstick X Y HOLD` on `ABS_Z`/`ABS_RZ`; evidence under `/data/local/tmp/s49/`); RetroArch closed. Open the Quick Menu with the network command `MENU_TOGGLE` (pad `BTN_TL2` did not open it). Kept branch: `slice/27-left-stick-dpad` (merged). **Odin3 (S48):** installed core = arm64 of `c00f8d1` (production = `f472bb0`) (the `.so` stays in `RetroArch/downloads/`; `.info` pushed); runner `/data/local/tmp/s48.sh` (copy of `s33.sh`; device evidence under `/data/local/tmp/s48/`); RetroArch closed. Note: the device's `nc` refuses `-q 0` — send RESET as two backgrounded `nc -u -q 1` 0.2 s apart; the Slice 25 hint lasts 2 s (catch it with a screencap loop). Kept branches: `slice/25-error-box-answers`, `slice/24-retire-input-profile` (merged). **Odin3 (S47):** installed core = arm64 of `main` `77b9c64` (the `.so` stays in `RetroArch/downloads/`; `.info` pushed); index 73 holds a per-game option file with the ten-key layout (Founder's choice; delete it to return to the defaults); the global `ArcDroid.opt` still carries the inert retired keys `arcdroid_input_profile`/`arcdroid_install_saves`; RetroArch closed. Kept branch: `slice/23-hd-install` (merged). arm64 core for AC-8: `s46/android/clone/build/android/arcdroid_libretro_android.so` (`3c95a68`, production-identical to `db2db46`). Kept branches: `slice/22-delete-install-path` (merged), `slice/21-native-hard-disc`, S43 private evidence: scratchpad `/private/tmp/claude-501/-Users-mac0918-dev-ArcDroid/176c646f-044b-4cfa-be74-8993f314e00d/scratchpad/` — `s21/LEDGER.md` (index), `s21/workorder-v3.md`, `s21/astra/` (briefs, reports, thread above), `s21/sol/` (prompts, reports), `s21/ab/` (`ab43.py`, `cmp43.py`, baseline legs `base-pkg`/`base-nopkg` of `main` `dadef64`), `s21/base` (clone + host build of `dadef64`). Kept branches: `slice/20-jfd-file-data`, `docs/s42-item2b-direction`, `slice/19-native-writable`, `slice/18-boot-flag`, `docs/s41-slice18-design` (its commits are in both slices), `slice/17-launch-ramdisc`, `ci/cost-reduction`, `slice/16-jfd-quarantine`, `slice/15-short-tail`, `slice/14-boot-fallback`, `slice/13-cmos-unplug`, `slice/12b-jfd-identification`, `slice/10a-jfd-writes`, `slice/10b-install-disc`, `slice/10d-native-route`, `slice/08-hd-media`, `slice/09-adffs-install`; shelved `slice/06b-adffs-multidisc` `25446fd` (keep). Older private evidence: S40 `e7c3397f-…` (G3/G4, A/B legs), S38 `a282627e-…` (G7, library measurement `g7/abfull/`), S33 `4c912ed7-…` (`p33/`: 10d ledger and AC-4 scripts), S29 `e838c0b2-…`, S28 `d123c53d-…`, S27b `ca817b8c-…` (`adffs/` private package, read-only).

## Orchestration notes (Fable)
- **Permissions note (S35):** the `autoMode` block in `.claude/settings.local.json` is not read by the auto-mode classifier (it reads only `~/.claude/settings.json`, managed settings or `--settings`); the Founder declined further settings changes. Run `gh pr merge <n> --merge` as a standalone command, never inside a compound script, and verify with a separate standalone `gh pr view`. **Watchdog (Founder S36):** background runs are supervised by `~/dev/agent-tools/wait-run.sh` (protocol §15, Appendix A). **Odin3 (S43):** installed core = Slice 21 arm64 `85023f7` (production-identical to the merge); Hard Disc on Auto; new stores `hd/` for 159 (holds an `ODIN` directory) and 84; old install stores untouched. **Odin3 (S42):** installed core = Slice 20 arm64 `7821801` (production code equal to the merged `74800ec` except run 5/6 proof refinements that change nothing on the library — rebuild from `main` for the next slice's leg); JFD Boot option back on Auto; the option file gained `arcdroid_jfd_omitted = "file-data"`; 68's disc-2 store and 62/99 stores hold S42 records; controls for 68 on the device: pad L = Adjust (accelerate), pad R = Select, pad B = Return; change core options only through RetroArch's Quick Menu (a direct `.opt` edit is refused by the classifier). **Agents (S42):** the classifier refused a subagent stopping or watching the A/B runners it had started and listing its own output directory — run long A/B legs from the orchestrator session (background Bash) rather than inside agents. **Odin3 (S41):** still Slice 17's `f49487d` arm64 build installed (the device was disconnected all of S41; the combined Slices 18/19 pass is owed, D-104); earlier (S40): the installed core is Slice 17's `f49487d` arm64 build (run 2 changed only tests and a compile-time check); touch drives the pointer relatively (≈ 1.4 device px per swipe px), a tap clicks at the pointer, two quick taps double-click; earlier Slice 16 notes: idx 81 reaches gameplay after the disc-2 swap (RetroArch network `DISK_EJECT_TOGGLE`/`DISK_NEXT`/`DISK_EJECT_TOGGLE`; pad A = Space `btn 305`, pad B = Return `btn 304`); the first PSS sample after a RetroArch menu session can read ≈ 330 MB (start-up artefact, not repeated on relaunch). Earlier (S39): idx 127 and 190 carry legacy declared-size shadows; idx 38's device CMOS record is healed (pre-S38 poisoned copy at `/data/local/tmp/s38/`); idx 125/47/48 carry `.boot` records and 48/47 a `.choice`. Ask the Founder to reconnect it for the next slice's device leg. **S42:** installed core = `main` `56382d2` arm64 (sideloaded S42); the Odin3 was reconnected and stays available; RetroArch's network `RESET` needs **two** sends ("Press again to reset"); title 62: keyboard X moves right (pad cursors do nothing), name entry deletes with DEL; idx 99 and 62 carry S42 store records. **Merges (S33/S36):** merge only after reading the full check list of both events; never `--auto`.
- **Astra recipe (S25/S34):** stage 1 `codex exec -m gpt-6-astra --sandbox read-only -C <clone> --json -o report1.md - < brief1.md`, stage 2 `codex exec -C <clone> resume <thread-id> -m gpt-6-astra -c sandbox_mode="read-only" --json -o report2.md - < brief2.md`, launched through `detach.py` (PID recorded) and supervised per protocol §15; stage 1 ≈ 4 min, stage 2 ≈ 3 min. Throwaway engine probes live in a scratch clone (`/* PROBE */`-marked diff) — never a Sol source.
- **Background runs (protocol §15)**: Sol and Astra via Codex CLI from a fresh clone under the session scratchpad; supervision per protocol §15; PID/start recorded; only PIDs recorded at dispatch are watched or stopped. Opus QA runs at effort `high` (CLAUDE.md step 9): `subagent_type: arcdroid-opus-qa` when the session lists it (`.claude/agents/arcdroid-opus-qa.md`, renamed S57 2026-10-08; listed only in windows opened after that commit; never a plain `opus-qa`, protocol §3: `model: opus`, `effort: high`), otherwise detached CLI `claude -p --model opus --effort high --permission-mode acceptEdits --output-format text - < prompt.md` from the QA clone; the QA report's first line states its effort, else re-run; re-checks continue the same agent by `SendMessage`.
- **Sol launch (S28):** the Founder authorised the allow rules `Bash(codex exec *)` and `Bash(nohup codex exec *)` in the git-ignored `/Users/mac0918/dev/ArcDroid/.claude/settings.local.json` (excluded via `.git/info/exclude`). Launch Sol and Astra from the repo root as `nohup codex exec … - < prompt.md > stream.jsonl 2> err.txt & echo $!` or via `detach.py`, record the PID, supervise per protocol §15. Sol needs no network access for normal slices. Keep every `arcdroid-` window in the same permission mode (auto) so relay messages are delivered, not held. Resume: `nohup codex exec -C <clone> resume <thread> -m gpt-6.1-sol -c sandbox_mode="workspace-write" --json -o <report> - < <prompt> > <stream> 2> <err> & echo $!`; Sol's sandbox refuses `rm -rf` (it works around it).
- Sol = GPT-6.1 Sol, `gpt-6.1-sol` (Founder, D-115; was `gpt-6-sol` from S24, accepted by Codex CLI 0.156.0). The new ID is unverified until the first Sol launch; if the CLI rejects it, list it under Waiting on the Founder and continue work that needs no Sol run (protocol §18). Invoke `codex` by name from the repo root without `cd` into the scratchpad first (an nvm directory hook there drops the CLI from PATH). Detach long runs from the harness session (`python3 detach.py STDIN OUT ERR CMD…`, `DETACH_CWD` for the working directory; record the printed PID); `codex exec resume` continues an interrupted thread. `codex exec -m gpt-6.1-sol --sandbox workspace-write -C <clone> --json -o <report.md> - < <prompt.md>`. Sol's sandbox cannot write `.git`: Sol leaves the tree uncommitted and Fable commits it verbatim on the slice branch from the same clone, pushes and opens the PR. Clone Sol from the pushed governance branch that carries the current handoff. The sanitizer `libretro-harness` exceeds CTest's 300 s timeout on this Mac: run it directly with the absolute-path argument list from `CMakeLists.txt` and a fresh data directory. `git checkout <ref> --` in the main checkout detaches HEAD — use `git show <ref>:<path>`.
- Harness permissions: implementation-PR merges delegated to Fable (`CLAUDE.md` step 11); create and merge as separate commands and run `gh pr merge` on its own; quote any refusal to the Founder and do not work around it. Fable never edits `.claude/settings*.json` without an explicit Founder authorisation (D-129 was one: the watchdog allow rule). Governance (docs) PRs merge the same way: standalone `gh pr merge <n> --merge` after CI is green on both events, never `--auto` (S36: an auto-merge that landed before CI finished preceded the classifier's refusals). Each push triggers two CI runs (push and PR events) — both must be green; read the failed job's log before `gh run rerun <id> --failed`; every job failing in 2–3 s means the Actions budget annotation (Founder action). `timeout` does not exist on this Mac (exit 127).
- Dev Mac: NDK `/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865` (Android build: `cmake -S . -B build/android -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=<NDK>/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 && cmake --build build/android`, guard `LLVM_READELF=<NDK>/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-readelf tools/check-android.sh build/android/arcdroid_libretro_android.so`), adb `/opt/homebrew/share/android-commandlinetools/platform-tools/adb` (not on `PATH`), MinGW, `JAVA_HOME=/opt/homebrew/opt/openjdk`, no ripgrep, no RetroArch, no PIL, no `timeout`; private ROMs under `ROMS/` (git-ignored; `ROM311` = RISC OS 3.11); the Founder's library at `~/Desktop/archimedes/Base Set` (218 zips; index = 0-based position in the sorted zip listing); ADFFS 2.88 zips on the Desktop (`adffs288.zip`, `adffs288lite.zip`; private). Reproduction: `cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/host && ctest --test-dir build/host --output-on-failure`; real boot `build/host/libretro_harness <dylib> <ROM> <content> <dir> --real-boot [--machine …] [--joystick …] [--osk-script FILE]` (content may be a `.zip`; the harness real-boot log carries the `Zip content:` and `Launch:` lines in `<dir>/evidence/real-boot.log`). Keep the host idle during Sol/Opus sanitizer harness runs; never run git commands that touch Sol's working tree while Sol runs; delete `tests/__pycache__` before committing.
- Odin3 (adb `cf0be0f3`, Android 15, RetroArch 1.22.2_GIT): system `/storage/emulated/0/BIOS`, cores `/data/user/0/com.retroarch.aarch64/cores/`, log `/storage/emulated/0/RetroArch/logs/retroarch.log` (one per run, `grep -a`), run config `/storage/emulated/0/RetroArch/arcdroid-run.cfg`; launch `am start -n com.retroarch.aarch64/com.retroarch.browser.retroactivity.RetroActivityFuture --es ROM <path> --es LIBRETRO <core> --es CONFIGFILE <cfg>` after `input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard`; sideload via `RetroArch/downloads/` → Load Core → Install or Restore a Core (`input tap 800 260`, fling the list to its end with `input swipe 900 1000 900 150 80` ≈ 40 times, screenshot until "Install or Restore a Core" is the second-last row, wait 3 s, `input tap 800 866`, then tap the `.so` row at y≈612; also push the repo `arcdroid_libretro.info` to `RetroArch/info/`); PSS via `dumpsys meminfo <pid> | grep "TOTAL PSS:"`; before device work check `pidof com.retroarch.aarch64` — an instance not started by the session must be closed by the Founder; emulated storage is case-insensitive. Input: synthetic keyboard does not reach the core; pad via kernel node `/dev/input/event8` (`sendevent`; HAT 16/17, `BTN_SOUTH 304` B, `BTN_EAST 305` A, `BTN_NORTH 307` Y, `BTN_START 315`, `BTN_TR 311` Select, `BTN_TL2 312` Menu, `BTN_THUMBL 317` OSK); first pad event consumed by autoconfig; OSK cells rows y≈665/740/812/885/960/1035, columns x≈273+68.5·n; pointer by swipe ≈1.5 px per swipe px; taps as `input swipe x y x y 90`; network commands `echo -n QUIT | timeout 3 nc -u -q 1 127.0.0.1 55355` on the device (twice to exit); the log is block-buffered — read after `QUIT`; chooser rows y≈229+45·row. Device runners: `/data/local/tmp/s33.sh` (`start` blocks the adb session — launch it as its own background task and never chain a `quit` after a `start` in the same backgrounded script (S39); `hat`, `btn`, `net`, `quit`/`kill`). Library on the device: `/storage/023E-37AB/ROMs/archimedes/Base Set` (= Desktop); `/data/user/0/...` is not readable from `adb shell`, so prove the installed core by behaviour (log fields), not by hash. In zsh never name a loop variable `path`.
- **Founder's Windows box (S26):** `ssh sinmu@192.168.1.66` (key auth, default shell cmd.exe — pipe scripts to `pwsh -NoProfile -Command -` over stdin), Windows 11 Pro; project folder `S:\dev\ArcDroid` (`repo` = clone of `transfer\arcdroid.bundle`, `input\<pkg>`, `work\<tag>`, `run-driver.ps1 -Launch -Tag T -ScriptPath P -InputDirectory D` starts the scheduled task `ArcDroid-Driver`; log `work\T.log`, exit code `work\T.exit`). Parse check `[System.Management.Automation.Language.Parser]::ParseFile` before every push that touches the `.ps1`. The box verifies parse, start-up, reloads and the early media steps, not the CI oracle end to end. Stop only processes this session started through the task.
- **Host load (S28):** 12 orphaned `yes` processes (PPID 1, PIDs 51354–51365) load this Mac; no ArcDroid session started them — never touch them; reported to the Founder in S28.
- Misc: Windows-only CI failures cost ≈ 8 min per round — give Sol the downloaded evidence (`gh run download <run> -n arcdroid-windows-retroarch-evidence`); `PushNotification` reaches the Founder when Remote Control is active; the Founder follows sessions remotely; a step needing the Odin3 or a Founder decision parks its workstream with a complete waiting-list entry and a `PushNotification`, and the session ends (case B) only when no independent work remains (D-124, superseding the earlier "ends the session" ask).
