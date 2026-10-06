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
to `arc_rpc_` and is forced only for RPCEmu objects. At this checkpoint only
the FDC component joins the test executable; the full backend is not linked.
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

No upstream scheduler or MMU patch in this checkpoint.

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
