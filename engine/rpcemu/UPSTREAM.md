# RPCEmu upstream snapshot

- Source: http://www.marutan.net/rpcemu/ (also named in upstream `readme.txt`).
- Version: RPCEmu 0.9.5; Mercurial changeset `1369:f005be758787`.
- Archive: `rpcemu-0.9.5.tar.gz`, SHA-256
  `4e4624641cd1a83af275bd6da8765d4a0e5e0c83f008f059014cc24c5c33d59e`.
- Imported: 2026-10-06, pristine commit
  `fe96e411a747021c7d110b4302d819fe4bca179c` (28 byte-identical files).
- Licence: GPL-2.0-or-later, preserved in `COPYING` and the source headers.
  ArcDroid additions are GPL-2.0-only. D-018 concerns Arculator assets only.

`manifest.json` records the exact allowlist, original paths and SHA-256 values,
quoted original licence headers, and patch lineage. Metadata is separate from
the upstream file allowlist. D2 records the reviewed `sound.c` layout adaptation
in the owned audio implementation; no additional upstream file is imported.

## Patch lineage after the pristine import

### Isolation

`isolation-swi` (`src/arm_common.c`): remove HostFS, network, mousehack and
Portable host-idle SWI interception. Every SWI takes ordinary guest exception
handling; the dormant headless backend provides no host filesystem or network.

ArcDroid's `engine/rpc/rpc_namespace.h` maps upstream external identifiers
to `arc_rpc_` and is forced only for RPCEmu objects. Run C2 links all twelve
adapted upstream C files and the owned session/scheduler edges into both cores,
in addition to the component executables. Only the test core exports session
admission; shipping launch routes and the machine registry remain Arculator.
Arculator keeps its legacy private symbols. `engine/backend-symbols.json`
lists its accepted definitions and boundary services. The object checker
recognises Mach-O, COFF and ELF, rejects common/weak engine symbols and checks
definitions, references and collisions. MinGW COMDAT `.refptr` helpers are
checked as references to their underlying symbol; the single Apple ASan
registration common is compiler bookkeeping, allowed only with `--sanitized`.

### Containment/reset

The controller reset clears its complete volatile state and IRQ/DMA lines as
part of `devices-empty-fdc`. `containment-device-scheduler` completes static
reset for keyboard, mouse, IOMD, SuperIO, i8042 and VIDC20. Mousehack, renderer
buffers/threads and desktop services are removed. Unknown i8042 commands set
receive-timeout; unsupported mouse commands return RESEND. Configuration and
remaining CPU/I2C/device invariants use a non-returning session unwind. Start
owns each allocation immediately and cleanup is idempotent; no frontend call
occurs inside a protected boundary. Absent IDE status is zero (data floats
high); absent podule/EASI reads float high and writes are ignored.

### Scheduler/MMU

`scheduler-mmu-owned-memory` (run C checkpoint: `src/arm.c`,
`src/arm_common.h`, `src/cp15.c/.h`, `src/mem.c/.h`): replace permission-free
translation/pointer shortcuts with a bounded 256-entry, 1 KiB tagged TLB.
Check domain and AP/S/R on every fetch and data hit, including User transfers.
Physical descriptor reads replace the cached host table pointer. Fetch pointers
refer to the actual backing allocation; no guest address is subtracted from a
host pointer. CPU batches are explicitly sized to at most 200 instructions;
CPU and CP15 mutable state reset on load. Unsupported CP15 reads and ARMv3
halfword encodings take Undefined; invalid descriptors take guest translation
aborts. The rotate helper defines rotation by zero. Allocation is owned at
each step: 8 MiB ROM aperture, two 32 MiB RAM banks, and exactly 2 MiB VRAM
on RiscPC or no VRAM on A7000. No realloc remains in memory handling. ROM
bytes mirror only declared 2/4/8 MiB images. DRAM dirty indices are bounded
to the existing 4 MiB tracking window. Run C2 connects instruction credit,
IOMD timers, PS/2 responses, FDC and raster flyback to the integer scheduler.
Timer GO replaces its deadline; periods use the documented latch-plus-one.
Events at an instruction boundary precede it, including immediately applied
MMIO. CPU waits on SA-110 and ARM7500 advance virtual time without credit.
ARM610/710 have no architected wait instruction in this slice. Rates are
20/35/100/35 MIPS. Each call bounds time, instructions and event deliveries.

`scheduler-mmu-batching` (run F: `src/arm.c`, `src/cp15.c`): keep each
instruction's exact rational timestamp inside batches of at most 200; yield
before an event/entry limit, DMA bus hold or CPU idle, and preserve the single
instruction at a completed-field boundary. MMIO changes the cached minimum
deadline immediately. A per-rate fraction table removes repeated division;
minimum scans occur on event mutation. Mix section and 1 KiB subpage bits in
the bounded TLB index to avoid systematic ROM/RAM/MMIO alias collisions.
Cache the in-object instruction pointer with the translation; domain/AP/S/R
and current privilege are still checked on every fetch and data access.
Invalidations clear metadata and pointers together. No workload/rate change.

### Devices/AV/input

`devices-empty-fdc` (`src/fdc.c`, `src/fdc.h`): remove image loading/saving and
format tables with `disc.h`, `disc_adf.h` and `disc_hfe.h`. Use the fresh
ArcDroid-owned `engine/rpc/rpc_floppy.h` request interface. Empty drives refuse
synchronously, end the command and lower DMA; a write refusal never raises a
later DMA request. Unsupported commands return the invalid-command byte.
FIFO parameter quotas end before execution, writes during execution/results
are ignored, result indices wrap within 16 bytes, and reset assertion aborts
pending work. Stale callbacks cannot execute a completed command again.
No code from the header-less disc layer is adapted.
VIDC20 register state and rational raster/flyback timing are retained in C2;
scanout, DMA progression and interlace are completed in D1; sound follows below.

`containment-cmos-rtc` (run C checkpoint: `src/cmos.c`): remove host file/time
services, automatic seed patching and checksum changes. Seed and snapshot are
exact in-memory bytes; guest NVRAM writes are counted, reset preserves them.
Reset clears the complete bus/chip transaction state, including prior pin
levels and cached slave/register fields. Virtual Gregorian conversion is
ArcDroid-authored (`engine/rpc/rpc_rtc.c`), bounded to 1970–2399. Remaining
transaction invariants use the non-returning fatal hook, exercised with a live
component boundary. Internal read/write callbacks are named read_byte/write_byte
so the strict host-service guard can distinguish them without exceptions.
Run C2 supplies epoch plus scheduler time from the real session; reset retains
volatile CMOS and elapsed RTC time. D2 adds factory capture below.

## Exclusions and verification

Excluded: Qt/GUI; host threads; network/slirp/TAP; HostFS; CD-ROM/ISO; dynarec;
podule loading/podules; FPA; `rpcemu.c`; `romload.c`, including ROM concatenation
and patch tables; host-threaded `sound.c`; all `disc*` files;
`cmos.ram`; blank media; every binary and guest file. None is in this subtree.

Run `python3 tools/check-rpcemu-import.py` to verify the tree, licence and
modification notices, patch records and literal/transitive include closure.
Add `--compiler <C compiler>` for compiler dependency closure of all 12 C files.
Add `--tarball <archive>` to verify its digest and compare every original file
with the pristine commit (the commit must be available locally). The archive
is read in memory and never extracted or committed. CI uses no private assets.

`vidc-line-scanout` (run D1: `src/vidc20.c`, `src/iomd.c`): sample every
raster at its leading scheduler edge, after tied devices and before the tied
instruction. Retain converted XRGB8888 lines across later memory/register
writes; no pixel-cycle claim. Preserve pending timing edges and carry rational
fractions; report programmed rates. Flyback reloads INIT; current-pointer
writes restart packed offset. DRAM (VIDCR bit 6) advances by quadwords; VRAM
advances its IOMD current register only on half-SAM requests with Inc*256,
masked END comparison and INIT's last-transfer flag. Cursor DMA is enabled
with video; there is no independent cursor-enable bit. Synchronous HDWR pads
the programmed word length. Reserved depths output border without fabricated
pixels. Interlace publishes fields with a final rational half-line; no weave
or frontend deinterlacing layer is added. A7000 ignores its reserved INT bit.

Presentation is a single lazily allocated 2048×2048 buffer (16 MiB), 2 KiB row
flags and 8 KiB sampled border history. Crop origin is the active top-left at
field start; growth extends it, clips storage only, and never rescans earlier
lines. Hidden rows and columns still consume DMA and bus occupancy. A queued
clipping diagnostic is delivered once outside the protected engine stack.
The session returns at field completion before another line overwrites the
buffer. At most six 16,666,667 ns chunks per frame entry, each with the existing
instruction/event limits plus 33,554,432 sampled pixels. Disabled data still
runs counters/flyback with bounded work. Output allocation failure and scanout
budget exhaustion are contained faults. Reset reuses output; stop frees it.

The instruction-rate scheduler models aggregated DMA bus occupation at a
line boundary (IOMD FS issue 4 section 7: 350 ns/DRAM quad, 156 ns/VRAM
half-SAM, cursor quad every two rows). It blocks instruction credit, while
all devices continue virtual-time delivery. It does not model individual
pixel requests, CPU cache/memory cycles or worst-case arbitration latency.
Frontend AV notifications happen after engine return; the last attempted
rate has 0.01 Hz hysteresis. A rejected/missing callback is not retried each
field; replacing the callback permits a fresh attempt. Size-only changes
use geometry; rate+size changes use one full AV notification. Arculator's
frame/AV branch remains unchanged. Plan admission remains D3.

`audio-input-capture` (run D2: `src/vidc20.c`, `src/iomd.c`,
`src/keyboard.c`, `src/cmos.c`): the owned `engine/rpc/rpc_audio.c` replaces
the host-threaded producer. RPCEmu 0.9.5 `src/sound.c` has a complete
GPL-2.0-or-later notice and Sarah Walker attribution, preserved at the owned
destination. Only its quadword/channel layout is adapted; no host sound,
thread, floating-point resampler or upstream output-buffer policy is retained.
Serial stereo is low signed 16-bit left/high signed 16-bit right per word.
IOMD alternates ready A/B buffers, copies one quadword, advances/wraps the
4 KiB page, and raises its DMA status independently of frontend consumption.
Programming the current bank from idle retains the interrupt requesting the
other bank; only an inactive-bank END write clears that request. Wrong-bank
idle writes do not arm a future transfer. A stop flag enters idle even when
the alternate bank has been programmed; rearming that bank recovers.

Sound source periods are the exact ratio `(SFR+2)/176400` or
`(SFR+2)/250000` seconds. Enable/format changes restart the source phase at the
MMIO timestamp. Ceiling nanosecond deadlines carry remainders; tied source
edges precede 48 kHz output edges, and device events precede tied instructions.
Integer zero-order hold produces continuous 48 kHz stereo pairs, with silence
while disabled or in unsupported 8-bit compatibility mode. The unsupported
mode is logged once outside protected execution when first enabled.

The queue owns 8192 pairs (32768 bytes) plus a four-word DMA cache. Its integer
contract is `generated = accepted + discarded + queued`; DMA frame/block
counts are separate. Peek never drains. Commit clamps acceptance to the offer,
counts the accepted prefix, discards the unaccepted suffix immediately, and
removes the complete offer. There is no retry; overflow discards the oldest
pair. Frontend offers are at most 2048 pairs. None of these policies changes
guest time, DMA progression or interrupts. Reset, stop and fault purge queued
pairs into discarded; reset/stop retain counters, fresh admission clears them.
Fault presentation generates 960 silent pairs per `retro_run`, without guest
execution; a bounded legacy audio read similarly supplies requested silence.
Arculator sound adapters and the `retro_run` function are unchanged.

Frontend polling and existing pad mapping/arbitration occur before protected
execution. A new final scheduler slot consumes at most one arbitrated event
per 1 ms tick, preserving the FIFO and complete-packet backpressure. Owned set-2
key levels, release-before-make reconciliation and rational virtual typematic
feed the existing bounded PS/2 transport. RiscPC mouse input uses quadrature;
A7000 uses PS/2 relative packets. Mousehack and native joystick remain absent.
Reset retires held input and typematic; source ownership remains in ArcDroid's
unchanged input service.

Test-only factory admission uses zeroed volatile CMOS and internally owns
Delete from time zero until exactly 12 seconds. At exactly 30 seconds it freezes
execution and returns the raw 256-byte snapshot, total guest-write count and
256-bit write mask. Zero writes produce a contained bounded failure. There
are no firmware addresses, checksum fixes or guessed seed bytes. Declared
seeds are copied untouched; reset preserves volatile bytes and cancels capture.
No seed/captured guest bytes are stored by the backend. Authored tests and their
analytic synthetic bytes are the only committed capture evidence. D3 supplies
external plans and runners; shipping admission remains dormant.

### D3 integrated qualification correction

`src/iomd.c`: writing FIQMSK now recomputes the interrupt level immediately,
as the existing IRQ mask writes do. The authored guest enables the force-bit
FIQ, counts one handler entry and clears its mask before exception return.
The previous code deferred assertion until another device updated interrupts
and retained the pending FIQ after masking it; both violated MMIO delivery.
No host service, rate, scheduler order or Arculator behavior changes.

`devices-keyboard-reset-held` (run F: `src/keyboard.c`): successful PS/2
self-test restores default scanning and typematic after the existing ACK/BAT
sequence. Held physical keys survive the guest reset in the input layer and
are emitted again after BAT. Opening the IOMD link exposes TX readiness even
when a pre-enable make is unread, allowing firmware to issue its normal reset.
CMOS factory input and seed mode are unchanged; no firmware-specific test or
address is used.

`devices-board-cmos-gate` (run F: `src/mem.c`, `src/iomd.c/.h`): the
RiscPC board latch at physical `0x03310000` echoes bit 7 with its CMOS
protection link unprotected. Byte/word writes update only that gate; mouse
buttons, VGA ID and sound-presence inputs keep their existing behavior.
IOMD reset clears the session-owned gate. ARM7500 has its existing separate
board interface. This is a hardware register, not a firmware address or CMOS
patch. The trial had the same missing-board-latch correction; see Acorn's
Risc PC Technical Reference Manual (Issue 1, September 1994), mouse interface
and LK5, and the RISC OS PRM, Filing system locking and resets.

`devices-fdc-control-status` (run F8: `src/fdc.c`): SMC Specify,
Configure and Sense commands return without an artificial execution delay.
MSR reads preserve FINT; the first result FIFO read and SIS acknowledge it.
Reset polling returns four distinct drive-select causes once, and SIS never
raises an interrupt. Seek results retain drive select and per-drive cylinders.
The typed empty-drive edge supplies mechanical TRK0 (independent of media),
DSKCHG and absent index; SMC ST3 bits 3 and 5 are fixed high. DIR exposes
DSKCHG; empty drives cannot emit index even with a running motor. Existing R1
synchronous data-command refusals and FM READ ID completion are retained.
Reference: SMSC FDC37C665GT datasheet, pp. 34, 36, 42, 69–70:
https://bitsavers.org/components/standardMicrosystems/_SMSC/_dataSheets/FDC37C665GT_SuperIO.pdf

`video-control-readback` (runs F9/G: `src/iomd.c`, `src/vidc20.c`): Risc PC
IOMD VIDCR reads return the programmed D/E/Inc bits. ARM7500 ignores writes
to D/Inc, reads `(value & 0xa0) | 0x50`, and always uses DRAM DMA (ARM DDI
0050C §13.3.64). The A7000 scroll test starts with a zero VIDCR write.
Remove upstream's forced `0x50` defaults on the Risc PC IOMD,
which made a guest enable read-modify-write switch VRAM to DRAM transfers.
The existing DMA paths then use their proper END+half-SAM versus END+quadword
wrap boundaries. Authored hardware-scroll tests cover all four CPUs with an
independent pixel oracle and guest VIDCUR observations; no firmware addresses.

### Run G QA remediation

`devices-keyboard-reset-progress` (`src/keyboard.c`): retain a pending reset
callback when RX is full, and resume reset ACK/BAT after a guest RX read.
Link reset still exposes TX readiness independently. Authored held-key tests
cover both extended Delete and single-byte Left Shift with unread RX.

`devices-fdc-reset-readiness` (`src/fdc.c`): RQM stays clear throughout the
500-unit DOR/DSR reset completion window. FIFO traffic, including synchronous
control commands and DUMPREG, is rejected while RQM is clear, so it cannot
consume or cancel the reset callback. DSR bit 7 self-clears; core/FIFO state
is reset while DOR, CCR, motors and physical head positions survive, and DOR
assertion takes precedence. Completion supplies the four reset SIS causes.
Reference: the SMSC FDC37C665GT datasheet linked above, pp. 33–34 and 42.

`video-hardware-work-bounds` (`src/vidc20.c`): HDER and synchronous HDWR
padding cannot consume pixels beyond HCR+8 clocks. Out-of-range PLL programming
saturates at 120 MHz on every model (run H), retaining exact rational timing
through that bound, including software's 108/110 MHz Risc PC modes. This produces
bounded garbage display rather than a containment fault. The owned scheduler's
event backstop allows the legal maximum raster/timer traffic in 16.667 ms;
its instruction and scanout backstops remain unchanged. No golden is updated.
References: ARM VIDC20 DDI 0030E, feature summary and horizontal-cycle register;
ARM7500 DDI 0050C §8.2.4:
https://www.bitsavers.org/components/arm/VIDC20_Datasheet_199502.pdf
https://documentation-service.arm.com/static/5ed11babca06a95ce53f8fee

Run H (`src/fdc.c`): asserting DOR reset also clears RQM after aborting the
controller state. FIFO writes remain rejected for the entire held interval;
only the rising edge of bit 2 starts the existing reset completion sequence.
The run G DSR software reset and its completion window are unchanged.

The owned RPC frontend derives post-fault 48 kHz silence from the last
announced frame rate, carrying fractional pairs and draining bounded chunks
so slow fields cannot overflow the queue. Factory admission no longer installs
a redundant unwind boundary around its ordinary input setup.

### R2a D-139 timing

`src/arm.c` records atomic instruction requests and yields to exact memory
service; `src/cp15.c/.h` preserve descriptor B/C and connect permission-checked
fetches, maintenance and SA clock switching; `src/mem.c` records successful
CPU attempts without charging descriptor walks or DMA; `src/iomd.c/.h` expose
DRAMWID and distinguish IOMD ROMCR reset 0 from ARM7500 reset 0x40. The owned
implementation is `engine/rpc/rpc_mem_timing.c/.h`, governed by
`docs/riscpc-timing.md`. Existing upstream copyright/licence and ArcDroid
modification notices remain. No firmware identity, PC or loop recognizer is
introduced. Dirty SA ROM castouts follow the accepted G7 interim timing rule;
ordinary ROM/I/O store misses remain unbuffered.

`devices-r2b-floppy` (R2b run A, `src/fdc.c`): D-139 replaces the empty-drive
controller with default-AT DOR pin enables (p.28), all FDC37C665GT commands
from tables 21/22 (pp.46–61), byte/FIFO
execution (pp.43–45), data/TC/result rules (pp.62–68), controls (pp.69–75), and
software-reset retention. Physical DOR selection and HDS are independent from
internal unit/PCN and requested CHRN. All deadlines share the existing FDC slot,
with the internal order documented in the source. The owned `rpc_floppy` service
uses the unchanged effective-sector/guard/store interfaces; no upstream floppy
loader or trial code is copied. Arculator is unchanged.

R2b run B (D-143, owned `rpc_floppy`): raw FORMAT accepts the complete geometry
ID set in any tuple order, with exact physical C/H and size N, and fills fixed
sector offsets. Every other set remains write protected before bytes change;
JFD FORMAT stays refused. Fidelity limit: raw images do not store the supplied
rotational skew/interleave; later reads retain sequential geometry timing.

R2b run C (`src/fdc.c`): media retirement cancels data on that drive while
SEEK/RECALIBRATE/RELATIVE SEEK retain their mechanical session (Table 17,
pp.39,69–72). Reset/teardown invalidate the session; index and head-unload
events remain mount-bound. Cancellation republishes the remaining pending
interrupt instead of acknowledging other units' status. Hardware DSR resets
to 02H/250 kb/s (p.33); software reset retains the programmed rate. D-144
reconciles the contradictory recalibrate descriptions: at most 79 pulses,
with equipment check if TRK0 has not asserted. Existing upstream notices
remain; no loader or third-party helper is added.

### R2c ATA import and patch

`src/ide.c` is added from the same pinned release archive, original SHA-256
`0c86b5f5e3677cfef725ed275086345cf4dedf425413c83fc8f1022a87f4cd62`.
It was imported byte-identically and checked before adaptation. The unchanged
original `ide.h` also matched the archive. This additional original is recorded
in `archive_only_imports`; the existing 28-file pristine commit remains immutable.
The tarball audit checks this original directly and retains the original git-blob
comparison for every earlier file. No git writes are required by implementation.

`devices-r2c-ide` (`src/ide.c`, `src/ide.h`): replace host-file, slave and ATAPI
paths with one `ArcIdeBackend` master. Freeze the command matrix to D-139;
PIO transfers use a 256-sector transaction latch and exactly one backend write
at completion. Use the reserved IDE slot between FDC and SOUND (nine slots
unchanged); readiness/completion takes 1 ms of virtual time, software reset
release 500 us. Byte and word PIO have identical readiness timing. nIEN masks
IOMD IRQB bit 1; main status acknowledges, alternate status preserves pending
IRQ. Reset, task replacement and detachment discard incomplete transfers.
IDENTIFY strings are ArcDroid-authored, with PIO and exact CHS/LBA capacities.
Absent device status stays zero and no IDE event is armed. Each change removes
an excluded host service or supplies the accepted bounded transactional device.

PIO readiness uses the existing ArcDroid Arculator IDE delay of 1,000 us
(`engine/arculator/src/ide.c:78`), expressed in the RPC virtual scheduler.
The 500 us software-reset release is a deterministic engineering latency,
not a measured physical-drive constant. Reset is bus-wide: the master completes
its signature even while the absent slave is selected. R2c activates the private
backend IDE capability; shipping RPC admission remains dormant.

### R2c run K signed long multiply (D-157)

`cpu-smlal-modulo` (`src/arm.c`): SMLAL/SMLALS retain the signed
32×32→64 product (always representable) and convert it to unsigned before
adding the unsigned high/low accumulator. Guest results wrap modulo 2^64;
N/Z observe the resulting bits, and C/V stay unchanged. This removes signed
left-shift and addition overflow. `rpc-cpu-smlal` runs the actual interpreter
on sign-bit, signed-overflow, wrap, negative-product and flag boundaries,
including halt-on-error UBSan. Original copyright and licence remain intact.

`session-preparation-r3b2b` (`src/mem.c`): D-170's offline construction edge
allocates one owned buffer, clears at most 1 MiB of RAM or copies at most 32 KiB
of mirrored ROM per step. No instructions run before complete construction and
final attachment; teardown releases partial allocations. Synchronous low-level
start drains the identical steps, preserving memory bytes, allocation order,
reset behavior and factory-capture timing. No upstream source was added.

### R3c-1 observation primitives

`observation-r3c1` (`src/arm.c`, `src/arm_common.c/.h`, `src/cp15.c`,
`src/mem.c`, `src/fdc.c`): gated original-fetch and successful-fetch continuation
hooks, condition-passed SWI entry/controlled completion, completed body and
event-check mode reports, pure stale-TLB/backed-memory inspector edges, and
internal per-drive idle/media-generation services. No observer policy, firmware
recognition, trace sink or production capability lives in the import. New
external edges already use `arc_rpc_` names; the existing forced namespace and
Mach-O/COFF/ELF definition/reference and export guards apply unchanged.
The session-owned adapter and contract are `engine/rpc/rpc_observer.*`,
`rpc_inspector.c` and `docs/riscpc-observation.md`. Original upstream hashes
and licence headers are unchanged.

R3c-1 run B retains this patch lineage. `arm.c` includes its own loop-only
section twice under compile-time macros, sharing one instruction body. Batch
admission selects the inactive or observed instantiation; the inactive body
has no observer loads or tests. `arm_common.c/.h` expose prefixed armed-only
SWI and status-restoring PC helpers. Timing/service/flush and exception ordering
are unchanged. No imported file, opcode or execution policy was added.

R3c-2 run E (Fable ruling to be D-180) extends the same `observation-r3c1`
patch in `arm.c`: a third instantiation accounts no-watch instruction prefixes
and constructs SWI context only at the condition-passed edge. Watch admission
ends that batch after normal instruction/event processing. Subscribed mode/trace
or live-watch execution selects the checked instantiation. Inactive code remains
the original run B instantiation. Policy, counters and lazy metadata construction
remain in the owned adapter; no upstream hash or licence attribution changes.
