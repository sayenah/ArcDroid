/* ArcDroid-owned RPCEmu memory ownership edge. GPL-2.0-only. */
#ifndef ARC_RPC_MEMORY_H
#define ARC_RPC_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Supplied by the session, with allocation-failure injection in test builds. */
void *arc_rpc_alloc(size_t bytes);
void arc_rpc_memory_close(void);
const uint32_t *arc_rpc_phys_page(uint32_t address);
bool arc_rpc_memory_load_rom(const uint8_t *bytes, size_t size);
#endif
