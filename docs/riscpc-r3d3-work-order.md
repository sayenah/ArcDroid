<!-- SPDX-License-Identifier: GPL-2.0-only -->

# R3d-3: production preparation host and concurrent-writer ownership

**Status (S65, D-193):** adopted after Astra stage 2 (REJECT of v1), Fable's rulings and the stage 2 re-check (adjudication at the end). `slice/r3d3-own` is dispatched to Sol now; `slice/r3d3-host` waits for its base (R3c-3b part 2 merged), P10b and the frozen notice texts. The pending-decision wording below about the `F_SETLK` fallback and the Android floor is settled: Fable confirmed the removal, and the Founder decided D-192.

**R3d-3 Stage:** DESIGN. **Owner:** Fable.
- Drafted in S65. Revised after two critics, after Astra stage 2, and after two checks of the stage-2 revision.
- Astra stage 2 returned **REJECT** with six findings (`.work/s65/astra/report3.md`). Fable ruled on all six, and this revision incorporates every ruling. See **Astra stage 2 adjudication** at the end.
- The two checks found three high-severity contradictions (H1 to H3), several medium gaps and some lock holes. Every valid issue is applied below. The rejected ones are listed under **Check issues not taken**.
- **One deviation from Fable's F1 ruling needs Fable's confirmation.** The ruling allowed a classic `F_SETLK` fallback where OFD is missing. This revision removes that fallback (R-10, under "Classic `F_SETLK` fallback").

**Next (Fable), in this order:**
1. **Record these decisions in `docs/decision-log.md`.** Each item is marked with where it lands.
   - **[D-186 amendment, sequencing]** R-1: the host branch stacks on R3c-3b part 2, and M1 (R3d-4) waits for part 2's merge. Astra's smaller reset-foundation alternative is recorded as rejected for now.
   - **[D-186 amendment, A-5 mechanism]** R-10: concurrent-writer ownership uses an OS advisory lock, which the operating system releases when the holder dies. It replaces A-5's candidate, VFS mkdir.
     - Automatic lease breaking is withdrawn.
     - Crash recovery is the OS release on process death.
     - D-172's reopen bound stands unamended.
     - The classic `F_SETLK` fallback is not adopted (deviation from the F1 ruling; Fable confirms).
   - **[New entry, narrow exception to D-021]** One native-I/O exception for zero-byte sentinel lock files under `<save>/arcdroid/locks-riscpc/`. It covers open, lock, unlock and close, plus `stat`/`fstat` identity checks.
     - The lock descriptors never carry data reads or writes.
     - The entry also authorises the matching shipping-I/O guard amendment (scope 12a).
   - **[Plan §4 and Amendment 11 amendment, failure classes]** (check M4):
     - The second-opener refusal (N-14), the defaults contention (N-16) and lock failures (N-15, N-18) are **class B**, because acquisition is a stepped VERIFY unit and not a load-time decision.
     - A non-native save directory stays class A wherever load already decides it. Hard-disc sessions are refused at begin (`arc_riscpc_launch_record_begin`, [3b] `rpc_preparation.c:151`).
     - A root that only the lock module refuses (non-HD sessions, relative paths) is class B N-15.
   - **[Plan §7 amendment]** New texts N-13 to N-18. N-15 and N-18 carry no path. It also adds the redaction tokens and the redacted on-screen forms of every catalogue notice that carries a member name or path (R-5, F5). The exact texts are frozen before `slice/r3d3-host` dispatch.
   - **[Plan §5.2 amendment]** P10 is redefined:
     - P10a is the primary-source lock research, recorded with its sources;
     - P10b is the on-device lock probes (R-10).
   - **[D-169 note]** The treatment of the two frozen error-path synchronous publications, from P8 (R-9). These are the RUNNING reset failure ([3b] `:663`) and the swap rollback ([3b] `:280`).
   - **[Carried item to R3d-4]** Privacy of the shared `arc_message` path. It uses `RETRO_MESSAGE_TARGET_ALL` and logs its text (`arcdroid_libretro.c:1337-1349`). It is unchanged here because ordinary-route identity depends on it (R-5, check M3).
2. **Add to the Waiting on the Founder list** (this does not block either branch, because the defaults are safe): **the Android floor for RiscPC concurrent-writer safety**, folded into PRD Q3 (`docs/PRD.md:786`).
   - Kernels without OFD (before Linux 3.15) are refused with N-15. The CI floor is `android-21`, so some arm64 Android 5–6 devices could not run RiscPC.
   - sdcardfs-era shared storage (Android 10 and earlier) has no probe. ArcDroid's position there is "unverified; refused only if the lock call fails".
   - The Founder decides whether these are out of scope or need a probe leg or another mechanism.
3. **Amend R3c-3b part 2's work order** (R-1 precondition 2). Part 2's merge of `main` with R3d-1 must make part 2 pass the R3d-1 source guard by converting to session kind:
   - its frontend `arc_engine_is_rpc(` sites;
   - its module sites, [3b] `rpc_preparation.c:627`, `:644` (inside the frozen RUNNING block, which part 2 owns) and `:679`.

   Sol does this in part 2's run, not in R3d-3.
4. **Measure or freeze the pre-dispatch items** (table below) before dispatching Sol. Each row states the decision it controls and the default that applies if it cannot be measured.
5. **Return this revised work order to Astra** on the same thread, for a re-check of findings 1–6 and of the lock mechanism. Adjudicate every finding.
6. **Dispatch Sol, Run A on `slice/r3d3-own`,** from `main` pinned at dispatch, once Astra is adjudicated.
   - Scope: 12a, the shipping-I/O guard amendment, and scope 13's ownership section.
   - Set `Stage: IMPLEMENTATION`, `Owner: Sol`.
7. **After `slice/r3d3-own` merges: run the P10b device legs** with the scope-12a ownership process binary, on the Odin3, Windows and the Mac.
   - The second Android device (a D-191 Founder item) adds a second kernel and MediaProvider leg when it is available. It does not block.
   - If OFD exclusion between two app processes fails on Odin3 shared storage, the choice of fallback (R-10) is a **Founder decision**, and 12b waits for it.
8. **Dispatch Sol, Run A on `slice/r3d3-host`,** once these hold:
   - R-1's base exists;
   - P10b has passed, or the Founder has decided;
   - the §7 texts and redaction forms are frozen.

   Scope: 1–11, 12b, and 13 except the ownership section. Each run reports the AC evidence listed under **Deterministic tests**.

**Pre-dispatch items (Fable measures or freezes):**

| Item | What it controls | Default if not measured |
|---|---|---|
| P1 | (a) The clear priority (R-5). (b) Whether two extended messages stay visible together (R-5 progress alongside an advisory). (c) Whether `RETRO_MESSAGE_TARGET_OSD` extended messages stay out of RetroArch's log (F5). | Single-slot rule on both paths. Clears at priority 3. If OSD-only behaviour is unverified, extended texts are redacted exactly like fallback texts, so no private string reaches either path. |
| P1b | Whether a failed plan-route negotiation (pixel format) shows its notice. | Release and return false, with the reason ID logged. No fallback session. |
| P3 | (a) Whether a second library copy in the same process (run-ahead) loads content. This includes the `preempt_deinit` path that re-sets `RUNAHEAD_AVAILABLE` (inferred from the source, not run). (b) Library lifetime across a content switch. | The source reading stands: the second instance never loads content (R-10). The lock module keeps no state across library loads. P3 no longer controls any lock fallback. Adding savestate support (PRD Q5) re-opens P3. |
| P8 | (a) R-9: S-4 synchronous or stepped. (b) The D-169 treatment of the frozen synchronous publications at [3b] `:663` (reset failure) and `:280` (swap rollback). | (a) Stepped, N = 60 frames. (b) Both are recorded as error-path exceptions. Each is counted in the summary line as its own exception kind with its measured cost, and an in-flight publication is completed before either one runs (R-9). |
| Hold table | R-6 per-pair bounds. | 25 s for every unmeasured pair. |
| §7 texts | N-13 to N-18, the redaction tokens, and the redacted form of every catalogue notice that carries a member name or path (R-5). | No default. They are required before `slice/r3d3-host` dispatch and not needed for `slice/r3d3-own`. |

**P10b (S68, complete):** Mac (APFS), Windows (NTFS) and the Odin3 (shared, internal app-private and `Android/data` storage, from RetroArch's own process through the W9 probe core) PASS on exclusion, post-death reacquire and deletion detection; the same-UID shared-storage rename-over residual is recorded under R-10's accepted residuals (D-208). **`slice/r3d3-host` Run A dispatched (S68).**

**Status (S68):** the §7 texts, redaction tokens, redacted and log forms are frozen in `docs/riscpc-ordinary-load-plan.md` §7a, which also records the P1 source-reading outcome and adopts the stated defaults for P1b, P3, P8 and the hold table. §7a rules that the staging `arc_log` lines reached from the host's media callbacks are redacted in RiscPC sessions (R-5's carve-out is the shared `arc_message` path only) and places AC-3's sentinels in a boot-archive member, a content ZIP member, the content leaf and the save path. P10b: Mac and Windows PASS (S68); Odin3 legs wait for W9 (`docs/handoff.md`).

**What this work order reconciles.** It brings §6.3 of `docs/riscpc-ordinary-load-plan.md` into line with:
- the plan's Amendments 1–8, 11 (as amended here), 17 and 19;
- the Astra stage 1 and stage 2 plan adjudications A-3, A-5 (as amended here), A-6, A-7 and A-8;
- answer Q11;
- the R3d-2 QA adjudication (4);
- D-168, D-170, D-172, D-177(7) and D-190;
- Astra's R3d-3 stage 2 findings F1–F6, as ruled;
- the two S65 checks of the stage-2 revision.

The plan governs anything not restated here. Amendment 23 belongs to R3d-5 and is not addressed here.

**Line references** are to `main` `ed604d9` unless marked:
- [d1]: `origin/slice/r3d1-session` `60457bc`;
- [3b]: `origin/slice/r3c3b-launch` `35a7318`.

## Objective

Move every frontend piece that RPC preparation needs into one production frontend host that owns the per-session `ArcRpcPreparation`. Those pieces now sit under `ARCDROID_TEST_PLANS`/`ARCDROID_TEST_FAULTS` and in `tests/arc_test_plan.c`. The host also:
- routes every RPC reset through the frozen A-3 transaction;
- rejects disk control while preparation is pending, with no side effects;
- gives notices module-set severities and checkable log channels that carry no private strings;
- applies the AV contract;
- adds the concurrent-writer ownership that R3d-4 needs (A-5, as amended by R-10).

The plan core becomes a thin client that keeps only its low-level modes (D-168(6), D-170(6)). Ordinary loads still never reach the host. The oracle therefore has two parts:
- the plan route is identical except for the enumerated deltas;
- shipping ordinary loads are byte-identical except for the cumulative gate-closed list (Amendment 17).

## Verified state

**No production owner exists.**
- `libretro/rpc_preparation.c` ships (CMakeLists.txt:153), but its only owner is the static `preparation` at `tests/arc_test_plan.c:28`.
- It is created through the low-level entry (`tests/arc_test_plan.c:425-427`).
- Plans without `persist` never create a preparation. They call `arc_engine_test_plan_start` directly (`tests/arc_test_plan.c:417`), yet R3d-1 sets `arc_kind = RISCPC` for every `.arcplan` load ([d1] `arcdroid_libretro.c:7722`).
- `tests/arc_test_plan.c` is compiled into every non-shipping target (CMakeLists.txt:231-234).
- `ARCDROID_TEST_PLANS` is defined only for `arcdroid_rpcplan_libretro` (CMakeLists.txt:2286-2287).

**Gated glue to promote:**

| Piece | Location (main unless marked) |
|---|---|
| attach (`arc_rpc_plan_media`) | `arcdroid_libretro.c:7438-7472` |
| RUNNING-reset media (`arc_rpc_plan_reset_media`) | `:7473-7513` |
| option freeze (`arc_rpc_keep_media_options`) | `:7514-7529`, called at `:7946` and `:7963` |
| notice host (`arc_plan_notice`/`_tick`/`_log`) | `:7530-7558`. Clears are sent at priority 2; terminals are logged at ERROR. |
| plan load branch | `:7562-7586` |
| `retro_reset` RPC branches | `:7616-7642` |
| pending frames (a per-frame `arc_notify_av_change(640,480,60)`, a fixed 800 samples) | `:8054-8067` |
| maintenance call | `:7975-7978` |
| teardown (`arc_test_plan_clear`) | `tests/arc_test_plan.c:231-245`, from `arc_release_session` `:1149-1153` |
| test host adapters | `tests/arc_test_plan.c:112-165`; [3b] `:108-200` |
| drive-0 re-stage and drive-1 view/policy | [3b] `arcdroid_libretro.c:7569-7612`; [3b] `tests/arc_test_plan.c:134-142` and `:184-190` |

**The RUNNING reset bypasses the module.**
- main `:7623-7642` calls `arc_engine_reset` and then re-prepares **both** drives. Every reset also sets `arc_rpc_av_attempted=false`, which re-issues AV.
- The A-3 transaction exists only on [3b], in `arc_riscpc_preparation_reset` (`rpc_preparation.c:637-674`).
  - Its RUNNING block, `if(p->state==ARC_RPC_PREP_RUNNING){…}`, is **`:643-669`**.
  - On failure, that block publishes the latest CMOS snapshot synchronously (`:663`, `arc_riscpc_cmos_store_put(&p->record,latest,NULL,…)`). The block is frozen, and the base already contains it (see AC-4).
  - Lines `:670-673` are the non-RUNNING path: `release(p);restart(p);` and the launch-record restart. They are **not** frozen. R-7 and 12b edit them.
- That transaction is reachable only through `arc_test_plan_preparation_restart` ([3b] `tests/arc_test_plan.c:203`, which calls `arc_test_plan_reset()`).
  - The runner's `--preparation-reset-at` and `--preparation-reset-stage` call it. The flags are parsed at [3b] `tools/rpc_runner.c:241-242` and used at `:285-352`.
  - Those flags are used by [3b] `tests/rpc_boot_plan_test.py:44-51` and `tests/rpc_launch_qualification.py:94-95`.
- main's module reset (`rpc_preparation.c:435-441`) is "engine reset, else release and restart".

**The module state that the promotion depends on ([3b] `rpc_preparation.c`):**
- `begin()` copies the drive-0 view, data pointer included (`:168`), and START attaches that copy (`:509`).
- The re-stage reallocates the backing (`arc_media[i]=next`, [3b] `arcdroid_libretro.c:7586`).
- The part-2 re-stage callback re-inserts drive 0 into the engine ([3b] `arcdroid_libretro.c:7593-7598`). It is valid only under the RUNNING barrier, never during isolated generation.
- **"Pending" does not mean "engine stopped" (F3):**
  - GENERATE runs an isolated engine (`:427-452`);
  - START constructs the requested engine (`:463-467`);
  - the non-RUNNING reset stops it only through `release()` (`:670`).
- `arc_riscpc_preparation_helper_write_policy` accepts only RUNNING with the barrier closed (`:618-622`).
- `verified_catalogue` (`:36`, `:328`) has no accessor.
- `release()` (`:95-106`) stops the engine and closes the launch-record, HD and CMOS stores. It is shared by cancel, free and the pending reset.
- `failed()` (`:107-115`) stops the engine immediately (`:113`), and so does a failed attachment (`:511`).
- `failed()` and the RECORD refusal (`:413`) issue a clear before every terminal, and so does maintenance (`:691`, `:693`).
- **Maintenance.**
  - It returns at once when the state is not RUNNING (`:685`).
  - A maintenance-detected fault returns before persisting (`:686-687`).
  - An observation-invalidation failure (`arc_io_remove(p->observations.path)`, `:697`) also stops the session through `failed()`, without capturing a snapshot.
- `arc_riscpc_preparation_frame` returns at once for `state>=RUNNING` (`:549`).
- **Free.** It persists only when RUNNING and the engine is active (`:714`). It first finishes an in-flight publication (`:717`). Its own terminal is cleared in the same call (`:725-726`).
- **Swap rollback.** `launch_aborted` (`:274-283`) publishes synchronously with `arc_riscpc_cmos_store_put` (`:280`).
- **`arc_riscpc_cmos_store_put_begin` refuses while `putting` is set** ([3b] `rpc_cmos_store.c:348`; `put` at `:484-500`). A stepped publication in flight therefore makes the frozen `:663` put and the `:280` put fail.
- The store handles are opened outside the frozen block: `arc_riscpc_cmos_store_begin` (`:187`, `:418`) and `arc_riscpc_hd_open_begin` (`:340`).
- `status()` (`:69-78`): `state>=RECORD && receipted` gives "Starting RiscPC", including the generation states. The non-receipted START state gives "Preparing the RiscPC hard disc".
- No frontend code calls `arc_riscpc_preparation_cancel` (only `tests/rpc_orchestration_test.c`).
- Production `begin` already verifies an authored fingerprint passed in `request.fingerprint` ([3b] `:163-167`, `:320-323`; [3b] `tests/rpc_orchestration_test.c:191-194`). For authored ROMs the verified catalogue is then 0.
- `begin` refuses a non-native save for hard-disc sessions through `arc_riscpc_launch_record_begin`, which calls `arc_install_store_native_path` ([3b] `:151`; `rpc_launch_record_store.c:8`).
- `arc_install_store_native_path` (`libretro/install_store.c:90-100`) accepts POSIX-absolute, drive-absolute and UNC paths. It also accepts any path without `:`, so it accepts relative paths.

**Store close and publication (F4).**
- `arc_riscpc_hd_close` returns `void`. It discards its close and flush results and frees the object (main `libretro/rpc_hd_store.c:1348`; [3b] `:1389`). [3b] `release()` calls it at `:104`.
- A CMOS publication is several namespace operations, including a remove and a rename (`rpc_cmos_store.c:365` onward).
- The launch-record store mutates the per-title directory directly ([3b] `rpc_launch_record_store.c:56`, `:59`, `:76-81`: remove, create, write, rename).
- D-169 accepts isolated indivisible spikes during first-use preparation. It does not cover a whole synchronous publication.

**Message logging (F5).**
- RetroArch 1.22.2's `SET_MESSAGE` handler logs the message text unconditionally with `RARCH_LOG`. The extended path logs unless its target is `RETRO_MESSAGE_TARGET_OSD` (`runloop.c`; confirmed by both checks).
- Boot errors can carry archive member names (`rpc_boot.c:47-50`). They reach the ERROR log at `:7539` through `rpc_preparation.c:231`.
- The shared `arc_message` (`arcdroid_libretro.c:1337-1349`) uses `RETRO_MESSAGE_TARGET_ALL` and also passes the text to `arc_log`. Arculator and plan-route sessions both use it, for example for disk-control and media notices.

**The disk setters clear state before admission.**
- On main (`arcdroid_libretro.c:5113-5230`) they clear observer and disc state and cancel launch before the `arc_loaded`/`ARC_SESSION_RUNNING` check (`:5121`).
- [3b] adds `arc_disk_rpc_pending()` (`:5175-5181`) as the first statement of all four setters: eject (`:5184`), index (`:5223`), replace (`:5241`) and add (`:5299`).
- Its non-plan clause uses `arc_engine_is_rpc()`, which is false in FAILED under R3d-1's lifecycle masking.

**Production FAILED or CANCELLED sessions fall through to the Arculator path.**
- On [d1], `retro_reset` and `retro_run` key on `arc_kind==RISCPC && arc_engine_active()` ([d1] `:7781`).
- A FAILED or CANCELLED RPC session has a stopped engine, so it takes the Arculator paths.
- Only `arc_test_plan_pending()` prevents this today.

**The R3d-1 source guard** ([d1] `tests/rpc_session_source_guard.py`):
- `arc_engine_is_rpc(`: exactly 2 in `arcdroid_libretro.c`, 1 in `rpc_preparation.c`, 0 in other `libretro/*.c`.
- `arc_engine_capabilities(`: exactly 1 in `arcdroid_libretro.c`, 0 elsewhere.
- `arc_engine_session_generation()`: exactly 2.
- `arc_frontend_session_id(`: none outside `arcdroid_libretro.c`.
- [3b] has 30 `arc_engine_is_rpc(` uses in `arcdroid_libretro.c` and 3 in `rpc_preparation.c` (`:627`, `:644`, `:679`), so it fails this guard today.

**The stores assume a single writer.**
- `docs/riscpc-hd-store.md:13-15` says so, as do `:66` (no lock files) and `:783`. So does the HD open-time recovery that deletes and truncates (`rpc_hd_store.c:1190-1288`).
- The CMOS put uses a fixed `.tmp`, check-then-write and remove-then-rename (`rpc_cmos_store.c:376-470`). Its refusal is sticky (`:347-351`).
- `arc_io_make_path_step` stats and then fails on any non-zero mkdir result, including `-2` (`libretro/arc_io.c:247-250`). HD open maps that failure to UNAVAILABLE (`rpc_hd_store.c:892-894`).

**The libretro VFS cannot provide ownership (P10a).**
- VFS v3 has no lock operation.
- RetroArch 1.22.2's `vfs_implementation.c` [R1] opens files with `_wopen`/`fopen_utf8` on Windows and `open`/`fopen` on POSIX. It sets no share mode and no `O_CLOEXEC`, and never calls `flock`, `fcntl` locking or `LockFile`.
- The same file handles a `saf://` scheme on Android SAF builds. Such a root has no native path.
- Its directory rename is an ordinary `_wrename`/`rename`, with no conditional ownership compare (Astra F1).
- A kernel lock therefore needs a native open and lock made by the core itself, which D-021 and `tools/check-shipping-io.sh` currently forbid.

**Lock behaviour from primary sources (P10a; expected until P10b confirms).**
- **Android shared storage (`/storage/emulated`, FUSE).**
  - MediaProvider's `FuseDaemon.cpp` [A1] adds `FUSE_CAP_FLOCK_LOCKS` to its init mask (`:724-726`, `:762`). It leaves `.getlk`, `.setlk` and `.flock` commented out (`:2370-2390`).
  - libfuse sets `FUSE_CAP_POSIX_LOCKS` only when both `getlk` and `setlk` exist ([A2] `fuse_lowlevel.c:2273`).
  - The kernel therefore sets `fc->no_lock` ([K2] `inode.c:1298-1299`), and `fuse_file_lock` applies POSIX and OFD locks locally with `posix_lock_file` ([K1] `file.c:2552-2572`).
  - So OFD locks conflict between two app processes on the shared FUSE superblock, and between two opens in one process. They are released at last close or process death, without involving the daemon.
  - `flock` reaches the daemon, which answers `ENOSYS` ([A2] `:1890-1893`).
- **FUSE-BPF** (Android common kernels android14-6.1, android15-6.6 and android16-6.12 [K3]; used for `Android/data` and `Android/obb`):
  - `fuse_file_flock` passes through to the backing file, so `flock` may succeed there;
  - OFD handling in `fuse_file_lock` is unchanged.
- **MediaProvider is a Mainline module,** so its behaviour can change without an OS update.
- **App-private storage.**
  - Internal `/data/user/0/<pkg>` is ext4 or f2fs with native kernel locks.
  - External `Android/data/<pkg>` is served through FUSE-BPF or a bind mount. Each needs its own probe.
- **OFD [L2]:**
  - available since Linux 3.15;
  - `l_pid` must be 0;
  - contention returns `EAGAIN`;
  - inherited across `fork`;
  - NFS can lose locks (`EIO`).
  - Classic `fcntl` locks are owned by the process: closing **any** descriptor for the file releases all of that process's locks on it.
- **`LockFileEx` [W1]:**
  - the handle needs `GENERIC_READ` or `GENERIC_WRITE`;
  - locking beyond end of file is allowed;
  - a child process that inherits the handle is not granted the region;
  - release after termination "depends upon available system resources".
  - W1 says only that a locked region cannot be accessed through another handle. It does not explicitly say that a second `LockFileEx` in the same process is refused, so P10b must prove that.
- **Run-ahead (P3, from the source [R2]):**
  - The second instance is created in the same process, from a copied library (`copy_core_to_temp_file()`), so the two copies do not share library static state.
  - `runahead_run` stops at `core_info_current_supports_runahead()`. ArcDroid's info file says `savestate = "false"` (`arcdroid_libretro.info:13`).
  - `runahead_create` stops on `runahead_save_state_size == 0` (`:1032-1036`). ArcDroid returns 0 from `retro_serialize_size` (`arcdroid_libretro.c:8348`).
  - Both stops happen before `secondary_core_ensure_exists()` (`:1243`). So the second instance never loads ArcDroid content.
  - A possible re-entry through `preempt_deinit` is inferred from the source, not run. P3's run must cover it.

**What the shipping-I/O guard forbids today (`tools/check-shipping-io.sh`).**
- **Patterns.** The general pattern (`:128`, applied at `:153`) rejects `fopen`, `open`, `_wfopen`, `gz*`, `CreateFile*` and `dumpregs(`.
- **Exceptions.** The only ones are one exact `arc.h` comment line (`:154`) and two `zutil.h` lines (`:179-180`). The `grep -Ev` filter admits any number of identical lines.
- **CMake cross-check.** `:109-126` requires every `.c` in `arcdroid_core` and in `ARCDROID_(LIBRETRO|ARCULATOR|RPC)_SOURCES` to be in the scan list.
- **RPC sources** (`engine/rpc`, `engine/rpcemu`) additionally reject `open`, `close`, `read`, `write`, `pthread_*`, time functions and `getenv` (`:147`).
- **Not matched today:** `fcntl`, `flock`, `LockFileEx`, `stat`/`fstat`, and `close` outside RPC sources.
- **Holes:** `openat(`, `creat(`, `fdopen(`, `CreateFile2(`, `NtCreateFile(`, `_open(`, `_wopen(`, `_sopen*(`, `open64(`, `fopen64(`, `fopen_s(`, `_wfopen_s(`, `freopen(`, `tmpfile(` and `mkstemp(` are unmatched. None of them occurs in shipping sources today (checked by grep).
- **Import checks.** `CreateFileW`, `LockFileEx` and `MultiByteToWideChar` are KERNEL32 imports, which `tools/check-windows.sh:16` allows. Android `fcntl`/`open` are in `libc.so`, which `tools/check-android.sh:12` allows.
- **Precedent.** Shipping code already makes native calls: `rpc_preparation.c:11-29` uses `QueryPerformanceCounter`/`clock_gettime`.

**CI.**
- Runners: ubuntu-24.04 (host and sanitizers; pinned by W7), ubuntu-24.04-arm (host-arm64, W6) and windows-latest (MinGW and the RetroArch 1.22.2 package).
- The sanitizer job selects tests by the regex at `.github/workflows/ci.yml:72`, which includes `rpc-.*`.
- No macOS runner exists.

**The design note is stale.** `docs/riscpc-session-preparation.md:97` still says running reset uses "the established engine reset".

## Rulings (Fable; R-7, R-10 and R-11 revised after Astra stage 2 and the checks)

### R-1 Base, sequencing and merge order (collision C5) — D-186 sequencing amendment

The A-3 RUNNING body and the `restage_drive0`/`reset_rebind` host fields exist only on R3c-3b part 2. R3d-3 may not edit that body (§3).

- **`slice/r3d3-own` (scope 12a).**
  - It starts from pinned `main` and carries no preparation wiring and no store enforcement (F2).
  - It is independent of part 2 and merges before `slice/r3d3-host`.
  - It may merge before or after part 2; whichever merges second rebases.
- **`slice/r3d3-host` (everything else, including 12b).**
  - It is stacked on part 2's reviewed head after run B, once that head has merged `main` (with R3c-2, R3d-1 and `slice/r3d3-own`).
  - It rebases onto part 2's **merged** commit, after part 2's promotion run and QA, and merges after it.
  - "RUNNING block byte-unchanged" is judged against that merged commit, whose `:644` already carries part 2's session-kind conversion.
  - Transitively, R3d-3 waits for R3c-3a, because part 2 is stacked on it ([3b] `efafff9`).
- **Precondition 1.** R3c-2 (PR #408) and R3d-1 (PR #417) are on `main`.
- **Precondition 2.** Part 2's head passes the R3d-1 source guard, at both its frontend sites and its module sites (`rpc_preparation.c:627`, `:644`, `:679`).
  - The conversion is part 2's work. Sol does it in part 2's next run, under Fable's amended part 2 work order.
  - It is not left to R3d-3 or to Fable.
- **Consequence.** M1 (R3d-4) waits for part 2's merge. This amends D-186's note that "R3d-4 needs nothing from part 2", and Fable records it as a D-186 amendment.
- **Rejected alternatives:**
  - Part 2 lands its reset interface as an early PR. That would drag in the attempt module.
  - **Astra stage 2's smaller part-2-owned reset foundation** (callbacks, barrier, helper admission and the frozen RUNNING body, without controller delivery). Rejected for now:
    - part 2 is near completion, so the split adds a review cycle for little schedule gain;
    - M1 independence is not currently on the critical path.
    - This is revisited if part 2 slips.

### R-2 Setter ownership (collision C2)

- Part 2 keeps `arc_disk_rpc_pending` as the single first-statement admission function in all four setters (eject, index, replace, add).
- R3d-3 replaces its body with one of two things, and owns its tests:
  - the host predicate (R-3);
  - or the existing non-preparation clauses, with session kind in place of `arc_engine_is_rpc()`.
- Part 2's post-admission policy (`arc_engine_rpc_launch_media_changed`, the boot-watch split) is untouched.

### R-3 Pending predicate (collision C3)

- **Preparation route.** The host sets a frontend flag at load when it creates the session. Today that means plan-core `persist` plans; later it means R3d-4's caller.
- **Host-pending** means a preparation-route session and one of:
  - no preparation object;
  - state ≠ RUNNING (VERIFY through START, FAILED including the R-11 drain, CANCELLED);
  - `arc_riscpc_preparation_execution_allowed` false.
- On the preparation route, `retro_run`, `retro_reset`, the setters and the keyboard and poll paths use this predicate and session kind, never `arc_engine_is_rpc()`/`arc_engine_active()`. The predicate is checked before the active-RPC route.
- **Direct-start plan sessions** (no `persist`) are not on the preparation route. They keep today's active-RPC route, including the direct `arc_engine_reset` (AC-1, AC-4).

### R-4 Test export (collision C8)

- `retro_reset` now routes, so in the same run:
  - the runner's `--preparation-reset-at` and `--preparation-reset-stage` call `retro_reset`, which the runner already loads ([3b] `tools/rpc_runner.c:266`). The CLI is unchanged.
  - `arc_test_plan_preparation_restart` is then removed from `tests/arc_test_plan.c/.h` and `tests/rpc_test_exports.def`.
- `tests/rpc_boot_plan_test.py` and `tests/rpc_launch_qualification.py` keep working through the runner. Their outputs differ only by delta 12.
- Part 2's qualification is closed before this branch merges (R-1).
- Part 2's reset matrix keeps its module-level form (class [1], calling `arc_riscpc_preparation_reset` directly). Its frontend form becomes real `retro_reset` on the plan core (class [2]).

### R-5 Notice contract, severities, arbitration and privacy (Amendment 3; F5)

- **Callback.** The module callback carries a severity: progress, timed advisory, terminal, or completion clear.
- **Clears the module stops sending:**
  - the pre-terminal clears (a terminal replaces any progress text): [3b] `:114`, `:413`, `:691` and `:693`;
  - the reset-head clear `:640`;
  - the cancel clear `:676`;
  - the free clear `:726`.
- **Host-owned clears.** The host issues reset, unload and switch clears, because it knows the reason. They are sent at priority 3 or higher (P1; default 3).
- **Precedence:** terminal > advisory > progress. The hold counts as progress.
  - The first retained terminal stays visible, with two exceptions. Both key on module state, never on text:
    1. A terminal raised when the module state becomes FAILED replaces a retained non-stopping terminal (CMOS refusal, HD terminal).
    2. The R-11 drain-outcome terminal replaces the fault terminal it completes, once.
  - Every terminal is logged once.
  - On the `SET_MESSAGE` fallback (one slot): while an advisory's lifetime runs, progress refreshes and the hold are logged but not displayed. When the advisory expires, the current progress text is shown at once.
  - On the extended path the same rule applies, unless P1 shows that two messages stay visible together. In that case progress continues alongside the advisory.
- **Lifetimes:**
  - progress: refreshed every 60 frames, gone no later than 120 frames after its last refresh;
  - advisory: 5 s on the extended path and 300 frames on the fallback, the same as store recovery advisories (`docs/riscpc-session-preparation.md:116-117`);
  - terminal: retained until a host clear, with the fallback re-sent every 60 frames.
- **Privacy of message paths (F5).** Scope: every notice issued by the host or the module (the catalogue notices).
  - Every host extended message is sent with `RETRO_MESSAGE_TARGET_OSD` only.
  - Fallback (`SET_MESSAGE`) texts never contain archive member names, paths or other private strings. The on-screen fallback text is the redacted catalogue form, frozen in the §7 amendment.
  - Only an OSD-only extended message may show the unredacted text. If P1 cannot confirm that RetroArch keeps OSD-only extended messages out of its log, the extended path also uses the redacted form (the default).
  - No ArcDroid log line from the host or the module, at any level, carries a member name or path.
  - Tests capture RetroArch's own log, not only ArcDroid's log callback (AC-3).
  - **Shared `arc_message` stays unchanged** (`TARGET_ALL`, logs its text). Changing it would alter ordinary-route output (AC-13). Its privacy is a carried item for R3d-4, which routes ordinary RPC sessions (check M3).

### R-6 Startup hold (Q11)

- **Text.** At the transition to RUNNING, the host itself emits "Starting RiscPC" on every path, receipted or not, and defers the completion clear.
- **No hold** when a terminal is already retained at RUNNING, for example a CMOS refusal.
- **Bound.** The hold ends when `arc_guest_time_us` reaches the pair's bound, or at the first terminal, whichever comes first.
  - Fable freezes a per-pair table in the design note before dispatch. Each entry is the measured emulated time to the first non-black field on that pair, plus 5 s, capped at 25 s. For example, 3.50 ARM610's 18 s gives 23 s (`docs/handoff.md:421`).
  - A pair with no measurement uses 25 s.
  - Accepted tradeoff: on fast pairs the text overlays the desktop for at most about 5 s.
- **Restart and cancel.** A successful RUNNING reset restarts the hold. A reset into the pending branch, an unload or a switch cancels it immediately.
- **Advisories** pre-empt the hold per R-5. The hold resumes if it is still inside its bound.
- The hold never claims desktop readiness. R3d-4's N-12 replaces the held text, and R3d-5 replaces the bound with the handoff signal.

### R-7 Option freeze, reset re-staging and the drive-0 view (scope 5; F3)

- **Freeze baseline.** The option values read at load (by the plan-core load now, by the R3d-4 caller later), not the attach-time re-read.
- **While pending,** the host also polls `GET_VARIABLE_UPDATE` and applies the freeze.
- **N-11** ("{option} takes effect after Restart.") is shown once per changed option per session. The shared Arculator flag `arc_jfd_tab_pending_notice_shown` is not reused.
- **RUNNING Restart (unchanged).**
  - Drive 0 is re-staged under the current disk-writes, JFD tab, short-tail and JFD-omitted values. This happens inside `restage_drive0`, under the barrier, within the frozen transaction.
  - The host sets the explicit-helper write policy there, through `arc_riscpc_preparation_helper_write_policy`, from the current disk-writes value.
  - Before calling `arc_riscpc_preparation_reset`, the host completes any in-flight CMOS publication (R-9).
- **Non-RUNNING Restart** covers VERIFY, RECOVER, VALIDATE, RECORD, DEFAULTS, every GENERATE substep, SAVE_DEFAULT, SAVE_RECORD, every POPULATE substep, every START substep, and FAILED. It edits the non-frozen lines `:670-673`. The order is fixed:
  1. The module reset invalidates the attempt, stops the engine (isolated generation or START construction) and releases offline resources through `release()`.
     - The content lock is kept and the defaults lock is released (R-10, R-12).
     - The module drops its drive-0 view before step 2, so no stale view survives.
  2. The module then calls a new host callback, `restage_stopped`, before `restart()` and before any further preparation step can run. In it the host:
     - re-stages drive 0 into frontend backing only, under the current option values at Restart (no engine insert, no eject, no attach, no drive-1 work);
     - replaces the module's drive-0 view;
     - updates the explicit-helper write policy from the current disk-writes value.
  3. `restart()` resumes VERIFY. Preparation continues on the next frame.
  - The engine is stopped throughout steps 2 and 3.
  - **Re-staging failure is a stopped-state failure transition.** If `restage_stopped` fails:
    - the session becomes FAILED with a class B terminal giving the cause;
    - the engine stays stopped, nothing is attached, and no store mutation follows;
    - the content lock is kept;
    - a further Restart retries the whole sequence.
- **Drive-0 view.** The module is the only drive-0 attacher at START, using its view, and it dereferences that view only at START.
  - One new `rpc_preparation.h` entry replaces the drive-0 view (it copies descriptor and path). It is legal only in two places:
    - inside `restage_stopped`, with the engine stopped;
    - inside `restage_drive0`, with the barrier closed.
  - START after a restart attaches the drive-0 image selected at Restart time, or nothing if drive 0 is ejected.
- **Drive 1 (A-3 step 4).**
  - The generated helper is preparation-owned and read-only.
  - An explicit drive-1 image (plan core only) is supplied at begin as `request.helper` with `helper_override=true`. This replaces the attach-time `helper_override` call.
  - `arc_riscpc_preparation_helper_write_policy` is widened to also accept the stopped window inside `restage_stopped`. This edit is outside the frozen block.
  - An explicit empty drive 1 stays empty.

### R-8 Unload persistence failure (Amendment 4; F6)

- **Logging.** Free logs the failure always, whatever `refusal_reported` says.
- **Display.** Free reports the failure as a timed advisory, not as a terminal followed by its own clear. The host's unload clear comes first, so the advisory stays visible for its lifetime after unload.
- **Marker.** While it still holds the content lock (12b), Free writes the zero-byte `cmos-riscpc/<content>-m<N>-<fw64>.unsaved`. The write goes through a new CMOS-store marker interface (write, query at RECORD, removal within the next successful persist).
  - The CMOS listing matches only `.0`/`.1` (`rpc_cmos_store.c:262-266`), so the marker is invisible to it.
  - **Publication-only failure** (the marker write itself is not injected to fail): the marker **must** be written.
  - **Marker-write failure** (for example, the directory has become unwritable): logging and the immediate advisory are the only outcomes. No marker exists, and the next load shows no N-13.
- **Next load.** At RECORD the next load shows the timed advisory N-13: "RiscPC settings from the last session could not be saved." The next successful persist removes the marker, as one namespace unit.
- **Exception.** A refused or corrupt record writes no marker, because its terminal already shows at every load (`tests/rpc_orchestration_test.c:378-389`).
- **Covered paths:** `retro_unload_game`, `retro_deinit` without unload, and the switch release.

### R-9 S-4 and the frozen synchronous publications (P8 → AC-6, AC-4; check H3)

- **Synchronous outcome.** If P8 shows a live CMOS publication within the D-152/D-160/D-161 budgets on two pairs, record the acceptance with those numbers and keep the publication synchronous.
- **Stepped outcome (the default).** Step the publication across maintenance ticks, bound the loss window at **N = 60 frames**, and test a kill inside it (Amendment 7).
- **In-flight completion (needed whenever S-4 is stepped).** `put_begin` refuses while `putting` is set, so every synchronous publication first completes an in-flight publication step by step, as free already does (`:717`):
  - **Reset.** The host calls a new `rpc_preparation.h` entry that completes the in-flight publication before it calls `arc_riscpc_preparation_reset`. The frozen `:663` put then sees no in-flight publication.
  - **Swap rollback.** `launch_aborted` completes the in-flight publication before its `:280` put. This edit is outside the frozen block.
- **Budget treatment.** The `:663` and `:280` publications stay synchronous (frozen, or part 2's), so P8(b) decides how they are budgeted. By default they are error-path exceptions, recorded in the decision log and each counted in the summary line with its measured cost.

### R-10 Ownership protocol (A-5 as amended; scope 12; F1) — D-186 A-5 amendment and D-021 exception

Automatic lease breaking is **withdrawn**. The owner record, nonce, renewal, fencing, breaker, guards, graveyard, stale threshold S and the 3.5 s post-kill D-172 amendment are all removed.

- **Mechanism.**
  - Each key has a dedicated zero-byte sentinel file. The core takes an exclusive, **non-blocking** advisory lock on it, and the operating system releases the lock on process death.
  - The lock is held from acquisition until the session's last store write and close.
  - The calls are made in one narrow platform module, outside the VFS (the D-021 exception):

    | Platform | Calls | Contention | Transient (retry within W) | Unsupported |
    |---|---|---|---|---|
    | Linux and Android | `open(path, O_RDWR\|O_CREAT\|O_CLOEXEC, 0600)`, then `fcntl(F_OFD_SETLK)` with `F_WRLCK`, whole file, `l_pid=0`. The build fails (`#error`) if `F_OFD_SETLK` is undefined. | `EAGAIN` only | `EINTR` on open or lock; `EEXIST` on the create race | `EINVAL` (OFD unavailable), `ENOSYS`, `ENOLCK`, `EOPNOTSUPP` |
    | Windows | `CreateFileW(path, GENERIC_READ\|GENERIC_WRITE, FILE_SHARE_READ\|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)` (no `FILE_SHARE_DELETE`, no overlapped flag, not inheritable; path from `MultiByteToWideChar(CP_UTF8)`), then `LockFileEx(LOCKFILE_EXCLUSIVE_LOCK\|LOCKFILE_FAIL_IMMEDIATELY)` over one byte | `ERROR_LOCK_VIOLATION` | `ERROR_SHARING_VIOLATION` on open (antivirus, sync clients, indexers) | `ERROR_INVALID_FUNCTION`, `ERROR_NOT_SUPPORTED` |
    | macOS (development only) | `open` (same flags and mode), then `flock(LOCK_EX\|LOCK_NB)` | `EWOULDBLOCK` | `EINTR` | `ENOTSUP`, `EOPNOTSUPP` |

    - `EACCES` from the OFD lock call is not treated as contention. It is `lock-error` (N-15), because an SELinux or daemon refusal must not show as "another copy".
    - A transient result still present at the end of W gives N-15 (`lock-create`).
  - **Classic `F_SETLK` fallback: not adopted.** This deviates from the F1 ruling, and Fable confirms it.
    - Classic locks belong to the process. Any `close()` of any descriptor for the lock file, by anything in RetroArch (a scan, an archive probe, a file picked in the browser), silently drops the lock [L2].
    - No single-open-file discipline inside the core can bind RetroArch's code.
    - Kernels without OFD therefore get N-15. That cost goes to the Founder under PRD Q3 (Next 2).
  - **Never used:**
    - classic `fcntl` locks;
    - `flock` on Linux or Android. On `/storage/emulated` FUSE it gives `ENOSYS`, or succeeds under FUSE-BPF pass-through;
    - any blocking lock call (`F_OFD_SETLKW`, `LockFileEx` without `FAIL_IMMEDIATELY`);
    - inheritable handles or descriptors without close-on-exec;
    - data reads or writes, truncation or extension through the lock descriptor.
  - **Same-process semantics.** OFD and `flock` refuse a second open file description in the same process [L1][L2]. For `LockFileEx`, W1 states only that the locked region cannot be accessed through another handle, so P10b must prove the same-process refusal.
    - A leaked descriptor therefore shows as a visible N-14 self-contention, never as silent shared ownership.
    - OFD `F_OFD_GETLK` reports `l_pid=-1`, so self-contention cannot be told apart from another process. N-14's text therefore also says to restart RetroArch.
    - A reload after a clean release must re-acquire.
- **Identity verification (check: rename-over or replacement).**
  - **POSIX, after every successful lock.** The module compares `fstat(fd)` with `stat(path)`. The device and inode must match, with `nlink ≥ 1`. On a mismatch it releases and retries the next frame within W.
  - **POSIX, while holding.** The holder re-verifies once per maintenance frame and inside the token check before each publication begin (CMOS put begin, HD publication begin, launch-record publish begin, marker write).
    - A mismatch sends the session to the R-11 lost-owner path: no further mutation, and the class B terminal **N-18**: "ArcDroid's RiscPC lock file was replaced by another program; this session stopped without saving. Restart this content."
    - Each check counts as a lock unit (scope 8).
  - **Windows.** Without `FILE_SHARE_DELETE`, delete and rename-over of a held lock file fail with a sharing violation, so no in-session check is needed.
  - **Every platform, once after acquisition.** A VFS stat of the lock path must show a regular file of size 0. Otherwise the session gets N-15 (`lock-root`). This catches a frontend VFS that does not map to native paths.
- **Placement and keys.** Lowercase, under the same `<save>/arcdroid/` root and keys as the stores they protect:
  - content lock: `<save>/arcdroid/locks-riscpc/c-<content40>.lock`;
  - defaults lock: `<save>/arcdroid/locks-riscpc/d-<fw64>-m<N>-p<proc>-t<timing>.lock`.
  - `locks-riscpc/` is created through the VFS with the new 12a mkdir primitive, which treats "exists" as success when a re-stat shows a directory.
  - **Lock files are never deleted.** Unlinking a held lock file would let a contender create and lock a new file, giving two owners. There is one zero-byte file per key.
  - **Alias handling.** The lock is on the file object the OS resolves. Any spelling, symlink or case variant of `<save>` that reaches the same store reaches the same lock file, provided the keys are computed identically (they are the store keys).
  - **Root check.** The module requires an absolute native path: POSIX `/`, drive-absolute or UNC, with no `scheme://` prefix (for example `saf://`). A relative path or a scheme root gives N-15 (`lock-root`). Plan §4's class A refusal at load still applies where `begin` already decides it (Next 1).
- **Acquisition and waiting.**
  - Each pending frame makes one non-blocking attempt **before** the W check, so the first frame after a long menu pause still tries first. The open descriptor or handle is kept between attempts.
  - Each attempt is one unit outside the `arc_io` census, admitted by measured cost (D-171) within the offline budget (D-172(1)). Its cost is reported as a unit kind of its own (scope 8). The `locks-riscpc/` mkdir is an `arc_io` namespace unit, declared through `next_namespace`.
  - **Content-lock contention.** The contender waits under "Checking the RiscPC hard disc". After W = 10 s (host monotonic time) it shows the class B terminal **N-14**: "This RiscPC title is open in another copy of RetroArch. Close it there, or restart RetroArch, then restart this content."
  - **Defaults-lock contention.** The contender waits under the first-use settings text. After W_d = 120 s (about 20 s of Odin3 generation, with margin) it shows the class B terminal **N-16**: "Another copy of RetroArch is preparing RiscPC settings; restart this content when it has finished." Once it acquires the lock, it re-reconciles and adopts the record it finds.
  - Contention is never broken. A suspended, frozen or menu-paused holder keeps its lock, and the contender gets N-14 or N-16 within its bound. That is the correct outcome.
- **Failure policy.** These cases give the class B terminal **N-15**: "ArcDroid cannot lock its RiscPC save files; choose a save folder on this device that RetroArch can write to." N-15 carries no path (F5). The log gives a reason token with no path: `lock-root`, `lock-create`, `lock-unsupported` or `lock-error`.
  - VFS mkdir failure for `locks-riscpc/` or its parents;
  - a native open failure, including a read-only lock file, or a transient error that outlasts W;
  - a root the module refuses, or a failed post-acquire VFS stat;
  - locking unsupported (the table's last column);
  - any other lock-call error, including OFD `EACCES`.
  - The session acquires no store.
- **Lock order.** Content lock first, then defaults. A session holds the defaults lock only while it holds its content lock, so no lock wait can deadlock.
- **Release.** Unlock and close happen after the final persist, the R-11 drain and the HD close.
- **Reopen after a kill.** Death releases the lock, so no lease wait applies.
  - On Linux, Android and the Mac, the next load acquires at its first attempt once the holder process has exited. D-172's 1 s reopen target stands unamended.
  - On Windows, release after termination "depends upon available system resources" [W1], so the contender retries within W. The time is recorded as informational (Amendment 18).
  - An Android crash, as opposed to a force-stop, leaves the process alive while the crash dump is written. W covers that.
- **P3 consequence (corrected, check M5).** The second run-ahead instance never loads ArcDroid content (Verified state). If a future frontend did load it:
  - it would **load successfully**, because acquisition happens in VERIFY frames, not in `retro_load_game`;
  - it would wait W and then show N-14 on the shared OSD;
  - RetroArch would show the secondary's video, and `RUNAHEAD_SECONDARY_CORE_AVAILABLE` would not be cleared.
  - The outcome is still safe, because no second writer exists, but it is a visible failure. Fable escalates before R3d-4 if P3's run contradicts the source reading. Adding savestates (PRD Q5) re-opens P3.
- **Probes before acceptance (P10b, with the 12a ownership process binary).** All of these are expected results until a device confirms them:
  - **Odin3 shared storage** (`/storage/emulated/0/arcdroid-r3d/`):
    - `flock` gives `ENOSYS`, or success under FUSE-BPF (recorded, not used);
    - OFD between two **app processes** gives `EAGAIN`;
    - two opens in one process conflict;
    - force-stop releases the lock immediately;
    - a rename-over leg and a delete leg, in which the post-acquire and in-session identity checks fire;
    - the save root opened with a different letter case reaches the same lock;
    - record the Android and kernel versions, the MediaProvider module versionCode (a version change triggers a re-run), whether FUSE-BPF is active (with the observed `flock` result), and `/proc/self/mountinfo` for both partners. An `adb shell` partner counts only if its mount view matches.
  - **Odin3 app-private storage:** the same legs, on internal `/data/user/0/<pkg>` and on external `Android/data/<pkg>`.
  - **Second Android device** (D-191 Founder item): the shared-storage legs, when the device is available.
  - **Windows NTFS:**
    - a second handle in the same process is refused, and so is a second process;
    - delete and rename-over of a held lock file fail with a sharing violation;
    - a transient-open leg with antivirus or OneDrive activity;
    - time-to-release after `TerminateProcess`.
  - **Mac APFS:** `flock` same-process and two-process legs; OFD if the SDK provides it.
- **Fallback if P10b fails on Odin3 shared storage (Founder decision before 12b):**
  - **Fallback 1:** atomic VFS mkdir acquisition with bounded refusal (N-14), stale ownership cleared only by an explicitly exclusive, user-initiated recovery.
    - It needs a further A-5 recovery amendment.
    - It gives up the automatic post-kill reopen.
    - It has a material UX cost under Android low-memory kills.
  - **Fallback 2:** OS locks wherever they work. On storage where locking is unsupported, N-15 points to app-private storage, or Fallback 1 applies there.
- **Accepted residuals.** The design note states them, and the AC-11 and AC-12 legs cover the testable ones.
  - Network save roots (NFS, CIFS, UNC) are outside the guarantee. NFS can lose advisory locks.
  - Synced save trees (two machines writing replicas through a sync tool) are outside the guarantee.
  - On POSIX, a replacement or deletion of a held lock file is detected within one maintenance frame or at the next publication begin (N-18). Within that window a contender can acquire the new file.
  - Aliasing below `<save>/arcdroid/` (a symlink or junction on `hd-riscpc/` or `cmos-riscpc/` shared between two save roots) puts two lock files over one store, so it is not excluded.
  - A client on a different Android mount view, such as a pass-through path, is not excluded. Two RetroArch app processes share the FUSE view (P10b checks this).
  - A `fork` without `exec` in the frontend process inherits the OFD lock until the child exits.
  - A holder blocked in a hung FUSE daemon (D-state), a Windows error-report dialog or a debugger keeps the lock until it exits. The contender gets N-14 within W.
  - Windows release latency after termination is OS-dependent; the bound is W.
  - Android 10 and earlier (sdcardfs) and kernels without OFD are covered by PRD Q3 (Next 2).
  - **Same-UID rename-over on Android shared storage (P10b, S68, D-208).** On the Odin3's `/storage/emulated` FUSE view, a rename-over of the held sentinel performed by RetroArch's own app UID is neither detected nor excluded: the FUSE daemon answers `fstat` on the held descriptor by the current path, so descriptor and path identity agree on the new file, and a second process then acquires the replacement. A deletion is detected at the next frame; a rename-over by another UID (the shell) is detected at once; internal app-private and `Android/data` storage detect a same-UID rename-over at the next frame. No cooperating client renames sentinels and RetroArch does not operate on `locks-riscpc/`, so this is recorded, not designed against; the design note states it.

### R-11 Fault-path persistence (J15, Amendment 8; F4)

- **Capture.** Every RUNNING-to-FAILED transition inside `arc_riscpc_preparation_maintenance` captures the CMOS snapshot while the engine is still readable, **before** the stop. There are two: the engine fault (`:686`) and the observation-invalidation failure (`:697`). The terminal is then raised at once, with its cause.
- **Bounded drain.** Persistence is a drain of that captured snapshot across the following FAILED frames, not one synchronous exception:
  1. If a CMOS publication was already in progress (`record.putting`, including S-4 stepped publication), it completes first, step by step.
  2. The captured snapshot is then published with `put_begin` and `put_step`.
  - Each namespace step is its own unit within the offline budget (D-172(1)), with D-169 isolated spikes attributed per step.
  - The drain ends within **60 frames**. Otherwise it stops and reports "could not be saved".
  - **Entry point.** The host drives the drain through a new `rpc_preparation.h` FAILED-frame entry, with a query that reports whether a drain is active. It is not driven through `maintenance` or `frame`, because both return outside RUNNING and pending (`:549`, `:685`).
- **Outcome.** When the drain ends, the fault terminal is replaced once (R-5 exception 2) by the same cause followed by "latest CMOS snapshot saved" or "could not be saved", as the reset failure does.
- **Reset, switch and unload during the drain.**
  - A reset requested during the drain is recorded by the host and runs when the drain ends (at most 60 frames).
  - Unload and switch finish the drain within free, as free already finishes a pending publication (D-170(5)). A switch is an unload and a load inside one frontend operation, so it cannot be deferred across frames.
- **HD.** The HD outcome is the store's close result at reset or unload (scope 6). If it fails, it is reported with N-17.
- **Ownership.** The content lock is held until the drain and the HD close have finished.
  - A path that does not hold the lock performs no store mutation. It closes the stores in discard mode and reports "could not be saved". This covers acquisition never completed, release already run, a reported lock-call failure, and an N-18 identity mismatch.
- **Edit location.** The capture is in `arc_riscpc_preparation_maintenance`, and the drain is new FAILED-state work. Both are outside the frozen block.

### R-12 CANCELLED

- No frontend path cancels. CANCELLED is covered by class [1] evidence only.
- A free before RUNNING is a release (unload or switch), not a cancel.
- Cancel and free drop the content lock after `release()` and after any drain and HD close.
- The pending reset keeps the content lock and drops the defaults lock, which is re-acquired if DEFAULTS is reached again.

## Scope

**Common to both branches: no ordinary route and no gate.**

1. **Host module** in `libretro/` (Sol names it), on `slice/r3d3-host`.
   - **Ownership.** It owns the one `ArcRpcPreparation` per preparation-route session. Session kind and session ID are injected from `arcdroid_libretro.c` (R3d-1 guard, collision C4).
   - **Entry points:**
     - production begin, from a resolved request;
     - in `ARCDROID_TEST_PLANS` builds only, a low-level begin over `arc_riscpc_preparation_low_level`.
       - It takes an optional test-extension table (started-after, attach-after, attached-after, diagnostics-after), run after the production behaviour.
       - The shipping host never references `_low_level` or the table (`rpc-symbol-guard`).
       - The plan core's rate seam, `observation_start`/`observation_frame`, script activation, observer override, gate fault and the HD cost wrapper all move into that table. Re-setting IDE through `arc_engine_rpc_ide` is allowed only there.
   - **Callbacks:**
     - clock: injectable without flags (D-170(3));
     - notice: with severity (R-5);
     - log;
     - attach;
     - started: a production no-op;
     - diagnostics: a production log drain;
     - attached: the frontend rebind hook;
     - `restage_drive0` and `reset_rebind`, promoted from [3b] `arcdroid_libretro.c:7569-7612` and `tests/arc_test_plan.c:134-142`;
     - `restage_stopped`, new (R-7).
   - **Attach** replaces `arc_rpc_plan_media`:
     - it does not prepare media again, because media is staged at load and passed as views;
     - it keeps the backing until engine stop;
     - it never attaches drive 0 itself, because the module attaches drive 0 from its view (R-7), and it never attaches through ADFFS;
     - it does not set the IDE backend again (G-2);
     - drive 1 has exactly one owner: preparation mounts it ([3b] `rpc_preparation.c:506-509`), and the host never mounts over it (G-1d);
     - observers bind only after attachment (D-177(7)).
   - **Reset callbacks:**
     - `restage_drive0` (RUNNING only) re-stages drive 0 only, updates the module's drive-0 view and sets the drive-1 write policy (R-7);
     - `restage_stopped` (non-RUNNING only) re-stages without any engine call, then updates the view and the write policy (R-7);
     - `reset_rebind` runs the engine reset, the observer rebind with the module's verified catalogue, and the frontend input, OSK, keyboard, sampling and guest-time reset. It does not use the test `input_ready` seam.
   - **Allowed interface changes, and only these:**
     - `rpc_preparation.h`:
       - the severity notice type (R-5);
       - the drive-0 view replacement (R-7);
       - the `restage_stopped` host field (R-7);
       - the stopped-window widening of `arc_riscpc_preparation_helper_write_policy` (R-7);
       - a const verified-catalogue getter;
       - the in-flight publication completion entry (R-9);
       - the FAILED-frame drain entry and its active query (R-11);
       - the 12b ownership hooks;
       - an accessor for the retained HD close result (R-11, scope 6).
     - `rpc_hd_store.h`, `rpc_cmos_store.h` and `rpc_launch_record_store.h` (12b):
       - an ownership token bound to the store handle at `arc_riscpc_hd_open_begin`, `arc_riscpc_cmos_store_begin` and `arc_riscpc_launch_record_begin`. Every mutation checks the bound token, so the put and step signatures are unchanged and the frozen `:663` call is untouched (check H2);
       - an HD close that returns its flush and close result, with a discard mode that performs no mutation;
       - the CMOS-store `.unsaved` marker write, query and removal (R-8);
       - a launch-record-store entry that invalidates the observation file under the token, replacing the raw `arc_io_remove(p->observations.path)` at [3b] `:697`.
   - **Staying test-only in the plan core:**
     - `.arcplan` parsing;
     - the authored ROM, seed, factory and import modes;
     - the catalogue override;
     - scripted input;
     - the observation trace;
     - the HD cost wrapper;
     - `pending_rate`;
     - the crash locator;
     - `arc_libretro_test_rpc_catalogue` (`arcdroid_libretro.c:379-390`).

2. **Pending frames** (R-3).
   - While host-pending, each `retro_run` gives one black frame through the normal video path, so `arc_send_video`, `arc_present_frame` and `arc_video_pitch` stay coherent.
   - Silent samples per frame are 48000 / the announced rate, with the fraction carried between frames (as `arc_rpc_fault_audio_fraction` does at `:8016-8021`). That is 800 at 60 Hz.
   - Input is isolated.
   - There is no per-frame `arc_notify_av_change`. The 640×480 @ 60 cadence is announced once at load.
   - FAILED, including FAILED after RUNNING and the R-11 drain frames, shows black at the last announced geometry and rate with the retained terminal.
   - A restart from FAILED-after-RUNNING re-announces 640×480 @ 60 once, if the last announced values differ, because the D-172 budgets are T-at-60 Hz figures. It is counted as a frontend-caused change at a guest → pending transition. No other AV call is made while pending.
   - The execution barrier gates `arc_rpc_present_run`.

3. **Live maintenance.** Exactly one `arc_riscpc_preparation_maintenance` per running frame, and none while pending, generating or draining.
   - It keeps the D-177(7) boundary order: refusal boundary, then diagnostics, then disc boundary, then maintenance.
   - A RUNNING-to-FAILED transition inside maintenance ([3b] `rpc_preparation.c:686`, `:697`) follows R-11 (capture, stop, drain through the FAILED-frame entry) and routes the next frame to the FAILED presentation (J15).
   - The in-session lock identity check (R-10) runs inside maintenance (12b).
   - S-4 is handled per R-9.

4. **Reset routing.** Every reset of a preparation-route session, in any state (R-3), goes to `arc_riscpc_preparation_reset`.
   - On the preparation route, delete the direct `arc_engine_reset` call and `arc_rpc_plan_reset_media`, which re-prepares drive 1 (`:7473-7513`, `:7623-7642`). Direct-start plans keep their path (R-3).
   - Both branches first clear what the Arculator branch clears:
     - input, keyboard raw, sampling and guest time;
     - error-hint generation and state ([d1]);
     - disc owned, previous and swapped state;
     - the disc notice ring;
     - `arc_engine_disc_clear` and `arc_disc_invalidate`.
   - The host issues the reset clear (R-5).
   - Before a RUNNING reset, the host completes any in-flight publication (R-9).
   - A non-RUNNING reset follows R-7's order: release, then `restage_stopped`, then restart. A reset during an R-11 drain runs when the drain ends.
   - After a successful RUNNING reset, AV is issued at the next guest field only if the values differ from those last announced.
   - Launch-outcome cancellation stays inside part 2's interrupt.
   - Start from the R3c-2 run H/I and R3d-1 text of `retro_reset` (collision C1).
   - **Frozen region.** The `if(p->state==ARC_RPC_PREP_RUNNING){…}` block of `arc_riscpc_preparation_reset` stays byte-identical to part 2's merged commit ([3b] `:643-669`).
     - Edits elsewhere in `rpc_preparation.c` are limited to R-5, R-6 (status), R-7 (the non-RUNNING lines `:670-673`, `restage_stopped`, the view, the write-policy widening), R-8, R-9 (in-flight completion, including in `launch_aborted`), R-11, R-12 and 12b.
     - Each edit must be diffable by QA.

5. **Option freeze and re-staging** per R-7. The production freeze also holds in shipping builds. Today `arc_rpc_present_run` copies options without a freeze (`:7936-7963`).

6. **Teardown (F4, F6).**
   - **Live engine** (RUNNING with an active engine): Free captures the latest snapshot and persists it before engine stop, as today.
   - **Engine already stopped by the module** (FAILED, failed attachment):
     - Free does not require a live engine;
     - where R-11 applies, the snapshot was captured before the stop;
     - a failed attachment has no live CMOS beyond the published record.
   - **Detach before free.** On every path (unload, deinit, switch, failed attachment, FAILED, CANCELLED), the engine has detached IDE and both borrowed media before any media entry, helper entry or backing is freed.
   - **Exactly once.** Final cleanup (`release()` and the host's media release) runs exactly once per session.
   - **HD close result.** `release()` retains the HD close result in the preparation object through teardown, and Free returns it with the CMOS outcome.
     - Reset reports a failure as the advisory N-17: "Recent RiscPC hard disc changes could not be saved."
     - Unload logs it and shows N-17 after the unload clear, as R-8 does for CMOS.
   - **Locks.** They are released after the final persist, the drain and the HD close (12b).
   - Nothing allocated for the session survives release (G-9b).
   - Remove the dead `arc_test_plan_persist` (`tests/arc_test_plan.c:157-158`).
   - The Windows crash locator stays test-only.

7. **Notice host** (R-5, R-6, R-8, R-11). Nothing is inferred from text or from `arc_test_plan_cmos_phase()` (`:7536`).
   - **N-8.** The revision-1 advisory ([3b] `rpc_preparation.c:350`) is emitted as an advisory, logged, and protected by R-5's precedence from the `refresh()` overwrite.
   - **Regeneration wording.** A receipted store generating CMOS for a new pair shows the first-use settings text. `status()`'s receipted short-circuit ([3b] `:72`) no longer covers DEFAULTS through SAVE_RECORD.
   - **Startup hold.** It lives in the host, emits its own text and defers only the completion clear ([3b] `:521`).
   - **Unload failure.** It follows R-8 ([3b] `:725-726`).
   - **Other advisories.** Part 2's "launch delivery unavailable" texts and D-190's one-drive notice use the advisory severity. R3d-1's LOW-4 notice keeps the class frozen in R3d-1.
   - **Running-phase store advisories** get their own case (AC-3).
   - **New texts** N-13 to N-18 (R-8, R-10, scope 6), and the redacted fallback forms (R-5).

8. **Verifiable channels** that ship permanently and carry no private data.
   - **Redaction (F5).**
     - Host and module log lines name each notice by its §7 catalogue ID (N-n, or a fixed reason token for texts outside §7), followed by the redacted text. Member names and paths are replaced by the frozen redaction tokens.
     - No log level carries the unredacted text.
     - Fallback on-screen texts use the same redacted forms, and host extended texts are OSD-only (R-5).
     - The design note states the ID-to-text and ID-to-redacted-text mappings that §9 uses as its oracle.
     - Today's member-bearing ERROR log at `:7539` (from `rpc_boot.c:47-50` through `rpc_preparation.c:231`) changes accordingly (deltas 17 and 20).
   - **Status lines.** One log line per status transition, deduplicated on severity and text, rather than one per 60-frame refresh. One line per terminal and per advisory.
   - **Summary lines (A-7, Amendment 1).** One summary line at RUNNING, FAILED or release before RUNNING, covering the pending phases. A second at release, covering the live phase. Each carries:
     - frames, elapsed host time, final state;
     - per phase (offline, generation, attach, live, drain): maximum preparation work per call, maximum and p99 complete `retro_run` duration, and maximum and p99 host interval between `retro_run` calls;
     - the count of intervals over 2 T;
     - exception counts attributed to D-169 (an isolated indivisible namespace unit), to D-162 (an install-checkpoint tick), and to the two error-path synchronous publications (R-9);
     - frontend-caused and guest-driven AV changes, counted separately;
     - lock attempts, lock-wait frames, identity checks, and the maximum lock-call cost, a unit of its own outside the `arc_io` census;
     - D-172 endpoints in ms from load entry: first pending presentation, preparation RUNNING, first guest field, and desktop readiness logged as `unavailable`.
   - **Measurement rules:**
     - T is 1 / the announced rate of the phase;
     - percentiles are nearest-rank over a static bounded histogram (0.05 T buckets up to 8 T, plus overflow);
     - intervals over 0.5 s are counted as frontend gaps (pause or menu), kept out of the over-2 T count, and reported;
     - fast-forward comes from `GET_FASTFORWARDING` when granted, and is logged as `unknown` otherwise.
   - **First-field line.** One post-first-field AV line: values, whether `SET_SYSTEM_AV_INFO` was issued, and the frame index.

9. **RPC load negotiation** (the plan route now, the R3d-4 caller later):
   - pixel format; on failure, release and return false ([d1]; P1b default);
   - `SET_CONTROLLER_INFO` and `SET_INPUT_DESCRIPTORS` with RPC labels and no A3010 joystick (riscpc.md §4.8; compare Arculator at `:7188-7198`);
   - `GET_INPUT_DEVICE_CAPABILITIES`;
   - `arc_resolve_inputs` at load;
   - keyboard callback;
   - OSK and sampling reset;
   - AV baseline 640×480 @ 60;
   - frame counters and fault flags.

10. **Per-frame parity.** OSK activation and its interruption notice, the input-overflow notice, and pad, keyboard and pointer behaviour match Arculator, or each difference has a test.

11. **Guards, each justified in the PR.**
    - **`slice/r3d3-own`:** the shipping-I/O guard amendment (scope 12a).
    - **`slice/r3d3-host`:**
      - add the host file to `sources` in `tools/check-shipping-io.sh` (beside `:95-96`);
      - `rpc-frontend-boundary-guard`;
      - the `backend-symbols.json` policy;
      - `rpc-symbol-guard`: no low-level entry or extension table in shipping;
      - `rpc-shipping-route` still proves there is no test entry and no ordinary routing;
      - the R3d-1 source guard, amended explicitly:
        - new files have zero `arc_engine_is_rpc(`, `arc_engine_capabilities(` and `arc_frontend_session_id(`;
        - `arc_engine_session_generation()` stays at 2 or is re-justified;
        - the `arcdroid_libretro.c` and `rpc_preparation.c` counts are re-justified against R-1's base, where part 2 has already met the guard;
      - a source guard that no `retro_reset` path **for a preparation-route session** reaches `arc_engine_reset` outside `reset_rebind`. The direct-start path is excluded by name and covered by its own compatibility test (F6);
      - a guard that every extended-message call in the host passes `RETRO_MESSAGE_TARGET_OSD`;
      - a guard that no `arc_io_remove`, `arc_io_rename`, `arc_io_create_file` or truncation in `rpc_preparation.c` bypasses a store's token check.

12. **Concurrent-writer ownership (A-5 as amended, R-10).**
    - **12a, on `slice/r3d3-own`, from `main`: the ownership primitive and its tests only.** It is dormant: it has no caller in preparation or in any store, and no store enforcement (F2).
      - **New `arc_io` primitive:** mkdir with no stat beforehand, returning created, exists or error.
        - It is used only by the lock module, to create `locks-riscpc/` and its parents, treating "exists" as success when a re-stat shows a directory.
        - `arc_io_make_path_step` is **not** changed here (that is 12b).
      - **Lock module** (for example `libretro/arc_lock.c`), outside the VFS and outside `engine/rpc` and `engine/rpcemu`.
        - **Operations:** acquire (non-blocking, one attempt per call), verify (identity, R-10), release, and a held query.
        - **Outcomes:** held, contended, transient, unsupported, root refused, create error, lock error, or identity lost. The caller maps them to N-14 to N-16 and N-18, but no caller exists in 12a.
        - It covers the platform backends of R-10. There is no classic `F_SETLK` backend or registry, and Linux/Android builds fail without `F_OFD_SETLK`.
        - A flag-free injectable backend table (open, try-lock, identity, unlock, close) supports deterministic CTest. Shipping passes the platform backend.
        - Keys, paths and the root check are as in R-10.
      - **Shipping-I/O guard amendment** (authorised by the new D-021 entry):
        1. Add the lock module to `sources`.
        2. Add path-keyed exact-line exceptions, in the style of the `arc.h` exception, for exactly two lines:
           - one POSIX `open(` line with pinned flags and mode (`O_RDWR|O_CREAT|O_CLOEXEC`, `0600`);
           - one `CreateFileW(` line pinning `GENERIC_READ|GENERIC_WRITE`, `FILE_SHARE_READ|FILE_SHARE_WRITE` without `FILE_SHARE_DELETE`, NULL security attributes, `OPEN_ALWAYS`, and no `FILE_FLAG_OVERLAPPED`.
        3. Assert that each exception matches exactly once.
        4. In the lock module, reject `read(`, `write(`, `pread(`, `pwrite(`, `ftruncate(`, `WriteFile(`, `ReadFile(`, `SetEndOfFile(` and stdio stream calls (`fopen`, `fread`, `fwrite`, `fprintf` and the like).
        5. Confine `fcntl`, `flock`, `LockFileEx`, `UnlockFileEx`, `stat` and `fstat` to the lock module.
        6. Widen the general pattern to `openat`, `creat`, `fdopen`, `CreateFile2`, `NtCreateFile`, `_open`, `_wopen`, `_sopen*`, `open64`, `fopen64`, `fopen_s`, `_wfopen_s`, `freopen`, `tmpfile` and `mkstemp`.
        7. Self-tests:
           - the allowed lines in any other file are rejected;
           - a second `open` or `CreateFileW` in the lock module is rejected, and so is a `CreateFileW` with `FILE_SHARE_DELETE`;
           - each confined call outside the module is rejected;
           - each newly covered call is rejected;
           - each lock-module data or size call in item 4 is rejected.
        - The Windows import check and the Android NEEDED list are unchanged.
      - **Ownership process binary** for AC-12a and P10b. It is test-only and is built for host, MinGW and Android arm64.
      - **Test VFSs** model mkdir `-2`: `tests/rpc_hd_vfs.h:91`, `tests/rpc_cmos_store_test.c:43`, `tools/rpc_runner_vfs.h:118-132`, `tests/libretro_harness.c:554`. No directory remove or rename primitive is added.
      - **Design note.** The ownership section of `docs/riscpc-session-preparation.md`: mechanism, platforms, error mapping, identity verification, residuals and P10b expectations.
    - **12b, on `slice/r3d3-host`, after 12a has merged and P10b has passed (or the Founder has decided): enforcement and caller migration, atomically.**
      - **Store enforcement.**
        - Every HD, CMOS and launch-record handle has its token bound at begin or open. Every mutation checks it before any namespace operation, and the check includes the POSIX identity verification at each publication begin. Mutations are recovery delete or truncate, fixed `.tmp` write, rename, remove, marker and observation invalidation.
        - The put and step signatures are unchanged.
        - Open-time recovery never runs without a held token.
        - Every caller migrates in the same commit, under "every caller": preparation, `tests/rpc_hd_process.c`, `tests/rpc_hd_store_test.c`, `tests/rpc_hd_handle_checks.h`, `tests/rpc_cmos_store_test.c`, `tests/rpc_launch_record_store_test.c`, `tests/rpc_boot_test.c`, `tests/rpc_ide_guest_test.c`, `tests/rpc_preparation_test.c`, `tests/rpc_orchestration_test.c`, the runner tools and the harness.
      - **Raw removal.** The raw `arc_io_remove(p->observations.path)` ([3b] `:697`) moves into the launch-record store under the token.
      - **Parent directories.** `arc_io_make_path_step` treats mkdir `-2` as success when a re-stat shows a directory, so two titles creating `hd-riscpc/` or `cmos-riscpc/` at once both succeed. The synchronous Arculator path is unchanged.
      - **Content lock.**
        - It is the first VERIFY unit, before launch-record recovery, HD open (`rpc_hd_store.c:892`) and CMOS reconcile. It does not depend on `request.hard_disc`.
        - It is held across both reset branches, `restage_stopped`, FAILED and the R-11 drain, and it is not in `release()`.
        - Free drops it after the final persist, the drain and the HD close, and cancel drops it too (R-12).
        - Maintenance verifies its identity once per running frame (R-10).
      - **Defaults lock.**
        - Held from DEFAULTS through SAVE_DEFAULT. Released at SAVE_DEFAULT completion or at a DEFAULTS FOUND outcome.
        - The defaults store makes no mutation after that, which the token check enforces.
        - A pending reset drops it.
      - The HD close result and discard mode (R-11, scope 6).
      - The launch-record interleavings and the R-8 marker are covered here.
      - **Contract docs.** Amend `docs/riscpc-hd-store.md:13-15` and `:66`, because the lock lives outside the store.

13. **Design note.** Update `docs/riscpc-session-preparation.md`:
    - reset at `:97`, including the non-RUNNING order (R-7) and in-flight completion (R-9);
    - the notice section at `:103-120`: severities, precedence (both exceptions), lifetimes, the hold table, the log ID mapping, the redacted forms, OSD targeting and the `arc_message` carried item;
    - host ownership;
    - teardown order and the HD close result;
    - the R-11 drain;
    - the A-6 delta list;
    - the ownership section, which goes on `slice/r3d3-own`.

## Non-goals

- No ordinary route, gate, `.info` or option change. No new option.
- No plan, attempt or action fields.
- No change to:
  - the frozen RUNNING block, including its synchronous failure-path publication;
  - the cancellation policy after admission;
  - the helper or gate bytes;
  - START's helper logic;
  - the shared `arc_message` path.
- No change to the preparation budgets or the D-168 order. Lock attempts, identity checks and the drain are new units inside the existing ceilings.
- No `engine/rpcemu/` change.
- No lease, owner record, renewal, fencing or automatic lock breaking. No deletion of lock files. No classic `fcntl` locks.
- No guarantee on network filesystems or synced save trees.

## Constraints and interfaces

- **Frozen A-3 transaction.**
  - **Pending or FAILED** (non-frozen lines `:670-673`):
    1. invalidate, stop, and release offline ownership while keeping the content lock (the defaults lock is dropped);
    2. keep launch suppression;
    3. re-stage through `restage_stopped` (R-7);
    4. restart VERIFY;
    5. attach nothing until START.
  - **RUNNING** (frozen `:643-669`), preceded by the host's in-flight publication completion (R-9):
    1. barrier;
    2. detach;
    3. `restage_drive0`, drive 0 only;
    4. restore drive 1 under the admission policy;
    5. `reset_rebind`;
    6. resume only on complete success. Any failure leaves a stopped session with a terminal, an explicit CMOS-snapshot outcome, no dangling backing, and published storage recoverable.
  - No host callback runs a guest instruction or touches drive 1, except for the write-policy call that R-7 allows.
  - `restage_stopped` makes no engine call at all.
- **No by-value inventory and a small stack.** No second large by-value inventory or other large stack object on the load or reset path (R3d-2 QA (4); the scan already peaks near 110 KB). Histograms are static.
- **Native I/O.** It is confined to the lock module (R-10, D-021 exception), and the lock descriptor never carries data.
- **A-6 enumerated deltas (plan route only) (F6).**
  - **Unaffected scenarios.** Persisted store bytes are strictly equal.
  - **Authorized changed files.** In their scenarios, each test asserts exactly which files changed, their exact expected contents and their generations:
    - (i) the zero-byte `cmos-riscpc/<content>-m<N>-<fw64>.unsaved` after a publication-only unload failure (R-8), removed by the next successful persist;
    - (ii) the next CMOS record generation (`.0`/`.1`), holding the captured fault snapshot, after an R-11 drain that saved. With an in-progress publication, that publication's generation comes first;
    - (iii) `locks-riscpc/` and its zero-byte `c-…lock` and `d-…lock` files, never deleted.
  - Everything else (HD store, launch records, defaults files) stays byte-equal.
  - "Published state preserved" means recoverable at the next load without CORRUPT, not byte immutability.
  - Each delta is listed in the design note with its reason. Nothing else may change.
  1. New status, advisory, terminal, summary and first-field log lines.
  2. N-8 is shown as an advisory under R-5's precedence.
  3. Regeneration on a receipted store shows the first-use text.
  4. The startup hold: host text, completion clear deferred.
  5. Clears are sent at priority 3 (today 2), and the module no longer clears before terminals.
  6. N-11 replaces "Disk options change will apply after reset or reload."
  7. The unload-failure log line, the advisory, and the N-13 marker on publication-only failures.
  8. AV changes:
     - no `SET_SYSTEM_AV_INFO` while pending;
     - none at the first field when the values equal 640×480 @ 60;
     - none after a successful RUNNING reset unless the values differ;
     - one re-baseline on a restart from FAILED-after-RUNNING at a different rate.
  9. RUNNING reset order: detach, re-stage drive 0, reset. Drive 1 is restored, not re-prepared. Counts change in [d1] `tests/rpc_observers_frontend_test.c:274,348,356,359` (collision C7).
  10. A failed reset gives a stopped session with a terminal, instead of a message and a running session.
  11. A pending setter rejection has no side effects, for all four setters.
  12. Resets clear the disc state and the disc cache, including the runner's preparation-reset flags, which now go through `retro_reset`.
  13. Media is staged at load, not inside the attach unit. START attaches the module's drive-0 view. A non-RUNNING Restart re-stages after release, through `restage_stopped`.
  14. The freeze baseline is the load-time read.
  15. RPC controller info, descriptors and device-capability calls at load.
  16. `locks-riscpc/` and its zero-byte lock files exist under `<save>/arcdroid/` and persist after the session.
  17. Host and module log lines carry catalogue IDs and redacted text at every level, never member names or paths.
  18. Silent samples follow the announced rate.
  19. A RUNNING-to-FAILED transition in maintenance drains the captured CMOS snapshot and reports the outcome (R-11).
  20. Catalogue fallback on-screen texts that carried member names or paths show their redacted forms, and host extended messages are OSD-only.
  21. An HD close failure is reported (N-17).
  22. Unit, namespace and frame counts include the `locks-riscpc/` mkdir namespace unit, the lock-attempt and identity-check unit kind, and the lock-wait frames. Any output that records those counts shifts by exactly these units.
  23. An in-flight publication completes before a RUNNING reset (R-9), which changes publication timing in stepped mode only.

## Acceptance criteria

Evidence classes (A-8):
- **[1]** flag-free production-module tests that call the host and module directly, with an authored fingerprint in `request.fingerprint` through production begin, as `tests/rpc_orchestration_test.c` does (verified catalogue 0);
- **[2]** plan-core frontend lifecycle tests through real `retro_*` entry points, and cases that need observer binding;
- **[P]** private legs.

New tests are named `rpc-*`, so the sanitizer selection at `.github/workflows/ci.yml:72` includes them. Otherwise the regex is extended in the same PR.

### `slice/r3d3-own` (12a)

**AC-11a Ownership primitive [1].** Uses the injectable backend and an injected clock. Expected outcomes are stated per case.
- **Basic outcomes:**
  - acquire on a free key gives held;
  - a second handle on the same key in the same library gives contended, through the backend's own refusal;
  - release, then re-acquire, gives held.
- **Non-blocking.** Acquire never blocks: a backend that would block is a test failure, and each attempt's duration is bounded and asserted.
- **Error mapping:**
  - OFD `EAGAIN` gives contended;
  - OFD `EACCES` gives lock error;
  - `EINTR`, `EEXIST` and `ERROR_SHARING_VIOLATION` give transient;
  - `ENOSYS`, `ENOLCK`, `EOPNOTSUPP`, OFD `EINVAL`, `ERROR_INVALID_FUNCTION` and `ERROR_NOT_SUPPORTED` give unsupported.
- **Fault injection:**
  - a `scheme://` root or a relative root gives root refused;
  - a post-acquire VFS stat that is not a zero-size regular file gives root refused;
  - VFS mkdir error gives create error;
  - mkdir `-2` with a directory present gives success;
  - native open failure, including a read-only lock file, gives create error.
- **Identity.**
  - A post-acquire identity mismatch releases the lock and reports transient.
  - A held-state identity mismatch (the path is replaced or unlinked) gives identity lost.
- **Keys and paths:**
  - paths and keys match R-10 byte for byte;
  - a differently spelled or symlinked root reaches the same lock file (real-OS leg);
  - the lock file is never removed by release.
- **A suspended holder is never displaced.** A holder paused for longer than W keeps its lock, and the contender sees contended on every attempt. These are Astra F1's schedules, which no longer apply because nothing breaks a lock.
- **Dormancy.** No preparation or store source references the lock module, and no shipping caller exists (source guard).

**AC-12a Ownership primitive, real processes [1][P].**
- **Where it runs.** The ownership process binary runs on one directory tree:
  - in CI on ubuntu-24.04 and ubuntu-24.04-arm (OFD) and windows-latest (`LockFileEx`);
  - Mac-local for `flock` (no CI runner);
  - on the Odin3, Windows and the Mac in P10b.
- **Cases:**
  - two processes on one key: the second is contended;
  - the holder is killed (SIGKILL or `TerminateProcess`) after acquisition, and the harness reaps it (`waitpid`/`WaitForSingleObject`) before asserting. The next acquire then succeeds at its first attempt on POSIX, and within W on Windows (the retry count is recorded);
  - a same-process double open is contended, including a same-process second `LockFileEx` handle;
  - a re-acquire after release in the same process is held;
  - a leaked descriptor shows as contended (self-contention), never as held twice;
  - on POSIX, a rename-over and a delete of the held lock file make the holder's next verify report identity lost;
  - on Windows, both fail with a sharing violation.

**AC-13a Shipping identity [1].** Shipping ordinary loads and plan-route goldens match pinned `main`. The lock module is present but unreferenced by runtime paths.

**AC-14a Builds and guards [1].**
- Host, sanitizer, MinGW and Android builds pass. The Android build defines `F_OFD_SETLK`.
- The amended shipping-I/O guard passes, and every self-test in scope 12a item 7 fails its mutant input.
- The import checks are unchanged.

### `slice/r3d3-host` (scopes 1–11, 12b)

**AC-1 Lifecycle [1][2].**
- J1, J2 and J12, plus reset, unload and deinit in every state:
  - in [2]: every pending state (including every GENERATE, POPULATE and START substep), RUNNING, FAILED and the R-11 drain;
  - in [1]: CANCELLED (R-12).
- Each is followed by an Arculator load that inherits nothing (D-168(6)).
- **Direct-start compatibility test.** A direct-start plan session keeps today's active-RPC route, including its direct `arc_engine_reset` on `retro_reset` (R-3; F6).
- Amendment 19:
  - eject, index, replace and add in each pending state are refused with zero side effects (observer, notice ring, disc cache and launch state byte-compared);
  - image append and replace while RUNNING, on RPC and on Arculator, check the media, keep content identity and never switch engines;
  - Disk Control never lists or addresses drive 1 (D-190).
- FAILED and CANCELLED never reach the Arculator reset or run paths.

**AC-2 AV and frames [2].**
- While pending, every `retro_run` gives exactly one video callback and one audio callback.
- Silent samples total 48000 / the announced rate over time, within one sample. This holds at 60 Hz, and after FAILED-after-RUNNING at 50 Hz and 75 Hz guest rates.
- AV calls:
  - none before the first guest field;
  - at most one at that field, issued only if the values differ;
  - none after a successful RUNNING reset with unchanged values;
  - exactly one re-baseline on a restart from FAILED-after-RUNNING at a non-60 rate;
  - no other frontend-caused AV change between pending phases.
- Guest-driven changes are counted separately (harness counters in `tests/libretro_harness.c`, around lines 1165-1178).

**AC-3 Notices and privacy [1][2].** The exact sequence, the log lines and both summary lines in every state. Cases:
- a terminal stops refresh;
- the first terminal wins, except that a FAILED terminal replaces a retained refusal and the drain-outcome terminal replaces its fault terminal once;
- no clear precedes a module terminal;
- N-8 is visible for its full advisory lifetime on both the extended and fallback paths, despite progress refreshes, and is logged;
- running-phase store advisories have their own case;
- receipted regeneration shows the first-use text;
- the hold:
  - shows "Starting RiscPC" on receipted and first-use paths;
  - is absent when a terminal is retained;
  - expires at its pair's bound;
  - is pre-empted by an advisory and then resumes;
  - is restarted by a RUNNING reset;
  - is cancelled by a pending reset, unload or switch;
  - yields to a terminal;
- reset, unload and switch clear at priority 3;
- expiry per severity:
  - progress fallback gone no later than 120 frames after its last refresh;
  - advisory 5 s extended, 300 frames fallback;
  - terminal retained, with the fallback re-sent every 60 frames;
- N-13 to N-18 appear exactly when their rulings say;
- **privacy (F5).** Scope: host and module catalogue notices. The shared `arc_message` path is excluded and carried to R3d-4.
  - The fixture's archive member name and save path contain unique sentinel strings. With it:
    - every host `SET_MESSAGE_EXT` payload has target `RETRO_MESSAGE_TARGET_OSD`;
    - every host `SET_MESSAGE` payload is the redacted form;
    - no host or module log line at any level contains a sentinel, and each line carries its catalogue ID.
  - A RetroArch 1.22.2 leg with `--verbose` log capture asserts that no sentinel from a catalogue notice appears in RetroArch's own log, on both message paths. It runs in the Windows CI job, which packages `arcdroid_rpcplan_libretro.dll` with RetroArch 1.22.2, and as [P] on the Odin3.

**AC-4 Reset transaction [1][2].** Exercised through real `retro_reset` on the plan core, and through the module with `launch==NULL` (M1 barrier, no attempt object).
- **RUNNING ordering.** Asserted by trace, as part 2's `reset_probe` does ([3b] `tests/rpc_orchestration_test.c:35-41`):
  - execution is not allowed inside either callback;
  - drive 1 is ejected before `restage_drive0`;
  - `restage_drive0` runs before `reset_rebind`.
- **Non-RUNNING ordering (F3).**
  - The trace is checked at VERIFY, RECOVER, VALIDATE, RECORD, DEFAULTS, each GENERATE substep (0, 1), SAVE_DEFAULT, SAVE_RECORD, each POPULATE substep (0 to 3), each START substep (0 to 3) and FAILED. Each shows engine stopped and offline resources released, then `restage_stopped`, then restart.
  - No engine call, insert or attach happens inside `restage_stopped`.
  - No preparation step runs between release and the view and policy update.
  - The defaults lock is dropped and the content lock is kept.
- **Re-staging failure.** An injected `restage_stopped` failure gives FAILED with a terminal, engine stopped, no attach, no store mutation and the content lock kept. A further Restart succeeds.
- **Injected failures (RUNNING):**
  - `restage_drive0`;
  - drive-1 mount, through a new `ARCDROID_TEST_FAULTS` engine seam failing `arc_engine_rpc_mount(1, …)` (class [2]);
  - helper construction failure, in [1] if an existing allocation seam reaches it, otherwise [2];
  - `reset_rebind`;
  - an engine that is already dead.
- **Stepped publication (if R-9 is stepped).**
  - A RUNNING reset failure with a stepped publication in flight: the in-flight publication completes first, then the frozen put reports "saved", and both generations are asserted.
  - The same holds for a swap rollback ([1], through part 2's rollback path).
- **Each failure gives** a stopped session, a terminal, an explicit "saved" or "could not be saved" snapshot outcome, no resumed instruction and no dangling backing.
  - Changed files are exactly the frozen path's record generation +1 holding the snapshot when "saved", or none when "could not be saved".
  - The next load recovers that state without CORRUPT (F6).
- **Success** resumes with the volatile CMOS and the HD intact.
- **Drive-1 cases from A-3 step 4,** in both branches:
  - generated helper;
  - revision-1;
  - helper-free;
  - explicit writable image and explicit read-only image, with the reset-time write policy applied after a disk-writes change (RUNNING, pending and FAILED Restart);
  - explicit empty drive 1, which stays empty.
- **Under ASan:**
  - a RUNNING reset with disk-writes changed, then a fault (FAILED), then a Restart attaches only live backing;
  - the same after an in-session drive-0 swap and replace before the fault;
  - a GENERATE-substep and a START-substep Restart with disk-writes changed attaches only the newly staged backing.
- **Source guard plus mutant.** No preparation-route `retro_reset` path reaches `arc_engine_reset` outside `reset_rebind`. The direct-start path is covered by AC-1's compatibility test.
- The frozen block (`:643-669`) is byte-identical to part 2's merged commit.

**AC-5 Baseline [2] (F6).**
- Plan-core goldens, runner outputs and the CI R2c AC-9 leg match pinned output from R-1's base commit, except for the A-6 deltas. Each delta is asserted individually, with no fixture regeneration.
- Persisted bytes are strictly equal in every unaffected scenario. In each authorized scenario, the test enumerates the changed files and asserts their exact contents and generations (A-6 (i)–(iii)).
- The plan core holds no host-adapter code beyond its low-level modes and the extension table (a source guard over `tests/arc_test_plan.c` and the `TEST_PLANS` blocks).

**AC-6 Maintenance and S-4 [1][2].**
- Exactly one maintenance call per running frame, and zero while pending, generating or draining.
- The drain entry is called only in FAILED frames with an active drain, on at most 60 frames.
- S-4 is resolved per R-9 with numbers. If it is stepped, a kill inside the 60-frame window recovers the previous publication (J14).

**AC-7 Teardown [1][2], under ASan (F4, F6).**
- No media entry, helper entry or backing is freed before the engine has detached it, on every path.
- With a live engine, Free captures and persists before engine stop.
- In FAILED and after a failed attachment, Free completes without requiring a live engine. Where R-11 applies, the snapshot used is the one captured before the stop.
- Final cleanup runs exactly once per session (double-release mutant).
- Nothing is left at unload (heap census, file handles, lock handles).
- The locks are released after the final persist, the drain and the HD close, on every clean path.
- **HD close result.**
  - It is retained through teardown, with failure injection at flush and at close.
  - On reset it is reported as the N-17 advisory. On unload it is logged, and N-17 shows after the unload clear.
  - A discard-mode close makes no mutation.
- **An injected unload publication failure** (marker write not failing):
  - is logged even after an earlier refusal;
  - shows as an advisory after the unload clear;
  - writes the zero-byte marker;
  - makes the next load show N-13;
  - is cleared from disk by the next successful persist.
- **An injected marker-write failure:** logged and shown as the advisory, no marker, and no N-13 at the next load.

**AC-8 Parity [2].**
- OSK, overflow, pad, keyboard and pointer parity, or a test for each documented difference.
- The plan-route load advertises RPC descriptors and controller info and no A3010 joystick.

**AC-9 Freeze [2].**
- A media option changed while pending or running is not applied until Restart or reload.
- N-11 is shown once per option per session.
- Restart re-stages drive 0 under the current values at Restart in both reset branches: RUNNING through `restage_drive0`, non-RUNNING through `restage_stopped`.
- Drive 1 keeps its identity.

**AC-10 J15 [1][2] (F4).** A contained fault through the harness fault seam, and an injected observation-invalidation failure (`:697`), each give:
- the retained class B terminal, raised at once;
- continued black and silent frames;
- the snapshot captured before the stop;
- a drain that completes within 60 frames and replaces the terminal once with "saved" (record generation +1 holding the captured snapshot) or "could not be saved";
- when an earlier publication is in progress at the fault, that publication completes first and then the captured one, with both generations asserted;
- a reset requested mid-drain runs when the drain ends;
- an unload or switch mid-drain finishes the drain within free;
- the content lock is held until the drain and HD close finish, and a not-held lock gives no mutation and "could not be saved";
- the HD close outcome is reported;
- a clean unload and a clean Arculator load follow.

**AC-11b Ownership enforcement [1].** Uses the injectable lock backend, an injected clock and frame counts. Expected outcomes are stated per case.
- **Atomic migration.**
  - Every HD, CMOS and launch-record handle binds a token at begin or open.
  - A begin or open without a token does not compile (signature), so no legacy path remains.
  - A not-held token causes no namespace operation.
  - The frozen `:663` call is unchanged.
  - No raw `arc_io_remove` of a store file remains in `rpc_preparation.c` (scope 11 guard).
- **Interleavings.** Two handles over one in-memory VFS, interleaved at every namespace boundary through `arc_riscpc_hd_open_step`, `arc_riscpc_cmos_store_step`, `arc_riscpc_cmos_store_put_step`, the launch-record recover and publish steps, and the observation invalidation:
  - exactly one owner;
  - the loser does no mutation and waits;
  - no temporary file is truncated or renamed by a non-owner.
- **Same title twice.** The second gets N-14 after W, while the first continues.
- **Two titles generating the same defaults.** Both succeed, the second adopts the first's record, and the bytes are identical. A defaults contender times out with N-16 after W_d.
- **Parent directories.** Two titles creating them at once both succeed.
- **Lock order and timing:**
  - the content lock precedes launch-record recovery, HD open and CMOS reconcile;
  - the defaults lock is held only from DEFAULTS through SAVE_DEFAULT;
  - a defaults mutation after release is refused (mutant);
  - after a pause longer than W, the first frame's attempt runs before the W check, and an acquisition there succeeds without N-14.
- **Lifetime.**
  - The lock is held across both reset branches, `restage_stopped`, FAILED and the drain.
  - Cancel drops it after `release()`. Free drops it after the persist, the drain and the close.
- **A suspended holder** (paused longer than W) is never displaced. Its contender gets N-14 and makes no mutation.
- **Identity lost.** An injected held-state mismatch during RUNNING, and one at a publication begin, each give N-18, no further mutation, a discard-mode close and "could not be saved".
- **N-15 cases.** Each gives no store acquired:
  - an unsupported backend;
  - a root the lock module refuses (a non-HD session with a `scheme://` save, or a relative root);
  - a mkdir error;
  - an open error;
  - a transient error that outlasts W.
  - Separately, a hard-disc session with a `scheme://` save is refused class A at load (existing behaviour, unchanged).

**AC-12b Ownership, real processes [1][2][P].**
- `tests/rpc_hd_process.c` ([1]) and the runner-driven `tests/rpc_hd_retroarch_test.py` ([2]) are extended to two processes on **one** save tree, for the same title and for two titles on one pair.
- SIGKILL or `TerminateProcess` is applied at each of these points, each reached through test-VFS hooks, not timing:
  - after lock acquisition;
  - CMOS `.tmp` written;
  - HD `chunk.tmp`/`manifest.tmp` pending;
  - launch-record publication;
  - mid-drain;
  - mid-release, after the persist and before the unlock.
- The harness reaps the killed process before the reload. After the reload, all of the following hold:
  - the latest published state is intact;
  - no false CORRUPT is reported;
  - the pair's defaults are still usable;
  - no sticky refusal remains;
  - the content lock is acquired at the first pending attempt on POSIX (frame counts under an injected clock in CTest), and within W on Windows.
- Wall-clock recovery times are measured privately.

**AC-13 Shipping identity [1].**
- A shipping-core ordinary load of RPC-flagged content matches the pinned pre-R3d base minus the cumulative gate-closed list (Amendment 17, A-6 as amended by the R3d-2 QA adjudication):
  - R3d-1's G-3 line;
  - R3d-2's `skipped`/`complete` wording and D-166(4) reason changes.
  - R3d-3 adds nothing to that list. Ordinary loads never reach the host or the lock module, and `arc_message` is unchanged.
- Test seams, the low-level host entry and the extension table are absent from shipping symbols.

**AC-14 Builds [1].** Host, sanitizer, MinGW and Android builds pass, the guards pass, and the library A/B passes in both package states.

## Deterministic tests

- **`slice/r3d3-own`:**
  - a flag-free ownership test executable for AC-11a;
  - the ownership process binary for AC-12a, in CTest on ubuntu-24.04, ubuntu-24.04-arm and windows-latest;
  - the guard self-tests;
  - goldens compared against its pinned `main`.
- **Host and module (`slice/r3d3-host`):** a flag-free module test executable for AC-1, AC-3, AC-4, AC-6, AC-7, AC-10 and AC-11b, with an injected clock, the injectable lock backend and an authored fingerprint through production begin.
- **Plan core:** frontend tests through `retro_*` on `arcdroid_rpcplan_libretro` for AC-1 to AC-5 and AC-8 to AC-10.
- **Mutants**, each of which must fail a test:
  - **Setters and reset:**
    - a setter side effect placed before admission;
    - the old RUNNING reset branch restored;
    - drive 1 re-prepared on reset;
    - a stale drive-0 view at START;
    - re-staging before `release()` in a non-RUNNING reset;
    - an engine insert inside `restage_stopped`;
    - the explicit-helper write policy not updated in a stopped Restart;
    - a `restage_stopped` failure that leaves the session runnable;
    - a RUNNING reset without in-flight publication completion (stepped mode).
  - **Notices and privacy:**
    - severity inferred from phase;
    - the clear sent at priority 2;
    - a pre-terminal clear restored;
    - N-8 overwritten by refresh;
    - a host extended message without the OSD-only target;
    - an unredacted catalogue fallback text;
    - a member name logged at DEBUG.
  - **Audio:** a fixed 800 samples after a rate change.
  - **Ownership:**
    - a classic `F_SETLK` call;
    - a blocking lock call (caught by the per-attempt duration bound, not only by a CTest timeout);
    - a lock descriptor without `O_CLOEXEC`, or an inheritable handle;
    - `FILE_SHARE_DELETE` on the lock handle;
    - OFD `EACCES` mapped to contended;
    - the post-acquire or in-session identity check removed;
    - a lock file deleted at release;
    - the lock released in `release()`;
    - the content lock acquired after HD open;
    - a store mutation with a not-held token, in each of the HD, CMOS and launch-record stores;
    - a defaults mutation after the defaults lock is released.
  - **Teardown and persistence:**
    - free placed after engine stop on a live engine;
    - a double release;
    - the HD close result discarded;
    - the R-11 snapshot captured after the stop, or dropped, on both the fault and the invalidation paths;
    - the drain collapsed into one synchronous publication;
    - the drain driven through `maintenance`;
    - the marker skipped on a publication-only failure.
- **Process legs:** the process and RetroArch-runner legs for AC-12a and AC-12b run in CTest.
- **Comparison:** every host-branch golden is compared against R-1's pinned base.

## Private legs (Fable)

Stores are populated after the freeze where R3c-3a has merged, and are disposable otherwise.

- **Odin3.** The Odin3 is Founder-held; Founder-only blockers are parked per protocol §18.
  - Plan core in RetroArch with an isolated `CONFIGFILE` under `/storage/emulated/0/arcdroid-r3d/` (P6, Amendment 21).
  - All seven pairs with the S62 kit:
    - J1 and J2 screencaps showing every sustained status (A-7), N-8 and the hold;
    - summary-line numbers against §9 and Amendment 1, including the lock unit and the identity-check cost;
    - D-172 targets, including a reopen after force-stop within the unamended 1 s.
  - J14 on two pairs.
  - P10b (R-10): shared storage, internal app-private and external `Android/data` storage, with the ownership process binary, plus the rename-over, delete and letter-case legs and the MediaProvider versionCode.
  - A force-stop, then a relaunch: the lock is acquired at the first attempt.
  - RetroArch log capture for the AC-3 privacy assertion.
- **Second Android device** (D-191 Founder item): the P10b shared-storage legs, when it is available.
- **Windows.**
  - 3.71 SA-110, 3.50 ARM610 and 3.60 ARM7500. Timings are informational (Amendment 18).
  - P10b on NTFS, including the same-process second-handle, sharing-violation and transient-open legs.
  - Two RetroArch instances on one title: N-14 shown after W.
  - One instance paused in the menu while the other opens: the paused holder keeps the lock, and the other gets N-14.
  - Time-to-reopen after `TerminateProcess`.
- **Mac.**
  - Runner and harness on all seven pairs, with the summary lines compared against the R3b-2b runner evidence.
  - The R-6 hold table measurements.
  - Library A/B in both package states.
  - P10b on APFS.
- **P3.** A run confirming that run-ahead's second instance does not load content, including the `preempt_deinit` re-entry path. If it does load content, the corrected R-10 P3 consequence applies and Fable escalates before R3d-4.

## QA focus

- **Semantic drift:** diff the old glue (`arcdroid_libretro.c:7438-7558`, [3b] `tests/arc_test_plan.c:108-200`) against the host and the extension table.
- **Release order** on every path: detach before free, cleanup exactly once, and snapshot before the stop.
- **Reset routing is complete:**
  - FAILED and CANCELLED never fall into the Arculator paths;
  - direct-start plans are unchanged;
  - the non-RUNNING order is release, then `restage_stopped`, then restart, at every substep;
  - in-flight publication completes before a RUNNING reset and before a rollback;
  - the frozen block (`:643-669`) is byte-identical to part 2's merged commit;
  - edits outside it are limited to the listed rulings.
- **The drive-0 view's lifetime** across re-stage, swap, FAILED and Restart.
- **Explicit drive-1 write policy** in both reset branches.
- **Pending setter rejection** has no side effects, for all four setters.
- **AV churn and audio cadence.**
- **Severity is never inferred,** and the precedence (both exceptions) holds on both message paths.
- **Privacy:** OSD-only targeting, redacted catalogue fallback texts, and no sentinel in RetroArch's log. The shared `arc_message` path is unchanged and carried.
- **Lock correctness:**
  - OFD, `LockFileEx` and `flock` are used per platform, and no classic locks exist;
  - non-blocking calls, no inheritance, and no `FILE_SHARE_DELETE`;
  - the error mapping (EAGAIN only is contention, and transients retry);
  - the post-acquire and in-session identity checks;
  - lock files are never deleted;
  - the content lock comes first and is held across reset, `restage_stopped`, FAILED and the drain;
  - the defaults lock scope;
  - no mutation without a held token in any of the three stores;
  - the guard exceptions are exact-once and confined.
- **Persistence:** the drain is bounded, covers an in-progress publication and both maintenance stop paths, and has its own entry; the HD close result is reported; the marker is conditional.
- **Test seams** are absent from shipping symbols.
- **No private content** in any new log line or catalogue fallback text.

## Provenance

- No ROM, boot-archive or title bytes, private screenshots or private logs in commits, CI or artifacts (protocol §10/§11, D-134, D-164(4)).
- Only the four committed fingerprint tuples (D-090).
- Authored carriers only in CI.
- Lock files are zero bytes and hold no path, user data or session ID. Their names contain only the store keys that the store paths already use.
- No `engine/rpcemu/` change.

## Astra stage 2 adjudication (REJECT, six findings; Fable's rulings)

| Finding | Ruling | Where |
|---|---|---|
| F1: R-10's fencing and breaker are unsafe | **Incorporated.** Automatic breaking is withdrawn, and an OS advisory lock released on process death replaces the mkdir lease. Contention is refused after W or W_d, never broken. A crash releases the lock, so D-172 stands. An unsupported lock or a refused root gives a class B refusal (N-15), recorded as a plan §4 and Amendment 11 amendment. A narrow D-021 exception and a guard amendment are added. P10b confirms the behaviour on devices, and a failure on Odin3 shared storage goes to the Founder with Fallbacks 1 and 2. Astra's two schedules become "a suspended holder is never displaced". **After the checks:** the classic fallback is dropped (Fable confirms), and identity verification and N-18 are added. | R-10, scope 12, AC-11a/b, AC-12a/b |
| F2: 12a enforcement cannot land independently | **Incorporated.** 12a is strictly the dormant primitive and its tests. Enforcement and caller migration land atomically in 12b, now including the launch-record store. Each branch has separate ACs. Astra's dormant-API alternative is not taken. | R-1, scope 12, ACs per branch |
| F3: R-7 re-stages before the engine is quiesced; the explicit-helper policy is missing | **Incorporated as Astra states.** Release first, then `restage_stopped` (re-stage with no attach, then the view and the helper policy), then restart. A stopped-state failure transition is added. The RUNNING transaction is unchanged; its frozen range is corrected to `:643-669`. Every pending state and substep, FAILED restart, writable and read-only helpers, and empty drive 1 are tested. | R-7, scopes 1 and 4, AC-4, AC-9 |
| F4: no reportable HD close; fault publication is not one D-169 exception | **Incorporated.** The HD close result is retained through teardown and tested with failure injection. Fault persistence is a bounded 60-frame drain of the captured snapshot, through its own entry, covering an in-progress publication and both maintenance stop paths. Ownership is held until persistence and close finish, and a not-held path never mutates. The frozen error-path publications get in-flight completion and a P8 budget treatment. Astra's "measured exception" alternative is not taken for the drain. | R-9, R-11, scope 6, AC-4, AC-7, AC-10 |
| F5: the fallback path leaks text to RetroArch's log | **Incorporated.** Host extended messages are OSD-only. Catalogue fallback texts never carry member names, paths or private strings (redacted on screen too), and no host or module log level carries them. Tests capture RetroArch's log. The catalogue change is frozen before host dispatch. The shared `arc_message` path is carried to R3d-4. Astra's "prohibit frontend logs from artifacts" alternative is not taken. | R-5, scope 8, deltas 17 and 20, AC-3 |
| F6: the baseline and lifecycle ACs are incompatible | **Incorporated as Astra states.** Exact equality in unaffected scenarios, and enumerated changed files with exact contents in failure scenarios. The `arc_engine_reset` guard is scoped to the preparation route, with a direct-start compatibility test. Snapshot before the stop, detach before freeing backing, cleanup exactly once. The marker is required only for publication-only failures. Tokens are bound at begin, so the frozen block is not touched. | A-6, AC-1, AC-4, AC-5, AC-7, scope 11 |
| R-1 note: a smaller reset foundation is possible | **Rejected for now.** Part 2 is near completion, and the split adds a review cycle. Revisited if part 2 slips. | R-1 |
| Freeze P1/P1b, P3, P8 and the hold table before dispatch | **Incorporated.** Each item has its decision and its default stated, and the §7 texts are added as a pre-dispatch item. | Next, pre-dispatch table |

## Critic issues not taken

- **Critic 1 #19, the translation-unit split for a "narrow catalogue boundary":** not needed. Production begin already verifies an authored fingerprint passed in `request.fingerprint` ([3b] `rpc_preparation.c:163-167`, `:320-323`; `tests/rpc_orchestration_test.c:191-194`). Only the wording is fixed. Observer-bound cases are class [2].
- **Critic 1 #11, the frozen range "~641-667":** corrected to the actual block, [3b] `:643-669`. The suggestion to keep the module's pre-terminal clears is replaced by Critic 2 #6's alternative: the pre-terminal clears are removed.
- **Critic 1 #13, "define a host cancel path":** not taken. No frontend action maps to cancel, so CANCELLED is limited to [1] evidence (R-12).
- **Critic 2 smaller fix, "`tests/rpc_observers_frontend_test.c` exists only on [3b]":** incorrect. The file is also on [d1], and the cited lines 274, 348, 356 and 359 match [d1], so it is cited as [d1].
- **Critic 2 #8, the Python tests "use" the export:** they reach it only through the runner flags (`--preparation-reset-*`). The runner migration in R-4 covers them, and the export itself is retired.

## Check issues not taken

- **Check 1: open the Windows lock file with `GENERIC_READ` only, so that a read-only attribute does not cause a permanent N-15.** Not taken. POSIX `F_WRLCK` needs a descriptor open for writing, so a read-only lock file gives N-15 there in any case. Pinning `GENERIC_READ|GENERIC_WRITE` (check 2) keeps one rule on every platform. N-15's text tells the user to choose a writable folder.
- **Check 1: put the locks inside the store subtrees to close aliasing below `<save>/arcdroid/`.** Not taken. The content lock must come before launch-record recovery and HD open, which create and recover those directories. The HD store contract also allows no other file kinds in its directory (`docs/riscpc-hd-store.md:66`). It is stated as a residual instead.
- **Check 2 M5 alternative: make one acquisition attempt at load.** Not taken. It would add a synchronous native call to load without changing the safe outcome, and it would split lock failures between class A and class B. The P3 text is corrected instead.
- **Check 2 M3 alternative: route every message from an RPC session through the host.** Not taken in R3d-3. It would change the shared `arc_message` path that Arculator and ordinary loads use (AC-13). AC-3 is scoped to catalogue notices, and the shared path is carried to R3d-4.
- **Check 2 H2 alternative: explicitly authorise one mechanical token edit inside the frozen block.** Not taken. Binding the token at begin or open avoids touching `:663` at all.
- **Check 1 and check 2 disagree on FUSE-BPF scope** (`flock` pass-through in general, versus FUSE-BPF used only for `Android/data` and `Android/obb`). Not resolved by ruling. Both readings are recorded as expected results, because `flock` is never used and OFD behaviour is unchanged under either. P10b records what each device actually does.
- **Check 2 low: the stale on-disk `R3D3-WORK-ORDER.md`.** No action in the work order. This revision replaces that file when Fable records it.

**Sources for R-10:**
- [W1] Microsoft, LockFileEx: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex
- [L1] Linux flock(2): https://man7.org/linux/man-pages/man2/flock.2.html
- [L2] Linux fcntl_locking(2): https://man7.org/linux/man-pages/man2/fcntl_locking.2.html
- [M1] macOS flock(2): https://keith.github.io/xcode-man-pages/flock.2.html
- [M2] macOS fcntl(2): https://keith.github.io/xcode-man-pages/fcntl.2.html
- [K1] Linux `fs/fuse/file.c` (`fuse_file_lock`, `fuse_file_flock`, `fuse_setlk`): https://github.com/torvalds/linux/blob/master/fs/fuse/file.c
- [K2] Linux `fs/fuse/inode.c` (`process_init_reply`, `no_lock`/`no_flock`): https://github.com/torvalds/linux/blob/master/fs/fuse/inode.c
- [K3] Android common kernel `fs/fuse/file.c` (FUSE-BPF backing-file paths), branches android14-6.1, android15-6.6 and android16-6.12: https://android.googlesource.com/kernel/common/
- [A1] MediaProvider `jni/FuseDaemon.cpp`, current main and `e3bd67de`: https://android.googlesource.com/platform/packages/providers/MediaProvider/+/refs/heads/main/jni/FuseDaemon.cpp
- [A2] AOSP `external/libfuse` `lib/fuse_lowlevel.c`: https://android.googlesource.com/platform/external/libfuse/+/refs/heads/main/lib/fuse_lowlevel.c
- [A3] Android scoped storage: https://source.android.com/docs/core/storage/scoped
- [R1] RetroArch v1.22.2 `libretro-common/vfs/vfs_implementation.c`: https://github.com/libretro/RetroArch/blob/v1.22.2/libretro-common/vfs/vfs_implementation.c
- [R2] RetroArch v1.22.2 `runahead.c`: https://github.com/libretro/RetroArch/blob/v1.22.2/runahead.c
- Android API floor (sdcardfs and pre-OFD kernel relevance): PRD Q3, `docs/PRD.md:786`; CI `-DANDROID_PLATFORM=android-21` (`.github/workflows/ci.yml:275`).
---

## Astra stage 2 re-check (S65): adjudication

Report `.work/s65/astra/report4.md`. F1 (the lock), F2, F3 and F5 resolved. **`slice/r3d3-own`: ACCEPT** as specified (dormant primitive, platform qualification). **`slice/r3d3-host`: ACCEPT WITH CHANGES**; both changes are adopted and govern the host branch:

1. **No dirty writable VFS handle across an ownership-loss boundary.** RetroArch's VFS close flushes buffered stdio data (`fclose`) and reports success regardless, so a "discard" close can still write. Every store write sequence (HD store, CMOS store, launch-record store) is qualified so that no buffered, unflushed writable handle survives any point where ownership loss can be detected: write, flush and close form one bounded unit, or an equivalent mechanism achieves the same invariant. All three stores are audited. A test uses a buffering VFS whose close really flushes. Explicit flush errors are reported while ownership is held; close-error reporting is described as limited to what the frontend exposes. The native-I/O exception is not broadened to store data.
2. **Failure oracle by publication phase.** AC-4 and the A-6 projection freeze the expected filesystem outcome for each injected failure phase of the existing CMOS transaction (`libretro/rpc_cmos_store.c`: unchanged-payload no-op success without a generation change; candidate written only; older destination slot retired; published then verification failed; completed publication), including a preceding in-flight publication. They assert preservation and recovery of the authoritative state with the exact allowed temporary-file and slot changes. The blanket "no changed files when saving fails" and "generation + 1" rules are replaced. These are existing transaction semantics, not defects to change.

**Stated conditions of the guarantee (kept explicit):** exclusion holds among cooperating clients using the same stable sentinel object in a stable, shared namespace. Sentinel replacement between an identity check and the next write, subtree aliases and different mount views are detected by identity polling, not excluded; the VFS zero-size stat is a consistency check, not proof of object identity. P10b qualifies the actual Android mount views and platform behaviour.
