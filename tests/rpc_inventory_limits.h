/* Cheap exhaustive byte-budget tests; production remains 64 MiB. GPL-2.0-only. */
#include "rpc_inventory.h"
_Static_assert(ARC_RPC_SCAN_BYTES == 64u*1024u*1024u, "production scan budget changed");
#undef ARC_RPC_SCAN_BYTES
#define ARC_RPC_SCAN_BYTES 4096u
