/* ArcDroid engine session API. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#ifndef ARCDROID_ENGINE_H
#define ARCDROID_ENGINE_H

#include "arcdroid_core.h"
#include "jfd.h"
#include "arculator/src/ide.h"
#include "adffs.h"
#include "arc_boot_watch.h"

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
typedef void (*ArcEngineCmosIgnoredCallback)(void *opaque, unsigned raw,
                                               uint8_t attempted, uint8_t retained);

typedef struct {
   const char *path;
   bool writable;
   ArcMediaKind kind;
   ArcMediaGeometry geometry;
   const ArcJfdImage *jfd;
   const uint8_t *data;
   size_t size;
   size_t effective_extent; /* ARC_MEDIA_EXTENT_UNRESTRICTED or raw prefix bytes. */
} ArcEngineMedia;

#define ARC_MEDIA_EXTENT_UNRESTRICTED 0u

bool arc_engine_start(const uint8_t *firmware, size_t firmware_size,
                      const ArcEngineMedia *media,
                      const ArcEngineMedia *helper,
                      const ArcMachineDescriptor *machine,
                      const ArcIdeBackend *ide_backend,
                      const ArcInstallDoorbell *doorbell,
                      const uint8_t cmos[ARC_CMOS_MACHINE_SIZE],
                      const uint8_t locked_raw_mask[32],
                      ArcEngineCmosIgnoredCallback ignored_callback,
                      void *ignored_opaque,
                      char *error, size_t error_size);
void arc_engine_set_log_callback(ArcEngineLogCallback callback, void *opaque);
bool arc_engine_reset(char *error, size_t error_size);
void arc_engine_stop(void);
bool arc_engine_run_frame(ArcInputState *input, ArcEngineFrame *frame);
void arc_engine_boot_watch_arm(void);
void arc_engine_boot_watch_cancel(void);
ArcBootWatchState arc_engine_boot_watch_state(void);
#if defined(ARCDROID_TEST_FAULTS)
void arc_engine_test_boot_watch_snapshot(ArcBootWatch *out);
#endif
size_t arc_engine_audio(int16_t *output, size_t capacity_frames);
void arc_engine_audio_stats(uint64_t *underrun_frames, uint64_t *overrun_frames);
bool arc_engine_media_eject(void);
bool arc_engine_media_insert(const ArcEngineMedia *media,
                             char *error, size_t error_size);
bool arc_engine_get_av_info(unsigned *width, unsigned *height, double *refresh_hz);
void arc_engine_cmos(uint8_t output[ARC_CMOS_PAYLOAD_SIZE]);
bool arc_engine_faulted(const char **message);
bool arc_engine_active(void);
void arc_engine_install_sector_read(unsigned drive, unsigned track,
                                    unsigned side, unsigned sector,
                                    unsigned density);
bool arc_engine_install_doorbell_latched(void);

#endif
