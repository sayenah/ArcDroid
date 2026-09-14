/* ArcDroid deterministic core tests. GPL-2.0-only. */
#include "arcdroid_core.h"

#include <assert.h>
#include <stdio.h>
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

static void test_input(void)
{
   ArcInputState state;
   arc_input_reset(&state);
   arc_input_key(&state, 42u, true, false);
   arc_input_key(&state, 42u, true, true);
   arc_input_key(&state, 42u, false, false);
   assert(arc_input_key_down(&state, 42u));
   arc_input_key(&state, 42u, false, true);
   assert(!arc_input_key_down(&state, 42u));

   arc_input_mouse_add(&state, 150, -90, 3u);
   int x, y; unsigned buttons;
   arc_input_mouse_packet(&state, &x, &y, &buttons);
   assert(x == 63 && y == -63 && buttons == 3u);
   arc_input_mouse_packet(&state, &x, &y, &buttons);
   assert(x == 63 && y == -27);
   arc_input_mouse_packet(&state, &x, &y, &buttons);
   assert(x == 24 && y == 0);

   arc_input_reset(&state);
   arc_input_pointer(&state, true, 1000, 1000, 1u);
   arc_input_mouse_packet(&state, &x, &y, &buttons);
   assert(x == 0 && y == 0 && buttons == 1u);
   arc_input_pointer(&state, true, 1025, 970, 1u);
   arc_input_mouse_packet(&state, &x, &y, &buttons);
   assert(x == 25 && y == -30 && buttons == 1u);
   arc_input_pointer(&state, false, 0, 0, 0u);
   arc_input_mouse_packet(&state, &x, &y, &buttons);
   assert(buttons == 0u);

   arc_input_reset(&state);
   arc_input_pointer_scaled(&state, true, -32767, -32767, 1u, 640u, 480u);
   arc_input_pointer_scaled(&state, true, 32767, 32767, 1u, 640u, 480u);
   int total_x = 0, total_y = 0;
   unsigned packets = 0u;
   do
   {
      arc_input_mouse_packet(&state, &x, &y, &buttons);
      total_x += x; total_y += y; ++packets;
   } while (x || y);
   assert(total_x == 640 && total_y == 480 && packets <= 12u);
}

int main(void)
{
   test_hashes();
   test_firmware_table();
   test_assembly();
   test_media_launch_and_cmos();
   test_input();
   puts("core_tests: all checks passed");
   return 0;
}
