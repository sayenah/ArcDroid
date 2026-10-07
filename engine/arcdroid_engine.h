/* ArcDroid engine session API. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#ifndef ARCDROID_ENGINE_H
#define ARCDROID_ENGINE_H

#include "arcdroid_core.h"
#include "jfd.h"
#include "arc_storage.h"
#include "adffs.h"
#include "arc_boot_watch.h"
#include "arc_launch_outcome.h"
#include "arc_error_box.h"
#include "arc_disc_request.h"

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
   bool av, keyboard, mouse, reset;
   bool boot_watch, launch_outcome, error_box, disc_request;
   bool floppy, helper, ide, native_joystick, power_on_input;
   bool cmos_persistence, serialisation;
} ArcEngineCapabilities;

ArcEngineCapabilities arc_engine_capabilities(void);
/* Private backend identity; never selects a shipping launch route. */
bool arc_engine_is_rpc(void);
/* Exact production/drain backends only. Unsupported backends return zero;
 * their existing consuming audio path remains independent. */
uint64_t arc_engine_session_generation(void);
size_t arc_engine_audio_peek(int16_t *output, size_t capacity_frames);
void arc_engine_audio_commit(size_t offered, size_t accepted);
void arc_engine_audio_fault_silence(size_t frames);

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
                      ArcOsClass os,
                      const ArcIdeBackend *ide_backend,
                      const uint8_t cmos[ARC_CMOS_MACHINE_SIZE],
                      const uint8_t locked_raw_mask[32],
                      ArcEngineCmosIgnoredCallback ignored_callback,
                      void *ignored_opaque,
                      char *error, size_t error_size);
void arc_engine_set_log_callback(ArcEngineLogCallback callback, void *opaque);
bool arc_engine_reset(char *error, size_t error_size);
void arc_engine_stop(void);
bool arc_engine_run_frame(ArcInputState *input, ArcEngineFrame *frame);
void arc_engine_power_on_arm(ArcInputState *input, bool armed);
bool arc_engine_power_on_take_release(uint64_t *at_us, bool *boot_action);
void arc_engine_boot_watch_arm(void);
void arc_engine_boot_watch_cancel(void);
ArcBootWatchState arc_engine_boot_watch_state(void);
bool arc_engine_launch_outcome_arm(const char *command);
void arc_engine_launch_outcome_cancel(const char *reason);
void arc_engine_launch_outcome_refused(unsigned drive);
void arc_engine_launch_outcome_snapshot(ArcLaunchOutcome *outcome);
void arc_engine_error_snapshot(ArcErrorBox *box);
bool arc_engine_error_request(ArcErrorAnswer answer);
void arc_engine_error_discard(void);
typedef unsigned (*ArcDiscResolveCallback)(void *opaque, const char *path,
                                            ArcDiscOperation operation);
typedef bool (*ArcDiscObservationCallback)(void *opaque, const char *path,
   unsigned candidate, unsigned observation, bool trigger);
void arc_engine_disc_configure(bool enabled, const char *inserted_name,
   ArcDiscResolveCallback resolve, ArcDiscObservationCallback observe,
   void *opaque);
void arc_engine_disc_clear(void);
void arc_engine_disc_snapshot(ArcDiscRequest *out);
void arc_engine_disc_a_notified(void);
uint64_t arc_engine_disc_time_us(void);
bool arc_engine_disc_controller_idle(void);
void arc_engine_adf_refusal_callback_set(
   void (*callback)(unsigned, unsigned, unsigned, unsigned));
size_t arc_engine_audio(int16_t *output, size_t capacity_frames);
void arc_engine_audio_stats(uint64_t *underrun_frames, uint64_t *overrun_frames);
bool arc_engine_media_eject(void);
bool arc_engine_media_insert(const ArcEngineMedia *media,
                             char *error, size_t error_size);
bool arc_engine_get_av_info(unsigned *width, unsigned *height, double *refresh_hz);
void arc_engine_cmos(uint8_t output[ARC_CMOS_PAYLOAD_SIZE]);
bool arc_engine_faulted(const char **message);
bool arc_engine_active(void);

#endif
