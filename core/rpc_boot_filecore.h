/* General RPC new-map creation writer. ArcDroid-authored GPL-2.0-only. */
#ifndef ARC_RPC_BOOT_FILECORE_H
#define ARC_RPC_BOOT_FILECORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ARC_BOOT_NODES 4096u
#define ARC_BOOT_DEPTH 32u
#define ARC_BOOT_EXTENTS 1024u
#define ARC_BOOT_MAP_AT 62652416u
#define ARC_BOOT_ROOT_AT 62657536u

typedef struct {
  char name[11];
  unsigned parent, child[77], count, source, member, asset;
  uint32_t load, exec, attr, size, address;
  uint8_t hash[32];
  bool directory, authored;
  uint8_t supplied, explicit_meta;
} ArcBootNode;
typedef struct {
  unsigned zone, first, units, id;
  uint32_t physical;
} ArcBootExtent;
typedef struct {
  uint8_t map[2560];
  ArcBootExtent extents[ARC_BOOT_EXTENTS];
  unsigned count, cursor[5], serial[5];
  uint64_t allocated_bytes;
} ArcBootLayout;
int arc_boot_name_compare(const char *, const char *);
void arc_boot_layout_init(ArcBootLayout *);
bool arc_boot_layout_directory(ArcBootLayout *, ArcBootNode *,
                               unsigned directory);
void arc_boot_layout_finish(ArcBootLayout *);
void arc_boot_layout_boot(uint8_t out[512]);
void arc_boot_layout_nick(const ArcBootNode *, unsigned directory,
                          uint8_t out[2048]);
/* One physically contiguous piece, at most requested bytes; logical offsets
 * resolve using fragment-id zone order including wraparound. */
bool arc_boot_layout_resolve(const ArcBootLayout *, uint32_t address,
                             uint32_t offset, uint32_t requested,
                             uint32_t *physical, uint32_t *bytes);
/* Guest-tolerant acceptance of the fixed RPC geometry, checks both maps and
 * live allocation/free chains. Does not read a disc or trust a receipt. */
bool arc_boot_layout_accept(ArcBootLayout *,const uint8_t boot[512],const uint8_t maps[5120]);
#endif
