/* ArcDroid private backend operations. GPL-2.0-only. */
#ifndef ARC_ENGINE_BACKEND_H
#define ARC_ENGINE_BACKEND_H

#include "arcdroid_engine.h"

typedef struct {
   ArcEngineCapabilities (*capabilities)(void);
   bool (*start)(const uint8_t *firmware, size_t firmware_size,
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
   void (*set_log_callback)(ArcEngineLogCallback callback, void *opaque);
   bool (*reset)(char *error, size_t error_size);
   void (*stop)(void);
   bool (*run_frame)(ArcInputState *input, ArcEngineFrame *frame);
   void (*power_on_arm)(ArcInputState *input, bool armed);
   bool (*power_on_take_release)(uint64_t *at_us, bool *boot_action);
   void (*boot_watch_arm)(void);
   void (*boot_watch_cancel)(void);
   ArcBootWatchState (*boot_watch_state)(void);
   bool (*launch_outcome_arm)(const char *command);
   void (*launch_outcome_cancel)(const char *reason);
   void (*launch_outcome_refused)(unsigned drive);
   void (*launch_outcome_snapshot)(ArcLaunchOutcome *outcome);
   void (*error_snapshot)(ArcErrorBox *box);
   bool (*error_request)(ArcErrorAnswer answer);
   void (*error_discard)(void);
   void (*disc_configure)(bool enabled, const char *inserted_name,
   ArcDiscResolveCallback resolve, ArcDiscObservationCallback observe,
   void *opaque);
   void (*disc_clear)(void);
   void (*disc_snapshot)(ArcDiscRequest *out);
   void (*disc_a_notified)(void);
   uint64_t (*disc_time_us)(void);
   bool (*disc_controller_idle)(void);
   void (*adf_refusal_callback_set)(
   void (*callback)(unsigned, unsigned, unsigned, unsigned));
   size_t (*audio)(int16_t *output, size_t capacity_frames);
   void (*audio_stats)(uint64_t *underrun_frames, uint64_t *overrun_frames);
   bool (*media_eject)(void);
   bool (*media_insert)(const ArcEngineMedia *media,
                             char *error, size_t error_size);
   bool (*get_av_info)(unsigned *width, unsigned *height, double *refresh_hz);
   void (*cmos)(uint8_t output[ARC_CMOS_PAYLOAD_SIZE]);
   bool (*faulted)(const char **message);
   bool (*active)(void);
} ArcEngineBackendOps;

extern const ArcEngineBackendOps arc_arculator_backend;

#endif
