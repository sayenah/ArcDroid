/* ArcDroid host-side boot-resource population. GPL-2.0-only. */
#ifndef ARC_RPC_BOOT_H
#define ARC_RPC_BOOT_H
#include "rpc_hd_store.h"
typedef struct ArcRpcBoot ArcRpcBoot;
typedef enum {
  ARC_RPC_BOOT_OPEN,
  ARC_RPC_BOOT_HASH,
  ARC_RPC_BOOT_INDEX,
  ARC_RPC_BOOT_PREFLIGHT,
  ARC_RPC_BOOT_TREE,
  ARC_RPC_BOOT_ASSETS,
  ARC_RPC_BOOT_ALLOCATE,
  ARC_RPC_BOOT_FORMAT,
  ARC_RPC_BOOT_WRITE,
  ARC_RPC_BOOT_VALIDATE,
  ARC_RPC_BOOT_REHASH,
  ARC_RPC_BOOT_RECEIPT,
  ARC_RPC_BOOT_SEAL,
  ARC_RPC_BOOT_PUBLISH,
  ARC_RPC_BOOT_DONE,
  ARC_RPC_BOOT_LEGACY,
  ARC_RPC_BOOT_FAILED,
  ARC_RPC_BOOT_CANCELLED
} ArcRpcBootState;
typedef struct {
  uint64_t units, io_bytes, operations, memory_high, max_unit_io_bytes,
      max_unit_operations, allocated_bytes, files, file_bytes;
  unsigned phase, entries, nodes;
  uint8_t packages[2][32];
  bool qualified[2];
} ArcRpcBootStats;
/* Additional resident memory <=8 MiB; per unit <=4 MiB I/O, <=128 callbacks,
 * one 32 KiB inflate output, or one bounded tree/directory operation.
 * step clamps its quantum count to 32. Caller keeps IDE quiescent throughout.
 */
ArcRpcBoot *arc_riscpc_boot_begin(ArcRpcHdStore *, const char *system,
                                  char *error, size_t error_size);
ArcRpcBootState arc_riscpc_boot_step(ArcRpcBoot *, unsigned units);
void arc_riscpc_boot_cancel(ArcRpcBoot *);
void arc_riscpc_boot_free(ArcRpcBoot *);
void arc_riscpc_boot_stats(const ArcRpcBoot *, ArcRpcBootStats *);
const char *arc_riscpc_boot_notice(const ArcRpcBoot *);
#endif
