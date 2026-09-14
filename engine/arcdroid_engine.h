/* ArcDroid engine session API. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#ifndef ARCDROID_ENGINE_H
#define ARCDROID_ENGINE_H

#include "arcdroid_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
   const uint32_t *pixels;
   unsigned width;
   unsigned height;
   size_t pitch;
   uint64_t elapsed_us;
   double refresh_hz;
   bool field_completed;
} ArcEngineFrame;

typedef void (*ArcEngineLogCallback)(void *opaque, const char *message);

bool arc_engine_start(const uint8_t *firmware, size_t firmware_size,
                      const char *media_path,
                      bool media_writable,
                      const uint8_t cmos[ARC_CMOS_MACHINE_SIZE],
                      char *error, size_t error_size);
void arc_engine_set_log_callback(ArcEngineLogCallback callback, void *opaque);
bool arc_engine_reset(char *error, size_t error_size);
void arc_engine_stop(void);
bool arc_engine_run_frame(ArcInputState *input, ArcEngineFrame *frame);
size_t arc_engine_audio(int16_t *output, size_t capacity_frames);
void arc_engine_audio_stats(uint64_t *underrun_frames, uint64_t *overrun_frames);
bool arc_engine_media_eject(void);
bool arc_engine_media_insert(const char *path, bool writable,
                             char *error, size_t error_size);
bool arc_engine_get_av_info(unsigned *width, unsigned *height, double *refresh_hz);
void arc_engine_cmos(uint8_t output[ARC_CMOS_PAYLOAD_SIZE]);
bool arc_engine_faulted(const char **message);
bool arc_engine_active(void);

#endif
