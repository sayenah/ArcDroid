# Authored plan expectations

These expectations are declared from the ARM instruction/exception rules and
the device contracts documented in `rpc-session-expectations.md`,
`rpc-video-expectations.md` and `rpc-d2-expectations.md`. The generators never
load a core or copy observations into an oracle. All ROM bytes and seeds are
authored, generated in the build tree, and excluded from source control.

## CPU/MMU matrix

`rpc_plan_matrix_rom.py` emits 173 four-word records on each model. Guest ARM
stores, mode changes, CP15 operations and exception handlers create the records;
the runner only reads the resulting RAM at physical `0x10040000`.

| Records | Independent expectation |
|---|---|
| 0–1 | SVC26 then SVC32: store/load 42, ALU result 43; address and stored word retained. |
| 2 | Execute RAM `MOV r3,#7`, rewrite it to `MOV r3,#9`, execute again: 7 then 9. |
| 3–66 | All AP × S/R × privileged/user read/write combinations. AP0 uses the declared S/R truth table; AP1 permits supervisor only; AP2 also permits user reads; AP3 permits all. Denial gives section permission FSR `0x1d`, FAR equal to the target; a denied write leaves the physical word unchanged. |
| 67–130 | All 16 domain fields with no-access/reserved/client/manager. The target AP0 denies client; manager bypasses permissions. FSR is domain field plus domain fault `9` or section permission fault `13`; driver code/results use a different permitted domain. |
| 131–150 | Small-page 1 KiB and large-page 16 KiB subpages with AP3/2/1/0, user loads/stores; AP2 is read-only, AP1/0 deny user access. Permission FSR `0x1f`; reserved second-level types give `0x17`. |
| 151–154 | LDRT/STRT/LDRBT/STRBT after a privileged cached hit must still enforce user access, keep destinations/physical words unchanged on denial, and report FSR `0x1d`. |
| 155–163 | Descriptor changes retain an existing translation until the model's invalidation; invalid descriptor types then abort. DACR and TTBR changes invalidate cached access immediately; restoration succeeds. |
| 164–167 | A denied data read establishes FAR/FSR. User fetch executes the last allowed instruction in a 1 KiB subpage, then prefetch-aborts at `0x00400400`: LR `0x00400404`, saved PSR `0xd0`, ABT32 PSR `0xd7`, FAR/FSR unchanged. The forbidden `MOV r3,#9` does not execute: r3 stays 7. |
| 168 | LDRH produces `0x1234` on SA-110; ARM610/710/7500 take Undefined and leave r3 at `0x55`. |
| 169 | Disabling MMU restores the ordinary physical RAM read. |
| 170–171 | Data Abort from SVC32 and USR32: LR = faulting instruction address + 8, SPSR `0xd3`/`0xd0`, ABT32 PSR `0xd7`. Handler recovery returns to the continuation without ending the session. |
| 172 | CP15 c9 read takes Undefined without changing destination `0x55`; IDs are ARM610 `41560610`, ARM710 `41007100`, SA-110 `4401a102`, ARM7500 `41027100`. |

The last guest store marks completion with `0xa5`. Goldens contain the complete
declared record matrix; `rpc-plan-matrix-*` compare every word and the complete
runner CPU/device/IRQ/raster/PCM output under flat and burst pacing.

## AV/input and devices

`rpc-plan-av-*` replay the D1 authored raster cases through content plans and
compare the unchanged analytic raster expectations. Three-frame D2 audio plans
produce 2400 actual libretro stereo pairs: analytic signed PCM hash, 2400
accepted, zero discarded or queued. The combined audio/palette guest compares
its first raster and the first 480 actual PCM pairs to independent D1/D2
expectations. Input plans expose keyboard `1c,f0,1c`; RiscPC quadrature is
`12,7` with buttons `70`. A7000 returns ACK then the discrete button packet,
relative motion packet and release packet:
`fa,09,00,00,09,0c,07,08,00,00`. Equal-frame script order is preserved.

`rpc-plan-devices` verifies both timer requests (`0x60`) through the guest IRQ
handler, a forced FIQ taken exactly once before the handler masks it, the empty
FDC result `40,05,00,00,00,00,00`, absent IDE status/data and expansion-bus
responses, keyboard ACK and model-correct mouse response. Each 2 MiB boundary
of the 8 MiB ROM aperture returns its independently authored marker mirrored
modulo the declared 2/4/8 MiB image size. Flat/burst traces agree.

## Admission and failure classes

Rejected configuration includes malformed/oversized plans or scripts, unknown
models/keys, duplicate fields, invalid rates/epochs/paths/CMOS combinations,
missing or wrong-sized ROM/seed inputs, and allocation failure at every owned
start or lazy presentation stage. Shipping content admission rejects plans and
has no test admission exports.

Architectural aborts, unsupported CP15/ARM instructions and documented invalid
device commands recover in the guest. Contained failures include a real guest
reserved-mode transition, protected-entry injection, and factory capture with
no guest writes by 30 virtual seconds. The plan lifecycle suite verifies reset,
post-fault queue accounting and explicit runner-only seed export. Plan reset
replays must reproduce the same keyboard/mouse wire sequence as a
fresh script, including its frame-zero events. Backend retirement is consumed
after the frontend clears input and resets the engine, before new input is
gathered. Existing
`rpc-session-lifecycle`, `rpc-session-switch` and component tests retain the
same-library fresh/reload/switch/50-cycle, malformed I²C and scheduler-boundary
coverage; no prior test or golden is changed. Internal invariant sites that
cannot be reached from admitted configuration remain listed in the report.

The AC-8 smoke checks its declared rate, mixed MMU loop, denied STRT recovery,
640×480/60 Hz geometry, palette-write counts 481/961/1441, live loop count,
continuous stereo DMA, actual PCM counts and engine timing fields. Device
throughput acceptance is a separate 120 s warm-up / 600 s wall-clock run.
