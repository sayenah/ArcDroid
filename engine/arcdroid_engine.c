/* ArcDroid session dispatcher. GPL-2.0-only. */
#include "engine_backend.h"
#if defined(ARCDROID_TEST_FAULTS)
#include "rpc/rpc_session.h"
extern const ArcEngineBackendOps arc_rpc_backend;
RETRO_API bool arc_rpc_test_start(ArcRpcModel model, const uint8_t *rom, size_t size,
    const uint8_t seed[256], uint64_t epoch, char *error, size_t error_size);
#endif

/* Shipping launches retain the existing Arculator path. The operations
 * pointer is also valid before construction and after teardown, keeping
 * observer snapshots and failed-start cleanup identical to the baseline. */
static const ArcEngineBackendOps *arc_backend = &arc_arculator_backend;

ArcEngineCapabilities arc_engine_capabilities(void)
{
   return arc_backend->capabilities();
}

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
                      char *error, size_t error_size)
{
   if (arc_backend != &arc_arculator_backend) {
      arc_backend->stop();
      arc_backend = &arc_arculator_backend;
   }
   return arc_backend->start(firmware, firmware_size, media, helper, machine, os,
      ide_backend, cmos, locked_raw_mask, ignored_callback, ignored_opaque,
      error, error_size);
}

void arc_engine_set_log_callback(ArcEngineLogCallback callback, void *opaque)
{
   arc_backend->set_log_callback(callback, opaque);
}

bool arc_engine_reset(char *error, size_t error_size)
{
   return arc_backend->reset(error, error_size);
}

void arc_engine_stop(void)
{
   arc_backend->stop();
}

bool arc_engine_run_frame(ArcInputState *input, ArcEngineFrame *frame)
{
   return arc_backend->run_frame(input, frame);
}

void arc_engine_power_on_arm(ArcInputState *input, bool armed)
{
   arc_backend->power_on_arm(input, armed);
}

bool arc_engine_power_on_take_release(uint64_t *at_us, bool *boot_action)
{
   return arc_backend->power_on_take_release(at_us, boot_action);
}

void arc_engine_boot_watch_arm(void)
{
   arc_backend->boot_watch_arm();
}

void arc_engine_boot_watch_cancel(void)
{
   arc_backend->boot_watch_cancel();
}

ArcBootWatchState arc_engine_boot_watch_state(void)
{
   return arc_backend->boot_watch_state();
}

bool arc_engine_launch_outcome_arm(const char *command)
{
   return arc_backend->launch_outcome_arm(command);
}

void arc_engine_launch_outcome_cancel(const char *reason)
{
   arc_backend->launch_outcome_cancel(reason);
}

void arc_engine_launch_outcome_refused(unsigned drive)
{
   arc_backend->launch_outcome_refused(drive);
}

void arc_engine_launch_outcome_snapshot(ArcLaunchOutcome *outcome)
{
   arc_backend->launch_outcome_snapshot(outcome);
}

void arc_engine_error_snapshot(ArcErrorBox *box)
{
   arc_backend->error_snapshot(box);
}

bool arc_engine_error_request(ArcErrorAnswer answer)
{
   return arc_backend->error_request(answer);
}

void arc_engine_error_discard(void)
{
   arc_backend->error_discard();
}

void arc_engine_disc_configure(bool enabled, const char *inserted_name,
   ArcDiscResolveCallback resolve, ArcDiscObservationCallback observe,
   void *opaque)
{
   arc_backend->disc_configure(enabled, inserted_name, resolve, observe, opaque);
}

void arc_engine_disc_clear(void)
{
   arc_backend->disc_clear();
}

void arc_engine_disc_snapshot(ArcDiscRequest *out)
{
   arc_backend->disc_snapshot(out);
}

void arc_engine_disc_a_notified(void)
{
   arc_backend->disc_a_notified();
}

uint64_t arc_engine_disc_time_us(void)
{
   return arc_backend->disc_time_us();
}

bool arc_engine_disc_controller_idle(void)
{
   return arc_backend->disc_controller_idle();
}

void arc_engine_adf_refusal_callback_set(
   void (*callback)(unsigned, unsigned, unsigned, unsigned))
{
   arc_backend->adf_refusal_callback_set(callback);
}

size_t arc_engine_audio(int16_t *output, size_t capacity_frames)
{
   return arc_backend->audio(output, capacity_frames);
}

void arc_engine_audio_stats(uint64_t *underrun_frames, uint64_t *overrun_frames)
{
   arc_backend->audio_stats(underrun_frames, overrun_frames);
}

bool arc_engine_media_eject(void)
{
   return arc_backend->media_eject();
}

bool arc_engine_media_insert(const ArcEngineMedia *media,
                             char *error, size_t error_size)
{
   return arc_backend->media_insert(media, error, error_size);
}

bool arc_engine_get_av_info(unsigned *width, unsigned *height, double *refresh_hz)
{
   return arc_backend->get_av_info(width, height, refresh_hz);
}

void arc_engine_cmos(uint8_t output[ARC_CMOS_PAYLOAD_SIZE])
{
   arc_backend->cmos(output);
}

bool arc_engine_faulted(const char **message)
{
   return arc_backend->faulted(message);
}

bool arc_engine_active(void)
{
   return arc_backend->active();
}

#if defined(ARCDROID_TEST_FAULTS)
RETRO_API bool arc_rpc_test_start(ArcRpcModel model, const uint8_t *rom, size_t size,
    const uint8_t seed[256], uint64_t epoch, char *error, size_t error_size)
{
    arc_backend->stop();
    arc_backend = &arc_rpc_backend; /* Partial start ownership is explicit. */
    return arc_rpc_session_start(model, rom, size, seed, epoch, error, error_size);
}
#endif

#if defined(ARCDROID_TEST_FAULTS)
RETRO_API const ArcEngineBackendOps *arc_rpc_test_engine_api(void)
{
   static const ArcEngineBackendOps api = {
   .capabilities = arc_engine_capabilities,
   .start = arc_engine_start,
   .set_log_callback = arc_engine_set_log_callback,
   .reset = arc_engine_reset,
   .stop = arc_engine_stop,
   .run_frame = arc_engine_run_frame,
   .power_on_arm = arc_engine_power_on_arm,
   .power_on_take_release = arc_engine_power_on_take_release,
   .boot_watch_arm = arc_engine_boot_watch_arm,
   .boot_watch_cancel = arc_engine_boot_watch_cancel,
   .boot_watch_state = arc_engine_boot_watch_state,
   .launch_outcome_arm = arc_engine_launch_outcome_arm,
   .launch_outcome_cancel = arc_engine_launch_outcome_cancel,
   .launch_outcome_refused = arc_engine_launch_outcome_refused,
   .launch_outcome_snapshot = arc_engine_launch_outcome_snapshot,
   .error_snapshot = arc_engine_error_snapshot,
   .error_request = arc_engine_error_request,
   .error_discard = arc_engine_error_discard,
   .disc_configure = arc_engine_disc_configure,
   .disc_clear = arc_engine_disc_clear,
   .disc_snapshot = arc_engine_disc_snapshot,
   .disc_a_notified = arc_engine_disc_a_notified,
   .disc_time_us = arc_engine_disc_time_us,
   .disc_controller_idle = arc_engine_disc_controller_idle,
   .adf_refusal_callback_set = arc_engine_adf_refusal_callback_set,
   .audio = arc_engine_audio,
   .audio_stats = arc_engine_audio_stats,
   .media_eject = arc_engine_media_eject,
   .media_insert = arc_engine_media_insert,
   .get_av_info = arc_engine_get_av_info,
   .cmos = arc_engine_cmos,
   .faulted = arc_engine_faulted,
   .active = arc_engine_active,
};
   return &api;
}
#endif
