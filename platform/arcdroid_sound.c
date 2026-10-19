/* ArcDroid bounded engine-audio queue. GPL-2.0-only. */
#include "arcdroid_platform.h"
#include "plat_sound.h"

#include <stdint.h>
#include <string.h>

#define ARC_AUDIO_QUEUE_FRAMES 16384u
static int16_t arc_audio_queue[ARC_AUDIO_QUEUE_FRAMES * 2u];
static size_t arc_audio_read;
static size_t arc_audio_count;
static uint64_t arc_audio_underrun_frames;
static uint64_t arc_audio_overrun_frames;

void arc_platform_audio_reset(void)
{
   arc_audio_read = 0u;
   arc_audio_count = 0u;
   arc_audio_underrun_frames = 0u;
   arc_audio_overrun_frames = 0u;
   memset(arc_audio_queue, 0, sizeof(arc_audio_queue));
}

void sound_dev_init(void) { arc_platform_audio_reset(); }
void sound_dev_close(void) { arc_platform_audio_reset(); }

void sound_givebuffer(int16_t *buffer)
{
   if (!buffer) return;
   for (size_t i = 0; i < 2400u; ++i)
   {
      if (arc_audio_count == ARC_AUDIO_QUEUE_FRAMES)
      {
         arc_audio_read = (arc_audio_read + 1u) % ARC_AUDIO_QUEUE_FRAMES;
         --arc_audio_count;
         ++arc_audio_overrun_frames;
      }
      size_t write = (arc_audio_read + arc_audio_count) % ARC_AUDIO_QUEUE_FRAMES;
      arc_audio_queue[write * 2u] = buffer[i * 2u];
      arc_audio_queue[write * 2u + 1u] = buffer[i * 2u + 1u];
      ++arc_audio_count;
   }
}

void sound_givebufferdd(int16_t *buffer) { (void)buffer; }

size_t arc_platform_audio_read(int16_t *output, size_t capacity_frames)
{
   size_t count = arc_audio_count < capacity_frames ? arc_audio_count : capacity_frames;
   arc_audio_underrun_frames += capacity_frames - count;
   for (size_t i = 0; i < count; ++i)
   {
      size_t source = (arc_audio_read + i) % ARC_AUDIO_QUEUE_FRAMES;
      output[i * 2u] = arc_audio_queue[source * 2u];
      output[i * 2u + 1u] = arc_audio_queue[source * 2u + 1u];
   }
   arc_audio_read = (arc_audio_read + count) % ARC_AUDIO_QUEUE_FRAMES;
   arc_audio_count -= count;
   return count;
}

void arc_platform_audio_stats(uint64_t *underrun_frames, uint64_t *overrun_frames)
{
   if (underrun_frames) *underrun_frames = arc_audio_underrun_frames;
   if (overrun_frames) *overrun_frames = arc_audio_overrun_frames;
}

#if defined(ARCDROID_SAVE_STATES)
#include "../engine/state-generated/arcdroid_sound_state.inc"
#endif
