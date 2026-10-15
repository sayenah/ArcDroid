/* Allocation ownership seam confined to the scanner test build. GPL-2.0-only. */
#include <stdlib.h>
#include "rpc_inventory_limits.h"
extern void *arc_test_rpc_inventory_malloc(size_t size);
extern void arc_test_rpc_inventory_free(void *data);
#define malloc arc_test_rpc_inventory_malloc
#define free arc_test_rpc_inventory_free
#include "../libretro/rpc_inventory.c"
