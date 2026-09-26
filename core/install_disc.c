/* Project-authored FileCore new-map install-disc formatter. GPL-2.0-only.
 * The map and directory rules follow the synthetic F-format fixture, scaled
 * to the fixed A3020 IDE geometry; no supplied-media bytes enter the image. */
#include "install_disc.h"
#include <string.h>

#define MAP_ZONE_BYTES ARC_INSTALL_HD_SECTOR
#define MAP_ZONES 5u
#define MAP_BYTES (MAP_ZONE_BYTES * MAP_ZONES)
#define MAP_ZONE_BITS (MAP_ZONE_BYTES * 8u - 32u)
#define MAP_ADDRESS ((2u * MAP_ZONE_BITS - 480u) * 1024u)
#define ROOT_ADDRESS (MAP_ADDRESS + 2u * MAP_BYTES)
#define LAST_ZONE_END (32u + ARC_INSTALL_HD_BYTES / 1024u - \
                       ((MAP_ZONES - 1u) * MAP_ZONE_BITS - 480u))

static void put32(uint8_t *p, uint32_t value)
{
   for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
static void set_bits(uint8_t *p, unsigned at, unsigned bits, unsigned value)
{
   for (unsigned i = 0u; i < bits; ++i)
      if ((value >> i) & 1u) p[(at + i) >> 3u] |= 1u << ((at + i) & 7u);
}
static uint8_t zone_check(const uint8_t *block)
{
   unsigned sum[4] = {0u, 0u, 0u, 0u};
   for (int at = (int)MAP_ZONE_BYTES - 4; at > 0; at -= 4) {
      sum[0] += block[at] + (sum[3] >> 8u); sum[3] &= 255u;
      sum[1] += block[at + 1] + (sum[0] >> 8u); sum[0] &= 255u;
      sum[2] += block[at + 2] + (sum[1] >> 8u); sum[1] &= 255u;
      sum[3] += block[at + 3] + (sum[2] >> 8u); sum[2] &= 255u;
   }
   sum[0] += sum[3] >> 8u;
   sum[1] += block[1] + (sum[0] >> 8u);
   sum[2] += block[2] + (sum[1] >> 8u);
   sum[3] += block[3] + (sum[2] >> 8u);
   return (uint8_t)(sum[0] ^ sum[1] ^ sum[2] ^ sum[3]);
}
static uint32_t ror13(uint32_t value)
{ return (value >> 13u) | (value << 19u); }
static void finish_directory(uint8_t directory[2048])
{
   uint32_t check = 0u;
   for (unsigned at = 0u; at < 4u; at += 4u) {
      uint32_t value = (uint32_t)directory[at] |
         ((uint32_t)directory[at + 1u] << 8u) |
         ((uint32_t)directory[at + 2u] << 16u) |
         ((uint32_t)directory[at + 3u] << 24u);
      check = value ^ ror13(check);
   }
   check = directory[4u] ^ ror13(check);
   for (unsigned at = 2008u; at < 2044u; at += 4u) {
      uint32_t value = (uint32_t)directory[at] |
         ((uint32_t)directory[at + 1u] << 8u) |
         ((uint32_t)directory[at + 2u] << 16u) |
         ((uint32_t)directory[at + 3u] << 24u);
      check = value ^ ror13(check);
   }
   directory[2047u] = (uint8_t)(check ^ (check >> 8u) ^
                                  (check >> 16u) ^ (check >> 24u));
}
static void make_root(uint8_t root[2048])
{
   memset(root, 0, 2048u);
   root[0] = 1u;
   memcpy(root + 1u, "Nick", 4u);
   root[2010u] = 0x0bu;
   root[2011u] = 2u; /* parent indirect address 0x20b */
   memcpy(root + 2013u, "ArcDroid fixture", 16u);
   root[2032u] = '$';
   memset(root + 2033u, '\r', 9u);
   root[2042u] = 1u;
   memcpy(root + 2043u, "Nick", 4u);
   finish_directory(root);
}
static uint8_t boot_check(const uint8_t *p)
{
   unsigned sum = 0u;
   for (unsigned i = 0u; i < 511u; ++i) sum += p[i];
   while (sum > 255u) sum = (sum & 255u) + (sum >> 8u);
   return (uint8_t)sum;
}

bool arc_install_hd_format(uint8_t *output, size_t size)
{
   if (!output || size != ARC_INSTALL_HD_BYTES ||
       ROOT_ADDRESS + 2048u > ARC_INSTALL_HD_BYTES) return false;
   memset(output, 0, size);
   uint8_t record[60] = {0};
   record[0] = 9u; record[1] = ARC_INSTALL_HD_SPT;
   record[2] = ARC_INSTALL_HD_HEADS; record[3] = 0u;
   record[4] = 15u; record[5] = 10u;
   record[9] = MAP_ZONES;
   record[10] = 32u;
   put32(record + 12u, 0x20bu);
   put32(record + 16u, ARC_INSTALL_HD_BYTES);
   record[20u] = 0x44u; record[21u] = 0x41u; /* disc id 0x4144 */
   memcpy(record + 22u, "ArcDroidHD", 10u);
   put32(output + 0xc00u, 0x20000000u);
   memcpy(output + 0xdc0u, record, sizeof(record));
   uint8_t map[MAP_BYTES] = {0};
   for (unsigned zone = 0u; zone < MAP_ZONES; ++zone) {
      uint8_t *block = map + zone * MAP_ZONE_BYTES;
      unsigned start = zone ? 32u : 512u;
      unsigned end = zone == MAP_ZONES - 1u ? LAST_ZONE_END : 4096u;
      if (zone == 0u) memcpy(block + 4u, record, sizeof(record));
      /* Fragment 2 owns the boot block in zone 0 and the maps/root in zone 2. */
      if (zone == 0u || zone == 2u) {
         set_bits(block, start, 15u, 2u);
         set_bits(block, start + 15u, 1u, 1u);
         start += 16u;
      }
      if (zone == MAP_ZONES - 1u) {
         /* FileCore marks the allocation beyond the disc end as object 1. */
         set_bits(block, end, 15u, 1u);
         set_bits(block, 4095u, 1u, 1u);
      }
      if (start < end) {
         set_bits(block, end - 1u, 1u, 1u);
         set_bits(block, 8u, 15u, start - 8u);
         set_bits(block, 23u, 1u, 1u);
      }
      block[3] = zone ? 0u : 0xffu;
      block[0] = zone_check(block);
   }
   memcpy(output + MAP_ADDRESS, map, MAP_BYTES);
   memcpy(output + MAP_ADDRESS + MAP_BYTES, map, MAP_BYTES);
   make_root(output + ROOT_ADDRESS);
   output[0xdffu] = boot_check(output + 0xc00u);
   return true;
}
