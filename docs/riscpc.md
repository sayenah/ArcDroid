# RiscPC workstream — engine evaluation and architecture (D-063, D-122, D-123)

Status: **GO (D-131): slices R1–R4 on RPCEmu 0.9.5's interpreter; R1 merged as `7244f9c` (D-137, D-138); R2 designed (D-139); R2a (memory timing + CMOS) merged as `6ee6460` (D-140); R2b floppy next, then R2c IDE.** The disposable trial (D-126) is closed and kept unmerged as the record. Indices refer to the Founder's private 218-zip library (0-based sorted position); they are test-coverage identifiers only and never implementation keys (D-090).

## 1. Why

RiscPC-only library images (JFD header CPU flags without any Archimedes-capable CPU, or OS flags only for RISC OS 3.5+) do not play today. Measured from S50 logs:

| Indices | JFD flags | Note |
|---|---|---|
| 31, 74, 84 | 31: SA, RO 3.7/4 · 74: ARM6/7, 7500, RO 3.5+ · 84: ARM6/7, 7500, SA, RO 3.7+ | **no playable edition in the library** |
| 50, 90, 110, 116, 117, 144 | SA-only or ARM6/7/7500/SA, RO 3.5–3.7+ | RiscPC/StrongARM editions of games whose originals already play (D-085) |
| 155 | ARM3/ARM250 CPU bits, OS bits 3.5/3.7/4 only | not promised; evaluated as compatible CPU/OS pairs |
| 112 | empty OS mask | unknown, not a beneficiary |

All nine are regular FileCore images (E or F; some need D-089's track-side-0 presentation). ArcDroid's native identification and launch discovery already find their applications on the Archimedes path. That does **not** prove that a RiscPC alone makes them playable and saving; the feasibility probe must show it. **Player value: three currently unavailable titles plus six additional editions — not nine new games.**

## 2. Engine evaluation (PRD §22, AR-1..AR-3)

Evidence: primary-source audit of RPCEmu 0.9.5, alternatives survey, Astra stages 1–2 (S51), and a scratch Odin3 throughput probe. Private copies: S51 scratchpad `riscpc/`.

- **RPCEmu 0.9.5** (26 Oct 2024; tarball sha256 `4e4624641cd1a83af275bd6da8765d4a0e5e0c83f008f059014cc24c5c33d59e`; Mercurial changeset 1369:f005be758787). GPL-2.0-or-later core (combinable into ArcDroid's GPL-2.0-only work); bundled third-party code only slirp/TAP (excluded); about 20 header-less files (disc layer, HostFS) need explicit provenance before import. Models ARM610/710/7500/7500FE/SA-110/ARM810, IOMD/IOMD2, VIDC20, SMC Super I/O (82077-class FDC), IDE, PCF8583 CMOS, PS/2 keyboard, quadrature (RiscPC) or PS/2 (A7000) mouse; no joystick; no FPA; no save states. Interpreter on every host; the dynarec is x86/x86-64 only (an AArch64 prototype exists privately upstream). Upstream Mercurial returned 403 on 2026-10-02; support moved to the Stardot forum — track by release tarballs.
- **Known RPCEmu gaps relevant to games:** the TLB caches translations without permissions (`src/mem.h:54`, `src/cp15.c:189`) — permission-dependent software misbehaves (also why ADFFS fails under RPCEmu); host-clock timing (IOMD timers, sound DMA, flyback, RTC) — not deterministic; VIDC20 pixel clock ignored and whole-frame rendering (no raster splits); 16-bit serial sound only (no 8-bit VIDC compatibility sound); direct `exit()` and guest-reachable `fatal()` paths; Qt-owned main loop and threads (the core itself compiles headless with 22 host hooks); 42 global symbol names collide with ArcDroid's Arculator build.
- **Alternatives:** Arculator has no RiscPC/IOMD/VIDC20/StrongARM code on any branch; every MAME RiscPC-class set is `MACHINE_NOT_WORKING` (ARM7 core models SA-110; useful only as a behavioural reference and for public ROM SHA-1 identification); Clock Signal and ArcEm are Archimedes-only; VirtualRPC/Red Squirrel are closed. The `rpcemu-extended` fork (GPL-2.0, Oct 2025) carries an MMU permission fix and an arm64 JIT; it is young and unreviewed — not adopted wholesale.
- **Verdict:** RPCEmu 0.9.5 is the **provisional** second engine. No stronger complete alternative exists. Unmodified, it meets none of ArcDroid's timing, media, persistence or containment requirements; adoption is conditional on the feasibility probe (§5).

## 3. Throughput evidence so far (scratch probe, not a gate result)

RPCEmu 0.9.5 interpreter core with stub host hooks, synthetic 2 MB ROM running a 7-instruction ALU/load/store loop from ROM with the MMU off, no video or sound; NDK r29 `-O2` arm64.

| Host | Model | Result |
|---|---|---|
| Odin3 (Snapdragon 8 Elite) | SA-110 | 214.7 MIPS (5 s) |
| Odin3 | ARM710 | 226.8 MIPS (5 s) |
| Odin3 | SA-110 | 216 MIPS over 20 s (from the loop register: 617,808,572 iterations × 7; the 32-bit instruction counter wraps after ≈ 19.9 s) |
| Dev Mac (loaded) | SA-110 | ≈ 105 MIPS |

A 200 M-instruction/s StrongARM budget would consume ≈ 93 % of this best case before MMU translation, VIDC20 conversion, sound and libretro costs: promising for ARM610/710/7500 budgets, **tight for sustained StrongARM**. Not a substitute for §5.

## 4. Architecture (binding for later slices once the Founder approves)

1. **One public engine boundary, private backends.** `arcdroid_engine.h` becomes a session API dispatching to backend operations (Arculator, RPCEmu) with explicit capabilities (observers, media, joystick, IDE, serialisation). Storage contracts move to ArcDroid-owned headers; CPU hooks stay private. Exactly one backend is active per session; Arculator→RPCEmu→Arculator switching is tested.
2. **Import:** allowlisted, tarball-tracked copy under `engine/rpcemu/` with its own `UPSTREAM.md`, per-file provenance (header-less files resolved before import), preserved notices, and a small marked patch series. Excluded: Qt GUI, threads, networking/slirp/TAP, HostFS, CD-ROM/ISO, dynarec, podule ROM loader, bundled CMOS/media assets, RPCEmu's file-backed disc loaders, alphabetical ROM concatenation and automatic ROM patches.
3. **Isolation and containment:** every RPCEmu external symbol and host hook prefixed (visibility alone is insufficient inside one library), with link/export checks on host, Windows and Android; all `fatal()`/`exit()` paths end the session safely (never process exit, never continued execution); bounded start/reset/run/media failure handling with partial-initialisation cleanup; callbacks quiesced before teardown.
4. **Deterministic virtual time:** one scheduler for CPU batches, IOMD timers, sound DMA, floppy, IDE and flyback; idle advances to the next event; RTC pinned in tests; per-model instruction-rate (or execution-cost) model chosen from calibration, never lowered to pass. Rates (millions of instructions per emulated second): SA-110 100 — the accepted Odin3 budget (D-131); ARM610 20, ARM710 35, ARM7500 35 — provisional engineering defaults (trial calibration was inconclusive), unqualified until R4 (D-137). Identical inputs → identical guest traces and audio sample counts regardless of host pacing. Host audio-buffer state never alters guest interrupt order.
5. **MMU correctness:** a general permission-cache correction (read/write, user/supervisor, domains, subpages, aborts, invalidation) with authored tests; the fork's change may be reviewed as an individually pinned reference.
6. **Media:** ArcDroid keeps all media I/O and policy (VFS, zip, folder sets/m3u, chooser, JFD descriptors, effective-sector presentation, quarantine, omitted sectors, overlays, store commits, guards). RPCEmu's disc layer shares Arculator's design lineage (`disc_funcs`, FDC data/sector-ID/not-found/CRC/write-protect callbacks), so ArcDroid's drivers attach through a **typed controller bridge with targeted FDC corrections**, never a flat-ADF conversion. The bridge must prove: physical head vs requested CHRN, side, size and deleted-data semantics (RPCEmu drops the requested size on writes); density mapping and virtual-time poll periods incl. multi-sector; READ ID, header/data CRC, absent sector and empty-drive completion; DMA/IRQ ordering, terminal count, short transfers, lost data and overrun; a synchronous refusal ends the command with no later DMA or stale callback; guard/store failure commits nothing and reaches the right guest error; reset/eject/swap cancels transfers incl. helper drive 1; shared driver globals bind to the active backend and reset between engines. Evidence from guest-issued commands through Super I/O/IOMD plus differential media-event traces. IDE: RPCEmu's controller gets ArcDroid's bounded transactional backend if the probe shows the native hard-disc route is needed; otherwise the Founder approves a floppy-only initial contract. **R2 reconciliation (D-139):** the "drivers attach" above is realised as an ArcDroid-owned, instance-based RPC drive model that uses ArcDroid's media core (effective-sector view, guards, store, write policy, staging) unchanged; Arculator's drive side is not modified. The trial proved the hard-disc route is needed, so IDE is in R2 (126 MiB discs in a chunked sparse store).
7. **Video/audio:** VIDC20 output bounded in geometry/pitch; refresh derived from emulated mode timing with the libretro AV-info update (not geometry alone) on change; palette, cursor, depth and mode changes handled; raster behaviour qualified before frame-only rendering is accepted. Guest DMA/IRQs decoupled from host consumption; deterministic resampling and rate-change handling; 8-bit compatibility sound qualified and implemented if an accepted title needs it.
8. **Input:** ArcDroid input translated to PS/2 keyboard and to the model's mouse (quadrature on RiscPC, PS/2 on A7000); held-key release, controller arbitration and reset behaviour preserved; pad-to-key mappings kept; no A3010 joystick advertised on RiscPC.
9. **CMOS and persistence:** PCF8583 contents keyed like today (content identity, machine, firmware/layout compatibility), generated defaults with recorded provenance, eager persistence for Android termination; existing storage isolation unchanged. **R2 (D-139):** the firmware/layout part of the key is the exact firmware SHA-256 until layout compatibility between versions is proven; defaults are the firmware's own held-Delete factory reset, generated locally and cached with provenance.
10. **Observers:** the boot watch, launch outcome, error-box and disc-request observers need RPCEmu equivalents of the full event and intervention contract — SWI entry, continuation and mode-transition events, controlled completion, side-effect-free bounds-checked virtual-memory inspection, normalised 26/32-bit PC/flags/privilege, controller-idle and emulated-time services — re-qualified against each supported firmware; each gated independently with manual input preserved when unavailable.
11. **Firmware:** RISC OS 3.50/3.60/3.70/3.71 user-supplied in the system directory, identified by fingerprint (permitted by D-090), never shipped, committed, logged or fetched; incompatible or ambiguous assemblies rejected. Support is claimed only for firmware/CPU pairs validated privately. RISC OS 5 is outside the launch contract (32-bit; it may later be an explicitly selected experimental target after a licence review of the actual ROM package). Missing firmware → before guest execution, a notice naming the needed CPU/OS family and setup location; never a substitute OS. **Boot files (Founder S55, D-134):** guest-side support files such as RISC OS Open's HardDisc4 boot sequence and `!System` resources (PlingSystem) are treated like firmware — ArcDroid never ships, bundles or downloads them; a player who needs them places them in RetroArch's system (BIOS) folder, and ArcDroid uses them from there (exact layout, verification and the missing-files notice are R2/R3 design).
12. **Machine selection (Auto):** JFD flags decoded as compatibility categories (the "3.5" bit's mapping to 3.50/3.60 made explicit); multi-disc sets intersect their constraints; an empty mask means unknown; conflicts give an explanatory failure plus the manual override, never an engine switch on disc insertion. Choose from installed, supported machine/firmware pairs by a stable general order: existing Arculator selection wherever allowed; for RiscPC a validated non-StrongARM model when allowed, StrongARM when required, A7000 when its hardware is required. Non-JFD media keep today's selection unless validated structural evidence says otherwise; a manual RiscPC option is offered. Existing working content never regresses (library A/B).
13. **Save states:** required later (D-011), unsupported until complete serialisation is proven for both engines.

## 5. Feasibility probe — stop/go criteria (fixed before measurement)

Disposable (never merged as product code) but representative: implements the §4.4 scheduler, the MMU correction and the workloads below; may include a minimal media bridge to reach private gameplay.

- **Rates swept** (labelled experimental): ARM610 20, ARM710/7500 35, SA-110 100/150/200 million instructions per emulated second. A production rate is approved only against independent hardware/reference measurements (ALU, branches, memory, timer-paced code); model-dependent execution costs if one rate cannot reproduce behaviour.
- **Workloads:** the existing loop; MMU hits/misses, permission faults and privilege switches with the correction on; RAM execution and self-modifying code; framebuffer/DMA contention; continuous stereo audio with rate changes; floppy/helper activity; 50/60 Hz modes at low resolution, 640×480 and 800×600; private scripted gameplay of 31, 74, 84 and one StrongARM-only title (50, 110 or 116). Desktop boot is only a smoke test.
- **Platforms:** Odin3 (the only Android device, D-126) and native Windows; production-equivalent optimisation, normal device performance/fan settings, through RetroArch.
- **Duration:** three independent runs per workload, 10-minute warm-up + 20 minutes measured; temperatures, frequencies and settings recorded; every run must pass.
- **CPU budget** (core execution excluding frontend sleep, frame period T): p95 ≤ 0.80 T, p99 ≤ 0.90 T, p99.9 ≤ T; no three consecutive deadline misses; frontend missed presentations < 0.1 %.
- **Sustained:** last-five-minute throughput within 10 % of the first measured five minutes; guest time tracks wall time within 0.1 %.
- **Audio:** zero core-caused underruns/overruns in steady gameplay; bounded buffers; no growing AV drift or audible discontinuity.
- **Determinism:** identical inputs give identical guest traces and audio sample counts across host pacing.
- **Failure at a calibrated required rate → stop and return to the Founder** (separate JIT evaluation, narrower initial support, or deferral). No import, no silent speed reduction.

## 6. Slice plan (after the gate)

| Unit | Scope | Evidence |
|---|---|---|
| P | Disposable feasibility probe (§5) | thresholds above; IDE need decided; per-title private gameplay reached for 31/74/84 |
| R1 | Contained headless import: allowlist/provenance, isolation, containment, virtual time, MMU correction, basic AV/input | authored CPU/MMU/IOMD/VIDC20 ROM traces on ARM610/710/7500/SA-110; repeatability on Windows and arm64; failure/reload/switching tests |
| R2 | Media bridge (JFD/raw/helper), IDE if required, CMOS/persistence; delivered as R2a memory timing (R1-1) + CMOS, R2b floppy drives + full controller, R2c IDE + scalable hard disc (D-139) | guest-driven controller tests; CRC/quarantine/guard refusals; interrupted writes; restart recovery; disc swaps; factory reset at production rates; end-of-R2 speed gate |
| R3 | Firmware inventory, Auto selection, launch machinery and observers | selection/conflict/missing-ROM matrix; authored observer arbitration; private 3.5–3.71 launch and save evidence |
| R4 | Platform and regression qualification | deterministic trace comparisons; lifecycle/resource stress; sustained Odin3/Windows gameplay; full-library A/B in both package states; release notes |

Completion means gameplay with the title's relevant save and input behaviour, with any exception explicit — not a title screen.

## 7. Founder gate (answered 2026-10-04, D-126: trial approved; firmware 3.50–3.71 held; sequence trial → R1 → R4 now; Odin3 only; trial proves the games work; no wholesale fork adoption)

1. **Probe authorisation:** approve the disposable feasibility probe with RPCEmu 0.9.5 as the provisional engine and the §5 thresholds; no import if it fails.
2. **Firmware:** does the Founder lawfully hold RISC OS 3.71 (and any of 3.50/3.60/3.70)? The licensed seller (3QD "Classic RISC OS ROMS") had sales on hold in late 2025; owners of RiscPC/A7000 ROM chips can dump their own. Support is claimed only for firmware validated privately. RISC OS 5 stays out of the launch contract.
3. **Value and timing:** three unavailable titles plus six editions for about five work units (probe + R1–R4) — now, or after roadmap item 7 (ADFFS-path failures) / item 5 (release baseline).
4. **Device floor and failure path:** confirm Odin3 + which Retroid Pocket model; if the interpreter fails the probe — separate JIT evaluation, narrower initial support (e.g. ARM710-class only), or deferral.
5. **Reduced initial contract** if the probe shows it is needed: floppy-only (no IDE), firmware-specific automation gaps, 8-bit sound or raster work deferred, fewer beneficiaries.
6. **Fork policy:** `rpcemu-extended` not adopted wholesale; its MMU change only as a reviewed reference; its JIT only through a separate workstream.
