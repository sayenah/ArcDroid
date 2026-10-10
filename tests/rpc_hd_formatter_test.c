/* Independent checks for ArcDroid's synthetic hard-disc formatter.
 * Project-authored GPL-2.0-only. */
#include "install_disc.h"
#include "arcdroid_core.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
   ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u); }
static unsigned get_bits(const uint8_t *p, unsigned at, unsigned n)
{
   unsigned value=0u;
   for (unsigned i=0u;i<n;++i)
      value |= ((p[(at+i)>>3u] >> ((at+i)&7u)) & 1u) << i;
   return value;
}
static uint8_t independent_zone_sum(const uint8_t *p)
{
   unsigned x = 0u, y = 0u, z = 0u, w = 0u;
   for (int i = 508; i >= 4; i -= 4) {
      x += p[i] + (w >> 8u); w &= 255u;
      y += p[i+1] + (x >> 8u); x &= 255u;
      z += p[i+2] + (y >> 8u); y &= 255u;
      w += p[i+3] + (z >> 8u); z &= 255u;
   }
   x += w >> 8u;
   y += p[1] + (x >> 8u);
   z += p[2] + (y >> 8u);
   w += p[3] + (z >> 8u);
   return (uint8_t)(x ^ y ^ z ^ w);
}
int main(void)
{
   assert(ARC_RPC_HD_SECTORS == 258048u);
   assert(ARC_RPC_HD_BYTES == 132120576u);
   uint8_t *first = malloc(132120576u + 1u);
   uint8_t *second = malloc(132120576u);
   assert(first && second);
   first[132120576u] = 0xa5u;
   assert(!arc_hd_format_range(256,13,0,NULL,132120576u));
   assert(!arc_hd_format_range(256,13,132120576u,first,1));
   assert(arc_hd_format_range(256,13,0,first,132120576u));
   assert(arc_hd_format_range(256,13,0,second,132120576u));
   assert(first[132120576u] == 0xa5u);
   assert(!memcmp(first, second, 132120576u));
   assert(le32(first + 0xc00u) == 0x20000000u);
   const uint8_t *record = first + 0xdc0u;
   assert(record[0] == 9u && record[1] == 63u && record[2] == 16u);
   assert(record[3] == 0u && record[4] == 15u && record[5] == 13u);
   assert(record[9] == 5u && record[10] == 32u && record[11] == 0u);
   assert(le32(record + 12u) == 0x20bu);
   assert(le32(record + 16u) == 132120576u);
   assert(record[20u] == 0x44u && record[21u] == 0x41u &&
          !memcmp(record+22u,"ArcDroidHD",10u));
   unsigned sum = 0u;
   for (unsigned i = 0u; i < 511u; ++i) sum += first[0xc00u + i];
   while (sum > 255u) sum = (sum & 255u) + (sum >> 8u);
   assert(first[0xdffu] == (uint8_t)sum);
   const size_t zone_size = (size_t)1u << record[0];
   const size_t allocation_bits = zone_size * 8u -
      ((size_t)record[10u] | ((size_t)record[11u] << 8u));
   const size_t map_at = ((record[9u] / 2u) * allocation_bits - 480u) *
      ((size_t)1u << record[5u]);
   const size_t map_bytes = (size_t)record[9u] * zone_size;
   const size_t root_at = map_at + 2u * map_bytes;
   assert(zone_size == 512u && allocation_bits == 4064u &&
          map_at == 0x3bc0000u && map_bytes == 2560u &&
          root_at == 0x3bc1400u);
   assert(root_at - map_at == ((le32(record+12u) & 255u)-1u)*zone_size);
   const uint8_t *map = first + map_at;
   assert(!memcmp(map, map + map_bytes, map_bytes));
   uint8_t cross = 0u;
   for (unsigned zone = 0u; zone < 5u; ++zone) {
      const uint8_t *block = map + zone * zone_size;
      assert(block[0] == independent_zone_sum(block));
      cross ^= block[3];
   }
   assert(cross == 0xffu);
   assert(!memcmp(map + 4u, record, 60u));
   assert(get_bits(map,512u,15u)==2u && get_bits(map,527u,1u)==1u);
   assert(get_bits(map+2u*zone_size,32u,15u)==2u &&
          get_bits(map+2u*zone_size,47u,1u)==1u);
   assert(get_bits(map+4u*zone_size,384u,15u)==1u &&
          get_bits(map+4u*zone_size,4095u,1u)==1u);
   const uint8_t *root = first + root_at;
   assert(root[0] == 1u && !memcmp(root + 1u, "Nick", 4u));
   assert(root[2007u] == 0u && root[2032u] == '$' &&
          root[2010u] == 0x0bu && root[2011u] == 2u &&
          !memcmp(root+2013u,"ArcDroid fixture",16u) &&
          root[2042u] == 1u && !memcmp(root + 2043u, "Nick", 4u));
   free(first);
   free(second);
   puts("rpc-hd-formatter: independent 126 MiB map/root/checksums pass");
   return 0;
}
