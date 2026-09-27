/* Fixed boot-observation grammar and stamp comparison. GPL-2.0-only. */
#include "boot_observation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
   ArcBootObservation stamp = {0}, parsed;
   memset(stamp.firmware, 'a', 40); stamp.firmware[40] = 0;
   memset(stamp.media, 'b', 40); stamp.media[40] = 0;
   memset(stamp.cmos, 'c', 40); stamp.cmos[40] = 0;
   stamp.machine = 2; stamp.cpu = 1; stamp.memory = 4096;
   stamp.jfd_boot = 1; stamp.disk_writes = 2; stamp.install_saves = 1;
   uint8_t bytes[ARC_BOOT_OBSERVATION_SIZE + 1u];
   memset(bytes, 0, sizeof(bytes));
   assert(!arc_boot_observation_format(&stamp, bytes,
                                      ARC_BOOT_OBSERVATION_SIZE - 1u));
   assert(arc_boot_observation_format(&stamp, bytes,
                                     ARC_BOOT_OBSERVATION_SIZE));
   assert(arc_boot_observation_parse(bytes, ARC_BOOT_OBSERVATION_SIZE, &parsed));
   assert(arc_boot_observation_equal(&stamp, &parsed));
   assert(!arc_boot_observation_parse(bytes, ARC_BOOT_OBSERVATION_SIZE - 1u, &parsed));
   assert(!arc_boot_observation_parse(bytes, sizeof(bytes), &parsed));
   for (unsigned offset = 0; offset < ARC_BOOT_OBSERVATION_SIZE; ++offset)
   {
      uint8_t original = bytes[offset];
      bytes[offset] ^= 0x80u;
      if (offset < 136u)
         assert(!arc_boot_observation_parse(bytes, ARC_BOOT_OBSERVATION_SIZE, &parsed));
      bytes[offset] = original;
   }
   for (unsigned field = 0; field < 6u; ++field)
   {
      ArcBootObservation other = stamp;
      uint32_t *values[] = {&other.machine, &other.cpu, &other.memory,
         &other.jfd_boot, &other.disk_writes, &other.install_saves};
      ++*values[field];
      assert(!arc_boot_observation_equal(&stamp, &other));
   }
   const uint32_t invalid[] = {4u, 2u, 1023u, 3u, 3u, 2u};
   const unsigned offsets[] = {136u, 140u, 144u, 148u, 152u, 156u};
   for (unsigned field = 0; field < 6u; ++field)
   {
      uint8_t saved[4];
      memcpy(saved, bytes + offsets[field], sizeof(saved));
      for (unsigned byte = 0; byte < 4u; ++byte)
         bytes[offsets[field] + byte] =
            (uint8_t)(invalid[field] >> (8u * byte));
      assert(!arc_boot_observation_parse(bytes, ARC_BOOT_OBSERVATION_SIZE,
                                         &parsed));
      memcpy(bytes + offsets[field], saved, sizeof(saved));
   }
   bytes[16] = 'A';
   assert(!arc_boot_observation_parse(bytes, ARC_BOOT_OBSERVATION_SIZE,
                                      &parsed));
   bytes[16] = 'a';
   ArcBootObservation other = stamp;
   other.firmware[0] = 'd';
   assert(!arc_boot_observation_equal(&stamp, &other));
   other = stamp; other.media[0] = 'd';
   assert(!arc_boot_observation_equal(&stamp, &other));
   other = stamp; other.cmos[0] = 'd';
   assert(!arc_boot_observation_equal(&stamp, &other));
   puts("boot-observation: passed");
   return 0;
}
