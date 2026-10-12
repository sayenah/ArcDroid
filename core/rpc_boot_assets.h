/* ArcDroid-authored RiscPC boot step. GPL-2.0-only. */
#ifndef ARC_RPC_BOOT_ASSETS_H
#define ARC_RPC_BOOT_ASSETS_H
#include <stddef.h>
#include <stdint.h>
#define ARC_BOOT_ASSET_COUNT 7u
typedef struct {
  const char *path;
  const uint8_t *data;
  size_t size;
  uint32_t type;
} ArcBootAsset;
extern const ArcBootAsset arc_boot_assets[ARC_BOOT_ASSET_COUNT];
#endif
