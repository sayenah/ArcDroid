/* ArcDroid deterministic core tests. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "cmos_defaults_data.h"
#include "e_format_fixture.h"
#include "folder_set.h"
#include "jfd.h"
#include "keytable_provenance.h"
#include "osk_font.h"
#include "playlist.h"
#include "libretro.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_fixture(const char *directory, const char *name, size_t *size)
{
   char path[4096];
   int written = snprintf(path, sizeof(path), "%s/%s", directory, name);
   assert(written > 0 && (size_t)written < sizeof(path));
   FILE *file = fopen(path, "rb");
   assert(file && fseek(file, 0, SEEK_END) == 0);
   long length = ftell(file);
   assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
   uint8_t *data = malloc((size_t)length ? (size_t)length : 1u);
   assert(data && fread(data, 1u, (size_t)length, file) == (size_t)length);
   assert(fclose(file) == 0);
   *size = (size_t)length;
   return data;
}

static void test_hashes(void)
{
   ArcDigest d = arc_digest("abc", 3u);
   assert(d.crc32 == UINT32_C(0x352441c2));
   assert(strcmp(d.sha1, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
   uint64_t start = UINT64_MAX - (UINT64_C(3) << 32);
   uint64_t end = start + (UINT64_C(9) << 32);
   assert(arc_timestamp_elapsed_cycles(start, end) == 9u);
}

static void test_cmos_unplug_policy(void)
{
   static const unsigned locked[] = {6u, 7u, 18u, 19u, 173u, 174u,
                                     175u, 176u, 217u, 218u, 219u};
   assert(arc_cmos_raw_address(6u) == 0x46u);
   assert(arc_cmos_raw_address(18u) == 0x52u);
   assert(arc_cmos_raw_address(173u) == 0xedu);
   assert(arc_cmos_raw_address(176u) == 0xf0u);
   assert(arc_cmos_raw_address(217u) == 0x29u);
   assert(arc_cmos_raw_address(219u) == 0x2bu);
   assert(arc_cmos_raw_address(239u) == 0x3fu);
   for (unsigned logical = 0u; logical < 240u; ++logical)
      assert(arc_cmos_logical_address(arc_cmos_raw_address(logical)) == logical);
   for (ArcOsClass os = ARC_OS_UNKNOWN; os <= ARC_OS_ARTHUR_030; ++os)
   {
      uint8_t mask[32], original[ARC_CMOS_PAYLOAD_SIZE], payload[ARC_CMOS_PAYLOAD_SIZE];
      bool protects = os == ARC_OS_TEST || (os >= ARC_OS_RISCOS_311 && os <= ARC_OS_RISCOS_300);
      arc_cmos_locked_raw_mask(os, mask);
      for (unsigned raw = 0u; raw < 256u; ++raw)
      {
         bool expected = false;
         for (unsigned i = 0u; i < sizeof(locked) / sizeof(locked[0]); ++i)
            if (protects && raw == arc_cmos_raw_address(locked[i])) expected = true;
         assert(((mask[raw >> 3u] >> (raw & 7u)) & 1u) == expected);
      }
      for (unsigned i = 0u; i < sizeof(payload); ++i)
         original[i] = payload[i] = (uint8_t)((i * 13u + 1u) & 0xffu);
      unsigned expected_cleared = 0u;
      for (unsigned i = 0u; i < sizeof(locked) / sizeof(locked[0]); ++i)
         if (protects && original[arc_cmos_raw_address(locked[i]) - 16u]) ++expected_cleared;
      assert(arc_cmos_clear_unplug_bytes(payload, os) == expected_cleared);
      for (unsigned i = 0u; i < sizeof(payload); ++i)
      {
         unsigned raw = i + 16u;
         bool locked_raw = (mask[raw >> 3u] & (1u << (raw & 7u))) != 0u;
         assert(payload[i] == (locked_raw ? 0u : original[i]));
      }
      if (os == ARC_OS_TEST || (os >= ARC_OS_RISCOS_311 && os <= ARC_OS_ARTHUR_030))
      {
         uint8_t image[ARC_CMOS_MACHINE_SIZE];
         assert(arc_cmos_machine_image(os, ARC_IO_OLD_WD1770, original, false, image));
         for (unsigned i = 0u; i < sizeof(locked) / sizeof(locked[0]); ++i)
            if (protects) assert(image[arc_cmos_raw_address(locked[i])] == 0u);
         unsigned checksum = 0u;
         for (unsigned i = 0u; i < 239u; ++i) checksum += image[arc_cmos_raw_address(i)];
         assert(image[0x3fu] == (uint8_t)(checksum + 1u));
      }
   }

   /* A persisted record can carry unplug bits alongside a preexisting IDE
    * count. Exercise the complete seed-to-machine construction order. */
   uint8_t poisoned[ARC_CMOS_PAYLOAD_SIZE];
   memset(poisoned, 0, sizeof(poisoned));
   for (unsigned i = 0u; i < sizeof(locked) / sizeof(locked[0]); ++i)
      poisoned[arc_cmos_raw_address(locked[i]) - 16u] = 0x96u;
   poisoned[199u - 16u] = 0x81u;
   poisoned[arc_cmos_raw_address(189u) - 16u] = 0x0au;
   arc_cmos_normalise_payload(poisoned, ARC_OS_RISCOS_311, ARC_IO_OLD_WD1770);
   assert(poisoned[arc_cmos_raw_address(189u) - 16u] == 0u);
   uint8_t attached[ARC_CMOS_MACHINE_SIZE];
   assert(arc_cmos_machine_image(ARC_OS_RISCOS_311, ARC_IO_OLD_WD1770,
                                 poisoned, false, attached));
   arc_cmos_machine_attach_ide(attached);
   assert(attached[arc_cmos_raw_address(189u)] == 0x0au);
   for (unsigned i = 0u; i < sizeof(locked) / sizeof(locked[0]); ++i)
   {
      unsigned raw = arc_cmos_raw_address(locked[i]);
      assert(poisoned[raw - 16u] == 0u && attached[raw] == 0u);
   }
   assert((attached[199u] & 0xc0u) == 0x40u);
   assert(!arc_machine_descriptor(ARC_MACHINE_A3010)->ide_capable &&
          arc_machine_descriptor(ARC_MACHINE_A3020)->ide_capable &&
          !arc_machine_descriptor(ARC_MACHINE_A4000)->ide_capable);
   unsigned attached_sum = 0u;
   for (unsigned i = 0u; i < 239u; ++i)
      attached_sum += attached[arc_cmos_raw_address(i)];
   assert(attached[0x3fu] == (uint8_t)(attached_sum + 1u));
}

static void test_fresh_cmos_default(void)
{
   static const uint8_t arthur[ARC_CMOS_MACHINE_SIZE] = {ARC_CMOS_ARTHUR_BYTES};
   static const uint8_t riscos2[ARC_CMOS_MACHINE_SIZE] = {ARC_CMOS_RISCOS2_BYTES};
   static const uint8_t riscos3_old[ARC_CMOS_MACHINE_SIZE] = {ARC_CMOS_RISCOS3_OLD_BYTES};
   static const uint8_t riscos3_new[ARC_CMOS_MACHINE_SIZE] = {ARC_CMOS_RISCOS3_NEW_BYTES};
   static const ArcOsClass unchanged[] = {
      ARC_OS_RISCOS_300, ARC_OS_RISCOS_200, ARC_OS_RISCOS_201,
      ARC_OS_ARTHUR_120, ARC_OS_ARTHUR_030
   };
   static const ArcOsClass corrected[] = {
      ARC_OS_RISCOS_310, ARC_OS_RISCOS_311, ARC_OS_RISCOS_319
   };
   const unsigned offset = arc_cmos_raw_address(195u) - 16u;
   assert(offset == 3u);
   for (ArcIoClass io = ARC_IO_OLD_WD1770; io <= ARC_IO_NEW_82C711; ++io)
   {
      const uint8_t *riscos3 = io == ARC_IO_NEW_82C711 ? riscos3_new : riscos3_old;
      uint8_t payload[ARC_CMOS_PAYLOAD_SIZE], expected[ARC_CMOS_PAYLOAD_SIZE];
      for (unsigned i = 0u; i < sizeof(unchanged) / sizeof(unchanged[0]); ++i)
      {
         ArcOsClass os = unchanged[i];
         const uint8_t *source = os == ARC_OS_RISCOS_300 ? riscos3 :
            os == ARC_OS_RISCOS_200 || os == ARC_OS_RISCOS_201 ? riscos2 : arthur;
         assert(arc_cmos_default(os, io, payload));
         assert(memcmp(payload, source + 16u, sizeof(payload)) == 0);
      }
      for (unsigned i = 0u; i < sizeof(corrected) / sizeof(corrected[0]); ++i)
      {
         assert(arc_cmos_default(corrected[i], io, payload));
         memcpy(expected, riscos3 + 16u, sizeof(expected));
         assert(expected[offset] & 0x04u);
         expected[offset] &= (uint8_t)~0x04u;
         assert(memcmp(payload, expected, sizeof(payload)) == 0);
      }
      payload[offset] |= 0x04u;
      ArcLaunchPlan plan;
      char error[160] = {0};
      const char id[] = "0123456789012345678901234567890123456789";
      assert(arc_launch_plan_default(&plan, ARC_MEDIA_ADF_800K,
             ARC_OS_RISCOS_311, id, false, error, sizeof(error)));
      uint8_t record[ARC_CMOS_RECORD_SIZE], decoded[ARC_CMOS_PAYLOAD_SIZE];
      assert(arc_cmos_encode(record, &plan, payload));
      assert(arc_cmos_decode(record, &plan, decoded));
      arc_cmos_normalise_payload(decoded, ARC_OS_RISCOS_311, io);
      assert(decoded[offset] & 0x04u);
   }
}

static void test_boot_stamp_starting_cmos(void)
{
   static const ArcOsClass risc_os_3[] = {
      ARC_OS_RISCOS_300, ARC_OS_RISCOS_310,
      ARC_OS_RISCOS_311, ARC_OS_RISCOS_319
   };
   for (unsigned i = 0u; i < sizeof(risc_os_3) / sizeof(risc_os_3[0]); ++i)
   {
      ArcOsClass os = risc_os_3[i];
      for (ArcIoClass io = ARC_IO_OLD_WD1770; io <= ARC_IO_NEW_82C711; ++io)
      {
         uint8_t fresh[ARC_CMOS_PAYLOAD_SIZE];
         uint8_t persisted[ARC_CMOS_PAYLOAD_SIZE];
         uint8_t starting[ARC_CMOS_PAYLOAD_SIZE];
         uint8_t repeated[ARC_CMOS_PAYLOAD_SIZE];
         uint8_t image[ARC_CMOS_MACHINE_SIZE];
         char first_stamp[41], second_stamp[41], changed_stamp[41];
         assert(arc_cmos_default(os, io, fresh));
         assert(arc_cmos_machine_image(os, io, fresh, false, image));
         memcpy(persisted, image + 16u, sizeof(persisted));
         assert(!(fresh[80u - 16u] & 0x10u));
         assert(!(persisted[80u - 16u] & 0x10u));
         assert(arc_cmos_boot_stamp_payload(os, io, fresh, false, starting));
         assert(arc_cmos_boot_stamp_payload(os, io, persisted, false, repeated));
         arc_sha1_hex(starting, sizeof(starting), first_stamp);
         arc_sha1_hex(repeated, sizeof(repeated), second_stamp);
         assert(strcmp(first_stamp, second_stamp) == 0);
         assert(!(starting[80u - 16u] & 0x10u));
         assert(starting[arc_cmos_raw_address(239u) - 16u] == 0u);
         assert(arc_cmos_boot_stamp_payload(os, io, fresh, true, repeated));
         assert(memcmp(starting, repeated, sizeof(starting)) == 0);

         persisted[arc_cmos_raw_address(100u) - 16u] ^= 0x01u;
         assert(arc_cmos_boot_stamp_payload(os, io, persisted, false, repeated));
         arc_sha1_hex(repeated, sizeof(repeated), changed_stamp);
         assert(strcmp(first_stamp, changed_stamp) != 0);
      }
   }
}

static void test_firmware_table(void)
{
   size_t count = 0u;
   const ArcFirmwareFingerprint *table = arc_firmware_table(&count);
   assert(table && count >= 40u);
   ArcDigest known = {2097152u, UINT32_C(0x54c0c963), "3487729e87bebc9cb51665838c27beff22f7b3bd"};
   assert(arc_firmware_match(&known)->os == ARC_OS_RISCOS_311);
   known.size++;
   assert(arc_firmware_match(&known) == NULL);
   size_t nearest = 0u;
   assert(arc_firmware_nearest_size(2097151u, 4096u, &nearest) && nearest == 2097152u);
   assert(arc_firmware_nearest_size(2097153u, 4096u, &nearest) && nearest == 2097152u);
   assert(!arc_firmware_nearest_size(42u, 4096u, &nearest));
   unsigned test_priority = arc_firmware_auto_priority(ARC_OS_TEST);
   assert(test_priority < arc_firmware_auto_priority(ARC_OS_UNKNOWN));
   for (ArcOsClass os = ARC_OS_RISCOS_311; os <= ARC_OS_ARTHUR_030; ++os)
      assert(arc_firmware_auto_priority(os) < test_priority);

   /* Exercise every catalogue row without redistributing firmware. For each
    * OS, derive synthetic lane files from one deterministic combined image,
    * assemble them through the catalogue recipe, and compare recomputed
    * fingerprints with the independently digested combined image. */
   bool *visited = calloc(count, sizeof(*visited));
   assert(visited);
   for (size_t combined_index = 0u; combined_index < count; ++combined_index)
   {
      const ArcFirmwareFingerprint *combined = &table[combined_index];
      if (combined->recipe != ARC_RECIPE_COMBINED) continue;
      visited[combined_index] = true;
      uint8_t *image = malloc(combined->size);
      uint8_t *assembled = malloc(combined->size);
      assert(image && assembled);
      for (size_t i = 0u; i < combined->size; ++i)
         image[i] = (uint8_t)((i * 73u) ^ (i >> 5u) ^ (unsigned)combined->os);
      ArcDigest expected = arc_digest(image, combined->size);
      ArcDigest recomputed = arc_digest(image, combined->size);
      assert(expected.size == recomputed.size && expected.crc32 == recomputed.crc32 &&
             strcmp(expected.sha1, recomputed.sha1) == 0);

      for (ArcFirmwareRecipe recipe = ARC_RECIPE_BYTE_LANES_4;
           recipe <= ARC_RECIPE_HALFWORD_LANES_2; ++recipe)
      {
         unsigned lane_count = recipe == ARC_RECIPE_BYTE_LANES_4 ? 4u : 2u;
         const uint8_t *parts[4] = {NULL, NULL, NULL, NULL};
         uint8_t *owned[4] = {NULL, NULL, NULL, NULL};
         size_t sizes[4] = {0u, 0u, 0u, 0u};
         unsigned found = 0u;
         for (size_t entry_index = 0u; entry_index < count; ++entry_index)
         {
            const ArcFirmwareFingerprint *entry = &table[entry_index];
            if (entry->os != combined->os || entry->recipe != recipe) continue;
            assert(entry->lane < lane_count && !parts[entry->lane]);
            assert(entry->size * lane_count == combined->size);
            owned[entry->lane] = malloc(entry->size);
            assert(owned[entry->lane]);
            parts[entry->lane] = owned[entry->lane];
            sizes[entry->lane] = entry->size;
            visited[entry_index] = true;
            ++found;
         }
         if (!found) continue;
         assert(found == lane_count);
         size_t lane_size = combined->size / lane_count;
         if (recipe == ARC_RECIPE_BYTE_LANES_4)
            for (size_t i = 0u; i < lane_size; ++i)
               for (unsigned lane = 0u; lane < lane_count; ++lane)
                  owned[lane][i] = image[i * 4u + lane];
         else
            for (size_t i = 0u; i < lane_size; i += 2u)
               for (unsigned lane = 0u; lane < lane_count; ++lane)
               {
                  owned[lane][i] = image[(i / 2u) * 4u + lane * 2u];
                  owned[lane][i + 1u] = image[(i / 2u) * 4u + lane * 2u + 1u];
               }
         char assembly_error[96] = {0};
         assert(arc_firmware_assemble(parts, sizes, lane_count, recipe,
                                      assembled, combined->size,
                                      assembly_error, sizeof(assembly_error)));
         recomputed = arc_digest(assembled, combined->size);
         assert(expected.size == recomputed.size && expected.crc32 == recomputed.crc32 &&
                strcmp(expected.sha1, recomputed.sha1) == 0);
         for (unsigned lane = 0u; lane < lane_count; ++lane) free(owned[lane]);
      }
      free(assembled);
      free(image);
   }
   for (size_t i = 0u; i < count; ++i) assert(visited[i]);
   free(visited);
}

static void test_assembly(void)
{
   const uint8_t a[] = {0, 4}, b[] = {1, 5}, c[] = {2, 6}, d[] = {3, 7};
   const uint8_t *parts[] = {a, b, c, d};
   const size_t sizes[] = {2, 2, 2, 2};
   uint8_t output[8] = {0};
   char error[80] = {0};
   assert(arc_firmware_assemble(parts, sizes, 4u, ARC_RECIPE_BYTE_LANES_4,
                                output, sizeof(output), error, sizeof(error)));
   for (unsigned i = 0; i < 8u; ++i) assert(output[i] == i);

   const uint8_t lo[] = {0, 1, 4, 5}, hi[] = {2, 3, 6, 7};
   const uint8_t *halves[] = {lo, hi};
   const size_t half_sizes[] = {4, 4};
   memset(output, 0, sizeof(output));
   assert(arc_firmware_assemble(halves, half_sizes, 2u, ARC_RECIPE_HALFWORD_LANES_2,
                                output, sizeof(output), error, sizeof(error)));
   for (unsigned i = 0; i < 8u; ++i) assert(output[i] == i);
}

static void test_media_launch_and_cmos(void)
{
   char error[160] = {0};
   uint8_t *raw = calloc(1u, 1638400u);
   ArcMediaInfo media;
   assert(raw);
   assert(arc_media_inspect("renamed.AdF", raw, 819200u, &media, error, sizeof(error)) && media.kind == ARC_MEDIA_ADF_800K);
   assert(arc_media_inspect("disc.adl", raw, 655360u, &media, error, sizeof(error)) && media.kind == ARC_MEDIA_ADL_640K);
   assert(arc_media_inspect("size-first.adl", raw, 819200u, &media, error, sizeof(error)) && media.kind == ARC_MEDIA_ADF_800K);
   assert(arc_media_inspect("size-first.adf", raw, 655360u, &media, error, sizeof(error)) && media.kind == ARC_MEDIA_ADL_640K);
   assert(arc_media_inspect("disc.adf", raw, 1638400u, &media, error, sizeof(error)));
   assert(media.geometry == ARC_GEOMETRY_1600K_F &&
          media.kind == ARC_MEDIA_ADF_1600K &&
          media.recognition == ARC_MEDIA_RECOGNITION_SIZE_FALLBACK);
   assert(!arc_media_inspect("disc.adf", raw, 42u, &media, error, sizeof(error)));
   free(raw);

   ArcLaunchPlan plan;
   const char id[] = "0123456789012345678901234567890123456789";
   assert(arc_launch_plan_default(&plan, ARC_MEDIA_ADF_800K, ARC_OS_RISCOS_311,
                                  id, false, error, sizeof(error)));
   assert(plan.machine == ARC_MACHINE_A3000 && plan.cpu == ARC_CPU_ARM2);
   assert(!arc_launch_plan_default(&plan, ARC_MEDIA_ADF_800K, ARC_OS_RISCOS_311,
                                   id, true, error, sizeof(error)));
   assert(strstr(error, "incompatible"));
   for (ArcMachine machine = ARC_MACHINE_A3000; machine < ARC_MACHINE_COUNT; ++machine)
   {
      const ArcMachineDescriptor *d = arc_machine_descriptor(machine);
      assert(d && arc_machine_from_name(d->name) == d && d->memory_kib == 4096u);
      assert(arc_launch_plan_resolve(&plan, ARC_MEDIA_ADF_800K,
             machine == ARC_MACHINE_A3000 ? ARC_OS_RISCOS_300 : ARC_OS_RISCOS_310,
             id, d->name, arc_cpu_name(d->cpu), false, error, sizeof(error)));
      assert(plan.machine == machine && plan.cpu == d->cpu);
   }
   assert(!arc_launch_plan_resolve(&plan, ARC_MEDIA_ADF_800K, ARC_OS_RISCOS_300,
                                   id, "A3010", "ARM250", false, error, sizeof(error)));
   assert(strstr(error, "not supported on the A3010"));
   assert(!arc_launch_plan_resolve(&plan, ARC_MEDIA_ADF_800K, ARC_OS_RISCOS_311,
                                   id, "A3010", "ARM2", false, error, sizeof(error)));
   assert(strstr(error, "ARM2 is incompatible with the A3010"));

   assert(arc_launch_plan_default(&plan, ARC_MEDIA_ADF_800K, ARC_OS_RISCOS_311,
                                  id, false, error, sizeof(error)));
   uint8_t payload[ARC_CMOS_PAYLOAD_SIZE], restored[ARC_CMOS_PAYLOAD_SIZE];
   uint8_t record[ARC_CMOS_RECORD_SIZE];
   for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)(i ^ 0x5a);
   assert(arc_cmos_encode(record, &plan, payload));
   assert(arc_cmos_decode(record, &plan, restored));
   assert(memcmp(payload, restored, sizeof(payload)) == 0);
   record[12] ^= 1u;
   assert(!arc_cmos_decode(record, &plan, restored));
   assert(arc_cmos_default(ARC_OS_RISCOS_311, ARC_IO_OLD_WD1770, payload));
   assert(payload[0] || payload[1] || payload[2]);
   assert(arc_cmos_default(ARC_OS_TEST, ARC_IO_OLD_WD1770, payload));
   for (unsigned i = 0; i < sizeof(payload); ++i) assert(payload[i] == 0u);
   uint8_t machine_cmos[ARC_CMOS_MACHINE_SIZE];
   assert(arc_cmos_default(ARC_OS_RISCOS_311, ARC_IO_OLD_WD1770, payload));
   assert(arc_cmos_machine_image(ARC_OS_RISCOS_311, ARC_IO_OLD_WD1770, payload, false, machine_cmos));
   assert(machine_cmos[69] == 8u && !(machine_cmos[75] & 7u));
   assert(!(machine_cmos[80] & 0x10u) && (machine_cmos[199] & 7u) == 1u);
   assert(arc_cmos_machine_image(ARC_OS_RISCOS_311, ARC_IO_OLD_WD1770, payload, true, machine_cmos));
   assert((machine_cmos[75] & 7u) == 1u && (machine_cmos[199] & 7u) == 2u);
   payload[42] = 0x5au;
   payload[59] = 0xffu;
   payload[183] = 0xfau;
   arc_cmos_normalise_payload(payload, ARC_OS_RISCOS_311, ARC_IO_OLD_WD1770);
   assert(payload[42] == 0x5au && (payload[59] & 7u) == 0u &&
          (payload[183] & 7u) == 1u);
   static const uint8_t persisted_drive_counts[] = {0x41u, 0x81u, 0xc9u};
   for (unsigned i = 0u; i < sizeof(persisted_drive_counts); ++i)
   {
      payload[183] = persisted_drive_counts[i];
      arc_cmos_normalise_payload(payload, ARC_OS_RISCOS_311, ARC_IO_NEW_82C711);
      assert(payload[183] == 1u);
   }
   assert(arc_cmos_default(ARC_OS_RISCOS_311, ARC_IO_NEW_82C711, payload));
   assert(arc_cmos_machine_image(ARC_OS_RISCOS_311, ARC_IO_NEW_82C711,
                                 payload, false, machine_cmos));
   assert(machine_cmos[199] == 1u);
   unsigned checksum = 0u;
   for (unsigned i = 0; i < 239u; ++i)
   {
      unsigned raw = i + 0x40u;
      if (raw > 255u) raw -= 240u;
      checksum += machine_cmos[raw];
   }
   assert(machine_cmos[63] == (uint8_t)(checksum + 1u));
   arc_cmos_machine_attach_ide(machine_cmos);
   assert((machine_cmos[199] & 0xc0u) == 0x40u);
   checksum = 0u;
   for (unsigned i = 0u; i < 239u; ++i)
      checksum += machine_cmos[arc_cmos_raw_address(i)];
   assert(machine_cmos[63] == (uint8_t)(checksum + 1u));

   ArcLaunchPlan other_machine = plan;
   other_machine.machine = ARC_MACHINE_A3010;
   assert(arc_cmos_encode(record, &plan, payload));
   assert(!arc_cmos_decode(record, &other_machine, restored));
}

static size_t test_l_offset(unsigned logical_sector)
{
   unsigned track = logical_sector / 16u;
   return (size_t)(((track % 80u) * 2u + track / 80u) * 16u +
                   logical_sector % 16u) * 256u;
}

static uint32_t test_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
          ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static uint32_t test_le24(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
          ((uint32_t)p[2] << 16u);
}

static uint8_t test_old_map_checksum(const uint8_t *block)
{
   unsigned checksum = 0xffu;
   for (unsigned index = 255u; index-- > 0u;)
   {
      if (checksum > 0xffu) checksum = (checksum + 1u) & 0xffu;
      checksum += block[index];
   }
   return (uint8_t)checksum;
}

static void test_short_d_free(uint8_t *image, const uint8_t base[512],
                              unsigned first, unsigned count,
                              unsigned second, unsigned second_count)
{
   memcpy(image, base, 512u);
   image[510] = second_count ? 6u : 3u;
   image[0] = (uint8_t)first; image[1] = (uint8_t)(first >> 8u);
   image[2] = (uint8_t)(first >> 16u);
   image[256] = (uint8_t)count; image[257] = (uint8_t)(count >> 8u);
   image[258] = (uint8_t)(count >> 16u);
   if (second_count)
   {
      image[3] = (uint8_t)second; image[4] = (uint8_t)(second >> 8u);
      image[5] = (uint8_t)(second >> 16u);
      image[259] = (uint8_t)second_count;
      image[260] = (uint8_t)(second_count >> 8u);
      image[261] = (uint8_t)(second_count >> 16u);
   }
   image[255] = test_old_map_checksum(image);
   image[511] = test_old_map_checksum(image + 256u);
}

static void test_short_e_bit(uint8_t *block, unsigned bit, unsigned value)
{
   uint8_t mask = (uint8_t)(1u << (bit & 7u));
   if (value) block[bit >> 3u] |= mask;
   else block[bit >> 3u] &= (uint8_t)~mask;
}

static void test_short_e_bits(uint8_t *block, unsigned start,
                              unsigned count, unsigned value)
{
   for (unsigned bit = 0u; bit < count; ++bit)
      test_short_e_bit(block, start + bit, (value >> bit) & 1u);
}

static void test_short_e_free(uint8_t *image, const uint8_t base[1024],
                              unsigned first, unsigned split)
{
   uint8_t block[1024];
   memcpy(block, base, sizeof(block));
   for (unsigned bit = 512u; bit <= 6911u; ++bit)
      test_short_e_bit(block, bit, 0u);
   test_short_e_bits(block, 8u, 15u, first - 8u);
   test_short_e_bits(block, 512u, 15u, 2u);
   test_short_e_bit(block, first - 1u, 1u);
   if (split)
   {
      test_short_e_bits(block, first, 15u, split - first);
      test_short_e_bit(block, split - 1u, 1u);
   }
   test_short_e_bit(block, 6911u, 1u);
   block[0] = arc_test_e_format_zone_check(block);
   memcpy(image, block, sizeof(block));
   memcpy(image + 1024u, block, sizeof(block));
}

static uint32_t test_ror32(uint32_t value, unsigned shift)
{
   return (value >> shift) | (value << (32u - shift));
}

static size_t test_new_name_length(const uint8_t name[10])
{
   size_t length = 0u;
   while (length < 10u && name[length] >= 0x20u) ++length;
   return length;
}

static uint8_t test_ascii_fold(uint8_t value)
{
   return value >= 'A' && value <= 'Z' ?
      (uint8_t)(value - 'A' + 'a') : value;
}

static int test_adfs_name_order(const uint8_t a[10], const uint8_t b[10])
{
   size_t a_length = test_new_name_length(a);
   size_t b_length = test_new_name_length(b);
   size_t common = a_length < b_length ? a_length : b_length;
   for (size_t i = 0u; i < common; ++i)
   {
      uint8_t folded_a = test_ascii_fold(a[i]);
      uint8_t folded_b = test_ascii_fold(b[i]);
      if (folded_a != folded_b) return folded_a < folded_b ? -1 : 1;
   }
   if (a_length == b_length) return 0;
   return a_length < b_length ? -1 : 1;
}

static uint8_t test_new_directory_checksum(const uint8_t directory[2048],
                                           unsigned entries)
{
   size_t used_end = 5u + (size_t)entries * 26u;
   size_t word_end = used_end & ~(size_t)3u;
   uint32_t check = 0u;
   for (size_t offset = 0u; offset < word_end; offset += 4u)
      check = test_le32(directory + offset) ^ test_ror32(check, 13u);
   for (size_t offset = word_end; offset < used_end; ++offset)
      check = directory[offset] ^ test_ror32(check, 13u);
   for (size_t offset = 2008u; offset < 2044u; offset += 4u)
      check = test_le32(directory + offset) ^ test_ror32(check, 13u);
   return (uint8_t)(check ^ (check >> 8u) ^
                    (check >> 16u) ^ (check >> 24u));
}

static unsigned test_validate_new_directory(const uint8_t directory[2048],
                                            uint32_t parent)
{
   assert(directory[0] == directory[2042] &&
          (!memcmp(directory + 1u, "Hugo", 4u) ||
           !memcmp(directory + 1u, "Nick", 4u)) &&
          !memcmp(directory + 1u, directory + 2043u, 4u) &&
          !directory[2007] && !directory[2008] && !directory[2009] &&
          test_le24(directory + 2010u) == parent);
   unsigned entries = 0u;
   while (entries < 77u && directory[5u + entries * 26u]) ++entries;
   assert(entries < 77u);
   for (unsigned i = 1u; i < entries; ++i)
      assert(test_adfs_name_order(directory + 5u + (i - 1u) * 26u,
                                  directory + 5u + i * 26u) < 0);
   assert(directory[2047] ==
          test_new_directory_checksum(directory, entries));
   return entries;
}

static void test_mark_extent(bool *sectors, unsigned total,
                             unsigned start, unsigned length)
{
   assert(start <= total && length <= total - start);
   for (unsigned sector = start; sector < start + length; ++sector)
   {
      assert(!sectors[sector]);
      sectors[sector] = true;
   }
}

static void test_validate_checkpoint_d_image(const uint8_t *image, size_t size,
                                             unsigned expected_root_entries)
{
   const unsigned units_per_sector = 1024u / 256u;
   assert(size == 819200u && test_le24(image + 252u) == 3200u &&
          image[255] == test_old_map_checksum(image) &&
          image[511] == test_old_map_checksum(image + 256u) &&
          4u % units_per_sector == 0u);
   unsigned total = test_le24(image + 252u);
   bool *allocated = calloc(total, sizeof(*allocated));
   bool *free_map = calloc(total, sizeof(*free_map));
   assert(allocated && free_map);
   test_mark_extent(allocated, total, 0u, 4u);
   const uint8_t *root = image + 1024u;
   unsigned root_entries = test_validate_new_directory(root, 4u);
   assert(root_entries == expected_root_entries);
   test_mark_extent(allocated, total, 4u, 8u);
   for (unsigned i = 0u; i < root_entries; ++i)
   {
      const uint8_t *root_entry = root + 5u + i * 26u;
      unsigned child_start = test_le24(root_entry + 22u);
      unsigned child_sectors = (test_le32(root_entry + 18u) + 255u) / 256u;
      assert((root_entry[25] & 8u) && child_sectors == 8u &&
             child_start % units_per_sector == 0u);
      test_mark_extent(allocated, total, child_start, child_sectors);
      const uint8_t *child = image + (size_t)child_start * 256u;
      unsigned child_entries = test_validate_new_directory(child, 4u);
      for (unsigned j = 0u; j < child_entries; ++j)
      {
         const uint8_t *entry = child + 5u + j * 26u;
         unsigned file_start = test_le24(entry + 22u);
         unsigned file_length = test_le32(entry + 18u);
         unsigned file_sectors =
            ((file_length + 1023u) / 1024u) * units_per_sector;
         assert(!(entry[25] & 8u) && file_sectors &&
                file_start % units_per_sector == 0u);
         test_mark_extent(allocated, total, file_start, file_sectors);
      }
   }
   assert(image[510] && image[510] % 3u == 0u);
   unsigned previous_end = 0u;
   for (unsigned offset = 0u; offset < image[510]; offset += 3u)
   {
      unsigned start = test_le24(image + offset);
      unsigned length = test_le24(image + 256u + offset);
      assert(length && start >= previous_end && start <= total &&
             length <= total - start);
      for (unsigned sector = start; sector < start + length; ++sector)
      {
         assert(!free_map[sector]);
         free_map[sector] = true;
      }
      previous_end = start + length;
   }
   for (unsigned sector = 0u; sector < total; ++sector)
      assert(allocated[sector] != free_map[sector]);
   free(free_map);
   free(allocated);
}

static ArcGuestName test_guest_name(const void *bytes, size_t length)
{
   ArcGuestName name = {{0}, 0};
   assert(length <= ARC_GUEST_NAME_MAX);
   memcpy(name.bytes, bytes, length);
   name.length = (uint8_t)length;
   return name;
}

static bool test_guest_name_equal(const ArcGuestName *name, const char *text)
{
   size_t length = strlen(text);
   return name->length == length && !memcmp(name->bytes, text, length);
}

static ArcLaunchDiscovery test_launch_spec(const void *target, size_t length)
{
   ArcLaunchDiscovery discovery;
   memset(&discovery, 0, sizeof(discovery));
   discovery.readable = true;
   discovery.candidate_count = 1u;
   discovery.application_count = 1u;
   discovery.target_index = 0;
   discovery.candidates[0].name = test_guest_name(target, length);
   discovery.candidates[0].state = ARC_LAUNCH_CANDIDATE_APPLICATION;
   discovery.candidates[0].launchable = true;
   return discovery;
}

static void test_launch_bindings(void)
{
   static const char fingerprint[] =
      "0123456789abcdef0123456789abcdef01234567";
   static const uint8_t expected[] =
      "v2\n3\n214e696da0566f78\n"
      "0123456789abcdef0123456789abcdef01234567\n";
   static const uint8_t legacy[] =
      "app3\n!Nim\xa0" "Vox\n"
      "0123456789abcdef0123456789abcdef01234567\n";
   static const uint8_t raw_name[] =
      {'!', 'N', 'i', 'm', 0xa0u, 'V', 'o', 'x'};
   assert(sizeof(fingerprint) == 41u);
   ArcGuestName target = test_guest_name(raw_name, sizeof(raw_name));
   uint8_t encoded[ARC_LAUNCH_BINDING_MAX_SIZE];
   size_t encoded_size = 0u;
   assert(arc_launch_binding_format(2u, &target, fingerprint, NULL, 0u,
                                    &encoded_size) &&
          encoded_size == sizeof(expected) - 1u &&
          !arc_launch_binding_format(2u, &target, fingerprint, encoded,
                                     encoded_size - 1u, &encoded_size) &&
          arc_launch_binding_format(2u, &target, fingerprint, encoded,
                                    sizeof(encoded), &encoded_size) &&
          !memcmp(encoded, expected, sizeof(expected) - 1u));

   ArcLaunchBinding binding;
   assert(arc_launch_binding_parse(encoded, encoded_size, &binding) &&
          binding.version == 2u && binding.ordinal == 2u &&
          binding.target.length == sizeof(raw_name) &&
          !memcmp(binding.target.bytes, raw_name, sizeof(raw_name)) &&
          !strcmp(binding.candidate_fingerprint, fingerprint));
   assert(arc_launch_binding_parse(legacy, sizeof(legacy) - 1u, &binding) &&
          binding.version == 1u && binding.ordinal == 2u &&
          binding.target.length == sizeof(raw_name) &&
          !memcmp(binding.target.bytes, raw_name, sizeof(raw_name)) &&
          !strcmp(binding.candidate_fingerprint, fingerprint));

   static const char *const malformed[] = {
      "", "v2\n", "v2\n0\n2141\n0123456789abcdef0123456789abcdef01234567\n",
      "v2\n1\n214\n0123456789abcdef0123456789abcdef01234567\n",
      "v2\n1\n21xz\n0123456789abcdef0123456789abcdef01234567\n",
      "v2\n1\n2141\n0123456789abcdef0123456789abcdef0123456z\n",
      "v2\n1\n2141\n0123456789abcdef0123456789abcdef01234567\ntrailing",
      "app1\n!A\n0123456789abcdef0123456789abcdef01234567",
      "app1\n!A\n0123456789abcdef0123456789abcdef01234567\ntrailing",
      "app9\n!A\n0123456789abcdef0123456789abcdef01234567\n",
   };
   for (size_t i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i)
      assert(!arc_launch_binding_parse(malformed[i], strlen(malformed[i]),
                                       &binding));
   uint8_t oversized[ARC_LAUNCH_BINDING_MAX_SIZE + 1u] = {0};
   assert(!arc_launch_binding_parse(oversized, sizeof(oversized), &binding));

   /* Mutate every bit in the canonical record. Accepted mutations must still
    * round-trip through the bounded formatter; rejected ones exercise each
    * strict parser boundary under the sanitizer configuration. */
   for (size_t byte = 0u; byte < sizeof(expected) - 1u; ++byte)
      for (unsigned bit = 0u; bit < 8u; ++bit)
      {
         memcpy(encoded, expected, sizeof(expected) - 1u);
         encoded[byte] ^= (uint8_t)(1u << bit);
         if (arc_launch_binding_parse(encoded, sizeof(expected) - 1u, &binding))
         {
            size_t round_trip_size = 0u;
            assert(binding.version == 2u && binding.ordinal < 8u &&
                   binding.target.length &&
                   binding.target.length <= ARC_GUEST_NAME_MAX &&
                   arc_launch_binding_format(binding.ordinal, &binding.target,
                      binding.candidate_fingerprint, NULL, 0u,
                      &round_trip_size) &&
                   round_trip_size <= sizeof(encoded));
         }
      }
}

static uint64_t test_pixels_hash(const uint32_t *pixels, size_t count)
{
   uint64_t hash = UINT64_C(1469598103934665603);
   const uint8_t *bytes = (const uint8_t *)pixels;
   for (size_t i = 0u; i < count * sizeof(*pixels); ++i)
   {
      hash ^= bytes[i];
      hash *= UINT64_C(1099511628211);
   }
   return hash;
}

static void test_chooser_row_text(const uint32_t *pixels, unsigned x,
                                  unsigned y, const char *label)
{
   for (unsigned character = 0u; label[character]; ++character)
      for (unsigned row = 0u; row < 8u; ++row)
         for (unsigned bit = 0u; bit < 8u; ++bit)
         {
            bool lit = (arc_osk_font[(uint8_t)label[character]][row] >>
                        (7u - bit)) & 1u;
            uint32_t expected = lit ? UINT32_C(0x00e8edf5) :
                                      UINT32_C(0x00101a2a);
            assert(pixels[(size_t)(y + row * 2u) * 640u +
                          x + character * 16u + bit * 2u] == expected);
         }
}

static void test_boot_hit_chooser(void)
{
   ArcLaunchDiscovery discovery = {0};
   discovery.reason = ARC_LAUNCH_REASON_AMBIGUOUS;
   discovery.candidate_count = discovery.application_count = 1u;
   discovery.unknown_count = 1u;
   discovery.candidates[0].name = test_guest_name("!A", 2u);
   discovery.candidates[0].state = ARC_LAUNCH_CANDIDATE_APPLICATION;
   discovery.candidates[0].launchable = true;
   ArcLaunchChooser chooser;
   uint32_t *pixels = calloc(640u * 480u, sizeof(*pixels));
   assert(pixels);
   assert(arc_launch_chooser_init(&chooser, &discovery, false, INT_MIN));
   assert(!chooser.inspection_incomplete && !chooser.boot_hit);
   assert(arc_launch_chooser_render(&chooser, &discovery, pixels,
                                   640u, 480u, 640u * sizeof(*pixels)));
   test_chooser_row_text(pixels, 104u, 112u, "Desktop (no launch)");

   arc_launch_chooser_boot_hit(&chooser, &discovery);
   assert(chooser.boot_hit && chooser.inspection_incomplete);
   assert(arc_launch_chooser_render(&chooser, &discovery, pixels,
                                   640u, 480u, 640u * sizeof(*pixels)));
   test_chooser_row_text(pixels, 104u, 112u, "Run disc's own startup");
   discovery.unknown_count = 0u;
   discovery.scan_incomplete = true;
   assert(arc_launch_chooser_init(&chooser, &discovery, false, INT_MIN));
   assert(!chooser.inspection_incomplete);
   arc_launch_chooser_boot_hit(&chooser, &discovery);
   assert(chooser.inspection_incomplete);
   free(pixels);
}

static void test_launch_choices_and_chooser(void)
{
   static const char fingerprint[] =
      "0123456789abcdef0123456789abcdef01234567";
   static const uint8_t expected[] =
      "v1\napp\n214e696da0566f78\n"
      "0123456789abcdef0123456789abcdef01234567\n";
   static const uint8_t desktop[] =
      "v1\ndesktop\n-\n"
      "0123456789abcdef0123456789abcdef01234567\n";
   static const uint8_t raw_name[] =
      {'!', 'N', 'i', 'm', 0xa0u, 'V', 'o', 'x'};
   ArcGuestName target = test_guest_name(raw_name, sizeof(raw_name));
   uint8_t encoded[ARC_LAUNCH_CHOICE_MAX_SIZE];
   size_t encoded_size = 0u;
   assert(arc_launch_choice_format(&target, false, fingerprint, NULL, 0u,
                                   &encoded_size) &&
          encoded_size == sizeof(expected) - 1u &&
          arc_launch_choice_format(&target, false, fingerprint, encoded,
                                   sizeof(encoded), &encoded_size) &&
          !memcmp(encoded, expected, encoded_size));
   ArcLaunchChoiceRecord choice;
   assert(arc_launch_choice_parse(encoded, encoded_size, &choice) &&
          !choice.desktop && choice.target.length == sizeof(raw_name) &&
          !memcmp(choice.target.bytes, raw_name, sizeof(raw_name)) &&
          !strcmp(choice.candidate_fingerprint, fingerprint));
   assert(arc_launch_choice_format(NULL, true, fingerprint, encoded,
                                   sizeof(encoded), &encoded_size) &&
          encoded_size == sizeof(desktop) - 1u &&
          !memcmp(encoded, desktop, encoded_size) &&
          arc_launch_choice_parse(encoded, encoded_size, &choice) &&
          choice.desktop && !choice.target.length);
   static const char *const malformed[] = {
      "", "v2\ndesktop\n-\n0123456789abcdef0123456789abcdef01234567\n",
      "v2\ninstall\nF00123\nrev=1\nvariant=second\nset=0123456789abcdef0123456789abcdef01234567\n",
      "v1\napp\n21A0\n0123456789abcdef0123456789abcdef01234567\n",
      "v1\napp\n214\n0123456789abcdef0123456789abcdef01234567\n",
      "v1\ndesktop\n21\n0123456789abcdef0123456789abcdef01234567\n",
      "v1\napp\n2141\n0123456789abcdef0123456789abcdef0123456g\n",
      "v1\napp\n2141\n0123456789abcdef0123456789abcdef01234567\nX"
   };
   for (size_t i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i)
      assert(!arc_launch_choice_parse(malformed[i], strlen(malformed[i]),
                                      &choice));
   uint8_t oversized[ARC_LAUNCH_CHOICE_MAX_SIZE + 1u] = {0};
   assert(!arc_launch_choice_parse(oversized, sizeof(oversized), &choice));
   for (size_t byte = 0u; byte < sizeof(expected) - 1u; ++byte)
      for (unsigned bit = 0u; bit < 8u; ++bit)
      {
         memcpy(encoded, expected, sizeof(expected) - 1u);
         encoded[byte] ^= (uint8_t)(1u << bit);
         if (arc_launch_choice_parse(encoded, sizeof(expected) - 1u, &choice))
         {
            size_t round_trip = 0u;
            assert(arc_launch_choice_format(&choice.target, choice.desktop,
                     choice.candidate_fingerprint, NULL, 0u, &round_trip) &&
                   round_trip <= sizeof(encoded));
         }
      }

   for (unsigned c = 0x21u; c <= 0x7eu; ++c)
   {
      bool nonblank = false;
      for (unsigned row = 0u; row < 8u; ++row)
         nonblank |= arc_osk_font[c][row] != 0u;
      assert(nonblank);
   }
   for (unsigned c = 'a'; c <= 'z'; ++c)
      assert(memcmp(arc_osk_font[c], arc_osk_font[c - 'a' + 'A'], 8u));

   ArcLaunchDiscovery discovery;
   memset(&discovery, 0, sizeof(discovery));
   discovery.reason = ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE;
   memcpy(discovery.candidate_fingerprint, fingerprint, sizeof(fingerprint));
   discovery.candidate_count = ARC_LAUNCH_MAX_CANDIDATES;
   discovery.application_count = ARC_LAUNCH_MAX_CANDIDATES;
   for (unsigned i = 0u; i < discovery.candidate_count; ++i)
   {
      char name[11];
      int length = snprintf(name, sizeof(name), "!app%02u", i);
      assert(length == 6);
      discovery.candidates[i].name = test_guest_name(name, (size_t)length);
      discovery.candidates[i].state = ARC_LAUNCH_CANDIDATE_APPLICATION;
      discovery.candidates[i].launchable = true;
   }
   ArcLaunchChooser chooser;
   assert(arc_launch_chooser_init(&chooser, &discovery, true, 76) &&
          chooser.row_count == 78u && chooser.focus == 76u &&
          chooser.first_visible > 0u &&
          arc_launch_chooser_selection(&chooser) == 76);
   for (unsigned i = 0u; i < chooser.row_count; ++i)
   {
      assert(arc_launch_chooser_focus(&chooser, i) &&
             arc_launch_chooser_selection(&chooser) ==
                (i == 77u ? -1 : (int)i));
   }
   arc_launch_chooser_move(&chooser, 1);
   assert(chooser.focus == 0u && chooser.first_visible == 0u);
   assert(arc_launch_chooser_hit(&chooser, 64u, 92u) == 0 &&
          arc_launch_chooser_hit(&chooser, 63u, 92u) < 0);

   uint32_t *pixels = calloc(640u * 480u, sizeof(*pixels));
   assert(pixels && arc_launch_chooser_render(&chooser, &discovery, pixels,
          640u, 480u, 640u * sizeof(*pixels)));
   uint64_t long_list_hash = test_pixels_hash(pixels, 640u * 480u);
   assert(long_list_hash != 0u);

   static const uint8_t distinct_names[][7] = {
      {'!', 'A', 'l', 'p', 'h', 'a', 0},
      {'!', 'A', 'l', 'p', 'h', 'A', 0},
      {'!', 0xa0u, 0, 0, 0, 0, 0},
      {'!', '\\', 'x', 'A', '0', 0, 0}
   };
   uint64_t hashes[4];
   for (unsigned i = 0u; i < 4u; ++i)
   {
      memset(&discovery, 0, sizeof(discovery));
      discovery.reason = ARC_LAUNCH_REASON_AMBIGUOUS;
      discovery.candidate_count = discovery.application_count = 1u;
      size_t length = 1u;
      while (length < sizeof(distinct_names[i]) && distinct_names[i][length])
         ++length;
      discovery.candidates[0].name =
         test_guest_name(distinct_names[i], length);
      discovery.candidates[0].state = ARC_LAUNCH_CANDIDATE_APPLICATION;
      discovery.candidates[0].launchable = true;
      assert(arc_launch_chooser_init(&chooser, &discovery, false, INT_MIN) &&
             arc_launch_chooser_render(&chooser, &discovery, pixels,
                640u, 480u, 640u * sizeof(*pixels)));
      hashes[i] = test_pixels_hash(pixels, 640u * 480u);
      for (unsigned previous = 0u; previous < i; ++previous)
         assert(hashes[i] != hashes[previous]);
   }
   static const uint8_t escaped_tail_names[2][10] = {
      {'!', 0x80u, 0x81u, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u, 0xa0u},
      {'!', 0x80u, 0x81u, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u, 0xa1u}
   };
   uint64_t escaped_tail_hashes[2];
   for (unsigned i = 0u; i < 2u; ++i)
   {
      discovery.candidates[0].name =
         test_guest_name(escaped_tail_names[i], sizeof(escaped_tail_names[i]));
      assert(arc_launch_chooser_init(&chooser, &discovery, false, INT_MIN) &&
             arc_launch_chooser_render(&chooser, &discovery, pixels,
                640u, 480u, 640u * sizeof(*pixels)));
      escaped_tail_hashes[i] = test_pixels_hash(pixels, 640u * 480u);
   }
   assert(escaped_tail_hashes[0] != escaped_tail_hashes[1]);

   /* A normal maximum-length name uses the same 2x glyph cells as its row
    * number. Only a name whose escaped form cannot fit falls back to 1x; the
    * final hex glyph proves that even the longest such name is not clipped. */
   static const uint8_t typical_name[10] =
      {'!', 'C', 'l', 'o', 'u', 'd', 'v', 'a', 'l', 'e'};
   discovery.candidates[0].name =
      test_guest_name(typical_name, sizeof(typical_name));
   assert(arc_launch_chooser_init(&chooser, &discovery, false, INT_MIN) &&
          arc_launch_chooser_focus(&chooser, 1u) &&
          arc_launch_chooser_render(&chooser, &discovery, pixels,
             640u, 480u, 640u * sizeof(*pixels)));
   bool typical_wide = false, typical_tall = false;
   for (unsigned y = 92u; y < 108u; ++y)
      for (unsigned x = 104u; x < 264u; ++x)
      {
         if (x >= 184u && pixels[(size_t)y * 640u + x] == UINT32_C(0x00e8edf5))
            typical_wide = true;
         if (y >= 100u && pixels[(size_t)y * 640u + x] == UINT32_C(0x00e8edf5))
            typical_tall = true;
      }
   assert(typical_wide && typical_tall);

   static const uint8_t maximal_escaped_name[10] =
      {'!', 0x80u, 0x81u, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u, 0x88u};
   discovery.candidates[0].name =
      test_guest_name(maximal_escaped_name, sizeof(maximal_escaped_name));
   assert(arc_launch_chooser_init(&chooser, &discovery, false, INT_MIN) &&
          arc_launch_chooser_focus(&chooser, 1u) &&
          arc_launch_chooser_render(&chooser, &discovery, pixels,
             640u, 480u, 640u * sizeof(*pixels)));
   bool escaped_tail_visible = false;
   for (unsigned y = 92u; y < 100u; ++y)
      for (unsigned x = 392u; x < 400u; ++x)
         escaped_tail_visible |=
            pixels[(size_t)y * 640u + x] == UINT32_C(0x00ffd060);
   assert(escaped_tail_visible);

   choice.desktop = false;
   choice.target = discovery.candidates[0].name;
   memset(choice.candidate_fingerprint, '0', 40u);
   choice.candidate_fingerprint[40] = '\0';
   assert(arc_launch_choice_match(&discovery, &choice) == 0);
   discovery.candidates[0].resource = true;
   assert(arc_launch_choice_match(&discovery, &choice) == INT_MIN);
   choice.desktop = true;
   assert(arc_launch_choice_match(&discovery, &choice) == -1);
   free(pixels);
}

static void test_launch_discovery_and_builder(const char *fixture_directory)
{
   typedef struct {
      const char *file;
      ArcMediaGeometry geometry;
      size_t root_offset;
      const char *signature;
      const char *disc_name;
      unsigned boot;
      unsigned candidates;
      unsigned applications;
      int target;
      ArcLaunchReason reason;
      bool readable;
   } ExpectedLaunch;
   /* This table is intentionally literal: offsets, signatures and expected
    * decisions are not obtained from the parser under test. */
   static const ExpectedLaunch expected[] = {
      {"launch-family/single-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/single-d-hugo.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/single-d-3180.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/single-d-nick.adf", ARC_GEOMETRY_800K_D, 1024u, "Nick", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/single-e-hugo.adf", ARC_GEOMETRY_800K_E, 2048u, "Hugo", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/single-e-nick.adf", ARC_GEOMETRY_800K_E, 2048u, "Nick", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/late-target-e.adf", ARC_GEOMETRY_800K_E, 2048u, "Hugo", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/stale-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/stale-e.adf", ARC_GEOMETRY_800K_E, 2048u, "Hugo", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/short-name-attr-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "A", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/top-bit-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Zorvex", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/hard-space-e.adf", ARC_GEOMETRY_800K_E, 2048u, "Hugo", "NimVox", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/full-ten-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Fixture", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/attr-88-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Zorvex", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/irrelevant-extent-e.adf", ARC_GEOMETRY_800K_E, 2048u, "Hugo", "Zorvex", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/multiple-exact-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Beta", 0u, 2u, 2u, 1, ARC_LAUNCH_REASON_DISC_NAME_MATCH, true},
      {"launch-family/multiple-trailing-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Beta2", 0u, 2u, 2u, 1, ARC_LAUNCH_REASON_DISC_NAME_MATCH, true},
      {"launch-family/ambiguous-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Other", 0u, 3u, 2u, -1, ARC_LAUNCH_REASON_AMBIGUOUS, true},
      {"launch-family/chooser-resources-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Other", 0u, 4u, 1u, 3, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/chooser-resource-pair-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Other", 0u, 5u, 2u, -1, ARC_LAUNCH_REASON_AMBIGUOUS, true},
      {"launch-family/chooser-incomplete-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Other", 0u, 2u, 1u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/no-run-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Fixture", 0u, 2u, 0u, -1, ARC_LAUNCH_REASON_NO_APPLICATIONS, true},
      {"launch-family/incomplete-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Fixture", 0u, 1u, 0u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/incomplete-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Fixture", 0u, 1u, 0u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/incomplete-e.adf", ARC_GEOMETRY_800K_E, 2048u, "Hugo", "Fixture", 0u, 1u, 0u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/label-with-unknown-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Zorvex", 0u, 2u, 1u, 0, ARC_LAUNCH_REASON_DISC_NAME_MATCH, true},
      {"launch-family/label-is-unknown-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Nulvane", 0u, 2u, 1u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/duplicate-root-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Qarvox", 0u, 2u, 0u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/duplicate-root-file-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Qarvox", 0u, 1u, 0u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/duplicate-run-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Zorvex", 0u, 1u, 0u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/duplicate-boot-e.adf", ARC_GEOMETRY_800K_E, 2048u, "Hugo", "Zorvex", 0u, 1u, 0u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/has-boot-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Zorvex", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/self-boot-unknown-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Broken", 2u, 1u, 0u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/more-eight-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Fixture", 0u, 10u, 1u, 9, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/fragmented-e.adf", ARC_GEOMETRY_800K_E, 2048u, "Hugo", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/unresolved-e.adf", ARC_GEOMETRY_800K_E, 2048u, "Hugo", "Demo", 0u, 1u, 0u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/illegal-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Fixture", 0u, 18u, 18u, -1, ARC_LAUNCH_REASON_AMBIGUOUS, true},
      {"launch-family/boot-0-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/boot-1-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Demo", 1u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/boot-2-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Demo", 2u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/boot-3-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Demo", 3u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/sibling-pipe-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Qaventra", 0u, 2u, 1u, 1, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/duplicate-siblings-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Qaventra", 0u, 3u, 1u, 2, ARC_LAUNCH_REASON_DISC_NAME_MATCH, true},
   };
   size_t size = 0u;
   uint8_t *application = read_fixture(fixture_directory,
                                       "arcdroid-application-l.adl", &size);
   assert(!strcmp(arc_digest(application, size).sha1,
                  "ee4706d07234df5c83816fdc03e59efda34a4c40"));
   ArcMediaInfo media;
   ArcLaunchDiscovery discovery;
   char error[192] = {0};
   assert(arc_media_inspect("application.adl", application, size,
                            &media, error, sizeof(error)));
   assert(media.geometry == ARC_GEOMETRY_640K_L && media.boot_option == 0u &&
          !strcmp(media.disc_name, "Demo"));
   assert(arc_launch_discover(application, size, &media, NULL, &discovery));
   assert(discovery.readable && discovery.candidate_count == 1u &&
          discovery.application_count == 1u && discovery.target_index == 0 &&
          test_guest_name_equal(&discovery.candidates[0].name, "!Demo"));
   const uint8_t *application_root = application + test_l_offset(2u);
   const uint8_t *application_child = application + test_l_offset(20u);
   static const uint8_t encoded_demo[] = {0xa1u, 0xc4u, 'e', 0xedu, 'o'};
   static const uint8_t encoded_run[] = {0xa1u, 0xd2u, 'u', 'n'};
   static const uint8_t encoded_run_image[] = {
      0xa1u, 0xd2u, 'u', 'n', 'I', 'm', 'a', 'g', 'e'
   };
   assert(!memcmp(application_root + 5u, encoded_demo, sizeof(encoded_demo)));
   assert(!memcmp(application_child + 5u, encoded_run, sizeof(encoded_run)));
   assert(!memcmp(application_child + 31u, encoded_run_image,
                  sizeof(encoded_run_image)));
   assert(test_le32(application_root + 5u + 10u) == UINT32_C(0xfff00000) &&
          test_le32(application_root + 5u + 14u) == 0u);
   assert(test_le32(application_child + 5u + 10u) == UINT32_C(0xffffeb00) &&
          test_le32(application_child + 5u + 14u) == 0u);
   assert(test_le32(application_child + 31u + 10u) == UINT32_C(0x00008000) &&
          test_le32(application_child + 31u + 14u) == UINT32_C(0x00008000));
   assert(application_child[31u + 18u] == 0u &&
          application_child[31u + 19u] == 20u &&
          application_child[31u + 22u] == 0xf6u &&
          application_child[31u + 23u] == 0x04u);
   assert(memcmp(application + test_l_offset(1279u),
                 application + test_l_offset(1280u), 256u));
   free(application);

   for (size_t index = 0u; index < sizeof(expected) / sizeof(expected[0]); ++index)
   {
      const ExpectedLaunch *item = &expected[index];
      uint8_t *bytes = read_fixture(fixture_directory, item->file, &size);
      assert(size == (item->geometry == ARC_GEOMETRY_640K_L ? 655360u : 819200u));
      assert(!memcmp(bytes + item->root_offset + 1u, item->signature, 4u));
      const uint8_t *first_entry = bytes + item->root_offset + 5u;
      assert((test_le32(first_entry + 10u) >> 20u) == 0xfffu &&
             test_le32(first_entry + 14u) == 0u);
      assert(arc_media_inspect(item->file, bytes, size, &media, error, sizeof(error)));
      if (media.geometry != item->geometry || media.boot_option != item->boot ||
          strcmp(media.disc_name, item->disc_name))
         fprintf(stderr, "launch fixture %s media geometry=%u boot=%u disc=%s\n",
                 item->file, media.geometry, media.boot_option, media.disc_name);
      assert(media.geometry == item->geometry && media.boot_option == item->boot &&
             !strcmp(media.disc_name, item->disc_name));
      assert(arc_launch_discover(bytes, size, &media, NULL, &discovery));
      if (discovery.readable != item->readable ||
          discovery.candidate_count != item->candidates ||
          discovery.application_count != item->applications ||
          discovery.target_index != item->target || discovery.reason != item->reason)
         fprintf(stderr, "launch fixture %s got readable=%u boot=%u candidates=%u apps=%u target=%d reason=%s\n",
                 item->file, discovery.readable, discovery.boot_option,
                 discovery.candidate_count, discovery.application_count,
                 discovery.target_index, arc_launch_reason_name(discovery.reason));
      assert(discovery.readable == item->readable &&
             discovery.boot_option == item->boot &&
             discovery.candidate_count == item->candidates &&
             discovery.application_count == item->applications &&
             discovery.target_index == item->target && discovery.reason == item->reason);
      if (!strcmp(item->file, "launch-family/single-l.adl"))
         assert(!strcmp(discovery.candidate_fingerprint,
                        "6db9e42a08bcd7aebad8b9e259466e268903b019"));
      if (strstr(item->file, "incomplete") || strstr(item->file, "unresolved") ||
          strstr(item->file, "duplicate-") || strstr(item->file, "label-is-unknown") ||
          strstr(item->file, "self-boot-unknown"))
         assert(discovery.unknown_count >= 1u);
      if (!strcmp(item->file, "launch-family/top-bit-d.adf"))
      {
         static const uint8_t expected_name[] = {'!', 'Z', 0xf6u, 'r', 'v', 'e', 'x'};
         assert(discovery.candidates[0].name.length == sizeof(expected_name) &&
                !memcmp(discovery.candidates[0].name.bytes, expected_name,
                        sizeof(expected_name)));
      }
      if (!strcmp(item->file, "launch-family/hard-space-e.adf"))
         assert(discovery.candidates[0].name.length == 8u &&
                discovery.candidates[0].name.bytes[4] == 0xa0u);
      if (!strcmp(item->file, "launch-family/full-ten-d.adf"))
         assert(discovery.candidates[0].name.length == 10u);
      if (!strcmp(item->file, "launch-family/attr-88-d.adf"))
         assert(discovery.ignored_attribute_bits);
      if (!strcmp(item->file, "launch-family/short-name-attr-l.adl"))
         assert(discovery.ignored_attribute_bits);
      if (!strcmp(item->file, "launch-family/has-boot-d.adf"))
         assert(discovery.candidates[0].has_boot);
      if (!strcmp(item->file, "launch-family/chooser-resources-d.adf"))
      {
         assert(discovery.candidates[0].resource &&
                discovery.candidates[1].resource &&
                discovery.candidates[2].resource &&
                !discovery.candidates[3].resource &&
                arc_launch_candidate_index(&discovery, 0u) == 3u &&
                arc_launch_candidate_index(&discovery, 1u) == UINT_MAX);
      }
      if (!strcmp(item->file, "launch-family/chooser-resource-pair-d.adf"))
      {
         ArcLaunchChooser resource_chooser;
         assert(discovery.candidates[0].resource &&
                discovery.candidates[1].resource &&
                discovery.candidates[2].resource &&
                !discovery.candidates[3].resource &&
                !discovery.candidates[4].resource &&
                arc_launch_chooser_init(&resource_chooser, &discovery,
                                        false, INT_MIN) &&
                resource_chooser.row_count == 3u &&
                resource_chooser.candidate_index[0] == 3u &&
                resource_chooser.candidate_index[1] == 4u &&
                resource_chooser.candidate_index[2] == UINT8_MAX &&
                arc_launch_chooser_selection(&resource_chooser) == 3 &&
                arc_launch_chooser_focus(&resource_chooser, 1u) &&
                arc_launch_chooser_selection(&resource_chooser) == 4 &&
                arc_launch_chooser_focus(&resource_chooser, 2u) &&
                arc_launch_chooser_selection(&resource_chooser) == -1);
      }
      if (!strcmp(item->file, "launch-family/illegal-l.adl"))
      {
         static const bool launchable[] = {
            false, false, false, true, false, false, false, false, false,
            false, false, false, false, false, false, true, false, true
         };
         assert(discovery.candidate_count == sizeof(launchable) / sizeof(launchable[0]));
         for (unsigned i = 0u; i < discovery.candidate_count; ++i)
            assert(discovery.candidates[i].launchable == launchable[i]);
      }
      if (strstr(item->file, "duplicate-") &&
          !strstr(item->file, "duplicate-siblings"))
         for (unsigned i = 0u; i < discovery.candidate_count; ++i)
            assert(discovery.candidates[i].state == ARC_LAUNCH_CANDIDATE_UNKNOWN &&
                   !discovery.candidates[i].launchable);
      free(bytes);
   }

   /* R-1: a boot-bearing sibling that cannot be addressed is omitted without
    * changing the valid target's byte-exact helper script. Duplicate sibling
    * leaves are likewise non-launchable and both omitted. */
   static const struct {
      const char *file;
      unsigned skipped;
   } r1_cases[] = {
      {"launch-family/sibling-pipe-l.adl", 1u},
      {"launch-family/duplicate-siblings-l.adl", 2u},
   };
   static const uint8_t r1_script[] =
      "| ArcDroid desktop boot file\r"
      "%Filer_Boot ADFS::0.$.!Qaventra\r"
      "%Dir ADFS::0.$\r"
      "%URD ADFS::0.$\r"
      "%Filer_Run ADFS::0.$.!Qaventra\r";
   for (size_t i = 0u; i < sizeof(r1_cases) / sizeof(r1_cases[0]); ++i)
   {
      uint8_t *bytes = read_fixture(fixture_directory, r1_cases[i].file, &size);
      assert(arc_media_inspect(r1_cases[i].file, bytes, size,
                               &media, error, sizeof(error)) &&
             arc_launch_discover(bytes, size, &media, NULL, &discovery) &&
             discovery.target_index >= 0 &&
             test_guest_name_equal(
                &discovery.candidates[discovery.target_index].name,
                "!Qaventra"));
      unsigned skipped = 0u;
      for (unsigned candidate = 0u;
           candidate < discovery.candidate_count; ++candidate)
         if ((int)candidate != discovery.target_index &&
             discovery.candidates[candidate].has_boot &&
             !arc_launch_sibling_bootable(&discovery.candidates[candidate]))
            ++skipped;
      assert(skipped == r1_cases[i].skipped);
      size_t r1_size = 0u;
      assert(arc_launch_script_build(&discovery,
                (unsigned)discovery.target_index, NULL, 0u, &r1_size,
                error, sizeof(error)) &&
             r1_size == sizeof(r1_script) - 1u);
      uint8_t *r1_output = malloc(r1_size);
      uint8_t *r1_helper = malloc(ARC_LAUNCH_DISC_SIZE);
      assert(r1_output && r1_helper &&
             arc_launch_script_build(&discovery,
                (unsigned)discovery.target_index, r1_output, r1_size,
                &r1_size, error, sizeof(error)) &&
             !memcmp(r1_output, r1_script, sizeof(r1_script) - 1u) &&
             arc_launch_disc_build(&discovery,
                (unsigned)discovery.target_index, r1_helper,
                error, sizeof(error)));
      const uint8_t *r1_root = r1_helper + test_l_offset(2u);
      assert(test_le32(r1_root + 31u + 18u) == sizeof(r1_script) - 1u &&
             !memcmp(r1_helper + test_l_offset(1280u), r1_script,
                     sizeof(r1_script) - 1u));
      free(r1_helper);
      free(r1_output);
      free(bytes);
   }

   /* Independent child offsets exercise typed !Run entries emitted by each
    * launch-family directory format, including the late E allocation. */
   static const struct { const char *file; size_t child_offset; } typed_children[] = {
      {"launch-family/single-l.adl", 2560u},
      {"launch-family/single-d-hugo.adf", 4096u},
      {"launch-family/single-e-hugo.adf", 4096u},
      {"launch-family/late-target-e.adf", 759808u},
   };
   for (size_t i = 0u; i < sizeof(typed_children) / sizeof(typed_children[0]); ++i)
   {
      uint8_t *bytes = read_fixture(fixture_directory, typed_children[i].file, &size);
      const uint8_t *run_entry = bytes + typed_children[i].child_offset + 5u;
      assert((test_le32(run_entry + 10u) >> 20u) == 0xfffu &&
             test_le32(run_entry + 14u) == 0u);
      free(bytes);
   }

   /* PRM single-zone layout: four bytes of zone header plus the 60-byte
    * disc record put allocation bit zero at map bit 512. The usable map has
    * 8192 - ZoneSpare - 480 bits, independent of the fixture builder. */
   uint8_t *late_e = read_fixture(fixture_directory,
                                  "launch-family/late-target-e.adf", &size);
   unsigned zone_spare = (unsigned)late_e[14] | ((unsigned)late_e[15] << 8u);
   unsigned allocation_start = 4u * 8u + 60u * 8u;
   unsigned allocation_bits = 8192u - zone_spare - 60u * 8u;
   unsigned allocation_end = allocation_start + allocation_bits;
   assert(zone_spare == 0x0520u && allocation_start == 512u &&
          allocation_bits == (819200u >> 7u) && allocation_end == 6912u);
   unsigned root_shared = 3u;
   size_t root_offset = (size_t)((root_shared - 1u) * (1024u >> 7u)) << 7u;
   unsigned late_child_bit = 6448u;
   unsigned late_run_bit = late_child_bit + 16u;
   size_t late_child_offset = (size_t)(late_child_bit - allocation_start) << 7u;
   size_t late_run_offset = (size_t)(late_run_bit - allocation_start) << 7u;
   assert(root_offset == 2048u && late_child_offset == 759808u &&
          late_run_offset == 761856u);
   assert(!memcmp(late_e + root_offset + 1u, "Hugo", 4u));
   assert(late_e[root_offset + 5u + 22u] == 0u &&
          late_e[root_offset + 5u + 23u] == 3u &&
          late_e[root_offset + 5u + 24u] == 0u);
   assert(!memcmp(late_e + late_child_offset + 1u, "Hugo", 4u));
   assert(late_e[late_run_offset] == '\r');
   assert(late_e[1] == 0x48u && late_e[2] == 0x19u);
   assert(!(late_e[6431u >> 3u] & (1u << (6431u & 7u))));
   assert(late_e[6911u >> 3u] & (1u << (6911u & 7u)));

   ArcMediaInfo e_media = {0};
   e_media.geometry = ARC_GEOMETRY_800K_E;
   late_e[14] = 0x1fu; /* one bounded excess allocation bit */
   late_e[6912u >> 3u] |= (uint8_t)(1u << (6912u & 7u));
   late_e[0] = 0u;
   late_e[0] = arc_test_e_format_zone_check(late_e);
   assert(arc_media_inspect("bounded-excess-e.adf", late_e, size,
                            &media, error, sizeof(error)) &&
          arc_launch_discover(late_e, size, &media, NULL, &discovery) &&
          discovery.readable && discovery.target_index == 0);
   late_e[14] = 0x21u; /* allocation bits no longer match disc size */
   late_e[0] = 0u;
   late_e[0] = arc_test_e_format_zone_check(late_e);
   assert(arc_launch_discover(late_e, size, &e_media, NULL, &discovery));
   assert(!discovery.readable && discovery.reason == ARC_LAUNCH_REASON_UNREADABLE);
   free(late_e);

   static const uint8_t stale_demo[10] = {
      '!', 'D', 'e', 'm', 'o', '\r', '\n', 0x01u, 0xffu, '\r'
   };
   static const uint8_t stale_run[10] = {
      '!', 'R', 'u', 'n', '\r', '\n', 0x01u, 0xffu, '\r', '\r'
   };
   static const char *stale_files[] = {
      "launch-family/stale-d.adf", "launch-family/stale-e.adf"
   };
   for (unsigned i = 0u; i < 2u; ++i)
   {
      uint8_t *stale = read_fixture(fixture_directory, stale_files[i], &size);
      size_t stale_root = i ? root_offset : 1024u;
      size_t stale_child = (size_t)(544u - allocation_start) << 7u;
      assert(stale_child == 4096u);
      assert(!memcmp(stale + stale_root + 5u, stale_demo, sizeof(stale_demo)));
      assert(!memcmp(stale + stale_child + 5u, stale_run, sizeof(stale_run)));
      free(stale);
   }

   /* A shortened D-format declaration constrains object reads even when the
    * physical image retains its conventional 800 KiB container. */
   uint8_t *short_d = read_fixture(fixture_directory,
                                   "launch-family/single-d-3180.adf", &size);
   assert(arc_media_inspect("single-d-3180.adf", short_d, size,
                            &media, error, sizeof(error)));
   short_d[1024u + 5u + 22u] = 0x6cu; /* sector 3180, outside the declaration */
   short_d[1024u + 5u + 23u] = 0x0cu;
   short_d[1024u + 5u + 24u] = 0u;
   assert(arc_launch_discover(short_d, size, &media, NULL, &discovery));
   assert(discovery.readable && discovery.candidate_count == 1u &&
          !discovery.application_count && discovery.unknown_count == 1u &&
          discovery.target_index < 0 &&
          discovery.reason == ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE);
   free(short_d);

   static const char *jfd_files[] = {
      "launch-family/single-l.jfd", "launch-family/single-l-extended.jfd",
      "launch-family/single-d-hugo.jfd", "launch-family/single-d-hugo-extended.jfd",
      "launch-family/single-e-hugo.jfd", "launch-family/single-e-hugo-extended.jfd",
      "launch-family/stale-d.jfd",
   };
   for (size_t index = 0u; index < sizeof(jfd_files) / sizeof(jfd_files[0]); ++index)
   {
      uint8_t *bytes = read_fixture(fixture_directory, jfd_files[index], &size);
      ArcJfdImage jfd;
      assert(arc_jfd_parse(bytes, size, &jfd, error, sizeof(error)));
      assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery));
      assert(discovery.readable && discovery.candidate_count == 1u &&
             discovery.application_count == 1u && discovery.target_index == 0 &&
             test_guest_name_equal(&discovery.candidates[0].name, "!Demo"));
      if (index == 3u)
      {
         /* Discovery reads the committed sector view, while Off hides it. */
         jfd.overlay = calloc(1u, sizeof(*jfd.overlay));
         assert(jfd.overlay);
         jfd.overlay->data = calloc(jfd.sector_count, sizeof(*jfd.overlay->data));
         assert(jfd.overlay->data);
         size_t first = jfd.tracks[0].first_sector;
         jfd.overlay->data[first] = calloc(jfd.sectors[first].size, 1u);
         assert(jfd.overlay->data[first]);
         jfd.overlay->visible = true;
         assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery) &&
                !discovery.readable);
         jfd.overlay->visible = false;
         assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery) &&
                discovery.readable && discovery.application_count == 1u);
      }
      if (index == 0u)
      {
         unsigned first = jfd.tracks[0].first_sector;
         uint8_t saved_id = jfd.sectors[first + 1u].id;
         jfd.sectors[first + 1u].id = jfd.sectors[first].id;
         assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery) && !discovery.readable);
         jfd.sectors[first + 1u].id = saved_id;
         jfd.sectors[first].flags |= ARC_JFD_DATA_CRC_BAD;
         assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery) && !discovery.readable);
         jfd.sectors[first].flags = 0u;
         jfd.sectors[first].id = 99u;
         assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery) && !discovery.readable);
      }
      arc_jfd_free(&jfd);
      free(bytes);
   }
   for (unsigned case_index = 0u; case_index < 3u; ++case_index)
   {
      const char *name = case_index == 0u ? "write-slow.jfd" :
         case_index == 1u ? "write-duplicate.jfd" : "write-shared.jfd";
      char path[128];
      snprintf(path, sizeof(path), "jfd-family/%s", name);
      uint8_t *bytes = read_fixture(fixture_directory, path, &size);
      ArcJfdImage jfd;
      assert(arc_jfd_parse(bytes, size, &jfd, error, sizeof(error)));
      for (size_t sector = 0u; sector < jfd.sector_count; ++sector)
         assert(!arc_jfd_sector_write_eligible(&jfd, sector));
      arc_jfd_free(&jfd);
      free(bytes);
   }

   static const struct {
      const char *file;
      ArcLaunchCandidateState state;
      const uint8_t *name;
      size_t name_length;
      unsigned candidates;
      unsigned applications;
      unsigned unknowns;
      int target;
      ArcLaunchReason reason;
      bool launchable;
      bool has_boot;
      bool ignored_attributes;
   } robust_jfds[] = {
      {"launch-family/short-name-attr-l.jfd", ARC_LAUNCH_CANDIDATE_APPLICATION,
       (const uint8_t *)"!A", 2u, 1u, 1u, 0u, 0,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION, true, false, true},
      {"launch-family/top-bit-d.jfd", ARC_LAUNCH_CANDIDATE_APPLICATION,
       (const uint8_t *)"!Z\xf6rvex", 7u, 1u, 1u, 0u, 0,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION, true, false, false},
      {"launch-family/hard-space-e.jfd", ARC_LAUNCH_CANDIDATE_APPLICATION,
       (const uint8_t *)"!Nim\xa0" "Vox", 8u, 1u, 1u, 0u, 0,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION, true, false, false},
      {"launch-family/full-ten-d.jfd", ARC_LAUNCH_CANDIDATE_APPLICATION,
       (const uint8_t *)"!ABCDEFGHI", 10u, 1u, 1u, 0u, 0,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION, true, false, false},
      {"launch-family/attr-88-d.jfd", ARC_LAUNCH_CANDIDATE_APPLICATION,
       (const uint8_t *)"!Zorvex", 7u, 1u, 1u, 0u, 0,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION, true, false, true},
      {"launch-family/irrelevant-extent-e.jfd", ARC_LAUNCH_CANDIDATE_APPLICATION,
       (const uint8_t *)"!Zorvex", 7u, 1u, 1u, 0u, 0,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION, true, false, false},
      {"launch-family/incomplete-l.jfd", ARC_LAUNCH_CANDIDATE_UNKNOWN,
       (const uint8_t *)"!Broken", 7u, 1u, 0u, 1u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true, false, false},
      {"launch-family/incomplete-d.jfd", ARC_LAUNCH_CANDIDATE_UNKNOWN,
       (const uint8_t *)"!Broken", 7u, 1u, 0u, 1u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true, false, false},
      {"launch-family/incomplete-e.jfd", ARC_LAUNCH_CANDIDATE_UNKNOWN,
       (const uint8_t *)"!Broken", 7u, 1u, 0u, 1u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true, false, false},
      {"launch-family/label-with-unknown-d.jfd", ARC_LAUNCH_CANDIDATE_APPLICATION,
       (const uint8_t *)"!Zorvex", 7u, 2u, 1u, 1u, 0,
       ARC_LAUNCH_REASON_DISC_NAME_MATCH, true, false, false},
      {"launch-family/label-is-unknown-d.jfd", ARC_LAUNCH_CANDIDATE_APPLICATION,
       (const uint8_t *)"!Zorvex", 7u, 2u, 1u, 1u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true, false, false},
      {"launch-family/duplicate-root-d.jfd", ARC_LAUNCH_CANDIDATE_UNKNOWN,
       (const uint8_t *)"!Qarvox", 7u, 2u, 0u, 2u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, false, false, false},
      {"launch-family/duplicate-root-file-d.jfd", ARC_LAUNCH_CANDIDATE_UNKNOWN,
       (const uint8_t *)"!Qarvox", 7u, 1u, 0u, 1u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, false, false, false},
      {"launch-family/duplicate-run-d.jfd", ARC_LAUNCH_CANDIDATE_UNKNOWN,
       (const uint8_t *)"!Zorvex", 7u, 1u, 0u, 1u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, false, false, false},
      {"launch-family/duplicate-boot-e.jfd", ARC_LAUNCH_CANDIDATE_UNKNOWN,
       (const uint8_t *)"!Zorvex", 7u, 1u, 0u, 1u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, false, false, false},
      {"launch-family/has-boot-d.jfd", ARC_LAUNCH_CANDIDATE_APPLICATION,
       (const uint8_t *)"!Zorvex", 7u, 1u, 1u, 0u, 0,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION, true, true, false},
      {"launch-family/self-boot-unknown-d.jfd", ARC_LAUNCH_CANDIDATE_UNKNOWN,
       (const uint8_t *)"!Broken", 7u, 1u, 0u, 1u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true, false, false},
      {"launch-family/illegal-l.jfd", ARC_LAUNCH_CANDIDATE_APPLICATION,
       (const uint8_t *)"!Bad.Name", 9u, 18u, 18u, 0u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false, false, false},
   };
   for (size_t index = 0u; index < sizeof(robust_jfds) / sizeof(robust_jfds[0]); ++index)
   {
      uint8_t *bytes = read_fixture(fixture_directory,
                                    robust_jfds[index].file, &size);
      ArcJfdImage jfd;
      assert(arc_jfd_parse(bytes, size, &jfd, error, sizeof(error)));
      assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery));
      assert(discovery.readable &&
             discovery.candidate_count == robust_jfds[index].candidates &&
             discovery.application_count == robust_jfds[index].applications &&
             discovery.unknown_count == robust_jfds[index].unknowns &&
             discovery.target_index == robust_jfds[index].target &&
             discovery.reason == robust_jfds[index].reason &&
             discovery.ignored_attribute_bits == robust_jfds[index].ignored_attributes &&
             discovery.candidates[0].state == robust_jfds[index].state &&
             discovery.candidates[0].launchable == robust_jfds[index].launchable &&
             discovery.candidates[0].has_boot == robust_jfds[index].has_boot &&
             discovery.candidates[0].name.length == robust_jfds[index].name_length &&
             !memcmp(discovery.candidates[0].name.bytes,
                     robust_jfds[index].name, robust_jfds[index].name_length));
      arc_jfd_free(&jfd);
      free(bytes);
   }

   uint8_t *checkpoint = read_fixture(fixture_directory,
      "launch-family/checkpoint-a-guest.adf", &size);
   assert(arc_media_inspect("checkpoint-a-guest.adf", checkpoint, size,
                            &media, error, sizeof(error)) &&
          arc_launch_discover(checkpoint, size, &media, NULL, &discovery));
   test_validate_checkpoint_d_image(checkpoint, size, 7u);
   static const char *const checkpoint_names[] = {
      "!Alias", "!Fault", "!Fonts", "!Prep", "!Qaventra", "!Scrap", "!System"
   };
   static const char *const checkpoint_boots[] = {
      "Set ArcDroid$Trace <ArcDroid$Trace>A\r"
      "Set ArcDroid$Count <ArcDroid$Count>1\r"
      "Set Alias$Dir Echo ARCDROID-DIR-ALIAS-CAPTURED\r"
      "Set Alias$Filer_Run Echo ARCDROID-RUN-ALIAS-CAPTURED\r",
      "Set ArcDroid$Trace <ArcDroid$Trace>E\r"
      "Set ArcDroid$Count <ArcDroid$Count>1\r"
      "Error ArcDroid synthetic sibling error\r",
      "Set ArcDroid$Trace <ArcDroid$Trace>F\r"
      "Set ArcDroid$Count <ArcDroid$Count>1\r",
      "Set ArcDroid$Trace <ArcDroid$Trace>P\r"
      "Set ArcDroid$Count <ArcDroid$Count>1\rSet ArcDroid$Ready Yes\r",
      "Set ArcDroid$Trace <ArcDroid$Trace>T\r"
      "Set ArcDroid$Count <ArcDroid$Count>1\r",
      "Set ArcDroid$Trace <ArcDroid$Trace>W\r"
      "Set ArcDroid$Count <ArcDroid$Count>1\r"
      "Echo ArcDroid-write-attempt { > ADFS::0.$.ArcWrite }\r",
      "Set ArcDroid$Trace S\rSet ArcDroid$Count 1\r",
   };
   assert(discovery.readable && discovery.candidate_count == 7u &&
          discovery.application_count == 1u && !discovery.unknown_count &&
          discovery.target_index == 4 &&
          discovery.reason == ARC_LAUNCH_REASON_SINGLE_APPLICATION);
   assert(!discovery.candidates[0].resource &&
          !discovery.candidates[1].resource &&
          discovery.candidates[2].resource &&
          !discovery.candidates[3].resource &&
          !discovery.candidates[4].resource &&
          discovery.candidates[5].resource &&
          discovery.candidates[6].resource &&
          arc_launch_candidate_index(&discovery, 0u) == 0u &&
          arc_launch_candidate_index(&discovery, 1u) == 1u &&
          arc_launch_candidate_index(&discovery, 2u) == 3u &&
          arc_launch_candidate_index(&discovery, 3u) == 4u &&
          arc_launch_candidate_index(&discovery, 4u) == UINT_MAX);
   for (unsigned i = 0u; i < 7u; ++i)
   {
      assert(test_guest_name_equal(&discovery.candidates[i].name,
                                   checkpoint_names[i]) &&
             discovery.candidates[i].has_boot);
      const uint8_t *root_entry = checkpoint + 1024u + 5u + i * 26u;
      assert(!memcmp(root_entry, checkpoint_names[i], strlen(checkpoint_names[i])));
      size_t child_offset = (size_t)test_le24(root_entry + 22u) * 256u;
      const uint8_t *boot_entry = checkpoint + child_offset + 5u;
      assert(!memcmp(boot_entry, "!Boot", 5u));
      size_t boot_length = test_le32(boot_entry + 18u);
      size_t boot_offset = (size_t)test_le24(boot_entry + 22u) * 256u;
      assert(boot_length == strlen(checkpoint_boots[i]) &&
             !memcmp(checkpoint + boot_offset, checkpoint_boots[i], boot_length));
   }
   static const char checkpoint_run[] =
      "Echo ARCDROID CHECKPOINT TRACE <ArcDroid$Trace> COUNT <ArcDroid$Count>\r"
      "If \"<ArcDroid$Ready>\" <> \"Yes\" Then Error ArcDroid prerequisite missing\r"
      "If \"<ArcDroid$Trace>\" <> \"SFWAEPT\" Then Error ArcDroid boot order wrong\r"
      "If \"<ArcDroid$Count>\" <> \"1111111\" Then Error ArcDroid boot count wrong\r"
      "Echo ARCDROID CHECKPOINT TARGET REACHED <ArcDroid$Trace> <ArcDroid$Count>\r";
   const uint8_t *target_entry = checkpoint + 1024u + 5u + 4u * 26u;
   size_t target_offset = (size_t)test_le24(target_entry + 22u) * 256u;
   const uint8_t *target_run = checkpoint + target_offset + 31u;
   size_t target_run_length = test_le32(target_run + 18u);
   size_t target_run_offset = (size_t)test_le24(target_run + 22u) * 256u;
   assert(!memcmp(target_run, "!Run", 4u) &&
          target_run_length == sizeof(checkpoint_run) - 1u &&
          !memcmp(checkpoint + target_run_offset, checkpoint_run,
                  sizeof(checkpoint_run) - 1u));
   static const uint8_t checkpoint_script[] =
      "| ArcDroid desktop boot file\r"
      "%Filer_Boot ADFS::0.$.!System\r"
      "%Filer_Boot ADFS::0.$.!Fonts\r"
      "%Filer_Boot ADFS::0.$.!Scrap\r"
      "%Filer_Boot ADFS::0.$.!Alias\r"
      "%Filer_Boot ADFS::0.$.!Fault\r"
      "%Filer_Boot ADFS::0.$.!Prep\r"
      "%Filer_Boot ADFS::0.$.!Qaventra\r"
      "%Dir ADFS::0.$\r"
      "%URD ADFS::0.$\r"
      "%Filer_Run ADFS::0.$.!Qaventra\r";
   size_t checkpoint_script_size = 0u;
   assert(arc_launch_script_build(&discovery, 4u, NULL, 0u,
                                  &checkpoint_script_size, error, sizeof(error)) &&
          checkpoint_script_size == sizeof(checkpoint_script) - 1u);
   uint8_t *checkpoint_script_output = malloc(checkpoint_script_size);
   assert(checkpoint_script_output &&
          arc_launch_script_build(&discovery, 4u, checkpoint_script_output,
                                  checkpoint_script_size,
                                  &checkpoint_script_size, error, sizeof(error)) &&
          !memcmp(checkpoint_script_output, checkpoint_script,
                  sizeof(checkpoint_script) - 1u));
   free(checkpoint_script_output);
   free(checkpoint);

   typedef struct {
      const char *file;
      const uint8_t name[10];
      uint8_t name_length;
      const char *run;
   } CheckpointNameDisc;
   static const CheckpointNameDisc checkpoint_name_discs[] = {
      {"launch-family/checkpoint-a-top-bit.adf",
       {'!', 'Z', 0xf6u, 'r', 'v', 'e', 'x'}, 7u,
       "Echo ARCDROID TOP-BIT NAME REACHED\r"},
      {"launch-family/checkpoint-a-hard-space.adf",
       {'!', 'N', 'i', 'm', 0xa0u, 'V', 'o', 'x'}, 8u,
       "Echo ARCDROID HARD-SPACE NAME REACHED\r"},
      {"launch-family/checkpoint-a-greater-than.adf",
       {'!', 'V', 'o', 'r', '>', 'n', 'e', 'x'}, 8u,
       "Echo ARCDROID GREATER-THAN NAME REACHED\r"},
   };
   for (size_t i = 0u; i < sizeof(checkpoint_name_discs) /
                                sizeof(checkpoint_name_discs[0]); ++i)
   {
      const CheckpointNameDisc *item = &checkpoint_name_discs[i];
      checkpoint = read_fixture(fixture_directory, item->file, &size);
      test_validate_checkpoint_d_image(checkpoint, size, 1u);
      assert(arc_media_inspect(item->file, checkpoint, size,
                               &media, error, sizeof(error)) &&
             arc_launch_discover(checkpoint, size, &media, NULL, &discovery));
      assert(discovery.readable && discovery.candidate_count == 1u &&
             discovery.application_count == 1u && !discovery.unknown_count &&
             discovery.target_index == 0 &&
             discovery.reason == ARC_LAUNCH_REASON_SINGLE_APPLICATION &&
             discovery.candidates[0].launchable &&
             discovery.candidates[0].name.length == item->name_length &&
             !memcmp(discovery.candidates[0].name.bytes,
                     item->name, item->name_length));
      const uint8_t *root_entry = checkpoint + 1024u + 5u;
      size_t child_offset = (size_t)test_le24(root_entry + 22u) * 256u;
      const uint8_t *run_entry = checkpoint + child_offset + 5u;
      size_t run_length = test_le32(run_entry + 18u);
      size_t run_offset = (size_t)test_le24(run_entry + 22u) * 256u;
      assert(!memcmp(run_entry, "!Run", 4u) && run_length == strlen(item->run) &&
             !memcmp(checkpoint + run_offset, item->run, run_length));
      free(checkpoint);
   }

   uint8_t *first = malloc(ARC_LAUNCH_DISC_SIZE);
   uint8_t *second = malloc(ARC_LAUNCH_DISC_SIZE);
   ArcLaunchDiscovery helper_spec = test_launch_spec("!Demo", 5u);
   assert(first && second &&
          arc_launch_disc_build(&helper_spec, 0u, first, error, sizeof(error)) &&
          arc_launch_disc_build(&helper_spec, 0u, second, error, sizeof(error)));
   assert(!memcmp(first, second, ARC_LAUNCH_DISC_SIZE));
   assert(arc_media_inspect("helper.adl", first, ARC_LAUNCH_DISC_SIZE,
                            &media, error, sizeof(error)));
   assert(media.boot_option == 2u && !strcmp(media.disc_name, "ArcDroid"));
   const uint8_t *root = first + test_l_offset(2u);
   assert(!memcmp(root + 1u, "Hugo", 4u));
   assert(test_le32(root + 5u + 10u) == UINT32_C(0xffffeb00));
   assert(!memcmp(root + 5u + 14u, "\0\0\0\0", 4u));
   assert(test_le32(root + 31u + 10u) == UINT32_C(0xffffea00));
   assert(!memcmp(root + 31u + 14u, "\0\0\0\0", 4u));
   assert(!memcmp(first + test_l_offset(1279u), "| ArcDroid launch disc", 22u));
   assert(!memcmp(first + test_l_offset(1280u), "| ArcDroid desktop boot file", 28u));
   assert(strstr((const char *)(first + test_l_offset(1280u)),
                 "%Filer_Boot ADFS::0.$.!Demo\r"
                 "%Dir ADFS::0.$\r%URD ADFS::0.$\r"
                 "%Filer_Run ADFS::0.$.!Demo\r"));
   static const char *bad_targets[] = {
      "!Bad.Name", "!Bad:Name",
      "!Bad$Name", "!Bad&Name", "!Bad@Name", "!Bad^Name", "!Bad%Name",
      "!Bad*Name", "!Bad#Name", "!Bad|Name", "!Bad\"Name", "!Bad<Name",
      "!Bad Name", "!Bad\\Name", "!Bad\001", "!Bad\177"
   };
   for (size_t i = 0u; i < sizeof(bad_targets) / sizeof(bad_targets[0]); ++i)
   {
      helper_spec = test_launch_spec(bad_targets[i], strlen(bad_targets[i]));
      assert(!arc_launch_disc_build(&helper_spec, 0u, second,
                                    error, sizeof(error)) &&
             !strcmp(error, "launch target leaf is not representable"));
   }
   helper_spec = test_launch_spec("!Demo", 5u);
   assert(arc_launch_disc_build(&helper_spec, 0u, second,
                                error, sizeof(error)));

   ArcLaunchDiscovery literals;
   memset(&literals, 0, sizeof(literals));
   static const uint8_t literal_names[][10] = {
      {'!', 'O', 'r', 'd'},
      {'!', 'S', 'y', 's', 't', 'e', 'm'},
      {'!', 'F', 'o', 'n', 't', 's'},
      {'!', 'S', 'c', 'r', 'a', 'p'},
      {'!', 'V', 'o', 'r', '>', 'n', 'e', 'x'},
      {'!', 'Z', 0xf6u, 'r', 'v', 'e', 'x'},
      {'!', 'N', 'i', 'm', 0xa0u, 'V', 'o', 'x'},
   };
   static const uint8_t literal_lengths[] = {4u, 7u, 6u, 6u, 8u, 7u, 8u};
   literals.candidate_count = (unsigned)(sizeof(literal_lengths) / sizeof(literal_lengths[0]));
   literals.application_count = 1u;
   for (unsigned i = 0u; i < literals.candidate_count; ++i)
   {
      literals.candidates[i].name = test_guest_name(literal_names[i], literal_lengths[i]);
      literals.candidates[i].state = i + 1u == literals.candidate_count ?
         ARC_LAUNCH_CANDIDATE_APPLICATION : ARC_LAUNCH_CANDIDATE_NOT_APPLICATION;
      literals.candidates[i].launchable = true;
      literals.candidates[i].has_boot = i + 1u != literals.candidate_count;
   }
   static const uint8_t expected_script[] =
      "| ArcDroid desktop boot file\r"
      "%Filer_Boot ADFS::0.$.!System\r"
      "%Filer_Boot ADFS::0.$.!Fonts\r"
      "%Filer_Boot ADFS::0.$.!Scrap\r"
      "%Filer_Boot ADFS::0.$.!Ord\r"
      "%Filer_Boot ADFS::0.$.!Vor>nex\r"
      "%Filer_Boot ADFS::0.$.!Z\xf6rvex\r"
      "%Filer_Boot ADFS::0.$.!Nim\xa0" "Vox\r"
      "%Dir ADFS::0.$\r"
      "%URD ADFS::0.$\r"
      "%Filer_Run ADFS::0.$.!Nim\xa0" "Vox\r";
   size_t script_size = 0u;
   assert(arc_launch_script_build(&literals, literals.candidate_count - 1u,
                                  NULL, 0u, &script_size, error, sizeof(error)) &&
          script_size == sizeof(expected_script) - 1u);
   uint8_t *script = malloc(script_size);
   assert(script && arc_launch_script_build(&literals,
          literals.candidate_count - 1u, script, script_size, &script_size,
          error, sizeof(error)) &&
          !memcmp(script, expected_script, sizeof(expected_script) - 1u));
   free(script);

   ArcGuestName diagnostic = test_guest_name("A\"\\\xa0", 4u);
   char escaped[32];
   assert(arc_launch_escape_name(&diagnostic, escaped, sizeof(escaped)) &&
          !strcmp(escaped, "A\\\"\\\\\\xA0"));

   ArcLaunchDiscovery maximum;
   memset(&maximum, 0, sizeof(maximum));
   maximum.candidate_count = ARC_LAUNCH_MAX_CANDIDATES;
   maximum.application_count = 1u;
   for (unsigned i = 0u; i < maximum.candidate_count; ++i)
   {
      char name[11];
      assert(snprintf(name, sizeof(name), "!Boot%05u", i) == 10);
      maximum.candidates[i].name = test_guest_name(name, 10u);
      maximum.candidates[i].state = i + 1u == maximum.candidate_count ?
         ARC_LAUNCH_CANDIDATE_APPLICATION : ARC_LAUNCH_CANDIDATE_NOT_APPLICATION;
      maximum.candidates[i].launchable = true;
      maximum.candidates[i].has_boot = i + 1u != maximum.candidate_count;
   }
   assert(arc_launch_disc_build(&maximum, maximum.candidate_count - 1u,
                                second, error, sizeof(error)));
   const uint8_t *max_root = second + test_l_offset(2u);
   uint32_t max_launch_length = test_le32(max_root + 31u + 18u);
   unsigned max_launch_sectors = (max_launch_length + 255u) / 256u;
   assert(max_launch_sectors > 1u && second[510] == 6u);
   unsigned free_start_0 = second[0] | ((unsigned)second[1] << 8u) |
                           ((unsigned)second[2] << 16u);
   unsigned free_start_1 = second[3] | ((unsigned)second[4] << 8u) |
                           ((unsigned)second[5] << 16u);
   unsigned free_length_0 = second[256] | ((unsigned)second[257] << 8u) |
                            ((unsigned)second[258] << 16u);
   unsigned free_length_1 = second[259] | ((unsigned)second[260] << 8u) |
                            ((unsigned)second[261] << 16u);
   assert(free_start_0 == 7u && free_start_1 == 1280u + max_launch_sectors &&
          free_start_0 + free_length_0 == 1279u &&
          free_start_1 + free_length_1 == 2560u &&
          free_length_0 + free_length_1 + 7u + 1u + max_launch_sectors == 2560u &&
          free_start_0 + free_length_0 <= 1279u &&
          1280u + max_launch_sectors <= free_start_1);

   uint32_t random = UINT32_C(0x32424c44);
   static const char *mutation_files[] = {
      "launch-family/single-l.adl", "launch-family/single-d-hugo.adf",
      "launch-family/single-e-hugo.adf", "launch-family/fragmented-e.adf",
      "launch-family/late-target-e.adf", "launch-family/stale-d.adf",
      "launch-family/stale-e.adf"
   };
   for (unsigned iteration = 0u; iteration < 1000u; ++iteration)
   {
      const char *name = mutation_files[
         iteration % (sizeof(mutation_files) / sizeof(mutation_files[0]))];
      uint8_t *bytes = read_fixture(fixture_directory, name, &size);
      random = random * UINT32_C(1664525) + UINT32_C(1013904223);
      unsigned changes = 1u + (random & 3u);
      for (unsigned change = 0u; change < changes; ++change)
      {
         random = random * UINT32_C(1664525) + UINT32_C(1013904223);
         bytes[random % size] ^= (uint8_t)(1u << ((random >> 16u) & 7u));
      }
      if (arc_media_inspect(name, bytes, size, &media, error, sizeof(error)))
      {
         assert(arc_launch_discover(bytes, size, &media, NULL, &discovery));
         assert(discovery.candidate_count <= ARC_LAUNCH_MAX_CANDIDATES);
      }
      free(bytes);
   }
   free(second);
   free(first);
}

static void test_nested_launch(const char *fixture_directory)
{
   {
      ArcLaunchDiscovery spec = test_launch_spec("!123456789", 10u);
      char target[23]; uint8_t length = 0u;
      memset(target, 'X', sizeof(target));
      assert(arc_launch_target_compose(&spec.candidates[0], target, 22u,
                                       &length) &&
             length == 10u && !memcmp(target, "!123456789\0", 11u) &&
             target[22] == 'X');
      spec.candidates[0].parent = test_guest_name("ABCDEFGHIJ", 10u);
      memset(target, 'X', sizeof(target));
      assert(arc_launch_target_compose(&spec.candidates[0], target, 22u,
                                       &length) &&
             length == 21u &&
             !memcmp(target, "ABCDEFGHIJ.!123456789\0", 22u) &&
             target[22] == 'X');
      assert(!arc_launch_target_compose(&spec.candidates[0], target, 21u,
                                        &length));
   }
   {
      size_t size = 0u;
      uint8_t *bytes = read_fixture(fixture_directory,
         "arcdroid-extended-chooser-d.adf", &size);
      ArcMediaInfo media; ArcLaunchDiscovery discovery;
      char error[192] = {0};
      assert(arc_media_inspect("arcdroid-extended-chooser-d.adf", bytes, size,
                               &media, error, sizeof(error)) &&
             arc_launch_discover(bytes, size, &media, NULL, &discovery) &&
             discovery.nested_scope && discovery.candidate_count == 2u &&
             discovery.candidates[0].addressable &&
             discovery.candidates[0].parent.bytes[3] == 0xa0u);
      uint8_t *helper = malloc(ARC_LAUNCH_DISC_SIZE);
      assert(helper && arc_launch_disc_build(&discovery, 0u, helper,
                                              error, sizeof(error)));
      free(helper);
      free(bytes);
   }
   {
      size_t size = 0u;
      uint8_t *bytes = read_fixture(fixture_directory,
         "arcdroid-g1-chooser-l.adl", &size);
      ArcMediaInfo media; ArcLaunchDiscovery discovery;
      char error[192] = {0};
      assert(arc_media_inspect("arcdroid-g1-chooser-l.adl", bytes, size,
                               &media, error, sizeof(error)) &&
             arc_launch_discover(bytes, size, &media, NULL, &discovery) &&
             discovery.boot_option == 3u && !discovery.root_boot &&
             discovery.reason == ARC_LAUNCH_REASON_SINGLE_APPLICATION &&
             discovery.target_index == 0);
      free(bytes);
   }
   static const struct {
      const char *file;
      unsigned count;
      int target;
      ArcLaunchReason reason;
      bool incomplete;
   } cases[] = {
      {"launch-family/nested-literal-d.adf", 4u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-extended-literal-d.adf", 1u, 0,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION, false},
      {"launch-family/nested-single-e.adf", 1u, 0,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION, false},
      {"launch-family/nested-disc-tie-d.adf", 2u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-unsafe-leaf-d.adf", 1u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-collision-d.adf", 2u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-file-collision-d.adf", 1u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-empty-prefix-d.adf", 1u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-pipe-d.adf", 1u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-quote-d.adf", 1u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-extended-boundary-d.adf", 1u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-extended-first-d.adf", 1u, 0,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION, false},
      {"launch-family/nested-one-byte-d.adf", 1u, 0,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION, false},
      {"launch-family/nested-case-file-d.adf", 1u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-literal-duplicate-d.adf", 2u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-parent-file-d.adf", 1u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-unreadable-d.adf", 1u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/nested-no-target-unreadable-d.adf", 0u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/nested-unreadable-child-d.adf", 1u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, false},
      {"launch-family/nested-child-collision-d.adf", 1u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, false},
      {"launch-family/nested-77-d.adf", 77u, -1,
       ARC_LAUNCH_REASON_AMBIGUOUS, false},
      {"launch-family/nested-78-d.adf", 77u, -1,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/nested-77-parents-d.adf", 0u, -1,
       ARC_LAUNCH_REASON_NO_CANDIDATES, false},
   };
   char error[192] = {0};
   for (unsigned i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i)
   {
      size_t size = 0u;
      uint8_t *bytes = read_fixture(fixture_directory, cases[i].file, &size);
      ArcMediaInfo media;
      ArcLaunchDiscovery discovery;
      assert(arc_media_inspect(cases[i].file, bytes, size,
                               &media, error, sizeof(error)) &&
             arc_launch_discover(bytes, size, &media, NULL, &discovery));
      if (discovery.candidate_count != cases[i].count ||
          discovery.target_index != cases[i].target ||
          discovery.reason != cases[i].reason)
         fprintf(stderr, "nested %s count=%u target=%d reason=%s\n",
                 cases[i].file, discovery.candidate_count,
                 discovery.target_index, arc_launch_reason_name(discovery.reason));
      assert(discovery.readable && discovery.nested_scope &&
             discovery.candidate_count == cases[i].count &&
             discovery.target_index == cases[i].target &&
             discovery.reason == cases[i].reason &&
             discovery.scan_incomplete == cases[i].incomplete);
      if (i == 0u)
      {
         assert(test_guest_name_equal(&discovery.candidates[0].parent, "Folder") &&
                test_guest_name_equal(&discovery.candidates[3].parent, "Extras") &&
                discovery.candidates[1].resource &&
                arc_launch_candidate_index(&discovery, 1u) == 2u);
         static const uint8_t script[] =
            "| ArcDroid desktop boot file\r"
            "%Filer_Boot ADFS::0.$.Folder.!System\r"
            "%Filer_Boot ADFS::0.$.Folder.!Nulvane\r"
            "%Dir ADFS::0.$\r%URD ADFS::0.$\r"
            "%Filer_Run ADFS::0.$.Folder.!Nulvane\r";
         uint8_t output[512]; size_t written = 0u;
         assert(arc_launch_script_build(&discovery, 0u, output,
                    sizeof(output), &written, error, sizeof(error)) &&
                written == sizeof(script) - 1u &&
                !memcmp(output, script, written));
         ArcGuestName parent = discovery.candidates[0].parent;
         ArcGuestName leaf = discovery.candidates[0].name;
         uint8_t record[ARC_LAUNCH_CHOICE_MAX_SIZE]; size_t length;
         ArcLaunchChoiceRecord choice;
         assert(arc_launch_choice_format_nested(&parent, &leaf, false,
                    discovery.candidate_fingerprint, record,
                    sizeof(record), &length) &&
                arc_launch_choice_parse(record, length, &choice) &&
                choice.version == 3u &&
                arc_launch_choice_match(&discovery, &choice) == 0);
         choice.parent.bytes[0] ^= 1u;
         assert(arc_launch_choice_match(&discovery, &choice) == INT_MIN);
         choice.parent.bytes[0] ^= 1u;
         choice.version = 1u;
         assert(arc_launch_choice_match(&discovery, &choice) == INT_MIN);
         record[length++] = 'x';
         assert(!arc_launch_choice_parse(record, length, &choice));
         assert(arc_launch_choice_format_nested(NULL, NULL, true,
                    discovery.candidate_fingerprint, record,
                    sizeof(record), &length) &&
                arc_launch_choice_parse(record, length, &choice) &&
                choice.version == 3u && choice.desktop &&
                arc_launch_choice_match(&discovery, &choice) == -1);
         uint8_t binding_text[ARC_LAUNCH_BINDING_MAX_SIZE];
         ArcLaunchBinding binding;
         assert(arc_launch_binding_format_nested(0u, &parent, &leaf,
                    discovery.candidate_fingerprint, binding_text,
                    sizeof(binding_text), &length) &&
                arc_launch_binding_parse(binding_text, length, &binding) &&
                binding.version == 3u && binding.ordinal == 0u &&
                binding.parent.length == parent.length);
      }
      if (i == 1u)
      {
         static const uint8_t parent[] = "Arc\xa0Games";
         assert(discovery.candidates[0].addressable &&
                discovery.candidates[0].parent_address.length == sizeof(parent) - 1u &&
                !memcmp(discovery.candidates[0].parent_address.bytes,
                        parent, sizeof(parent) - 1u));
         uint8_t output[256]; size_t written = 0u;
         assert(arc_launch_script_build(&discovery, 0u, output,
                    sizeof(output) - 1u, &written, error, sizeof(error)));
         output[written] = 0u;
         assert(strstr((const char *)output, "Arc\xa0Games.!Demo"));
      }
      if (!strcmp(cases[i].file, "launch-family/nested-empty-prefix-d.adf") ||
          !strcmp(cases[i].file, "launch-family/nested-pipe-d.adf") ||
          !strcmp(cases[i].file, "launch-family/nested-quote-d.adf") ||
          !strcmp(cases[i].file, "launch-family/nested-extended-boundary-d.adf") ||
          !strcmp(cases[i].file, "launch-family/nested-file-collision-d.adf") ||
          !strcmp(cases[i].file, "launch-family/nested-case-file-d.adf") ||
          !strcmp(cases[i].file, "launch-family/nested-literal-duplicate-d.adf"))
      {
         assert(!discovery.candidates[0].addressable &&
                !discovery.candidates[0].launchable &&
                discovery.candidates[0].parent_address.length == 0u);
         ArcLaunchChooser chooser;
         uint8_t script[256]; size_t written = 0u;
         assert(!arc_launch_chooser_init(&chooser, &discovery, false, -1) &&
                !arc_launch_script_build(&discovery, 0u, script,
                                         sizeof(script), &written,
                                         error, sizeof(error)));
      }
      if (!strcmp(cases[i].file, "launch-family/nested-unreadable-d.adf"))
         assert(discovery.unreadable_parents == 1u);
      if (!strcmp(cases[i].file, "launch-family/nested-78-d.adf"))
         assert(discovery.omitted_candidates == 1u);
      free(bytes);
   }
   for (unsigned boot = 1u; boot <= 3u; ++boot)
      for (unsigned kind = 0u; kind < 2u; ++kind)
      {
         char file[80];
         snprintf(file, sizeof(file), "launch-family/boot-%s-%u-l.adl",
                  kind ? "dir" : "file", boot);
         size_t size = 0u;
         uint8_t *bytes = read_fixture(fixture_directory, file, &size);
         ArcMediaInfo media; ArcLaunchDiscovery discovery;
         assert(arc_media_inspect(file, bytes, size, &media,
                                  error, sizeof(error)) &&
                arc_launch_discover(bytes, size, &media, NULL, &discovery) &&
                discovery.root_boot &&
                discovery.reason == ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF);
         free(bytes);
      }
   static const struct {
      const char *file;
      unsigned count;
      ArcLaunchReason reason;
   } root_scope[] = {
      {"launch-family/nested-mixed-root-d.adf", 1u,
       ARC_LAUNCH_REASON_SINGLE_APPLICATION},
      {"launch-family/nested-resource-root-d.adf", 1u,
       ARC_LAUNCH_REASON_NO_APPLICATIONS},
      {"launch-family/nested-unknown-root-d.adf", 1u,
       ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE},
   };
   for (unsigned i = 0u; i < sizeof(root_scope) / sizeof(root_scope[0]); ++i)
   {
      size_t size = 0u;
      uint8_t *bytes = read_fixture(fixture_directory, root_scope[i].file, &size);
      ArcMediaInfo media; ArcLaunchDiscovery discovery;
      assert(arc_media_inspect(root_scope[i].file, bytes, size,
                               &media, error, sizeof(error)) &&
             arc_launch_discover(bytes, size, &media, NULL, &discovery) &&
             !discovery.nested_scope &&
             discovery.candidate_count == root_scope[i].count &&
             discovery.reason == root_scope[i].reason);
      free(bytes);
   }
   {
      const char *file = "launch-family/nested-depth-two-d.adf";
      size_t size = 0u;
      uint8_t *bytes = read_fixture(fixture_directory, file, &size);
      ArcMediaInfo media; ArcLaunchDiscovery discovery;
      assert(arc_media_inspect(file, bytes, size, &media,
                               error, sizeof(error)) &&
             arc_launch_discover(bytes, size, &media, NULL, &discovery) &&
             discovery.nested_scope && !discovery.candidate_count &&
             discovery.reason == ARC_LAUNCH_REASON_NO_CANDIDATES);
      free(bytes);
   }
   static const char *const boot_objects[] = {
      "absent", "file", "directory"
   };
   static const char *const boot_shapes[] = {
      "zero", "one", "multiple", "unknown"
   };
   for (unsigned boot = 0u; boot <= 3u; ++boot)
      for (unsigned object = 0u; object < 3u; ++object)
         for (unsigned shape = 0u; shape < 4u; ++shape)
         {
            char file[96];
            snprintf(file, sizeof(file),
                     "launch-family/boot-matrix-%u-%s-%s.adf",
                     boot, boot_objects[object], boot_shapes[shape]);
            size_t size = 0u;
            uint8_t *bytes = read_fixture(fixture_directory, file, &size);
            ArcMediaInfo media; ArcLaunchDiscovery discovery;
            assert(arc_media_inspect(file, bytes, size, &media,
                                     error, sizeof(error)) &&
                   arc_launch_discover(bytes, size, &media, NULL, &discovery) &&
                   discovery.readable && discovery.boot_option == boot &&
                   discovery.root_boot == (object != 0u) &&
                   (discovery.reason == ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF) ==
                      (boot != 0u && object != 0u));
            free(bytes);
         }
   for (unsigned broken = 0u; broken < 2u; ++broken)
   {
      const char *file = broken ?
         "launch-family/boot-unreadable-root-d.adf" :
         "launch-family/boot-duplicate-root-d.adf";
      size_t size = 0u;
      uint8_t *bytes = read_fixture(fixture_directory, file, &size);
      ArcMediaInfo media; ArcLaunchDiscovery discovery;
      assert(arc_media_inspect(file, bytes, size, &media,
                               error, sizeof(error)) &&
             arc_launch_discover(bytes, size, &media, NULL, &discovery));
      assert(broken ? !discovery.readable &&
                      discovery.reason == ARC_LAUNCH_REASON_UNREADABLE :
                      discovery.root_boot &&
                      discovery.reason == ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF);
      free(bytes);
   }
   for (unsigned kind = 0u; kind < 2u; ++kind)
   {
      const char *file = kind ?
         "launch-family/checkpoint-nested-extended.adf" :
         "launch-family/checkpoint-nested-literal.adf";
      size_t size = 0u;
      uint8_t *bytes = read_fixture(fixture_directory, file, &size);
      ArcMediaInfo media; ArcLaunchDiscovery discovery;
      assert(arc_media_inspect(file, bytes, size, &media,
                               error, sizeof(error)) &&
             arc_launch_discover(bytes, size, &media, NULL, &discovery) &&
             discovery.candidate_count == 4u && discovery.target_index == 2 &&
             discovery.reason == ARC_LAUNCH_REASON_SINGLE_APPLICATION);
      uint8_t script[512]; size_t written = 0u;
      assert(arc_launch_script_build(&discovery, 2u, script,
                 sizeof(script) - 1u, &written, error, sizeof(error)));
      script[written] = 0u;
      assert(strstr((const char *)script,
                    kind ? "%Filer_Boot ADFS::0.$.Arc\xa0Games.!System\r" :
                           "%Filer_Boot ADFS::0.$.Folder.!System\r") &&
             strstr((const char *)script,
                    kind ? "%Filer_Run ADFS::0.$.Arc\xa0Games.!Target\r" :
                           "%Filer_Run ADFS::0.$.Folder.!Target\r") &&
             !strstr((const char *)script, ".Other.!Other") &&
             strstr((const char *)script,
                    "%Dir ADFS::0.$\r%URD ADFS::0.$\r"));
      free(bytes);
   }
   for (unsigned kind = 0u; kind < 2u; ++kind)
   {
      const char *file = kind ?
         "launch-family/nested-extended-literal-d.jfd" :
         "launch-family/nested-literal-d.jfd";
      size_t size = 0u;
      uint8_t *bytes = read_fixture(fixture_directory, file, &size);
      ArcJfdImage jfd; ArcLaunchDiscovery discovery;
      assert(arc_jfd_parse(bytes, size, &jfd, error, sizeof(error)) &&
             arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery) &&
             discovery.readable && discovery.nested_scope &&
             discovery.candidate_count == (kind ? 1u : 4u));
      arc_jfd_free(&jfd);
      free(bytes);
   }
}

static void test_make_old_map(uint8_t *image, size_t size, uint32_t old_size)
{
   memset(image, 0, size);
   image[252] = (uint8_t)old_size;
   image[253] = (uint8_t)(old_size >> 8u);
   image[254] = (uint8_t)(old_size >> 16u);
   image[509] = 3u;
   image[255] = test_old_map_checksum(image);
   image[511] = test_old_map_checksum(image + 256u);
}

static void test_short_media_policy(void)
{
   char error[192] = {0};
   ArcMediaInfo media;
   uint8_t *image = malloc(819200u);
   assert(image);

   /* OldBoot (&1fd) is independent of the free-list end pointer (&1fe).
    * Full malformed maps retain size fallback; short images require a valid
    * descriptor because their geometry cannot be inferred from length. */
   static const uint8_t free_ends[] = {3u, 1u, 247u};
   for (unsigned boot = 0u; boot < 4u; ++boot)
      for (unsigned end_case = 0u; end_case < sizeof(free_ends); ++end_case)
      {
         bool valid = end_case == 0u;
         test_make_old_map(image, 655360u, 2560u);
         image[509] = (uint8_t)boot;
         image[510] = free_ends[end_case];
         memcpy(image + 247u, "Dm\r\r\r", 5u);
         memcpy(image + 502u, "eo\r\r\r", 5u);
         image[255] = test_old_map_checksum(image);
         image[511] = test_old_map_checksum(image + 256u);
         assert(arc_media_inspect("matrix.adl", image, 655360u,
                                  &media, error, sizeof(error)));
         assert(media.recognition == (valid ? ARC_MEDIA_RECOGNITION_DESCRIPTOR :
                                               ARC_MEDIA_RECOGNITION_SIZE_FALLBACK));
         if (valid)
            assert(media.boot_option == boot && !strcmp(media.disc_name, "Demo"));
         assert(arc_media_inspect("matrix.adl", image, 655360u - 256u,
                                  &media, error, sizeof(error)) == valid);
         if (valid) assert(media.boot_option == boot && media.short_image);
      }

   assert(arc_test_make_e_format(image, 819200u));
   assert(arc_media_inspect("e.adf", image, 819200u - 5120u,
                            &media, error, sizeof(error)));
   assert(media.geometry == ARC_GEOMETRY_800K_E && media.short_image &&
          media.padding_required == 5120u && media.declared_size == 819200u);
   assert(arc_media_inspect("e.adf", image, 819200u - 1024u,
                            &media, error, sizeof(error)));
   assert(arc_media_inspect("e.adf", image, 819200u - 10240u,
                            &media, error, sizeof(error)));
   assert(arc_media_inspect("e.adf", image, 819200u - 20480u,
                            &media, error, sizeof(error)));
   assert(!arc_media_inspect("e.adf", image, 819200u - 256u,
                             &media, error, sizeof(error)));
   assert(strstr(error, "sector-aligned"));
   assert(!arc_media_inspect("e.adf", image, 819200u - 10241u,
                             &media, error, sizeof(error)));
   assert(!arc_media_inspect("e.adf", image, 819200u - 21504u,
                             &media, error, sizeof(error)));

   image[0] ^= 1u;
   assert(!arc_media_inspect("e.adf", image, 819200u - 1024u,
                             &media, error, sizeof(error)));
   assert(arc_media_inspect("e.adf", image, 819200u,
                            &media, error, sizeof(error)));
   assert(media.recognition == ARC_MEDIA_RECOGNITION_SIZE_FALLBACK);

   static const struct {
      size_t size;
      uint32_t old_size;
      size_t sector_size;
      size_t cap;
      ArcMediaGeometry geometry;
   } old_cases[] = {
      {327680u, 1280u, 256u, 4096u, ARC_GEOMETRY_320K},
      {655360u, 2560u, 256u, 8192u, ARC_GEOMETRY_640K_L},
      {819200u, 12u, 1024u, 20480u, ARC_GEOMETRY_800K_D},
      {819200u, 3180u, 1024u, 20480u, ARC_GEOMETRY_800K_D},
      {819200u, 3200u, 1024u, 20480u, ARC_GEOMETRY_800K_D},
   };
   for (size_t i = 0u; i < sizeof(old_cases) / sizeof(old_cases[0]); ++i)
   {
      test_make_old_map(image, old_cases[i].size, old_cases[i].old_size);
      assert(arc_media_inspect("old.adf", image,
         old_cases[i].size - old_cases[i].sector_size,
         &media, error, sizeof(error)));
      assert(media.geometry == old_cases[i].geometry &&
             media.padding_required == old_cases[i].sector_size);
      assert(arc_media_inspect("old.adf", image,
         old_cases[i].size - old_cases[i].cap,
         &media, error, sizeof(error)));
      assert(!arc_media_inspect("old.adf", image,
         old_cases[i].size - old_cases[i].sector_size - 1u,
         &media, error, sizeof(error)));
      assert(!arc_media_inspect("old.adf", image,
         old_cases[i].size - old_cases[i].cap - old_cases[i].sector_size,
         &media, error, sizeof(error)));
   }
   test_make_old_map(image, 655360u, 3199u);
   assert(arc_media_inspect("old.adl", image, 655360u - 256u,
                            &media, error, sizeof(error)));
   assert(media.geometry == ARC_GEOMETRY_640K_L &&
          media.recognition == ARC_MEDIA_RECOGNITION_DESCRIPTOR);
   test_make_old_map(image, 655360u, 3201u);
   assert(!arc_media_inspect("old.adl", image, 655360u - 256u,
                             &media, error, sizeof(error)));
   assert(arc_media_inspect("old.adl", image, 655360u,
                            &media, error, sizeof(error)));
   assert(media.geometry == ARC_GEOMETRY_640K_L &&
          media.recognition == ARC_MEDIA_RECOGNITION_SIZE_FALLBACK);
   free(image);
}

static void test_short_free_map(const char *directory)
{
   char error[256] = {0};
   ArcMediaInfo media;
   size_t size = 0u;
   uint8_t *image = read_fixture(directory,
      "short-family/800k-d-cap-plus-sector.adf", &size);
   assert(size == 819200u - 21504u);
   assert(arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          media.geometry == ARC_GEOMETRY_800K_D && media.free_map_accept);
   uint8_t original_map[512];
   memcpy(original_map, image, sizeof(original_map));
   /* The first missing 256-byte unit is 3116; the last is 3199. */
   test_short_d_free(image, original_map, 3116u, 84u, 0u, 0u);
   assert(arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          media.free_map_accept); /* exact edge */
   test_short_d_free(image, original_map, 3117u, 83u, 0u, 0u);
   assert(!arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "missing area free")); /* one allocated unit */
   test_short_d_free(image, original_map, 3116u, 83u, 0u, 0u);
   assert(!arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "missing area free")); /* free list ends one unit early */
   test_short_d_free(image, original_map, 3116u, 44u, 3160u, 40u);
   assert(arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          media.free_map_accept); /* abutting extents */
   test_short_d_free(image, original_map, 3116u, 44u, 3161u, 39u);
   assert(!arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "missing area free")); /* one allocated unit inside */
   test_short_d_free(image, original_map, 0u, 3200u, 0u, 0u);
   assert(!arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "overlaps map, boot block, or root"));
   test_short_d_free(image, original_map, 7u, 3193u, 0u, 0u);
   assert(!arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "overlaps map, boot block, or root"));
   memcpy(image, original_map, sizeof(original_map));
   /* The same valid checksums cannot prove an allocated tail. */
   image[256] = 7u;
   image[257] = 0u;
   image[258] = 0u;
   image[511] = test_old_map_checksum(image + 256u);
   assert(!arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "missing area free"));
   image[256] = image[257] = image[258] = 0u;
   image[511] = test_old_map_checksum(image + 256u);
   image[510] = 6u; /* A zero-length second free extent is malformed. */
   image[511] = test_old_map_checksum(image + 256u);
   assert(!arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "free list is malformed"));
   memcpy(image, original_map, sizeof(original_map));
   image[256] = 100u; image[257] = 0u; /* gap before the missing tail */
   image[511] = test_old_map_checksum(image + 256u);
   assert(!arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "missing area free"));
   memcpy(image, original_map, sizeof(original_map));
   image[510] = 6u;
   image[256] = 100u; image[257] = 0u;
   image[3] = 50u; image[4] = image[5] = 0u;
   image[259] = 0x4cu; image[260] = 0x0cu; image[261] = 0u;
   image[255] = test_old_map_checksum(image);
   image[511] = test_old_map_checksum(image + 256u);
   assert(!arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "free list is malformed")); /* overlapping */
   memcpy(image, original_map, sizeof(original_map));
   image[0] = 0x76u; image[1] = 0x0cu; image[2] = 0u; /* first=3190 */
   image[256] = 20u; image[257] = 0u; /* end past 3200 */
   image[255] = test_old_map_checksum(image);
   image[511] = test_old_map_checksum(image + 256u);
   assert(!arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "free list is malformed"));
   const char *failure = NULL;
   assert(!arc_short_free_map_proof(original_map, 4096u,
           ARC_GEOMETRY_800K_D, &failure) &&
          strstr(failure, "identification track-side"));
   free(image);
   image = read_fixture(directory, "short-family/800k-e-free-map.adf", &size);
   assert(arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          media.geometry == ARC_GEOMETRY_800K_E && media.free_map_accept);
   uint8_t original_e_map[1024];
   memcpy(original_e_map, image, sizeof(original_e_map));
   test_short_e_free(image, original_e_map, 6744u, 0u);
   assert(arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          media.free_map_accept); /* exact map-bit edge */
   test_short_e_free(image, original_e_map, 6745u, 0u);
   assert(!arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "missing area free")); /* one allocated map bit */
   test_short_e_free(image, original_e_map, 544u, 6744u);
   assert(arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          media.free_map_accept); /* abutting free fragments */
   test_short_e_free(image, original_e_map, 544u, 0u);
   for (size_t copy = 0u; copy <= 1024u; copy += 1024u)
   {
      test_short_e_bits(image + copy, 8u, 15u, 504u); /* free starts at map bit 512 */
      image[copy] = arc_test_e_format_zone_check(image + copy);
   }
   assert(!arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "overlaps map or boot block"));
   test_short_e_free(image, original_e_map, 528u, 0u);
   assert(!arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "root directory is missing")); /* free claims root */
   test_short_e_free(image, original_e_map, 544u, 0u);
   for (size_t copy = 0u; copy <= 1024u; copy += 1024u)
   {
      test_short_e_bit(image + copy, 6910u, 1u);
      test_short_e_bit(image + copy, 6911u, 0u);
      image[copy] = arc_test_e_format_zone_check(image + copy);
   }
   assert(!arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          error[0]); /* ends one bit early */
   memcpy(image, original_e_map, sizeof(original_e_map));
   memcpy(image + 1024u, original_e_map, sizeof(original_e_map));
   image[1024] ^= 1u; /* A valid primary does not excuse an invalid second map. */
   assert(!arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "map copy is invalid"));
   image[1024] ^= 1u;
   for (size_t copy = 0u; copy <= 1024u; copy += 1024u)
   {
      image[copy + 16u] = 0x00u; /* Root fragment 127 is absent from both maps. */
      image[copy + 17u] = 0x7fu;
      image[copy] = arc_test_e_format_zone_check(image + copy);
   }
   assert(!arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "root directory is missing"));
   free(image);
   image = read_fixture(directory, "short-family/800k-e-allocated-tail.adf", &size);
   assert(!arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "missing area free"));
   free(image);
   image = read_fixture(directory, "short-family/800k-e-cyclic-free.adf", &size);
   assert(!arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "free chain is cyclic"));
   free(image);
   image = read_fixture(directory, "short-family/800k-e-unterminated.adf", &size);
   assert(!arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "fragment does not terminate"));
   free(image);
   image = read_fixture(directory, "short-family/800k-e-conflicting-maps.adf", &size);
   assert(!arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "copies conflict"));
   free(image);
   image = read_fixture(directory, "short-family/800k-e-conflicting-allocation.adf", &size);
   assert(!arc_media_inspect("e.adf", image, size, &media, error, sizeof(error)) &&
          strstr(error, "copies conflict"));
   free(image);
   image = read_fixture(directory, "short-family/discovery-directory-full.adf", &size);
   assert(arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)));
   ArcLaunchDiscovery discovery;
   assert(arc_launch_discover(image, size, &media, NULL, &discovery) &&
          discovery.application_count == 1u && !discovery.scan_incomplete);
   assert(arc_launch_discover(image, size - 1024u, &media, NULL, &discovery) &&
          discovery.reason == ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE &&
          discovery.application_count == 0u);
   free(image);
   image = read_fixture(directory, "short-family/discovery-payload-tail.adf", &size);
   assert(arc_media_inspect("d.adf", image, size, &media, error, sizeof(error)) &&
          arc_launch_discover(image, size, &media, NULL, &discovery) &&
          discovery.application_count == 1u && !discovery.scan_incomplete);
   free(image);
}

static void test_adfs_recognition(void)
{
   uint8_t old_map[512] = {0};
   old_map[255] = test_old_map_checksum(old_map);
   old_map[511] = test_old_map_checksum(old_map + 256u);
   assert(arc_media_is_recognised_adfs(old_map, sizeof(old_map)));

   /* Independent vector captured from the second old-map block of the
    * project-authored L-format disc after its accepted RISC OS 3.11 mount.
    * Its stored check byte is 6; the former forward traversal produced 7. */
   uint8_t mounted_vector[256] = {0};
   mounted_vector[0] = 242u; mounted_vector[1] = 9u;
   memcpy(mounted_vector + 246u, "ROISO", 5u);
   mounted_vector[251] = 209u; mounted_vector[252] = 166u;
   mounted_vector[253] = 2u; mounted_vector[254] = 3u;
   assert(test_old_map_checksum(mounted_vector) == 6u);
   uint8_t mounted_image[512] = {0};
   mounted_image[255] = test_old_map_checksum(mounted_image);
   memcpy(mounted_image + 256u, mounted_vector, sizeof(mounted_vector));
   mounted_image[511] = 6u;
   assert(arc_media_is_recognised_adfs(mounted_image, sizeof(mounted_image)));

   size_t size = ARC_TEST_E_FORMAT_SIZE;
   uint8_t *e_format = calloc(1u, size);
   assert(e_format);
   assert(arc_test_make_e_format(e_format, size));
   uint8_t *record = e_format + 4u;
   assert(record[6] == 1u && record[10] == 0x20u && record[11] == 0x05u &&
          record[12] == 0x03u && record[13] == 0x02u &&
          (record[20] || record[21]) &&
          memcmp(record + 22u, "ArcDroidE1", 10u) == 0);
   assert(arc_media_is_recognised_adfs(e_format, size));
   assert(!arc_media_is_recognised_adfs(e_format, 63u));
   e_format[0] ^= 1u;
   assert(!arc_media_is_recognised_adfs(e_format, size));
   e_format[0] ^= 1u;
   e_format[3] = 0u;
   e_format[0] = arc_test_e_format_zone_check(e_format);
   assert(!arc_media_is_recognised_adfs(e_format, size));
   e_format[3] = 0xffu;
   record[4] = 12u;
   e_format[0] = arc_test_e_format_zone_check(e_format);
   assert(!arc_media_is_recognised_adfs(e_format, size));
   record[4] = 15u;
   record[8] = 0x80u;
   e_format[0] = arc_test_e_format_zone_check(e_format);
   assert(!arc_media_is_recognised_adfs(e_format, size));
   memset(e_format, 0xa5, size);
   assert(!arc_media_is_recognised_adfs(e_format, size));
   free(e_format);
}

static void test_playlists(void)
{
   ArcPlaylist *playlist = malloc(sizeof(*playlist));
   char error[256];
   unsigned line = 0u;
   assert(playlist);
   static const uint8_t basic[] =
      "\xef\xbb\xbf#EXTM3U\r\n# comment\r\n\r\n#EXTINF:1,First disc\r\n"
      "one.adf\r\ndir\\two.jfd\r\none.adf\r\n";
   assert(arc_playlist_detect("set.txt", basic, sizeof(basic) - 1u));
   assert(arc_playlist_parse("/sets/game/list.m3u", basic, sizeof(basic) - 1u,
                             false, playlist, &line, error, sizeof(error)));
   assert(playlist->count == 3u);
   assert(!strcmp(playlist->entries[0].path, "/sets/game/one.adf"));
   assert(!strcmp(playlist->entries[0].label, "First disc"));
   assert(!strcmp(playlist->entries[1].path, "/sets/game/dir\\two.jfd"));
   assert(!strcmp(playlist->entries[2].path, playlist->entries[0].path));

   static const uint8_t windows[] = "C:\\media\\one.adf\n\\\\server\\share\\two.adl\n";
   assert(arc_playlist_parse("C:\\sets\\list.m3u", windows, sizeof(windows) - 1u,
                             true, playlist, &line, error, sizeof(error)));
   assert(!strcmp(playlist->entries[0].path, "C:\\media\\one.adf"));
   assert(!strcmp(playlist->entries[1].path, "\\\\server\\share\\two.adl"));
   static const char *bad_paths[] = {
      "X:disc.adf\n", "\\disc.adf\n", "/dir/x.adf\n", "./disc.adf\n", "a/../disc.adf\n",
      "https://example/disc.adf\n", "nested.m3u\n"
   };
   for (unsigned i = 0u; i < sizeof(bad_paths) / sizeof(bad_paths[0]); ++i)
      assert(!arc_playlist_parse("C:\\sets\\list.m3u",
         (const uint8_t *)bad_paths[i], strlen(bad_paths[i]), true,
         playlist, &line, error, sizeof(error)) && line == 1u);
   static const char *zip_paths[] = {
      "game.zip\n", "game.ZIP#Disc1.adf\n"
   };
   for (unsigned i = 0u; i < sizeof(zip_paths) / sizeof(zip_paths[0]); ++i)
   {
      assert(!arc_playlist_parse("/sets/list.m3u",
         (const uint8_t *)zip_paths[i], strlen(zip_paths[i]), false,
         playlist, &line, error, sizeof(error)) && line == 1u);
      assert(strstr(error, "zip entries are unsupported in playlists"));
   }
   static const uint8_t saf[] = "saf://opaque.identifier/disc.adf\n";
   assert(arc_playlist_parse("saf://playlist/list.m3u", saf, sizeof(saf) - 1u,
                             false, playlist, &line, error, sizeof(error)));
   assert(!strcmp(playlist->entries[0].path, "saf://opaque.identifier/disc.adf"));

   char entries[1024] = {0};
   size_t used = 0u;
   for (unsigned i = 0u; i < 32u; ++i)
      used += (size_t)snprintf(entries + used, sizeof(entries) - used, "d%u.adf\n", i);
   assert(arc_playlist_parse("/x/list.m3u", (uint8_t *)entries, used, false,
                             playlist, &line, error, sizeof(error)) && playlist->count == 32u);
   used += (size_t)snprintf(entries + used, sizeof(entries) - used, "overflow.adf\n");
   assert(!arc_playlist_parse("/x/list.m3u", (uint8_t *)entries, used, false,
                              playlist, &line, error, sizeof(error)) && line == 33u);

   char *long_path = malloc(4097u);
   assert(long_path);
   memset(long_path, 'a', 4090u); memcpy(long_path + 4090u, ".adf\n", 5u);
   assert(arc_playlist_parse("/list.m3u", (uint8_t *)long_path, 4095u, false,
                             playlist, &line, error, sizeof(error)));
   memset(long_path, 'a', 4091u); memcpy(long_path + 4091u, ".adf\n", 5u);
   assert(!arc_playlist_parse("/list.m3u", (uint8_t *)long_path, 4096u, false,
                              playlist, &line, error, sizeof(error)));
   free(long_path);

   char labels[300];
   memcpy(labels, "#EXTINF:0,", 10u); memset(labels + 10u, 'L', 127u);
   memcpy(labels + 137u, "\ndisc.adf\n", 11u);
   assert(arc_playlist_parse("list.m3u", (uint8_t *)labels, 147u, false,
                             playlist, &line, error, sizeof(error)));
   labels[137] = 'L'; labels[138] = '\n'; memcpy(labels + 139, "disc.adf\n", 9u);
   assert(!arc_playlist_parse("list.m3u", (uint8_t *)labels, 148u, false,
                              playlist, &line, error, sizeof(error)) && line == 1u);
   uint8_t *oversize = calloc(1u, ARC_PLAYLIST_MAX_BYTES + 1u);
   assert(oversize && !arc_playlist_parse("list.m3u", oversize,
      ARC_PLAYLIST_MAX_BYTES + 1u, false, playlist, &line, error, sizeof(error)) && line == 0u);
   free(oversize);
   static const uint8_t nul[] = {'d','i','s','c',0,'.','a','d','f','\n'};
   assert(!arc_playlist_parse("list.m3u", nul, sizeof(nul), false,
                              playlist, &line, error, sizeof(error)));
   static const uint8_t empty[] = "#EXTM3U\n# only comments\n";
   assert(!arc_playlist_parse("list.m3u", empty, sizeof(empty) - 1u, false,
                              playlist, &line, error, sizeof(error)) && line == 0u);
   static const uint8_t spaced[] = "#EXTM3U \t\n \tdisc.adf\t \n";
   assert(arc_playlist_detect("set.txt", spaced, sizeof(spaced) - 1u));
   assert(arc_playlist_parse("/x/list.m3u", spaced, sizeof(spaced) - 1u,
                             false, playlist, &line, error, sizeof(error)));
   assert(playlist->count == 1u &&
          !strcmp(playlist->entries[0].path, "/x/disc.adf"));
   free(playlist);
}

static void make_folder_boundary_path(char path[ARC_MEDIA_PATH_CAPACITY],
                                      size_t directory_size)
{
   static const char leaf[] = "Same.adf";
   assert(directory_size > sizeof(leaf) + 1u);
   size_t filler = directory_size - sizeof(leaf) - 1u;
   path[0] = '/';
   memset(path + 1u, 'p', filler);
   path[1u + filler] = '/';
   memcpy(path + 2u + filler, leaf, sizeof(leaf) - 1u);
   path[directory_size] = '/';
   memcpy(path + directory_size + 1u, leaf, sizeof(leaf));
}

static void test_folder_sets(void)
{
   ArcFolderSet *set = malloc(sizeof(*set));
   assert(set);
   assert(arc_folder_set_begin("/sets/Qaventra.adf/Qaventra.adf", false,
                               NULL) == ARC_FOLDER_SET_READY);
   assert(arc_folder_set_begin("/sets/Other.adf/Qaventra.adf", false,
                               NULL) == ARC_FOLDER_SET_NONE);
   assert(arc_folder_set_begin("/sets/Qaventra.adf/Qaventra.adf", false,
                               set) == ARC_FOLDER_SET_READY);
   assert(!strcmp(set->directory, "/sets/Qaventra.adf") &&
          !strcmp(set->selected_name, "Qaventra.adf") && set->separator == '/');
   assert(arc_folder_set_add(set, "qaventra.adf", true) ==
          ARC_FOLDER_MEMBER_ADDED);
   assert(arc_folder_set_begin("C:\\sets\\Qaventra.adf\\Qaventra.adf", false,
                               set) == ARC_FOLDER_SET_READY && set->separator == '\\');
   assert(arc_folder_set_begin("/sets/Qaventra.adf/qaventra.adf", false,
                               set) == ARC_FOLDER_SET_NONE);
   assert(arc_folder_set_begin("C:\\sets\\Qaventra.ADF\\qaventra.adf", true,
                               set) == ARC_FOLDER_SET_READY);
   assert(arc_folder_set_add(set, "QAVENTRA.ADF", true) ==
          ARC_FOLDER_MEMBER_IGNORED);
   assert(arc_folder_set_begin("/sets/Other.adf/Qaventra.adf", false, set) ==
          ARC_FOLDER_SET_NONE);
   assert(arc_folder_set_begin("/sets/Qaventra.adf", false, set) ==
          ARC_FOLDER_SET_NONE);
   assert(arc_folder_set_begin("content://sets/Qaventra.adf/Qaventra.adf", false,
                               set) == ARC_FOLDER_SET_NONE);
   assert(arc_folder_set_begin("saf://sets/Qaventra.adf/Qaventra.adf", false,
                               set) == ARC_FOLDER_SET_NONE);

   assert(arc_folder_set_begin("/sets/Set.adf/Set.adf", false, set) ==
          ARC_FOLDER_SET_READY);
   static const char *const unordered[] = {
      "Set 10.adf", "set 2.ADF", "Set 1.jfd", "Set 01.adf", "set 3.adl",
      "Set 20.adf", "Set 11.adf", "set 9.adf", "Set 4.adf", "Set 02.adf"
   };
   for (unsigned i = 0u; i < 10u; ++i)
      assert(arc_folder_set_add(set, unordered[i], true) == ARC_FOLDER_MEMBER_ADDED);
   assert(arc_folder_set_add(set, ".Set 0.adf", true) == ARC_FOLDER_MEMBER_IGNORED);
   assert(arc_folder_set_add(set, "Set 5.m3u", true) == ARC_FOLDER_MEMBER_IGNORED);
   assert(arc_folder_set_add(set, "Set 6.txt", true) == ARC_FOLDER_MEMBER_IGNORED);
   assert(arc_folder_set_add(set, "Set 7.adf", false) == ARC_FOLDER_MEMBER_IGNORED);
   assert(arc_folder_set_add(set, "Set.adf", true) == ARC_FOLDER_MEMBER_IGNORED);
   arc_folder_set_sort(set);
   static const char *const ordered[] = {
      "Set 01.adf", "Set 1.jfd", "Set 02.adf", "set 2.ADF", "set 3.adl",
      "Set 4.adf", "set 9.adf", "Set 10.adf", "Set 11.adf", "Set 20.adf"
   };
   for (unsigned i = 0u; i < 10u; ++i)
      assert(!strcmp(set->siblings[i], ordered[i]));
   char resolved[ARC_MEDIA_PATH_CAPACITY];
   assert(arc_folder_set_path(set, 0u, resolved) &&
          !strcmp(resolved, "/sets/Set.adf/Set 01.adf"));

   assert(arc_folder_set_begin("Root.adf/Root.adf", false, set) ==
          ARC_FOLDER_SET_READY);
   char sibling[64];
   for (unsigned i = 0u; i < ARC_FOLDER_SET_MAX_SIBLINGS; ++i)
   {
      snprintf(sibling, sizeof(sibling), "Sibling %u.adf", i);
      assert(arc_folder_set_add(set, sibling, true) == ARC_FOLDER_MEMBER_ADDED);
   }
   assert(set->count == 31u && !set->overflow);
   assert(arc_folder_set_add(set, "Sibling 31.adf", true) ==
          ARC_FOLDER_MEMBER_LIMIT_EXCEEDED);
   assert(set->overflow && set->qualified_count == 32u);

   char boundary[ARC_MEDIA_PATH_CAPACITY];
   make_folder_boundary_path(boundary, 1021u);
   assert(arc_folder_set_begin(boundary, false, set) == ARC_FOLDER_SET_READY &&
          strlen(set->directory) == 1021u);
   make_folder_boundary_path(boundary, 1022u);
   assert(arc_folder_set_begin(boundary, false, set) ==
          ARC_FOLDER_SET_DIRECTORY_TOO_LONG && strlen(set->directory) == 1022u);
   free(set);
}

static void store_u32(uint8_t *p, uint32_t value)
{
   p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8u);
   p[2] = (uint8_t)(value >> 16u); p[3] = (uint8_t)(value >> 24u);
}

static void make_plain_jfd(uint8_t image[452])
{
   memset(image, 0, 452u);
   memcpy(image, "JFDI", 4u); store_u32(image + 8u, 452u);
   store_u32(image + 24u, 304u); store_u32(image + 28u, 312u);
   store_u32(image + 32u, 324u); memcpy(image + 48u, "Test", 4u);
   store_u32(image + 304u, 0u); store_u32(image + 308u, UINT32_MAX);
   store_u32(image + 312u, 1u << 16u); store_u32(image + 316u, 0u);
   store_u32(image + 320u, UINT32_MAX);
   for (unsigned i = 0u; i < 128u; ++i) image[324u + i] = (uint8_t)i;
}

static void test_jfd_parser(const char *fixture_directory)
{
   static const uint8_t gzip_jfd[] = {
      0x1f,0x8b,0x08,0x00,0x00,0x00,0x00,0x00,0x02,0xff,0xf3,0x72,0x73,0xf1,0x64,0x00,
      0x82,0x23,0x8c,0x0c,0x28,0xc0,0x00,0xc8,0xb7,0x00,0x62,0x17,0x34,0xf1,0x90,0xd4,
      0xe2,0x12,0x86,0x11,0x0e,0xfe,0x03,0x01,0x03,0x03,0x23,0x82,0xcd,0xc8,0xc4,0xcc,
      0xc2,0xca,0xc6,0xce,0xc1,0xc9,0xc5,0xcd,0xc3,0xcb,0xc7,0x2f,0x20,0x28,0x24,0x2c,
      0x22,0x2a,0x26,0x2e,0x21,0x29,0x25,0x2d,0x23,0x2b,0x27,0xaf,0xa0,0xa8,0xa4,0xac,
      0xa2,0xaa,0xa6,0xae,0xa1,0xa9,0xa5,0xad,0xa3,0xab,0xa7,0x6f,0x60,0x68,0x64,0x6c,
      0x62,0x6a,0x66,0x6e,0x61,0x69,0x65,0x6d,0x63,0x6b,0x67,0xef,0xe0,0xe8,0xe4,0xec,
      0xe2,0xea,0xe6,0xee,0xe1,0xe9,0xe5,0xed,0xe3,0xeb,0xe7,0x1f,0x10,0x18,0x14,0x1c,
      0x12,0x1a,0x16,0x1e,0x11,0x19,0x15,0x1d,0x13,0x1b,0x17,0x9f,0x90,0x98,0x94,0x9c,
      0x92,0x9a,0x96,0x9e,0x91,0x99,0x95,0x9d,0x93,0x9b,0x97,0x5f,0x50,0x58,0x54,0x5c,
      0x52,0x5a,0x56,0x5e,0x51,0x59,0x55,0x5d,0x53,0x5b,0x57,0x0f,0x00,0x82,0xbf,0x6f,
      0x5b,0xc4,0x01,0x00,0x00
   };
   uint8_t plain[452];
   char error[256];
   ArcJfdImage parsed;
   make_plain_jfd(plain);
   assert(arc_jfd_parse(plain, sizeof(plain), &parsed, error, sizeof(error)));
   assert(parsed.track_count == 2u && parsed.sector_count == 1u &&
          parsed.sectors[0].size == 128u && parsed.sectors[0].data_offset == 324u &&
          !strcmp(parsed.title, "Test"));
   arc_jfd_free(&parsed);
   assert(arc_jfd_parse(gzip_jfd, sizeof(gzip_jfd), &parsed, error, sizeof(error)));
   arc_jfd_free(&parsed);
   uint8_t corrupt_gzip[sizeof(gzip_jfd)];
   memcpy(corrupt_gzip, gzip_jfd, sizeof(gzip_jfd));
   corrupt_gzip[sizeof(corrupt_gzip) - 8u] ^= 1u;
   assert(!arc_jfd_parse(corrupt_gzip, sizeof(corrupt_gzip), &parsed, error, sizeof(error)));
   memcpy(corrupt_gzip, gzip_jfd, sizeof(gzip_jfd));
   corrupt_gzip[sizeof(corrupt_gzip) - 4u] ^= 1u;
   assert(!arc_jfd_parse(corrupt_gzip, sizeof(corrupt_gzip), &parsed, error, sizeof(error)));
   memcpy(corrupt_gzip, gzip_jfd, sizeof(gzip_jfd));
   corrupt_gzip[10] ^= 1u;
   assert(!arc_jfd_parse(corrupt_gzip, sizeof(corrupt_gzip), &parsed, error, sizeof(error)));
   static const uint8_t gzip_optional_fields[] = "fixture.jfd\0ArcDroid";
   uint8_t gzip_headers[sizeof(gzip_jfd) + sizeof(gzip_optional_fields)];
   memcpy(gzip_headers, gzip_jfd, 10u);
   gzip_headers[3] = 0x18u; /* ordinary FNAME + FCOMMENT variation */
   gzip_headers[4] = 0x78u; gzip_headers[9] = 0x03u; /* timestamp + OS */
   memcpy(gzip_headers + 10u, gzip_optional_fields, sizeof(gzip_optional_fields));
   memcpy(gzip_headers + 10u + sizeof(gzip_optional_fields), gzip_jfd + 10u,
          sizeof(gzip_jfd) - 10u);
   assert(arc_jfd_parse(gzip_headers, sizeof(gzip_headers),
                        &parsed, error, sizeof(error)));
   arc_jfd_free(&parsed);
   uint8_t trailing[sizeof(gzip_jfd) + 1u];
   memcpy(trailing, gzip_jfd, sizeof(gzip_jfd)); trailing[sizeof(gzip_jfd)] = 0u;
   assert(!arc_jfd_parse(trailing, sizeof(trailing), &parsed, error, sizeof(error)));
   uint8_t two_members[sizeof(gzip_jfd) * 2u];
   memcpy(two_members, gzip_jfd, sizeof(gzip_jfd));
   memcpy(two_members + sizeof(gzip_jfd), gzip_jfd, sizeof(gzip_jfd));
   assert(!arc_jfd_parse(two_members, sizeof(two_members),
                         &parsed, error, sizeof(error)) && strstr(error, "declared"));

   size_t multi_size = 0u;
   uint8_t *multi = read_fixture(fixture_directory,
      "arcdroid-test-two-member.jfd", &multi_size);
   assert(multi_size > 36u && multi[9] == 13u);
   assert(arc_jfd_parse(multi, multi_size, &parsed, error, sizeof(error)));
   assert(parsed.extended && parsed.decoded_size == 592u);
   arc_jfd_free(&parsed);
   assert(!arc_jfd_parse(multi, multi_size - 5u, &parsed, error, sizeof(error)) &&
          strstr(error, "truncated"));
   uint8_t *garbage = malloc(multi_size + 1u);
   assert(garbage);
   memcpy(garbage, multi, multi_size); garbage[multi_size] = 0u;
   assert(!arc_jfd_parse(garbage, multi_size + 1u, &parsed, error, sizeof(error)) &&
          strstr(error, "trailing"));
   free(garbage);
   free(multi);
   multi = read_fixture(fixture_directory,
      "arcdroid-test-three-member.jfd", &multi_size);
   assert(arc_jfd_parse(multi, multi_size, &parsed, error, sizeof(error)));
   assert(parsed.extended && parsed.decoded_size == 592u);
   arc_jfd_free(&parsed);
   free(multi);

   uint8_t *owned = malloc(sizeof(plain));
   assert(owned);
   memcpy(owned, plain, sizeof(plain));
   assert(arc_jfd_parse_owned(owned, sizeof(plain), &parsed, error, sizeof(error)));
   arc_jfd_free(&parsed);

   uint8_t rejected[452];
   memcpy(rejected, plain, sizeof(rejected)); store_u32(rejected + 36u, 4u);
   assert(!arc_jfd_parse(rejected, sizeof(rejected), &parsed, error, sizeof(error)) && strstr(error, "delta"));
   memcpy(rejected, plain, sizeof(rejected)); store_u32(rejected + 312u, 8u << 16u);
   assert(arc_jfd_parse(rejected, sizeof(rejected), &parsed, error, sizeof(error)) &&
          parsed.tracks[0].quarantine_reason &&
          !strcmp(parsed.tracks[0].quarantine_reason, "octal-density"));
   arc_jfd_free(&parsed);
   memcpy(rejected, plain, sizeof(rejected)); store_u32(rejected + 312u, (1u << 20u) | (1u << 16u));
   assert(arc_jfd_parse(rejected, sizeof(rejected), &parsed, error, sizeof(error)) &&
          parsed.tracks[0].quarantine_reason &&
          !strcmp(parsed.tracks[0].quarantine_reason, "whole-track"));
   arc_jfd_free(&parsed);
   memcpy(rejected, plain, sizeof(rejected)); store_u32(rejected + 320u, 0u);
   assert(!arc_jfd_parse(rejected, sizeof(rejected), &parsed, error, sizeof(error)) &&
          (strstr(error, "truncated") || strstr(error, "unterminated")));

   static const uint32_t boundaries[] = {0u, 303u, 304u, 312u, 324u,
                                         452u, 453u, UINT32_MAX};
   static const unsigned offset_fields[] = {24u, 28u, 32u, 36u, 40u, 44u};
   for (size_t field = 0u; field < sizeof(offset_fields) / sizeof(offset_fields[0]); ++field)
      for (size_t value = 0u; value < sizeof(boundaries) / sizeof(boundaries[0]); ++value)
      {
         memcpy(rejected, plain, sizeof(rejected));
         store_u32(rejected + offset_fields[field], boundaries[value]);
         if (arc_jfd_parse(rejected, sizeof(rejected), &parsed, error, sizeof(error)))
            arc_jfd_free(&parsed);
      }

   uint8_t extended[468];
   memset(extended, 0, sizeof(extended));
   memcpy(extended, "JFDI", 4u); store_u32(extended + 8u, sizeof(extended));
   store_u32(extended + 24u, 316u); store_u32(extended + 28u, 324u);
   store_u32(extended + 32u, 336u); store_u32(extended + 312u, 4u);
   store_u32(extended + 316u, 0u); store_u32(extended + 320u, UINT32_MAX);
   store_u32(extended + 324u, 1u << 16u); store_u32(extended + 328u, 0u);
   store_u32(extended + 332u, UINT32_MAX);
   assert(arc_jfd_parse(extended, sizeof(extended), &parsed, error, sizeof(error)) &&
          parsed.extended && parsed.obey_length == 4u);
   arc_jfd_free(&parsed);
   store_u32(extended + 312u, 5u);
   assert(!arc_jfd_parse(extended, sizeof(extended), &parsed, error, sizeof(error)) &&
          strstr(error, "payload"));
   store_u32(extended + 312u, UINT32_MAX);
   assert(!arc_jfd_parse(extended, sizeof(extended), &parsed, error, sizeof(error)) &&
          strstr(error, "obey"));

   uint8_t *at_cap = calloc(1u, ARC_JFD_MAX_DECODED + 1u);
   assert(at_cap);
   make_plain_jfd(at_cap);
   store_u32(at_cap + 8u, ARC_JFD_MAX_DECODED);
   assert(arc_jfd_parse(at_cap, ARC_JFD_MAX_DECODED, &parsed, error, sizeof(error)));
   arc_jfd_free(&parsed);
   store_u32(at_cap + 8u, ARC_JFD_MAX_DECODED + 1u);
   assert(!arc_jfd_parse(at_cap, ARC_JFD_MAX_DECODED + 1u,
                         &parsed, error, sizeof(error)) && strstr(error, "8 MiB"));
   free(at_cap);

   uint32_t random = UINT32_C(0x4a464432);
   for (unsigned iteration = 0u; iteration < 2000u; ++iteration)
   {
      random = random * UINT32_C(1664525) + UINT32_C(1013904223);
      bool gzip = (iteration & 1u) != 0u;
      size_t size = gzip ? sizeof(gzip_jfd) : sizeof(plain);
      uint8_t *mutation = malloc(size);
      assert(mutation);
      memcpy(mutation, gzip ? gzip_jfd : plain, size);
      unsigned changes = 1u + (random & 3u);
      for (unsigned change = 0u; change < changes; ++change)
      {
         random = random * UINT32_C(1664525) + UINT32_C(1013904223);
         size_t offset = random % size;
         mutation[offset] ^= (uint8_t)(1u << ((random >> 16u) & 7u));
      }
      if (arc_jfd_parse(mutation, size, &parsed, error, sizeof(error)))
      {
         assert(parsed.decoded_size <= ARC_JFD_MAX_DECODED);
         assert(parsed.track_count >= 2u && parsed.track_count <= ARC_JFD_MAX_TRACK_SIDES);
         for (size_t i = 0u; i < parsed.sector_count; ++i)
            assert(parsed.sectors[i].size >= 128u && parsed.sectors[i].size <= 1024u);
         arc_jfd_free(&parsed);
      }
      free(mutation);
   }
}

static void test_input(void)
{
   ArcInputState state;
   arc_input_reset(&state);
   ArcInputEvent event;
   arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, 42u, true, true);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_PAD0, 42u, true, true);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, 42u, false, true);
   assert(arc_input_pending(&state) == 1u);
   assert(arc_input_consume(&state, true, &event));
   assert(event.type == ARC_INPUT_EVENT_KEY && event.code == 42u && event.down);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_PAD0, 42u, false, true);
   assert(arc_input_consume(&state, true, &event) && !event.down);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, 500u, true, false);
   assert(!arc_input_pending(&state));

   arc_input_source_buttons(&state, ARC_INPUT_SOURCE_PAD0, 1u);
   arc_input_source_buttons(&state, ARC_INPUT_SOURCE_MOUSE1, 1u);
   arc_input_source_buttons(&state, ARC_INPUT_SOURCE_PAD0, 0u);
   assert(arc_input_pending(&state) == 1u);
   assert(arc_input_consume(&state, true, &event) && event.down);
   arc_input_source_buttons(&state, ARC_INPUT_SOURCE_MOUSE1, 0u);
   assert(arc_input_consume(&state, true, &event) && !event.down);

   arc_input_motion(&state, 150, -90, false);
   int total_x = 0, total_y = 0;
   unsigned packets = 0u;
   while (arc_input_pending(&state))
   {
      assert(arc_input_consume(&state, true, &event));
      assert(event.type == ARC_INPUT_EVENT_MOTION);
      assert(event.dx >= -63 && event.dx <= 63 && event.dy >= -63 && event.dy <= 63);
      total_x += (int)event.dx; total_y += (int)event.dy; ++packets;
   }
   assert(total_x == 150 && total_y == -90 && packets == 3u);

   arc_input_motion(&state, 150, 0, false);
   arc_input_source_buttons(&state, ARC_INPUT_SOURCE_MOUSE1, 1u);
   arc_input_motion(&state, 70, 0, false);
   arc_input_source_buttons(&state, ARC_INPUT_SOURCE_MOUSE1, 0u);
   total_x = 0;
   while (arc_input_pending(&state))
   {
      assert(arc_input_consume(&state, true, &event));
      if (event.type == ARC_INPUT_EVENT_MOTION) total_x += (int)event.dx;
      else if (event.down) assert(total_x == 150);
      else assert(total_x == 220);
   }
   assert(total_x == 220);

   arc_input_motion(&state, 10, 0, true);
   arc_input_motion(&state, 20, -80, true);
   assert(arc_input_pending(&state) == 1u);
   assert(arc_input_consume(&state, true, &event));
   assert(event.type == ARC_INPUT_EVENT_MOTION && event.velocity &&
          event.dx == 20 && event.dy == -63 && !arc_input_pending(&state));

   arc_input_reset(&state);
   arc_input_motion(&state, 30, -20, false);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, 42u, true, true);
   assert(arc_input_pending(&state) == 2u);
   assert(arc_input_consume(&state, false, &event));
   assert(event.type == ARC_INPUT_EVENT_KEY && event.code == 42u && event.down);
   assert(!arc_input_pending(&state));

   arc_input_reset(&state);
   arc_input_motion(&state, 5, 6, true);
   assert(arc_input_pending(&state) == 1u);
   assert(!arc_input_consume(&state, false, &event));
   assert(!arc_input_pending(&state));

   for (unsigned code = 1u; code <= 9u; ++code)
      arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, code, true, true);
   arc_input_motion(&state, 1, 1, false);
   for (unsigned code = 10u; code <= 20u; ++code)
      arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, code, true, true);
   unsigned discrete = 0u;
   while (arc_input_pending(&state))
   {
      assert(arc_input_consume(&state, true, &event));
      if (event.type == ARC_INPUT_EVENT_MOTION)
      {
         assert(discrete <= ARC_INPUT_MOTION_STARVATION_BOUND);
         break;
      }
      ++discrete;
   }

   arc_input_reset(&state);
   for (unsigned i = 0u; i < ARC_INPUT_FIFO_CAPACITY / 2u; ++i)
   {
      arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, 42u, true, true);
      arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, 42u, false, true);
   }
   assert(arc_input_pending(&state) == ARC_INPUT_FIFO_CAPACITY);
   arc_input_source_buttons(&state, ARC_INPUT_SOURCE_PAD0, 7u);
   assert(arc_input_take_overflow_report(&state));
   unsigned button_downs[8] = {0};
   while (arc_input_pending(&state))
   {
      assert(arc_input_consume(&state, true, &event));
      assert(event.type == ARC_INPUT_EVENT_BUTTON && event.down && event.code < 8u);
      ++button_downs[event.code];
   }
   assert(button_downs[1] == 1u && button_downs[2] == 1u &&
          button_downs[4] == 1u);

   arc_input_reset(&state);
   for (unsigned i = 0u; i < 300u; ++i)
      arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, 42u,
                           (i & 1u) == 0u, true);
   assert(arc_input_take_overflow_report(&state));
   assert(!arc_input_take_overflow_report(&state));
   while (arc_input_pending(&state)) assert(arc_input_consume(&state, true, &event));
   assert(!state.applied_keys[42u]);

   arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, 42u, true, true);
   assert(arc_input_consume(&state, true, &event) && event.down);
   arc_input_release_all(&state);
   assert(arc_input_consume(&state, true, &event) && !event.down);
}

static void test_input_mapping(void)
{
   size_t key_count = 0u;
   const ArcInputKey *keys = arc_input_supported_keys(&key_count);
   assert(keys && key_count == 97u && key_count + 3u <= 128u);
   for (size_t i = 0u; i < key_count; ++i)
   {
      unsigned engine_row = UINT_MAX, engine_column = UINT_MAX;
      assert(keys[i].value && keys[i].label && keys[i].key < ARC_INPUT_KEY_COUNT);
      assert(keys[i].kart_row <= 6u && keys[i].kart_column <= 15u);
      assert(arc_test_keytable_position(keys[i].key, &engine_row,
                                        &engine_column));
      assert(keys[i].kart_row == engine_row &&
             keys[i].kart_column == engine_column);
      assert(arc_input_key_value(keys[i].value) == &keys[i]);
      assert(arc_input_is_modifier(keys[i].key) == keys[i].modifier);
      for (size_t prior = 0u; prior < i; ++prior)
         assert(strcmp(keys[i].value, keys[prior].value));
   }
   assert(!arc_input_key_value("pound"));
   const ArcInputKey *backslash = arc_input_key_value("backslash");
   assert(!arc_input_key_value("hash_backslash"));
   assert(backslash && !strcmp(backslash->label, "Backslash") &&
          backslash->kart_row == 3u && backslash->kart_column == 3u);
   const ArcInputKey *keypad_hash = arc_input_key_value("kp_hash");
   assert(!arc_input_key_value("kp_minus"));
   assert(keypad_hash && keypad_hash->key == RETROK_KP_MINUS &&
          !strcmp(keypad_hash->label, "Keypad #") &&
          keypad_hash->kart_row == 2u && keypad_hash->kart_column == 5u);
   assert(!arc_input_key(RETROK_PRINT));
   size_t osk_only_count = 0u;
   const ArcInputKey *osk_only = arc_input_osk_only_keys(&osk_only_count);
   static const unsigned osk_codes[] = {
      RETROK_NUMLOCK, RETROK_SCROLLOCK, RETROK_PAUSE
   };
   assert(osk_only && osk_only_count == 3u);
   for (size_t i = 0u; i < osk_only_count; ++i)
   {
      unsigned engine_row = UINT_MAX, engine_column = UINT_MAX;
      assert(osk_only[i].key == osk_codes[i] && !osk_only[i].modifier);
      assert(arc_test_keytable_position(osk_only[i].key, &engine_row,
                                        &engine_column));
      assert(osk_only[i].kart_row == engine_row &&
             osk_only[i].kart_column == engine_column);
   }
   assert(!strcmp(osk_only[2].value, "break") &&
          !strcmp(osk_only[2].label, "Break") &&
          osk_only[2].kart_row == 0u && osk_only[2].kart_column == 15u);
   for (size_t i = 0u; i < osk_only_count; ++i)
      assert(strcmp(osk_only[i].value, "pause"));
   unsigned omitted_row = UINT_MAX, omitted_column = UINT_MAX;
   assert(!arc_test_keytable_position(RETROK_PRINT, &omitted_row,
                                      &omitted_column));
   ArcInputOverride overrides[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT] = {{{0}}};
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
         overrides[port][input].automatic = true;
   ArcInputResolution resolution;
   assert(arc_input_resolve(overrides, &resolution));
   assert(!resolution.custom);
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
      {
         assert(resolution.source[port][input] == ARC_MAPPING_SOURCE_DEFAULT);
         assert(resolution.binding[port][input].bound ==
                arc_input_generic_mapping()[input].bound);
         assert(resolution.binding[port][input].key ==
                arc_input_generic_mapping()[input].key);
      }
   overrides[0][ARC_PAD_B].automatic = false;
   overrides[0][ARC_PAD_B].binding = (ArcInputBinding){false, 0u};
   overrides[1][ARC_PAD_A].automatic = false;
   overrides[1][ARC_PAD_A].binding = (ArcInputBinding){true, RETROK_LSHIFT};
   assert(arc_input_resolve(overrides, &resolution) && resolution.custom);
   assert(!resolution.binding[0][ARC_PAD_B].bound &&
          resolution.source[0][ARC_PAD_B] == ARC_MAPPING_SOURCE_OPTION);
   assert(resolution.binding[1][ARC_PAD_A].key == RETROK_LSHIFT &&
          resolution.source[1][ARC_PAD_A] == ARC_MAPPING_SOURCE_OPTION);
   assert(resolution.binding[0][ARC_PAD_A].key == RETROK_SPACE &&
          resolution.source[0][ARC_PAD_A] == ARC_MAPPING_SOURCE_DEFAULT);
   assert(resolution.binding[1][ARC_PAD_B].key == RETROK_RETURN &&
          resolution.source[1][ARC_PAD_B] == ARC_MAPPING_SOURCE_DEFAULT);
   assert(!arc_input_resolve(overrides, NULL));

   ArcInputState state;
   ArcInputEvent event;
   bool port0[ARC_INPUT_KEY_COUNT] = {false};
   bool port1[ARC_INPUT_KEY_COUNT] = {false};
   arc_input_reset(&state);
   port0[RETROK_SPACE] = true;
   port1[RETROK_SPACE] = true;
   port1[RETROK_LSHIFT] = true;
   arc_input_pad_key_sets(&state, port0, port1);
   assert(arc_input_consume(&state, true, &event) &&
          event.code == RETROK_LSHIFT && event.down);
   assert(arc_input_consume(&state, true, &event) &&
          event.code == RETROK_SPACE && event.down);
   memset(port0, 0, sizeof(port0));
   arc_input_pad_key_sets(&state, port0, port1);
   assert(!arc_input_pending(&state));
   memset(port1, 0, sizeof(port1));
   arc_input_pad_key_sets(&state, port0, port1);
   assert(arc_input_consume(&state, true, &event) &&
          event.code == RETROK_SPACE && !event.down);
   assert(arc_input_consume(&state, true, &event) &&
          event.code == RETROK_LSHIFT && !event.down);

   uint64_t generation = arc_input_reconcile_generation(&state);
   for (unsigned i = 0u; i <= ARC_INPUT_FIFO_CAPACITY; ++i)
      arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, RETROK_a,
                           (i & 1u) == 0u, true);
   assert(arc_input_reconcile_generation(&state) > generation);

   /* A reconcile in the middle of a pad-key batch rebuilds the desired set
    * once; no stale tail of the old batch may be appended afterwards. */
   arc_input_reset(&state);
   state.fifo_count = ARC_INPUT_FIFO_CAPACITY - 1u;
   memset(port0, 0, sizeof(port0));
   memset(port1, 0, sizeof(port1));
   port0[RETROK_a] = true;
   port0[RETROK_b] = true;
   port0[RETROK_c] = true;
   generation = arc_input_reconcile_generation(&state);
   arc_input_pad_key_sets(&state, port0, port1);
   assert(arc_input_reconcile_generation(&state) == generation + 1u);
   unsigned seen_a = 0u, seen_b = 0u, seen_c = 0u;
   while (arc_input_consume(&state, true, &event))
   {
      assert(event.type == ARC_INPUT_EVENT_KEY && event.down);
      if (event.code == RETROK_a) ++seen_a;
      else if (event.code == RETROK_b) ++seen_b;
      else if (event.code == RETROK_c) ++seen_c;
      else assert(false);
   }
   assert(seen_a == 1u && seen_b == 1u && seen_c == 1u);

   /* Source teardown preserves chord meaning: ordinary keys are released
    * before their modifiers regardless of numeric key-code order. */
   arc_input_reset(&state);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_OSK, RETROK_LSHIFT,
                        true, true);
   assert(RETROK_BREAK > RETROK_LSHIFT);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_OSK, RETROK_BREAK, true, true);
   assert(arc_input_consume(&state, true, &event) && event.down);
   assert(arc_input_consume(&state, true, &event) && event.down);
   arc_input_clear_source(&state, ARC_INPUT_SOURCE_OSK);
   assert(arc_input_consume(&state, true, &event) &&
          event.code == RETROK_BREAK && !event.down);
   assert(arc_input_consume(&state, true, &event) &&
          event.code == RETROK_LSHIFT && !event.down);
   assert(!arc_input_pending(&state));
}

static unsigned drain_input(ArcInputState *state, ArcInputEvent *events,
                            unsigned capacity)
{
   unsigned count = 0u;
   while (count < capacity && arc_input_consume(state, true, &events[count])) ++count;
   return count;
}

static void complete_osk_chord(ArcOsk *osk, ArcInputState *input,
                               unsigned key, unsigned modifier,
                               uint64_t *guest_us)
{
   ArcInputEvent events[4];
   arc_osk_update(osk, input);
   assert(drain_input(input, events, 4u) == 1u &&
          events[0].code == modifier && events[0].down);
   arc_osk_update(osk, input);
   assert(osk->activation_deadline_us == *guest_us + UINT64_C(60000));
   assert(!arc_osk_advance(osk, osk->activation_deadline_us - 1u));
   arc_osk_update(osk, input);
   assert(!arc_input_pending(input));
   *guest_us = osk->activation_deadline_us;
   assert(!arc_osk_advance(osk, *guest_us));
   arc_osk_update(osk, input);
   assert(drain_input(input, events, 4u) == 1u &&
          events[0].code == key && events[0].down);

   arc_osk_release_activation(osk);
   arc_osk_update(osk, input);
   assert(osk->activation_deadline_us == *guest_us + UINT64_C(60000));
   assert(!arc_osk_advance(osk, osk->activation_deadline_us - 1u));
   arc_osk_update(osk, input);
   assert(!arc_input_pending(input));
   *guest_us = osk->activation_deadline_us;
   assert(!arc_osk_advance(osk, *guest_us));
   arc_osk_update(osk, input);
   assert(drain_input(input, events, 4u) == 1u &&
          events[0].code == key && !events[0].down);

   arc_osk_update(osk, input);
   assert(osk->activation_deadline_us == *guest_us + UINT64_C(60000));
   assert(!arc_osk_advance(osk, osk->activation_deadline_us - 1u));
   arc_osk_update(osk, input);
   assert(!arc_input_pending(input));
   *guest_us = osk->activation_deadline_us;
   assert(!arc_osk_advance(osk, *guest_us));
   arc_osk_update(osk, input);
   assert(drain_input(input, events, 4u) == 1u &&
          events[0].code == modifier && !events[0].down);
   arc_osk_update(osk, input);
   assert(!osk->activation_phase);
}

static const ArcInputKey *osk_catalogue_key(unsigned key)
{
   size_t count = 0u;
   const ArcInputKey *catalogue = arc_input_supported_keys(&count);
   for (size_t i = 0u; i < count; ++i)
      if (catalogue[i].key == key) return &catalogue[i];
   catalogue = arc_input_osk_only_keys(&count);
   for (size_t i = 0u; i < count; ++i)
      if (catalogue[i].key == key) return &catalogue[i];
   return NULL;
}

static void assert_osk_physical_row(const ArcOsk *osk, unsigned row,
                                    const unsigned *expected,
                                    size_t expected_count)
{
   assert(osk && !osk->compact && expected && expected_count <= 21u);
   unsigned current_row = 0u;
   size_t actual_count = 0u;
   for (unsigned i = 0u; i < osk->key_count; ++i)
   {
      if (i > 0u && osk->keys[i].y != osk->keys[i - 1u].y) ++current_row;
      if (current_row != row) continue;
      assert(actual_count < expected_count);
      assert(osk->keys[i].key == expected[actual_count++]);
   }
   assert(actual_count == expected_count);
}

static void assert_osk_compact_page(const ArcOsk *osk,
                                    const unsigned *expected,
                                    size_t expected_count)
{
   assert(osk && osk->compact && expected);
   size_t seen = 0u;
   unsigned row_count = 0u;
   for (unsigned i = 0u; i < osk->key_count; ++i)
   {
      if (i > 0u && osk->keys[i].y != osk->keys[i - 1u].y)
      {
         assert(row_count <= 10u);
         row_count = 0u;
      }
      ++row_count;
      if (osk->keys[i].action != ARC_OSK_ACTION_PAGE)
      {
         assert(seen < expected_count);
         assert(osk->keys[i].key == expected[seen++]);
      }
   }
   assert(row_count <= 10u);
   assert(seen == expected_count);
}

static void test_osk(void)
{
   static const unsigned row0[] = {
      RETROK_ESCAPE,
      RETROK_F1, RETROK_F2, RETROK_F3, RETROK_F4, RETROK_F5, RETROK_F6,
      RETROK_F7, RETROK_F8, RETROK_F9, RETROK_F10, RETROK_F11, RETROK_F12,
      RETROK_SCROLLOCK, RETROK_PAUSE
   };
   static const unsigned row1[] = {
      RETROK_BACKQUOTE,
      RETROK_1, RETROK_2, RETROK_3, RETROK_4, RETROK_5,
      RETROK_6, RETROK_7, RETROK_8, RETROK_9, RETROK_0,
      RETROK_MINUS, RETROK_EQUALS, RETROK_BACKSPACE,
      RETROK_INSERT, RETROK_HOME, RETROK_PAGEUP, RETROK_NUMLOCK,
      RETROK_KP_DIVIDE, RETROK_KP_MULTIPLY, RETROK_KP_MINUS
   };
   static const unsigned row2[] = {
      RETROK_TAB,
      RETROK_q, RETROK_w, RETROK_e, RETROK_r, RETROK_t,
      RETROK_y, RETROK_u, RETROK_i, RETROK_o, RETROK_p,
      RETROK_LEFTBRACKET, RETROK_RIGHTBRACKET, RETROK_BACKSLASH,
      RETROK_DELETE, RETROK_END, RETROK_PAGEDOWN,
      RETROK_KP7, RETROK_KP8, RETROK_KP9
   };
   static const unsigned row3[] = {
      RETROK_LCTRL,
      RETROK_a, RETROK_s, RETROK_d, RETROK_f, RETROK_g,
      RETROK_h, RETROK_j, RETROK_k, RETROK_l,
      RETROK_SEMICOLON, RETROK_QUOTE, RETROK_RETURN,
      RETROK_KP4, RETROK_KP5, RETROK_KP6, RETROK_KP_PLUS
   };
   static const unsigned row4[] = {
      RETROK_LSHIFT,
      RETROK_z, RETROK_x, RETROK_c, RETROK_v, RETROK_b, RETROK_n, RETROK_m,
      RETROK_COMMA, RETROK_PERIOD, RETROK_SLASH, RETROK_RSHIFT, RETROK_UP,
      RETROK_KP1, RETROK_KP2, RETROK_KP3
   };
   static const unsigned row5[] = {
      RETROK_CAPSLOCK, RETROK_LALT, RETROK_SPACE, RETROK_RALT, RETROK_RCTRL,
      RETROK_LEFT, RETROK_DOWN, RETROK_RIGHT,
      RETROK_KP0, RETROK_KP_PERIOD, RETROK_KP_ENTER
   };
   static const struct {
      const unsigned *keys;
      size_t count;
   } physical_rows[] = {
      {row0, sizeof(row0) / sizeof(row0[0])},
      {row1, sizeof(row1) / sizeof(row1[0])},
      {row2, sizeof(row2) / sizeof(row2[0])},
      {row3, sizeof(row3) / sizeof(row3[0])},
      {row4, sizeof(row4) / sizeof(row4[0])},
      {row5, sizeof(row5) / sizeof(row5[0])}
   };
   ArcOsk osk = {0};
   ArcInputState input;
   ArcInputEvent events[16];
   arc_input_reset(&input);
   assert(!arc_osk_open(&osk, 256u, 256u) && !osk.disabled && !osk.visible);
   memset(&osk, 0, sizeof(osk));
   assert(arc_osk_open(&osk, 320u, 256u) && osk.visible && osk.compact &&
          osk.page_count == 2u && osk.key_count > 40u);
   unsigned compact_expected[100u];
   size_t compact_count = 0u;
   for (unsigned row = 0u; row < 3u; ++row)
      for (size_t key = 0u; key < physical_rows[row].count; ++key)
         compact_expected[compact_count++] = physical_rows[row].keys[key];
   assert_osk_compact_page(&osk, compact_expected, compact_count);
   bool saw_compact_keypad_hash = false;
   for (unsigned i = 0u; i < osk.key_count; ++i)
      if (osk.keys[i].key == RETROK_KP_MINUS)
         saw_compact_keypad_hash = !strcmp(osk.keys[i].label, "K#");
   assert(saw_compact_keypad_hash);
   osk.page = 1u;
   assert(arc_osk_layout(&osk, 320u, 256u));
   compact_count = 0u;
   for (unsigned row = 3u; row < 6u; ++row)
      for (size_t key = 0u; key < physical_rows[row].count; ++key)
         compact_expected[compact_count++] = physical_rows[row].keys[key];
   assert_osk_compact_page(&osk, compact_expected, compact_count);
   osk.page = 0u;
   assert(arc_osk_layout(&osk, 320u, 256u));

   ArcOsk full_layout = {0};
   assert(arc_osk_open(&full_layout, 640u, 256u) && !full_layout.compact);
   for (unsigned row = 0u; row < 6u; ++row)
      assert_osk_physical_row(&full_layout, row, physical_rows[row].keys,
                              physical_rows[row].count);
   bool saw_full_keypad_hash = false;
   for (unsigned i = 0u; i < full_layout.key_count; ++i)
      if (full_layout.keys[i].key == RETROK_KP_MINUS)
         saw_full_keypad_hash = !strcmp(full_layout.keys[i].label, "KP#");
   assert(saw_full_keypad_hash);
   /* Every keyboard key has a tier-specific label which fits its cell and is
    * distinct from every other rendered label at that tier. Compact pages
    * are checked together rather than allowing collisions across pages. */
   for (unsigned tier = 0u; tier < 2u; ++tier)
   {
      unsigned width = tier ? 640u : 320u;
      ArcOsk labelled = {0};
      const char *labels[ARC_OSK_MAX_KEYS] = {0};
      unsigned label_count = 0u;
      assert(arc_osk_open(&labelled, width, 256u));
      for (unsigned page = 0u; page < labelled.page_count; ++page)
      {
         labelled.page = page;
         assert(arc_osk_layout(&labelled, width, 256u));
         for (unsigned i = 0u; i < labelled.key_count; ++i)
         {
            const ArcOskKey *key = &labelled.keys[i];
            for (const unsigned char *glyph =
                    (const unsigned char *)key->label; *glyph; ++glyph)
            {
               unsigned index = *glyph;
               if (index >= 'a' && index <= 'z') index -= 'a' - 'A';
               assert(index < 130u);
               if (index == ' ') continue;
               bool nonblank = false;
               for (unsigned row = 0u; row < 8u; ++row)
                  nonblank |= arc_osk_font[index][row] != 0u;
               assert(nonblank);
            }
            if (key->action != ARC_OSK_ACTION_PAGE)
            {
               const ArcInputKey *catalogue = osk_catalogue_key(key->key);
               assert(catalogue);
            }
            assert(strlen(key->label) <= (key->width - 2u) / 8u);
            for (unsigned previous = 0u; previous < label_count; ++previous)
               assert(strcmp(labels[previous], key->label));
            assert(label_count < ARC_OSK_MAX_KEYS);
            labels[label_count++] = key->label;
         }
      }
      assert(label_count == (labelled.compact ? 102u : 100u));
   }
   unsigned first_focus = osk.focus;
   arc_osk_move(&osk, 1, 0, 100u, true);
   assert(osk.focus != first_focus);
   unsigned repeated = osk.focus;
   arc_osk_move(&osk, 1, 0, 1000u, false);
   assert(osk.focus == repeated);
   arc_osk_move(&osk, 1, 0, 400000u, false);
   assert(osk.focus != repeated);

   unsigned page_key = UINT_MAX;
   for (unsigned i = 0u; i < osk.key_count; ++i)
      if (osk.keys[i].action == ARC_OSK_ACTION_PAGE) page_key = i;
   assert(page_key != UINT_MAX);
   osk.focus = page_key;
   assert(arc_osk_press_focused(&osk, &input) && osk.page == 1u);

   /* Captured touches activate only if they release on the original key. */
   const ArcOskKey touch_key = osk.keys[0];
   assert(arc_osk_touch(&osk, (int)touch_key.x + 1, (int)touch_key.y + 1,
                        true, &input));
   assert(!arc_osk_touch(&osk, (int)(touch_key.x + touch_key.width + 20u),
                         (int)touch_key.y + 1, true, &input));
   assert(!arc_osk_touch(&osk, 0, 0, false, &input) &&
          !osk.activation_phase);

   unsigned captured = UINT_MAX;
   for (unsigned i = 0u; i < osk.key_count; ++i)
      if (osk.keys[i].action == ARC_OSK_ACTION_KEY)
      {
         captured = i;
         break;
      }
   assert(captured != UINT_MAX);
   const ArcOskKey captured_key = osk.keys[captured];
   assert(arc_osk_touch(&osk, (int)captured_key.x + 1,
                         (int)captured_key.y + 1, true, &input));
   assert(osk.focus == captured && osk.contact_key == captured);
   arc_osk_move(&osk, 1, 0, 500000u, true);
   assert(osk.focus == captured && osk.contact_key == captured);
   unsigned displaced_focus = captured ? 0u : 1u;
   assert(displaced_focus < osk.key_count && displaced_focus != captured);
   osk.focus = displaced_focus;
   assert(arc_osk_touch(&osk, (int)captured_key.x + 1,
                         (int)captured_key.y + 1, false, &input));
   assert(osk.focus == displaced_focus);
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 1u &&
          events[0].code == captured_key.key && events[0].down);
   arc_osk_update(&osk, &input);
   assert(!arc_osk_advance(&osk, osk.activation_deadline_us));
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 1u &&
          events[0].code == captured_key.key && !events[0].down);
   arc_osk_update(&osk, &input);
   assert(!arc_osk_advance(&osk, osk.activation_deadline_us));
   arc_osk_update(&osk, &input);
   assert(!osk.activation_phase);

   assert(arc_osk_layout(&osk, 640u, 480u) && !osk.compact);
   unsigned shift = UINT_MAX, right_shift = UINT_MAX, letter = UINT_MAX;
   unsigned letter_b = UINT_MAX, return_key = UINT_MAX;
   for (unsigned i = 0u; i < osk.key_count; ++i)
   {
      if (osk.keys[i].key == RETROK_LSHIFT) shift = i;
      if (osk.keys[i].key == RETROK_RSHIFT) right_shift = i;
      if (osk.keys[i].key == RETROK_a) letter = i;
      if (osk.keys[i].key == RETROK_b) letter_b = i;
      if (osk.keys[i].key == RETROK_RETURN) return_key = i;
   }
   bool saw_num_lock = false, saw_scroll_lock = false, saw_break = false;
   for (unsigned i = 0u; i < osk.key_count; ++i)
   {
      saw_num_lock |= osk.keys[i].key == RETROK_NUMLOCK;
      saw_scroll_lock |= osk.keys[i].key == RETROK_SCROLLOCK;
      if (osk.keys[i].key == RETROK_PAUSE)
         saw_break = !strcmp(osk.keys[i].label, "BRK");
      assert(osk.keys[i].key != RETROK_PRINT);
   }
   assert(saw_num_lock && saw_scroll_lock && saw_break);
   assert(shift != UINT_MAX && right_shift != UINT_MAX && letter != UINT_MAX &&
          letter_b != UINT_MAX && return_key != UINT_MAX);
   /* Holding an activation freezes focus, and releasing still completes the
    * key that originally accepted the activation. */
   osk.focus = letter;
   assert(arc_osk_press_focused(&osk, &input));
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 1u &&
          events[0].code == RETROK_a && events[0].down);
   arc_osk_move(&osk, 1, 0, 500000u, true);
   assert(osk.focus == letter);
   const ArcOskKey held_touch = osk.keys[shift];
   assert(!arc_osk_touch(&osk, (int)held_touch.x + 1,
                         (int)held_touch.y + 1, true, &input));
   assert(osk.focus == letter && !osk.contact && !osk.contact_valid);
   assert(!arc_osk_touch(&osk, (int)held_touch.x + 1,
                         (int)held_touch.y + 1, false, &input));
   assert(osk.focus == letter);
   arc_osk_update(&osk, &input);
   uint64_t osk_time = osk.activation_deadline_us + UINT64_C(10000);
   assert(!arc_osk_advance(&osk, osk_time));
   arc_osk_update(&osk, &input);
   assert(!arc_input_pending(&input));
   arc_osk_release_activation(&osk);
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 1u &&
          events[0].code == RETROK_a && !events[0].down);
   arc_osk_update(&osk, &input);
   assert(osk.activation_deadline_us == osk_time + UINT64_C(60000));
   osk_time = osk.activation_deadline_us;
   assert(!arc_osk_advance(&osk, osk_time));
   arc_osk_update(&osk, &input);
   assert(!osk.activation_phase);
   osk.focus = shift;
   assert(arc_osk_press_focused(&osk, &input) &&
          osk.modifiers[0] == ARC_OSK_MOD_ONESHOT);
   osk.focus = letter;
   assert(arc_osk_press_focused(&osk, &input));
   complete_osk_chord(&osk, &input, RETROK_a, RETROK_LSHIFT, &osk_time);
   assert(!osk.activation_phase && osk.modifiers[0] == ARC_OSK_MOD_OFF);

   /* Left and right modifiers have independent sticky state and emit their
    * own physical key identities. */
   osk.focus = right_shift;
   assert(arc_osk_press_focused(&osk, &input) &&
          osk.modifiers[1] == ARC_OSK_MOD_ONESHOT &&
          osk.modifiers[0] == ARC_OSK_MOD_OFF);
   osk.focus = letter;
   assert(arc_osk_press_focused(&osk, &input));
   complete_osk_chord(&osk, &input, RETROK_a, RETROK_RSHIFT, &osk_time);
   assert(!osk.activation_phase && osk.modifiers[1] == ARC_OSK_MOD_OFF);

   /* Locked modifiers persist for another activation but never remain as an
    * invisible source level between activations. */
   osk.focus = shift;
   assert(arc_osk_press_focused(&osk, &input));
   assert(arc_osk_press_focused(&osk, &input) &&
          osk.modifiers[0] == ARC_OSK_MOD_LOCKED);
   osk.focus = letter;
   assert(arc_osk_press_focused(&osk, &input));
   complete_osk_chord(&osk, &input, RETROK_a, RETROK_LSHIFT, &osk_time);
   assert(!osk.activation_phase && osk.modifiers[0] == ARC_OSK_MOD_LOCKED &&
          !arc_input_source_level(&input, ARC_INPUT_SOURCE_OSK, RETROK_LSHIFT));
   osk.focus = shift;
   assert(arc_osk_press_focused(&osk, &input) &&
          osk.modifiers[0] == ARC_OSK_MOD_OFF);
   osk.focus = letter;

   /* An already-effective key completes without an edge or losing ownership. */
   arc_input_source_key(&input, ARC_INPUT_SOURCE_KEYBOARD, RETROK_a, true, true);
   assert(drain_input(&input, events, 16u) == 1u);
   osk.focus = letter;
   assert(arc_osk_press_focused(&osk, &input));
   arc_osk_update(&osk, &input);
   arc_osk_release_activation(&osk);
   arc_osk_update(&osk, &input);
   osk_time = osk.activation_deadline_us;
   assert(!arc_osk_advance(&osk, osk_time));
   arc_osk_update(&osk, &input);
   osk_time = osk.activation_deadline_us;
   assert(!arc_osk_advance(&osk, osk_time));
   arc_osk_update(&osk, &input);
   osk_time = osk.activation_deadline_us;
   assert(!arc_osk_advance(&osk, osk_time));
   arc_osk_update(&osk, &input);
   assert(!arc_input_pending(&input) && !osk.activation_phase &&
          arc_input_source_level(&input, ARC_INPUT_SOURCE_KEYBOARD, RETROK_a) &&
          arc_input_effective_level(&input, RETROK_a));

   /* Reconciliation invalidates an activation once and recovers OSK levels. */
   assert(!arc_osk_advance(&osk, osk_time + 100u));
   for (unsigned attempt = 0u; attempt < 3u; ++attempt)
   {
      assert(arc_osk_press_focused(&osk, &input));
      arc_osk_update(&osk, &input);
      arc_input_release_all(&input);
      arc_osk_update(&osk, &input);
      assert(!osk.activation_phase && arc_osk_take_interrupted(&osk) &&
             !arc_osk_take_interrupted(&osk) && osk.interrupted_visible);
      while (arc_input_pending(&input))
         (void)arc_input_consume(&input, true, &events[0]);
   }
   /* Reaching the one-second deadline alone does not clear INTERRUPTED: an
    * action after that deadline is the later boundary and clears it. */
   uint64_t interrupted_until = osk.interrupted_until_us;
   assert(!arc_osk_advance(&osk, interrupted_until) &&
          osk.interrupted_visible);
   unsigned interrupted_focus = osk.focus;
   arc_osk_move(&osk, 1, 0, interrupted_until, true);
   assert(osk.focus != interrupted_focus && !osk.interrupted_visible);

   /* One activation may wait behind the current activation. A third is
    * refused with a persistent BUSY notice and cannot replace the queued key. */
   arc_input_reset(&input);
   osk.focus = letter;
   assert(arc_osk_press_focused(&osk, &input));
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 1u &&
          events[0].code == RETROK_a && events[0].down);
   arc_osk_release_activation(&osk);
   osk.focus = letter_b;
   assert(arc_osk_press_focused(&osk, &input) && osk.queued_activation &&
          osk.queued_activation_key == RETROK_b);
   arc_osk_release_activation(&osk);
   osk.focus = return_key;
   assert(!arc_osk_press_focused(&osk, &input) && osk.busy &&
          osk.queued_activation && osk.queued_activation_key == RETROK_b);
   uint64_t outstanding_busy_until = osk.busy_until_us;

   arc_osk_update(&osk, &input);
   osk_time = osk.activation_deadline_us;
   assert(!arc_osk_advance(&osk, osk_time));
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 1u &&
          events[0].code == RETROK_a && !events[0].down);
   arc_osk_update(&osk, &input);
   osk_time = osk.activation_deadline_us;
   assert(!arc_osk_advance(&osk, osk_time));
   arc_osk_update(&osk, &input);
   assert(osk.activation_phase && !osk.queued_activation &&
          osk.activation_key == RETROK_b);
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 1u &&
          events[0].code == RETROK_b && events[0].down);
   arc_osk_update(&osk, &input);
   osk_time = osk.activation_deadline_us;
   assert(!arc_osk_advance(&osk, osk_time));
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 1u &&
          events[0].code == RETROK_b && !events[0].down);
   arc_osk_update(&osk, &input);
   osk_time = osk.activation_deadline_us;
   assert(!arc_osk_advance(&osk, osk_time));
   arc_osk_update(&osk, &input);
   assert(!osk.activation_phase && !osk.queued_activation &&
          !arc_input_pending(&input) && osk.busy);
   assert(!arc_osk_advance(&osk, outstanding_busy_until) && osk.busy);
   osk.focus = letter;
   arc_osk_move(&osk, 1, 0, outstanding_busy_until, true);
   assert(!osk.busy);

   /* A full queue reports busy and does not start a partial chord. */
   arc_input_reset(&input);
   for (unsigned i = 0u; i < ARC_INPUT_FIFO_CAPACITY / 2u; ++i)
   {
      arc_input_source_key(&input, ARC_INPUT_SOURCE_KEYBOARD, RETROK_b, true, true);
      arc_input_source_key(&input, ARC_INPUT_SOURCE_KEYBOARD, RETROK_b, false, true);
   }
   assert(!arc_osk_shortcut(&osk, &input, RETROK_RETURN) && osk.busy);
   uint64_t busy_until = osk.busy_until_us;
   while (arc_input_pending(&input))
      (void)arc_input_consume(&input, true, &events[0]);
   osk.focus = shift;
   assert(arc_osk_press_focused(&osk, &input) && osk.busy);
   assert(!arc_osk_advance(&osk, busy_until - 1u) && osk.busy);
   assert(arc_osk_advance(&osk, busy_until) && !osk.busy);

   /* A long label is clipped at its own cell instead of painting over the
    * neighbouring key. */
   {
      ArcOsk clipped = {0};
      uint32_t pixels[64u * 48u] = {0};
      clipped.visible = true;
      clipped.width = 64u;
      clipped.height = 48u;
      clipped.key_count = 2u;
      clipped.keys[0] = (ArcOskKey){ARC_OSK_ACTION_KEY, RETROK_a,
                                    "MMMM", 1u, 24u, 15u, 12u};
      clipped.keys[1] = (ArcOskKey){ARC_OSK_ACTION_KEY, RETROK_b,
                                    "", 16u, 24u, 15u, 12u};
      assert(arc_osk_render(&clipped, pixels, 64u, 48u,
                            64u * sizeof(uint32_t), ""));
      for (unsigned y = 25u; y < 33u; ++y)
         for (unsigned x = 16u; x < 31u; ++x)
            assert(pixels[y * 64u + x] != UINT32_C(0x00ffffff));
   }

   static const unsigned widths[] = {320u, 640u, 640u};
   static const unsigned heights[] = {256u, 256u, 480u};
   static const char *goldens[3][2][2] = {
      {{"d52cca88212e55f511eee1e0bd1725e4a79b0e64", "c3448dc71a273dd554d196878fa566dfed344a3a"},
       {"7c7117e8a370d4d0376db323a162b5af64f568d5", "d93122c9d2101aa3772b9b8dee98b6bbcfbd367f"}},
      {{"fcca267a752fdb89132fed048c69cec0af5e3758", NULL},
       {"7d38270a10e38de0dbd7d5d5ad5fcd17c168432f", NULL}},
      {{"ea7f9888e5a7d22d9e4f82975ac8f37273662803", NULL},
       {"03140896bd4cffd368e958826c1dcb7f8b8705e3", NULL}}
   };
   for (unsigned geometry = 0u; geometry < 3u; ++geometry)
      for (unsigned alpha = 0u; alpha < 2u; ++alpha)
      {
         unsigned width = widths[geometry], height = heights[geometry];
         size_t count = (size_t)width * height;
         uint32_t *surface = malloc((count + 2u) * sizeof(*surface));
         assert(surface);
         surface[0] = UINT32_C(0xdeadbeef);
         surface[count + 1u] = UINT32_C(0xcafef00d);
         memset(&osk, 0, sizeof(osk));
         assert(arc_osk_open(&osk, width, height));
         osk.transparent = alpha != 0u;
         unsigned pages = osk.page_count;
         for (unsigned page = 0u; page < pages; ++page)
         {
            for (size_t i = 0u; i < count; ++i)
               surface[i + 1u] = (uint32_t)(i * UINT32_C(2654435761));
            osk.page = page;
            assert(arc_osk_layout(&osk, width, height));
            assert(arc_osk_render(&osk, surface + 1u, width, height,
                                  (size_t)width * sizeof(uint32_t),
                                  "Golden custom"));
            char digest[41];
            arc_sha1_hex(surface + 1u, count * sizeof(uint32_t), digest);
            if (!goldens[geometry][alpha][page] ||
                strcmp(digest, goldens[geometry][alpha][page]))
               fprintf(stderr, "OSK golden[%u][%u][%u] = %s\n",
                       geometry, alpha, page, digest);
            assert(goldens[geometry][alpha][page] &&
                   !strcmp(digest, goldens[geometry][alpha][page]));
         }
         assert(surface[0] == UINT32_C(0xdeadbeef) &&
                surface[count + 1u] == UINT32_C(0xcafef00d));
         free(surface);
      }

   /* The authored arrow byte reaches its bitmap. Derive the compact INT
    * golden through a real generation interruption; rendering does not
    * acknowledge or prematurely clear the guest-time/action-held notice. */
   memset(&osk, 0, sizeof(osk));
   arc_input_reset(&input);
   assert(arc_osk_open(&osk, 320u, 256u));
   bool saw_up_arrow = false;
   for (unsigned page = 0u; page < osk.page_count; ++page)
   {
      osk.page = page;
      assert(arc_osk_layout(&osk, 320u, 256u));
      for (unsigned i = 0u; i < osk.key_count; ++i)
         if (osk.keys[i].key == RETROK_UP)
            saw_up_arrow = !strcmp(osk.keys[i].label, "\x80");
   }
   assert(saw_up_arrow);
   osk.page = 1u;
   assert(arc_osk_layout(&osk, 320u, 256u));
   unsigned notice_key = UINT_MAX;
   for (unsigned i = 0u; i < osk.key_count; ++i)
      if (osk.keys[i].key == RETROK_a) notice_key = i;
   assert(notice_key != UINT_MAX);
   osk.focus = notice_key;
   assert(arc_osk_advance(&osk, 500u) == false);
   assert(arc_osk_press_focused(&osk, &input));
   arc_osk_update(&osk, &input);
   arc_input_release_all(&input);
   arc_osk_update(&osk, &input);
   assert(arc_osk_take_interrupted(&osk) && osk.interrupted_visible);
   uint32_t *notice = calloc(320u * 256u, sizeof(*notice));
   assert(notice && arc_osk_render(&osk, notice, 320u, 256u,
                                   320u * sizeof(uint32_t), "Custom controls"));
   char notice_digest[41];
   arc_sha1_hex(notice, 320u * 256u * sizeof(*notice), notice_digest);
   if (strcmp(notice_digest, "9e78d83aea422e7377d401db810c232c5dd704ac"))
      fprintf(stderr, "OSK compact INT golden = %s\n", notice_digest);
   assert(!strcmp(notice_digest, "9e78d83aea422e7377d401db810c232c5dd704ac"));
   assert(osk.interrupted_visible);
   memset(notice, 0, 320u * 256u * sizeof(*notice));
   assert(arc_osk_render(&osk, notice, 320u, 256u,
                         320u * sizeof(uint32_t), "Default controls"));
   char default_digest[41];
   arc_sha1_hex(notice, 320u * 256u * sizeof(*notice), default_digest);
   assert(strcmp(default_digest, notice_digest));
   memset(notice, 0, 320u * 256u * sizeof(*notice));
   assert(arc_osk_render(&osk, notice, 320u, 256u,
                         320u * sizeof(uint32_t),
                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"));
   /* The header begins at x=4 and fits 39 glyphs. Its long controls label is
    * truncated so the final T in the short "INT" state remains rendered. */
   unsigned notice_panel_y = 256u - 256u * 45u / 100u;
   assert(notice[(notice_panel_y + 4u) * 320u + 309u] ==
          UINT32_C(0x00ffffff));
   free(notice);
}

static void test_f_media(const char *directory)
{
   char error[256] = {0};
   ArcMediaInfo media;
   ArcLaunchDiscovery discovery;
   for (unsigned choice = 0u; choice < 2u; ++choice)
   {
      size_t size;
      uint8_t *image = read_fixture(directory,
         choice ? "hd-family/backup-f.adf" : "hd-family/valid-f.adf", &size);
      assert(size == 1638400u);
      unsigned boot_checksum = 0u;
      for (size_t byte = 0xc00u; byte < 0xdffu; ++byte)
      {
         unsigned sum = boot_checksum + image[byte];
         boot_checksum = (sum & 0xffu) + (sum >> 8u);
      }
      assert(image[0xdffu] == boot_checksum && boot_checksum == 0xd6u);
      assert(arc_media_inspect("f.adf", image, size, &media, error, sizeof(error)));
      assert(media.kind == ARC_MEDIA_ADF_1600K &&
             media.geometry == ARC_GEOMETRY_1600K_F &&
             media.recognition == ARC_MEDIA_RECOGNITION_DESCRIPTOR);
      assert(arc_launch_discover(image, size, &media, NULL, &discovery));
      if (!discovery.readable || discovery.application_count != 1u)
         fprintf(stderr, "F discovery %u: readable=%d candidates=%u applications=%u reason=%s\n",
            choice, discovery.readable, discovery.candidate_count,
            discovery.application_count, arc_launch_reason_name(discovery.reason));
      assert(discovery.readable && discovery.application_count == 1u &&
             discovery.target_index == 0 &&
             discovery.backup_map_used == (choice != 0u));
      assert(discovery.disc_name.length == 8u &&
             !memcmp(discovery.disc_name.bytes, "ArcFTest", 8u));
      free(image);
   }
   for (unsigned choice = 0u; choice < 2u; ++choice)
   {
      size_t size;
      uint8_t *image = read_fixture(directory, choice ?
         "hd-family/stray-tail-bits-f.adf" :
         "hd-family/blank-boot-name-f.adf", &size);
      unsigned boot_checksum = 0u;
      for (size_t byte = 0xc00u; byte < 0xdffu; ++byte)
      {
         unsigned sum = boot_checksum + image[byte];
         boot_checksum = (sum & 0xffu) + (sum >> 8u);
      }
      assert(image[0xdffu] == boot_checksum);
      assert(arc_media_inspect("f.adf", image, size, &media, error,
                               sizeof(error)));
      if (!choice) assert(media.disc_name_length == 0u);
      assert(arc_launch_discover(image, size, &media, NULL, &discovery));
      assert(discovery.readable && discovery.application_count == 1u &&
             discovery.disc_name.length == 8u &&
             !memcmp(discovery.disc_name.bytes, "ArcFTest", 8u));
      free(image);
   }
   for (unsigned case_index = 0u; case_index < 2u; ++case_index)
   {
      size_t size;
      uint8_t *image = read_fixture(directory, case_index ?
         "hd-family/long-f.adf" : "hd-family/short-f.adf", &size);
      assert(!arc_media_inspect("bad-f.adf", image, size,
                                &media, error, sizeof(error)));
      assert(strstr(error, "unsupported raw image geometry"));
      free(image);
   }
   size_t size;
   uint8_t *jfd_bytes = read_fixture(directory, "hd-family/valid-f.jfd", &size);
   ArcJfdImage jfd;
   assert(arc_jfd_parse_owned(jfd_bytes, size, &jfd, error, sizeof(error)));
   assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery));
   assert(discovery.readable && discovery.application_count == 1u &&
          discovery.target_index == 0);
   arc_jfd_free(&jfd);
   static const struct {
      const char *name;
      unsigned count;
      unsigned density;
      unsigned time_us;
      unsigned flags;
      bool id_only;
   } quad_cases[] = {
      {"quad-data.jfd", 1u, 4u, 0u, 0u, false},
      {"quad-timed.jfd", 1u, 4u, 17000u, 0u, false},
      {"quad-id-only.jfd", 1u, 4u, 0u, 0u, true},
      {"quad-id-crc.jfd", 1u, 4u, 0u, ARC_JFD_ID_CRC_BAD, false},
      {"quad-data-crc.jfd", 1u, 4u, 0u, ARC_JFD_DATA_CRC_BAD, false},
      {"mixed-density.jfd", 2u, 2u, 0u, 0u, false}
   };
   for (size_t i = 0u; i < sizeof(quad_cases) / sizeof(quad_cases[0]); ++i)
   {
      char path[128];
      snprintf(path, sizeof(path), "jfd-family/%s", quad_cases[i].name);
      jfd_bytes = read_fixture(directory, path, &size);
      assert(arc_jfd_parse_owned(jfd_bytes, size, &jfd, error, sizeof(error)));
      assert(jfd.sector_count == quad_cases[i].count &&
             jfd.sectors[0].density == quad_cases[i].density &&
             jfd.sectors[0].time_us == quad_cases[i].time_us &&
             jfd.sectors[0].flags == quad_cases[i].flags &&
             (jfd.sectors[0].data_offset == UINT32_MAX) == quad_cases[i].id_only);
      if (i == 5u)
         assert(jfd.sectors[1].density == 4u &&
                jfd.sectors[1].time_us == 7680u);
      arc_jfd_free(&jfd);
   }
   for (unsigned case_index = 0u; case_index < 2u; ++case_index)
   {
      jfd_bytes = read_fixture(directory, case_index ?
         "hd-family/unreadable-primary-f.jfd" : "hd-family/backup-f.jfd", &size);
      assert(arc_jfd_parse_owned(jfd_bytes, size, &jfd, error, sizeof(error)));
      assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery));
      assert(discovery.readable && discovery.backup_map_used &&
             discovery.application_count == 1u);
      arc_jfd_free(&jfd);
   }
   uint8_t *old = read_fixture(directory,
      "hd-family/old-overdeclared-d.adf", &size);
   assert(arc_media_inspect("old.adf", old, size, &media, error, sizeof(error)));
   assert(arc_launch_discover(old, size, &media, NULL, &discovery));
   assert(discovery.readable && discovery.old_map_clamped &&
          discovery.application_count == 1u);
   free(old);
   jfd_bytes = read_fixture(directory, "hd-family/old-overdeclared-d.jfd", &size);
   assert(arc_jfd_parse_owned(jfd_bytes, size, &jfd, error, sizeof(error)));
   assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery));
   assert(discovery.readable && discovery.old_map_clamped &&
          discovery.application_count == 1u);
   arc_jfd_free(&jfd);
   jfd_bytes = read_fixture(directory, "hd-family/old-overdeclared-hole-d.jfd", &size);
   assert(arc_jfd_parse_owned(jfd_bytes, size, &jfd, error, sizeof(error)));
   assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery));
   assert(!discovery.readable && discovery.old_map_clamped);
   arc_jfd_free(&jfd);
   const char *bad[] = {"corrupt-both-f.adf", "invalid-zones-f.adf",
                        "invalid-free-f.adf", "invalid-id-f.adf",
                        "invalid-cross-f.adf", "small-zone-spare-f.adf",
                        "unterminated-last-f.adf"};
   for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i)
   {
      char path[128];
      snprintf(path, sizeof(path), "hd-family/%s", bad[i]);
      uint8_t *image = read_fixture(directory, path, &size);
      assert(arc_media_inspect(path, image, size, &media, error, sizeof(error)));
      assert(arc_launch_discover(image, size, &media, NULL, &discovery));
      if (discovery.readable) fprintf(stderr, "unexpected readable F fixture: %s\n", bad[i]);
      assert(!discovery.readable);
      free(image);
   }
   jfd_bytes = read_fixture(directory, "hd-family/sparse-map-holes-f.jfd", &size);
   assert(arc_jfd_parse_owned(jfd_bytes, size, &jfd, error, sizeof(error)));
   assert(arc_launch_discover(NULL, 0u, NULL, &jfd, &discovery));
   assert(!discovery.readable);
   arc_jfd_free(&jfd);
}

static uint32_t ramdisc_u24(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u); }

static uint8_t ramdisc_checksum(const uint8_t *p)
{
   unsigned sum = 255u;
   for (unsigned i = 255u; i-- > 0u;)
   { if (sum > 255u) sum = (sum + 1u) & 255u; sum += p[i]; }
   return (uint8_t)sum;
}

static void test_launch_ramdisc(void)
{
   /* Complete helper-disc digests built from origin/main's launch_disc.c. */
   static const char *const disabled_goldens[2][2] = {
      {"2de5bfc02757e367c72907042b471547369dd533",
       "55ac70b2643d745f9ca20840a2bd43b84cf3c57d"},
      {"2bfc4851d6b1bd7aa499b44849c3636a76bb3eaf",
       "c7d71dbea303ff4ea89dbed1919add4fd4c9dc08"}
   };
   static const char program[] =
      "10 SYS \"OS_ReadDynamicArea\",5 TO ,size%\r"
      "20 IF size%=0 THEN SYS \"XOS_ChangeDynamicArea\",5,32768\r";
   static const char request[] = "%BASIC -quit ADFS::1.$.ArcRAM\r";
   uint8_t *enabled = malloc(ARC_LAUNCH_DISC_SIZE);
   uint8_t *disabled = malloc(ARC_LAUNCH_DISC_SIZE);
   uint8_t *legacy = malloc(ARC_LAUNCH_DISC_SIZE);
   assert(enabled && disabled && legacy);
   for (unsigned nested = 0u; nested < 2u; ++nested)
   for (unsigned siblings = 0u; siblings < 2u; ++siblings)
   {
      ArcLaunchDiscovery discovery = test_launch_spec("!Demo", 5u);
      if (nested)
      {
         discovery.candidates[0].parent = test_guest_name("Games", 5u);
         discovery.candidates[0].parent_address = discovery.candidates[0].parent;
         discovery.candidates[0].addressable = true;
      }
      if (siblings)
      {
         discovery.candidates[1] = discovery.candidates[0];
         discovery.candidates[1].name = test_guest_name("!System", 7u);
         discovery.candidates[1].state = ARC_LAUNCH_CANDIDATE_NOT_APPLICATION;
         discovery.candidates[1].has_boot = true;
         discovery.candidate_count = 2u;
      }
      char error[128];
      assert(arc_launch_disc_build_ramdisc(&discovery, 0u, true, enabled,
                                          error, sizeof(error)) &&
             arc_launch_disc_build_ramdisc(&discovery, 0u, false, disabled,
                                          error, sizeof(error)) &&
             arc_launch_disc_build(&discovery, 0u, legacy, error, sizeof(error)));
      assert(!memcmp(disabled, legacy, ARC_LAUNCH_DISC_SIZE));
      char disabled_sha1[41];
      arc_sha1_hex(disabled, ARC_LAUNCH_DISC_SIZE, disabled_sha1);
      assert(!strcmp(disabled_sha1, disabled_goldens[nested][siblings]));
      assert(enabled[255] == ramdisc_checksum(enabled) &&
             enabled[511] == ramdisc_checksum(enabled + 256u));
      const uint8_t *root = enabled + test_l_offset(2u);
      const char *names[] = {"!Boot", "ArcLaunch", "ArcRAM"};
      uint32_t starts[3], ends[3];
      for (unsigned i = 0u; i < 3u; ++i)
      {
         const uint8_t *entry = root + 5u + 26u * i;
         for (size_t ch = 0u; ch < strlen(names[i]); ++ch)
            assert((entry[ch] & (ch < 2u ? 0x7fu : 0xffu)) ==
                   (uint8_t)names[i][ch]);
         starts[i] = ramdisc_u24(entry + 22u);
         ends[i] = starts[i] + ((test_le32(entry + 18u) + 255u) / 256u);
         assert(ends[i] <= 2560u && starts[i] < ends[i]);
         for (unsigned j = 0u; j < i; ++j)
            assert(ends[i] <= starts[j] || starts[i] >= ends[j]);
         for (unsigned f = 0u; f < 2u; ++f)
         {
            uint32_t free_start = ramdisc_u24(enabled + f * 3u);
            uint32_t free_end = free_start + ramdisc_u24(enabled + 256u + f * 3u);
            assert(free_start < free_end && free_end <= 2560u);
            /* The map occupies sectors 0–1 and the root directory 2–6. */
            assert(free_start >= 2u);
            assert(free_end <= 2u || free_start >= 7u);
            assert(free_end <= starts[i] || free_start >= ends[i]);
         }
      }
      assert(root[5u + 26u * 3u] == 0u);
      assert(starts[2] == 7u && test_le32(root + 57u + 18u) == sizeof(program) - 1u);
      uint8_t recovered[sizeof(program) - 1u];
      for (size_t n = 0u; n < sizeof(recovered); ++n)
         recovered[n] = enabled[test_l_offset(starts[2] + (unsigned)(n / 256u)) + n % 256u];
      assert(!memcmp(recovered, program, sizeof(recovered)) &&
             recovered[sizeof(recovered) - 1u] == '\r');
      const uint8_t *script = enabled + test_l_offset(starts[1]);
      assert(!memcmp(script, "| ArcDroid desktop boot file\r", 29u));
      assert(!memcmp(script + 29u, request, sizeof(request) - 1u));
      size_t script_length = test_le32(root + 31u + 18u);
      assert(script_length > 29u + sizeof(request) - 1u);
      assert(!memcmp(script + 29u + sizeof(request) - 1u,
                     disabled + test_l_offset(1280u) + 29u,
                     script_length - 29u - (sizeof(request) - 1u)));
      const char *first_boot = strstr((const char *)script, "%Filer_Boot");
      assert(first_boot && (const uint8_t *)first_boot >= script + 29u + sizeof(request) - 1u);
   }
   ArcLaunchDiscovery maximum = {0};
   maximum.candidate_count = ARC_LAUNCH_MAX_CANDIDATES;
   maximum.application_count = 1u;
   for (unsigned i = 0u; i < maximum.candidate_count; ++i)
   {
      char name[11];
      assert(snprintf(name, sizeof(name), "!Boot%05u", i) == 10);
      maximum.candidates[i].name = test_guest_name(name, 10u);
      maximum.candidates[i].launchable = true;
      maximum.candidates[i].has_boot = i + 1u != maximum.candidate_count;
      maximum.candidates[i].state = i + 1u == maximum.candidate_count ?
         ARC_LAUNCH_CANDIDATE_APPLICATION : ARC_LAUNCH_CANDIDATE_NOT_APPLICATION;
   }
   char error[128];
   assert(arc_launch_disc_build_ramdisc(&maximum, maximum.candidate_count - 1u,
                                       true, enabled, error, sizeof(error)));
   free(enabled); free(disabled); free(legacy);
   puts("launch-ramdisc-unit: passed");
}

int main(int argc, char **argv)
{
   assert(argc == 2 || (argc == 3 && (!strcmp(argv[2], "--short-tail") ||
                                          !strcmp(argv[2], "--launch-ramdisc"))));
   if (argc == 3)
   {
      if (!strcmp(argv[2], "--launch-ramdisc")) test_launch_ramdisc();
      else { test_short_free_map(argv[1]); puts("short-tail-unit: passed"); }
      return 0;
   }
   test_hashes();
   test_cmos_unplug_policy();
   test_fresh_cmos_default();
   test_boot_stamp_starting_cmos();
   test_firmware_table();
   test_assembly();
   test_media_launch_and_cmos();
   test_launch_bindings();
   test_launch_choices_and_chooser();
   test_boot_hit_chooser();
   test_launch_discovery_and_builder(argv[1]);
   test_nested_launch(argv[1]);
   test_f_media(argv[1]);
   test_adfs_recognition();
   test_short_media_policy();
   test_playlists();
   test_folder_sets();
   test_jfd_parser(argv[1]);
   test_input();
   test_input_mapping();
   test_osk();
   puts("core_tests: all checks passed");
   return 0;
}
