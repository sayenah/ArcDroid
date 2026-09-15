/* ArcDroid deterministic core tests. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "e_format_fixture.h"
#include "jfd.h"
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
   assert(!arc_media_inspect("disc.adf", raw, 1638400u, &media, error, sizeof(error)));
   assert(strstr(error, "high-density"));
   assert(!arc_media_inspect("disc.adf", raw, 42u, &media, error, sizeof(error)));
   free(raw);

   ArcLaunchPlan plan;
   const char id[] = "0123456789012345678901234567890123456789";
   assert(arc_launch_plan_default(&plan, ARC_MEDIA_ADF_800K, ARC_OS_RISCOS_311,
                                  id, false, error, sizeof(error)));
   assert(strcmp(plan.machine, "A3000") == 0 && strcmp(plan.cpu, "ARM2") == 0);
   assert(!arc_launch_plan_default(&plan, ARC_MEDIA_ADF_800K, ARC_OS_RISCOS_311,
                                   id, true, error, sizeof(error)));
   assert(strstr(error, "incompatible"));

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
   assert(arc_cmos_default(ARC_OS_RISCOS_311, payload));
   assert(payload[0] || payload[1] || payload[2]);
   assert(arc_cmos_default(ARC_OS_TEST, payload));
   for (unsigned i = 0; i < sizeof(payload); ++i) assert(payload[i] == 0u);
   uint8_t machine_cmos[ARC_CMOS_MACHINE_SIZE];
   assert(arc_cmos_default(ARC_OS_RISCOS_311, payload));
   assert(arc_cmos_machine_image(ARC_OS_RISCOS_311, payload, false, machine_cmos));
   assert(machine_cmos[69] == 8u && !(machine_cmos[75] & 7u));
   assert((machine_cmos[80] & 0x10u) && (machine_cmos[199] & 7u) == 1u);
   assert(arc_cmos_machine_image(ARC_OS_RISCOS_311, payload, true, machine_cmos));
   assert((machine_cmos[75] & 7u) == 1u && (machine_cmos[199] & 7u) == 2u);
   payload[42] = 0x5au;
   payload[59] = 0xffu;
   payload[183] = 0xfau;
   arc_cmos_normalise_payload(payload);
   assert(payload[42] == 0x5au && (payload[59] & 7u) == 0u &&
          (payload[183] & 7u) == 1u);
   unsigned checksum = 0u;
   for (unsigned i = 0; i < 239u; ++i)
   {
      unsigned raw = i + 0x40u;
      if (raw > 255u) raw -= 240u;
      checksum += machine_cmos[raw];
   }
   assert(machine_cmos[63] == (uint8_t)(checksum + 1u));
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
      {"launch-family/multiple-exact-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Beta", 0u, 2u, 2u, 1, ARC_LAUNCH_REASON_DISC_NAME_MATCH, true},
      {"launch-family/multiple-trailing-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Beta2", 0u, 2u, 2u, 1, ARC_LAUNCH_REASON_DISC_NAME_MATCH, true},
      {"launch-family/ambiguous-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Other", 0u, 3u, 2u, -1, ARC_LAUNCH_REASON_AMBIGUOUS, true},
      {"launch-family/no-run-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Fixture", 0u, 2u, 0u, -1, ARC_LAUNCH_REASON_NO_APPLICATIONS, true},
      {"launch-family/incomplete-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Fixture", 0u, 1u, 0u, -1, ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE, true},
      {"launch-family/more-eight-d.adf", ARC_GEOMETRY_800K_D, 1024u, "Hugo", "Fixture", 0u, 10u, 1u, 9, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/fragmented-e.adf", ARC_GEOMETRY_800K_E, 2048u, "Hugo", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/unresolved-e.adf", ARC_GEOMETRY_800K_E, 2048u, "Hugo", "Demo", 0u, 0u, 0u, -1, ARC_LAUNCH_REASON_UNREADABLE, false},
      {"launch-family/illegal-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Fixture", 0u, 0u, 0u, -1, ARC_LAUNCH_REASON_UNREADABLE, false},
      {"launch-family/boot-0-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Demo", 0u, 1u, 1u, 0, ARC_LAUNCH_REASON_SINGLE_APPLICATION, true},
      {"launch-family/boot-1-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Demo", 1u, 1u, 1u, -1, ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF, true},
      {"launch-family/boot-2-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Demo", 2u, 1u, 1u, -1, ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF, true},
      {"launch-family/boot-3-l.adl", ARC_GEOMETRY_640K_L, 512u, "Hugo", "Demo", 3u, 1u, 1u, -1, ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF, true},
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
          !strcmp(discovery.candidates[0].name, "!Demo"));
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
   assert(!discovery.readable && !discovery.candidate_count &&
          !discovery.application_count && discovery.target_index < 0 &&
          discovery.reason == ARC_LAUNCH_REASON_UNREADABLE);
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
             !strcmp(discovery.candidates[0].name, "!Demo"));
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

   uint8_t *first = malloc(ARC_LAUNCH_DISC_SIZE);
   uint8_t *second = malloc(ARC_LAUNCH_DISC_SIZE);
   assert(first && second && arc_launch_disc_build("Demo", "!Demo", first) &&
          arc_launch_disc_build("Demo", "!Demo", second));
   assert(!memcmp(first, second, ARC_LAUNCH_DISC_SIZE));
   assert(!strcmp(arc_digest(first, ARC_LAUNCH_DISC_SIZE).sha1,
                  "507ee32561fa0a2fbc7c1044a6d92e63a0037dad"));
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
                 "Dir ADFS::Demo.$\rURD ADFS::Demo.$\rFiler_Run ADFS::Demo.$.!Demo\r"));
   static const char *bad_targets[] = {
      "!Bad.Name", "!Bad:Name", "!Bad<Name", "!Bad>Name", "!Bad|Name",
      "!Bad$Name", "!Bad&Name", "!Bad@Name", "!Bad^Name", "!Bad%Name",
      "!Bad*Name", "!Bad#Name", "!Bad\"Name", "!Bad Name", "!Bad\001",
      "!Bad\177", "!Bad\200"
   };
   for (size_t i = 0u; i < sizeof(bad_targets) / sizeof(bad_targets[0]); ++i)
      assert(!arc_launch_disc_build("Demo", bad_targets[i], second));
   assert(arc_launch_disc_build("Bad.Name", "!Demo", second));
   assert(strstr((const char *)(second + test_l_offset(1280u)),
                 "Filer_Run ADFS::0.$.!Demo\r"));

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
   assert(!arc_media_inspect("e.adf", image, 819200u - 256u,
                             &media, error, sizeof(error)));
   assert(strstr(error, "sector-aligned"));
   assert(!arc_media_inspect("e.adf", image, 819200u - 10241u,
                             &media, error, sizeof(error)));
   assert(!arc_media_inspect("e.adf", image, 819200u - 11264u,
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
      {819200u, 12u, 1024u, 10240u, ARC_GEOMETRY_800K_D},
      {819200u, 3180u, 1024u, 10240u, ARC_GEOMETRY_800K_D},
      {819200u, 3200u, 1024u, 10240u, ARC_GEOMETRY_800K_D},
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
   memcpy(rejected, plain, sizeof(rejected)); store_u32(rejected + 312u, 4u << 16u);
   assert(!arc_jfd_parse(rejected, sizeof(rejected), &parsed, error, sizeof(error)) && strstr(error, "quad/octal"));
   memcpy(rejected, plain, sizeof(rejected)); store_u32(rejected + 312u, (1u << 20u) | (1u << 16u));
   assert(!arc_jfd_parse(rejected, sizeof(rejected), &parsed, error, sizeof(error)) && strstr(error, "DiscOp 3"));
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
      assert(keys[i].value && keys[i].label && keys[i].key < ARC_INPUT_KEY_COUNT);
      assert(keys[i].kart_row <= 6u && keys[i].kart_column <= 15u);
      assert(arc_input_key_value(keys[i].value) == &keys[i]);
      assert(arc_input_is_modifier(keys[i].key) == keys[i].modifier);
      for (size_t prior = 0u; prior < i; ++prior)
         assert(strcmp(keys[i].value, keys[prior].value));
   }
   assert(!arc_input_key_value("pound"));
   const ArcInputKey *backslash = arc_input_key_value("hash_backslash");
   assert(backslash && !strcmp(backslash->label, "Backslash") &&
          backslash->kart_row == 3u && backslash->kart_column == 3u);
   assert(!arc_input_key(RETROK_PRINT));
   size_t osk_only_count = 0u;
   const ArcInputKey *osk_only = arc_input_osk_only_keys(&osk_only_count);
   static const unsigned osk_codes[] = {
      RETROK_NUMLOCK, RETROK_SCROLLOCK, RETROK_PAUSE
   };
   static const unsigned osk_rows[] = {2u, 0u, 0u};
   static const unsigned osk_columns[] = {2u, 14u, 15u};
   assert(osk_only && osk_only_count == 3u);
   for (size_t i = 0u; i < osk_only_count; ++i)
      assert(osk_only[i].key == osk_codes[i] &&
             osk_only[i].kart_row == osk_rows[i] &&
             osk_only[i].kart_column == osk_columns[i] &&
             !osk_only[i].modifier);
   assert(arc_input_profiles_validate(NULL, 0u));

   ArcInputProfile test_profile = {
      .id = "test-profile",
      .name = "Generated Test Profile",
      .digests = {"0123456789abcdef0123456789abcdef01234567"},
      .digest_count = 1u,
      .launch_target = "!TEST",
      .has_port1 = true,
      .evidence = "project-authored core test entry"
   };
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      memcpy(test_profile.mapping[port], arc_input_generic_mapping(),
             sizeof(test_profile.mapping[port]));
   assert(arc_input_profiles_validate_table(&test_profile, 1u, NULL, 0u));
   ArcInputProfile invalid_profile = test_profile;
   invalid_profile.digests[0] = "0123456789ABCDEF0123456789ABCDEF01234567";
   assert(!arc_input_profiles_validate_table(&invalid_profile, 1u, NULL, 0u));
   ArcInputProfile duplicate_profiles[2] = {test_profile, test_profile};
   duplicate_profiles[1].id = "other-profile";
   assert(!arc_input_profiles_validate_table(duplicate_profiles, 2u, NULL, 0u));

   ArcInputOverride overrides[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT];
   memset(overrides, 0, sizeof(overrides));
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
         overrides[port][input].automatic = true;
   ArcInputResolution resolution;
   assert(arc_input_resolve("0000000000000000000000000000000000000000", "!TEST",
                            "Auto", overrides, &resolution));
   assert(!strcmp(resolution.profile_name, "Generic") &&
          resolution.binding[0][ARC_PAD_A].key == RETROK_SPACE &&
          resolution.source[0][ARC_PAD_A] == ARC_MAPPING_SOURCE_GENERIC);
   assert(arc_input_resolve("0000000000000000000000000000000000000000", "!TEST",
                            "Generic", overrides, &resolution));
   overrides[1][ARC_PAD_A].automatic = false;
   overrides[1][ARC_PAD_A].binding = (ArcInputBinding){true, RETROK_LSHIFT};
   overrides[0][ARC_PAD_B].automatic = false;
   overrides[0][ARC_PAD_B].binding = (ArcInputBinding){false, 0u};
   assert(arc_input_resolve("0000000000000000000000000000000000000000", "!TEST",
                            "Auto", overrides, &resolution));
   assert(resolution.custom && !resolution.binding[0][ARC_PAD_B].bound &&
          resolution.binding[1][ARC_PAD_A].key == RETROK_LSHIFT &&
          resolution.source[1][ARC_PAD_A] == ARC_MAPPING_SOURCE_OPTION);
   overrides[0][ARC_PAD_B].automatic = true;
   overrides[1][ARC_PAD_A].automatic = true;
   assert(arc_input_resolve_table(&test_profile, 1u,
            "0123456789abcdef0123456789abcdef01234567", "!TEST", "Auto",
            overrides, &resolution));
   assert(!strcmp(resolution.profile_id, "test-profile") &&
          resolution.source[0][ARC_PAD_A] == ARC_MAPPING_SOURCE_PROFILE);
   assert(arc_input_resolve_table(&test_profile, 1u,
            "0000000000000000000000000000000000000000", "!OTHER",
            "test-profile", overrides, &resolution));
   assert(resolution.source[0][ARC_PAD_A] == ARC_MAPPING_SOURCE_PRESET);

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
}

static unsigned drain_input(ArcInputState *state, ArcInputEvent *events,
                            unsigned capacity)
{
   unsigned count = 0u;
   while (count < capacity && arc_input_consume(state, true, &events[count])) ++count;
   return count;
}

static void test_osk(void)
{
   ArcOsk osk = {0};
   ArcInputState input;
   ArcInputEvent events[16];
   arc_input_reset(&input);
   assert(!arc_osk_open(&osk, 256u, 256u) && !osk.disabled && !osk.visible);
   memset(&osk, 0, sizeof(osk));
   assert(arc_osk_open(&osk, 320u, 256u) && osk.visible && osk.compact &&
          osk.page_count == 2u && osk.key_count > 40u);
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

   assert(arc_osk_layout(&osk, 640u, 480u) && !osk.compact);
   unsigned shift = UINT_MAX, right_shift = UINT_MAX, letter = UINT_MAX;
   for (unsigned i = 0u; i < osk.key_count; ++i)
   {
      if (osk.keys[i].key == RETROK_LSHIFT) shift = i;
      if (osk.keys[i].key == RETROK_RSHIFT) right_shift = i;
      if (osk.keys[i].key == RETROK_a) letter = i;
   }
   bool saw_num_lock = false, saw_scroll_lock = false, saw_pause = false;
   for (unsigned i = 0u; i < osk.key_count; ++i)
   {
      saw_num_lock |= osk.keys[i].key == RETROK_NUMLOCK;
      saw_scroll_lock |= osk.keys[i].key == RETROK_SCROLLOCK;
      saw_pause |= osk.keys[i].key == RETROK_PAUSE;
      assert(osk.keys[i].key != RETROK_PRINT);
   }
   assert(saw_num_lock && saw_scroll_lock && saw_pause);
   assert(shift != UINT_MAX && right_shift != UINT_MAX && letter != UINT_MAX);
   osk.focus = shift;
   assert(arc_osk_press_focused(&osk, &input) &&
          osk.modifiers[0] == ARC_OSK_MOD_ONESHOT);
   osk.focus = letter;
   assert(arc_osk_press_focused(&osk, &input));
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 2u &&
          events[0].code == RETROK_LSHIFT && events[0].down &&
          events[1].code == RETROK_a && events[1].down);
   arc_osk_release_activation(&osk);
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 2u &&
          events[0].code == RETROK_a && !events[0].down &&
          events[1].code == RETROK_LSHIFT && !events[1].down);
   arc_osk_update(&osk, &input);
   assert(!osk.activation_phase && osk.modifiers[0] == ARC_OSK_MOD_OFF);

   /* Left and right modifiers have independent sticky state and emit their
    * own physical key identities. */
   osk.focus = right_shift;
   assert(arc_osk_press_focused(&osk, &input) &&
          osk.modifiers[1] == ARC_OSK_MOD_ONESHOT &&
          osk.modifiers[0] == ARC_OSK_MOD_OFF);
   osk.focus = letter;
   assert(arc_osk_press_focused(&osk, &input));
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 2u &&
          events[0].code == RETROK_RSHIFT && events[0].down &&
          events[1].code == RETROK_a && events[1].down);
   arc_osk_release_activation(&osk);
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 2u &&
          events[0].code == RETROK_a && !events[0].down &&
          events[1].code == RETROK_RSHIFT && !events[1].down);
   arc_osk_update(&osk, &input);
   assert(!osk.activation_phase && osk.modifiers[1] == ARC_OSK_MOD_OFF);

   /* Locked modifiers persist for another activation but never remain as an
    * invisible source level between activations. */
   osk.focus = shift;
   assert(arc_osk_press_focused(&osk, &input));
   assert(arc_osk_press_focused(&osk, &input) &&
          osk.modifiers[0] == ARC_OSK_MOD_LOCKED);
   osk.focus = letter;
   assert(arc_osk_press_focused(&osk, &input));
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 2u &&
          events[0].code == RETROK_LSHIFT && events[0].down &&
          events[1].code == RETROK_a && events[1].down);
   arc_osk_release_activation(&osk);
   arc_osk_update(&osk, &input);
   assert(drain_input(&input, events, 16u) == 2u &&
          events[0].code == RETROK_a && !events[0].down &&
          events[1].code == RETROK_LSHIFT && !events[1].down);
   arc_osk_update(&osk, &input);
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
   arc_osk_update(&osk, &input);
   assert(!arc_input_pending(&input) && !osk.activation_phase &&
          arc_input_source_level(&input, ARC_INPUT_SOURCE_KEYBOARD, RETROK_a) &&
          arc_input_effective_level(&input, RETROK_a));

   /* Reconciliation invalidates an activation once and recovers OSK levels. */
   for (unsigned attempt = 0u; attempt < 3u; ++attempt)
   {
      assert(arc_osk_press_focused(&osk, &input));
      arc_osk_update(&osk, &input);
      arc_input_release_all(&input);
      arc_osk_update(&osk, &input);
      assert(!osk.activation_phase && arc_osk_take_interrupted(&osk) &&
             !arc_osk_take_interrupted(&osk));
      while (arc_input_pending(&input))
         (void)arc_input_consume(&input, true, &events[0]);
   }

   /* A full queue reports busy and does not start a partial chord. */
   arc_input_reset(&input);
   for (unsigned i = 0u; i < ARC_INPUT_FIFO_CAPACITY / 2u; ++i)
   {
      arc_input_source_key(&input, ARC_INPUT_SOURCE_KEYBOARD, RETROK_b, true, true);
      arc_input_source_key(&input, ARC_INPUT_SOURCE_KEYBOARD, RETROK_b, false, true);
   }
   assert(!arc_osk_shortcut(&osk, &input, RETROK_RETURN) && osk.busy);

   static const unsigned widths[] = {320u, 640u, 640u};
   static const unsigned heights[] = {256u, 256u, 480u};
   static const char *goldens[3][2][2] = {
      {{"f465e83bcb842199e59411fd9ea7ca65c3ebcaa2", "acd9758a101ce2c63f18271ea1e9a58908c0644c"},
       {"79b7a0e3d1622fe46db0b836ad4c9aabf59dc6c1", "701c405ab64806ace5cee554621fbf245d7124fb"}},
      {{"7018adb4dfffd417c4917451b09e7a175f4aa474", NULL},
       {"9def35c5d2f6ab41d2f1253c361bf9cb4fba3e38", NULL}},
      {{"f1cf1d564fd3ae464107fcfc2432560e5cbdc57b", NULL},
       {"fe14df35a3cdf2befb4dbe68d381ef421d204eb7", NULL}}
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
            assert(goldens[geometry][alpha][page] &&
                   !strcmp(digest, goldens[geometry][alpha][page]));
         }
         assert(surface[0] == UINT32_C(0xdeadbeef) &&
                surface[count + 1u] == UINT32_C(0xcafef00d));
         free(surface);
      }
}

int main(int argc, char **argv)
{
   assert(argc == 2);
   test_hashes();
   test_firmware_table();
   test_assembly();
   test_media_launch_and_cmos();
   test_launch_discovery_and_builder(argv[1]);
   test_adfs_recognition();
   test_short_media_policy();
   test_playlists();
   test_jfd_parser(argv[1]);
   test_input();
   test_input_mapping();
   test_osk();
   puts("core_tests: all checks passed");
   return 0;
}
