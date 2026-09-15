/* ArcDroid launch planning. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool arc_extension(const char *path, const char *expected)
{
   const char *dot = path ? strrchr(path, '.') : NULL;
   if (!dot) return false;
   ++dot;
   while (*dot && *expected)
   {
      if (tolower((unsigned char)*dot++) != tolower((unsigned char)*expected++)) return false;
   }
   return !*dot && !*expected;
}

ArcMediaKind arc_media_inspect(const char *path, size_t size,
                               char *error, size_t error_size)
{
   ArcMediaKind kind = ARC_MEDIA_INVALID;
   bool supported_extension = arc_extension(path, "adf") || arc_extension(path, "adl");
   if (supported_extension && size == 327680u) kind = ARC_MEDIA_ADF_320K;
   else if (supported_extension && size == 819200u) kind = ARC_MEDIA_ADF_800K;
   else if (supported_extension && size == 655360u) kind = ARC_MEDIA_ADL_640K;
   else if (supported_extension && size == 1638400u) kind = ARC_MEDIA_ADF_HD_UNSUPPORTED;
   if (kind == ARC_MEDIA_ADF_HD_UNSUPPORTED)
   {
      if (error && error_size) snprintf(error, error_size, "1600 KiB high-density ADF is unsupported by the Slice 1 WD1772 machine");
   }
   else if (kind == ARC_MEDIA_INVALID && error && error_size)
      snprintf(error, error_size, "unsupported or corrupt ADF/ADL geometry (%zu bytes)", size);
   return kind;
}

static uint8_t arc_old_map_checksum(const uint8_t *block)
{
   unsigned checksum = 0xffu;
   for (unsigned index = 0u; index < 255u; ++index)
   {
      if (checksum > 0xffu) checksum = (checksum + 1u) & 0xffu;
      checksum += block[index];
   }
   return (uint8_t)checksum;
}

static uint16_t arc_read_le16(const uint8_t *data)
{
   return (uint16_t)(data[0] | ((uint16_t)data[1] << 8u));
}

static uint32_t arc_read_le32(const uint8_t *data)
{
   return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) |
          ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
}

static uint8_t arc_new_map_zone_checksum(const uint8_t *map, unsigned log2_sector)
{
   unsigned sum[4] = {0u, 0u, 0u, 0u};
   size_t sector_size = (size_t)1u << log2_sector;
   size_t rover = sector_size - 4u;
   while (rover > 0u)
   {
      sum[0] += map[rover] + (sum[3] >> 8u); sum[3] &= 0xffu;
      sum[1] += map[rover + 1u] + (sum[0] >> 8u); sum[0] &= 0xffu;
      sum[2] += map[rover + 2u] + (sum[1] >> 8u); sum[1] &= 0xffu;
      sum[3] += map[rover + 3u] + (sum[2] >> 8u); sum[2] &= 0xffu;
      rover -= 4u;
   }
   /* FileCore excludes map[0], the stored ZoneCheck byte. */
   sum[0] += sum[3] >> 8u;
   sum[1] += map[1] + (sum[0] >> 8u);
   sum[2] += map[2] + (sum[1] >> 8u);
   sum[3] += map[3] + (sum[2] >> 8u);
   return (uint8_t)((sum[0] ^ sum[1] ^ sum[2] ^ sum[3]) & 0xffu);
}

static bool arc_new_map_e_format(const uint8_t *data, size_t size)
{
   if (!data || size < 1024u) return false;
   const uint8_t *record = data + 4u;
   unsigned log2_sector = record[0];
   /* Skew is formatter metadata, not a fixed E-format discriminator. */
   if (log2_sector != 10u || record[1] != 5u || record[2] != 2u ||
       record[3] != 2u || record[8] != 0u ||
       record[9] != 1u) return false;
   size_t sector_size = (size_t)1u << log2_sector;
   if (size != 819200u || size % (sector_size * record[1] * record[2]) != 0u ||
       size / (sector_size * record[1] * record[2]) != 80u) return false;
   if (arc_read_le32(record + 16u) != size) return false;

   unsigned id_length = record[4];
   unsigned log2_bytes_per_map_bit = record[5];
   unsigned zone_spare = arc_read_le16(record + 10u);
   size_t sector_bits = sector_size * 8u;
   if (id_length < log2_sector + 3u || id_length > 15u ||
       log2_bytes_per_map_bit < 7u || log2_bytes_per_map_bit > log2_sector ||
       zone_spare > sector_bits - 480u) return false;
   /* The 32-bit zone header prefixes zone_size; only the 60-byte disc record
    * occupies allocation space in zone zero. */
   uint64_t allocation_bits = sector_bits - zone_spare - 480u;
   uint64_t required_bits = size >> log2_bytes_per_map_bit;
   uint64_t ids_per_zone = (sector_bits - zone_spare) / (id_length + 1u);
   if (allocation_bits < required_bits || allocation_bits - required_bits >= ids_per_zone ||
       ids_per_zone < 3u || ids_per_zone > (UINT64_C(1) << 15u))
      return false;
   uint32_t root = arc_read_le32(record + 12u);
   uint32_t root_fragment = (root >> 8u) & 0x7fffu;
   if ((root >> 29u) != 0u || root_fragment < 2u || root_fragment >= ids_per_zone)
      return false;
   for (unsigned offset = 36u; offset < 60u; ++offset)
      if (record[offset]) return false;

   /* A single-zone E-format map has CrossCheck 0xff by definition. */
   return data[3] == 0xffu && data[0] == arc_new_map_zone_checksum(data, log2_sector);
}

bool arc_media_is_recognised_adfs(const uint8_t *data, size_t size)
{
   /* Old-map media retain the two FileCore byte checks. E-format is the
    * single-zone new-map floppy: its bounded map record begins at offset 4,
    * and both ZoneCheck and the all-zone CrossCheck must validate. Failure is
    * advisory because raw game floppies may intentionally use other layouts. */
   return (data && size >= 512u &&
           data[255] == arc_old_map_checksum(data) &&
           data[511] == arc_old_map_checksum(data + 256u)) ||
          arc_new_map_e_format(data, size);
}

bool arc_launch_plan_default(ArcLaunchPlan *plan, ArcMediaKind media,
                             ArcOsClass os, const char content_sha1[41],
                             bool request_a3010_joystick,
                             char *error, size_t error_size)
{
   if (!plan || media == ARC_MEDIA_INVALID || media == ARC_MEDIA_ADF_HD_UNSUPPORTED || os == ARC_OS_UNKNOWN)
   {
      if (error && error_size) snprintf(error, error_size, "launch plan inputs are invalid");
      return false;
   }
   if (request_a3010_joystick)
   {
      if (error && error_size) snprintf(error, error_size, "A3010 joystick interface is incompatible with the A3000 machine");
      return false;
   }
   memset(plan, 0, sizeof(*plan));
   plan->os = os;
   plan->machine = "A3000";
   plan->cpu = "ARM2";
   plan->memory_kib = 4096u;
   plan->media = media;
   if (content_sha1) memcpy(plan->content_id, content_sha1, 41u);
   return true;
}
