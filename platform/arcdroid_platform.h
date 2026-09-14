/* ArcDroid headless Arculator platform bridge. GPL-2.0-only. */
#ifndef ARCDROID_PLATFORM_H
#define ARCDROID_PLATFORM_H

#include "arcdroid_core.h"

#include <stddef.h>
#include <stdint.h>

void arc_platform_video_reset(void);
const uint32_t *arc_platform_video_frame(unsigned *width, unsigned *height,
                                         size_t *pitch, uint64_t *sequence);
void arc_platform_audio_reset(void);
size_t arc_platform_audio_read(int16_t *output, size_t capacity_frames);
void arc_platform_audio_stats(uint64_t *underrun_frames, uint64_t *overrun_frames);
void arc_platform_input_apply(ArcInputState *input);

#endif
