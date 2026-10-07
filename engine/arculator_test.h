/* ArcDroid test-core-only reset observations. GPL-2.0-only. */
#ifndef ARCDROID_ARCULATOR_TEST_H
#define ARCDROID_ARCULATOR_TEST_H

#include <stdint.h>

typedef struct {
   int phase, line, in_display, displayon, displayon_latch;
   uint32_t video_address, cursor_address;
} ArcArculatorVidcResetState;

typedef struct {
   int64_t cpu_credit;
   int sound_request, cursor_request, video_request;
   uint64_t sound_timestamp, cursor_timestamp, video_timestamp;
   uint64_t video_start_timestamp;
   int video_enable, sound_enable, refresh_enable, refresh_always, do_refresh;
   ArcArculatorVidcResetState vidc;
} ArcArculatorResetState;

#endif
