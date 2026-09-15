/* ArcDroid deterministic core tests. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "e_format_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
   assert(arc_media_inspect("renamed.AdF", 819200u, error, sizeof(error)) == ARC_MEDIA_ADF_800K);
   assert(arc_media_inspect("disc.adl", 655360u, error, sizeof(error)) == ARC_MEDIA_ADL_640K);
   assert(arc_media_inspect("size-first.adl", 819200u, error, sizeof(error)) == ARC_MEDIA_ADF_800K);
   assert(arc_media_inspect("size-first.adf", 655360u, error, sizeof(error)) == ARC_MEDIA_ADL_640K);
   assert(arc_media_inspect("disc.adf", 1638400u, error, sizeof(error)) == ARC_MEDIA_ADF_HD_UNSUPPORTED);
   assert(strstr(error, "high-density"));
   assert(arc_media_inspect("disc.adf", 42u, error, sizeof(error)) == ARC_MEDIA_INVALID);

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
   assert(arc_cmos_machine_image(ARC_OS_RISCOS_311, payload, machine_cmos));
   assert(machine_cmos[69] == 8u && !(machine_cmos[75] & 7u));
   assert((machine_cmos[80] & 0x10u) && (machine_cmos[199] & 7u) >= 1u);
   unsigned checksum = 0u;
   for (unsigned i = 0; i < 239u; ++i)
   {
      unsigned raw = i + 0x40u;
      if (raw > 255u) raw -= 240u;
      checksum += machine_cmos[raw];
   }
   assert(machine_cmos[63] == (uint8_t)(checksum + 1u));
}

static uint8_t test_old_map_checksum(const uint8_t *block)
{
   unsigned checksum = 0xffu;
   for (unsigned index = 0u; index < 255u; ++index)
   {
      if (checksum > 0xffu) checksum = (checksum + 1u) & 0xffu;
      checksum += block[index];
   }
   return (uint8_t)checksum;
}

static void test_adfs_recognition(void)
{
   uint8_t old_map[512] = {0};
   old_map[255] = test_old_map_checksum(old_map);
   old_map[511] = test_old_map_checksum(old_map + 256u);
   assert(arc_media_is_recognised_adfs(old_map, sizeof(old_map)));

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

int main(void)
{
   test_hashes();
   test_firmware_table();
   test_assembly();
   test_media_launch_and_cmos();
   test_adfs_recognition();
   test_input();
   puts("core_tests: all checks passed");
   return 0;
}
