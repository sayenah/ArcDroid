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
the upstream file allowlist. No additional upstream source is imported.

## Patch lineage after the pristine import

### Isolation

`isolation-swi` (`src/arm_common.c`): remove HostFS, network, mousehack and
Portable host-idle SWI interception. Every SWI takes ordinary guest exception
handling; the dormant headless backend provides no host filesystem or network.

ArcDroid's `engine/rpc/rpc_namespace.h` maps upstream external identifiers
to `arc_rpc_` and is forced only for RPCEmu objects. Run C2 links all eleven
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

### Devices/AV/input

`devices-empty-fdc` (`src/fdc.c`, `src/fdc.h`): remove image loading/saving and
format tables with `disc.h`, `disc_adf.h` and `disc_hfe.h`. Use the fresh
ArcDroid-owned `engine/rpc/rpc_floppy.h` request interface. Empty drives refuse
synchronously, end the command and lower DMA; a write refusal never raises a
later DMA request. Unsupported commands return the invalid-command byte.
FIFO parameter quotas end before execution, writes during execution/results
are ignored, result indices wrap within 16 bytes, and reset assertion aborts
pending work. Stale callbacks cannot execute a completed command again.
No code from the header-less disc layer is adapted. No `sound.c` code is adapted.
VIDC20 register state and rational raster/flyback timing are retained in C2;
scanout, DMA progression, interlace and audio production/drain remain for D.

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
volatile CMOS and elapsed RTC time. Factory capture remains for D.

## Exclusions and verification

Excluded: Qt/GUI; host threads; network/slirp/TAP; HostFS; CD-ROM/ISO; dynarec;
podule loading/podules; FPA; `rpcemu.c`; `romload.c`, including ROM concatenation
and patch tables; host-threaded `sound.c`; `ide.c`; all `disc*` files;
`cmos.ram`; blank media; every binary and guest file. None is in this subtree.

Run `python3 tools/check-rpcemu-import.py` to verify the tree, licence and
modification notices, patch records and literal/transitive include closure.
Add `--compiler <C compiler>` for compiler dependency closure of all 11 C files.
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
frame/AV branch remains unchanged. Audio/input and plan admission remain D2/D3.
