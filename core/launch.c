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

bool arc_filecore_e_map_layout_valid(unsigned id_length,
                                     unsigned log2_bytes_per_map_bit,
                                     unsigned zone_spare,
                                     size_t disc_size)
{
   const unsigned sector_bits = 1024u * 8u;
   if (id_length < 13u || id_length > 15u ||
       log2_bytes_per_map_bit < 7u || log2_bytes_per_map_bit > 10u ||
       zone_spare > sector_bits - 480u)
      return false;
   uint64_t allocation_bits = sector_bits - zone_spare - 480u;
   uint64_t required_bits = (uint64_t)disc_size >> log2_bytes_per_map_bit;
   uint64_t ids_per_zone = (sector_bits - zone_spare) / (id_length + 1u);
   return allocation_bits >= required_bits &&
          allocation_bits - required_bits < ids_per_zone &&
          ids_per_zone >= 3u && ids_per_zone <= (UINT64_C(1) << 15u);
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
   if (!arc_filecore_e_map_layout_valid(id_length,
                                        log2_bytes_per_map_bit,
                                        zone_spare, declared)) return false;
   uint64_t ids_per_zone = (sector_size * 8u - zone_spare) /
                           (id_length + 1u);
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
      found.disc_name_length = (uint8_t)strlen(found.disc_name);
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

typedef struct {
   const uint8_t *data;
   size_t size;
   size_t at;
} ArcBindingCursor;

static bool arc_binding_line(ArcBindingCursor *cursor,
                             const uint8_t **line, size_t *length)
{
   if (!cursor || !line || !length || cursor->at >= cursor->size) return false;
   size_t start = cursor->at;
   while (cursor->at < cursor->size && cursor->data[cursor->at] != '\n')
      ++cursor->at;
   if (cursor->at >= cursor->size) return false;
   *line = cursor->data + start;
   *length = cursor->at - start;
   ++cursor->at;
   return true;
}

static int arc_binding_hex_nibble(uint8_t value)
{
   if (value >= '0' && value <= '9') return value - '0';
   if (value >= 'a' && value <= 'f') return value - 'a' + 10;
   if (value >= 'A' && value <= 'F') return value - 'A' + 10;
   return -1;
}

static bool arc_binding_fingerprint(const uint8_t *line, size_t length,
                                    char output[41])
{
   if (!line || length != 40u) return false;
   for (size_t i = 0u; i < length; ++i)
      if (arc_binding_hex_nibble(line[i]) < 0) return false;
   memcpy(output, line, length);
   output[length] = '\0';
   return true;
}

bool arc_launch_binding_parse(const void *data, size_t size,
                              ArcLaunchBinding *binding)
{
   if (!data || !binding || !size || size > ARC_LAUNCH_BINDING_MAX_SIZE)
      return false;
   ArcBindingCursor cursor = {(const uint8_t *)data, size, 0u};
   const uint8_t *first, *second, *third, *fourth = NULL;
   size_t first_length, second_length, third_length, fourth_length = 0u;
   if (!arc_binding_line(&cursor, &first, &first_length) ||
       !arc_binding_line(&cursor, &second, &second_length) ||
       !arc_binding_line(&cursor, &third, &third_length)) return false;

   ArcLaunchBinding parsed;
   memset(&parsed, 0, sizeof(parsed));
   if (first_length == 2u && !memcmp(first, "v2", 2u))
   {
      if (!arc_binding_line(&cursor, &fourth, &fourth_length) ||
          cursor.at != cursor.size || second_length != 1u ||
          second[0] < '1' || second[0] > '8' ||
          !third_length || third_length > 2u * ARC_GUEST_NAME_MAX ||
          (third_length & 1u) ||
          !arc_binding_fingerprint(fourth, fourth_length,
                                   parsed.candidate_fingerprint))
         return false;
      parsed.version = 2u;
      parsed.ordinal = (unsigned)(second[0] - '1');
      parsed.target.length = (uint8_t)(third_length / 2u);
      for (unsigned i = 0u; i < parsed.target.length; ++i)
      {
         int high = arc_binding_hex_nibble(third[i * 2u]);
         int low = arc_binding_hex_nibble(third[i * 2u + 1u]);
         if (high < 0 || low < 0) return false;
         parsed.target.bytes[i] = (uint8_t)((unsigned)high << 4u | (unsigned)low);
      }
   }
   else
   {
      if (cursor.at != cursor.size || first_length != 4u ||
          memcmp(first, "app", 3u) || first[3] < '1' || first[3] > '8' ||
          !second_length || second_length > ARC_GUEST_NAME_MAX ||
          !arc_binding_fingerprint(third, third_length,
                                   parsed.candidate_fingerprint))
         return false;
      parsed.version = 1u;
      parsed.ordinal = (unsigned)(first[3] - '1');
      parsed.target.length = (uint8_t)second_length;
      memcpy(parsed.target.bytes, second, second_length);
   }
   if (!parsed.target.length || parsed.target.bytes[0] != '!') return false;
   *binding = parsed;
   return true;
}

bool arc_launch_binding_format(unsigned ordinal, const ArcGuestName *target,
                               const char candidate_fingerprint[41],
                               uint8_t *output, size_t capacity,
                               size_t *output_size)
{
   static const char hex[] = "0123456789abcdef";
   if (output_size) *output_size = 0u;
   if (ordinal >= 8u || !target || !target->length ||
       target->length > ARC_GUEST_NAME_MAX || target->bytes[0] != '!' ||
       !candidate_fingerprint || candidate_fingerprint[40] != '\0') return false;
   for (unsigned i = 0u; i < 40u; ++i)
      if (arc_binding_hex_nibble((uint8_t)candidate_fingerprint[i]) < 0)
         return false;
   size_t required = 2u + 1u + 1u + 1u +
                     (size_t)target->length * 2u + 1u + 40u + 1u;
   if (output_size) *output_size = required;
   if (!output) return capacity == 0u;
   if (capacity < required) return false;
   size_t at = 0u;
   output[at++] = 'v'; output[at++] = '2'; output[at++] = '\n';
   output[at++] = (uint8_t)('1' + ordinal); output[at++] = '\n';
   for (unsigned i = 0u; i < target->length; ++i)
   {
      output[at++] = (uint8_t)hex[target->bytes[i] >> 4u];
      output[at++] = (uint8_t)hex[target->bytes[i] & 15u];
   }
   output[at++] = '\n';
   memcpy(output + at, candidate_fingerprint, 40u); at += 40u;
   output[at++] = '\n';
   return at == required;
}
