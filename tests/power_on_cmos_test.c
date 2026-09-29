/* Slice 18 CMOS and internal input ownership checks. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "libretro.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void checksum(uint8_t image[ARC_CMOS_MACHINE_SIZE])
{
   unsigned sum = 0u;
   for (unsigned i = 0u; i < 239u; ++i)
      sum += image[arc_cmos_raw_address(i)];
   image[arc_cmos_raw_address(239u)] = (uint8_t)(sum + 1u);
}

int main(void)
{
   const ArcOsClass oses[] = {ARC_OS_RISCOS_300, ARC_OS_RISCOS_310,
      ARC_OS_RISCOS_311, ARC_OS_RISCOS_319};
   for (unsigned n = 0u; n < 4u; ++n)
      for (ArcIoClass io = ARC_IO_OLD_WD1770; io <= ARC_IO_NEW_82C711; ++io)
      {
         uint8_t fresh[ARC_CMOS_PAYLOAD_SIZE], image[ARC_CMOS_MACHINE_SIZE];
         uint8_t old[ARC_CMOS_MACHINE_SIZE], validated[ARC_CMOS_MACHINE_SIZE];
         assert(arc_cmos_default(oses[n], io, fresh));
         assert(!(fresh[64u] & 0x10u));
         assert(arc_cmos_machine_image(oses[n], io, fresh, false, image));
         assert(!(image[80u] & 0x10u));
         assert(arc_cmos_power_on_shift(oses[n], image));
         memcpy(old, image, sizeof(old));
         old[80u] |= 0x10u; /* main's only additional image mutation */
         checksum(old);
         for (unsigned i = 0u; i < sizeof(old); ++i)
            if (i != 80u && i != arc_cmos_raw_address(239u))
               assert(old[i] == image[i]);
         memcpy(validated, image, sizeof(validated));
         checksum(validated);
         assert(!memcmp(validated, image, sizeof(image)));
         fresh[64u] = 0xbfu;
         assert(arc_cmos_machine_image(oses[n], io, fresh, false, image));
         assert(image[80u] == 0xbfu); /* live guest value retained */
         assert(!arc_cmos_power_on_shift(oses[n], image));
         arc_cmos_normalise_payload(fresh, oses[n], io);
         assert(fresh[64u] == 0xafu); /* only Boot launch-owned */
      }
   const ArcOsClass others[] = {ARC_OS_RISCOS_200, ARC_OS_RISCOS_201,
      ARC_OS_ARTHUR_030, ARC_OS_ARTHUR_120, ARC_OS_TEST};
   for (unsigned n = 0u; n < 5u; ++n)
      for (ArcIoClass io = ARC_IO_OLD_WD1770; io <= ARC_IO_NEW_82C711; ++io)
      {
         uint8_t payload[ARC_CMOS_PAYLOAD_SIZE], image[ARC_CMOS_MACHINE_SIZE];
         assert(arc_cmos_default(others[n], io, payload));
         payload[64u] = 0x5au;
         assert(arc_cmos_machine_image(others[n], io, payload, false, image));
         assert(image[80u] == 0x5au);
         assert(!arc_cmos_power_on_shift(others[n], image));
         arc_cmos_normalise_payload(payload, others[n], io);
         assert(payload[64u] == 0x5au);
      }
   ArcInputState state = {0};
   ArcInputEvent event;
   arc_input_source_key(&state, ARC_INPUT_SOURCE_POWER_ON, RETROK_LSHIFT, true, true);
   arc_input_reset_frontend(&state);
   assert(arc_input_source_level(&state, ARC_INPUT_SOURCE_POWER_ON, RETROK_LSHIFT));
   ArcOsk osk = {0};
   assert(arc_osk_open(&osk, 640u, 480u));
   arc_osk_close(&osk, &state);
   arc_osk_reset(&osk, &state);
   assert(arc_input_source_level(&state, ARC_INPUT_SOURCE_POWER_ON, RETROK_LSHIFT));
   assert(arc_input_consume(&state, false, &event) && event.down && event.code == RETROK_LSHIFT);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, RETROK_LSHIFT, true, true);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_POWER_ON, RETROK_LSHIFT, false, true);
   assert(!arc_input_pending(&state) && arc_input_effective_level(&state, RETROK_LSHIFT));
   arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, RETROK_LSHIFT, false, true);
   assert(arc_input_consume(&state, false, &event) && !event.down);
   arc_input_reset(&state);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, RETROK_LSHIFT, true, true);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_POWER_ON, RETROK_LSHIFT, true, true);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_KEYBOARD, RETROK_LSHIFT, false, true);
   assert(arc_input_effective_level(&state, RETROK_LSHIFT));
   arc_input_source_key(&state, ARC_INPUT_SOURCE_POWER_ON, RETROK_LSHIFT, false, true);
   assert(arc_input_pending(&state) == 2u);
   assert(arc_input_consume(&state, false, &event) && event.down);
   assert(arc_input_consume(&state, false, &event) && !event.down);
   arc_input_reset(&state);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_POWER_ON, RETROK_LSHIFT, true, true);
   assert(arc_input_consume(&state, false, &event) && event.down);
   arc_input_source_key(&state, ARC_INPUT_SOURCE_POWER_ON, RETROK_LSHIFT, false, true);
   arc_input_reset_frontend(&state);
   assert(arc_input_consume(&state, false, &event) && !event.down);
   arc_input_reset(&state);
   assert(!arc_input_pending(&state));
   puts("power-on-cmos: passed");
   return 0;
}
