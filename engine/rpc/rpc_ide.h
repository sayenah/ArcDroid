/* ArcDroid instance boundary for the single RPC ATA master. GPL-2.0-only. */
#ifndef ARC_RPC_IDE_H
#define ARC_RPC_IDE_H
#include "arc_storage.h"
void arc_rpc_ide_set_backend(const ArcIdeBackend *backend);
void arc_rpc_ide_cancel(void);
bool arc_rpc_ide_pending(void);
#endif
