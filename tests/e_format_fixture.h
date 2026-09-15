/* ArcDroid synthetic ADFS E-format test fixture. GPL-2.0-only. */
#ifndef ARCDROID_TEST_E_FORMAT_FIXTURE_H
#define ARCDROID_TEST_E_FORMAT_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARC_TEST_E_FORMAT_SIZE 819200u

static uint8_t arc_test_e_format_zone_check(const uint8_t *map)
{
   unsigned sum[4] = {0u, 0u, 0u, 0u};
   size_t rover = 1020u;
   while (rover > 0u)
   {
      sum[0] += map[rover] + (sum[3] >> 8u); sum[3] &= 0xffu;
      sum[1] += map[rover + 1u] + (sum[0] >> 8u); sum[0] &= 0xffu;
      sum[2] += map[rover + 2u] + (sum[1] >> 8u); sum[1] &= 0xffu;
      sum[3] += map[rover + 3u] + (sum[2] >> 8u); sum[2] &= 0xffu;
      rover -= 4u;
   }
   sum[0] += sum[3] >> 8u;
   sum[1] += map[1] + (sum[0] >> 8u);
   sum[2] += map[2] + (sum[1] >> 8u);
   sum[3] += map[3] + (sum[2] >> 8u);
   return (uint8_t)((sum[0] ^ sum[1] ^ sum[2] ^ sum[3]) & 0xffu);
}

static bool arc_test_make_e_format(uint8_t *image, size_t size)
{
   static const uint8_t disc_name[10] =
      {'A', 'r', 'c', 'D', 'r', 'o', 'i', 'd', 'E', '1'};
   if (!image || size != ARC_TEST_E_FORMAT_SIZE) return false;
   memset(image, 0, size);
   uint8_t *record = image + 4u;
   image[3] = 0xffu;
   record[0] = 10u; /* 1024-byte sectors. */
   record[1] = 5u;
   record[2] = 2u;
   record[3] = 2u;
   record[4] = 15u;
   record[5] = 7u;
   record[6] = 1u;
   record[9] = 1u;
   record[10] = 0x20u; record[11] = 0x05u; /* 0x0520 spare bits. */
   record[12] = 0x03u; record[13] = 0x02u; /* Root 0x00000203. */
   record[17] = 0x80u; record[18] = 0x0cu; /* 0x000c8000 bytes. */
   record[20] = 0x34u; record[21] = 0x12u; /* Synthetic, non-zero disc id. */
   memcpy(record + 22u, disc_name, sizeof(disc_name));
   image[0] = arc_test_e_format_zone_check(image);
   return true;
}

#endif
