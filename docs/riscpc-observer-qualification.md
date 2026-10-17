<!-- SPDX-License-Identifier: GPL-2.0-only -->

# RiscPC observer qualification record (R3c-2, R3c-3b)

Status: adopted (D-181; amended by D-187 for ordinary error numbers and by D-203 for the boot and launch cells). Fable's adjudication for the R3c-2 and R3c-3b qualification-and-promotion checkpoints (D-177(8)). The production capability table in `engine/rpc/rpc_observer.c` must equal this record exactly. A cell absent from this record stays disabled with the reason given here.

This record holds no firmware bytes, addresses, signatures or classification data. Its evidence is Fable's private, local qualification legs, named here by leg identifier only. They ran on the Mac through `rpc_runner` and the test-flagged plan core, with the test-flag override set to the profile values below.

## Builds and method

- **Error-box legs:** run C, `3964a0d` (`a3`, `b3`, `b14`, `a14`, `o`); graphical-box legs run E, `686b1b7` (`ga`, `gb`, `gna`, `gnb`).
- **Disc legs:** run D, `f185c16`.
- **Cost legs:** run E, `686b1b7`, on the Odin3.
- **Firmware and CPU pairs:** the seven recognised pairs from the R3b populated stores. Each pair boots its populated hard disc with factory CMOS to the desktop, and the scenario then runs through the observer path.
- **Pad answers:** driven by `rpc_runner --pad-at` through the real libretro input path.
- **Observer states:** read from the core log (`recognised-open`, `answer-available`, `modal-unknown`, `RPC disc:`).
- **Guest outcomes:** read from screenshots.

## Profile revision 1 parameters

Shared by every enabled pair below.

| Parameter | Value |
|---|---|
| Error-box poll | `OS_Byte 129` with R1 = 0 and R2 = 0, from ROM, in SVC26 or SVC32 at SWI entry. The site and stack are learned from the first such poll while the user report is pending. |
| Answerable forms | Old-style flags 3 and 14 only. |
| Answerable error numbers | Ordinary numbers: any number with the serious-error bit (bit 31) clear. Serious numbers are observation only. |
| Form 3 | OK and Cancel; default OK. A answers OK (Return), B answers Cancel (Escape). |
| Form 14 | Cancel only; default Cancel. B answers Cancel; A gives no answer. |
| Other synchronous forms | Observation only: a truthful manual hint, no OK/Cancel claim. |
| Disc signal A device forms | Unspecified device only (`XOS_UpCall` R3 = 0xFFFFFFFF, D-179). |
| Disc signal B | The shared Arculator path and operation predicates (`ADFS::<inserted disc or 0>.$…`), first miss ignored and a repeated miss resolved, as on Arculator. |
| Launch outcome | Bound to the explicit attempt and its exact generated command. Role-1 authored status on `XOS_Byte 0` in the frozen 64-byte layout (consume, registered, dispatch, start returned, returned, desktop, rejected, closed). A StartTask return with a live child stays pending. Delivery observation horizon 120 s virtual; expiry is unknown, never a retry. |
| Boot watch | A pending user-mode `OS_ReadLine` during the exact blocked command: R1 1–255, R2 = 32, R3 = 126, backed RAM buffer. Return continuation ceiling 64,000,000 instructions and 2 s virtual; manual interaction offered after 250 ms virtual; at most one watch; no mode subscription; never a relaunch. |

## Calibration evidence

From legs `c-371-sa110` and `c-350-arm610`:

- **The box's own poll.** While a user-mode `Wimp_ReportError` has not returned, only the box's own poll site runs `OS_Byte 129`. It runs from ROM, in mode 3, with R1 = 0 and R2 = 0.
- **The ordinary key poll never competes.** The Wimp's ordinary key poll uses the same arguments from another site and stack. It runs before and after the box, never inside it.
- **No nested reports.** No report is nested inside the box.

## Cells

Pairs are written catalogue × CPU, using the indices in `core/rpc_selection.h` and `engine/rpc/rpc_session.h`.

| Pair | Error observe | Error answer | Disc A observe | Disc A replace | Disc B observe | Disc B replace | Boot | Launch |
|---|---|---|---|---|---|---|---|---|
| 3.50 ARM610 (1 × 0) | on | on | on | on | on | on | on | on |
| 3.60 ARM710 (2 × 1) | on | on | on | on | on | on | on | on |
| 3.60 ARM7500 (2 × 3) | on | on | on | on | on | on | on | on |
| 3.70 SA-110 (3 × 2) | on | on | on | on | on | on | on | on |
| 3.71 ARM710 (4 × 1) | on | on | on | on | on | on | on | on |
| 3.71 ARM7500 (4 × 3) | on | on | on | on | on | on | on | on |
| 3.71 SA-110 (4 × 2) | on | on | on | on | on | on | on | on |

**Boot and launch (all pairs, D-203):** enabled at profile revision 1 on the R3c-3b evidence below. The route that issues launch attempts stays behind the closed compile-time gate until R3d-4 (D-186); production routing and session-ID allocation are re-verified there.

## Evidence per cell

The error-box evidence covers the error observe and error answer cells.

| Leg | Pairs | Result |
|---|---|---|
| `a3-*`, `b3-*`: BASIC user-mode `Wimp_ReportError` flags 3, error number 0, from F12 (text-mode report) | all seven | Hint "A = OK · B = Cancel". Pad A returned 1 and pad B returned 2. |
| `ga-*`, `gb-*`: the same call, error number 0, from a `*WimpTask` BASIC program (graphical Cancel/OK box) | all seven | Hint "A = OK · B = Cancel". Pad A returned 1 and pad B returned 2. |
| `gna-*`, `gnb-*`: the graphical box with ordinary error number 291 | all seven | The same Cancel/OK box. Pad A returned 1 and pad B returned 2. |
| `b14-*`: `*WimpTask Error` (flags 14) | all seven | Manual hint, upgraded to "B = Cancel" after the first poll. Pad B cancelled the box. |
| `a14-*`: the same box, pad A | all seven | No answer; the box stayed open. Keyboard Return then ended it. |
| `o-350-arm610` | 3.50 ARM610 | Under observation alone, keyboard Return still dismissed the box. |

The disc evidence covers the disc A and disc B cells.

| Leg | Pairs | Result |
|---|---|---|
| `k-*`: `*WimpTask Cat` of disc 2's name with disc 1 of a two-disc set in drive 0 | all seven | Signal A was seen (unspecified device), candidate 2 was inserted automatically, and the catalogue was shown. |
| `u-*`: `*WimpTask Cat ADFS::ArcNone.$` | all seven | Signal A was seen with no candidate. The request was left to the user, and Escape cancelled it. |
| `sb-*`: user-mode `OS_File 5` on a directory present only on disc 2 (`ADFS::0.$.<name>`) twice, then once more | all seven | The first miss was ignored and the second inserted disc 2. The final call found the object. |
| `d-084d`: a real two-disc install | 3.71 SA-110 | The request for disc 2 was satisfied automatically, and the install completed with no manual swap. |

The launch evidence covers the boot and launch cells (R3c-3b at `2bde88d`, D-203). The legs ran through `rpc_runner` and the test-flagged plan core with project-authored subjects on admitted two-drive copies of the pair stores, cells 0 and 3, the authored-status trace on.

| Leg | Pairs | Result |
|---|---|---|
| `ret-*`, `desk-*`: authored returning command and desktop application | all seven | Role-1 consume, registered, dispatch, start returned, then returned or desktop, closed; one token. Cells 3: returned / desktop handoff established. Cells 0: guest-identical, outcome unknown. |
| `blk-*`, `prm-*`: authored blocked command and pending prompt | all seven | Dispatched and pending, then unknown at the 120 s horizon, never retried. Cells 3 recognised the pending read as waiting for input. Cells 0 and 3 guest-identical. |
| `pin-*`: the prompt answered, then the final pause | all seven | Waiting, input resumed, returned; one token. |
| `dup-*`: the gate run again after the command returned | all seven | The guest's own run counter stays 1; no second consume; the helper reports its duplicate summary. |
| `rst-*`: reset at prepared, consumed, dispatch, executing and handoff | all seven | Interrupted; the no-op helper attached and the barrier reopened; the reboot reaches the desktop with no relaunch and no new token. |
| `swp-*`: authorised swaps before launch and in flight | all seven | The attempt is kept; no second execution. |
| `i084-371sa110`, `p084-371sa110`: a real two-disc install and its product | 3.71 SA-110 | Started through the controller; the request for disc 2 resolved and inserted automatically; installed. A cold relaunch of the installed application was verified, launched under a new token and reached gameplay; a missing product was refused before execution. |
| `one-371sa110`: an original one-drive store | 3.71 SA-110 | "RiscPC launch delivery is unavailable: helper drive :1 is unconfigured. Manual input remains available."; no attempt; the guest reaches the desktop. |
| `odin-371sa110`, `win-371sa110`: the 3.71 SA-110 legs on the Odin3 (Android arm64) and native Windows (MinGW) | 3.71 SA-110 | Attempt states, role-1 events and every frame's guest counters identical to the Mac. |

**Configuration policy (D-203).** New CMOS records use D-190's two-floppy defaults, so drive 1 holds the helper. Existing one-drive records are kept unchanged; preparation reports launch delivery as unavailable and leaves the session manual. No CMOS byte is changed automatically.

## Cost

Measured on the Odin3 with cells as above (`observer_cells=252`) at run E `686b1b7`, on the authored hot workload against pinned `98bb162` (realtime, two ABBA orders of six 300 s runs): p50 +1.56 % and +0.82 %, p95 within 3 % (D-180, D-181).

R3c-3b (D-203), Odin3, measurement cores (shipping-identical hot code), authored hot workload, realtime 60 + 300 s, two six-run orders: cells 255 against observers off on the same bytes p50 +1.45 % and +1.13 %, p95 +1.36 % and +1.48 %; `main` before R3c-3b against R3c-3b, both cells 255, p50 −0.04 % and −0.64 %. Launch phases, tracing off, cells 0 against 3 (3.71 SA-110, plan core): dispatch −1.3 %, desktop −1.4 %, blocked +0.3 %, prompt while its watch is live +36 % (ceiling 100 %) and +0.4 % after it, expired −2.0 % at p50; p95 within the same ceilings.

## Not qualified

These stay observation-only or disabled:

- new-style (bit 8) report forms;
- serious error numbers (bit 31 set): RISC OS replaces the Cancel/OK box with "may have gone wrong… Describe / Quit / Continue" (legs `gsa`, `gsb` on 3.50 ARM610 and 3.71 SA-110), so the form-3 answers would be wrong there;
- immediate-return and close-control forms, which are excluded by D-177(2);
- explicit drive 0 for signal A;
- unknown firmware;
- authored guests.
