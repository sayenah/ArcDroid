/* Allocation seam confined to the scanner test build. GPL-2.0-only. */
#include <stdlib.h>
extern void *arc_test_rpc_inventory_malloc(size_t size);
#define malloc arc_test_rpc_inventory_malloc
#include "../libretro/rpc_inventory.c"
