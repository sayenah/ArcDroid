/* Frozen RPC helper transport. ArcDroid-authored GPL-2.0-only. */
#ifndef ARC_RPC_LAUNCH_HELPER_H
#define ARC_RPC_LAUNCH_HELPER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ARC_RPC_HELPER_SIZE 655360u
#define ARC_RPC_HELPER_MAX 8192u
/* Descriptor validation is independent of the helper's versioned plan. */
bool arc_riscpc_helper_descriptor(const uint8_t *, size_t, uint32_t *entry);
bool arc_riscpc_helper_build(uint8_t *, size_t);
#endif
