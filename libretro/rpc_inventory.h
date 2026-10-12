/* Separate diagnostic firmware scan; legacy scan remains untouched. GPL-2.0-only. */
#ifndef ARC_RPC_INVENTORY_H
#define ARC_RPC_INVENTORY_H
#include "rpc_selection.h"
#include "libretro.h"
#define ARC_RPC_SCAN_ENTRIES 1024u
#define ARC_RPC_SCAN_DEPTH 8u
#define ARC_RPC_SCAN_BYTES (64u * 1024u * 1024u)
/* The supplied catalogue seam lets public tests scan authored bytes through
 * exactly the production scanner. No firmware buffers survive this call. */
ArcRpcFirmwareInventory arc_riscpc_inventory_scan(struct retro_vfs_interface *vfs,
   const char *directory, const ArcRpcFirmwareFingerprint *catalogue, size_t count);
#endif
