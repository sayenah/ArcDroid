/* ArcDroid deterministic drive-1 launch disc. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <stdio.h>
#include <stdlib.h>
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

static uint8_t arc_ascii_fold(uint8_t c)
{
   return c >= 'A' && c <= 'Z' ? (uint8_t)(c - 'A' + 'a') : c;
}

static bool arc_name_literal(const ArcGuestName *name, const char *literal)
{
   size_t length = strlen(literal);
   if (!name || name->length != length) return false;
   for (size_t i = 0u; i < length; ++i)
      if (arc_ascii_fold(name->bytes[i]) !=
          arc_ascii_fold((uint8_t)literal[i])) return false;
   return true;
}

static bool arc_leaf_representable(const ArcGuestName *name)
{
   if (!name || name->length < 2u || name->length > ARC_GUEST_NAME_MAX ||
       name->bytes[0] != '!') return false;
   for (unsigned i = 1u; i < name->length; ++i)
   {
      uint8_t c = name->bytes[i];
      if (c <= 0x20u || c == 0x7fu || c == '.' || c == ':' || c == '*' ||
          c == '#' || c == '$' || c == '&' || c == '@' || c == '^' ||
          c == '%' || c == '\\' || c == '|' || c == '"' || c == '<')
         return false;
   }
   return true;
}

typedef struct {
   uint8_t *output;
   size_t capacity;
   size_t size;
   bool ok;
} ArcScriptWriter;

static void arc_script_bytes(ArcScriptWriter *writer, const void *data,
                             size_t size)
{
   if (!writer->ok || size > SIZE_MAX - writer->size)
   { writer->ok = false; return; }
   if (writer->output)
   {
      if (writer->size > writer->capacity ||
          size > writer->capacity - writer->size)
      { writer->ok = false; return; }
      memcpy(writer->output + writer->size, data, size);
   }
   writer->size += size;
}

static void arc_script_text(ArcScriptWriter *writer, const char *text)
{
   arc_script_bytes(writer, text, strlen(text));
}

static void arc_script_leaf(ArcScriptWriter *writer, const ArcGuestName *name)
{
   arc_script_bytes(writer, name->bytes, name->length);
}

static void arc_script_path(ArcScriptWriter *writer, const ArcGuestName *name)
{
   arc_script_text(writer, "ADFS::0.$.");
   arc_script_leaf(writer, name);
}

static void arc_script_boot(ArcScriptWriter *writer,
                            const ArcLaunchCandidate *candidate)
{
   arc_script_text(writer, "%Filer_Boot ");
   arc_script_path(writer, &candidate->name);
   arc_script_text(writer, "\r");
}

static void arc_launch_error(char *error, size_t error_size,
                             const char *message)
{
   if (error && error_size) snprintf(error, error_size, "%s", message);
}

bool arc_launch_script_build(const ArcLaunchDiscovery *discovery,
                             unsigned target_index, uint8_t *output,
                             size_t capacity, size_t *output_size,
                             char *error, size_t error_size)
{
   if (output_size) *output_size = 0u;
   if (!discovery || target_index >= discovery->candidate_count)
   { arc_launch_error(error, error_size, "launch target does not exist"); return false; }
   const ArcLaunchCandidate *target = &discovery->candidates[target_index];
   if (target->state != ARC_LAUNCH_CANDIDATE_APPLICATION || !target->launchable ||
       !arc_leaf_representable(&target->name))
   { arc_launch_error(error, error_size, "launch target leaf is not representable"); return false; }

   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
      if (i != target_index && discovery->candidates[i].has_boot &&
          (!discovery->candidates[i].launchable ||
           !arc_leaf_representable(&discovery->candidates[i].name)))
      { arc_launch_error(error, error_size, "sibling boot leaf is not representable"); return false; }

   ArcScriptWriter writer = {output, capacity, 0u, true};
   arc_script_text(&writer, "| ArcDroid desktop boot file\r");
   static const char *const preferred[] = {"!System", "!Fonts", "!Scrap"};
   for (unsigned p = 0u; p < sizeof(preferred) / sizeof(preferred[0]); ++p)
      for (unsigned i = 0u; i < discovery->candidate_count; ++i)
         if (i != target_index && discovery->candidates[i].has_boot &&
             arc_name_literal(&discovery->candidates[i].name, preferred[p]))
            arc_script_boot(&writer, &discovery->candidates[i]);
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
   {
      const ArcLaunchCandidate *candidate = &discovery->candidates[i];
      if (i == target_index || !candidate->has_boot ||
          arc_name_literal(&candidate->name, "!System") ||
          arc_name_literal(&candidate->name, "!Fonts") ||
          arc_name_literal(&candidate->name, "!Scrap")) continue;
      arc_script_boot(&writer, candidate);
   }
   arc_script_boot(&writer, target);
   arc_script_text(&writer, "%Dir ADFS::0.$\r");
   arc_script_text(&writer, "%URD ADFS::0.$\r");
   arc_script_text(&writer, "%Filer_Run ");
   arc_script_path(&writer, &target->name);
   arc_script_text(&writer, "\r");
   if (!writer.ok)
   { arc_launch_error(error, error_size, "desktop boot file buffer is too small"); return false; }
   if (output_size) *output_size = writer.size;
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
   /* RISC OS 3.11 will not run a typed file whose exec field equals load. */
   memset(entry + 14u, 0, 4u);
   entry[18] = (uint8_t)length;
   entry[19] = (uint8_t)(length >> 8u);
   entry[20] = (uint8_t)(length >> 16u);
   entry[21] = (uint8_t)(length >> 24u);
   arc_store_u24(entry + 22u, sector);
   entry[25] = 0u;
}

bool arc_launch_disc_build(const ArcLaunchDiscovery *discovery,
                           unsigned target_index,
                           uint8_t output[ARC_LAUNCH_DISC_SIZE],
                           char *error, size_t error_size)
{
   static const char boot[] =
      "| ArcDroid launch disc\r"
      "%Desktop -File \"ADFS::1.$.ArcLaunch\"\r";
   size_t launch_size = 0u;
   if (!output || !arc_launch_script_build(discovery, target_index, NULL, 0u,
                                            &launch_size, error, error_size))
      return false;
   size_t launch_sectors = (launch_size + ARC_L_SECTOR_SIZE - 1u) /
                           ARC_L_SECTOR_SIZE;
   if (!launch_sectors || launch_sectors > ARC_L_TOTAL_SECTORS - ARC_L_LAUNCH_SECTOR)
   { arc_launch_error(error, error_size, "desktop boot file does not fit helper disc"); return false; }
   uint8_t *launch = malloc(launch_size);
   if (!launch)
   { arc_launch_error(error, error_size, "desktop boot file allocation failed"); return false; }
   size_t written = 0u;
   if (!arc_launch_script_build(discovery, target_index, launch, launch_size,
                                &written, error, error_size) ||
       written != launch_size)
   { free(launch); return false; }

   memset(output, 0, ARC_LAUNCH_DISC_SIZE);
   /* Free extents surround the boot sector and the exact launch-file extent. */
   arc_store_u24(output, 7u);
   arc_store_u24(output + 3u, ARC_L_LAUNCH_SECTOR + (unsigned)launch_sectors);
   memcpy(output + 247u, "Acri\r", 5u); /* ArcDroid, interleaved halves. */
   arc_store_u24(output + 252u, ARC_L_TOTAL_SECTORS);
   arc_store_u24(output + 256u, ARC_L_BOOT_SECTOR - 7u);
   arc_store_u24(output + 259u,
                 ARC_L_TOTAL_SECTORS - ARC_L_LAUNCH_SECTOR -
                 (unsigned)launch_sectors);
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
                 launch_size, ARC_L_LAUNCH_SECTOR);
   uint8_t *tail = root + 1227u;
   tail[0] = 0u;
   memset(tail + 1u, '\r', 10u);
   tail[1] = '$';
   arc_store_u24(tail + 11u, ARC_L_ROOT_SECTOR);
   memcpy(tail + 14u, "ArcDroid launch", 15u);
   tail[47] = 1u;
   memcpy(tail + 48u, "Hugo", 4u);
   tail[52] = 0u; /* FileCore's accepted old-directory sentinel. */

   bool ok = arc_l_write(output, ARC_L_BOOT_SECTOR,
                         (const uint8_t *)boot, sizeof(boot) - 1u) &&
             arc_l_write(output, ARC_L_LAUNCH_SECTOR, launch, launch_size);
   free(launch);
   if (!ok) arc_launch_error(error, error_size, "helper disc write failed");
   return ok;
}
