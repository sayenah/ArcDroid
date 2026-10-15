/* Separate diagnostic firmware scan; legacy scan remains untouched. GPL-2.0-only. */
#ifndef ARC_RPC_INVENTORY_H
#define ARC_RPC_INVENTORY_H
#include "rpc_selection.h"
#include "libretro.h"
#define ARC_RPC_SCAN_ENTRIES 1024u
#define ARC_RPC_SCAN_DEPTH 8u
#define ARC_RPC_SCAN_BYTES (64u * 1024u * 1024u)
#define ARC_RPC_IMAGE_BYTES (4u * 1024u * 1024u)
/* The supplied catalogue seam lets public tests scan authored bytes through
 * exactly the production scanner. No firmware buffers survive this call. */
ArcRpcFirmwareInventory arc_riscpc_inventory_scan(const char *directory,
   const ArcRpcFirmwareFingerprint *catalogue, size_t count);
/* Uses the currently bound arc_io VFS. On success caller owns *data (free).
 * Failure always returns NULL/zero outputs; no copy/pair fallback occurs. */
bool arc_riscpc_firmware_read(const ArcRpcFirmwareInventory *inventory,
   ArcRpcSelectionOs os, void **data, size_t *size);
#endif
