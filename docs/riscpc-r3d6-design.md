<!-- SPDX-License-Identifier: GPL-2.0-only -->

# R3d-6 option contract

2026-10-21. Implements the adopted R6-1–R6-10 work order at `b69390c`.
This note extends R3d-4's allowed open-gate differences. Closed builds retain
their complete metadata and behaviour. No recognition, qualification, ranking,
engine, record version or path changes are required.

## Tokens and precedence

The two options follow the 20 existing base options, before generated mappings.
Both default to `auto`. Tokens are permanent; labels are presentation only.

| Key | Tokens in order | Labels in order |
|---|---|---|
| `arcdroid_rpc_model` | `auto`, `arm610`, `arm710`, `sa110`, `arm7500` | Auto, ARM610, ARM710, StrongARM, ARM7500 |
| `arcdroid_rpc_os` | `auto`, `350`, `360`, `370`, `371` | Auto, RISC OS 3.50, RISC OS 3.60, RISC OS 3.70, RISC OS 3.71 |

| Any Archimedes Machine/CPU/OS explicit | Any RiscPC axis explicit | Route and diagnostics |
|---|---|---|
| Yes | No | Existing Arculator path; N-9 on flagged content |
| Yes | Yes | Same Arculator path; one N-22 conflict advisory, plus N-9 if applicable; legacy staging |
| No | Yes | RiscPC request before staging, even on raw or legacy-header media; safe staging, inventory scan, per-axis override |
| No | No | Existing flag predicate decides; no extra inventory scan for unflagged content |

A manual model clears CPU hints only; manual OS clears OS hints only. Verified
firmware reads, pair qualification and preparation validation remain mandatory.
A refusal never falls back to Arculator. Missing/empty new values mean Auto;
unknown nonempty new values mean Auto plus N-23 once per session. Existing
invalid-key behaviour is unchanged. Only values actually delivered by a
frontend can produce N-23.

## Notice catalogue additions and replacements

All text is fixed except the allowlisted option label in N-24. No delivered
option value or content name is interpolated. Advisories deduplicate for the
whole loaded session, across Restart, preparation recovery and guest start.

| ID | Text | Lifetime |
|---|---|---|
| N-2 (replacement) | This RiscPC model and RISC OS combination is not supported yet. Choose another RiscPC combination, or try an Archimedes in core options. | Existing class A refusal |
| N-22 | An Archimedes Machine, CPU or Operating System is selected in core options, so the RiscPC options are ignored for this load. | Once on a conflicting load |
| N-23 | An unrecognised RiscPC option value was received. Auto is used for that option. | Once per session, either or both new keys |
| N-24 | {RiscPC model / RiscPC operating system / Machine / CPU / Operating System} takes effect after reloading the content. | Once per changed option per session |
| pair-changed (replacement) | The RiscPC model or operating system changed. This combination has separate RiscPC settings; the content's hard disc is kept. | Existing successful-admission sidecar rule |

Selection baselines are sampled on load, separate from mutable input/media
options. Every successful option read checks reload advisories, including reads
inside Restart without an intervening frame. RiscPC axes advise in either kind;
Archimedes axes advise only in RiscPC. Restart never selects a new backend or
RiscPC pair. Failed loads clear baselines and advisory state for the next load.
Preparation-owned sessions use the host advisory arbiter; Arculator uses the
existing message path. M1's N-12, ignored-option line and seven Archimedes-only
help suffixes remain applicable (R3d-5 must reconcile them when merging).

## Meaning matrix

Rows list option keys without their common `arcdroid_` prefix. Grouped mappings
cover every generated key. In both kinds malformed values of existing keys
retain their existing parsing rules, including Memory. Pending/failed RPC
preparation uses the same frozen selection and media policies as RUNNING.

| Keys | Arculator: load / Restart / runtime | RiscPC M1: load / Restart / runtime |
|---|---|---|
| `machine`, `cpu`, `os` | Select within Arculator / resample and select within Arculator / existing behaviour | Route precedence at load / pair and kind stay fixed / N-24, applies on reload |
| `memory` | Existing 4 MiB selection and validation / unchanged / unchanged | Validated, ignored / ignored / ignored |
| `rpc_model`, `rpc_os` | Suppressed by explicit Archimedes axis / never switch / N-24 | Per-axis selection / never reselect / N-24 |
| `joystick_if` | Existing A3010 hardware gate and keyboard mapping / unchanged / existing input update | Keyboard mapping; A3010 hardware ignored / same / keyboard mappings update |
| `hard_disc` | Native A3020 policy / existing reset behaviour / existing behaviour | Ignored; IDE always attached / unchanged / ignored |
| `jfd_boot`, `adffs_package` | Existing ADFFS/native selection / resampled / existing behaviour | Ignored; native media / ignored / ignored |
| `launch` **M1→R3d-5** | Existing automatic/off/numbered action / resampled / existing behaviour | Ignored; starts manually / ignored / ignored |
| `launch_chooser` **M1→R3d-5** | Existing remember/ask policy / resampled / existing behaviour | Ignored / ignored / ignored |
| `launch_ramdisc` **M1→R3d-5** | Existing optional volatile RAM disc / resampled / existing behaviour | Ignored / ignored / ignored |
| `disk_writes`, `jfd_write_protect`, `short_tail`, `jfd_omitted` | Existing media policy / resampled / existing notices and freeze | Sample / resample / frozen until Restart, N-11 per option |
| `disc_requests` | Auto/hint/off / unchanged / live | Same where observer qualified; otherwise manual / unchanged / live |
| `pointer_mode`, `pointer_stick`, `pointer_speed`, `pointer_deadzone` | Sample / resample / live | Same / same / live (pending input isolated) |
| `pad{1,2}_{up,down,left,right,a,b,x,y,start,select}` | Auto/Unbound/key / resample / live | Same, using RPC keyboard defaults / same / live (pending input isolated) |
| `pad1_left_stick`, `pad2_left_stick` | D-pad/off / resample / live | Same / same / live (pending input isolated) |
| `osk_toggle` | L3/R3/Off / resample / live | Same / same / live (pending input isolated) |

Open-only help additions explicitly label Archimedes selectors/Memory,
reload-only RiscPC selection and the four Restart-frozen media options. Existing
seven Archimedes-only suffixes keep their exact M1 text. All options stay visible.

## Stored-value mapping and reuse

| Model token | Selection enum | Engine/CMOS key/sidecar byte 16 | CMOS record tag/path |
|---|---:|---:|---:|
| `auto` | 0 | Resolved model only | Resolved model only |
| `arm610` | 1 | 0 | 1 |
| `arm710` | 2 | 1 | 2 |
| `sa110` | 3 | 2 | 3 |
| `arm7500` | 4 | 3 | 4 |

| OS token | Selection enum/sidecar byte 17 | CMOS layout |
|---|---:|---:|
| `auto` | 0 (sidecar stores resolved OS) | Resolved layout |
| `350` | 1 | 1 |
| `360` | 2 | 2 |
| `370` | 3 | 3 |
| `371` | 4 | 3 |

The firmware SHA-256 distinguishes 3.70 from 3.71. HD identity remains content
only. A→B→A reopens A's original CMOS; it is never copied from B. The advisory
sidecar is published only after successful admission under the existing content
lock. Missing/corrupt/interrupted sidecars cannot change selection or invalidate
HD/CMOS. Boot records still require the complete existing binding (CPU,
firmware, configuration, content, media, store, plan, helper and revisions);
pair equality alone never admits a record. R3d-5 owns ordinary boot-record wiring.

RetroArch option-file semantics are those of the pinned 1.22.2
[`core_option_manager.c`](https://github.com/libretro/RetroArch/blob/v1.22.2/core_option_manager.c)
and [`runloop.c`](https://github.com/libretro/RetroArch/blob/v1.22.2/runloop.c):
existing unknown keys survive a save; newly created overrides include declared
keys only; a per-game file replaces the global file rather than overlaying it.
Absent RiscPC keys therefore default to Auto. Tests model that storage boundary
explicitly and deliver the resulting values through `retro_*`; native frontend
save/reopen and visibility evidence remains Fable's private leg.
