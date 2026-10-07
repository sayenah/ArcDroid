# R1 D2 independent guest expectations

These are authored ARM instruction streams, not firmware-derived ROMs. The
build generates ROM headers from `rpc_d2_rom.py`; no ROM binary or private
seed is committed. `rpc_d2_goldens.py` computes expectations with Python
`Fraction`, declared MMIO instruction ordinals and stated device contracts.
It never executes or reads the core. `rpc_d2_expectations.py` generates the
build-only PCM arrays and compares real execution traces to the six committed
text goldens. Every PCM pair is compared, in addition to its hash and counts.
Changing a new golden requires changing its independent expectation; core
output is never used as a golden generator.

## References and declared timing

- ARM [IOMD functional specification](https://www.bitsavers.org/components/arm/IOMD_Functional_Specification_199406.pdf),
  issue 4, sections 4 and 7: DMA alternating Cur/End buffers, quadword requests,
  page wrap, status/interrupt, enable/clear and end stop flag.
- ARM [VIDC20 datasheet](https://www.bitsavers.org/components/arm/VIDC20_Datasheet_199502.pdf),
  DDI0030E, section 13: serial 16-bit sound, SFR divider and sound control.
- Licensed RPCEmu 0.9.5 `sound.c` (manifest adaptation record): low 16-bit left,
  high 16-bit right word layout. The complete source notice is preserved in
  the owned implementation; host sound code is excluded.
- PS/2 set 2: `a` make `1c`, break `f0,1c`; Space `29`; Delete
  `e0,71` / `e0,f0,71`. Default typematic parameter `2b` gives 500 ms initial
  delay and `(8+3)*2^1/240 = 11/120` seconds per repeat. Parameter `00`
  gives 250 ms delay and `1/30` second per repeat. These wire expectations
  are constants declared here, independent of the implementation mapping.

The accepted instruction-rate model is 20/35/100/35 MIPS for models 0…3.
An instruction ordinal n executes at `ceil(n*1000/rate)` ns. Events tied with
an instruction run first; tied source sound runs before 48 kHz output. Rational
deadlines are ceiling-rounded with carried remainders. This is the accepted
integer scheduler contract, not a CPU cache/bus-cycle accuracy claim.

## PCM and integer acceptance

The guest puts four stereo words in both DMA banks:

| Word | Left | Right |
|---|---:|---:|
| `7fff8000` | -32768 | 32767 |
| `fb2e04d2` | 1234 | -1234 |
| `04d2fb2e` | -1234 | 1234 |
| `80007fff` | 32767 | -32768 |

It enables 44.1 kHz (`176400/(2+2)`), waits about 100 µs using a declared
instruction loop, changes to 62.5 kHz (`250000/(2+2)`), waits again, disables,
waits, rearms both banks and enables, then selects unsupported compatibility
sound. Serial format/rate changes restart source phase at the MMIO timestamp;
they preserve already generated output. Zero-order hold samples at exactly
48 kHz; disabled/compatibility output is zero. No floating-point tolerance is
used. At 10 ms each model generates 480 pairs and consumes 16 DMA pairs in
four quadwords. Guest-visible DMA status is `03` after bank A's first transfer,
`06` after exhaustion, and RAW sound IRQ is `10`. Compatibility mode has the
same DMA counts, all-zero output, and one deferred unsupported-mode log.

A separate state-machine guest expects status sequence
`06,06,02,07,07,03,06,02,00,06,02,00,07,03,06,07`:
clear enters idle OIA; a wrong-bank idle write leaves it there; programming A
enters busy IA with its interrupt still requesting B. Exhaustion enters OIB,
which similarly requires B before requesting A. Programming the inactive bank
clears the interrupt. StopA prevents continuation even with B programmed;
explicit B rearming recovers. The final transfer runs from offset `ff0` through
page wrap to `000`, with exactly two quadwords. Total is eight quadwords/32
pairs. These status constants are declared separately from the PCM generator
and are observed by guest register reads.

Hashes use FNV-1a over canonical little-endian stereo words, independent of
native endianness. All four models have their own declared MMIO chronology
and committed `rpc-d2-N.trace`; actual frontend PCM has a separate 50,000,001 ns
analytic hash.

Actual `retro_run` acceptance legs each generate 800 pairs. A callback first
rejects all, next accepts one third (266), then accepts all (800). Thus
generated=2400, accepted=1066, discarded=1334, queued=0. A full-accept leg has
identical executed instruction count, DMA count and guest time. Queue-only
testing generates 9600 pairs at 200 ms: the 8192-pair bound discards 1408 oldest
pairs, and repeated peek does not drain. Recovery accepts the remaining 8192.
Over-acceptance clamps to the offer; a missing callback discards its offer.

A fault with 48 queued pairs purges them. Each subsequent frontend run emits
960 zero pairs while guest instructions/time remain frozen. The legacy audio
read also emits bounded silence. Reset and stop purge queued output into
discarded; the identity `generated=accepted+discarded+queued` is checked.

## Keyboard, ownership and mouse

The guest reads real IOMD receive/status registers and logs wire bytes in RAM.
Tests submit ArcDroid source-owned input and also exercise actual frontend
polling. They check that guest time does not change during frontend gathering.
Source FIFO ingestion is one event per nominal 1 ms scheduler tick or exact
repeat/release edge; pending releases
precede pending makes, followed by typematic. All existing event slots retain
their ordering, with input appended last.

- Default `a`: one make, two repeats in the test interval, one break. F3/00:
  one make, eight repeats, one break, including command/parameter ACKs.
- Physical keyboard and pad holding the same key yield one effective make;
  retiring the pad cannot break a physically held key. Retiring the final
  owner produces one break. Reset cancels ownership/repeat; holding the old
  source state cannot generate later repeats without a new submission.
- Default pad mapping produces Space make/break. Actual runtime remapping
  retires Space, enforces the established neutral gate, then makes `a` after
  a fresh press; shared physical ownership survives pad retirement.
- Relative mouse `(+12,-7)` yields RiscPC quadrature `(12,7)`. GPIO buttons
  are active low: left held `30`, released `70` (hex). A7000 PS/2 emits
  ACK `fa`, left-down `09,00,00`, movement `09,0c,07`, release `08,00,00`.
- Actual frontend gathering submits motion before button changes, matching
  the existing frontend contract: A7000 `fa,08,0c,07,09,00,00,08,00,00`.
- Left/right/middle are source-arbitrated: headers `09,0b,0f`, then retirement
  `0e,0c,08`. Relative `(-130,+130)` splits magnitudes `63,63,4`; A7000
  signed headers are `38` with `c1,c1`, `c1,c1`, then `fc,fc`. RiscPC's
  16-bit quadrature after the earlier movement is `(65418,65413)`.

The common input service/mapping and platform adapters are unchanged. Native
joystick and mousehack remain disabled for these models.

## Factory capture

On SA-110 RiscPC and A7000, an authored guest waits for Delete make and writes
all 256 CMOS addresses over the real I2C MMIO bus while that key is held. Byte
i is exactly `i XOR 5a`. The caller's declared seed is `i XOR a5`, and a separate
seed/reset test verifies it is copied unchanged and volatile reset preserves
the guest-written bytes. Neither synthetic pattern contains firmware content.

Factory admission supplies zero volatile bytes, internally holds Delete at
time 0 and 11,999,999,999 ns, releases it at 12,000,000,000 ns, and completes
at 30,000,000,000 ns. The guest observes the Delete break. Expected write count
is 256 and the 32-byte written-address mask is all `ff`. The golden's complete
256-byte hex line is independently calculated from `i XOR 5a`, never captured
from the backend. Completion freezes guest time; reset cancels completion,
preserves volatile bytes, and resumes ordinary guest execution.

A second authored guest writes nothing: at exactly 30 seconds it faults with
zero writes and purges audio. No firmware address or checksum heuristic is
involved. Factory tests use idle-capable models for bounded host cost; broader
platform/private-firmware qualification remains D3/E.

## Test selection

`rpc-d2-guest-{0,1,2,3}` and `rpc-factory-capture-{2,3}` load the actual test core
through the dispatcher, execute authored MMIO, assert the boundaries above and
compare the complete trace. Existing sanitizer `rpc-.*` selects all six; the
native Windows CTest job runs all registered tests. Neither job needs a changed
regex. The runner accepts `<test-core> --guest <0..3>` or `--factory <2..3>`;
External plan/script admission and integrated pacing replay remain D3.
