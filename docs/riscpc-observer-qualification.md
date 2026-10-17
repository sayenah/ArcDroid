<!-- SPDX-License-Identifier: GPL-2.0-only -->

# RiscPC observer qualification record (R3c-2)

Status: adopted (D-181; amended by D-187 for ordinary error numbers). Fable's adjudication for the R3c-2 qualification-and-promotion checkpoint (D-177(8)). The production capability table in `engine/rpc/rpc_observer.c` must equal this record exactly. A cell absent from this record stays disabled with the reason given here.

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

## Calibration evidence

From legs `c-371-sa110` and `c-350-arm610`:

- **The box's own poll.** While a user-mode `Wimp_ReportError` has not returned, only the box's own poll site runs `OS_Byte 129`. It runs from ROM, in mode 3, with R1 = 0 and R2 = 0.
- **The ordinary key poll never competes.** The Wimp's ordinary key poll uses the same arguments from another site and stack. It runs before and after the box, never inside it.
- **No nested reports.** No report is nested inside the box.

## Cells

Pairs are written catalogue × CPU, using the indices in `core/rpc_selection.h` and `engine/rpc/rpc_session.h`.

| Pair | Error observe | Error answer | Disc A observe | Disc A replace | Disc B observe | Disc B replace | Boot | Launch |
|---|---|---|---|---|---|---|---|---|
| 3.50 ARM610 (1 × 0) | on | on | on | on | on | on | off | off |
| 3.60 ARM710 (2 × 1) | on | on | on | on | on | on | off | off |
| 3.60 ARM7500 (2 × 3) | on | on | on | on | on | on | off | off |
| 3.70 SA-110 (3 × 2) | on | on | on | on | on | on | off | off |
| 3.71 ARM710 (4 × 1) | on | on | on | on | on | on | off | off |
| 3.71 ARM7500 (4 × 3) | on | on | on | on | on | on | off | off |
| 3.71 SA-110 (4 × 2) | on | on | on | on | on | on | off | off |

**Disabled reason, boot and launch (all pairs):** "awaiting R3c-3 launch hook (S64: *WimpTask starts tasks in SVC mode; F12 calls Wimp_Initialise; 3.5+ prompt buffers lie above 0x8000)". Their profile parameters (prompt buffer form, launch provenance) are established with R3c-3's hook.

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
| `k-*`: `*WimpTask Cat ADFS::FlyingH2.$` with disc 1 of a two-disc set in drive 0 | all seven | Signal A was seen (unspecified device), candidate 2 was inserted automatically, and the catalogue was shown. |
| `u-*`: `*WimpTask Cat ADFS::ArcNone.$` | all seven | Signal A was seen with no candidate. The request was left to the user, and Escape cancelled it. |
| `sb-*`: user-mode `OS_File 5` on `ADFS::0.$.EuroBlastr` twice, then once more | all seven | The first miss was ignored and the second inserted disc 2. The final call found the object. |
| `d-084d`: a real two-disc install | 3.71 SA-110 | "Please insert disc 'FlyingH2'" was satisfied automatically, and the install completed with no manual swap. |

## Cost

Measured on the Odin3 with cells as above (`observer_cells=252`) at run E `686b1b7`, on the authored hot workload against pinned `98bb162` (realtime, two ABBA orders of six 300 s runs): p50 +1.56 % and +0.82 %, p95 within 3 % (D-180, D-181).

## Not qualified

These stay observation-only or disabled:

- new-style (bit 8) report forms;
- serious error numbers (bit 31 set): RISC OS replaces the Cancel/OK box with "may have gone wrong… Describe / Quit / Continue" (legs `gsa`, `gsb` on 3.50 ARM610 and 3.71 SA-110), so the form-3 answers would be wrong there;
- immediate-return and close-control forms, which are excluded by D-177(2);
- explicit drive 0 for signal A;
- unknown firmware;
- authored guests.
