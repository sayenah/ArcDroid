/* ArcDroid headless video backend. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arcdroid_platform.h"
#include "plat_video.h"
#include "arc_state.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARC_VIDEO_MAX_WIDTH 4096
#define ARC_VIDEO_MAX_HEIGHT 2048

static uint32_t *arc_video_frame;
static uint16_t arc_video_rows[2048][2];
static unsigned arc_video_width = 640u;
static unsigned arc_video_height = 480u;
static uint64_t arc_video_sequence;
int selected_video_renderer = RENDERER_SOFTWARE;

void arc_platform_video_reset(void)
{
   arc_video_width = 640u;
   arc_video_height = 480u;
   arc_video_sequence = 0u;
   memset(arc_video_rows,0,sizeof(arc_video_rows));
   if (arc_video_frame)
      memset(arc_video_frame, 0,
             (size_t)ARC_VIDEO_MAX_WIDTH * ARC_VIDEO_MAX_HEIGHT * sizeof(*arc_video_frame));
}

int video_renderer_init(void *window)
{
   (void)window;
#if defined(ARCDROID_TEST_FAULTS)
   const char *fail = getenv("ARCDROID_TEST_FAIL_VIDEO_ALLOC");
   if (fail && fail[0] == '1') return 0;
#endif
   if (!arc_video_frame)
      arc_video_frame = calloc((size_t)ARC_VIDEO_MAX_WIDTH * ARC_VIDEO_MAX_HEIGHT,
                               sizeof(*arc_video_frame));
   arc_platform_video_reset();
   return arc_video_frame != NULL;
}

int video_renderer_reinit(void *window)
{
   (void)window;
   return arc_video_frame != NULL || video_renderer_init(NULL);
}

void video_renderer_close(void)
{
   free(arc_video_frame);
   arc_video_frame = NULL;
   arc_video_sequence = 0u;
}

int video_renderer_available(int id) { return id == RENDERER_SOFTWARE; }
char *video_renderer_get_name(int id) { (void)id; return "ArcDroid"; }
int video_renderer_get_id(char *name) { (void)name; return RENDERER_SOFTWARE; }

void video_renderer_update(BITMAP *source, int source_x, int source_y,
                           int dest_x, int dest_y, int width, int height)
{
   if (!arc_video_frame || !source || !source->dat || width <= 0 || height <= 0)
      return;
   if (source_x < 0) { dest_x -= source_x; width += source_x; source_x = 0; }
   if (source_y < 0) { dest_y -= source_y; height += source_y; source_y = 0; }
   if (dest_x < 0) { source_x -= dest_x; width += dest_x; dest_x = 0; }
   if (dest_y < 0) { source_y -= dest_y; height += dest_y; dest_y = 0; }
   if (source_x + width > source->w) width = source->w - source_x;
   if (source_y + height > source->h) height = source->h - source_y;
   if (dest_x + width > ARC_VIDEO_MAX_WIDTH) width = ARC_VIDEO_MAX_WIDTH - dest_x;
   if (dest_y + height > ARC_VIDEO_MAX_HEIGHT) height = ARC_VIDEO_MAX_HEIGHT - dest_y;
   if (width <= 0 || height <= 0) return;
   for (int y = 0; y < height; ++y) {
      arc_state_raster_mark(arc_video_rows,(unsigned)(dest_y+y),(unsigned)dest_x,(unsigned)width);
      memcpy(arc_video_frame + (size_t)(dest_y + y) * ARC_VIDEO_MAX_WIDTH + dest_x,
             source->line[source_y + y] + (size_t)source_x * sizeof(uint32_t),
             (size_t)width * sizeof(uint32_t));
   }
}

void video_renderer_present(int source_x, int source_y, int width, int height, int line_double)
{
   (void)source_x;
   (void)source_y;
   if (!arc_video_frame || width <= 0 || height <= 0) return;
   if (width > ARC_VIDEO_MAX_WIDTH) width = ARC_VIDEO_MAX_WIDTH;
   if (height > ARC_VIDEO_MAX_HEIGHT) height = ARC_VIDEO_MAX_HEIGHT;
   if (line_double && height <= ARC_VIDEO_MAX_HEIGHT / 2)
   {
      for (int y = height - 1; y >= 0; --y)
      {
         uint32_t *source = arc_video_frame + (size_t)y * ARC_VIDEO_MAX_WIDTH;
         arc_state_raster_mark(arc_video_rows,(unsigned)y*2u,0,(unsigned)width);
         arc_state_raster_mark(arc_video_rows,(unsigned)y*2u+1u,0,(unsigned)width);
         memcpy(arc_video_frame + (size_t)(y * 2 + 1) * ARC_VIDEO_MAX_WIDTH,
                source, (size_t)width * sizeof(uint32_t));
         memcpy(arc_video_frame + (size_t)(y * 2) * ARC_VIDEO_MAX_WIDTH,
                source, (size_t)width * sizeof(uint32_t));
      }
      height *= 2;
   }
   arc_video_width = (unsigned)width;
   arc_video_height = (unsigned)height;
   ++arc_video_sequence;
}

const uint32_t *arc_platform_video_frame(unsigned *width, unsigned *height,
                                         size_t *pitch, uint64_t *sequence)
{
   if (width) *width = arc_video_width;
   if (height) *height = arc_video_height;
   if (pitch) *pitch = ARC_VIDEO_MAX_WIDTH * sizeof(uint32_t);
   if (sequence) *sequence = arc_video_sequence;
   return arc_video_frame;
}

void updatewindowsize(int width, int height) { (void)width; (void)height; }

#if defined(ARCDROID_SAVE_STATES)
#include "../engine/state-generated/arcdroid_video_state.inc"
#include "../engine/state-generated/platform_video_extra.inc"
void arc_state_visit_platform_arcdroid_video_extra(ArcState *s) { arc_state_platform_video_extra(s); }
#endif

#if defined(ARCDROID_TEST_FAULTS)
void arc_platform_test_seed_video_history(void)
{ if(arc_video_frame)arc_video_frame[4096u*2048u-1u]=0x00506070u;arc_state_raster_mark(arc_video_rows,2047u,4095u,1u); }
#endif
