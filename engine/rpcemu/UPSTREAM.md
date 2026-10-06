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
to `arc_rpc_` and is forced only for RPCEmu objects. At the run C checkpoint FDC, CPU/memory/MMU and CMOS/RTC join separate
component test executables; the full backend is not linked.
Arculator keeps its legacy private symbols. `engine/backend-symbols.json`
lists its accepted definitions and boundary services. The object checker
recognises Mach-O, COFF and ELF, rejects common/weak engine symbols and checks
definitions, references and collisions. MinGW COMDAT `.refptr` helpers are
checked as references to their underlying symbol; the single Apple ASan
registration common is compiler bookkeeping, allowed only with `--sanitized`.

### Containment/reset

The controller reset clears its complete volatile state and IRQ/DMA lines as
part of `devices-empty-fdc`. Other engine containment work remains in R1.

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
to the existing 4 MiB tracking window. These sources join component tests only;
device/session containment and scheduler integration remain unfinished.

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

`containment-cmos-rtc` (run C checkpoint: `src/cmos.c`): remove host file/time
services, automatic seed patching and checksum changes. Seed and snapshot are
exact in-memory bytes; guest NVRAM writes are counted, reset preserves them.
Reset clears the complete bus/chip transaction state, including prior pin
levels and cached slave/register fields. Virtual Gregorian conversion is
ArcDroid-authored (`engine/rpc/rpc_rtc.c`), bounded to 1970–2399. Remaining
transaction invariants use the non-returning fatal hook, exercised with a live
component boundary. Internal read/write callbacks are named read_byte/write_byte
so the strict host-service guard can distinguish them without exceptions.
Factory capture and real session ownership of the RTC hook remain unfinished.

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
