/* ArcDroid deterministic drive-1 launch disc. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <stdio.h>
#include <string.h>

enum {
   ARC_L_SECTOR_SIZE = 256u,
   ARC_L_TOTAL_SECTORS = ARC_LAUNCH_DISC_SIZE / ARC_L_SECTOR_SIZE,
   ARC_L_ROOT_SECTOR = 2u,
   ARC_L_BOOT_SECTOR = 1279u,
   ARC_L_LAUNCH_SECTOR = 1280u
};

static void arc_store_u24(uint8_t *p, unsigned value)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8u);
   p[2] = (uint8_t)(value >> 16u);
}

static uint8_t arc_old_checksum(const uint8_t *block)
{
   unsigned checksum = 0xffu;
   for (unsigned index = 255u; index-- > 0u;)
   {
      if (checksum > 0xffu) checksum = (checksum + 1u) & 0xffu;
      checksum += block[index];
   }
   return (uint8_t)checksum;
}

static bool arc_launch_name_valid(const char *name, bool application)
{
   if (!name) return false;
   size_t length = strlen(name);
   if (application)
   {
      if (length < 2u || length > 10u || name[0] != '!') return false;
   }
   else if (!length || length > 10u) return false;
   for (size_t i = application ? 1u : 0u; i < length; ++i)
   {
      unsigned char c = (unsigned char)name[i];
      bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
      bool digit = c >= '0' && c <= '9';
      if (!alpha && !digit && c != '_' && c != '-' && c != '+') return false;
   }
   return true;
}

static size_t arc_l_physical_offset(unsigned logical_sector)
{
   unsigned track = logical_sector / 16u;
   unsigned side = track / 80u;
   unsigned cylinder = track % 80u;
   unsigned physical_sector = (cylinder * 2u + side) * 16u +
                              logical_sector % 16u;
   return (size_t)physical_sector * ARC_L_SECTOR_SIZE;
}

static bool arc_l_write(uint8_t output[ARC_LAUNCH_DISC_SIZE],
                        unsigned logical_sector, const uint8_t *data,
                        size_t size)
{
   size_t at = 0u;
   while (at < size)
   {
      unsigned sector = logical_sector + (unsigned)(at / ARC_L_SECTOR_SIZE);
      size_t within = at % ARC_L_SECTOR_SIZE;
      size_t amount = ARC_L_SECTOR_SIZE - within;
      if (sector >= ARC_L_TOTAL_SECTORS) return false;
      if (amount > size - at) amount = size - at;
      memcpy(output + arc_l_physical_offset(sector) + within, data + at, amount);
      at += amount;
   }
   return true;
}

static void arc_old_entry(uint8_t *entry, const char *name, uint32_t load,
                          size_t length, unsigned sector)
{
   size_t name_length = strlen(name);
   memset(entry, '\r', 10u);
   memcpy(entry, name, name_length);
   entry[0] |= 0x80u; /* owner read */
   entry[1] |= 0x80u; /* owner write */
   entry[10] = (uint8_t)load;
   entry[11] = (uint8_t)(load >> 8u);
   entry[12] = (uint8_t)(load >> 16u);
   entry[13] = (uint8_t)(load >> 24u);
   memcpy(entry + 14u, entry + 10u, 4u);
   entry[18] = (uint8_t)length;
   entry[19] = (uint8_t)(length >> 8u);
   entry[20] = (uint8_t)(length >> 16u);
   entry[21] = (uint8_t)(length >> 24u);
   arc_store_u24(entry + 22u, sector);
   entry[25] = 0u;
}

bool arc_launch_disc_build(const char *disc_name, const char *target,
                           uint8_t output[ARC_LAUNCH_DISC_SIZE])
{
   static const char boot[] =
      "| ArcDroid launch disc\r"
      "Desktop -File ADFS::1.$.ArcLaunch\r";
   char disc[24];
   char launch[256];
   if (!output || !arc_launch_name_valid(target, true)) return false;
   if (arc_launch_name_valid(disc_name, false))
      snprintf(disc, sizeof(disc), "ADFS::%s", disc_name);
   else
      snprintf(disc, sizeof(disc), "ADFS::0");
   int launch_size = snprintf(launch, sizeof(launch),
      "| ArcDroid desktop boot file\r"
      "Dir %s.$\r"
      "URD %s.$\r"
      "Filer_Run %s.$.%s\r", disc, disc, disc, target);
   if (launch_size < 0 || (size_t)launch_size >= sizeof(launch)) return false;

   memset(output, 0, ARC_LAUNCH_DISC_SIZE);
   /* Two free extents surround the two allocated bootstrap sectors. */
   arc_store_u24(output, 7u);
   arc_store_u24(output + 3u, ARC_L_LAUNCH_SECTOR + 1u);
   memcpy(output + 247u, "Acri\r", 5u); /* ArcDroid, interleaved halves. */
   arc_store_u24(output + 252u, ARC_L_TOTAL_SECTORS);
   arc_store_u24(output + 256u, ARC_L_BOOT_SECTOR - 7u);
   arc_store_u24(output + 259u, ARC_L_TOTAL_SECTORS - ARC_L_LAUNCH_SECTOR - 1u);
   memcpy(output + 502u, "rDod\r", 5u);
   output[507] = 0x44u;
   output[508] = 0x41u;
   output[509] = 2u;
   output[510] = 6u;
   output[255] = arc_old_checksum(output);
   output[511] = arc_old_checksum(output + 256u);

   uint8_t *root = output + arc_l_physical_offset(ARC_L_ROOT_SECTOR);
   root[0] = 1u;
   memcpy(root + 1u, "Hugo", 4u);
   arc_old_entry(root + 5u, "!Boot", UINT32_C(0xffffeb00),
                 sizeof(boot) - 1u, ARC_L_BOOT_SECTOR);
   arc_old_entry(root + 31u, "ArcLaunch", UINT32_C(0xffffea00),
                 (size_t)launch_size, ARC_L_LAUNCH_SECTOR);
   uint8_t *tail = root + 1227u;
   tail[0] = 0u;
   memset(tail + 1u, '\r', 10u);
   tail[1] = '$';
   arc_store_u24(tail + 11u, ARC_L_ROOT_SECTOR);
   memcpy(tail + 14u, "ArcDroid launch", 15u);
   tail[47] = 1u;
   memcpy(tail + 48u, "Hugo", 4u);
   tail[52] = 0u; /* FileCore's accepted old-directory sentinel. */

   return arc_l_write(output, ARC_L_BOOT_SECTOR,
                      (const uint8_t *)boot, sizeof(boot) - 1u) &&
          arc_l_write(output, ARC_L_LAUNCH_SECTOR,
                      (const uint8_t *)launch, (size_t)launch_size);
}
