/* ArcDroid launch planning. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "jfd.h"

#include <stdio.h>
#include <string.h>

static uint8_t arc_old_map_checksum(const uint8_t *block)
{
   unsigned checksum = 0xffu;
   for (unsigned index = 255u; index-- > 0u;)
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

static bool arc_new_map_e_format(const uint8_t *data, size_t available,
                                 size_t declared)
{
   if (!data || available < 1024u || declared != 819200u) return false;
   const uint8_t *record = data + 4u;
   unsigned log2_sector = record[0];
   /* Skew is formatter metadata, not a fixed E-format discriminator. */
   if (log2_sector != 10u || record[1] != 5u || record[2] != 2u ||
       record[3] != 2u || record[8] != 0u ||
       record[9] != 1u) return false;
   size_t sector_size = (size_t)1u << log2_sector;
   if (declared % (sector_size * record[1] * record[2]) != 0u ||
       declared / (sector_size * record[1] * record[2]) != 80u) return false;
   if (arc_read_le32(record + 16u) != declared) return false;

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
   uint64_t required_bits = declared >> log2_bytes_per_map_bit;
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
          arc_new_map_e_format(data, size, size);
}

static bool arc_old_map_geometry(const uint8_t *data, size_t size,
                                 ArcMediaGeometry *geometry,
                                 size_t *declared_size, size_t *max_missing)
{
   if (!data || size < 512u ||
       data[255] != arc_old_map_checksum(data) ||
       data[511] != arc_old_map_checksum(data + 256u) ||
       data[510] > 246u || data[510] % 3u) return false;
   uint32_t old_size = (uint32_t)data[252] | ((uint32_t)data[253] << 8u) |
                       ((uint32_t)data[254] << 16u);
   switch (old_size)
   {
      case 1280u: *geometry = ARC_GEOMETRY_320K; *declared_size = 327680u; *max_missing = 4096u; return true;
      case 2560u: *geometry = ARC_GEOMETRY_640K_L; *declared_size = 655360u; *max_missing = 8192u; return true;
      case 3200u: *geometry = ARC_GEOMETRY_800K_D; *declared_size = 819200u; *max_missing = 10240u; return true;
      default:
         /* Some real 800K D-format maps declare fewer usable sectors while
          * retaining the standard 80x2x5 physical geometry. */
         if (old_size >= 12u && old_size < 3200u &&
             size >= 819200u - 10240u && size <= 819200u &&
             size % 1024u == 0u)
         {
            *geometry = ARC_GEOMETRY_800K_D;
            *declared_size = 819200u;
            *max_missing = 10240u;
            return true;
         }
         return false;
   }
}

static const char *arc_geometry_name(size_t size)
{
   size_t d320 = size > 327680u ? size - 327680u : 327680u - size;
   size_t d640 = size > 655360u ? size - 655360u : 655360u - size;
   size_t d800 = size > 819200u ? size - 819200u : 819200u - size;
   if (d320 <= d640 && d320 <= d800) return "320K";
   return d640 <= d800 ? "640K L" : "800K";
}

bool arc_media_inspect(const char *path, const uint8_t *data, size_t size,
                       ArcMediaInfo *info, char *error, size_t error_size)
{
   (void)path;
   ArcMediaInfo found;
   memset(&found, 0, sizeof(found));
   found.available_size = size;
   if (!data || !info)
   {
      if (error && error_size) snprintf(error, error_size, "invalid media inspection input");
      return false;
   }
   if (arc_jfd_has_signature(data, size))
   {
      ArcJfdImage jfd;
      if (!arc_jfd_parse(data, size, &jfd, error, error_size)) return false;
      found.kind = ARC_MEDIA_JFD;
      found.declared_size = jfd.decoded_size;
      found.recognition = ARC_MEDIA_RECOGNITION_CONTAINER;
      arc_jfd_free(&jfd);
      *info = found;
      return true;
   }

   ArcMediaGeometry geometry = ARC_GEOMETRY_NONE;
   size_t declared = 0u, max_missing = 0u;
   bool descriptor = arc_new_map_e_format(data, size, 819200u);
   if (descriptor)
   {
      geometry = ARC_GEOMETRY_800K_E;
      declared = 819200u;
      max_missing = 10240u;
   }
   else
      descriptor = arc_old_map_geometry(data, size, &geometry, &declared, &max_missing);
   if (descriptor)
   {
      if (size > declared)
      {
         if (error && error_size) snprintf(error, error_size,
            "%s image overshoots declared geometry by %zu bytes",
            arc_geometry_name(declared), size - declared);
         return false;
      }
      size_t missing = declared - size;
      size_t sector_size = geometry == ARC_GEOMETRY_800K_D ||
                           geometry == ARC_GEOMETRY_800K_E ? 1024u : 256u;
      if (missing && (missing > max_missing || missing % sector_size))
      {
         if (error && error_size) snprintf(error, error_size,
            "%s image is short by %zu bytes (tail must be sector-aligned and no more than %zu bytes)",
            arc_geometry_name(declared), missing, max_missing);
         return false;
      }
      found.geometry = geometry;
      found.declared_size = declared;
      found.padding_required = missing;
      found.short_image = missing != 0u;
      found.recognition = ARC_MEDIA_RECOGNITION_DESCRIPTOR;
      if (geometry == ARC_GEOMETRY_800K_E)
      {
         found.boot_option = data[11];
         memcpy(found.disc_name, data + 26u, 10u);
      }
      else
      {
         found.boot_option = data[509];
         for (unsigned i = 0u; i < 5u; ++i)
         {
            found.disc_name[i * 2u] = (char)data[247u + i];
            found.disc_name[i * 2u + 1u] = (char)data[502u + i];
         }
      }
      for (unsigned i = 10u; i > 0u; --i)
      {
         unsigned char c = (unsigned char)found.disc_name[i - 1u];
         if (c > 0x20u && c != 0x7fu) break;
         found.disc_name[i - 1u] = '\0';
      }
   }
   else
   {
      found.declared_size = size;
      found.recognition = ARC_MEDIA_RECOGNITION_SIZE_FALLBACK;
      if (size == 327680u) found.geometry = ARC_GEOMETRY_320K;
      else if (size == 655360u) found.geometry = ARC_GEOMETRY_640K_L;
      else if (size == 819200u) found.geometry = ARC_GEOMETRY_800K_E;
      else if (size == 1638400u)
      {
         if (error && error_size) snprintf(error, error_size,
            "1600 KiB high-density ADF is unsupported by the Slice 1 WD1772 machine");
         return false;
      }
      else
      {
         if (error && error_size) snprintf(error, error_size,
            "unsupported raw image geometry (%zu bytes; nearest %s)", size,
            arc_geometry_name(size));
         return false;
      }
   }
   found.kind = found.geometry == ARC_GEOMETRY_320K ? ARC_MEDIA_ADF_320K :
                found.geometry == ARC_GEOMETRY_640K_L ? ARC_MEDIA_ADL_640K :
                ARC_MEDIA_ADF_800K;
   *info = found;
   return true;
}

bool arc_launch_plan_default(ArcLaunchPlan *plan, ArcMediaKind media,
                             ArcOsClass os, const char content_sha1[41],
                             bool request_a3010_joystick,
                             char *error, size_t error_size)
{
   return arc_launch_plan_resolve(plan, media, os, content_sha1, "Auto", "Auto",
                                  request_a3010_joystick, error, error_size);
}

bool arc_launch_plan_resolve(ArcLaunchPlan *plan, ArcMediaKind media,
                             ArcOsClass os, const char content_sha1[41],
                             const char *machine_name, const char *cpu_name,
                             bool request_a3010_joystick,
                             char *error, size_t error_size)
{
   const ArcMachineDescriptor *descriptor = arc_machine_from_name(machine_name);
   if (!plan || media == ARC_MEDIA_INVALID || media == ARC_MEDIA_ADF_HD_UNSUPPORTED || os == ARC_OS_UNKNOWN)
   {
      if (error && error_size) snprintf(error, error_size, "launch plan inputs are invalid");
      return false;
   }
   if (!descriptor)
   {
      if (error && error_size) snprintf(error, error_size, "invalid machine option");
      return false;
   }
   if (!arc_machine_supports_os(descriptor->id, os))
   {
      if (error && error_size) snprintf(error, error_size, "%s is not supported on the %s",
                                        arc_os_name(os), descriptor->name);
      return false;
   }
   ArcCpu requested_cpu = descriptor->cpu;
   if (cpu_name && strcmp(cpu_name, "Auto"))
   {
      if (!strcmp(cpu_name, "ARM2")) requested_cpu = ARC_CPU_ARM2;
      else if (!strcmp(cpu_name, "ARM250")) requested_cpu = ARC_CPU_ARM250;
      else
      {
         if (error && error_size) snprintf(error, error_size, "invalid CPU option");
         return false;
      }
      if (requested_cpu != descriptor->cpu)
      {
         if (error && error_size) snprintf(error, error_size, "%s is incompatible with the %s machine",
                                           cpu_name, descriptor->name);
         return false;
      }
   }
   if (request_a3010_joystick && descriptor->id != ARC_MACHINE_A3010)
   {
      if (error && error_size) snprintf(error, error_size,
         "A3010 joystick interface is incompatible with the %s machine", descriptor->name);
      return false;
   }
   memset(plan, 0, sizeof(*plan));
   plan->os = os;
   plan->machine = descriptor->id;
   plan->cpu = requested_cpu;
   plan->memory_kib = descriptor->memory_kib;
   plan->a3010_joystick = request_a3010_joystick;
   plan->media = media;
   if (content_sha1) memcpy(plan->content_id, content_sha1, 41u);
   return true;
}
