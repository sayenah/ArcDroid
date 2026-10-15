/* ArcDroid libretro adapter. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "libretro.h"
#include "arc_io.h"
#include "rpc_inventory.h"
#include "install_store.h"
#include "arcdroid_core.h"
#include "arcdroid_engine.h"
#include "arcdroid_platform.h"
#include "adffs.h"
#include "folder_set.h"
#include "jfd.h"
#include "jfd_store.h"
#include "playlist.h"
#include "zip_content.h"
#include "boot_observation.h"
#include "hard_disc_scope.h"
#if defined(ARCDROID_TEST_FAULTS)
#include "arc_test_plan.h"
#include "engine_test.h"
#endif

#include <limits.h>
#include <ctype.h>
#include <stdbool.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ARCDROID_VERSION
#define ARCDROID_VERSION "0.1.0"
#endif

#define ARC_WIDTH 640u
#define ARC_HEIGHT 480u
#define ARC_PRESENT_MAX_WIDTH 4096u
#define ARC_PRESENT_MAX_HEIGHT 2048u
#define ARC_RATE 50.0
#define ARC_DEFAULT_POINTER_DEADZONE 15u
#define ARC_AUDIO_RATE 48000.0
#define ARC_FIRMWARE_SIZE_MARGIN 4096u
#define ARC_VIDC_WARNING_THRESHOLD_US UINT64_C(2000000)
#define ARC_DEVICE_KEYS RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1)
#define ARC_DEVICE_MOUSE RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1)

enum {
   ARC_INPUT_CURATED_KEY_ROWS = 97u,
   ARC_INPUT_SHARED_KEY_ALIASES = 0u,
   ARC_INPUT_OSK_ONLY_KEYS = 3u,
   ARC_INPUT_POINTER_BUTTONS = 3u,
   /* Leave room for future adapter bindings without risking truncation when
    * overflow reconciliation rebuilds every held discrete input level. */
   ARC_INPUT_RECONCILE_MARGIN = 16u
};

_Static_assert(ARC_INPUT_CURATED_KEY_ROWS - ARC_INPUT_SHARED_KEY_ALIASES +
               ARC_INPUT_OSK_ONLY_KEYS +
               ARC_INPUT_POINTER_BUTTONS + ARC_INPUT_RECONCILE_MARGIN <=
               ARC_INPUT_FIFO_CAPACITY,
               "supported input surface exceeds reconciliation capacity");

static retro_environment_t arc_environment;
static retro_video_refresh_t arc_video;
static retro_audio_sample_t arc_audio;
static retro_audio_sample_batch_t arc_audio_batch;
static retro_input_poll_t arc_input_poll;
static retro_input_state_t arc_input_state;
static struct retro_vfs_interface *arc_vfs;
static retro_log_printf_t arc_log;
static uint32_t arc_frame[ARC_WIDTH * ARC_HEIGHT];
static size_t arc_video_pitch;
/* Frontends may reread the most recently presented frame during an A/V
 * reinitialisation. Engine and OSK surfaces can be freed on reset or machine
 * recreation, so video callbacks always receive this core-lifetime surface. */
static uint32_t arc_present_frame[ARC_PRESENT_MAX_WIDTH * ARC_PRESENT_MAX_HEIGHT];
static int16_t arc_audio_frame[2048u * 2u];
static uint8_t arc_cmos_record[256];
static uint8_t arc_cmos_last_written[256];
static uint8_t arc_cmos_prepared[256];
static uint8_t *arc_firmware;
static size_t arc_firmware_size;
static ArcDigest arc_content_digest;
static ArcLaunchPlan arc_plan;
#if defined(ARCDROID_TEST_FAULTS)
/* The authored test ROM can opt into the RISC OS 3 CMOS and Shift path.
 * No corresponding branch exists in the shipping library. */
static ArcOsClass arc_cmos_policy_os(void)
{
   const char *opt_in = getenv("ARCDROID_TEST_POWER_ON_SHIFT");
   return arc_plan.os == ARC_OS_TEST && opt_in && !strcmp(opt_in, "1") ?
      ARC_OS_RISCOS_311 : arc_plan.os;
}
#else
static ArcOsClass arc_cmos_policy_os(void) { return arc_plan.os; }
#endif

static bool arc_cmos_policy_riscos3(void)
{
   ArcOsClass os = arc_cmos_policy_os();
   return os == ARC_OS_RISCOS_300 || os == ARC_OS_RISCOS_310 ||
      os == ARC_OS_RISCOS_311 || os == ARC_OS_RISCOS_319;
}
static ArcInputState arc_inputs;
static bool arc_pad_keys[2][10];
static bool arc_pad_neutral_gate[2];
static ArcInputResolution arc_input_resolution;
static unsigned arc_pad_pointer_buttons;
static bool arc_keyboard_menu;
static bool arc_keyboard_lmeta;
static bool arc_keyboard_polling;
static unsigned arc_port_device[2] = {ARC_DEVICE_KEYS, ARC_DEVICE_MOUSE};
static char arc_cmos_path[4096];
static char arc_save_path[4096];
static char arc_helper_path[4096];
static uint64_t arc_frame_number;
static long double arc_audio_fraction;
static unsigned arc_video_width = ARC_WIDTH;
static unsigned arc_video_height = ARC_HEIGHT;
static double arc_video_rate = ARC_RATE;
static bool arc_fault_reported;
static bool arc_vidc_warning_reported;
static bool arc_vidc_transient_logged;
static uint64_t arc_vidc_incomplete_us;
static bool arc_audio_warning_reported;
static bool arc_loaded;
static bool arc_rpc_av_attempted;
static double arc_rpc_fault_audio_fraction;
static bool arc_can_dupe;
static bool arc_cmos_last_written_valid;
static bool arc_cmos_ignored_logged;
static unsigned arc_cmos_ignored_attempts;
static unsigned arc_cmos_ignored_changes;
static bool arc_ejected;
static unsigned arc_image_index;
static uint64_t arc_guest_time_us;
static bool arc_power_on_shift;
static int16_t arc_stick_x;
static int16_t arc_stick_y;
static bool arc_stick_precision;
static long double arc_stick_fraction_x;
static long double arc_stick_fraction_y;
static ArcOsk arc_osk;
static uint32_t *arc_osk_surface;
static size_t arc_osk_surface_pixels;
static bool arc_osk_present_dirty;
static bool arc_osk_toggle_level;
static bool arc_osk_pad_levels[16];
static bool arc_osk_too_narrow_reported;
static bool arc_osk_failure_reported;
static bool arc_error_pad_gate;
static bool arc_error_was_ready;
static uint64_t arc_error_seen_generation;
static bool arc_error_pad_levels[2];
static uint64_t arc_error_hint_generation;
static unsigned arc_error_hint_state;
typedef enum {
   ARC_SESSION_EMPTY = 0,
   ARC_SESSION_PREPARED,
   ARC_SESSION_CHOOSING,
   ARC_SESSION_RUNNING,
   ARC_SESSION_FAILED
} ArcSessionState;
static ArcSessionState arc_session;
static ArcLaunchDiscovery arc_boot_discovery;
static bool arc_boot_eligible;
static bool arc_boot_attempt;
static bool arc_boot_post_chooser;
static bool arc_boot_input;
static bool arc_boot_recovery_phase;
static const char *arc_boot_recovery_action;
static const char *arc_boot_watch_override;
static ArcBootWatchState arc_boot_last_state;
static ArcBootObservation arc_boot_start_stamp;
static bool arc_boot_stamp_ready;
static bool arc_boot_record_hit;
static bool arc_boot_record_suppressed;
static ArcLaunchDiscovery arc_chooser_discovery;
static ArcLaunchChooser arc_chooser;
static char arc_choice_path[4096];
static bool arc_chooser_pad_levels[4];
static bool arc_chooser_keyboard_levels[3];
static bool arc_keyboard_raw[ARC_INPUT_KEY_COUNT];
static bool arc_chooser_pointer_down;
static int arc_chooser_pointer_row = -1;
static bool arc_chooser_input_armed;
static bool arc_chooser_release_gate;
static bool arc_av_transition_pending;
static bool arc_adffs_active;
static uint8_t *arc_adffs_carrier;
static uint8_t *arc_adffs_support;
static ArcInstallStore *arc_hd_store;
static bool arc_hd_option_auto;
static bool arc_hd_failure_noticed;
static bool arc_hd_readonly_noticed;
static bool arc_hd_write_failure_noticed;
static char arc_hd_reason[96] = "not-attachable";
static bool arc_hd_damaged;
static ArcHdScope arc_hd_scope;
static ArcHdHelperMode arc_hd_mode;
static ArcHdHelperMode arc_hd_requested_mode;
static bool arc_hd_observe_target;
static bool arc_hd_observe_copy;
static bool arc_hd_install_used;
static bool arc_hd_product_used;
static bool arc_hd_notice_shown;
static bool arc_hd_reset_cancel_pending;
static ArcOutcomeState arc_hd_last_outcome_state;
static char arc_hd_source[160];
static char arc_hd_target[11];
static char arc_hd_product[11];
static char arc_hd_media[41];
static char arc_hd_fingerprint[41];
static unsigned arc_hd_selected;
static bool arc_hd_scope_cached;
static bool arc_hd_scope_reported;
#if defined(ARCDROID_TEST_FAULTS)
static bool arc_test_adffs_accept_test_os = true;
static bool arc_test_fail_adffs_content_read;
static unsigned arc_test_mouse_input_samples;
static bool arc_test_fail_next_construct;
static bool arc_test_jfd_wp_layout_supported;
void arc_libretro_test_jfd_wp_layout_supported(bool enabled)
{ arc_test_jfd_wp_layout_supported = enabled; }
void arc_libretro_test_fail_next_construct(void)
{ arc_test_fail_next_construct = true; }
unsigned arc_libretro_test_mouse_input_samples(void)
{
   return arc_test_mouse_input_samples;
}
void arc_libretro_test_adffs_accept_test_os(bool accept)
{
   arc_test_adffs_accept_test_os = accept;
}
void arc_libretro_test_fail_adffs_content_read(bool fail)
{
   arc_test_fail_adffs_content_read = fail;
}
bool arc_libretro_test_adffs_content_read_failure_pending(void)
{
   return arc_test_fail_adffs_content_read;
}
bool arc_libretro_test_adffs_released(void)
{
   return !arc_adffs_carrier && !arc_adffs_support && !arc_adffs_active;
}
bool arc_libretro_test_adffs_carrier_matches(const void *data, size_t size)
{
   uint8_t *expected = malloc(ARC_ADFFS_DISC_SIZE);
   char error[128] = {0};
   bool matches = expected && arc_adffs_carrier &&
      arc_adffs_carrier_build(data, size, expected, error, sizeof(error)) &&
      !memcmp(expected, arc_adffs_carrier, ARC_ADFFS_DISC_SIZE);
   free(expected);
   return matches;
}
#endif
static char arc_system_path[4096];
static void arc_adffs_discard(void);
static void arc_zip_adffs_raw_discard(void);
#if defined(ARCDROID_TEST_FAULTS)
static unsigned arc_osk_fail_allocations;
static bool arc_osk_source_unchanged;
static bool arc_osk_source_checked;
static bool arc_test_stall_kart;
static unsigned arc_test_presented_osk_notice;
void arc_libretro_test_fail_osk_allocations(unsigned count)
{
   arc_osk_fail_allocations = count;
}
void arc_libretro_test_drop_osk_surface(void)
{
   free(arc_osk_surface);
   arc_osk_surface = NULL;
   arc_osk_surface_pixels = 0u;
}
bool arc_libretro_test_osk_source_unchanged(void)
{
   return arc_osk_source_checked && arc_osk_source_unchanged;
}
void arc_libretro_test_stall_kart(bool stalled)
{
   arc_test_stall_kart = stalled;
}
void arc_libretro_test_fill_input_queue(void)
{
   while (arc_input_available(&arc_inputs) >= 2u)
   {
      arc_input_source_key(&arc_inputs, ARC_INPUT_SOURCE_KEYBOARD,
                           RETROK_b, true, true);
      arc_input_source_key(&arc_inputs, ARC_INPUT_SOURCE_KEYBOARD,
                           RETROK_b, false, true);
   }
}
unsigned arc_libretro_test_input_pending(void)
{
   return arc_input_pending(&arc_inputs);
}
bool arc_libretro_test_osk_visible(void) { return arc_osk.visible; }
bool arc_libretro_test_pad_source_level(unsigned port, unsigned key)
{
   return arc_input_source_level(&arc_inputs,
      port ? ARC_INPUT_SOURCE_PAD1 : ARC_INPUT_SOURCE_PAD0, key);
}
bool arc_libretro_test_osk_busy(void) { return arc_osk.busy; }
bool arc_libretro_test_osk_activation_active(void)
{
   return arc_osk.activation_phase != 0u;
}
void arc_libretro_test_force_present(void) { arc_osk_present_dirty = true; }
unsigned arc_libretro_test_osk_modifier_state(unsigned key)
{
   static const unsigned modifier_keys[ARC_OSK_MODIFIER_COUNT] = {
      RETROK_LSHIFT, RETROK_RSHIFT, RETROK_LCTRL,
      RETROK_RCTRL, RETROK_LALT, RETROK_RALT
   };
   for (unsigned i = 0u; i < ARC_OSK_MODIFIER_COUNT; ++i)
      if (modifier_keys[i] == key) return (unsigned)arc_osk.modifiers[i];
   return UINT_MAX;
}
unsigned arc_libretro_test_presented_osk_notice(void)
{
   return arc_test_presented_osk_notice;
}
const char *arc_libretro_test_content_id(void)
{
   return arc_content_digest.sha1;
}
unsigned arc_libretro_test_session_state(void) { return (unsigned)arc_session; }
unsigned arc_libretro_test_chooser_focus(void) { return arc_chooser.focus; }
unsigned arc_libretro_test_chooser_rows(void) { return arc_chooser.row_count; }
void arc_libretro_test_force_input_reconcile(void)
{
   uint64_t generation = arc_input_reconcile_generation(&arc_inputs);
   bool down = false;
   while (arc_input_reconcile_generation(&arc_inputs) == generation)
   {
      down = !down;
      arc_input_source_key(&arc_inputs, ARC_INPUT_SOURCE_KEYBOARD,
                           RETROK_BREAK, down, true);
   }
   arc_input_source_key(&arc_inputs, ARC_INPUT_SOURCE_KEYBOARD,
                        RETROK_BREAK, false, true);
}
#endif

typedef struct {
   bool populated;
   bool high_density;
   bool created_this_preparation;
   char source[ARC_MEDIA_PATH_CAPACITY];
   char staged[ARC_MEDIA_PATH_CAPACITY];
   char label[ARC_MEDIA_LABEL_CAPACITY];
   ArcMediaInfo info;
   size_t effective_extent;
   bool zero_tail_presentation;
   ArcDigest digest;
   ArcJfdImage jfd;
   bool jfd_damage_notice_shown;
   bool jfd_off_notice_shown;
   bool jfd_native_notice_shown;
   bool jfd_map_unavailable_shown;
   uint8_t jfd_refusal_seen[ARC_JFD_MAX_TRACK_SIDES][32];
   ArcRpcCompatibility compatibility; /* Validated masks survive decoded-buffer release. */
   uint32_t jfd_game_id;
   uint16_t jfd_disc_number;
   uint16_t jfd_disc_count;
   char zip_member[256];
   uint32_t zip_crc32;
   uint32_t zip_packed_size;
   uint32_t zip_size;
} ArcMediaEntry;

static ArcMediaEntry arc_media[ARC_PLAYLIST_MAX_ENTRIES];
#if defined(ARCDROID_TEST_PLANS)
static ArcMediaEntry arc_rpc_helper_entry;
#endif
static unsigned arc_media_count;
#if defined(ARCDROID_TEST_FAULTS)
static const ArcRpcFirmwareFingerprint *arc_test_rpc_catalogue;
static size_t arc_test_rpc_catalogue_count;
RETRO_API void arc_libretro_test_rpc_catalogue(const ArcRpcFirmwareFingerprint *table, size_t count)
{ arc_test_rpc_catalogue=table; arc_test_rpc_catalogue_count=count; }
RETRO_API bool arc_libretro_test_rpc_media(unsigned i, ArcRpcCompatibility *out)
{
   if (i>=arc_media_count || !out) return false;
   *out=arc_media[i].compatibility;
   return arc_media[i].jfd.decoded == NULL;
}
#endif

typedef struct {
   bool valid;
   uint64_t last_used;
   char path[256];
   bool image_valid[ARC_PLAYLIST_MAX_ENTRIES];
   ArcDiscObject objects[ARC_PLAYLIST_MAX_ENTRIES];
} ArcDiscCacheEntry;
#define ARC_DISC_CACHE_CAPACITY 64u
static ArcDiscCacheEntry arc_disc_paths[ARC_DISC_CACHE_CAPACITY];
static ArcGuestName arc_disc_names[ARC_PLAYLIST_MAX_ENTRIES];
static bool arc_disc_names_valid[ARC_PLAYLIST_MAX_ENTRIES];
static uint64_t arc_disc_generation = 1u;
static uint64_t arc_disc_cache_clock;
static uint64_t arc_disc_watch_generation;
#if defined(ARCDROID_TEST_FAULTS)
static unsigned arc_disc_temporary_jfd_live, arc_disc_temporary_jfd_peak;
static unsigned arc_disc_temporary_jfd_decodes;
#endif
static bool arc_disc_b_log_active;
static uint64_t arc_disc_b_logged_us;
static unsigned arc_disc_b_suppressed;
typedef struct {
   bool valid;
   char object[256];
   uint32_t continuation, sp;
   uint64_t logged_us, last_used;
   unsigned suppressed;
} ArcDiscFirstLog;
static ArcDiscFirstLog arc_disc_first_logs[ARC_DISC_IDENTITY_CAPACITY];
static uint64_t arc_disc_first_log_clock;
typedef enum { ARC_DISC_MODE_AUTO, ARC_DISC_MODE_HINT, ARC_DISC_MODE_OFF } ArcDiscMode;
static ArcDiscMode arc_disc_mode = ARC_DISC_MODE_AUTO;
static bool arc_disc_owned;
static unsigned arc_disc_launch_index, arc_disc_previous_index;
static uint64_t arc_disc_swapped_us;
static char arc_disc_notices[8][64];
static unsigned arc_disc_notice_read, arc_disc_notice_write;
static bool arc_disc_back_request(unsigned candidate,uint64_t now)
{
   return arc_disc_owned && candidate==arc_disc_previous_index && now>=arc_disc_swapped_us &&
      now-arc_disc_swapped_us<UINT64_C(10000000);
}
#if defined(ARCDROID_TEST_FAULTS)
RETRO_API bool arc_libretro_test_disc_back_request(unsigned candidate,unsigned *previous,uint64_t *time,unsigned *notices)
{
   *previous=arc_disc_previous_index;*time=arc_disc_swapped_us;
   *notices=(arc_disc_notice_write+8u-arc_disc_notice_read)%8u;
   return arc_disc_back_request(candidate,arc_engine_disc_time_us());
}
#endif
static void arc_disc_invalidate(void)
{
   ++arc_disc_generation;
   memset(arc_disc_paths, 0, sizeof(arc_disc_paths));
   memset(arc_disc_names_valid, 0, sizeof(arc_disc_names_valid));
   arc_disc_cache_clock = 0u;
   arc_disc_b_log_active = false;
   arc_disc_b_suppressed = 0u;
   memset(arc_disc_first_logs, 0, sizeof(arc_disc_first_logs));
   arc_disc_first_log_clock = 0u;
}
static void arc_disc_invalidate_image(unsigned image)
{
   if (image >= ARC_PLAYLIST_MAX_ENTRIES) return;
   ++arc_disc_generation;
   for (unsigned i = 0u; i < ARC_DISC_CACHE_CAPACITY; ++i)
      arc_disc_paths[i].image_valid[image] = false;
   arc_disc_names_valid[image] = false;
}
static void arc_disc_write_observer(const char *path)
{
   if (!path) return;
   for (unsigned i = 0u; i < arc_media_count; ++i)
   {
      ArcMediaEntry *entry = &arc_media[i];
      if (!entry->populated) continue;
      if (!strcmp(path, entry->staged)) arc_disc_invalidate_image(i);
   }
}
static bool arc_original_jfd_notice_shown;
static bool arc_jfd_tab_pending_notice_shown;
static bool arc_jfd_tab_hidden_notice_shown;
static bool arc_jfd_tab_adffs_notice_shown;
static bool arc_jfd_failure_status_shown;
#if defined(ARCDROID_TEST_FAULTS)
bool arc_libretro_test_jfd_commit(unsigned image_index, size_t descriptor,
                                  uint8_t value)
{
   if (image_index >= arc_media_count ||
       arc_media[image_index].info.kind != ARC_MEDIA_JFD) return false;
   ArcJfdImage *image = &arc_media[image_index].jfd;
   if (!image->overlay || !image->overlay->writable ||
       descriptor >= image->sector_count ||
       !image->overlay->eligible || !image->overlay->eligible[descriptor])
      return false;
   size_t size = image->sectors[descriptor].size;
   uint8_t *bytes = malloc(size);
   if (!bytes) return false;
   memset(bytes, value, size);
   bool committed = image->overlay->commit(image, descriptor, bytes);
   free(bytes);
   return committed;
}
/* Exact map I/O for authored lifecycle and snapshot-interruption tests. */
RETRO_API bool arc_libretro_test_jfd_map(unsigned image_index, uint8_t bytes[1024],
                               bool write, uint64_t *generation)
{
   if (image_index >= arc_media_count ||
       arc_media[image_index].info.kind != ARC_MEDIA_JFD) return false;
   ArcJfdImage *image = &arc_media[image_index].jfd;
   if (!image->overlay || !image->track_count) return false;
   const ArcJfdTrack *track = &image->tracks[0];
   for (unsigned i = 0u; i < track->sector_count; ++i)
   {
      size_t index = track->first_sector + i;
      if (image->sectors[index].id != 0u ||
          image->sectors[index].size != 1024u) continue;
      bool okay;
      if (write) okay = image->overlay->commit(image, index, bytes);
      else
      {
         ArcJfdView view;
         okay = arc_jfd_effective_sector(image, 0u, i, false, &view) && view.data;
         if (okay) memcpy(bytes, view.data, 1024u);
      }
      *generation = image->overlay->generation;
      return okay;
   }
   return false;
}
bool arc_libretro_test_jfd_formatted_reads(unsigned image_index)
{
   if (image_index >= arc_media_count ||
       arc_media[image_index].info.kind != ARC_MEDIA_JFD) return false;
   const ArcJfdOverlay *overlay = arc_media[image_index].jfd.overlay;
   if (!overlay || !overlay->map_enabled || !overlay->map_valid) return false;
   for (unsigned t = 0u; t < ARC_JFD_PDD5_TRACKS; ++t)
      if (overlay->allocation_mask[t] & overlay->creation_mask[t] &
          (uint8_t)~overlay->probe_mask[t]) return true;
   return false;
}
uint8_t arc_libretro_test_jfd_probe_mask(unsigned image_index,
                                        unsigned track_side)
{
   if (image_index >= arc_media_count || track_side >= ARC_JFD_PDD5_TRACKS ||
       arc_media[image_index].info.kind != ARC_MEDIA_JFD ||
       !arc_media[image_index].jfd.overlay) return 0u;
   return arc_media[image_index].jfd.overlay->probe_mask[track_side];
}
bool arc_libretro_test_jfd_created_byte(unsigned image_index,
                                        unsigned track_side, unsigned id,
                                        unsigned offset, uint8_t *value)
{
   if (!value || image_index >= arc_media_count ||
       arc_media[image_index].info.kind != ARC_MEDIA_JFD ||
       id >= ARC_JFD_PDD5_IDS || offset >= 1024u ||
       track_side >= arc_media[image_index].jfd.track_count)
      return false;
   ArcJfdImage *image = &arc_media[image_index].jfd;
   ArcJfdView view;
   if (!arc_jfd_effective_sector(image, track_side,
                                 image->tracks[track_side].sector_count + id,
                                 false, &view) || !view.created || !view.data)
      return false;
   *value = view.data[offset];
   return true;
}
bool arc_libretro_test_jfd_presented(unsigned image_index, unsigned id,
                                     uint32_t *position)
{
   if (image_index >= arc_media_count ||
       arc_media[image_index].info.kind != ARC_MEDIA_JFD ||
       id >= ARC_JFD_MAX_STANDARD_IDS) return false;
   const ArcJfdImage *image = &arc_media[image_index].jfd;
   if (!(image->identification_mask & (1u << id))) return false;
   if (position) *position = image->identification_time[id];
   return true;
}
uint8_t arc_libretro_test_jfd_slot_state(unsigned image_index,
                                        unsigned track_side, unsigned id,
                                        uint32_t *position,
                                        uint32_t *creation_time)
{
   if (image_index >= arc_media_count ||
       arc_media[image_index].info.kind != ARC_MEDIA_JFD ||
       id >= ARC_JFD_PDD5_IDS) return 0u;
   const ArcJfdImage *image = &arc_media[image_index].jfd;
   if (track_side >= image->track_count) return 0u;
   ArcJfdView view;
   if (!arc_jfd_effective_sector(image, track_side,
                                 image->tracks[track_side].sector_count + id,
                                 false, &view)) return 0u;
   if (position) *position = view.sector.time_us;
   if (creation_time) *creation_time = image->overlay &&
      track_side < ARC_JFD_PDD5_TRACKS ?
      image->overlay->creation_time[track_side][id] : 0u;
   return (uint8_t)((view.id_visible ? 1u : 0u) |
                    (view.data ? 2u : 0u) |
                    (view.write_eligible ? 4u : 0u) |
                    (image->overlay && track_side < ARC_JFD_PDD5_TRACKS &&
                     image->overlay->created[track_side][id] ? 8u : 0u));
}
const char *arc_libretro_test_staged_path(unsigned index)
{
   return index < arc_media_count && arc_media[index].populated ?
      arc_media[index].staged : NULL;
}
const char *arc_libretro_test_cmos_path(void) { return arc_cmos_path; }
uint8_t arc_libretro_test_prepared_cmos_byte(unsigned offset)
{ return offset < sizeof(arc_cmos_prepared) ? arc_cmos_prepared[offset] : 0u; }
const char *arc_libretro_test_choice_path(void) { return arc_choice_path; }
#endif
static bool arc_folder_set_active;
static bool arc_zip_active;
static char arc_zip_path[ARC_MEDIA_PATH_CAPACITY];
static uint8_t *arc_zip_adffs_raw;
static size_t arc_zip_adffs_raw_size;
static char arc_zip_adffs_member[256];

typedef enum {
   ARC_POINTER_STICK_RIGHT = 0,
   ARC_POINTER_STICK_LEFT,
   ARC_POINTER_STICK_DISABLED
} ArcPointerStick;

typedef enum {
   ARC_LAUNCH_AUTO = 0,
   ARC_LAUNCH_OFF,
   ARC_LAUNCH_APP1,
   ARC_LAUNCH_APP2,
   ARC_LAUNCH_APP3,
   ARC_LAUNCH_APP4,
   ARC_LAUNCH_APP5,
   ARC_LAUNCH_APP6,
   ARC_LAUNCH_APP7,
   ARC_LAUNCH_APP8
} ArcLaunchOption;

typedef enum {
   ARC_LAUNCH_CHOOSER_REMEMBER = 0,
   ARC_LAUNCH_CHOOSER_ASK
} ArcLaunchChooserOption;

typedef enum { ARC_JFD_BOOT_AUTO = 0, ARC_JFD_BOOT_NATIVE,
               ARC_JFD_BOOT_ADFFS } ArcJfdBootOption;
typedef enum { ARC_ADFFS_VERIFIED_ONLY = 0, ARC_ADFFS_ALLOW_UNTESTED } ArcAdffsOption;

typedef enum {
   ARC_OSK_TOGGLE_L3 = 0,
   ARC_OSK_TOGGLE_R3,
   ARC_OSK_TOGGLE_OFF
} ArcOskToggle;

typedef struct {
   bool valid;
   int x;
   int y;
   int start_x;
   int start_y;
   uint64_t start_us;
} ArcTouchContact;

typedef struct {
   ArcTouchContact contact[2];
   unsigned count;
   bool two_finger;
   bool one_lifted;
   bool candidate;
   bool blocked_until_zero;
   long double fraction_x;
   long double fraction_y;
} ArcTouchState;

static ArcTouchState arc_touch;

typedef struct {
   ArcOsClass requested_os;
   ArcMachine machine;
   ArcMachine requested_machine;
   bool machine_auto;
   ArcCpu cpu;
   bool cpu_auto;
   bool request_a3010_joystick;
   bool pointer_relative;
   ArcPointerStick pointer_stick;
   unsigned pointer_speed;
   unsigned pointer_deadzone;
   ArcDiskWriteMode disk_writes;
   bool short_tail_zero;
   bool jfd_omitted_strict;
   bool hard_disc_auto;
   ArcLaunchOption launch;
   ArcLaunchChooserOption launch_chooser;
   bool launch_ramdisc;
   ArcJfdBootOption jfd_boot;
   ArcJfdWritePolicy jfd_write_policy;
   ArcAdffsOption adffs_package;
   ArcInputOverride pad[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT];
   ArcOskToggle osk_toggle;
   bool left_stick_dpad[ARC_PAD_PORTS];
} ArcCoreOptions;

static ArcCoreOptions arc_core_options;
static const char *arc_left_stick_option_names[ARC_PAD_PORTS] = {
   "arcdroid_pad1_left_stick", "arcdroid_pad2_left_stick"
};
typedef struct {
   bool routing_valid;
   bool eligible;
   bool osk_visible;
   bool joystick;
   bool automatic[4];
   ArcInputBinding binding[4];
   bool awaiting;
   unsigned neutral_polls;
   unsigned directions;
   bool polled;
} ArcLeftStick;
static ArcLeftStick arc_left_sticks[ARC_PAD_PORTS];

static void arc_left_stick_await(unsigned port)
{
   ArcLeftStick *stick = &arc_left_sticks[port];
   stick->awaiting = true;
   stick->neutral_polls = 0u;
   stick->directions = 0u;
}

static bool arc_preparing_zero_tail;
static ArcMachine arc_session_machine;
static bool arc_riscpc_notice_shown;

static bool arc_media_is_hd(const ArcMediaEntry *entry)
{
   return entry && entry->populated && entry->high_density;
}

static bool arc_hd_native_route(const ArcCoreOptions *options,
                                ArcMachine machine)
{
   const ArcMachineDescriptor *descriptor = arc_machine_descriptor(machine);
   /* Preparation frees the parsed JFD before machine preflight; these
    * retained fields also remain valid after the selected JFD is refreshed. */
   return options->jfd_boot == ARC_JFD_BOOT_AUTO && descriptor &&
      descriptor->ide_capable && arc_media_count == 1u &&
      !arc_folder_set_active && arc_media[0].info.kind == ARC_MEDIA_JFD &&
      arc_media_is_hd(&arc_media[0]) &&
      arc_media[0].jfd_disc_number == 1u &&
      arc_media[0].jfd_disc_count == 1u;
}

static bool arc_options_resolve(ArcCoreOptions *options, ArcMachine machine,
                                char *error, size_t error_size)
{
   const ArcMachineDescriptor *descriptor = arc_machine_descriptor(machine);
   if (!descriptor) return false;
   options->machine = machine;
   if (options->cpu_auto) options->cpu = descriptor->cpu;
   if (options->cpu != descriptor->cpu)
   {
      if (error && error_size) snprintf(error, error_size,
         "%s is incompatible with the %s machine",
         arc_cpu_name(options->cpu), descriptor->name);
      return false;
   }
   if (options->requested_os != ARC_OS_UNKNOWN &&
       !arc_machine_supports_os(machine, options->requested_os))
   {
      if (error && error_size) snprintf(error, error_size,
         "%s is not supported on the %s",
         arc_os_name(options->requested_os), descriptor->name);
      return false;
   }
   if (options->request_a3010_joystick && machine != ARC_MACHINE_A3010)
   {
      if (error && error_size) snprintf(error, error_size,
         "A3010 joystick interface is incompatible with the %s machine",
         descriptor->name);
      return false;
   }
   return true;
}

static bool arc_machine_accepts_media(ArcMachine machine,
                                      const ArcMediaEntry *entry,
                                      char *error, size_t error_size)
{
   if (!arc_media_is_hd(entry) ||
       arc_machine_descriptor(machine)->io == ARC_IO_NEW_82C711) return true;
   if (error && error_size) snprintf(error, error_size,
      "hd-media-needs-new-io: High-density media requires A3010, A3020 or A4000.");
   return false;
}

static const char *const arc_pad_option_names[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT] = {
   {"arcdroid_pad1_up", "arcdroid_pad1_down", "arcdroid_pad1_left",
    "arcdroid_pad1_right", "arcdroid_pad1_a", "arcdroid_pad1_b",
    "arcdroid_pad1_x", "arcdroid_pad1_y", "arcdroid_pad1_start",
    "arcdroid_pad1_select"},
   {"arcdroid_pad2_up", "arcdroid_pad2_down", "arcdroid_pad2_left",
    "arcdroid_pad2_right", "arcdroid_pad2_a", "arcdroid_pad2_b",
    "arcdroid_pad2_x", "arcdroid_pad2_y", "arcdroid_pad2_start",
    "arcdroid_pad2_select"}
};

static const char *arc_option_value(const char *key, const char *fallback)
{
   struct retro_variable variable = {key, NULL};
   if (arc_environment && arc_environment(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
       variable.value && variable.value[0]) return variable.value;
   return fallback;
}

static bool arc_read_options(ArcCoreOptions *options, char *error, size_t error_size)
{
   const char *machine = arc_option_value("arcdroid_machine", "Auto");
   const char *cpu = arc_option_value("arcdroid_cpu", "Auto");
   const char *memory = arc_option_value("arcdroid_memory", "Auto");
   const char *os = arc_option_value("arcdroid_os", "Auto");
   const char *joystick = arc_option_value("arcdroid_joystick_if", "Auto");
   const char *pointer = arc_option_value("arcdroid_pointer_mode", "relative");
   const char *pointer_stick = arc_option_value("arcdroid_pointer_stick", "right");
   const char *pointer_speed = arc_option_value("arcdroid_pointer_speed", "600");
   const char *pointer_deadzone = arc_option_value("arcdroid_pointer_deadzone", "15");
   const char *writes = arc_option_value("arcdroid_disk_writes", "shadow");
   const char *short_tail = arc_option_value("arcdroid_short_tail", "missing");
   const char *jfd_omitted = arc_option_value("arcdroid_jfd_omitted", "file-data");
   const char *hard_disc = arc_option_value("arcdroid_hard_disc", "auto");
   const char *launch = arc_option_value("arcdroid_launch", "auto");
   const char *launch_chooser =
      arc_option_value("arcdroid_launch_chooser", "remember");
   const char *launch_ramdisc =
      arc_option_value("arcdroid_launch_ramdisc", "Auto");
   const char *jfd_tab = arc_option_value("arcdroid_jfd_write_protect", "as-imaged");
   const char *jfd_boot = arc_option_value("arcdroid_jfd_boot", "auto");
   const char *adffs_package = arc_option_value("arcdroid_adffs_package", "verified");
   const char *osk_toggle = arc_option_value("arcdroid_osk_toggle", "L3");
   if (!options) return false;
   memset(options, 0, sizeof(*options));
   const ArcMachineDescriptor *descriptor = arc_machine_from_name(machine);
   if (!descriptor || (strcmp(cpu, "Auto") && strcmp(cpu, "ARM2") && strcmp(cpu, "ARM250")) ||
       (strcmp(memory, "Auto") && strcmp(memory, "4 MB")))
      goto invalid;
   options->machine = descriptor->id;
   options->requested_machine = descriptor->id;
   options->machine_auto = !strcmp(machine, "Auto");
   options->cpu_auto = !strcmp(cpu, "Auto");
   options->cpu = !strcmp(cpu, "ARM2") ? ARC_CPU_ARM2 :
                  !strcmp(cpu, "ARM250") ? ARC_CPU_ARM250 : descriptor->cpu;
   if (!strcmp(os, "RISC OS 3.11")) options->requested_os = ARC_OS_RISCOS_311;
   else if (!strcmp(os, "RISC OS 3.10")) options->requested_os = ARC_OS_RISCOS_310;
   else if (!strcmp(os, "RISC OS 3.19")) options->requested_os = ARC_OS_RISCOS_319;
   else if (!strcmp(os, "RISC OS 3.00")) options->requested_os = ARC_OS_RISCOS_300;
   else if (!strcmp(os, "RISC OS 2.00")) options->requested_os = ARC_OS_RISCOS_200;
   else if (!strcmp(os, "Arthur 1.20")) options->requested_os = ARC_OS_ARTHUR_120;
   else if (strcmp(os, "Auto")) goto invalid;
   if (!strcmp(joystick, "A3010")) options->request_a3010_joystick = true;
   else if (strcmp(joystick, "Auto") && strcmp(joystick, "Keyboard")) goto invalid;
   if (!strcmp(pointer, "relative")) options->pointer_relative = true;
   else if (strcmp(pointer, "disabled")) goto invalid;
   if (!strcmp(pointer_stick, "right")) options->pointer_stick = ARC_POINTER_STICK_RIGHT;
   else if (!strcmp(pointer_stick, "left")) options->pointer_stick = ARC_POINTER_STICK_LEFT;
   else if (!strcmp(pointer_stick, "disabled")) options->pointer_stick = ARC_POINTER_STICK_DISABLED;
   else goto invalid;
   if (!strcmp(jfd_tab, "off")) options->jfd_write_policy = ARC_JFD_WRITE_TAB_OFF;
   else if (strcmp(jfd_tab, "as-imaged")) goto invalid;
   if (!strcmp(jfd_boot, "auto")) options->jfd_boot = ARC_JFD_BOOT_AUTO;
   else if (!strcmp(jfd_boot, "native")) options->jfd_boot = ARC_JFD_BOOT_NATIVE;
   else if (!strcmp(jfd_boot, "adffs")) options->jfd_boot = ARC_JFD_BOOT_ADFFS;
   else goto invalid;
   if (!strcmp(adffs_package, "verified")) options->adffs_package = ARC_ADFFS_VERIFIED_ONLY;
   else if (!strcmp(adffs_package, "untested")) options->adffs_package = ARC_ADFFS_ALLOW_UNTESTED;
   else goto invalid;
   if (!strcmp(launch_chooser, "remember"))
      options->launch_chooser = ARC_LAUNCH_CHOOSER_REMEMBER;
   else if (!strcmp(launch_chooser, "ask"))
      options->launch_chooser = ARC_LAUNCH_CHOOSER_ASK;
   else goto invalid;
   if (!strcmp(launch_ramdisc, "Auto")) options->launch_ramdisc = true;
   else if (strcmp(launch_ramdisc, "Disabled")) goto invalid;
   if (!strcmp(pointer_speed, "300")) options->pointer_speed = 300u;
   else if (!strcmp(pointer_speed, "450")) options->pointer_speed = 450u;
   else if (!strcmp(pointer_speed, "600")) options->pointer_speed = 600u;
   else if (!strcmp(pointer_speed, "900")) options->pointer_speed = 900u;
   else if (!strcmp(pointer_speed, "1200")) options->pointer_speed = 1200u;
   else goto invalid;
   if (!strcmp(pointer_deadzone, "0")) options->pointer_deadzone = 0u;
   else if (!strcmp(pointer_deadzone, "10")) options->pointer_deadzone = 10u;
   else if (!strcmp(pointer_deadzone, "15")) options->pointer_deadzone = 15u;
   else if (!strcmp(pointer_deadzone, "20")) options->pointer_deadzone = 20u;
   else if (!strcmp(pointer_deadzone, "25")) options->pointer_deadzone = 25u;
   else goto invalid;
   if (!strcmp(writes, "original")) options->disk_writes = ARC_DISK_WRITES_ORIGINAL;
   else if (!strcmp(writes, "off")) options->disk_writes = ARC_DISK_WRITES_OFF;
   else if (strcmp(writes, "shadow")) goto invalid;
   if (!strcmp(short_tail, "zero-filled")) options->short_tail_zero = true;
   else if (strcmp(short_tail, "missing")) goto invalid;
   if (!strcmp(jfd_omitted, "strict")) options->jfd_omitted_strict = true;
   else if (strcmp(jfd_omitted, "file-data")) goto invalid;
   if (!strcmp(hard_disc, "auto")) options->hard_disc_auto = true;
   else if (strcmp(hard_disc, "off")) goto invalid;
   if (!strcmp(launch, "auto")) options->launch = ARC_LAUNCH_AUTO;
   else if (!strcmp(launch, "off")) options->launch = ARC_LAUNCH_OFF;
   else if (!strncmp(launch, "app", 3u) && launch[3] >= '1' && launch[3] <= '8' && !launch[4])
      options->launch = (ArcLaunchOption)(ARC_LAUNCH_APP1 + launch[3] - '1');
   else goto invalid;
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
      {
         const char *value = arc_option_value(arc_pad_option_names[port][input], "Auto");
         ArcInputOverride *override = &options->pad[port][input];
         if (!strcmp(value, "Auto")) override->automatic = true;
         else if (!strcmp(value, "Unbound"))
         {
            override->automatic = false;
            override->binding = (ArcInputBinding){false, 0u};
         }
         else
         {
            const ArcInputKey *key = arc_input_key_value(value);
            if (!key) goto invalid;
            override->automatic = false;
            override->binding = (ArcInputBinding){true, key->key};
         }
      }
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
   {
      const char *value = arc_option_value(arc_left_stick_option_names[port], "dpad");
      if (!strcmp(value, "dpad")) options->left_stick_dpad[port] = true;
      else if (strcmp(value, "off")) goto invalid;
   }
   if (!strcmp(osk_toggle, "L3")) options->osk_toggle = ARC_OSK_TOGGLE_L3;
   else if (!strcmp(osk_toggle, "R3")) options->osk_toggle = ARC_OSK_TOGGLE_R3;
   else if (!strcmp(osk_toggle, "Off")) options->osk_toggle = ARC_OSK_TOGGLE_OFF;
   else goto invalid;
   return true;
invalid:
   if (error && error_size) snprintf(error, error_size, "Invalid ArcDroid core option value");
   return false;
}

static void arc_engine_log(void *opaque, const char *message)
{
   (void)opaque;
   if (arc_log && message) arc_log(RETRO_LOG_DEBUG, "%s", message);
}
static void arc_install_ide_unsupported(void *opaque, uint8_t command)
{
   (void)opaque;
   if (arc_log)
      arc_log(RETRO_LOG_WARN, "IDE unsupported command ABRT code=0x%02X",
              (unsigned)command);
}

typedef struct {
   uint8_t *data;
   size_t size;
   const ArcFirmwareFingerprint *match;
} ArcFirmwarePart;

typedef struct {
   const ArcFirmwareFingerprint *match;
   uint8_t *data;
   size_t size;
   unsigned priority;
   ArcOsClass best_available;
   unsigned best_available_priority;
   ArcOsClass requested_os;
   ArcMachine machine;
   size_t suspicious_size;
   size_t nearest_size;
   ArcFirmwarePart parts[ARC_OS_ARTHUR_030 + 1u][4];
} ArcFirmwareSearch;

static bool arc_visit_firmware(const char *path, void *opaque)
{
   ArcFirmwareSearch *search = (ArcFirmwareSearch *)opaque;
   void *data = NULL;
   size_t size = 0u;
   if (!arc_io_read_all(path, &data, &size)) return true;
   ArcDigest digest = arc_digest(data, size);
   const ArcFirmwareFingerprint *match = arc_firmware_match(&digest);
   if (match)
   {
      unsigned available_priority = arc_firmware_auto_priority(match->os);
      if (available_priority < search->best_available_priority)
      {
         search->best_available = match->os;
         search->best_available_priority = available_priority;
      }
   }
   if (match && !arc_machine_supports_os(search->machine, match->os))
   {
      free(data);
      return true;
   }
   size_t nearest = 0u;
   if (!match && arc_firmware_nearest_size(size, ARC_FIRMWARE_SIZE_MARGIN, &nearest))
   {
      search->suspicious_size = size;
      search->nearest_size = nearest;
   }
   if (match && match->recipe == ARC_RECIPE_COMBINED)
   {
      if (search->requested_os != ARC_OS_UNKNOWN && match->os != search->requested_os)
      {
         free(data);
         return true;
      }
      unsigned priority = arc_firmware_auto_priority(match->os);
      if (!search->match || priority < search->priority)
      {
         free(search->data);
         search->data = (uint8_t *)data;
         search->size = size;
         search->match = match;
         search->priority = priority;
         return true;
      }
   }
   else if (match && match->recipe != ARC_RECIPE_COMBINED && match->lane < 4u)
   {
      if (search->requested_os != ARC_OS_UNKNOWN && match->os != search->requested_os)
      {
         free(data);
         return true;
      }
      ArcFirmwarePart *part = &search->parts[match->os][match->lane];
      free(part->data);
      part->data = data;
      part->size = size;
      part->match = match;
      return true;
   }
   free(data);
   return true;
}

static void arc_finish_firmware_search(ArcFirmwareSearch *search)
{
   char error[160];
   for (unsigned os = ARC_OS_TEST; os <= ARC_OS_ARTHUR_030; ++os)
   {
      if (search->requested_os != ARC_OS_UNKNOWN &&
          (ArcOsClass)os != search->requested_os) continue;
      if (!arc_machine_supports_os(search->machine, (ArcOsClass)os)) continue;
      ArcFirmwarePart *first = &search->parts[os][0];
      if (!first->match) continue;
      ArcFirmwareRecipe recipe = first->match->recipe;
      unsigned count = recipe == ARC_RECIPE_BYTE_LANES_4 ? 4u :
                       recipe == ARC_RECIPE_HALFWORD_LANES_2 ? 2u : 0u;
      const uint8_t *parts[4] = {NULL, NULL, NULL, NULL};
      size_t sizes[4] = {0u, 0u, 0u, 0u};
      bool complete = count != 0u;
      for (unsigned lane = 0; lane < count; ++lane)
      {
         ArcFirmwarePart *part = &search->parts[os][lane];
         if (!part->match || part->match->os != (ArcOsClass)os ||
             part->match->recipe != recipe || part->match->lane != lane)
            complete = false;
         parts[lane] = part->data;
         sizes[lane] = part->size;
      }
      if (!complete || sizes[0] > SIZE_MAX / count) continue;
      size_t assembled_size = sizes[0] * count;
      uint8_t *assembled = malloc(assembled_size);
      if (!assembled || !arc_firmware_assemble(parts, sizes, count, recipe,
                                                assembled, assembled_size,
                                                error, sizeof(error)))
      {
         free(assembled);
         continue;
      }
      ArcDigest digest = arc_digest(assembled, assembled_size);
      const ArcFirmwareFingerprint *combined = arc_firmware_match(&digest);
      unsigned priority = arc_firmware_auto_priority((ArcOsClass)os);
      if (combined && combined->recipe == ARC_RECIPE_COMBINED &&
          combined->os == (ArcOsClass)os &&
          (!search->match || priority < search->priority))
      {
         free(search->data);
         search->data = assembled;
         search->size = assembled_size;
         search->match = combined;
         search->priority = priority;
      }
      else
         free(assembled);
   }
   for (unsigned os = 0; os <= ARC_OS_ARTHUR_030; ++os)
      for (unsigned lane = 0; lane < 4u; ++lane)
      {
         free(search->parts[os][lane].data);
         search->parts[os][lane].data = NULL;
      }
}

static bool arc_select_firmware(ArcOsClass requested, ArcMachine machine, uint8_t **data,
                                size_t *size,
                                const ArcFirmwareFingerprint **match,
                                char *error, size_t error_size)
{
   const char *system_dir = NULL;
   char firmware_dir[ARC_MEDIA_PATH_CAPACITY];
   ArcFirmwareSearch search = {0};
   search.priority = 100u;
   search.best_available_priority = 100u;
   search.requested_os = requested;
   search.machine = machine;
   if (!arc_environment ||
       !arc_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) ||
       !system_dir || !system_dir[0] ||
       snprintf(firmware_dir, sizeof(firmware_dir), "%s/arcdroid/firmware", system_dir) < 0)
   {
      if (error && error_size) snprintf(error, error_size,
         "Firmware directory unavailable; expected <system>/arcdroid/firmware.");
      return false;
   }
   arc_io_walk(firmware_dir, 8u, arc_visit_firmware, &search);
   arc_finish_firmware_search(&search);
   if (!search.match)
   {
      if (error && error_size)
      {
         if (search.suspicious_size)
            snprintf(error, error_size,
               "Unrecognised firmware size %zu bytes; nearest expected size is %zu bytes.",
               search.suspicious_size, search.nearest_size);
         else
            if (requested != ARC_OS_UNKNOWN)
               snprintf(error, error_size, "Missing %s firmware in <system>/arcdroid/firmware.", arc_os_name(requested));
            else
            {
               const ArcMachineDescriptor *d = arc_machine_descriptor(machine);
               if (d && d->io == ARC_IO_NEW_82C711)
                  snprintf(error, error_size,
                           "%s requires RISC OS 3.10 or later firmware; found %s",
                           d->name,
                           search.best_available == ARC_OS_UNKNOWN ? "none" :
                           arc_os_name(search.best_available));
               else
                  snprintf(error, error_size,
                           "Missing RISC OS 3.11 firmware in <system>/arcdroid/firmware.");
            }
      }
      return false;
   }
   *data = search.data;
   *size = search.size;
   if (match) *match = search.match;
   return true;
}

static const char *arc_os_slug(ArcOsClass os)
{
   switch (os)
   {
      case ARC_OS_TEST: return "test";
      case ARC_OS_RISCOS_311: return "riscos311";
      case ARC_OS_RISCOS_310: return "riscos310";
      case ARC_OS_RISCOS_319: return "riscos319";
      case ARC_OS_RISCOS_300: return "riscos300";
      case ARC_OS_RISCOS_201: return "riscos201";
      case ARC_OS_RISCOS_200: return "riscos200";
      case ARC_OS_ARTHUR_120: return "arthur120";
      case ARC_OS_ARTHUR_030: return "arthur030";
      default: return "unknown";
   }
}

static void arc_release_session(void)
{
#if defined(ARCDROID_TEST_FAULTS)
   arc_test_plan_clear();
#endif
   arc_engine_disc_configure(false, NULL, NULL, NULL, NULL);
   arc_io_set_write_observer(NULL);
   arc_disc_owned = false;
   arc_disc_notice_read = arc_disc_notice_write = 0u;
   arc_disc_invalidate();
   arc_boot_eligible = false;
   arc_boot_attempt = false;
   arc_boot_post_chooser = false;
   arc_boot_recovery_phase = false;
   arc_boot_recovery_action = NULL;
   arc_boot_watch_override = NULL;
   arc_boot_last_state = ARC_BOOT_WATCH_DISABLED;
   arc_boot_stamp_ready = false;
   arc_boot_record_hit = false;
   arc_boot_record_suppressed = false;
   if (arc_jfd_failure_status_shown && arc_environment)
   {
      struct retro_message_ext clear = {"", 1u, 3u, RETRO_LOG_INFO,
         RETRO_MESSAGE_TARGET_OSD, RETRO_MESSAGE_TYPE_STATUS, -1};
      (void)arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &clear);
      arc_jfd_failure_status_shown = false;
   }
   arc_chooser_input_armed = false;
   arc_chooser_release_gate = false;
   arc_engine_stop();
#if defined(ARCDROID_TEST_PLANS)
   arc_jfd_free(&arc_rpc_helper_entry.jfd);
   memset(&arc_rpc_helper_entry,0,sizeof(arc_rpc_helper_entry));
#endif
   arc_error_pad_gate = false;
   arc_error_was_ready = false;
   arc_error_seen_generation = 0u;
   arc_error_pad_levels[0] = arc_error_pad_levels[1] = false;
   arc_error_hint_generation = 0u;
   arc_error_hint_state = 0u;
   arc_install_store_close(arc_hd_store);
   arc_hd_store = NULL;
   arc_hd_failure_noticed = false;
   arc_hd_readonly_noticed = false;
   arc_hd_write_failure_noticed = false;
   arc_hd_damaged = false;
   memset(&arc_hd_scope, 0, sizeof(arc_hd_scope));
   arc_hd_mode = arc_hd_requested_mode = ARC_HD_HELPER_FLOPPY;
   arc_hd_observe_target = arc_hd_observe_copy = false;
   arc_hd_install_used = arc_hd_product_used = false;
   arc_hd_notice_shown = false;
   arc_hd_reset_cancel_pending = false;
   arc_hd_last_outcome_state = ARC_OUTCOME_OFF;
   arc_hd_scope_cached = false;
   snprintf(arc_hd_reason, sizeof(arc_hd_reason), "not-attachable");
   arc_power_on_shift = false;
   arc_input_reset(&arc_inputs);
   arc_adffs_discard();
   free(arc_firmware);
   arc_firmware = NULL;
   arc_firmware_size = 0u;
   for (unsigned i = 0u; i < ARC_PLAYLIST_MAX_ENTRIES; ++i)
      arc_jfd_free(&arc_media[i].jfd);
   memset(arc_media, 0, sizeof(arc_media));
   arc_media_count = 0u;
   arc_original_jfd_notice_shown = false;
   arc_jfd_tab_pending_notice_shown = false;
   arc_jfd_tab_hidden_notice_shown = false;
   arc_jfd_tab_adffs_notice_shown = false;
   arc_folder_set_active = false;
   arc_zip_active = false;
   arc_zip_path[0] = '\0';
   arc_zip_adffs_raw_discard();
   arc_image_index = 0u;
   arc_cmos_path[0] = '\0';
   arc_helper_path[0] = '\0';
   arc_choice_path[0] = '\0';
   arc_cmos_last_written_valid = false;
   arc_cmos_ignored_logged = false;
   arc_cmos_ignored_attempts = 0u;
   arc_cmos_ignored_changes = 0u;
   arc_session = ARC_SESSION_EMPTY;
   memset(&arc_chooser_discovery, 0, sizeof(arc_chooser_discovery));
   memset(&arc_chooser, 0, sizeof(arc_chooser));
   memset(&arc_plan, 0, sizeof(arc_plan));
}

static void arc_sync_cmos(void)
{
   const ArcMachineDescriptor *machine = arc_machine_descriptor(arc_plan.machine);
   uint8_t payload[ARC_CMOS_PAYLOAD_SIZE];
   if (!arc_loaded || !arc_engine_active()) return;
   if (!arc_cmos_decode(arc_cmos_record, &arc_plan, payload))
   {
      if (!arc_cmos_default(arc_plan.os, machine->io, payload)) return;
   }
   else
      arc_engine_cmos(payload);
   arc_cmos_normalise_payload(payload, arc_cmos_policy_os(), machine->io);
   arc_cmos_encode(arc_cmos_record, &arc_plan, payload);
}

static void arc_seed_cmos(void)
{
   const ArcMachineDescriptor *machine = arc_machine_descriptor(arc_plan.machine);
   uint8_t payload[ARC_CMOS_PAYLOAD_SIZE] = {0};
   void *existing = NULL;
   size_t existing_size = 0u;
   if (arc_cmos_path[0] && arc_io_read_all(arc_cmos_path, &existing, &existing_size) &&
       existing_size == sizeof(arc_cmos_record) &&
      arc_cmos_decode((const uint8_t *)existing, &arc_plan, payload))
   {
      memcpy(arc_cmos_record, existing, sizeof(arc_cmos_record));
      memcpy(arc_cmos_last_written, existing, sizeof(arc_cmos_last_written));
      arc_cmos_last_written_valid = true;
   }
   else
   {
      if (!arc_cmos_default(arc_plan.os, machine->io, payload)) return;
      arc_cmos_encode(arc_cmos_record, &arc_plan, payload);
      arc_cmos_last_written_valid = false;
   }
   unsigned healed = arc_cmos_clear_unplug_bytes(payload, arc_plan.os);
   if (healed && arc_cmos_last_written_valid && arc_log)
      arc_log(RETRO_LOG_INFO, "CMOS: cleared %u unplug byte(s) from the persisted record\n", healed);
   if (arc_cmos_last_written_valid && arc_cmos_policy_riscos3() &&
       (payload[64u] & 0x10u) && arc_log)
      arc_log(RETRO_LOG_INFO, "CMOS: cleared launch-owned boot flag\n");
   arc_cmos_normalise_payload(payload, arc_cmos_policy_os(), machine->io);
   arc_cmos_encode(arc_cmos_record, &arc_plan, payload);
   memcpy(arc_cmos_prepared, arc_cmos_record, sizeof(arc_cmos_prepared));
   free(existing);
}

static void arc_cmos_ignored_write(void *opaque, unsigned raw,
                                   uint8_t attempted, uint8_t retained)
{
   (void)opaque;
   if (arc_cmos_ignored_attempts != UINT_MAX) ++arc_cmos_ignored_attempts;
   if (attempted != retained && arc_cmos_ignored_changes != UINT_MAX)
      ++arc_cmos_ignored_changes;
   if (!arc_cmos_ignored_logged && arc_log)
   {
      unsigned logical = arc_cmos_logical_address(raw);
      arc_log(RETRO_LOG_INFO,
              "CMOS: guest write to unplug byte %u (raw %02X) ignored: attempted %02X retained %02X (module unplugging is session-scoped on %s)\n",
              logical, raw, attempted, retained, arc_os_name(arc_plan.os));
      arc_cmos_ignored_logged = true;
   }
}

static void arc_persist_cmos(void)
{
   const ArcMachineDescriptor *machine = arc_machine_descriptor(arc_plan.machine);
   uint8_t payload[ARC_CMOS_PAYLOAD_SIZE];
   if (!arc_cmos_path[0] || arc_session != ARC_SESSION_RUNNING ||
       !arc_engine_active()) return;
   arc_sync_cmos();
   if (!arc_cmos_decode(arc_cmos_record, &arc_plan, payload))
   {
      if (!arc_cmos_default(arc_plan.os, machine->io, payload)) return;
      arc_cmos_encode(arc_cmos_record, &arc_plan, payload);
   }
   if (!arc_cmos_last_written_valid ||
       memcmp(arc_cmos_record, arc_cmos_last_written, sizeof(arc_cmos_record)) != 0)
   {
      if (arc_io_write_all(arc_cmos_path, arc_cmos_record, sizeof(arc_cmos_record)))
      {
         memcpy(arc_cmos_last_written, arc_cmos_record, sizeof(arc_cmos_last_written));
         arc_cmos_last_written_valid = true;
      }
   }
}

static void arc_boot_persist_seed(void)
{
   if (!arc_boot_post_chooser || !arc_cmos_path[0]) return;
   if ((!arc_cmos_last_written_valid ||
        memcmp(arc_cmos_prepared, arc_cmos_last_written,
               sizeof(arc_cmos_prepared))) &&
       arc_io_write_all(arc_cmos_path, arc_cmos_prepared,
                        sizeof(arc_cmos_prepared)))
   {
      memcpy(arc_cmos_last_written, arc_cmos_prepared,
             sizeof(arc_cmos_last_written));
      arc_cmos_last_written_valid = true;
   }
}

static void arc_message(const char *text, enum retro_log_level level)
{
   struct retro_message_ext ext = {
      text, 5000u, 3u, level, RETRO_MESSAGE_TARGET_ALL,
      RETRO_MESSAGE_TYPE_NOTIFICATION, -1
   };
   struct retro_message legacy = {text, 300u};
   if (!arc_environment)
      return;
   if (arc_log)
      arc_log(level, "%s\n", text);
   if (!arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &ext))
      arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE, &legacy);
}
static void arc_disc_queue_notice(const char *text)
{
   unsigned next = (arc_disc_notice_write + 1u) % 8u;
   if (next == arc_disc_notice_read) return;
   snprintf(arc_disc_notices[arc_disc_notice_write],
            sizeof(arc_disc_notices[0]), "%s", text);
   arc_disc_notice_write = next;
}
static void arc_disc_present_notice(void)
{
   if (arc_disc_notice_read == arc_disc_notice_write) return;
   const char *message = arc_disc_notices[arc_disc_notice_read];
   struct retro_message_ext ext = {message, 2000u, 3u, RETRO_LOG_INFO,
      RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION, -1};
   struct retro_message legacy = {message, 120u};
   if (arc_log) arc_log(RETRO_LOG_INFO, "%s\n", message);
   if (arc_environment &&
       !arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &ext))
      arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE, &legacy);
   arc_disc_notice_read = (arc_disc_notice_read + 1u) % 8u;
}
static void arc_error_hint(const ArcErrorBox *box)
{
   const char *message = box->ok && box->cancel ?
      "Error box: A = OK · B = Cancel" :
      box->ok ? "Error box: A = OK" : "Error box: B = Cancel";
   struct retro_message_ext ext = {message, 2000u, 3u, RETRO_LOG_INFO,
      RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION, -1};
   struct retro_message legacy = {message, 120u};
   if (arc_log) arc_log(RETRO_LOG_INFO, "%s\n", message);
   if (arc_environment &&
       !arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &ext))
      arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE, &legacy);
}

void arc_jfd_store_notice(const char *message, bool warning)
{
   if (warning && strstr(message, "can no longer be saved this session"))
   {
      struct retro_message_ext status = {message, 3600000u, 3u,
         RETRO_LOG_WARN, RETRO_MESSAGE_TARGET_OSD,
         RETRO_MESSAGE_TYPE_STATUS, -1};
      if (arc_log) arc_log(RETRO_LOG_WARN, "%s\n", message);
      if (arc_environment &&
          arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &status))
      { arc_jfd_failure_status_shown = true; return; }
   }
   arc_message(message, warning ? RETRO_LOG_WARN : RETRO_LOG_INFO);
}

void arc_jfd_store_log_commit(unsigned image_number, uint64_t generation,
                              size_t sector_index)
{
   if (image_number) arc_disc_invalidate_image(image_number - 1u);
#if defined(ARCDROID_TEST_FAULTS)
   if (arc_log) arc_log(RETRO_LOG_INFO,
      "JFD write: image=%u generation=%llu descriptor=%zu\n",
      image_number, (unsigned long long)generation, sector_index);
#else
   (void)image_number; (void)generation; (void)sector_index;
#endif
}

static void arc_media_notice(const char *text, bool notify_user,
                             bool *notice_reported)
{
   if (notice_reported) *notice_reported = true;
   if (notify_user)
      arc_message(text, RETRO_LOG_WARN);
   else if (arc_log)
      arc_log(RETRO_LOG_WARN, "%s\n", text);
}

static void arc_vidc_episode_update(bool field_completed, uint64_t elapsed_us)
{
   if (field_completed)
   {
      arc_vidc_warning_reported = false;
      arc_vidc_transient_logged = false;
      arc_vidc_incomplete_us = 0u;
      return;
   }
   if (!arc_vidc_transient_logged)
   {
      if (arc_log)
         arc_log(RETRO_LOG_INFO,
                 "VIDC field incomplete; presenting bounded fallback while guest video is programmed.\n");
      arc_vidc_transient_logged = true;
   }
   /* Two seconds is the midpoint of R-36's permitted 1--3 second range: long
    * enough for normal boot/mode transitions, while still reporting a
    * sustained bad program promptly. Count only bounded guest execution. */
   if (elapsed_us >= ARC_VIDC_WARNING_THRESHOLD_US - arc_vidc_incomplete_us)
      arc_vidc_incomplete_us = ARC_VIDC_WARNING_THRESHOLD_US;
   else
      arc_vidc_incomplete_us += elapsed_us;
   if (arc_vidc_incomplete_us >= ARC_VIDC_WARNING_THRESHOLD_US &&
       !arc_vidc_warning_reported)
   {
      arc_message("VIDC program did not complete a field; presenting bounded fallback.",
                  RETRO_LOG_WARN);
      arc_vidc_warning_reported = true;
   }
}

static void arc_media_stem(const char *path, char label[ARC_MEDIA_LABEL_CAPACITY])
{
   const char *slash = strrchr(path, '/');
   const char *backslash = strrchr(path, '\\');
   const char *name = slash && backslash ? (slash > backslash ? slash : backslash) + 1 :
                      slash ? slash + 1 : backslash ? backslash + 1 : path;
   const char *dot = strrchr(name, '.');
   size_t length = dot && dot > name ? (size_t)(dot - name) : strlen(name);
   if (length >= ARC_MEDIA_LABEL_CAPACITY) length = ARC_MEDIA_LABEL_CAPACITY - 1u;
   memcpy(label, name, length);
   label[length] = '\0';
}

static bool arc_atomic_publish(const char *target, const uint8_t *data, size_t size,
                               bool *created, char *error, size_t error_size)
{
   char temporary[ARC_MEDIA_PATH_CAPACITY];
   int written = snprintf(temporary, sizeof(temporary), "%s.tmp", target);
   if (written < 0 || (size_t)written >= sizeof(temporary))
      goto failure;
   (void)arc_io_remove(temporary);
   if (!arc_io_write_all(temporary, data, size) || !arc_io_rename(temporary, target))
   {
      (void)arc_io_remove(temporary);
      goto failure;
   }
   if (created) *created = true;
   return true;
failure:
   if (error && error_size) snprintf(error, error_size,
      "Unable to publish staged media with flush, close and rename.");
   return false;
}

static bool arc_file_digest_matches(const char *path, const uint8_t *expected,
                                    size_t expected_size)
{
   void *data = NULL;
   size_t size = 0u;
   if (!arc_io_read_all(path, &data, &size)) return false;
   ArcDigest actual_digest = arc_digest(data, size);
   ArcDigest expected_digest = arc_digest(expected, expected_size);
   bool matches = size == expected_size &&
      !strcmp(actual_digest.sha1, expected_digest.sha1);
   free(data);
   return matches;
}

static bool arc_prepare_bytes(const char *source, const char *playlist_label,
                              ArcDiskWriteMode mode, bool notify_user,
                              bool *notice_reported,
                              const uint8_t *source_data, size_t source_size,
                              bool take_decoded_jfd_ownership,
                              ArcMediaEntry *entry, char *error,
                              size_t error_size)
{
   uint8_t *staged_data = NULL;
   memset(entry, 0, sizeof(*entry));
   if (!source || !source[0] || strlen(source) >= sizeof(entry->source) ||
       !source_data)
   {
      if (error && error_size) snprintf(error, error_size,
         "invalid media preparation input");
      if (take_decoded_jfd_ownership)
         free((void *)(uintptr_t)source_data);
      return false;
   }
   if (!arc_media_inspect(source, source_data, source_size, &entry->info,
                          error, error_size))
   {
      if (take_decoded_jfd_ownership)
         free((void *)(uintptr_t)source_data);
      return false;
   }
   entry->zero_tail_presentation = entry->info.short_image && arc_preparing_zero_tail;
   entry->effective_extent = entry->info.short_image && !arc_preparing_zero_tail ?
      entry->info.available_size : ARC_MEDIA_EXTENT_UNRESTRICTED;
   strcpy(entry->source, source);
   if (playlist_label && playlist_label[0])
      strcpy(entry->label, playlist_label);

   const uint8_t *publish_data = source_data;
   size_t publish_size = source_size;
   bool immutable = mode == ARC_DISK_WRITES_OFF;
   const char *extension = "adf";
   if (entry->info.kind == ARC_MEDIA_JFD)
   {
      /* Plain-file JFDI preparation transfers the VFS read buffer to the
       * parser, preserving the pre-zip peak. Zip members stay borrowed here
       * because a single JFD member may also be retained for ADFFS. */
      bool parsed = take_decoded_jfd_ownership ?
         arc_jfd_parse_owned((uint8_t *)(uintptr_t)source_data, source_size,
                             &entry->jfd, error, error_size) :
         arc_jfd_parse(source_data, source_size, &entry->jfd, error,
                       error_size);
      if (!parsed)
         return false;
      entry->compatibility = arc_riscpc_compatibility(entry->jfd.extended, entry->jfd.flags);
      entry->high_density = arc_jfd_high_density(&entry->jfd);
      if (arc_log)
      {
         arc_log(RETRO_LOG_INFO,
            "JFD: title=\"%s\" sequence=%08x flags=%08x fps_x2=%u obey=%u bytes header=%s\n",
            entry->jfd.title, entry->jfd.sequence, entry->jfd.flags,
            entry->jfd.fps_times_two, entry->jfd.obey_length,
            entry->jfd.extended ? "extended" : "legacy");
         if (entry->jfd.ignored_header_fields)
            arc_log(RETRO_LOG_WARN,
               "JFD: unknown flags or reserved header bytes are ignored by Slice 2a.\n");
      }
      if (entry->jfd.extended && !arc_riscpc_notice_shown)
      {
         unsigned cpu_mask = (entry->jfd.flags >> 8u) & 0x3fu;
         unsigned os_mask = (entry->jfd.flags >> 16u) & 0x7fu;
         if (cpu_mask && os_mask && !(cpu_mask & 3u) && !(os_mask & 3u))
         {
            arc_riscpc_notice_shown = true;
            arc_media_notice("This title is flagged for RiscPC-class machines (StrongARM/RISC OS 3.5+), which ArcDroid does not emulate; booting anyway.",
                             notify_user, notice_reported);
         }
      }
      entry->digest = arc_digest(entry->jfd.decoded, entry->jfd.decoded_size);
      entry->jfd_game_id = entry->jfd.game_id;
      entry->jfd_disc_number = entry->jfd.disc_number;
      entry->jfd_disc_count = entry->jfd.disc_count;
      publish_data = entry->jfd.decoded;
      publish_size = entry->jfd.decoded_size;
      immutable = true;
      extension = "jfd";
      if (!entry->label[0] && entry->jfd.title[0])
      {
         size_t length = strlen(entry->jfd.title);
         if (length >= sizeof(entry->label)) length = sizeof(entry->label) - 1u;
         memcpy(entry->label, entry->jfd.title, length);
         entry->label[length] = '\0';
      }
   }
   else
   {
      entry->high_density = entry->info.geometry == ARC_GEOMETRY_1600K_F;
      entry->digest = arc_digest(source_data, source_size);
      extension = entry->info.geometry == ARC_GEOMETRY_640K_L ? "adl" : "adf";
      if (entry->info.short_image)
      {
         if (mode == ARC_DISK_WRITES_ORIGINAL)
         {
            if (error && error_size) snprintf(error, error_size,
               "Short image cannot use original writes; choose shadow or off.");
            return false;
         }
         staged_data = calloc(1u, entry->info.declared_size);
         if (!staged_data)
         { if (error && error_size) snprintf(error, error_size, "out of memory"); return false; }
         memcpy(staged_data, source_data, source_size);
         publish_data = staged_data;
         publish_size = entry->info.declared_size;
      }
      if (entry->info.recognition == ARC_MEDIA_RECOGNITION_SIZE_FALLBACK &&
          !arc_media_is_recognised_adfs(source_data, source_size))
         arc_media_notice("Media is not a recognised ADFS image; booting anyway.",
                          notify_user, notice_reported);
   }
   if (!entry->label[0]) arc_media_stem(source, entry->label);

   if (mode == ARC_DISK_WRITES_ORIGINAL && entry->info.kind != ARC_MEDIA_JFD)
      strcpy(entry->staged, source);
   else
   {
      char directory[ARC_MEDIA_PATH_CAPACITY];
      int n = snprintf(directory, sizeof(directory), "%s/arcdroid/media/%s",
                       arc_save_path, entry->digest.sha1);
      int staged_n = n < 0 || (size_t)n >= sizeof(directory) ? -1 :
         snprintf(entry->staged, sizeof(entry->staged), "%s/%s.%s", directory,
                  immutable ? "disc-readonly" : "disc", extension);
      if (n < 0 || (size_t)n >= sizeof(directory) || staged_n < 0 ||
          (size_t)staged_n >= sizeof(entry->staged) || !arc_io_make_path(directory))
      {
         if (error && error_size) snprintf(error, error_size, "unable to prepare staged media directory");
         goto reject;
      }
      size_t existing_size = 0u;
      bool exists = arc_io_file_size(entry->staged, &existing_size);
      if (exists && !immutable)
      {
         void *check = NULL;
         size_t check_size = 0u;
         if (existing_size != publish_size || !arc_io_read_all(entry->staged, &check, &check_size) ||
             check_size != publish_size)
         {
            free(check);
            if (error && error_size) snprintf(error, error_size,
               "Writable media shadow has a structural fault and was not replaced.");
            goto reject;
         }
         free(check);
      }
      else if (!exists || !arc_file_digest_matches(entry->staged, publish_data, publish_size))
      {
         if (exists && !arc_io_remove(entry->staged))
         {
            if (error && error_size) snprintf(error, error_size, "unable to replace invalid immutable staged media");
            goto reject;
         }
         if (!arc_atomic_publish(entry->staged, publish_data, publish_size,
                                 &entry->created_this_preparation, error, error_size))
            goto reject;
      }
   }
   entry->populated = true;
   if (entry->info.short_image)
   {
      char warning[256];
      if (entry->zero_tail_presentation)
         snprintf(warning, sizeof(warning),
            "Image is missing its last %zu bytes; they are presented zero-filled (option 'Short image tail'). Source unchanged.",
            entry->info.padding_required);
      else
         snprintf(warning, sizeof(warning),
            "Image is missing its last %zu bytes; they are presented as absent (unreadable) sectors. Source unchanged. Option 'Short image tail' can present them zero-filled.",
            entry->info.padding_required);
      arc_media_notice(warning, notify_user, notice_reported);
      if (arc_log)
         arc_log(RETRO_LOG_INFO,
            "Short image: geometry=%s source=%zu backing=%zu effective=%zu absent-sectors=%zu presentation=%s accept=%s shadow=%s\n",
            entry->info.geometry == ARC_GEOMETRY_320K ? "320K" :
            entry->info.geometry == ARC_GEOMETRY_640K_L ? "L" :
            entry->info.geometry == ARC_GEOMETRY_800K_D ? "D" : "E",
            entry->info.available_size, entry->info.declared_size,
            entry->effective_extent ? entry->effective_extent : entry->info.declared_size,
            entry->effective_extent ? entry->info.padding_required /
               (entry->info.geometry == ARC_GEOMETRY_320K ||
                entry->info.geometry == ARC_GEOMETRY_640K_L ? 256u : 1024u) : 0u,
            entry->zero_tail_presentation ? "zero-filled" : "missing",
            entry->info.free_map_accept ? "free-map" : "tolerance",
            entry->created_this_preparation ? "new" : "existing");
   }
   /* JFD staging is immutable. Retain its metadata, digest and label here,
    * then re-read and parse only the selected image immediately before an
    * insert. This keeps exactly one decoded JFD buffer live per session. */
   if (entry->info.kind == ARC_MEDIA_JFD) arc_jfd_free(&entry->jfd);
   free(staged_data);
   return true;
reject:
   arc_jfd_free(&entry->jfd);
   free(staged_data);
   memset(entry, 0, sizeof(*entry));
   return false;
}

static bool arc_prepare_source(const char *source, const char *playlist_label,
                               ArcDiskWriteMode mode, bool notify_user,
                               bool *notice_reported, ArcMediaEntry *entry,
                               char *error, size_t error_size)
{
   void *source_data = NULL;
   size_t source_size = 0u;
   if (!source || !source[0] ||
       !arc_io_read_all(source, &source_data, &source_size))
   {
      if (error && error_size) snprintf(error, error_size,
         "unable to read media through the frontend VFS");
      return false;
   }
   bool decoded_source = source_size >= 4u &&
      !memcmp(source_data, "JFDI", 4u);
   bool prepared = arc_prepare_bytes(source, playlist_label, mode, notify_user,
                                     notice_reported, source_data, source_size,
                                     decoded_source,
                                     entry, error, error_size);
   if (!decoded_source) free(source_data);
   return prepared;
}

static uint8_t arc_hex_nibble(char value)
{
   return (uint8_t)(value >= '0' && value <= '9' ? value - '0' :
      value >= 'a' && value <= 'f' ? value - 'a' + 10 : value - 'A' + 10);
}

static void arc_cleanup_failed_preparation(void)
{
   for (unsigned i = 0u; i < arc_media_count; ++i)
   {
      if (arc_media[i].created_this_preparation && arc_media[i].staged[0])
         (void)arc_io_remove(arc_media[i].staged);
      arc_jfd_free(&arc_media[i].jfd);
   }
   memset(arc_media, 0, sizeof(arc_media));
   arc_media_count = 0u;
}

static void arc_cleanup_folder_siblings(void)
{
   for (unsigned i = 1u; i < arc_media_count; ++i)
   {
      if (arc_media[i].created_this_preparation && arc_media[i].staged[0])
         (void)arc_io_remove(arc_media[i].staged);
      arc_jfd_free(&arc_media[i].jfd);
      memset(&arc_media[i], 0, sizeof(arc_media[i]));
   }
   arc_media_count = arc_media_count ? 1u : 0u;
}

typedef struct {
   ArcFolderSet *set;
   unsigned omissions;
} ArcFolderCollection;

static bool arc_collect_folder_member(const char *name, bool regular_file,
                                      void *opaque)
{
   ArcFolderCollection *collection = opaque;
   ArcFolderMemberResult result = arc_folder_set_add(collection->set, name,
                                                      regular_file);
   if (result == ARC_FOLDER_MEMBER_PATH_TOO_LONG)
   {
      ++collection->omissions;
      if (arc_log)
         arc_log(RETRO_LOG_WARN,
                 "Folder disc set sibling omitted: path exceeds 4095 bytes.\n");
   }
   return true;
}

static void arc_log_folder_result(const ArcFolderSet *set, unsigned omissions)
{
   if (arc_log)
      arc_log(RETRO_LOG_INFO,
              "Folder disc set: VFS directory=\"%s\" qualified=%u prepared=%u omitted=%u\n",
              set->directory, set->qualified_count, arc_media_count, omissions);
}

static void arc_folder_summary(const char *action, unsigned omissions,
                               unsigned notices)
{
   char message[192];
   if (!omissions && !notices) return;
   snprintf(message, sizeof(message),
            "Folder disc set %s with %u sibling%s omitted and %u media notice%s; see the log for details.",
            action, omissions, omissions == 1u ? "" : "s",
            notices, notices == 1u ? "" : "s");
   arc_message(message, RETRO_LOG_WARN);
}

static void arc_discard_candidate(ArcMediaEntry *entry)
{
   if (entry->created_this_preparation && entry->staged[0])
      (void)arc_io_remove(entry->staged);
   arc_jfd_free(&entry->jfd);
   memset(entry, 0, sizeof(*entry));
}

static bool arc_prepare_folder_siblings(const char *content_path,
                                        bool windows_host,
                                        ArcDiskWriteMode mode,
                                        size_t selected_logical)
{
   ArcFolderSetTrigger trigger = arc_folder_set_begin(content_path, windows_host,
                                                       NULL);
   if (trigger == ARC_FOLDER_SET_NONE) return false;
   if (trigger == ARC_FOLDER_SET_DIRECTORY_TOO_LONG)
   {
      if (arc_log)
         arc_log(RETRO_LOG_WARN,
                 "Folder disc set enumeration skipped: parent directory exceeds 1021 bytes.\n");
      arc_message("Folder disc set directory is too long to enumerate safely; loading the selected image alone.",
                  RETRO_LOG_WARN);
      return true;
   }
   ArcFolderSet *set = malloc(sizeof(*set));
   if (!set)
   {
      arc_message("Folder disc set discovery ran out of memory; loading the selected image alone.",
                  RETRO_LOG_WARN);
      return true;
   }
   (void)arc_folder_set_begin(content_path, windows_host, set);

   ArcFolderCollection collection = {set, 0u};
   if (!arc_io_list_directory(set->directory, arc_collect_folder_member,
                              &collection))
   {
      if (arc_log)
         arc_log(RETRO_LOG_WARN,
                 "Folder disc set enumeration failed for VFS directory=\"%s\".\n",
                 set->directory);
      arc_message("Folder disc set directory could not be opened; loading the selected image alone.",
                  RETRO_LOG_WARN);
      free(set);
      return true;
   }
   if (set->overflow)
   {
      arc_log_folder_result(set, set->qualified_count);
      arc_message("Folder disc set has more than 31 siblings; loading the selected image alone.",
                  RETRO_LOG_WARN);
      free(set);
      return true;
   }

   arc_folder_set_sort(set);
   size_t total = selected_logical;
   unsigned notices = 0u;
   for (unsigned sibling = 0u; sibling < set->count; ++sibling)
   {
      char source[ARC_MEDIA_PATH_CAPACITY];
      char reason[512] = {0};
      ArcMediaEntry candidate;
      bool notice = false;
      if (!arc_folder_set_path(set, sibling, source) ||
          !arc_prepare_source(source, NULL, mode, false, &notice, &candidate,
                              reason, sizeof(reason)))
      {
         ++collection.omissions;
         if (arc_log)
            arc_log(RETRO_LOG_WARN,
                    "Folder disc set sibling omitted: path=\"%s\" reason=%s\n",
                    arc_folder_set_path(set, sibling, source) ? source :
                       set->siblings[sibling],
                    reason[0] ? reason : "resolved path is invalid");
         continue;
      }
      if (notice) ++notices;
      bool duplicate = false;
      for (unsigned previous = 0u; previous < arc_media_count; ++previous)
         if (!strcmp(arc_media[previous].digest.sha1, candidate.digest.sha1))
            duplicate = true;
      size_t logical = candidate.info.declared_size;
      if (!duplicate && logical > 256u * 1024u * 1024u - total)
      {
         arc_discard_candidate(&candidate);
         arc_cleanup_folder_siblings();
         arc_log_folder_result(set, collection.omissions);
         arc_message("Folder disc set exceeds 256 MiB of logical media; loading the selected image alone.",
                     RETRO_LOG_WARN);
         free(set);
         return true;
      }
      if (!duplicate) total += logical;
      arc_media[arc_media_count++] = candidate;
   }
   arc_log_folder_result(set, collection.omissions);
   arc_folder_summary("loaded", collection.omissions, notices);
   free(set);
   return true;
}

static const char *arc_path_leaf(const char *path)
{
   const char *slash = path ? strrchr(path, '/') : NULL;
   const char *backslash = path ? strrchr(path, '\\') : NULL;
   return slash && backslash ? (slash > backslash ? slash : backslash) + 1u :
      slash ? slash + 1u : backslash ? backslash + 1u : path;
}

static void arc_zip_escape_member(const char *member, char escaped[513])
{
   size_t output = 0u;
   for (size_t input = 0u; member && member[input] && output < 512u; ++input)
   {
      if ((member[input] == '"' || member[input] == '\\') && output < 511u)
         escaped[output++] = '\\';
      escaped[output++] = member[input];
   }
   escaped[output] = '\0';
}

static void arc_zip_summary(unsigned omissions, unsigned notices, bool fallback,
                            const char *preferred, const char *boot)
{
   if (fallback)
   {
      char skipped[513], started[513], message[1152];
      arc_zip_escape_member(preferred, skipped);
      arc_zip_escape_member(boot, started);
      unsigned other_omissions = omissions ? omissions - 1u : 0u;
      if (other_omissions || notices)
         snprintf(message, sizeof(message),
                  "Zip preferred image \"%s\" could not be used; started \"%s\" instead (%u other image%s omitted, %u media notice%s).",
                  skipped, started, other_omissions,
                  other_omissions == 1u ? "" : "s", notices,
                  notices == 1u ? "" : "s");
      else
         snprintf(message, sizeof(message),
                  "Zip preferred image \"%s\" could not be used; started \"%s\" instead.",
                  skipped, started);
      arc_message(message, RETRO_LOG_WARN);
      return;
   }
   if (!omissions && !notices) return;
   char message[256];
   snprintf(message, sizeof(message),
            "Zip content loaded with %u image%s omitted and %u media notice%s; see the log for details.",
            omissions, omissions == 1u ? "" : "s",
            notices, notices == 1u ? "" : "s");
   arc_message(message, RETRO_LOG_WARN);
}

static void arc_zip_log(const char *leaf, unsigned entries, unsigned images,
                        unsigned prepared, unsigned omitted,
                        const ArcZipContent *content, const char *preferred,
                        const char *boot, bool fallback, const char *selected)
{
   if (!arc_log) return;
   char escaped_preferred[513], escaped_boot[513], escaped_selected[513];
   arc_zip_escape_member(preferred, escaped_preferred);
   arc_zip_escape_member(boot, escaped_boot);
   arc_zip_escape_member(selected, escaped_selected);
   if (selected && selected[0] && boot && boot[0])
      arc_log(RETRO_LOG_INFO,
              "Zip content: file=%s entries=%u images=%u prepared=%u omitted=%u rule=%s candidates=%u preferred=\"%s\" boot=\"%s\" fallback=%s selected=\"%s\"\n",
              leaf, entries, images, prepared, omitted,
              arc_zip_content_rule_name(content->rule), content->candidate_count,
              escaped_preferred, escaped_boot, fallback ? "yes" : "no",
              escaped_selected);
   else if (selected && selected[0])
      arc_log(RETRO_LOG_INFO,
              "Zip content: file=%s entries=%u images=%u prepared=%u omitted=%u rule=%s candidates=%u preferred=\"%s\" boot=none fallback=%s selected=\"%s\"\n",
              leaf, entries, images, prepared, omitted,
              arc_zip_content_rule_name(content->rule), content->candidate_count,
              escaped_preferred, fallback ? "yes" : "no", escaped_selected);
   else if (boot && boot[0])
      arc_log(RETRO_LOG_INFO,
              "Zip content: file=%s entries=%u images=%u prepared=%u omitted=%u rule=%s candidates=%u preferred=\"%s\" boot=\"%s\" fallback=%s\n",
              leaf, entries, images, prepared, omitted,
              arc_zip_content_rule_name(content->rule), content->candidate_count,
              escaped_preferred, escaped_boot, fallback ? "yes" : "no");
   else
      arc_log(RETRO_LOG_INFO,
              "Zip content: file=%s entries=%u images=%u prepared=%u omitted=%u rule=%s candidates=%u preferred=\"%s\" boot=none fallback=%s\n",
              leaf, entries, images, prepared, omitted,
              arc_zip_content_rule_name(content->rule), content->candidate_count,
              escaped_preferred, fallback ? "yes" : "no");
   if (!prepared) return;
   const size_t order_size = 18000u;
   char *order = malloc(order_size);
   if (!order) return;
   size_t used = (size_t)snprintf(order, order_size, "Zip content order:");
   for (unsigned i = 0u; i < prepared && used < order_size; ++i)
   {
      char member[513];
      arc_zip_escape_member(arc_media[i].zip_member, member);
      int written = snprintf(order + used, order_size - used, " %u=\"%s\"",
                             i + 1u, member);
      if (written < 0 || (size_t)written >= order_size - used)
      {
         used = order_size - 1u;
         break;
      }
      used += (size_t)written;
   }
   if (used < order_size - 1u)
      order[used++] = '\n';
   order[used] = '\0';
   arc_log(RETRO_LOG_INFO, "%s", order);
   free(order);
}

static bool arc_prepare_zip_content(const char *archive_path,
                                    const char *selected_member,
                                    bool explicit_member,
                                    ArcDiskWriteMode mode,
                                    char *error, size_t error_size)
{
   const char *leaf = arc_path_leaf(archive_path);
   if (explicit_member && !selected_member[0])
   {
      if (error && error_size)
         snprintf(error, error_size,
                  "Empty zip member is not a supported disc image in %.450s",
                  leaf);
      return false;
   }
   void *archive_data = NULL;
   size_t archive_size = 0u;
   bool too_large = false;
   if (!arc_io_read_all_limit(archive_path, &archive_data, &archive_size,
                              ARC_ZIP_CONTENT_ARCHIVE_MAX, &too_large))
   {
      if (error && error_size)
         snprintf(error, error_size, "%s",
                  too_large ? "Zip archive exceeds 32 MiB."
                            : "Unable to read the zip through the frontend VFS.");
      return false;
   }
   const ArcZipLimits limits = {ARC_ZIP_CONTENT_ARCHIVE_MAX, 1024u,
                                ARC_ZIP_CONTENT_MEMBER_MAX};
   ArcZipArchive archive;
   char reason[512] = {0};
   if (!arc_zip_index(archive_data, archive_size, &limits, &archive, reason,
                      sizeof(reason)))
   {
      if (error && error_size)
         snprintf(error, error_size, "Invalid zip archive %s: %s", leaf,
                  reason[0] ? reason : "invalid archive structure");
      free(archive_data);
      return false;
   }
   ArcZipContent content;
   if (!arc_zip_content_index(&archive, archive_path, selected_member, &content, reason,
                              sizeof(reason)))
   {
      if (error && error_size)
      {
         if (selected_member && selected_member[0] &&
             strstr(reason, "not a supported disc image"))
            snprintf(error, error_size,
                     "%s is not a supported disc image in %s",
                     selected_member, leaf);
         else if (strstr(reason, "more than 32 supported disc images"))
            snprintf(error, error_size,
                     "Zip %.450s contains more than 32 supported disc images",
                     leaf);
         else
            snprintf(error, error_size, "%s", reason);
      }
      arc_zip_log(leaf, archive.count, content.qualifying_count, 0u, 0u,
                  &content, "", "", false, selected_member);
      arc_zip_free(&archive);
      free(archive_data);
      return false;
   }
   if (!content.count)
   {
      if (error && error_size)
         snprintf(error, error_size,
                  "No supported disc image (adf, adl, jfd) in %s", leaf);
      arc_zip_log(leaf, archive.count, 0u, 0u, 0u, &content, "", "", false,
                  selected_member);
      arc_zip_free(&archive);
      free(archive_data);
      return false;
   }

   ArcDiskWriteMode prepare_mode = mode;
   if (mode == ARC_DISK_WRITES_ORIGINAL)
   {
      prepare_mode = ARC_DISK_WRITES_SHADOW;
      if (arc_log)
         arc_log(RETRO_LOG_INFO,
                 "Zip content: original writes redirected to shadow copies.\n");
      arc_message("Zip content is never written in place; writes go to the shadow copy.",
                  RETRO_LOG_WARN);
   }

   arc_media_count = 0u;
   unsigned omissions = 0u;
   unsigned notices = 0u;
   size_t logical_total = 0u;
   char first_reason[512] = {0};
   uint8_t *adffs_raw = NULL;
   size_t adffs_raw_size = 0u;
   char preferred_member[256];
   snprintf(preferred_member, sizeof(preferred_member), "%s",
            archive.entries[content.preferred_index].name);
   bool preferred_omitted = false;

   /* Peak ownership is deliberately sequential: the archive (<=32 MiB),
    * one extracted member (<=8 MiB), one decoded JFD copy for a zip member
    * (<=8 MiB), and one staging verification buffer (<=8 MiB). A plain
    * decoded JFDI file instead transfers its read buffer to the parser. The
    * archive never survives machine construction; only an eligible single
    * JFD's member buffer may survive as the bounded ADFFS carrier source. */
   for (unsigned image = 0u; image < content.count; ++image)
   {
      ArcZipEntry *zip_entry = &archive.entries[content.entries[image]];
      uint8_t *member = NULL;
      size_t member_size = 0u;
      reason[0] = '\0';
      const char *status = arc_zip_member_status_reason(zip_entry->status);
      if (status)
         snprintf(reason, sizeof(reason), "%s", status);
      else if (!arc_zip_extract(archive_data, archive_size, zip_entry, &member,
                                &member_size, reason, sizeof(reason)))
      {
         if (!reason[0]) snprintf(reason, sizeof(reason), "extraction-failed");
      }
      char source[ARC_MEDIA_PATH_CAPACITY];
      int source_length = snprintf(source, sizeof(source), "%s#%s",
                                   archive_path, zip_entry->name);
      ArcMediaEntry candidate;
      bool notice = false;
      char label[ARC_MEDIA_LABEL_CAPACITY];
      arc_media_stem(zip_entry->name, label);
      bool prepared = !reason[0] && source_length >= 0 &&
         (size_t)source_length < sizeof(source) &&
         arc_prepare_bytes(source, label, prepare_mode, false, &notice,
                           member, member_size, false, &candidate, reason,
                           sizeof(reason));
      if (!prepared)
      {
         if (!reason[0])
            snprintf(reason, sizeof(reason),
                     source_length < 0 || (size_t)source_length >= sizeof(source)
                        ? "content-path-too-long" : "media-preparation-failed");
         if (!first_reason[0])
            snprintf(first_reason, sizeof(first_reason), "%s", reason);
         ++omissions;
         if (!image) preferred_omitted = true;
         if (arc_log)
            arc_log(RETRO_LOG_WARN,
                    "Zip member omitted: member=%s reason=%s\n",
                    zip_entry->name, reason);
         free(member);
         continue;
      }
      candidate.zip_crc32 = zip_entry->crc32;
      candidate.zip_packed_size = zip_entry->packed_size;
      candidate.zip_size = zip_entry->size;
      snprintf(candidate.zip_member, sizeof(candidate.zip_member), "%s",
               zip_entry->name);
      if (candidate.info.declared_size >
          ARC_ZIP_CONTENT_LOGICAL_MAX - logical_total)
      {
         arc_discard_candidate(&candidate);
         free(member);
         free(adffs_raw);
         arc_zip_free(&archive);
         free(archive_data);
         arc_cleanup_failed_preparation();
         if (error && error_size)
            snprintf(error, error_size,
                     "Zip content exceeds 256 MiB of prepared logical media.");
         return false;
      }
      logical_total += candidate.info.declared_size;
      if (notice) ++notices;
      if (!arc_media_count && candidate.info.kind == ARC_MEDIA_JFD &&
          member_size <= ARC_ADFFS_CARRIER_CAPACITY)
      {
         adffs_raw = member;
         adffs_raw_size = member_size;
         member = NULL;
      }
      else if (arc_media_count)
      {
         free(adffs_raw);
         adffs_raw = NULL;
         adffs_raw_size = 0u;
      }
      arc_media[arc_media_count++] = candidate;
      free(member);
   }
   bool fallback = preferred_omitted;
   const char *boot_member = arc_media_count ? arc_media[0].zip_member : "";
   arc_zip_log(leaf, archive.count, content.qualifying_count, arc_media_count,
               omissions, &content, preferred_member, boot_member, fallback,
               selected_member);
   arc_zip_free(&archive);
   free(archive_data);
   if (!arc_media_count)
   {
      free(adffs_raw);
      if (error && error_size)
         snprintf(error, error_size, "No usable disc image in %s (%s)", leaf,
                  first_reason[0] ? first_reason : "media preparation failed");
      return false;
   }
   if (arc_media_count != 1u || arc_media[0].info.kind != ARC_MEDIA_JFD)
   {
      free(adffs_raw);
      adffs_raw = NULL;
      adffs_raw_size = 0u;
   }
   arc_zip_adffs_raw = adffs_raw;
   arc_zip_adffs_raw_size = adffs_raw_size;
   snprintf(arc_zip_adffs_member, sizeof(arc_zip_adffs_member), "%s",
            adffs_raw ? arc_media[0].zip_member : "");
   arc_content_digest = arc_media[0].digest;
   arc_zip_active = true;
   snprintf(arc_zip_path, sizeof(arc_zip_path), "%s", archive_path);
   arc_zip_summary(omissions, notices, fallback, preferred_member, boot_member);
   return true;
}

static bool arc_prepare_media_list(const char *content_path,
                                   ArcDiskWriteMode mode,
                                   char *error, size_t error_size)
{
   char archive_path[ARC_MEDIA_PATH_CAPACITY];
   char selected_member[256];
   if (arc_zip_content_path(content_path, archive_path, selected_member))
      return arc_prepare_zip_content(archive_path, selected_member,
                                     content_path[strlen(archive_path)] == '#',
                                     mode,
                                     error, error_size);
   const char *fragment = strchr(content_path, '#');
   const char *path_end = fragment ? fragment : content_path + strlen(content_path);
   const char *extension = NULL;
   for (const char *at = content_path; at < path_end; ++at)
      if (*at == '.') extension = at;
   if (extension)
   {
      char lowered[8] = {0};
      size_t length = (size_t)(path_end - extension);
      if (length < sizeof(lowered))
         for (size_t i = 0u; i < length; ++i)
            lowered[i] = (char)tolower((unsigned char)extension[i]);
      if (!strcmp(lowered, ".7z") || !strcmp(lowered, ".rar") ||
          !strcmp(lowered, ".tar") || !strcmp(lowered, ".gz"))
      {
         if (error && error_size)
            snprintf(error, error_size,
                     "Unsupported archive container; ArcDroid supports .zip files only.");
         return false;
      }
   }
   void *top_data = NULL;
   size_t top_size = 0u;
   if (!arc_io_read_all(content_path, &top_data, &top_size))
   {
      if (error && error_size) snprintf(error, error_size,
         "Unable to read the selected content through the frontend VFS.");
      return false;
   }
   ArcPlaylist playlist;
   bool is_playlist = arc_playlist_detect(content_path, top_data, top_size);
#if defined(_WIN32)
   const bool windows_host = true;
#else
   const bool windows_host = false;
#endif
   if (is_playlist)
   {
      unsigned error_line = 0u;
      if (!arc_playlist_parse(content_path, top_data, top_size, windows_host,
                              &playlist, &error_line, error, error_size))
      { free(top_data); return false; }
   }
   free(top_data);
   arc_media_count = is_playlist ? playlist.count : 1u;
   size_t total = 0u;
   for (unsigned i = 0u; i < arc_media_count; ++i)
   {
      const char *source = is_playlist ? playlist.entries[i].path : content_path;
      const char *label = is_playlist ? playlist.entries[i].label : NULL;
      if (!arc_prepare_source(source, label, mode, true, NULL, &arc_media[i],
                              error, error_size))
      {
         if (is_playlist && error && error_size)
         {
            char reason[512];
            snprintf(reason, sizeof(reason), "%s", error);
            snprintf(error, error_size, "playlist line %u: %s", playlist.entries[i].line, reason);
         }
         arc_cleanup_failed_preparation();
         return false;
      }
      bool duplicate = false;
      for (unsigned previous = 0u; previous < i; ++previous)
         if (!strcmp(arc_media[previous].digest.sha1, arc_media[i].digest.sha1))
            duplicate = true;
      if (!duplicate)
      {
         size_t logical = arc_media[i].info.declared_size;
         if (logical > 256u * 1024u * 1024u - total)
         {
            if (error && error_size) snprintf(error, error_size,
               "prepared logical media exceeds 256 MiB");
            arc_cleanup_failed_preparation();
            return false;
         }
         total += logical;
      }
   }
   if (!is_playlist)
   {
      arc_content_digest = arc_media[0].digest;
      arc_folder_set_active = arc_prepare_folder_siblings(
         content_path, windows_host, mode, arc_media[0].info.declared_size);
   }
   else
   {
      static const char domain[] = "arcdroid-playlist-v1";
      uint8_t identity[sizeof(domain) - 1u + 4u + ARC_PLAYLIST_MAX_ENTRIES * 20u];
      size_t at = 0u;
      memcpy(identity, domain, sizeof(domain) - 1u); at += sizeof(domain) - 1u;
      identity[at++] = (uint8_t)arc_media_count;
      identity[at++] = 0u;
      identity[at++] = 0u;
      identity[at++] = 0u;
      for (unsigned i = 0u; i < arc_media_count; ++i)
         for (unsigned byte = 0u; byte < 20u; ++byte)
            identity[at++] = (uint8_t)((arc_hex_nibble(arc_media[i].digest.sha1[byte * 2u]) << 4u) |
                                       arc_hex_nibble(arc_media[i].digest.sha1[byte * 2u + 1u]));
      arc_content_digest = arc_digest(identity, at);
   }
   return true;
}

static bool arc_reprepare_folder_media(ArcDiskWriteMode mode,
                                       unsigned *selected,
                                       char *error, size_t error_size)
{
   const unsigned old_count = arc_media_count;
   const bool selected_at_end = selected && *selected == old_count;
   char selected_source[ARC_MEDIA_PATH_CAPACITY] = {0};
   if (selected && *selected < old_count && arc_media[*selected].populated)
      snprintf(selected_source, sizeof(selected_source), "%s",
               arc_media[*selected].source);

   unsigned kept = 0u;
   unsigned omissions = 0u;
   unsigned notices = 0u;
   for (unsigned i = 0u; i < old_count; ++i)
   {
      if (!arc_media[i].populated)
      {
         if (kept != i)
         {
            arc_media[kept] = arc_media[i];
            memset(&arc_media[i], 0, sizeof(arc_media[i]));
         }
         ++kept;
         continue;
      }

      char source[ARC_MEDIA_PATH_CAPACITY];
      char label[ARC_MEDIA_LABEL_CAPACITY];
      snprintf(source, sizeof(source), "%s", arc_media[i].source);
      snprintf(label, sizeof(label), "%s", arc_media[i].label);
      ArcMediaEntry replacement;
      char reason[512] = {0};
      bool notice = false;
      if (!arc_prepare_source(source, label, mode, i == 0u, &notice,
                              &replacement, reason, sizeof(reason)))
      {
         if (i == 0u)
         {
            if (error && error_size)
               snprintf(error, error_size, "%s", reason[0] ? reason :
                        "unable to re-prepare the selected folder-set image");
            return false;
         }
         ++omissions;
         if (arc_log)
            arc_log(RETRO_LOG_WARN,
                    "Folder disc set sibling omitted: path=\"%s\" reason=%s phase=reset\n",
                    source, reason[0] ? reason : "media preparation failed");
         arc_jfd_free(&arc_media[i].jfd);
         memset(&arc_media[i], 0, sizeof(arc_media[i]));
         continue;
      }
      if (i && notice) ++notices;
      arc_jfd_free(&arc_media[i].jfd);
      if (kept != i)
      {
         arc_media[kept] = replacement;
         memset(&arc_media[i], 0, sizeof(arc_media[i]));
      }
      else
         arc_media[i] = replacement;
      ++kept;
   }
   for (unsigned i = kept; i < old_count; ++i)
      memset(&arc_media[i], 0, sizeof(arc_media[i]));
   arc_media_count = kept;

   if (selected)
   {
      if (selected_at_end)
         *selected = arc_media_count;
      else if (selected_source[0])
      {
         unsigned matched = arc_media_count;
         for (unsigned i = 0u; i < arc_media_count; ++i)
            if (arc_media[i].populated &&
                !strcmp(arc_media[i].source, selected_source))
            { matched = i; break; }
         *selected = matched < arc_media_count ? matched : 0u;
      }
      else if (*selected > arc_media_count)
         *selected = arc_media_count;
   }
   arc_folder_summary("reset", omissions, notices);
   return true;
}

static bool arc_reprepare_zip_media(ArcDiskWriteMode mode,
                                    unsigned *selected,
                                    char *error, size_t error_size)
{
   void *archive_data = NULL;
   size_t archive_size = 0u;
   bool too_large = false;
   if (!arc_io_read_all_limit(arc_zip_path, &archive_data, &archive_size,
                              ARC_ZIP_CONTENT_ARCHIVE_MAX, &too_large))
   {
      if (error && error_size)
         snprintf(error, error_size, "%s",
                  too_large ? "Zip archive exceeds 32 MiB during reset."
                            : "Unable to re-open the zip during reset.");
      return false;
   }
   const ArcZipLimits limits = {ARC_ZIP_CONTENT_ARCHIVE_MAX, 1024u,
                                ARC_ZIP_CONTENT_MEMBER_MAX};
   ArcZipArchive archive;
   char reason[512] = {0};
   if (!arc_zip_index(archive_data, archive_size, &limits, &archive, reason,
                      sizeof(reason)))
   {
      if (error && error_size)
         snprintf(error, error_size, "Zip reset verification failed: %s",
                  reason[0] ? reason : "invalid archive structure");
      free(archive_data);
      return false;
   }
   ArcDiskWriteMode prepare_mode = mode == ARC_DISK_WRITES_ORIGINAL ?
      ARC_DISK_WRITES_SHADOW : mode;
   if (mode == ARC_DISK_WRITES_ORIGINAL)
   {
      if (arc_log)
         arc_log(RETRO_LOG_INFO,
                 "Zip content: original writes redirected to shadow copies.\n");
      arc_message("Zip content is never written in place; writes go to the shadow copy.",
                  RETRO_LOG_WARN);
   }
   unsigned old_count = arc_media_count;
   bool selected_at_end = selected && *selected == old_count;
   unsigned old_selected = selected && *selected < old_count ?
      *selected : old_count;
   unsigned remapped_selected = 0u;
   bool selected_retained = false;
   unsigned kept = 0u;
   unsigned omissions = 0u;
   unsigned notices = 0u;
   uint8_t *adffs_raw = NULL;
   size_t adffs_raw_size = 0u;
   for (unsigned i = 0u; i < old_count; ++i)
   {
      ArcMediaEntry *old = &arc_media[i];
      if (!old->zip_member[0])
      {
         ArcMediaEntry replacement;
         bool notice = false;
         reason[0] = '\0';
         if (!arc_prepare_source(old->source, old->label, mode, i == 0u,
                                 &notice, &replacement, reason, sizeof(reason)))
         {
            if (i == 0u)
            {
               arc_zip_free(&archive);
               free(archive_data);
               free(adffs_raw);
               if (error && error_size)
                  snprintf(error, error_size, "%s", reason[0] ? reason :
                           "unable to re-prepare replacement media");
               return false;
            }
            ++omissions;
            arc_jfd_free(&old->jfd);
            memset(old, 0, sizeof(*old));
            continue;
         }
         if (notice) ++notices;
         free(adffs_raw);
         adffs_raw = NULL;
         adffs_raw_size = 0u;
         arc_jfd_free(&old->jfd);
         if (kept != i)
         {
            arc_media[kept] = replacement;
            memset(old, 0, sizeof(*old));
         }
         else
            *old = replacement;
         if (i == old_selected)
         {
            remapped_selected = kept;
            selected_retained = true;
         }
         ++kept;
         continue;
      }
      ArcZipEntry *zip_entry = NULL;
      for (unsigned z = 0u; z < archive.count; ++z)
         if (!strcmp(archive.entries[z].name, old->zip_member))
         { zip_entry = &archive.entries[z]; break; }
      uint8_t *member = NULL;
      size_t member_size = 0u;
      reason[0] = '\0';
      if (!zip_entry || !arc_zip_content_member(zip_entry))
         snprintf(reason, sizeof(reason), "member is missing or no longer supported");
      else if (zip_entry->crc32 != old->zip_crc32 ||
               zip_entry->packed_size != old->zip_packed_size ||
               zip_entry->size != old->zip_size)
         snprintf(reason, sizeof(reason), "archive member metadata changed");
      else
         (void)arc_zip_extract(archive_data, archive_size, zip_entry, &member,
                               &member_size, reason, sizeof(reason));
      ArcMediaEntry replacement;
      bool notice = false;
      if (!reason[0])
         (void)arc_prepare_bytes(old->source, old->label, prepare_mode, i == 0u,
                                 &notice, member, member_size, false, &replacement,
                                 reason, sizeof(reason));
      if (!reason[0] && strcmp(replacement.digest.sha1, old->digest.sha1))
         snprintf(reason, sizeof(reason), "archive member digest changed");
      if (reason[0])
      {
         free(member);
         if (i == 0u)
         {
            arc_zip_free(&archive);
            free(archive_data);
            free(adffs_raw);
            if (error && error_size)
               snprintf(error, error_size,
                        "Zip reset verification failed for %s: %s",
                        old->zip_member, reason);
            return false;
         }
         ++omissions;
         if (arc_log)
            arc_log(RETRO_LOG_WARN,
                    "Zip member omitted: member=%s reason=%s phase=reset\n",
                    old->zip_member, reason);
         arc_jfd_free(&old->jfd);
         memset(old, 0, sizeof(*old));
         continue;
      }
      replacement.zip_crc32 = old->zip_crc32;
      replacement.zip_packed_size = old->zip_packed_size;
      replacement.zip_size = old->zip_size;
      snprintf(replacement.zip_member, sizeof(replacement.zip_member), "%s",
               old->zip_member);
      if (notice) ++notices;
      if (!kept && replacement.info.kind == ARC_MEDIA_JFD &&
          member_size <= ARC_ADFFS_CARRIER_CAPACITY)
      {
         adffs_raw = member;
         adffs_raw_size = member_size;
         member = NULL;
      }
      else if (kept)
      {
         free(adffs_raw);
         adffs_raw = NULL;
         adffs_raw_size = 0u;
      }
      arc_jfd_free(&old->jfd);
      if (kept != i)
      {
         arc_media[kept] = replacement;
         memset(old, 0, sizeof(*old));
      }
      else
         *old = replacement;
      if (i == old_selected)
      {
         remapped_selected = kept;
         selected_retained = true;
      }
      ++kept;
      free(member);
   }
   for (unsigned i = kept; i < old_count; ++i)
      memset(&arc_media[i], 0, sizeof(arc_media[i]));
   arc_media_count = kept;
   if (selected)
   {
      if (selected_at_end)
         *selected = kept;
      else
         *selected = selected_retained ? remapped_selected : 0u;
   }
   if (kept != 1u || arc_media[0].info.kind != ARC_MEDIA_JFD)
   {
      free(adffs_raw);
      adffs_raw = NULL;
      adffs_raw_size = 0u;
   }
   arc_zip_adffs_raw_discard();
   arc_zip_adffs_raw = adffs_raw;
   arc_zip_adffs_raw_size = adffs_raw_size;
   snprintf(arc_zip_adffs_member, sizeof(arc_zip_adffs_member), "%s",
            adffs_raw ? arc_media[0].zip_member : "");
   arc_zip_free(&archive);
   free(archive_data);
   arc_zip_summary(omissions, notices, false, "", "");
   return true;
}

static ArcEngineMedia arc_engine_media(const ArcMediaEntry *entry)
{
   ArcEngineMedia media = {entry->staged,
      entry->info.kind == ARC_MEDIA_JFD ?
         (entry->jfd.overlay && entry->jfd.overlay->writable) :
         arc_plan.disk_writes != ARC_DISK_WRITES_OFF,
      entry->info.kind, entry->info.geometry,
      entry->info.kind == ARC_MEDIA_JFD ? &entry->jfd : NULL, NULL, 0u,
      entry->effective_extent};
   return media;
}

static void arc_entry_tail_presentation(ArcMediaEntry *entry, bool zero_tail)
{
   entry->zero_tail_presentation = entry->info.short_image && zero_tail;
   entry->effective_extent = entry->info.short_image && !zero_tail ?
      entry->info.available_size : ARC_MEDIA_EXTENT_UNRESTRICTED;
}

static void arc_jfd_log_refusal(unsigned image_number, unsigned track,
                                unsigned side, unsigned sector,
                                const char *reason)
{
   if (!image_number || image_number > arc_media_count || track >= 83u ||
       side > 1u || sector > 255u) return;
   ArcMediaEntry *entry = &arc_media[image_number - 1u];
   unsigned track_side = track * 2u + side;
   uint8_t mask = (uint8_t)(1u << (sector & 7u));
   uint8_t *seen = &entry->jfd_refusal_seen[track_side][sector >> 3u];
   if (image_number == 1u) arc_engine_launch_outcome_refused(0u);
   if (*seen & mask) return;
   *seen |= mask;
   if (arc_log)
      arc_log(RETRO_LOG_INFO,
         "JFD write refused: image=%u track=%u side=%u sector=%u reason=%s\n",
         image_number, track, side, sector, reason);
}

static void arc_adf_log_refusal(unsigned drive, unsigned track,
                                unsigned side, unsigned sector)
{
   if (drive != 0u || arc_image_index >= arc_media_count ||
       track >= 83u || side > 1u ||
       sector > 255u) return;
   ArcMediaEntry *entry = &arc_media[arc_image_index];
   if (entry->info.kind == ARC_MEDIA_JFD ||
       entry->info.kind == ARC_MEDIA_INVALID) return;
   uint8_t *seen = &entry->jfd_refusal_seen[track * 2u + side][sector >> 3u];
   uint8_t mask = (uint8_t)(1u << (sector & 7u));
   arc_engine_launch_outcome_refused(0u);
   if (*seen & mask) return;
   *seen |= mask;
   if (arc_log)
      arc_log(RETRO_LOG_INFO,
              "ADF write refused: image=%u track=%u side=%u sector=%u\n",
              arc_image_index + 1u, track, side, sector);
}

static void arc_jfd_log_map_unavailable(unsigned image_number)
{
   if (!image_number || image_number > arc_media_count) return;
   ArcMediaEntry *entry = &arc_media[image_number - 1u];
   if (entry->jfd_map_unavailable_shown) return;
   entry->jfd_map_unavailable_shown = true;
   if (arc_log)
      arc_log(RETRO_LOG_INFO,
              "JFD allocation map unavailable: image=%u; omitted reads retain record-not-found\n",
              image_number);
}

static bool arc_refresh_jfd(ArcMediaEntry *entry, ArcDiskWriteMode mode,
                            ArcJfdWritePolicy policy,
                            char *error, size_t error_size)
{
   if (entry->info.kind != ARC_MEDIA_JFD) return true;
   void *data = NULL;
   size_t size = 0u;
   if (!arc_io_read_all(entry->staged, &data, &size) ||
       strcmp(arc_digest(data, size).sha1, entry->digest.sha1))
   {
      free(data);
      if (error && error_size) snprintf(error, error_size, "Staged JFD digest mismatch.");
      return false;
   }
   ArcJfdImage parsed;
   if (!arc_jfd_parse_owned(data, size, &parsed, error, error_size)) return false;
   arc_jfd_free(&entry->jfd);
   entry->jfd = parsed;
   const char *reason = NULL;
   unsigned image_number = (unsigned)(entry - arc_media) + 1u;
   if (!arc_jfd_store_load(&entry->jfd, arc_save_path, policy,
                           mode != ARC_DISK_WRITES_OFF, false,
                           image_number, &reason))
   {
      reason = "overlay-unreadable";
      if (mode != ARC_DISK_WRITES_OFF &&
          arc_jfd_write_authorized(&entry->jfd, policy))
         arc_jfd_store_notice("Disc changes can no longer be saved this session.", true);
   }
   if (entry->jfd.overlay)
   {
      entry->jfd.overlay->reason = reason;
      entry->jfd.overlay->log_refusal = arc_jfd_log_refusal;
      entry->jfd.overlay->log_map_unavailable = arc_jfd_log_map_unavailable;
   }
   if (entry->jfd.overlay && entry->jfd.overlay->damaged &&
       !entry->jfd_damage_notice_shown)
   {
      arc_jfd_store_notice(
         "Disc writes are disabled because a saved disc-change file is damaged.", true);
      entry->jfd_damage_notice_shown = true;
   }
   else if (entry->jfd.overlay && mode == ARC_DISK_WRITES_OFF &&
            entry->jfd.overlay->generation && !entry->jfd_off_notice_shown)
   {
      arc_jfd_store_notice("Saved disc changes are hidden while Disk Writes is Off.", false);
      entry->jfd_off_notice_shown = true;
   }
   if (entry->jfd.overlay && entry->jfd.overlay->hidden_store &&
       !arc_jfd_tab_hidden_notice_shown)
   {
      arc_jfd_store_notice(
         "Saved disc changes are hidden while Write-protect Tab is As imaged.", false);
      arc_jfd_tab_hidden_notice_shown = true;
   }
   if (mode == ARC_DISK_WRITES_ORIGINAL &&
       arc_jfd_write_authorized(&entry->jfd, policy) &&
       !arc_original_jfd_notice_shown)
   {
      arc_message("JFD content is never written in place; disc changes use the save directory.",
                  RETRO_LOG_INFO);
      arc_original_jfd_notice_shown = true;
   }
   return true;
}

static const char *arc_jfd_file_data_class(const ArcJfdImage *image,
                                            ArcDiskWriteMode mode,
                                            ArcJfdWritePolicy policy)
{
   const ArcJfdOverlay *overlay = image->overlay;
   /* Only stores inspected by the loader can contribute visible records or
    * be marked damaged; ineligible images keep a coherent base-only view. */
   if (overlay && overlay->guard_violated && overlay->visible)
      return "guard-violated";
   if (overlay && overlay->damaged) return "damaged-store";
   if (overlay && overlay->writable) return "writable";
   if (mode != ARC_DISK_WRITES_OFF && arc_jfd_write_authorized(image, policy) &&
       overlay && overlay->layout_supported) return "store-unavailable";
   if (mode == ARC_DISK_WRITES_OFF) return "disk-writes-off";
   if (!arc_jfd_write_authorized(image, policy))
      return arc_jfd_header_write_reason(image, policy);
   return "layout-unsupported";
}

static void arc_finish_jfd_preparation(ArcMediaEntry *entry, bool native,
                                        ArcJfdWritePolicy policy)
{
   if (!entry || entry->info.kind != ARC_MEDIA_JFD) return;
   unsigned image_number = (unsigned)(entry - arc_media) + 1u;
   ArcJfdOverlay *overlay = entry->jfd.overlay;
   /* C-11c derives formatted omissions from the committed FileCore map.
    * Preparation also starts a new mounted lifetime for probe marks. */
   if (overlay)
   {
      overlay->map_enabled = native && overlay->writable;
      arc_jfd_map_prepare(&entry->jfd);
   }
   arc_jfd_prepare_identification(&entry->jfd, native);
#if defined(ARCDROID_TEST_FAULTS)
   /* Exercise the class boundary that the normal WP loader short-circuits. */
   if (arc_test_jfd_wp_layout_supported && overlay &&
       !arc_jfd_write_authorized(&entry->jfd, policy) && entry->jfd.extended &&
       (entry->jfd.flags & 3u) == 3u)
      overlay->layout_supported = true;
#endif
   const char *image_class = arc_jfd_file_data_class(&entry->jfd,
                                                     arc_plan.disk_writes,
                                                     policy);
   arc_jfd_file_data_prepare(&entry->jfd, native,
      arc_core_options.jfd_omitted_strict, image_class);
   if (native && arc_log)
   {
      const ArcJfdFileData *p = &entry->jfd.file_data;
      const char *format = p->format == 'D' ? "D" :
         p->format == 'E' ? "E" : "other";
      arc_log(RETRO_LOG_INFO,
         "JFD file-data: image=%u policy=1 class=%s format=%s copies=%u/%u presented=%u skipped-sides=%u incomplete-dirs=%u reason=%s\n",
         image_number, image_class, format,
         p->valid_copies, p->agreeing_copies, p->presented,
         p->skipped_sides, p->incomplete_dirs, p->reason);
   }
   if (!arc_log) return;
   unsigned quarantined = 0u, first = 0u;
   for (unsigned t = 0u; t < entry->jfd.track_count; ++t)
      if (entry->jfd.tracks[t].quarantine_reason)
      {
         if (!quarantined) first = t;
         ++quarantined;
      }
   if (quarantined)
      arc_log(RETRO_LOG_WARN,
         "JFD native quarantine: image=%u sides=%u first=%u:%s more=%u\n",
         image_number, quarantined, first,
         entry->jfd.tracks[first].quarantine_reason, quarantined - 1u);
   char captured[40] = {0}, presented[40] = {0};
   size_t c_at = 0u, p_at = 0u;
   if (entry->jfd.track_count && !entry->jfd.tracks[0].quarantine_reason)
   {
      const ArcJfdTrack *track = &entry->jfd.tracks[0];
      for (unsigned i = 0u; i < track->sector_count && c_at + 4u < sizeof(captured); ++i)
      {
         unsigned id = entry->jfd.sectors[track->first_sector + i].id;
         int written = snprintf(captured + c_at, sizeof(captured) - c_at,
                                "%s%u", c_at ? "," : "", id);
         if (written < 0 || (size_t)written >= sizeof(captured) - c_at) break;
         c_at += (size_t)written;
      }
   }
   for (unsigned id = 0u; id < entry->jfd.identification_schedule; ++id)
      if (entry->jfd.identification_mask & (1u << id))
      {
         int written = snprintf(presented + p_at, sizeof(presented) - p_at,
                                "%s%u", p_at ? "," : "", id);
         if (written < 0 || (size_t)written >= sizeof(presented) - p_at) break;
         p_at += (size_t)written;
      }
   arc_log(RETRO_LOG_INFO,
      "JFD identification: image=%u native=%s format=%s schedule=%u timing=%s captured=%s presented=%s basis=%s maps=%u reason=%s\n",
      image_number, native ? "yes" : "no",
      entry->jfd.identification_format == 'D' ? "D" :
      entry->jfd.identification_format == 'E' ? "E" :
      entry->jfd.identification_format == 'F' ? "F" : "none",
      entry->jfd.identification_schedule,
      native && entry->jfd.track_count && entry->jfd.tracks[0].sector_count &&
         !entry->jfd.tracks[0].quarantine_reason ?
         entry->jfd.tracks[0].timed ? "timed" : "untimed" : "none",
      captured[0] ? captured : "none", presented[0] ? presented : "none",
      entry->jfd.identification_basis,
      entry->jfd.identification_maps,
      entry->jfd.identification_reason);
   if (policy == ARC_JFD_WRITE_TAB_OFF)
   {
      const char *write_reason = native ?
         overlay && overlay->reason ? overlay->reason : "overlay-unreadable" :
         "not-native";
      arc_log(RETRO_LOG_INFO,
         "JFD writes: image=%u tab=off header=%s effective=%s state=%s reason=%s guard=%s unavailable=%u tail=%u generation=%llu records=%u\n",
         image_number,
         arc_jfd_write_authorized(&entry->jfd, ARC_JFD_WRITE_AS_IMAGED) ? "yes" : "no",
         arc_jfd_write_authorized(&entry->jfd, policy) ? "yes" : "no",
         native && overlay && overlay->writable ? "writable" : "protected",
         write_reason, overlay && overlay->guarded ? "old-map" : "none",
         overlay ? overlay->unavailable_slots : 0u,
         overlay ? overlay->guard_tail_units : 0u,
         (unsigned long long)(overlay ? overlay->generation : 0u),
         overlay ? overlay->records : 0u);
   }
   else if (native && overlay && overlay->guarded && overlay->writable)
      arc_log(RETRO_LOG_INFO,
         "JFD writes: image=%u state=writable reason=rw-flag guard=old-map unavailable=%u tail=%u generation=%llu records=%u; accepted map writes never increase unavailable allocation\n",
         image_number, overlay->unavailable_slots, overlay->guard_tail_units,
         (unsigned long long)overlay->generation, overlay->records);
   else arc_log(RETRO_LOG_INFO,
      "JFD writes: image=%u state=%s reason=%s generation=%llu records=%u\n",
      image_number,
      native && overlay && overlay->writable ? "writable" : "protected",
      native ? overlay && overlay->reason ? overlay->reason : "overlay-unreadable" :
               "not-native",
      (unsigned long long)(overlay ? overlay->generation : 0u),
      overlay ? overlay->records : 0u);
}

/* Resolve against the current writable raw image or a single temporary JFD
 * view. A second non-inserted JFD is never retained between iterations. */
static ArcDiscObject arc_disc_image_object(unsigned index, const char *path,
                                            ArcGuestName *name)
{
   if (index >= arc_media_count || !arc_media[index].populated)
      return ARC_DISC_UNKNOWN;
   ArcMediaEntry *entry = &arc_media[index];
   void *bytes = NULL;
   size_t size = 0u;
   if (!arc_io_read_all(entry->staged, &bytes, &size)) return ARC_DISC_UNKNOWN;
   ArcDiscObject object = ARC_DISC_UNKNOWN;
   if (entry->info.kind == ARC_MEDIA_JFD)
   {
      ArcJfdImage temporary = {0};
      ArcJfdImage *view = &entry->jfd;
      if (index != arc_image_index)
      {
         char error[128] = {0};
         const char *reason = NULL;
         if (strcmp(arc_digest(bytes, size).sha1, entry->digest.sha1) ||
             !arc_jfd_parse(bytes, size, &temporary, error, sizeof(error)))
            goto done;
#if defined(ARCDROID_TEST_FAULTS)
         ++arc_disc_temporary_jfd_live;
         ++arc_disc_temporary_jfd_decodes;
         if (arc_disc_temporary_jfd_live > arc_disc_temporary_jfd_peak)
            arc_disc_temporary_jfd_peak = arc_disc_temporary_jfd_live;
#endif
         view = &temporary;
         if (!arc_jfd_store_load(view, arc_save_path,
                 arc_core_options.jfd_write_policy,
                 arc_plan.disk_writes != ARC_DISK_WRITES_OFF,
                 false, index + 1u, &reason))
            goto jfd_done;
         if (view->overlay)
         {
            view->overlay->map_enabled = view->overlay->writable;
            arc_jfd_map_prepare(view);
         }
         arc_jfd_prepare_identification(view, true);
         arc_jfd_file_data_prepare(view, true,
             arc_core_options.jfd_omitted_strict,
             arc_jfd_file_data_class(view, arc_plan.disk_writes,
                                       arc_core_options.jfd_write_policy));
      }
      if (view->overlay && !view->overlay->damaged && !view->overlay->failed)
         object = path ? arc_disc_resolve(NULL, 0u, NULL, view, path, name) :
            arc_disc_read_name(NULL, 0u, NULL, view, name) ? ARC_DISC_DIR : ARC_DISC_UNKNOWN;
jfd_done:
#if defined(ARCDROID_TEST_FAULTS)
      if (index != arc_image_index && temporary.decoded)
         --arc_disc_temporary_jfd_live;
#endif
      arc_jfd_free(&temporary);
   }
   else
   {
      size_t visible = size;
      if (entry->effective_extent && visible > entry->effective_extent)
         visible = entry->effective_extent;
      object = path ? arc_disc_resolve(bytes, visible, &entry->info, NULL, path, name) :
         arc_disc_read_name(bytes, visible, &entry->info, NULL, name) ? ARC_DISC_DIR : ARC_DISC_UNKNOWN;
   }
done:
   free(bytes);
   return object;
}

static bool arc_disc_ascii_equal(const char *requested,
                                  const ArcGuestName *available)
{
   size_t length = strlen(requested);
   if (length != available->length) return false;
   for (size_t i = 0u; i < length; ++i)
   {
      unsigned a = (unsigned char)requested[i], b = available->bytes[i];
      if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
      if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
      if (a != b) return false;
   }
   return true;
}
static bool arc_disc_name(unsigned index, ArcGuestName *name)
{
   if (index >= arc_media_count || !arc_media[index].populated) return false;
   if (!arc_disc_names_valid[index])
   {
      if (arc_disc_image_object(index, NULL, &arc_disc_names[index]) ==
          ARC_DISC_UNKNOWN) return false;
      arc_disc_names_valid[index] = true;
   }
   *name = arc_disc_names[index];
   return true;
}
static unsigned arc_disc_resolve_candidate(void *opaque, const char *path,
                                            ArcDiscOperation operation)
{
   (void)opaque;
   if (!path || arc_media_count < 2u) return UINT_MAX;
   unsigned candidate = UINT_MAX;
   ArcDiscCacheEntry *entry = NULL, *oldest = NULL;
   for (unsigned j = 0u; j < ARC_DISC_CACHE_CAPACITY; ++j)
   {
      ArcDiscCacheEntry *slot = &arc_disc_paths[j];
      if (slot->valid && !strcmp(slot->path, path))
      { entry = slot; break; }
      if (!oldest || !slot->valid ||
          (oldest->valid && slot->last_used < oldest->last_used))
         oldest = slot;
   }
   if (!entry)
   {
      entry = oldest;
      memset(entry, 0, sizeof(*entry));
      strcpy(entry->path, path);
      entry->valid = true;
   }
   entry->last_used = ++arc_disc_cache_clock;
   for (unsigned i = 0u; i < arc_media_count; ++i)
   {
      if (i == arc_image_index || !arc_media[i].populated) continue;
      if (!entry->image_valid[i])
      {
         ArcGuestName name = {0};
         entry->objects[i] = arc_disc_image_object(i, path, &name);
         entry->image_valid[i] = true;
         if (entry->objects[i] != ARC_DISC_UNKNOWN && !arc_disc_names_valid[i])
         { arc_disc_names[i] = name; arc_disc_names_valid[i] = true; }
      }
      ArcDiscObject object = entry->objects[i];
      if (object == ARC_DISC_UNKNOWN) return UINT_MAX;
      bool compatible = object == ARC_DISC_FILE ||
         (object == ARC_DISC_DIR && (operation == ARC_DISC_CATALOGUE ||
                                     operation == ARC_DISC_OPEN_DIR));
      if (compatible)
      {
         if (candidate != UINT_MAX) return UINT_MAX;
         candidate = i;
      }
   }
   arc_disc_watch_generation = arc_disc_generation;
   return candidate;
}
#if defined(ARCDROID_TEST_FAULTS)
int arc_libretro_test_disc_candidate(const char *path, unsigned operation)
{
   if (operation > ARC_DISC_OPEN_DIR) return -1;
   unsigned index = arc_disc_resolve_candidate(NULL, path,
                                               (ArcDiscOperation)operation);
   return index == UINT_MAX ? -1 : (int)index;
}
unsigned arc_libretro_test_disc_temporary_jfd_peak(void)
{ return arc_disc_temporary_jfd_peak; }
unsigned arc_libretro_test_disc_temporary_jfd_live(void)
{ return arc_disc_temporary_jfd_live; }
unsigned arc_libretro_test_disc_temporary_jfd_decodes(void)
{ return arc_disc_temporary_jfd_decodes; }
bool arc_libretro_test_disc_jfd_root_name(unsigned index, uint8_t first)
{
   if (index >= arc_media_count || index == arc_image_index ||
       arc_media[index].info.kind != ARC_MEDIA_JFD) return false;
   ArcMediaEntry *entry = &arc_media[index];
   char error[128] = {0};
   if (!arc_refresh_jfd(entry, arc_plan.disk_writes,
                        arc_core_options.jfd_write_policy, error, sizeof(error)))
      return false;
   ArcJfdImage *image = &entry->jfd;
   bool changed = false;
   if (image->overlay && image->overlay->writable)
      for (size_t i = 0u; i < image->sector_count; ++i)
      {
         ArcJfdSector *sector = &image->sectors[i];
         if (sector->size < 1024u ||
             sector->data_offset > image->decoded_size ||
             sector->size > image->decoded_size - sector->data_offset ||
             !image->overlay->eligible[i]) continue;
         const uint8_t *base = image->overlay->data && image->overlay->data[i] ?
            image->overlay->data[i] : image->decoded + sector->data_offset;
         if (memcmp(base + 1u, "Hugo", 4u) ||
             memcmp(base + 6u, "Demo", 4u)) continue;
         uint8_t *copy = malloc(sector->size);
         if (!copy) break;
         memcpy(copy, base, sector->size);
         copy[5u] = first;
         changed = image->overlay->commit(image, i, copy);
         free(copy);
         break;
      }
   arc_jfd_free(image);
   return changed;
}
#endif

typedef struct {
   char (*names)[256];
   unsigned count;
   unsigned capacity;
   bool failed;
} ArcZipNames;
static bool arc_collect_zip(const char *name, bool regular, void *opaque) {
   ArcZipNames *z = opaque;
   size_t n = name ? strlen(name) : 0u;
   if (!regular || n <= 4u || name[n - 4u] != '.' ||
       tolower((unsigned char)name[n - 3u]) != 'z' ||
       tolower((unsigned char)name[n - 2u]) != 'i' ||
       tolower((unsigned char)name[n - 1u]) != 'p')
      return true;
   if (n >= sizeof(z->names[0]) || z->count == 1024u) {
      z->failed = true;
      return false;
   }
   if (z->count == z->capacity) {
      unsigned capacity = z->capacity ? z->capacity * 2u : 8u;
      char(*names)[256] = realloc(z->names, capacity * sizeof(z->names[0]));
      if (!names) {
         z->failed = true;
         return false;
      }
      z->names = names;
      z->capacity = capacity;
   }
   strcpy(z->names[z->count++], name);
   return true;
}
static int arc_zip_name_compare(const void *a, const void *b) {
   return strcmp(a, b);
}
static void arc_adffs_discard(void) {
   free(arc_adffs_carrier);
   free(arc_adffs_support);
   arc_adffs_carrier = NULL;
   arc_adffs_support = NULL;
   arc_adffs_active = false;
}
static void arc_zip_adffs_raw_discard(void) {
   free(arc_zip_adffs_raw);
   arc_zip_adffs_raw = NULL;
   arc_zip_adffs_raw_size = 0u;
   arc_zip_adffs_member[0] = '\0';
}
static void arc_jfd_boot_log(const char *reason) {
   if (arc_log)
      arc_log(RETRO_LOG_INFO, "JFD boot: path=native reason=%s\n", reason);
}
static void arc_jfd_boot_notice(const char *reason, const ArcJfdImage *jfd) {
   char text[320];
   if (!strcmp(reason, "no-package"))
      snprintf(text, sizeof(text),
               jfd && jfd->obey_length
                  ? "This image carries a JASPP boot script; place the ADFFS "
                    "zip in <system>/arcdroid/adffs to use it"
                  : "This JFD may boot better with ADFFS; place the ADFFS zip "
                    "in <system>/arcdroid/adffs");
   else
      snprintf(text, sizeof(text), "JFD ADFFS boot unavailable: %s", reason);
   arc_message(text, RETRO_LOG_WARN);
}
static void arc_reason_token(const char *input, char output[96]) {
   size_t at = 0;
   for (size_t i = 0; input && input[i] && at + 1u < 96u; ++i) {
      unsigned char c = input[i];
      if (isalnum(c))
         output[at++] = (char)tolower(c);
      else if (!at || output[at - 1] != '-')
         output[at++] = '-';
   }
   while (at && output[at - 1] == '-')
      --at;
   output[at] = 0;
}
static void arc_hd_store_log(void *opaque, const char *line)
{
   (void)opaque;
   bool failed = !strncmp(line, "store-write-failed=", 19u) ||
                 !strcmp(line, "write-commit-failed");
   if (arc_log) arc_log(failed || strstr(line, "=failed") ?
      RETRO_LOG_WARN : RETRO_LOG_INFO, "Hard disc: %s\n", line);
   if (failed) arc_engine_launch_outcome_refused(4u);
   if (failed && !arc_hd_write_failure_noticed) {
      arc_hd_write_failure_noticed = true;
      snprintf(arc_hd_reason, sizeof(arc_hd_reason), "read-only:%s",
         !strcmp(line, "store-write-failed=journal-cap") ? "journal-cap" : "storage-failure");
      arc_message(!strcmp(line, "store-write-failed=journal-cap") ?
         "Hard disc saves are no longer possible this session: journal full" :
         "Hard disc saves are no longer possible this session: storage failure",
         RETRO_LOG_WARN);
   }
}

static void arc_hd_reconcile(void)
{
   arc_install_store_close(arc_hd_store);
   arc_hd_store = NULL;
   arc_hd_option_auto = arc_core_options.hard_disc_auto;
   arc_hd_damaged = false;
   arc_hd_write_failure_noticed = false;
   const ArcMachineDescriptor *machine = arc_machine_descriptor(arc_session_machine);
   const char *reason = "not-attachable";
   char unavailable_reason[sizeof(arc_hd_reason)];
   if (arc_adffs_active) reason = "adffs-session";
   else if (!machine || !machine->ide_capable) reason = "not-attachable";
   else if (!arc_hd_option_auto) reason = "option-off";
   else {
      bool writable = arc_plan.disk_writes != ARC_DISK_WRITES_OFF;
      char error[256] = {0};
      arc_hd_store = arc_hd_store_open(arc_save_path, arc_plan.content_id,
         writable, arc_hd_store_log, NULL, error, sizeof(error));
      if (arc_hd_store &&
          arc_install_store_state(arc_hd_store) == ARC_INSTALL_STORE_READY) {
         reason = arc_install_store_writable(arc_hd_store) ? "attached" :
                  (writable ? "read-only:write-probe" : "disk-writes-off");
         if (!arc_install_store_writable(arc_hd_store) &&
             !arc_hd_readonly_noticed) {
            arc_hd_readonly_noticed = true;
            arc_message("Hard disc is read-only; saves cannot be changed in this session.",
                        RETRO_LOG_WARN);
         }
      } else if (arc_hd_store &&
                 arc_install_store_state(arc_hd_store) == ARC_INSTALL_STORE_DAMAGED) {
         reason = "damaged";
         arc_hd_damaged = true;
         char message[4096];
         snprintf(message, sizeof(message),
            "Saved hard disc is damaged; preserve %s/arcdroid/hd/%s and restore a backup.",
            arc_save_path, arc_plan.content_id);
         arc_message(message, RETRO_LOG_ERROR);
      } else if (!writable) reason = "disk-writes-off";
      else {
         snprintf(unavailable_reason, sizeof(unavailable_reason),
                  "unavailable:%s", error[0] ? error : "storage-failure");
         reason = unavailable_reason;
         if (!arc_hd_failure_noticed) {
            arc_hd_failure_noticed = true;
            char message[320];
            snprintf(message, sizeof(message), "Hard disc unavailable: %s",
                     error[0] ? error : "storage failure");
            arc_message(message, RETRO_LOG_WARN);
         }
      }
      if (!arc_hd_store ||
          arc_install_store_state(arc_hd_store) != ARC_INSTALL_STORE_READY) {
         arc_install_store_close(arc_hd_store);
         arc_hd_store = NULL;
      }
   }
   snprintf(arc_hd_reason, sizeof(arc_hd_reason), "%s", reason);
}
#if defined(ARCDROID_TEST_FAULTS)
bool arc_libretro_test_hd_exhaust_journal(void)
{
   if (!arc_hd_store ||
       arc_install_store_state(arc_hd_store) != ARC_INSTALL_STORE_READY ||
       !arc_install_store_writable(arc_hd_store)) return false;
   uint8_t *sectors = malloc(256u * ARC_INSTALL_HD_SECTOR);
   bool good = sectors && arc_install_store_read(arc_hd_store, 0u, 256u,
                                                  sectors);
   for (unsigned i = 0u; good && i < 80u &&
        arc_install_store_writable(arc_hd_store); ++i)
      (void)arc_install_store_write(arc_hd_store, 0u, 256u, sectors);
   free(sectors);
   return good && !arc_install_store_writable(arc_hd_store) &&
      arc_hd_write_failure_noticed;
}
bool arc_libretro_test_hd_observation_write(unsigned mode)
{
   if (!arc_hd_store ||
       arc_install_store_state(arc_hd_store) != ARC_INSTALL_STORE_READY ||
       !arc_install_store_writable(arc_hd_store)) return false;
   uint8_t sector[ARC_INSTALL_HD_SECTOR], changed[ARC_INSTALL_HD_SECTOR];
   if (!arc_install_store_read(arc_hd_store, 15456u, 1u, sector)) return false;
   if (mode == 0u)
      return arc_install_store_write(arc_hd_store, 15456u, 1u, sector);
   if (mode == 1u) {
      memcpy(changed, sector, sizeof(changed));
      changed[0] ^= 0x40u;
      return arc_install_store_write(arc_hd_store, 15456u, 1u, changed) &&
         arc_install_store_write(arc_hd_store, 15456u, 1u, sector);
   }
   if (mode == 2u)
      return !arc_install_store_write(arc_hd_store, ARC_INSTALL_HD_SECTORS,
                                      1u, sector);
   if (mode == 3u) {
      arc_install_store_failed(arc_hd_store, "store-write-failed=io");
      return !arc_install_store_writable(arc_hd_store);
   }
   if (mode == 4u) {
      uint8_t *sectors = malloc(256u * ARC_INSTALL_HD_SECTOR);
      if (!sectors || !arc_install_store_read(arc_hd_store, 0u, 256u, sectors)) {
         free(sectors);
         return false;
      }
      bool good = true;
      for (unsigned i = 0u; i < 9u && good; ++i)
         good = arc_install_store_write(arc_hd_store, 0u, 256u, sectors);
      free(sectors);
      return good;
   }
   return false;
}
#endif
static bool arc_prepare_adffs(bool notify, ArcJfdWritePolicy policy) {
   arc_adffs_discard();
   ArcMediaEntry *entry = arc_media_count ? &arc_media[0] : NULL;
   const ArcJfdImage *jfd = entry ? &entry->jfd : NULL;
   if (arc_hd_native_route(&arc_core_options, arc_session_machine)) {
      arc_jfd_boot_log("hard-disc");
      return false;
   }
   const char *reason = NULL;
   if (!entry || entry->info.kind != ARC_MEDIA_JFD)
      reason = "not-jfd";
   else if (arc_media_count != 1u || arc_folder_set_active)
      reason = "multi-image";
   else if (jfd->disc_number != 1u || jfd->disc_count != 1u)
      reason = "multi-disc";
   else if (arc_core_options.jfd_boot == ARC_JFD_BOOT_NATIVE)
      reason = "option";
   else if (arc_core_options.launch != ARC_LAUNCH_AUTO)
      reason = "launch-option";
   else if (arc_plan.os != ARC_OS_RISCOS_311
#if defined(ARCDROID_TEST_FAULTS)
            && !(arc_plan.os == ARC_OS_TEST && arc_test_adffs_accept_test_os)
#endif
   )
      reason = "os";
   if (!reason && !arc_folder_set_active &&
       arc_media_count == 1u && jfd->disc_number == 1u &&
       jfd->disc_count == 1u &&
       arc_core_options.jfd_boot == ARC_JFD_BOOT_AUTO &&
       arc_jfd_write_authorized(jfd, policy) &&
       jfd->overlay && jfd->overlay->layout_supported &&
       (!(jfd->flags & 0x7f0000u) || (jfd->flags & (1u << 17)))) {
      arc_jfd_boot_log("native-writable");
      return false;
   }
   if (reason) {
      arc_jfd_boot_log(reason);
      return false;
   }

   char dir[4096];
   int dir_length =
      snprintf(dir, sizeof(dir), "%s/arcdroid/adffs", arc_system_path);
   if (dir_length < 0 || (size_t)dir_length >= sizeof(dir)) {
      arc_jfd_boot_log("no-package");
      return false;
   }
   ArcZipNames names = {0};
   (void)arc_io_list_directory(dir, arc_collect_zip, &names);
   if (names.count > 1u)
      qsort(names.names, names.count, sizeof(names.names[0]),
            arc_zip_name_compare);
   ArcAdffsPackage *package = calloc(1u, sizeof(*package));
   if (!package) {
      free(names.names);
      arc_jfd_boot_log("invalid-package:out-of-memory");
      if (notify)
         arc_jfd_boot_notice("invalid-package:out-of-memory", jfd);
      return false;
   }
   char parse_error[160] = "";
   char chosen[256] = "";
   bool valid = false;
   if (names.failed)
      snprintf(parse_error, sizeof(parse_error),
               "package directory exceeds limits");
   for (unsigned i = 0; i < names.count && !valid && !names.failed; ++i) {
      void *bytes = NULL;
      size_t size = 0;
      size_t path_capacity = strlen(dir) + 1u + strlen(names.names[i]) + 1u;
      char *path = malloc(path_capacity);
      if (!path) {
         snprintf(parse_error, sizeof(parse_error), "out of memory");
         break;
      }
      int path_length =
         snprintf(path, path_capacity, "%s/%s", dir, names.names[i]);
      if (path_length < 0 || (size_t)path_length >= path_capacity ||
          !arc_io_read_all(path, &bytes, &size)) {
         free(path);
         continue;
      }
      free(path);
      if (arc_adffs_package_parse(bytes, size, package, parse_error,
                                  sizeof(parse_error))) {
         valid = true;
         strcpy(chosen, names.names[i]);
      }
      free(bytes);
   }
   bool found_package = names.count != 0u || names.failed;
   free(names.names);
   if (!valid) {
      if (found_package) {
         char why[96];
         arc_reason_token(parse_error[0] ? parse_error : "invalid-archive",
                          why);
         char full[128];
         snprintf(full, sizeof(full), "invalid-package:%s", why);
         arc_jfd_boot_log(full);
         if (notify)
            arc_jfd_boot_notice(full, jfd);
      } else {
         arc_jfd_boot_log("no-package");
         if (notify && (jfd->obey_length || jfd->game_id))
            arc_jfd_boot_notice("no-package", jfd);
      }
      free(package);
      return false;
   }
   if (arc_log)
      arc_log(RETRO_LOG_INFO, "ADFFS package: file=%s\n", chosen);
   if (!package->verified &&
       arc_core_options.adffs_package == ARC_ADFFS_VERIFIED_ONLY) {
      arc_adffs_package_free(package);
      free(package);
      arc_jfd_boot_log("untested-package");
      if (notify)
         arc_jfd_boot_notice("untested-package", jfd);
      return false;
   }
   uint8_t *builtin = NULL;
   size_t builtin_size = 0;
   parse_error[0] = 0;
   bool have_builtin =
      jfd->game_id && arc_adffs_builtin_script(
                         package, jfd->game_id, jfd->disc_number, &builtin,
                         &builtin_size, parse_error, sizeof(parse_error));
   if (parse_error[0] && !have_builtin) {
      char why[96];
      arc_reason_token(parse_error, why);
      char full[128];
      snprintf(full, sizeof(full), "invalid-package:%s", why);
      arc_adffs_package_free(package);
      free(package);
      arc_jfd_boot_log(full);
      if (notify)
         arc_jfd_boot_notice(full, jfd);
      return false;
   }
   const uint8_t *script =
      have_builtin ? builtin
                   : (jfd->obey_length
                         ? jfd->decoded + jfd->decoded_size - jfd->obey_length
                         : NULL);
   size_t script_size = have_builtin ? builtin_size : jfd->obey_length;
   if (!script) {
      free(builtin);
      arc_adffs_package_free(package);
      free(package);
      arc_jfd_boot_log("no-script");
      return false;
   }
   char missing[128];
   ArcAdffsSelection selection;
   if (!arc_adffs_select(package, script, script_size,
                         &selection, missing)) {
      char full[192];
      snprintf(full, sizeof(full), "missing-resource:%s", missing);
      free(builtin);
      arc_adffs_package_free(package);
      free(package);
      arc_jfd_boot_log(full);
      if (notify)
         arc_jfd_boot_notice(full, jfd);
      return false;
   }
   void *raw = NULL;
   size_t raw_size = 0;
   size_t declared_size = 0;
   bool zip_member = entry->zip_member[0] != '\0';
   bool carrier_too_large = zip_member ?
      entry->zip_size > ARC_ADFFS_CARRIER_CAPACITY :
      (arc_io_file_size(entry->source, &declared_size) &&
       declared_size > ARC_ADFFS_CARRIER_CAPACITY);
   if (carrier_too_large) {
      free(builtin);
      arc_adffs_package_free(package);
      free(package);
      arc_jfd_boot_log("too-large");
      if (notify)
         arc_jfd_boot_notice("too-large", jfd);
      return false;
   }
   bool content_read = true;
   bool raw_owned = false;
   if (zip_member) {
      if (arc_zip_adffs_raw &&
          !strcmp(arc_zip_adffs_member, entry->zip_member)) {
         raw = arc_zip_adffs_raw;
         raw_size = arc_zip_adffs_raw_size;
      } else {
         content_read = false;
      }
   } else {
#if defined(ARCDROID_TEST_FAULTS)
      if (arc_test_fail_adffs_content_read) {
         arc_test_fail_adffs_content_read = false;
         content_read = false;
      } else
#endif
         content_read = arc_io_read_all(entry->source, &raw, &raw_size);
      raw_owned = content_read;
   }
   if (!content_read) {
      free(builtin);
      arc_adffs_package_free(package);
      free(package);
      arc_jfd_boot_log("content-unreadable");
      return false;
   }
   if (raw_size > ARC_ADFFS_CARRIER_CAPACITY) {
      if (raw_owned) free(raw);
      free(builtin);
      arc_adffs_package_free(package);
      free(package);
      arc_jfd_boot_log("too-large");
      if (notify)
         arc_jfd_boot_notice("too-large", jfd);
      return false;
   }
   if (jfd->decoded_size > 2048u * 1024u) {
      if (raw_owned) free(raw);
      free(builtin);
      arc_adffs_package_free(package);
      free(package);
      arc_jfd_boot_log("decoded-too-large");
      if (notify)
         arc_jfd_boot_notice("decoded-too-large", jfd);
      return false;
   }
   arc_adffs_carrier = malloc(ARC_ADFFS_DISC_SIZE);
   arc_adffs_support = malloc(ARC_ADFFS_DISC_SIZE);
   char build_error[160] = "";
   bool built = arc_adffs_carrier && arc_adffs_support &&
                arc_adffs_carrier_build(raw, raw_size, arc_adffs_carrier,
                                        build_error, sizeof(build_error)) &&
                arc_adffs_support_build(package, &selection, arc_adffs_support,
                                        build_error, sizeof(build_error));
   if (raw_owned) free(raw);
   free(builtin);
   if (!built) {
      if (!strcmp(build_error, "carried set does not fit support disc")) {
         arc_adffs_package_free(package);
         free(package);
         arc_adffs_discard();
         arc_jfd_boot_log("too-large");
         if (notify)
            arc_jfd_boot_notice("too-large", jfd);
         return false;
      }
      char why[96];
      arc_reason_token(build_error[0] ? build_error : "out-of-memory", why);
      char full[128];
      snprintf(full, sizeof(full), "invalid-package:%s", why);
      arc_adffs_package_free(package);
      free(package);
      arc_adffs_discard();
      arc_jfd_boot_log(full);
      if (notify)
         arc_jfd_boot_notice(full, jfd);
      return false;
   }
   /* A native save takes precedence only when the complete ADFFS path is
    * viable. Discard the unused carrier before returning to native boot. */
   if (jfd->overlay && jfd->overlay->generation && !jfd->overlay->damaged)
   {
      arc_adffs_package_free(package);
      free(package);
      arc_adffs_discard();
      arc_jfd_boot_log("native-saves");
      if (notify && !entry->jfd_native_notice_shown)
      {
         arc_message("Saved disc changes found; booting this JFD natively to keep them visible.",
                     RETRO_LOG_INFO);
         entry->jfd_native_notice_shown = true;
      }
      return false;
   }
   if (arc_log)
      arc_log(RETRO_LOG_INFO,
              "JFD boot: path=adffs script=%s profile=%s version=\"%s\"\n",
              have_builtin ? "builtin" : "embedded",
              package->verified ? package->profile : "untested", package->version);
   arc_adffs_package_free(package);
   free(package);
   arc_adffs_active = true;
   if (policy == ARC_JFD_WRITE_TAB_OFF &&
       !arc_jfd_tab_adffs_notice_shown)
   {
      arc_message("Write-protect Tab has no effect on the ADFFS route. Use JFD Boot: Native to keep disc writes.",
                  RETRO_LOG_INFO);
      arc_jfd_tab_adffs_notice_shown = true;
   }
   arc_plan.launch_helper = true;
   arc_plan.launch_target[0] = 0;
   arc_plan.launch_target_length = 0;
   return true;
}

static bool arc_launch_os_supported(ArcOsClass os) {
#if defined(ARCDROID_TEST_FAULTS)
  if (os == ARC_OS_TEST)
    return true;
#endif
  return os == ARC_OS_RISCOS_300 || os == ARC_OS_RISCOS_310 ||
         os == ARC_OS_RISCOS_311 || os == ARC_OS_RISCOS_319;
}

static void arc_launch_candidates_message(const ArcLaunchDiscovery *discovery,
                                          const char *prefix,
                                          bool applications_only)
{
   char message[512];
   size_t at = (size_t)snprintf(message, sizeof(message), "%s", prefix);
   bool listed = false;
   bool suggestible = true;
   unsigned ordinal = 0u;
   for (unsigned i = 0u; i < discovery->candidate_count && at < sizeof(message); ++i)
   {
      if (discovery->candidates[i].resource) continue;
      ++ordinal;
      if (applications_only &&
          discovery->candidates[i].state != ARC_LAUNCH_CANDIDATE_APPLICATION)
         continue;
      if (!arc_launch_candidate_is_target(&discovery->candidates[i]) ||
          ordinal > 8u) suggestible = false;
      char name[4u * (2u * ARC_GUEST_NAME_MAX + 1u) + 1u];
      const ArcLaunchCandidate *candidate = &discovery->candidates[i];
      if (candidate->parent.length)
      {
         char parent[4u * ARC_GUEST_NAME_MAX + 1u];
         char leaf[4u * ARC_GUEST_NAME_MAX + 1u];
         (void)arc_launch_escape_name(&candidate->parent, parent, sizeof(parent));
         (void)arc_launch_escape_name(&candidate->name, leaf, sizeof(leaf));
         snprintf(name, sizeof(name), "%s.%s", parent, leaf);
      }
      else (void)arc_launch_escape_name(&candidate->name, name, sizeof(name));
      int written = snprintf(message + at, sizeof(message) - at, "%s%u %s",
                             listed ? " " : "", ordinal, name);
      if (written < 0 || (size_t)written >= sizeof(message) - at)
      { at = sizeof(message) - 1u; break; }
      at += (size_t)written;
      listed = true;
   }
   if (suggestible && at < sizeof(message) - 28u)
      snprintf(message + at, sizeof(message) - at, " — set Launch to Application N");
   arc_message(message, RETRO_LOG_WARN);
}

static bool arc_launch_binding_publish(const char *path, bool replacing,
                                       const uint8_t *data, size_t size)
{
   if (!replacing)
      return arc_atomic_publish(path, data, size, NULL, NULL, 0u);

   /* The libretro VFS rename contract forbids an existing destination. Keep
    * the old binding recoverable while installing the fully-written record. */
   char temporary[ARC_MEDIA_PATH_CAPACITY];
   char previous[ARC_MEDIA_PATH_CAPACITY];
   int t = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
   int p = snprintf(previous, sizeof(previous), "%s.previous", path);
   if (t < 0 || (size_t)t >= sizeof(temporary) ||
       p < 0 || (size_t)p >= sizeof(previous)) return false;
   (void)arc_io_remove(temporary);
   size_t previous_size = 0u;
   if (arc_io_file_size(previous, &previous_size) &&
       !arc_io_remove(previous)) return false;
   if (!arc_io_write_all(temporary, data, size))
   {
      (void)arc_io_remove(temporary);
      return false;
   }
   if (!arc_io_rename(path, previous))
   {
      size_t target_size = 0u;
      if (!arc_io_file_size(path, &target_size) &&
          arc_io_file_size(previous, &previous_size))
         (void)arc_io_rename(previous, path);
      else
         (void)arc_io_remove(previous);
      (void)arc_io_remove(temporary);
      return false;
   }
   if (!arc_io_rename(temporary, path))
   {
      (void)arc_io_remove(path);
      (void)arc_io_rename(previous, path);
      (void)arc_io_remove(temporary);
      return false;
   }
   (void)arc_io_remove(previous);
   return true;
}

static bool arc_launch_binding(const ArcLaunchDiscovery *discovery,
                               unsigned selected, bool reselected,
                               char *reason, size_t reason_size)
{
   char directory[ARC_MEDIA_PATH_CAPACITY];
   char path[ARC_MEDIA_PATH_CAPACITY];
   char leaf[46];
   int d = snprintf(directory, sizeof(directory), "%s/arcdroid/launch", arc_save_path);
   int l = snprintf(leaf, sizeof(leaf), "%s.bind", arc_plan.content_id);
   int p = d >= 0 && (size_t)d < sizeof(directory) &&
           l >= 0 && (size_t)l < sizeof(leaf) ?
      snprintf(path, sizeof(path), "%s/%s", directory, leaf) : -1;
   if (d < 0 || (size_t)d >= sizeof(directory) || p < 0 ||
       (size_t)p >= sizeof(path) || l < 0 || (size_t)l >= sizeof(leaf) ||
       !arc_io_make_path(directory))
   { snprintf(reason, reason_size, "binding storage unavailable"); return false; }

   bool exists = false;
   if (!arc_io_directory_contains(directory, leaf, &exists))
   { snprintf(reason, reason_size, "binding directory could not be listed"); return false; }

   uint8_t existing[ARC_LAUNCH_BINDING_MAX_SIZE];
   size_t existing_size = 0u;
   if (exists)
   {
      size_t declared_size = 0u;
      if (!arc_io_file_size(path, &declared_size))
      {
         snprintf(reason, reason_size,
                  "binding could not be read; existing binding preserved");
         return false;
      }
      if (declared_size > sizeof(existing))
      {
         snprintf(reason, reason_size,
                  "binding is invalid; existing binding preserved");
         return false;
      }
      ArcIoFile *file = arc_io_open_file(path, false);
      int64_t opened_size = file ? arc_io_file_length(file) : -1;
      existing_size = file ? arc_io_read_file(file, existing, declared_size) : 0u;
      uint8_t trailing;
      size_t trailing_size = file ? arc_io_read_file(file, &trailing, 1u) : 0u;
      bool closed = file && arc_io_close_file(file);
      if (opened_size < 0 || (uint64_t)opened_size != declared_size ||
          existing_size != declared_size || trailing_size || !closed)
      {
         snprintf(reason, reason_size,
                  "binding could not be read; existing binding preserved");
         return false;
      }
      ArcLaunchBinding binding;
      if (!arc_launch_binding_parse(existing, existing_size, &binding))
      {
         snprintf(reason, reason_size,
                  "binding is invalid; existing binding preserved");
         return false;
      }
      const ArcLaunchCandidate *candidate = &discovery->candidates[selected];
      const ArcGuestName *target = &candidate->name;
      if (!reselected)
      {
         unsigned selected_ordinal = 0u;
         for (unsigned i = 0u; i < selected; ++i)
            if (!discovery->candidates[i].resource) ++selected_ordinal;
         if ((candidate->parent.length &&
              (binding.version != 3u || binding.ordinal != selected_ordinal)) ||
             (!candidate->parent.length && binding.version == 3u) ||
             binding.parent.length != candidate->parent.length ||
             memcmp(binding.parent.bytes, candidate->parent.bytes,
                    candidate->parent.length) ||
             binding.target.length != target->length ||
             memcmp(binding.target.bytes, target->bytes, target->length))
         {
            snprintf(reason, reason_size,
                     "binding changed target; re-select Launch");
            return false;
         }
         return true;
      }
   }

   unsigned ordinal = 0u;
   for (unsigned i = 0u; i < selected; ++i)
      if (!discovery->candidates[i].resource) ++ordinal;
   uint8_t binding[ARC_LAUNCH_BINDING_MAX_SIZE];
   size_t binding_size = 0u;
   const ArcLaunchCandidate *candidate = &discovery->candidates[selected];
   bool formatted = candidate->parent.length ?
      arc_launch_binding_format_nested(ordinal, &candidate->parent,
         &candidate->name, discovery->candidate_fingerprint,
         binding, sizeof(binding), &binding_size) :
      arc_launch_binding_format(ordinal, &candidate->name,
         discovery->candidate_fingerprint,
         binding, sizeof(binding), &binding_size);
   if (!formatted ||
       !arc_launch_binding_publish(path, exists, binding, binding_size))
   { snprintf(reason, reason_size, "binding could not be published"); return false; }
   return true;
}

static void arc_launch_log(const ArcLaunchDiscovery *discovery,
                           int decision_index, const char *reason)
{
   if (!arc_log) return;
   char candidates[1024] = "";
   char applications[512] = "";
   char disc_name[4u * ARC_GUEST_NAME_MAX + 1u];
   char decision[4u * (2u * ARC_GUEST_NAME_MAX + 1u) + 1u] = "none";
   char address[4u * (2u * ARC_GUEST_NAME_MAX + 1u) + 1u] = "none";
   size_t ca = 0u, aa = 0u;
   unsigned ordinal = 0u;
   bool candidates_truncated = false, applications_truncated = false;
   (void)arc_launch_escape_name(&discovery->disc_name,
                                disc_name, sizeof(disc_name));
   if (decision_index >= 0 &&
       (unsigned)decision_index < discovery->candidate_count)
   {
      const ArcLaunchCandidate *c = &discovery->candidates[decision_index];
      char leaf[4u * ARC_GUEST_NAME_MAX + 1u];
      (void)arc_launch_escape_name(&c->name, leaf, sizeof(leaf));
      if (c->parent.length)
      {
         char parent[4u * ARC_GUEST_NAME_MAX + 1u];
         char operand[4u * ARC_GUEST_NAME_MAX + 1u];
         (void)arc_launch_escape_name(&c->parent, parent, sizeof(parent));
         (void)arc_launch_escape_name(&c->parent_address, operand, sizeof(operand));
         snprintf(decision, sizeof(decision), "%s.%s", parent, leaf);
         if (c->addressable)
            snprintf(address, sizeof(address), "%s.%s", operand, leaf);
      }
      else snprintf(decision, sizeof(decision), "%s", leaf);
   }
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
   {
      char name[4u * (2u * ARC_GUEST_NAME_MAX + 1u) + 1u];
      const ArcLaunchCandidate *c = &discovery->candidates[i];
      char leaf[4u * ARC_GUEST_NAME_MAX + 1u];
      (void)arc_launch_escape_name(&c->name, leaf, sizeof(leaf));
      if (c->parent.length)
      {
         char parent[4u * ARC_GUEST_NAME_MAX + 1u];
         (void)arc_launch_escape_name(&c->parent, parent, sizeof(parent));
         snprintf(name, sizeof(name), "%s.%s", parent, leaf);
      }
      else snprintf(name, sizeof(name), "%s", leaf);
      const char *state = discovery->candidates[i].state == ARC_LAUNCH_CANDIDATE_APPLICATION ? "app" :
                          discovery->candidates[i].state == ARC_LAUNCH_CANDIDATE_UNKNOWN ? "unknown" : "other";
      if (discovery->candidates[i].resource) state = "resource";
      int n = snprintf(candidates + ca, sizeof(candidates) - ca, "%s%u:%s:%s",
                       i ? "," : "", i + 1u, name, state);
      if (n > 0 && (size_t)n < sizeof(candidates) - ca - 24u)
         ca += (size_t)n;
      else { candidates_truncated = true; break; }
      if (!discovery->candidates[i].resource) ++ordinal;
      if (discovery->candidates[i].state == ARC_LAUNCH_CANDIDATE_APPLICATION &&
          !discovery->candidates[i].resource && !applications_truncated)
      {
         n = snprintf(applications + aa, sizeof(applications) - aa, "%s%u:%s",
                      aa ? "," : "", ordinal, name);
         if (n > 0 && (size_t)n < sizeof(applications) - aa - 24u)
            aa += (size_t)n;
         else applications_truncated = true;
      }
   }
   if (candidates_truncated)
      snprintf(candidates + ca, sizeof(candidates) - ca, "...total=%u",
               discovery->candidate_count);
   if (applications_truncated || candidates_truncated)
      snprintf(applications + aa, sizeof(applications) - aa, "...total=%u",
               discovery->application_count);
   arc_log(RETRO_LOG_INFO,
      "Launch: boot=%u disc=\"%s\" candidates=[%s] applications=[%s] decision=%s reason=%s ignored-attribute-bits=%s helper=%s root-boot=%s scope=%s scan=%s discovery-reason=%s address=%s phase=%s boot-watch=%s%s%s\n",
      discovery->boot_option, disc_name, candidates, applications,
      decision, reason, discovery->ignored_attribute_bits ? "yes" : "no",
      arc_helper_path[0] ? arc_helper_path : "none",
      discovery->root_boot ? "yes" : "no",
      discovery->nested_scope ? "nested" : "root",
      discovery->scan_incomplete ? "incomplete" : "complete",
      arc_launch_reason_name(discovery->reason), address,
      arc_boot_recovery_phase ? "recovery" : "initial",
      arc_boot_watch_override ? arc_boot_watch_override :
      arc_boot_recovery_phase ? "prompt" :
         arc_boot_eligible ? "armed" : "disabled",
      arc_boot_recovery_phase ? " recovery=" : "",
      arc_boot_recovery_phase ? arc_boot_recovery_action : "");
}

static void arc_launch_log_skipped_siblings(
   const ArcLaunchDiscovery *discovery, unsigned target_index)
{
   if (!arc_log || !discovery) return;
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
   {
      const ArcLaunchCandidate *candidate = &discovery->candidates[i];
      if (i == target_index || !candidate->has_boot ||
          candidate->parent.length !=
             discovery->candidates[target_index].parent.length ||
          memcmp(candidate->parent.bytes,
                 discovery->candidates[target_index].parent.bytes,
                 candidate->parent.length) ||
          arc_launch_sibling_bootable(candidate)) continue;
      char name[4u * ARC_GUEST_NAME_MAX + 1u];
      (void)arc_launch_escape_name(&candidate->name, name, sizeof(name));
      arc_log(RETRO_LOG_WARN,
         "Launch sibling boot skipped: candidate=%u leaf=%s reason=not launchable or representable\n",
         i + 1u, name);
   }
}

static bool arc_choice_path_build(bool create_directory)
{
   char directory[ARC_MEDIA_PATH_CAPACITY];
   int d = snprintf(directory, sizeof(directory), "%s/arcdroid/launch",
                    arc_save_path);
   int p = d >= 0 && (size_t)d < sizeof(directory) ?
      snprintf(arc_choice_path, sizeof(arc_choice_path), "%s/%s.choice",
               directory, arc_plan.content_id) : -1;
   if (d < 0 || (size_t)d >= sizeof(directory) || p < 0 ||
       (size_t)p >= sizeof(arc_choice_path) ||
       (create_directory && !arc_io_make_path(directory)))
   {
      arc_choice_path[0] = '\0';
      return false;
   }
   return true;
}

static bool arc_choice_load(const ArcLaunchDiscovery *discovery,
                            bool *valid, int *selected)
{
   *valid = false;
   *selected = INT_MIN;
   if (!arc_choice_path_build(false)) return false;
   size_t declared = 0u;
   if (!arc_io_file_size(arc_choice_path, &declared)) return true;
   if (!declared || declared > ARC_LAUNCH_CHOICE_MAX_SIZE) return true;
   uint8_t data[ARC_LAUNCH_CHOICE_MAX_SIZE];
   ArcIoFile *file = arc_io_open_file(arc_choice_path, false);
   int64_t opened = file ? arc_io_file_length(file) : -1;
   size_t got = file ? arc_io_read_file(file, data, declared) : 0u;
   uint8_t trailing = 0u;
   size_t extra = file ? arc_io_read_file(file, &trailing, 1u) : 0u;
   bool closed = file && arc_io_close_file(file);
   if (opened < 0 || (uint64_t)opened != declared || got != declared ||
       extra || !closed) return false;
   ArcLaunchChoiceRecord choice;
   if (!arc_launch_choice_parse(data, declared, &choice)) return true;
   int match = arc_launch_choice_match(discovery, &choice);
   if (match == INT_MIN) return true;
   if (match == -2 &&
       (arc_hd_scope.state != ARC_HD_COMPLETED ||
        !arc_hd_scope.product_present ||
        choice.attempt != arc_hd_scope.attempt ||
        choice.target.length != arc_hd_scope.product.length ||
        memcmp(choice.target.bytes,arc_hd_scope.product.bytes,
               choice.target.length))) return true;
   *valid = true;
   *selected = match;
   return true;
}

static bool arc_choice_publish(const ArcLaunchDiscovery *discovery,
                               int selected)
{
   if (!arc_choice_path_build(true)) return false;
   uint8_t data[ARC_LAUNCH_CHOICE_MAX_SIZE];
   size_t size = 0u;
   const ArcGuestName *target = selected >= 0 ?
      &discovery->candidates[selected].name : NULL;
   bool formatted = selected == -2 ?
      arc_launch_choice_format_installed(&arc_hd_scope.product,
         discovery->candidate_fingerprint,arc_hd_scope.attempt,
         data,sizeof(data),&size) : discovery->nested_scope ?
      arc_launch_choice_format_nested(selected >= 0 ?
         &discovery->candidates[selected].parent : NULL,
         target, selected < 0, discovery->candidate_fingerprint,
         data, sizeof(data), &size) :
      arc_launch_choice_format(target, selected < 0,
         discovery->candidate_fingerprint, data, sizeof(data), &size);
   if (!formatted) return false;
   size_t ignored = 0u;
   bool replacing = arc_io_file_size(arc_choice_path, &ignored);
   return arc_launch_binding_publish(arc_choice_path, replacing, data, size);
}

static bool arc_boot_record_path(char path[ARC_MEDIA_PATH_CAPACITY],
                                 bool create_directory)
{
   char directory[ARC_MEDIA_PATH_CAPACITY];
   int d = snprintf(directory, sizeof(directory), "%s/arcdroid/launch", arc_save_path);
   int p = d >= 0 && (size_t)d < sizeof(directory) ?
      snprintf(path, ARC_MEDIA_PATH_CAPACITY, "%s/%s.boot", directory,
               arc_plan.content_id) : -1;
   return d >= 0 && (size_t)d < sizeof(directory) && p >= 0 &&
      (size_t)p < ARC_MEDIA_PATH_CAPACITY &&
      (!create_directory || arc_io_make_path(directory));
}

/* Hash the whole ordered image set. JFD store hashes are taken only from
 * snapshots accepted by the existing store validator. */
static bool arc_boot_media_digest(char output[41])
{
   uint8_t bytes[1u + ARC_PLAYLIST_MAX_ENTRIES * (92u + ARC_JFD_PDD5_TRACKS + 2u)];
   size_t at = 0u;
   bytes[at++] = (uint8_t)arc_media_count;
   for (unsigned i = 0u; i < arc_media_count; ++i)
   {
      ArcMediaEntry *entry = &arc_media[i];
      bytes[at++] = entry->populated ? 1u : 0u;
      bytes[at++] = (uint8_t)entry->info.kind;
      bytes[at++] = (uint8_t)i;
      bytes[at++] = entry->zero_tail_presentation ? 1u : 0u;
      uint64_t extent = entry->effective_extent;
      for (unsigned shift = 0u; shift < 8u; ++shift)
         bytes[at++] = (uint8_t)(extent >> (shift * 8u));
      if (!entry->populated)
      {
         memset(bytes + at, '0', 80u); at += 80u;
         continue;
      }
      memcpy(bytes + at, entry->digest.sha1, 40u); at += 40u;
      if (entry->info.kind == ARC_MEDIA_JFD)
      {
         ArcJfdImage temporary = {0};
         ArcJfdImage *view = &entry->jfd;
         if (i != arc_image_index)
         {
            void *raw = NULL;
            size_t size = 0u;
            char error[128] = {0};
            const char *reason = NULL;
            if (!arc_io_read_all(entry->staged, &raw, &size)) return false;
            bool base_valid = !strcmp(arc_digest(raw, size).sha1,
                                      entry->digest.sha1);
            if (!base_valid)
            { free(raw); return false; }
            if (!arc_jfd_parse_owned(raw, size, &temporary, error,
                                     sizeof(error))) return false;
            view = &temporary;
            if (!arc_jfd_store_load(view, arc_save_path,
                 arc_core_options.jfd_write_policy,
                    arc_plan.disk_writes != ARC_DISK_WRITES_OFF,
                    false, i + 1u, &reason))
            { arc_jfd_free(&temporary); return false; }
         }
         if (!view->overlay || view->overlay->damaged || view->overlay->failed)
         { arc_jfd_free(&temporary); return false; }
         /* The temporary has no D-089 mask; D/E identification covers only wholly free slots, outside R1'. */
         if (view == &temporary)
            arc_jfd_file_data_prepare(view, true,
               arc_core_options.jfd_omitted_strict,
               arc_jfd_file_data_class(view, arc_plan.disk_writes,
                                       arc_core_options.jfd_write_policy));
         bytes[at++] = 1u; /* Native JFD presentation revision. */
         bytes[at++] = arc_core_options.jfd_omitted_strict ? 0u : 1u;
         memcpy(bytes + at, view->file_data.mask, ARC_JFD_PDD5_TRACKS);
         at += ARC_JFD_PDD5_TRACKS;
         const char *store = view->overlay->visible &&
            view->overlay->store_sha1[0] ? view->overlay->store_sha1 : NULL;
         if (store) memcpy(bytes + at, store, 40u);
         else memset(bytes + at, '0', 40u);
         arc_jfd_free(&temporary);
      }
      else
      {
         void *raw = NULL;
         size_t size = 0u;
         if (!arc_io_read_all(entry->staged, &raw, &size)) return false;
         ArcDigest digest = arc_digest(raw, size);
         free(raw);
         memcpy(bytes + at, digest.sha1, 40u);
      }
      at += 40u;
   }
   arc_sha1_hex(bytes, at, output);
   return true;
}
#ifdef ARCDROID_TEST_FAULTS
bool arc_libretro_test_boot_media_digest(char output[41])
{
   return arc_boot_media_digest(output);
}
#endif

static bool arc_boot_stamp(ArcBootObservation *stamp)
{
   uint8_t prepared[ARC_CMOS_PAYLOAD_SIZE];
   uint8_t starting[ARC_CMOS_PAYLOAD_SIZE];
   const ArcMachineDescriptor *machine = arc_machine_descriptor(arc_plan.machine);
   if (!arc_firmware || !arc_firmware_size ||
       !machine || !arc_cmos_decode(arc_cmos_prepared, &arc_plan, prepared) ||
       !arc_cmos_boot_stamp_payload(arc_cmos_policy_os(), machine->io, prepared,
                                    arc_plan.launch_helper, starting) ||
       !arc_boot_media_digest(stamp->media)) return false;
   arc_sha1_hex(arc_firmware, arc_firmware_size, stamp->firmware);
   arc_sha1_hex(starting, sizeof(starting), stamp->cmos);
   stamp->machine = (uint32_t)arc_plan.machine;
   stamp->cpu = (uint32_t)arc_plan.cpu;
   stamp->memory = arc_plan.memory_kib;
   stamp->jfd_boot = (uint32_t)arc_core_options.jfd_boot;
   stamp->disk_writes = (uint32_t)arc_plan.disk_writes;
   stamp->jfd_write_policy = (uint32_t)arc_core_options.jfd_write_policy;
   stamp->legacy_slot = 1u;
   stamp->hard_disc_policy = arc_hd_option_auto ? 1u : 0u;
   stamp->hard_disc_attached = arc_hd_store &&
      arc_install_store_state(arc_hd_store) == ARC_INSTALL_STORE_READY;
   stamp->hard_disc_writable = stamp->hard_disc_attached &&
      arc_install_store_writable(arc_hd_store);
   if (stamp->hard_disc_attached)
      arc_sha256_hex(arc_install_store_image(arc_hd_store),
                     ARC_INSTALL_HD_BYTES, stamp->hard_disc);
   else { memset(stamp->hard_disc, '0', 64u); stamp->hard_disc[64] = 0; }
   return true;
}
#ifdef ARCDROID_TEST_FAULTS
bool arc_libretro_test_boot_stamp(ArcBootObservation *stamp)
{
   return stamp && arc_boot_stamp(stamp);
}
bool arc_libretro_test_boot_start_stamp(ArcBootObservation *stamp)
{
   if (!stamp || !arc_boot_stamp_ready) return false;
   *stamp=arc_boot_start_stamp;
   return true;
}
bool arc_libretro_test_boot_record_hit(void)
{ return arc_boot_record_hit; }
#endif

static void arc_boot_record_invalidate(void)
{
   char path[ARC_MEDIA_PATH_CAPACITY];
   arc_boot_record_suppressed = true;
   arc_boot_record_hit = false;
   if (arc_boot_record_path(path, false)) (void)arc_io_remove(path);
}

static bool arc_boot_record_load(const ArcBootObservation *current)
{
   char path[ARC_MEDIA_PATH_CAPACITY];
   size_t size = 0u;
   if (arc_boot_record_suppressed || !arc_boot_record_path(path, false) ||
       !arc_io_file_size(path, &size)) return false;
   uint8_t data[ARC_BOOT_OBSERVATION_SIZE];
   bool valid = false;
   if (size == sizeof(data))
   {
      ArcIoFile *file = arc_io_open_file(path, false);
      int64_t length = file ? arc_io_file_length(file) : -1;
      size_t got = file ? arc_io_read_file(file, data, size) : 0u;
      uint8_t trailing;
      size_t extra = file ? arc_io_read_file(file, &trailing, 1u) : 0u;
      bool closed = file && arc_io_close_file(file);
      ArcBootObservation stored;
      valid = length == (int64_t)size && got == size && !extra && closed &&
         arc_boot_observation_parse(data, size, &stored) &&
         arc_boot_observation_equal(&stored, current);
   }
   if (!valid) (void)arc_io_remove(path);
   return valid;
}

static void arc_boot_record_publish(void)
{
   if (!arc_boot_stamp_ready || arc_boot_record_suppressed ||
       arc_install_store_wrote(arc_hd_store)) return;
   ArcBootObservation current;
   if (!arc_boot_stamp(&current) ||
       strcmp(current.media, arc_boot_start_stamp.media) ||
       current.hard_disc_policy != arc_boot_start_stamp.hard_disc_policy ||
       current.hard_disc_attached != arc_boot_start_stamp.hard_disc_attached ||
       current.hard_disc_writable != arc_boot_start_stamp.hard_disc_writable ||
       strcmp(current.hard_disc, arc_boot_start_stamp.hard_disc)) return;
   char path[ARC_MEDIA_PATH_CAPACITY];
   uint8_t data[ARC_BOOT_OBSERVATION_SIZE];
   size_t existing = 0u;
   if (arc_boot_record_path(path, true) &&
       arc_boot_observation_format(&arc_boot_start_stamp, data, sizeof(data)))
      (void)arc_launch_binding_publish(path,
         arc_io_file_size(path, &existing), data, sizeof(data));
}

static bool arc_launch_helper_publish(const ArcLaunchDiscovery *discovery,
                                      unsigned selected, char *error,
                                      size_t error_size);

static void arc_hd_scope_report(bool notify, int target_index)
{
   if (arc_log && !arc_hd_scope_reported && (notify || target_index >= 0))
   {
      arc_log(RETRO_LOG_INFO,
         "Hard disc scope: state=%s copy=%s product=%s applications=%u other=%u attempt=%u\n",
         arc_hd_scope_name(arc_hd_scope.state),
         arc_hd_scope.copy_present?"yes":"no",
         arc_hd_scope.product_present?"yes":"no",
         arc_hd_scope.application_count,arc_hd_scope.other_count,
         arc_hd_scope.attempt);
      arc_hd_scope_reported=true;
   }
   if (notify && arc_hd_scope.state != ARC_HD_ABSENT &&
       !(arc_hd_scope.state == ARC_HD_COMPLETED &&
         arc_hd_scope.product_present) && !arc_hd_notice_shown)
   {
      char notice[256];
      if (arc_hd_scope.state == ARC_HD_UNOWNED)
         snprintf(notice,sizeof(notice),
            "An unrelated ArcDroid directory exists on the hard disc; move it aside to allow installation.");
      else if (arc_hd_scope.state == ARC_HD_COMPLETED &&
               !arc_hd_scope.product_present)
         snprintf(notice,sizeof(notice),
            "The installed product is missing from ADFS::4.$.ArcDroid; move the directory aside (it may hold saves) to install again.");
      else snprintf(notice,sizeof(notice),
         "ArcDroid's installation on the hard disc is %s; move ADFS::4.$.ArcDroid aside (it may hold saves) to install again.",
         arc_hd_scope_name(arc_hd_scope.state));
      arc_message(notice,RETRO_LOG_WARN);
      arc_hd_notice_shown=true;
   }
}

static bool arc_hd_scope_prepare(const ArcLaunchDiscovery *discovery,
                                 unsigned selected, bool notify)
{
   if (notify && arc_hd_scope_cached && selected == arc_hd_selected)
   { arc_hd_scope_report(notify,discovery->target_index); return true; }
   const ArcMachineDescriptor *machine=arc_machine_descriptor(arc_session_machine);
   if (!machine || !machine->ide_capable || !arc_hd_store ||
       arc_install_store_state(arc_hd_store) != ARC_INSTALL_STORE_READY ||
       arc_adffs_active || arc_core_options.launch != ARC_LAUNCH_AUTO ||
       arc_media_count != 1u || arc_folder_set_active ||
       arc_image_index != 0u || selected >= discovery->candidate_count)
      return false;
   const ArcLaunchCandidate *candidate=&discovery->candidates[selected];
   char relative[22]; uint8_t length=0u;
   if (!arc_launch_candidate_is_target(candidate) ||
       !arc_launch_target_compose(candidate,relative,sizeof(relative),&length) ||
       snprintf(arc_hd_source,sizeof(arc_hd_source),"ADFS::0.$.%s",relative) >=
          (int)sizeof(arc_hd_source) ||
       !arc_boot_media_digest(arc_hd_media)) return false;
   memcpy(arc_hd_target,candidate->name.bytes,candidate->name.length);
   arc_hd_target[candidate->name.length]=0;
   memcpy(arc_hd_fingerprint,discovery->candidate_fingerprint,41u);
   ArcHdExpected expected={arc_hd_source,arc_hd_media,arc_hd_fingerprint,
      arc_plan.content_id,arc_os_slug(arc_plan.os),&candidate->name};
   arc_hd_scope=arc_hd_scope_read(arc_install_store_read,arc_hd_store,&expected);
   arc_hd_scope_cached=true;
   arc_hd_selected=selected;
   if (arc_hd_scope.product.length) {
      memcpy(arc_hd_product,arc_hd_scope.product.bytes,arc_hd_scope.product.length);
      arc_hd_product[arc_hd_scope.product.length]=0;
   } else arc_hd_product[0]=0;
   arc_hd_scope_report(notify,discovery->target_index);
   return true;
}

#if defined(ARCDROID_TEST_FAULTS)
extern bool arc_hd_test_seed_product(ArcInstallStore *store,
                                    const ArcHdExpected *expected);
extern bool arc_hd_test_seed_started(ArcInstallStore *store,
                                    const ArcHdExpected *expected,
                                    unsigned variant);
extern bool arc_hd_test_preflight_collision(ArcInstallStore *store);
RETRO_API bool arc_libretro_test_seed_hd_product(char id[41])
{
   if (!id || !arc_hd_scope_cached || arc_hd_scope.state != ARC_HD_ABSENT ||
       !arc_hd_store || !arc_install_store_writable(arc_hd_store) ||
       arc_hd_selected >= arc_boot_discovery.candidate_count) return false;
   const ArcLaunchCandidate *candidate=&arc_boot_discovery.candidates[arc_hd_selected];
   ArcHdExpected expected={arc_hd_source,arc_hd_media,arc_hd_fingerprint,
      arc_plan.content_id,arc_os_slug(arc_plan.os),&candidate->name};
   if (!arc_hd_test_seed_product(arc_hd_store,&expected)) return false;
   memcpy(id,arc_plan.content_id,41u);
   return true;
}
RETRO_API bool arc_libretro_test_hd_preflight_collision(void)
{
   return arc_hd_scope_cached && arc_hd_scope.state == ARC_HD_ABSENT &&
      arc_hd_store && arc_hd_test_preflight_collision(arc_hd_store);
}
RETRO_API bool arc_libretro_test_seed_hd_started(unsigned variant,
                                                 bool arm_copy,
                                                 char id[41])
{
   if (!id || !arc_hd_scope_cached || arc_hd_scope.state != ARC_HD_ABSENT ||
       !arc_hd_store || !arc_install_store_writable(arc_hd_store) ||
       arc_hd_selected >= arc_boot_discovery.candidate_count ||
       (arm_copy && variant != 1u)) return false;
   const ArcLaunchCandidate *candidate=&arc_boot_discovery.candidates[arc_hd_selected];
   ArcHdExpected expected={arc_hd_source,arc_hd_media,arc_hd_fingerprint,
      arc_plan.content_id,arc_os_slug(arc_plan.os),&candidate->name};
   if (!arc_hd_test_seed_started(arc_hd_store,&expected,variant)) return false;
   memcpy(id,arc_plan.content_id,41u);
   if (arm_copy) {
      char command[257];
      int length=snprintf(command,sizeof(command),
         "Run ADFS::4.$.ArcDroid.%s.!Run",arc_hd_target);
      if (length <= 0 || (size_t)length >= sizeof(command) ||
          !arc_engine_launch_outcome_arm(command)) return false;
      arc_hd_mode=ARC_HD_HELPER_INSTALL;
      arc_hd_observe_target=false;
      arc_hd_observe_copy=true;
      arc_hd_install_used=true;
      arc_hd_last_outcome_state=ARC_OUTCOME_WAIT;
   }
   return true;
}
RETRO_API bool arc_libretro_test_repeat_hd_outcome(void)
{
   if (!arc_hd_install_used || !arc_hd_target[0]) return false;
   char command[257];
   int length=snprintf(command,sizeof(command),
      "Run ADFS::0.$.%s.!Run",arc_hd_target);
   if (length <= 0 || (size_t)length >= sizeof(command) ||
       !arc_engine_launch_outcome_arm(command)) return false;
   arc_hd_observe_target=true;
   arc_hd_observe_copy=false;
   arc_hd_last_outcome_state=ARC_OUTCOME_WAIT;
   /* Replay a second qualifying sequence against the ROM's guest command. */
   extern void arc_engine_boot_watch_swi(uint32_t,uint32_t,uint32_t,uint32_t,
                                         uint32_t,uint32_t,uint32_t);
   const uint32_t rom=UINT32_C(0x03800020);
   arc_engine_boot_watch_swi(0x400deu,0x02001000u,0u,0u,0u,rom,0u);
   arc_engine_launch_outcome_refused(0u);
   arc_engine_boot_watch_swi(0x11u,0u,0u,0u,0u,rom,0u);
   arc_engine_boot_watch_swi(0x400ddu,0u,0u,0u,0u,rom,0u);
   arc_engine_boot_watch_swi(0x400c7u,0u,0u,0u,0u,rom,0u);
   ArcLaunchOutcome outcome;
   arc_engine_launch_outcome_snapshot(&outcome);
   return outcome.state == ARC_OUTCOME_POLL && outcome.refused_floppy == 1u;
}
#endif

static bool arc_boot_prepare_hit(const ArcLaunchDiscovery *discovery)
{
   const char *reason = "remembered boot observation; disc !Boot skipped";
   bool remembered = false;
   int remembered_target = INT_MIN;
   (void)arc_choice_load(discovery, &remembered, &remembered_target);
   int selected = -1;
   ArcLaunchRecovery action = arc_launch_recovery_decide(discovery, &selected);
   if (remembered && arc_core_options.launch_chooser != ARC_LAUNCH_CHOOSER_ASK)
   {
      if (remembered_target == -1)
      {
         arc_boot_eligible = false;
         arc_launch_log(discovery, -1, reason);
         return false;
      }
      selected = remembered_target == -2 ? (int)arc_hd_selected : remembered_target;
      if (remembered_target == -2)
         arc_hd_requested_mode=ARC_HD_HELPER_PRODUCT;
      action = ARC_LAUNCH_RECOVERY_HELPER;
   }
   if (arc_core_options.launch_chooser == ARC_LAUNCH_CHOOSER_ASK &&
       selected >= 0 && arc_hd_scope.state == ARC_HD_COMPLETED &&
       arc_hd_scope.product_present &&
       arc_launch_chooser_init(&arc_chooser,discovery,remembered,
                               remembered_target))
   {
      (void)arc_launch_chooser_add_installed(&arc_chooser,
         &arc_hd_scope.product,arc_hd_scope.attempt,
         remembered_target == -2);
      arc_launch_chooser_boot_hit(&arc_chooser,discovery);
      arc_chooser_discovery=*discovery;
      arc_boot_eligible=false;
      arc_launch_log(discovery,-1,reason);
      return true;
   }
   if (action == ARC_LAUNCH_RECOVERY_CHOOSER)
   {
      if (arc_launch_chooser_init(&arc_chooser, discovery,
                                  remembered, remembered_target))
      {
         arc_launch_chooser_boot_hit(&arc_chooser, discovery);
         arc_chooser_discovery = *discovery;
         arc_boot_eligible = false;
         arc_launch_log(discovery, -1, reason);
         return true;
      }
   }
   else if (action == ARC_LAUNCH_RECOVERY_HELPER)
   {
      char error[160] = {0};
      if (!remembered && selected >= 0 && arc_hd_scope_cached &&
          arc_hd_selected == (unsigned)selected &&
          arc_hd_scope.state == ARC_HD_COMPLETED &&
          arc_hd_scope.product_present)
         arc_hd_requested_mode=ARC_HD_HELPER_PRODUCT;
      bool published=arc_launch_helper_publish(discovery,(unsigned)selected,
                                                error,sizeof(error));
      arc_hd_requested_mode=ARC_HD_HELPER_FLOPPY;
      if (published)
      {
         arc_boot_eligible = false;
         arc_launch_log(discovery, selected,
            arc_hd_mode == ARC_HD_HELPER_PRODUCT ? "hd-product" : reason);
         return false;
      }
      arc_boot_record_invalidate();
      arc_hd_requested_mode=ARC_HD_HELPER_FLOPPY;
      arc_message(error[0] ? error : "Remembered launch helper publication failed.",
                  RETRO_LOG_ERROR);
   }
   arc_boot_eligible = false;
   arc_launch_log(discovery, -1, reason);
   return false;
}

static bool arc_launch_helper_publish(const ArcLaunchDiscovery *discovery,
                                      unsigned selected, char *refusal,
                                      size_t refusal_size)
{
   const ArcLaunchCandidate *candidate = &discovery->candidates[selected];
   char target[sizeof(arc_plan.launch_target)];
   uint8_t target_length = 0u;
   if (!arc_launch_target_compose(candidate, target, sizeof(target),
                                  &target_length))
   {
      snprintf(refusal, refusal_size, "launch target path is too long");
      return false;
   }
   ArcHdHelperMode mode=arc_hd_requested_mode;
   bool inspected=false;
   if (mode == ARC_HD_HELPER_FLOPPY)
      inspected=arc_hd_scope_prepare(discovery,selected,true);
   ArcHdProgramSpec spec={arc_hd_source,arc_hd_target,arc_hd_product,
      arc_hd_media,arc_hd_fingerprint,arc_plan.content_id,
      arc_os_slug(arc_plan.os),arc_hd_scope.attempt};
   uint8_t *helper = malloc(ARC_LAUNCH_DISC_SIZE);
   bool ramdisc = mode == ARC_HD_HELPER_FLOPPY &&
      arc_core_options.launch_ramdisc &&
      arc_launch_os_supported(arc_plan.os);
   if (!helper || !(mode == ARC_HD_HELPER_FLOPPY ?
       arc_launch_disc_build_ramdisc(discovery,selected,ramdisc,helper,
                                     refusal,refusal_size) :
       arc_launch_disc_build_mode(discovery,selected,mode,&spec,helper,
                                  refusal,refusal_size)))
   {
      free(helper);
      return false;
   }
   char directory[ARC_MEDIA_PATH_CAPACITY];
   int d = snprintf(directory, sizeof(directory), "%s/arcdroid/launch", arc_save_path);
   int p = d >= 0 && (size_t)d < sizeof(directory) ?
      snprintf(arc_helper_path, sizeof(arc_helper_path), "%s/%s%s.adl",
               directory, arc_plan.content_id,
               mode == ARC_HD_HELPER_INSTALL ? ".install" :
               mode == ARC_HD_HELPER_MARK ? ".product" : "") : -1;
   if (d < 0 || (size_t)d >= sizeof(directory) || p < 0 ||
       (size_t)p >= sizeof(arc_helper_path) || !arc_io_make_path(directory))
      arc_helper_path[0] = '\0';
   bool valid = arc_helper_path[0] &&
      arc_file_digest_matches(arc_helper_path, helper, ARC_LAUNCH_DISC_SIZE);
   if (!valid && arc_helper_path[0])
   {
      size_t ignored = 0u;
      if (arc_io_file_size(arc_helper_path, &ignored) &&
          !arc_io_remove(arc_helper_path)) arc_helper_path[0] = '\0';
      if (arc_helper_path[0] &&
          !arc_atomic_publish(arc_helper_path, helper, ARC_LAUNCH_DISC_SIZE,
                              NULL, NULL, 0u)) arc_helper_path[0] = '\0';
   }
   free(helper);
   if (!arc_helper_path[0]) return false;
   arc_plan.launch_helper = true;
   arc_hd_selected=selected;
   arc_hd_mode=mode;
   arc_hd_observe_target=mode == ARC_HD_HELPER_FLOPPY && inspected &&
      arc_hd_scope.state == ARC_HD_ABSENT &&
      arc_install_store_writable(arc_hd_store);
   arc_hd_observe_copy=mode == ARC_HD_HELPER_INSTALL;
   memcpy(arc_plan.launch_target, target, (size_t)target_length + 1u);
   arc_plan.launch_target_length = target_length;
   arc_launch_log_skipped_siblings(discovery, selected);
   if (arc_log) arc_log(RETRO_LOG_INFO, "RAM disc: %s\n",
                        ramdisc ? "request=32K if absent" : "disabled");
   return true;
}

static bool arc_prepare_launch(ArcMediaEntry *entry, bool ejected,
                               ArcLaunchOption option, bool reselected)
{
   ArcLaunchDiscovery discovery;
   memset(&discovery, 0, sizeof(discovery));
   discovery.target_index = -1;
   discovery.reason = ARC_LAUNCH_REASON_UNREADABLE;
   arc_boot_eligible = false;
   arc_helper_path[0] = '\0';
   arc_plan.launch_helper = false;
   arc_plan.launch_target[0] = '\0';
   arc_plan.launch_target_length = 0u;
   arc_hd_observe_target = arc_hd_observe_copy = false;
   arc_hd_scope_cached=false;
   arc_hd_scope_reported=false;
   memset(&arc_hd_scope,0,sizeof(arc_hd_scope));
   arc_hd_product[0]=0;
   if (ejected || !entry || !entry->populated)
   { arc_launch_log(&discovery, -1, "drive 0 empty"); return false; }

   void *raw = NULL;
   size_t raw_size = 0u;
   const ArcJfdImage *jfd = entry->info.kind == ARC_MEDIA_JFD ? &entry->jfd : NULL;
   if (!jfd && !arc_io_read_all(entry->staged, &raw, &raw_size))
   { arc_launch_log(&discovery, -1, "media read failed"); return false; }
   if (!jfd && entry->effective_extent && raw_size > entry->effective_extent)
      raw_size = entry->effective_extent;
   (void)arc_launch_discover(raw, raw_size, &entry->info, jfd, &discovery);
   free(raw);
   arc_boot_discovery = discovery;
   arc_boot_eligible = option == ARC_LAUNCH_AUTO && arc_image_index == 0u &&
      discovery.reason == ARC_LAUNCH_REASON_DISC_BOOTS_ITSELF &&
      arc_launch_os_supported(arc_plan.os) && !arc_adffs_active;
   arc_boot_stamp_ready = false;
   arc_boot_record_hit = false;
   arc_install_store_clear_wrote(arc_hd_store);
   if (arc_log && discovery.backup_map_used)
      arc_log(RETRO_LOG_WARN, "FileCore: primary map invalid or unreadable; using backup map.\n");
   if (arc_log && discovery.old_map_clamped)
      arc_log(RETRO_LOG_WARN, "FileCore: declared old-map sector count exceeds physical geometry; clamped.\n");
   if (option == ARC_LAUNCH_OFF)
   { arc_launch_log(&discovery, -1, "disabled"); return false; }
   if (!arc_launch_os_supported(arc_plan.os))
   { arc_launch_log(&discovery, -1, "OS class unsupported"); return false; }

   if (option == ARC_LAUNCH_AUTO && discovery.target_index >= 0)
      (void)arc_hd_scope_prepare(&discovery,(unsigned)discovery.target_index,true);
   else if (option == ARC_LAUNCH_AUTO)
      for (unsigned i=0u; i<discovery.candidate_count; ++i)
         if (arc_launch_candidate_is_target(&discovery.candidates[i]) &&
             arc_hd_scope_prepare(&discovery,i,false) &&
             arc_hd_scope.state == ARC_HD_COMPLETED &&
             arc_hd_scope.product_present)
         {
            arc_hd_scope_report(true,discovery.target_index);
            break;
         }

   if (arc_boot_eligible && arc_boot_stamp(&arc_boot_start_stamp))
   {
      arc_boot_stamp_ready = true;
      if (arc_boot_record_load(&arc_boot_start_stamp))
      {
         arc_boot_record_hit = true;
         return arc_boot_prepare_hit(&discovery);
      }
   }

   int selected = discovery.target_index;
   char refusal[160] = {0};
   const char *reason = arc_launch_reason_name(discovery.reason);
   bool remembered_floppy=false;
   bool ask_remembered=false;
   int ask_target=INT_MIN;
   if (arc_core_options.launch_chooser == ARC_LAUNCH_CHOOSER_ASK)
      (void)arc_choice_load(&discovery,&ask_remembered,&ask_target);
   if (option == ARC_LAUNCH_AUTO &&
       arc_core_options.launch_chooser == ARC_LAUNCH_CHOOSER_ASK &&
       selected >= 0 && arc_hd_scope.state == ARC_HD_COMPLETED &&
       arc_hd_scope.product_present &&
       arc_launch_chooser_init(&arc_chooser,&discovery,
                               ask_remembered,ask_target))
   {
      (void)arc_launch_chooser_add_installed(&arc_chooser,
         &arc_hd_scope.product,arc_hd_scope.attempt,ask_target == -2);
      arc_chooser_discovery=discovery;
      arc_launch_log(&discovery,-1,"chooser asked");
      return true;
   }
   if (option == ARC_LAUNCH_AUTO && selected >= 0 &&
       arc_core_options.launch_chooser == ARC_LAUNCH_CHOOSER_REMEMBER &&
       arc_hd_scope.state == ARC_HD_COMPLETED &&
       arc_hd_scope.product_present)
   {
      bool remembered=false;
      int remembered_target=INT_MIN;
      (void)arc_choice_load(&discovery,&remembered,&remembered_target);
      if (remembered && remembered_target == -1)
      { arc_launch_log(&discovery,-1,"chooser remembered desktop"); return false; }
      if (remembered && remembered_target >= 0)
      {
         selected=remembered_target;
         reason="chooser remembered target";
         remembered_floppy=true;
      }
   }
   if (option >= ARC_LAUNCH_APP1)
   {
      unsigned ordinal = (unsigned)(option - ARC_LAUNCH_APP1);
      unsigned candidate = arc_launch_candidate_index(&discovery, ordinal);
      if (!discovery.readable)
         snprintf(refusal, sizeof(refusal), "Application choice refused: inspection incomplete");
      else if (candidate == UINT_MAX)
         snprintf(refusal, sizeof(refusal), "Application choice refused: candidate %u does not exist", ordinal + 1u);
      else if (discovery.candidates[candidate].state != ARC_LAUNCH_CANDIDATE_APPLICATION)
         snprintf(refusal, sizeof(refusal), "Application choice refused: candidate %u has no !Run", ordinal + 1u);
      else if (!discovery.candidates[candidate].launchable)
         snprintf(refusal, sizeof(refusal),
                  discovery.candidates[candidate].parent.length &&
                  !discovery.candidates[candidate].addressable ?
                  "Application choice refused: candidate %u parent is not representable" :
                  "Application choice refused: candidate %u leaf is not launchable",
                  ordinal + 1u);
      else
      {
         selected = (int)candidate;
         if (!arc_launch_binding(&discovery, candidate, reselected,
                                 refusal, sizeof(refusal)))
         {
            char detail[sizeof(refusal)];
            snprintf(detail, sizeof(detail), "%s", refusal);
            snprintf(refusal, sizeof(refusal), "Application choice refused: %.120s", detail);
         }
         else
            reason = discovery.boot_option ?
               "explicit choice; disc !Boot skipped because helper is boot drive" :
               "explicit application choice";
      }
      if (refusal[0])
      {
         arc_message(refusal, RETRO_LOG_WARN);
         arc_launch_log(&discovery, -1, refusal);
         return false;
      }
   }
   else if (selected < 0)
   {
      unsigned verified_targets = 0u;
      for (unsigned i = 0u; i < discovery.candidate_count; ++i)
         if (arc_launch_candidate_is_target(&discovery.candidates[i]))
            ++verified_targets;
      bool chooser_eligible = arc_image_index == 0u &&
         (discovery.reason == ARC_LAUNCH_REASON_AMBIGUOUS ||
          discovery.reason == ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE) &&
         verified_targets >= 1u;
      if (discovery.nested_scope && !verified_targets)
         for (unsigned i = 0u; i < discovery.candidate_count; ++i)
            if (discovery.candidates[i].state == ARC_LAUNCH_CANDIDATE_APPLICATION &&
                !discovery.candidates[i].addressable)
            {
               arc_message("Application parent cannot be addressed uniquely.",
                           RETRO_LOG_WARN);
               break;
            }
      if (chooser_eligible)
      {
         bool remembered = false;
         int remembered_target = INT_MIN;
         (void)arc_choice_load(&discovery, &remembered, &remembered_target);
         if (remembered &&
             arc_core_options.launch_chooser == ARC_LAUNCH_CHOOSER_REMEMBER)
         {
            if (remembered_target == -2)
            {
               arc_hd_requested_mode=ARC_HD_HELPER_PRODUCT;
               bool published=arc_launch_helper_publish(&discovery,
                  arc_hd_selected,refusal,sizeof(refusal));
               arc_hd_requested_mode=ARC_HD_HELPER_FLOPPY;
               if (!published)
               {
                  arc_message(refusal[0]?refusal:
                     "Installed application helper publication failed.",
                     RETRO_LOG_WARN);
                  arc_launch_log(&discovery,-1,"installed helper publication failed");
                  return false;
               }
               arc_launch_log(&discovery,(int)arc_hd_selected,"hd-product");
               return false;
            }
            if (remembered_target >= 0 &&
                !arc_launch_helper_publish(&discovery,
                   (unsigned)remembered_target, refusal, sizeof(refusal)))
            {
               arc_message(refusal[0] ? refusal :
                  "Remembered launch helper publication failed.", RETRO_LOG_WARN);
               arc_launch_log(&discovery, -1,
                  refusal[0] ? refusal : "remembered helper publication failed");
               return false;
            }
            if (remembered_target >= 0 && discovery.scan_incomplete)
               arc_message("Application inspection incomplete; launching the remembered choice.",
                           RETRO_LOG_WARN);
            arc_launch_log(&discovery, remembered_target,
               remembered_target < 0 ? "chooser remembered desktop" :
                                       "chooser remembered target");
            return false;
         }
         if (arc_launch_chooser_init(&arc_chooser, &discovery, remembered,
                                     remembered_target))
         {
            if (arc_hd_scope.state == ARC_HD_COMPLETED &&
                arc_hd_scope.product_present)
               (void)arc_launch_chooser_add_installed(&arc_chooser,
                  &arc_hd_scope.product,arc_hd_scope.attempt,
                  remembered_target == -2);
            arc_chooser_discovery = discovery;
            arc_launch_log(&discovery, -1,
               arc_core_options.launch_chooser == ARC_LAUNCH_CHOOSER_ASK ?
                  "chooser asked" : "chooser shown");
            return true;
         }
      }
      if (discovery.reason == ARC_LAUNCH_REASON_AMBIGUOUS &&
          discovery.application_count >= 2u)
         arc_launch_candidates_message(&discovery, "No unique application: ", true);
      else if (discovery.reason == ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE &&
               discovery.candidate_count &&
               (!discovery.nested_scope || verified_targets))
         arc_launch_candidates_message(&discovery,
            "Application inspection incomplete: ", false);
      else if (discovery.nested_scope && discovery.scan_incomplete)
         arc_message("Application inspection incomplete; no verified launch target.",
                     RETRO_LOG_WARN);
      arc_launch_log(&discovery, -1, reason);
      return false;
   }
   if (option == ARC_LAUNCH_AUTO && !remembered_floppy &&
       arc_hd_scope_cached &&
       arc_hd_selected == (unsigned)selected &&
       arc_hd_scope.state == ARC_HD_COMPLETED &&
       arc_hd_scope.product_present)
      arc_hd_requested_mode=ARC_HD_HELPER_PRODUCT;
   bool published=arc_launch_helper_publish(&discovery,(unsigned)selected,
                                             refusal,sizeof(refusal));
   arc_hd_requested_mode=ARC_HD_HELPER_FLOPPY;
   if (!published)
   {
      arc_message(refusal[0] ? refusal : "Launch helper publication failed.",
                  RETRO_LOG_WARN);
      arc_launch_log(&discovery, -1,
         refusal[0] ? refusal : "helper publication failed");
      return false;
   }
   if (option >= ARC_LAUNCH_APP1 && discovery.scan_incomplete)
      arc_message("Application inspection incomplete; launching the verified choice.",
                  RETRO_LOG_WARN);
   arc_launch_log(&discovery, selected,
      arc_hd_mode == ARC_HD_HELPER_PRODUCT ? "hd-product" : reason);
   return false;
}

static void arc_disc_setup(void);
typedef enum {
   ARC_INSERT_PREFLIGHT,
   ARC_INSERT_MANUAL,
   ARC_INSERT_COMMIT
} ArcInsertPhase;

/* Manual insertion does both phases; automatic insertion prepares before its
 * zero-time eject/select/insert transaction and commits the prepared view. */
static bool arc_disk_insert_entry(ArcMediaEntry *entry, ArcInsertPhase phase,
                                  char *error, size_t error_size)
{
   if (phase != ARC_INSERT_COMMIT)
   {
      if ((!arc_engine_is_rpc() && !arc_machine_accepts_media(arc_session_machine, entry,
                                     error, error_size)) ||
          !arc_refresh_jfd(entry, arc_plan.disk_writes,
                           arc_core_options.jfd_write_policy, error, error_size))
      {
         if (phase == ARC_INSERT_PREFLIGHT) arc_jfd_free(&entry->jfd);
         return false;
      }
   }
   const char *tail_option = arc_option_value("arcdroid_short_tail", "missing");
   if (strcmp(tail_option, "missing") && strcmp(tail_option, "zero-filled"))
   {
      if (error && error_size)
         snprintf(error, error_size, "Invalid Short image tail option.");
      if (phase == ARC_INSERT_PREFLIGHT) arc_jfd_free(&entry->jfd);
      return false;
   }
   bool zero_tail = !strcmp(tail_option, "zero-filled");
   if (phase == ARC_INSERT_PREFLIGHT)
   {
      if (entry->info.kind == ARC_MEDIA_JFD)
      {
         if (!entry->jfd.overlay || entry->jfd.overlay->damaged ||
             entry->jfd.overlay->failed ||
             (entry->jfd.overlay->reason &&
              !strcmp(entry->jfd.overlay->reason, "overlay-unreadable")))
         {
            arc_jfd_free(&entry->jfd);
            return false;
         }
      }
      else
      {
         void *bytes = NULL;
         size_t size = 0u;
         bool readable = arc_io_read_all(entry->staged, &bytes, &size);
         free(bytes);
         if (!readable || size < entry->info.available_size) return false;
      }
      return true;
   }
   ArcEngineMedia media = arc_engine_media(entry);
   media.effective_extent = entry->info.short_image && !zero_tail ?
      entry->info.available_size : ARC_MEDIA_EXTENT_UNRESTRICTED;
   if (!arc_engine_media_insert(&media, error, error_size))
   {
      arc_jfd_free(&entry->jfd);
      if (error && error_size && !error[0])
         snprintf(error, error_size, "Unable to insert the selected floppy image.");
      return false;
   }
   arc_entry_tail_presentation(entry, zero_tail);
   arc_core_options.short_tail_zero = zero_tail;
   arc_preparing_zero_tail = zero_tail;
   arc_finish_jfd_preparation(entry, true, arc_core_options.jfd_write_policy);
   return true;
}
#if defined(ARCDROID_TEST_FAULTS)
void arc_libretro_test_disc_adffs_active(bool active)
{
   arc_adffs_active = active;
   arc_disc_setup();
}
static bool arc_test_disc_preflight_fail;
void arc_libretro_test_fail_disc_preflight(void)
{ arc_test_disc_preflight_fail = true; }
static void (*arc_test_disc_preflight_callback)(void);
static bool arc_test_disc_preflight_stale;
RETRO_API void arc_libretro_test_disc_after_preflight(void (*callback)(void),bool stale)
{ arc_test_disc_preflight_callback=callback;arc_test_disc_preflight_stale=stale; }
#endif
typedef enum {
   ARC_DISC_COMMIT_INSERTED, ARC_DISC_COMMIT_BUSY, ARC_DISC_COMMIT_FAILED
} ArcDiscCommit;
static ArcDiscCommit arc_disc_commit(unsigned candidate, uint64_t generation, ArcDiscSignal signal)
{
   if (arc_disc_mode != ARC_DISC_MODE_AUTO || !arc_loaded ||
       arc_session != ARC_SESSION_RUNNING || arc_adffs_active || arc_ejected ||
       (!arc_engine_is_rpc() && arc_hd_mode != ARC_HD_HELPER_FLOPPY) ||
       candidate >= arc_media_count || candidate == arc_image_index ||
       !arc_media[candidate].populated || generation != arc_disc_generation ||
       !arc_engine_disc_controller_idle()) return ARC_DISC_COMMIT_BUSY;
   ArcErrorBox box;
   arc_engine_error_snapshot(&box);
   if(arc_engine_is_rpc()){
      if(!arc_engine_rpc_observers_replace(signal))return ARC_DISC_COMMIT_BUSY;
   }else if (box.stage == ARC_ERROR_READY) return ARC_DISC_COMMIT_BUSY;
   ArcMediaEntry *entry = &arc_media[candidate];
   char error[512] = {0};
#if defined(ARCDROID_TEST_FAULTS)
   if (arc_test_disc_preflight_fail)
   { arc_test_disc_preflight_fail = false; goto failed; }
#endif
   if (!arc_disk_insert_entry(entry, ARC_INSERT_PREFLIGHT,
                              error, sizeof(error))) goto failed;
#if defined(ARCDROID_TEST_FAULTS)
   if(arc_test_disc_preflight_stale){arc_test_disc_preflight_stale=false;++arc_disc_generation;}
   if(arc_test_disc_preflight_callback){
      void (*callback)(void)=arc_test_disc_preflight_callback;
      arc_test_disc_preflight_callback=NULL;callback();
   }
#endif
   if(arc_engine_is_rpc() && (generation!=arc_disc_generation ||
      !arc_engine_disc_controller_idle() || !arc_engine_rpc_observers_replace(signal)))return ARC_DISC_COMMIT_BUSY;
   unsigned old_index = arc_image_index;
   ArcEngineMedia old_media = arc_engine_media(&arc_media[old_index]);
   if (!arc_engine_media_eject()) goto failed;
   arc_ejected = true;
   arc_image_index = candidate;
   if (!arc_disk_insert_entry(entry, ARC_INSERT_COMMIT,
                              error, sizeof(error)))
   {
      arc_image_index = old_index;
      if (arc_engine_media_insert(&old_media, NULL, 0u)) arc_ejected = false;
      else arc_ejected = true;
      goto failed;
   }
   arc_ejected = false;
   arc_jfd_free(&arc_media[old_index].jfd);
   if (!arc_disc_owned) arc_disc_launch_index = old_index;
   arc_disc_owned = true;
   arc_disc_previous_index = old_index;
   arc_disc_swapped_us = arc_engine_disc_time_us();
   arc_disc_invalidate();
   arc_engine_launch_outcome_cancel("media");
   arc_engine_boot_watch_cancel();
   arc_disc_setup();
   char notice[64];
   snprintf(notice, sizeof(notice), "Disc %u inserted", candidate + 1u);
   arc_disc_queue_notice(notice);
   return ARC_DISC_COMMIT_INSERTED;
failed:
   {
      char notice[64];
      snprintf(notice, sizeof(notice), "Disc %u could not be inserted",
               candidate + 1u);
      arc_disc_queue_notice(notice);
   }
   return ARC_DISC_COMMIT_FAILED;
}
#if defined(ARCDROID_TEST_FAULTS)
int arc_libretro_test_disc_commit(unsigned candidate, bool stale,
                                   bool helper_active, bool machine_refuse)
{
   ArcHdHelperMode saved_mode = arc_hd_mode;
   ArcMachine saved_machine = arc_session_machine;
   if (helper_active) arc_hd_mode = ARC_HD_HELPER_INSTALL;
   if (machine_refuse) arc_session_machine = ARC_MACHINE_A3000;
   ArcDiscCommit result = arc_disc_commit(candidate,
      stale ? arc_disc_generation - 1u : arc_disc_generation, ARC_DISC_SIGNAL_B);
   arc_hd_mode = saved_mode;
   arc_session_machine = saved_machine;
   return (int)result;
}
bool arc_libretro_test_disc_owned(void) { return arc_disc_owned; }
unsigned arc_libretro_test_decoded_jfd_count(void)
{
   unsigned count = 0u;
   for (unsigned i = 0u; i < arc_media_count; ++i)
      if (arc_media[i].jfd.decoded) ++count;
   return count;
}
const char *arc_libretro_test_disc_jfd_digest(unsigned image)
{
   return image < arc_media_count && arc_media[image].info.kind == ARC_MEDIA_JFD ?
      arc_media[image].digest.sha1 : NULL;
}
#endif

static bool arc_disc_observation(void *opaque, const char *path,
   unsigned candidate, unsigned observation, bool trigger)
{
   (void)opaque;
   bool retry = false;
   char object[256];
   snprintf(object, sizeof(object), "%s", path);
   const char *action = "ignored", *reason = "first-observation";
   if (trigger)
   {
      uint64_t now = arc_engine_disc_time_us();
      bool backwards = arc_disc_back_request(candidate,now);
      if (arc_disc_mode == ARC_DISC_MODE_HINT || backwards ||
          (arc_engine_is_rpc() && !arc_engine_rpc_observers_capability(ARC_RPC_CELL_REPLACE_B)))
      {
         char notice[64];
         snprintf(notice, sizeof(notice), "Insert disc %u with Disk Control",
                  candidate + 1u);
         arc_disc_queue_notice(notice);
         action = "hint";
         reason = backwards ? "back-request" : "option";
      }
      else if (arc_disc_mode == ARC_DISC_MODE_AUTO)
      {
         ArcDiscCommit commit = arc_disc_commit(candidate,
                                                arc_disc_watch_generation, ARC_DISC_SIGNAL_B);
         retry = commit == ARC_DISC_COMMIT_BUSY;
         action = commit == ARC_DISC_COMMIT_INSERTED ? "inserted" : "ignored";
         reason = commit == ARC_DISC_COMMIT_INSERTED ? "unique" :
                  commit == ARC_DISC_COMMIT_FAILED ? "commit-failed" : "precondition";
      }
      else reason = "off";
   }
   bool log_line = true;
   unsigned suppressed = 0u;
   if (observation == 1u)
   {
      ArcDiscRequest request;
      arc_engine_disc_snapshot(&request);
      ArcDiscFirstLog *entry = NULL, *oldest = NULL;
      for (unsigned i = 0u; i < ARC_DISC_IDENTITY_CAPACITY; ++i)
      {
         ArcDiscFirstLog *slot = &arc_disc_first_logs[i];
         if (slot->valid && slot->continuation == request.continuation &&
             slot->sp == request.sp && !strcmp(slot->object, object))
         { entry = slot; break; }
         if (!oldest || !slot->valid ||
             (oldest->valid && slot->last_used < oldest->last_used))
            oldest = slot;
      }
      uint64_t now = arc_engine_disc_time_us();
      if (!entry)
      {
         entry = oldest;
         memset(entry, 0, sizeof(*entry));
         entry->valid = true;
         strcpy(entry->object, object);
         entry->continuation = request.continuation;
         entry->sp = request.sp;
         entry->logged_us = now;
      }
      else if (now >= entry->logged_us &&
               now - entry->logged_us < UINT64_C(10000000))
      { ++entry->suppressed; log_line = false; }
      else
      {
         suppressed = entry->suppressed;
         entry->suppressed = 0u;
         entry->logged_us = now;
      }
      entry->last_used = ++arc_disc_first_log_clock;
   }
   else if (observation == 2u)
   {
      uint64_t now = arc_engine_disc_time_us();
      if (arc_disc_b_log_active && now >= arc_disc_b_logged_us &&
          now - arc_disc_b_logged_us < UINT64_C(10000000))
      { ++arc_disc_b_suppressed; log_line = false; }
      else
      {
         arc_disc_b_log_active = true;
         arc_disc_b_logged_us = now;
         suppressed = arc_disc_b_suppressed;
         arc_disc_b_suppressed = 0u;
      }
   }
   if (arc_log && log_line)
   {
      if (suppressed)
         arc_log(RETRO_LOG_INFO,
            "Disc request: signal=B object=%s candidate=%u observation=%u action=%s reason=%s suppressed=%u\n",
            object, candidate + 1u, observation, action, reason, suppressed);
      else
         arc_log(RETRO_LOG_INFO,
            "Disc request: signal=B object=%s candidate=%u observation=%u action=%s reason=%s\n",
            object, candidate + 1u, observation, action, reason);
   }
   return retry;
}

static void arc_disc_setup(void)
{
   char inserted[11] = {0};
   bool enabled = arc_disc_mode != ARC_DISC_MODE_OFF &&
      arc_media_count >= 2u && !arc_adffs_active && !arc_ejected &&
      arc_image_index < arc_media_count &&
      arc_session == ARC_SESSION_RUNNING &&
      (arc_engine_is_rpc() || arc_hd_mode == ARC_HD_HELPER_FLOPPY);
   ArcGuestName disc = {0};
   if (enabled && arc_disc_name(arc_image_index, &disc) && disc.length <= 10u)
   {
      bool printable = true;
      for (unsigned i = 0u; i < disc.length; ++i)
         if (disc.bytes[i] < 0x20u || disc.bytes[i] > 0x7eu)
            printable = false;
      if (printable)
      {
         memcpy(inserted, disc.bytes, disc.length);
         inserted[disc.length] = 0;
      }
   }
   arc_engine_disc_configure(enabled, inserted,
      arc_disc_resolve_candidate, arc_disc_observation, NULL);
}

static void arc_disc_option_refresh(void)
{
   const char *value = arc_option_value("arcdroid_disc_requests", "auto");
   ArcDiscMode mode = !strcmp(value, "hint") ? ARC_DISC_MODE_HINT :
      !strcmp(value, "off") ? ARC_DISC_MODE_OFF : ARC_DISC_MODE_AUTO;
   if (mode == arc_disc_mode) return;
   arc_disc_mode = mode;
   arc_disc_notice_read = arc_disc_notice_write = 0u;
   arc_disc_invalidate();
   arc_disc_setup();
}

/* Host boundary only: a changed decision is immediate; repeats are bounded
 * by virtual time, including busy requests that retry every frame. */
static void arc_rpc_disc_log(const char *action,const char *reason,const char *candidate)
{
   static struct {char line[384];uint64_t time,generation;} history[4];
   if(!arc_engine_is_rpc() || !arc_log)return;
   unsigned slot=!strcmp(action,"resolved")?1u:!strcmp(action,"busy")?2u:
      (!strcmp(action,"inserted") || !strcmp(action,"hint") || !strcmp(action,"ignored"))?3u:0u;
   /* Waiting/terminal decisions end the previous attempt. A later retry
    * becomes a fresh decision even when its candidate is the same. */
   if(slot==0u || slot==3u){history[1].line[0]=0;history[2].line[0]=0;}
   ArcRpcDiscStatus status;arc_engine_rpc_disc_status(&status);
   char line[384];snprintf(line,sizeof(line),
      "RPC disc: signal=%s gate=%s name=%s active=%u notified=%u replacement=%s candidate=%s action=%s reason=%s observation=%s:%s\n",
      status.signal,status.gate,status.name,status.active,status.notified,status.replacement,candidate,action,reason,status.last_signal,status.last_reason);
   uint64_t time=arc_engine_disc_time_us(),session=arc_engine_session_generation();
   if(time<history[slot].time)history[slot].time=time;
   bool busy_repeat=slot==2u && time-history[slot].time>=UINT64_C(2000000);
   if(session!=history[slot].generation || strcmp(line,history[slot].line) || busy_repeat){
      arc_log(RETRO_LOG_INFO,"%s",line);snprintf(history[slot].line,sizeof(history[slot].line),"%s",line);history[slot].time=time;history[slot].generation=session;
   }
}

static void arc_disc_boundary(void)
{
   arc_disc_option_refresh();
   arc_disc_present_notice();
   if (arc_disc_mode == ARC_DISC_MODE_OFF || arc_adffs_active ||
       arc_session != ARC_SESSION_RUNNING || arc_ejected ||
       arc_media_count < 2u) {
      arc_rpc_disc_log("inactive",arc_disc_mode==ARC_DISC_MODE_OFF?"option-off":arc_adffs_active?"adffs":
         arc_session!=ARC_SESSION_RUNNING?"session":arc_ejected?"ejected":"single-media","none");return;
   }
   ArcDiscRequest request;
   arc_engine_disc_snapshot(&request);
   if (!request.a_active || request.a_notified) {
      arc_rpc_disc_log("waiting",request.a_notified?"already-notified":"signal-gate","none");return;
   }
   unsigned candidate = UINT_MAX, count = 0u;
   bool unknown = false;
   for (unsigned i = 0u; i < arc_media_count; ++i)
   {
      if (i == arc_image_index || !arc_media[i].populated) continue;
      ArcGuestName name = {0};
      if (!arc_disc_name(i, &name)) { unknown = true; continue; }
      if (arc_disc_ascii_equal(request.a_name, &name))
      { candidate = i; ++count; }
   }
   const char *candidate_text = count > 1u ? "ambiguous" : "none";
   char candidate_number[12];
   if (count == 1u && !unknown)
   {
      snprintf(candidate_number, sizeof(candidate_number), "%u", candidate + 1u);
      candidate_text = candidate_number;
   }
   const char *action = "ignored", *reason = "no-candidate";
   if (unknown) reason = "unknown-image";
   else if (count > 1u) reason = "ambiguous";
   else if (count == 1u)
   {
      uint64_t now = arc_engine_disc_time_us();
      bool backwards = arc_disc_back_request(candidate,now);
      if (arc_disc_mode == ARC_DISC_MODE_HINT || backwards ||
          (arc_engine_is_rpc() && !arc_engine_rpc_observers_capability(ARC_RPC_CELL_REPLACE_A)))
      {
         char notice[64];
         snprintf(notice, sizeof(notice), "Insert disc %u with Disk Control",
                  candidate + 1u);
         arc_disc_queue_notice(notice);
         action = "hint";
         reason = backwards ? "back-request" : "option";
      }
      else
      {
         arc_rpc_disc_log("resolved","unique",candidate_text);
         uint64_t attempt_generation=arc_disc_generation;
         ArcDiscCommit commit = arc_disc_commit(candidate, arc_disc_generation, ARC_DISC_SIGNAL_A);
         if (commit == ARC_DISC_COMMIT_BUSY) {
            arc_rpc_disc_log("busy",attempt_generation!=arc_disc_generation?"stale-generation":
               !arc_engine_disc_controller_idle()?"controller-busy":"replacement-gate",candidate_text);
            return; /* Retry next boundary. */
         }
         action = commit == ARC_DISC_COMMIT_INSERTED ? "inserted" : "ignored";
         reason = commit == ARC_DISC_COMMIT_INSERTED ? "unique" : "commit-failed";
      }
   }
   arc_rpc_disc_log(action,reason,candidate_text);
   if (arc_log) arc_log(RETRO_LOG_INFO,
      "Disc request: signal=A name=%s candidate=%s action=%s reason=%s\n",
      request.a_name, candidate_text, action, reason);
   arc_engine_disc_a_notified();
}

static bool arc_disk_set_eject(bool ejected)
{
   arc_disc_owned = false;
   arc_disc_notice_read = arc_disc_notice_write = 0u;
   arc_engine_disc_clear();
   arc_disc_invalidate();
   char error[512] = {0};
   arc_engine_launch_outcome_cancel("media");
   if (!arc_loaded || arc_session != ARC_SESSION_RUNNING || arc_adffs_active) return false;
   if (ejected == arc_ejected) return true;
   arc_engine_boot_watch_cancel();
   if (ejected)
   {
      if (!arc_engine_media_eject()) return false;
      if (arc_image_index < arc_media_count)
         arc_jfd_free(&arc_media[arc_image_index].jfd);
      arc_ejected = true;
      arc_disc_setup();
      return true;
   }
   if (arc_image_index < arc_media_count && arc_media[arc_image_index].populated)
   {
      ArcMediaEntry *entry = &arc_media[arc_image_index];
      if (!arc_disk_insert_entry(entry, ARC_INSERT_MANUAL,
                                 error, sizeof(error)))
      {
         arc_message(error, RETRO_LOG_ERROR);
         return false;
      }
   }
   arc_ejected = false;
   arc_disc_setup();
   return true;
}
static bool arc_disk_get_eject(void) { return arc_adffs_active ? false : arc_ejected; }
static unsigned arc_disk_get_index(void) { return arc_adffs_active ? 0u : arc_image_index; }
static bool arc_disk_set_index(unsigned index)
{
   arc_disc_owned = false;
   arc_disc_notice_read = arc_disc_notice_write = 0u;
   arc_engine_disc_clear();
   arc_disc_invalidate();
   arc_engine_launch_outcome_cancel("media");
   if (!arc_loaded || arc_session != ARC_SESSION_RUNNING || arc_adffs_active || !arc_ejected) return false;
   if (index < arc_media_count && !arc_media[index].populated) return false;
   arc_engine_boot_watch_cancel();
   arc_image_index = index < arc_media_count ? index : arc_media_count;
   return true;
}
static unsigned arc_disk_get_count(void) { return arc_adffs_active ? 1u : arc_media_count; }
static bool arc_disk_replace(unsigned index, const struct retro_game_info *info)
{
   arc_disc_owned = false;
   arc_disc_notice_read = arc_disc_notice_write = 0u;
   arc_engine_disc_clear();
   arc_disc_invalidate();
   arc_engine_launch_outcome_cancel("media");
   if (!arc_loaded || arc_session != ARC_SESSION_RUNNING || arc_adffs_active || !arc_ejected ||
       index >= arc_media_count) return false;
   arc_engine_boot_watch_cancel();
   if (!info)
   {
      bool selected = arc_image_index == index;
      arc_jfd_free(&arc_media[index].jfd);
      memmove(&arc_media[index], &arc_media[index + 1u],
              (arc_media_count - index - 1u) * sizeof(arc_media[0]));
      memset(&arc_media[arc_media_count - 1u], 0, sizeof(arc_media[0]));
      --arc_media_count;
      if (selected) arc_image_index = arc_media_count;
      else if (arc_image_index > index && arc_image_index <= arc_media_count) --arc_image_index;
      else if (arc_image_index > arc_media_count) arc_image_index = arc_media_count;
      return true;
   }
   ArcMediaEntry replacement;
   char error[512] = {0};
   char zip_path[ARC_MEDIA_PATH_CAPACITY];
   char zip_member[256];
   bool zip = info->path &&
      arc_zip_content_path(info->path, zip_path, zip_member);
   if (zip)
      snprintf(error, sizeof(error),
               "Zip files cannot be added through disk control; load the zip as content.");
   if (!info->path || zip ||
       !arc_prepare_source(info->path, NULL, arc_plan.disk_writes,
                                           true, NULL, &replacement,
                                           error, sizeof(error)))
   {
      if (!arc_media[index].populated && index + 1u == arc_media_count)
         --arc_media_count;
      arc_message(error[0] ? error : "Replacement floppy image is invalid.", RETRO_LOG_ERROR);
      return false;
   }
   if (!arc_engine_is_rpc() && !arc_machine_accepts_media(arc_session_machine, &replacement,
                                  error, sizeof(error)))
   {
      arc_discard_candidate(&replacement);
      if (!arc_media[index].populated && index + 1u == arc_media_count)
         --arc_media_count;
      arc_message(error, RETRO_LOG_ERROR);
      return false;
   }
   arc_jfd_free(&arc_media[index].jfd);
   arc_media[index] = replacement;
   return true;
}
static bool arc_disk_add(void)
{
   arc_disc_owned = false;
   arc_disc_notice_read = arc_disc_notice_write = 0u;
   arc_engine_disc_clear();
   arc_disc_invalidate();
   arc_engine_launch_outcome_cancel("media");
   if (!arc_loaded || arc_session != ARC_SESSION_RUNNING || arc_adffs_active ||
       arc_media_count >= ARC_PLAYLIST_MAX_ENTRIES) return false;
   arc_engine_boot_watch_cancel();
   memset(&arc_media[arc_media_count++], 0, sizeof(arc_media[0]));
   return true;
}
static bool arc_disk_get_path(unsigned index, char *path, size_t length)
{
   if (arc_adffs_active && index != 0u) return false;
   if (!path || index >= arc_media_count || !arc_media[index].populated) return false;
   size_t needed = strlen(arc_media[index].source) + 1u;
   if (needed > length) return false;
   memcpy(path, arc_media[index].source, needed);
   return true;
}
static bool arc_disk_get_label(unsigned index, char *label, size_t length)
{
   if (arc_adffs_active && index != 0u) return false;
   if (!label || index >= arc_media_count || !arc_media[index].populated) return false;
   size_t needed = strlen(arc_media[index].label) + 1u;
   if (needed > length) return false;
   memcpy(label, arc_media[index].label, needed);
   return true;
}

static void arc_keyboard(bool down, unsigned keycode, uint32_t character,
                         uint16_t modifiers)
{
   arc_engine_launch_outcome_cancel("input");
   if (down && arc_boot_attempt) arc_engine_boot_watch_cancel();
   (void)character;
   (void)modifiers;
   if (keycode < ARC_INPUT_KEY_COUNT) arc_keyboard_raw[keycode] = down;
   if (arc_session == ARC_SESSION_CHOOSING || arc_chooser_release_gate) return;
   if (keycode == RETROK_MENU || keycode == RETROK_LMETA)
   {
      if (keycode == RETROK_MENU) arc_keyboard_menu = down;
      else arc_keyboard_lmeta = down;
      arc_input_source_buttons(&arc_inputs, ARC_INPUT_SOURCE_KEYBOARD,
                               (arc_keyboard_menu || arc_keyboard_lmeta) ? 4u : 0u);
      return;
   }
   bool supported = (keycode >= RETROK_a && keycode <= RETROK_z) ||
                    (keycode >= RETROK_0 && keycode <= RETROK_9) ||
                    (keycode >= RETROK_KP0 && keycode <= RETROK_KP9) ||
                    (keycode >= RETROK_F1 && keycode <= RETROK_F12);
   switch (keycode)
   {
      case RETROK_ESCAPE: case RETROK_BACKQUOTE: case RETROK_MINUS:
      case RETROK_EQUALS: case RETROK_BACKSPACE: case RETROK_TAB:
      case RETROK_LEFTBRACKET: case RETROK_RIGHTBRACKET: case RETROK_RETURN:
      case RETROK_SEMICOLON: case RETROK_QUOTE: case RETROK_SLASH:
      case RETROK_BACKSLASH: case RETROK_COMMA: case RETROK_PERIOD:
      case RETROK_SPACE: case RETROK_INSERT: case RETROK_DELETE:
      case RETROK_HOME: case RETROK_END: case RETROK_PAGEUP:
      case RETROK_PAGEDOWN: case RETROK_LEFT: case RETROK_RIGHT:
      case RETROK_UP: case RETROK_DOWN: case RETROK_KP_DIVIDE:
      case RETROK_KP_MULTIPLY: case RETROK_KP_MINUS: case RETROK_KP_PLUS:
      case RETROK_KP_PERIOD: case RETROK_KP_ENTER: case RETROK_LSHIFT:
      case RETROK_RSHIFT: case RETROK_LCTRL: case RETROK_RCTRL:
      case RETROK_CAPSLOCK: case RETROK_SCROLLOCK: case RETROK_NUMLOCK:
      case RETROK_LALT: case RETROK_RALT: case RETROK_PAUSE:
         supported = true;
         break;
      default:
         break;
   }
   arc_input_source_key(&arc_inputs, ARC_INPUT_SOURCE_KEYBOARD,
                        keycode, down, supported);
}

static void arc_touch_reset(void)
{
   memset(&arc_touch, 0, sizeof(arc_touch));
   arc_input_source_buttons(&arc_inputs, ARC_INPUT_SOURCE_TOUCH, 0u);
}

static void arc_input_sampling_reset(bool release_engine_levels)
{
   if (release_engine_levels) arc_input_release_all(&arc_inputs);
   else arc_input_reset_frontend(&arc_inputs);
   memset(arc_left_sticks, 0, sizeof(arc_left_sticks));
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      arc_left_stick_await(port);
   memset(arc_pad_keys, 0, sizeof(arc_pad_keys));
   memset(arc_pad_neutral_gate, 0, sizeof(arc_pad_neutral_gate));
   arc_pad_pointer_buttons = 0u;
   arc_keyboard_menu = arc_keyboard_lmeta = false;
   arc_stick_x = arc_stick_y = 0;
   arc_stick_precision = false;
   arc_stick_fraction_x = arc_stick_fraction_y = 0.0L;
   memset(&arc_touch, 0, sizeof(arc_touch));
   memset(arc_osk_pad_levels, 0, sizeof(arc_osk_pad_levels));
   arc_osk_toggle_level = false;
}

static void arc_osk_restore_gameplay(void)
{
   arc_left_stick_await(0u);
   arc_pad_neutral_gate[0] = true;
   memset(arc_pad_keys[0], 0, sizeof(arc_pad_keys[0]));
   arc_pad_pointer_buttons = 0u;
   arc_touch_reset();
   /* A contact owned by the modal OSK must not become a fresh trackpad
    * gesture when gameplay resumes.  arc_touch_poll clears this only after
    * observing a fully neutral pointer sample. */
   arc_touch.blocked_until_zero = true;
   arc_stick_x = arc_stick_y = 0;
   arc_stick_fraction_x = arc_stick_fraction_y = 0.0L;
}

static void arc_osk_presentation_failure(void)
{
   arc_osk_close(&arc_osk, &arc_inputs);
   arc_error_pad_gate = true;
   arc_osk.disabled = true;
   arc_osk_present_dirty = true;
   arc_osk_restore_gameplay();
   if (!arc_osk_failure_reported)
   {
      arc_message("On-screen keyboard disabled: unable to allocate its presentation surface.",
                  RETRO_LOG_ERROR);
      arc_osk_failure_reported = true;
   }
}

static bool arc_osk_prepare_surface(unsigned width, unsigned height)
{
   if (!width || !height || width > 4096u || height > 2048u ||
       (size_t)width > SIZE_MAX / height) return false;
   size_t needed = (size_t)width * height;
   if (needed <= arc_osk_surface_pixels && arc_osk_surface) return true;
#if defined(ARCDROID_TEST_FAULTS)
   if (arc_osk_fail_allocations)
   {
      --arc_osk_fail_allocations;
      return false;
   }
#endif
   uint32_t *grown = realloc(arc_osk_surface, needed * sizeof(*grown));
   if (!grown) return false;
   arc_osk_surface = grown;
   arc_osk_surface_pixels = needed;
   return true;
}

static void arc_osk_free_surface(void)
{
   free(arc_osk_surface);
   arc_osk_surface = NULL;
   arc_osk_surface_pixels = 0u;
}

static void arc_osk_report_too_narrow(void)
{
   if (!arc_osk_too_narrow_reported)
   {
      arc_message("On-screen keyboard unavailable below 320 pixels wide.",
                  RETRO_LOG_WARN);
      arc_osk_too_narrow_reported = true;
   }
}

#if defined(ARCDROID_TEST_FAULTS)
static uint64_t arc_osk_source_hash(const ArcEngineFrame *frame)
{
   uint64_t hash = UINT64_C(1469598103934665603);
   for (unsigned row = 0u; row < frame->height; ++row)
   {
      const uint8_t *bytes = (const uint8_t *)frame->pixels +
         (size_t)row * frame->pitch;
      for (size_t byte = 0u; byte < (size_t)frame->width * sizeof(uint32_t); ++byte)
      {
         hash ^= bytes[byte];
         hash *= UINT64_C(1099511628211);
      }
   }
   return hash;
}
#endif

static void arc_osk_set_visible(bool visible)
{
   if (visible)
   {
      if (arc_osk.disabled) return;
      if (arc_video_width < 320u)
      {
         arc_osk_report_too_narrow();
         return;
      }
      if (!arc_osk_prepare_surface(arc_video_width, arc_video_height))
      {
         arc_osk_presentation_failure();
         return;
      }
      if (!arc_osk_open(&arc_osk, arc_video_width, arc_video_height)) return;
      arc_input_clear_source(&arc_inputs, ARC_INPUT_SOURCE_PAD0);
      arc_input_clear_source(&arc_inputs, ARC_INPUT_SOURCE_TOUCH);
      arc_osk_restore_gameplay();
   }
   else
   {
      arc_osk_close(&arc_osk, &arc_inputs);
      arc_osk_restore_gameplay();
      arc_error_pad_gate = true;
   }
   arc_osk_present_dirty = true;
}

#if defined(ARCDROID_TEST_FAULTS)
void arc_libretro_test_probe_osk_geometry(unsigned width, unsigned height)
{
   unsigned saved_width = arc_video_width;
   unsigned saved_height = arc_video_height;
   arc_video_width = width;
   arc_video_height = height;
   arc_osk_set_visible(true);
   arc_video_width = saved_width;
   arc_video_height = saved_height;
}

unsigned arc_libretro_test_osk_focus(void) { return arc_osk.focus; }

bool arc_libretro_test_osk_focus_key(unsigned key)
{
   if (!arc_osk.visible) return false;
   for (unsigned i = 0u; i < arc_osk.key_count; ++i)
      if (arc_osk.keys[i].key == key)
      {
         arc_osk.focus = i;
         arc_osk_present_dirty = true;
         return true;
      }
   return false;
}

bool arc_libretro_test_osk_key_center(unsigned key, unsigned *x, unsigned *y)
{
   if (!arc_osk.visible || !x || !y) return false;
   for (unsigned i = 0u; i < arc_osk.key_count; ++i)
      if (arc_osk.keys[i].key == key)
      {
         *x = arc_osk.keys[i].x + arc_osk.keys[i].width / 2u;
         *y = arc_osk.keys[i].y + arc_osk.keys[i].height / 2u;
         return true;
      }
   return false;
}
#endif

static int arc_pointer_to_pixel(int value, unsigned extent)
{
   int64_t scaled = ((int64_t)value + 32768) * extent;
   int result = (int)(scaled / 65536);
   if (result < 0) result = 0;
   if (extent && result >= (int)extent) result = (int)extent - 1;
   return result;
}

/* Shared sampling for gameplay and modal OSK navigation, at most once per
 * input poll. Routing changes reset only the stick's contribution. */
static unsigned arc_sample_left_stick(unsigned port)
{
   ArcLeftStick *stick = &arc_left_sticks[port];
   bool eligible = arc_core_options.left_stick_dpad[port] &&
      (arc_port_device[port] == ARC_DEVICE_KEYS ||
       arc_port_device[port] == RETRO_DEVICE_JOYPAD) &&
      (port || arc_core_options.pointer_stick != ARC_POINTER_STICK_LEFT);
   bool visible = !port && arc_osk.visible;
   bool joystick = arc_loaded && arc_plan.a3010_joystick;
   bool changed = !stick->routing_valid || eligible != stick->eligible ||
      visible != stick->osk_visible || joystick != stick->joystick;
   for (unsigned input = 0u; input < 4u; ++input)
   {
      const ArcInputBinding *binding = &arc_input_resolution.binding[port][input];
      bool automatic = arc_core_options.pad[port][input].automatic;
      if (binding->bound != stick->binding[input].bound ||
          binding->key != stick->binding[input].key ||
          automatic != stick->automatic[input]) changed = true;
      stick->binding[input] = *binding;
      stick->automatic[input] = automatic;
   }
   stick->routing_valid = true;
   stick->eligible = eligible;
   stick->osk_visible = visible;
   stick->joystick = joystick;
   if (changed) arc_left_stick_await(port);
   if (!eligible) return 0u;
   if (stick->polled) return stick->directions;
   stick->polled = true;
   int32_t x = arc_input_state(port, RETRO_DEVICE_ANALOG,
      RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
   int32_t y = arc_input_state(port, RETRO_DEVICE_ANALOG,
      RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
   if (stick->awaiting)
   {
      bool neutral = x > -13107 && x < 13107 && y > -13107 && y < 13107;
      stick->neutral_polls = neutral ? stick->neutral_polls + 1u : 0u;
      if (stick->neutral_polls >= 2u) stick->awaiting = false;
      return 0u;
   }
   stick->directions = arc_input_stick_directions((int16_t)x, (int16_t)y,
                                                  stick->directions);
   return stick->directions;
}

static void arc_poll_osk_controls(void)
{
   if (!arc_input_state) return;
   if (arc_osk_advance(&arc_osk, arc_guest_time_us))
      arc_osk_present_dirty = true;
   unsigned toggle_id = arc_core_options.osk_toggle == ARC_OSK_TOGGLE_R3 ?
      RETRO_DEVICE_ID_JOYPAD_R3 : RETRO_DEVICE_ID_JOYPAD_L3;
   bool toggle = arc_core_options.osk_toggle != ARC_OSK_TOGGLE_OFF &&
      arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u, toggle_id) != 0;
   if (toggle) arc_boot_input = true;
   if (toggle && !arc_osk_toggle_level)
      arc_osk_set_visible(!arc_osk.visible);
   arc_osk_toggle_level = toggle;
   if (!arc_osk.visible) return;

   static const unsigned ids[] = {
      RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y,
      RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT
   };
   bool level[10];
   for (unsigned i = 0u; i < 10u; ++i)
   {
      level[i] = arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u, ids[i]) != 0;
      if (level[i]) arc_boot_input = true;
   }
   unsigned directions = arc_sample_left_stick(0u);
   for (unsigned input = 0u; input < 4u; ++input)
      level[input] |= (directions & (1u << input)) != 0u;
   if (directions) arc_boot_input = true;
   int dx = level[ARC_PAD_LEFT] ? -1 : level[ARC_PAD_RIGHT] ? 1 : 0;
   int dy = level[ARC_PAD_UP] ? -1 : level[ARC_PAD_DOWN] ? 1 : 0;
   bool direction_edge =
      (level[ARC_PAD_LEFT] && !arc_osk_pad_levels[ARC_PAD_LEFT]) ||
      (level[ARC_PAD_RIGHT] && !arc_osk_pad_levels[ARC_PAD_RIGHT]) ||
      (level[ARC_PAD_UP] && !arc_osk_pad_levels[ARC_PAD_UP]) ||
      (level[ARC_PAD_DOWN] && !arc_osk_pad_levels[ARC_PAD_DOWN]);
   unsigned old_focus = arc_osk.focus;
   if (dx || dy) arc_osk_move(&arc_osk, dx, dy, arc_guest_time_us, direction_edge);
   if (old_focus != arc_osk.focus) arc_osk_present_dirty = true;
   if (level[ARC_PAD_A] && !arc_osk_pad_levels[ARC_PAD_A])
   {
      (void)arc_osk_press_focused(&arc_osk, &arc_inputs);
      arc_osk_present_dirty = true;
   }
   if (!level[ARC_PAD_A] && arc_osk_pad_levels[ARC_PAD_A])
      arc_osk_release_activation(&arc_osk);
   if (level[ARC_PAD_X] && !arc_osk_pad_levels[ARC_PAD_X])
   {
      (void)arc_osk_shortcut(&arc_osk, &arc_inputs, RETROK_BACKSPACE);
      arc_osk_present_dirty = true;
   }
   if (level[ARC_PAD_Y] && !arc_osk_pad_levels[ARC_PAD_Y])
   {
      (void)arc_osk_shortcut(&arc_osk, &arc_inputs, RETROK_RETURN);
      arc_osk_present_dirty = true;
   }
   if (level[ARC_PAD_SELECT] && !arc_osk_pad_levels[ARC_PAD_SELECT])
   {
      arc_osk.transparent = !arc_osk.transparent;
      arc_osk_present_dirty = true;
   }
   if ((level[ARC_PAD_B] && !arc_osk_pad_levels[ARC_PAD_B]) ||
       (level[ARC_PAD_START] && !arc_osk_pad_levels[ARC_PAD_START]))
   {
      memcpy(arc_osk_pad_levels, level, sizeof(level));
      arc_osk_set_visible(false);
      return;
   }
   memcpy(arc_osk_pad_levels, level, sizeof(level));

   bool down = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                               RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
   if (down) arc_boot_input = true;
   bool offscreen = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                                    RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN) != 0;
   int raw_x = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                               RETRO_DEVICE_ID_POINTER_X);
   int raw_y = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                               RETRO_DEVICE_ID_POINTER_Y);
   bool before_contact = arc_osk.contact;
   unsigned before_focus = arc_osk.focus;
   if (offscreen || raw_x == -32768 || raw_y == -32768)
   {
      /* Keep an offscreen held contact down long enough to invalidate its
       * capture; treating it as a release would activate the old key. */
      if (arc_osk.contact)
      {
         (void)arc_osk_touch(&arc_osk, -1, -1, true, &arc_inputs);
         if (!down) (void)arc_osk_touch(&arc_osk, -1, -1, false, &arc_inputs);
      }
   }
   else
      (void)arc_osk_touch(&arc_osk, arc_pointer_to_pixel(raw_x, arc_video_width),
                          arc_pointer_to_pixel(raw_y, arc_video_height), down,
                          &arc_inputs);
   if (before_contact != arc_osk.contact || before_focus != arc_osk.focus)
      arc_osk_present_dirty = true;
}

static void arc_poll_keyboard_snapshot(void)
{
   if (!arc_keyboard_polling || !arc_input_state) return;
   size_t count = 0u;
   const ArcInputKey *keys = arc_input_supported_keys(&count);
   bool seen[ARC_INPUT_KEY_COUNT] = {false};
   for (size_t i = 0u; i < count; ++i)
   {
      unsigned key = keys[i].key;
      if (key >= ARC_INPUT_KEY_COUNT || seen[key]) continue;
      seen[key] = true;
      bool down = arc_input_state(0u, RETRO_DEVICE_KEYBOARD, 0u, key) != 0;
      arc_input_source_key(&arc_inputs, ARC_INPUT_SOURCE_KEYBOARD, key, down, true);
   }
   static const unsigned extra[] = {RETROK_NUMLOCK, RETROK_SCROLLOCK, RETROK_PAUSE};
   for (unsigned i = 0u; i < sizeof(extra) / sizeof(extra[0]); ++i)
   {
      unsigned key = extra[i];
      bool down = arc_input_state(0u, RETRO_DEVICE_KEYBOARD, 0u, key) != 0;
      arc_input_source_key(&arc_inputs, ARC_INPUT_SOURCE_KEYBOARD, key, down, true);
   }
   arc_keyboard_menu = arc_input_state(0u, RETRO_DEVICE_KEYBOARD, 0u,
                                        RETROK_MENU) != 0;
   arc_keyboard_lmeta = arc_input_state(0u, RETRO_DEVICE_KEYBOARD, 0u,
                                         RETROK_LMETA) != 0;
   arc_input_source_buttons(&arc_inputs, ARC_INPUT_SOURCE_KEYBOARD,
                            (arc_keyboard_menu || arc_keyboard_lmeta) ? 4u : 0u);
}

static uint64_t arc_touch_distance_squared(const ArcTouchContact *contact,
                                           int x, int y)
{
   int64_t dx = (int64_t)x - contact->start_x;
   int64_t dy = (int64_t)y - contact->start_y;
   return (uint64_t)(dx * dx + dy * dy);
}

static bool arc_touch_tap_valid(const ArcTouchContact *contact, uint64_t now)
{
   return contact->valid && now - contact->start_us <= UINT64_C(250000) &&
          arc_touch_distance_squared(contact, contact->x, contact->y) <=
             UINT64_C(655) * UINT64_C(655);
}

static ArcTouchContact arc_touch_start(int x, int y, uint64_t now)
{
   ArcTouchContact contact = {true, x, y, x, y, now};
   return contact;
}

static uint64_t arc_point_distance(int x0, int y0, int x1, int y1)
{
   int64_t dx = (int64_t)x0 - x1;
   int64_t dy = (int64_t)y0 - y1;
   return (uint64_t)(dx * dx + dy * dy);
}

static void arc_touch_motion(int64_t raw_x, int64_t raw_y, unsigned divisor)
{
   if (!divisor) return;
   arc_touch.fraction_x += (long double)raw_x * arc_video_width /
                           ((long double)65534.0L * divisor);
   arc_touch.fraction_y += (long double)raw_y * arc_video_height /
                           ((long double)65534.0L * divisor);
   int64_t dx = (int64_t)truncl(arc_touch.fraction_x);
   int64_t dy = (int64_t)truncl(arc_touch.fraction_y);
   arc_touch.fraction_x -= dx;
   arc_touch.fraction_y -= dy;
   arc_input_motion(&arc_inputs, dx, dy, false);
}

static void arc_touch_poll(void)
{
   bool pressed = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                                  RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
   if (pressed) arc_boot_input = true;
   if (!arc_core_options.pointer_relative)
   {
      arc_touch_reset();
      return;
   }
   int reported = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                                  RETRO_DEVICE_ID_POINTER_COUNT);
   unsigned count = reported > 0 ? (unsigned)reported : 0u;
   bool primary_pressed = pressed;
   if (primary_pressed && count < 1u) count = 1u;
   if (count) arc_boot_input = true;
   if (count > 2u)
   {
      arc_touch_reset();
      arc_touch.blocked_until_zero = true;
      return;
   }
   if (!count)
   {
      if (!arc_touch.blocked_until_zero && arc_touch.candidate)
      {
         bool valid = false;
         unsigned button = 1u;
         if (arc_touch.two_finger)
         {
            if (arc_touch.count == 2u)
               valid = arc_touch_tap_valid(&arc_touch.contact[0], arc_guest_time_us) &&
                       arc_touch_tap_valid(&arc_touch.contact[1], arc_guest_time_us);
            else if (arc_touch.count == 1u && arc_touch.one_lifted)
               valid = arc_touch_tap_valid(&arc_touch.contact[0], arc_guest_time_us);
            button = 4u;
         }
         else if (arc_touch.count == 1u)
            valid = arc_touch_tap_valid(&arc_touch.contact[0], arc_guest_time_us);
         if (valid)
            arc_input_source_click(&arc_inputs, ARC_INPUT_SOURCE_TOUCH, button);
      }
      arc_touch_reset();
      return;
   }

   int x[2] = {0, 0};
   int y[2] = {0, 0};
   for (unsigned index = 0u; index < count; ++index)
   {
      bool pressed = arc_input_state(0u, RETRO_DEVICE_POINTER, index,
                                     RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
      x[index] = arc_input_state(0u, RETRO_DEVICE_POINTER, index,
                                 RETRO_DEVICE_ID_POINTER_X);
      y[index] = arc_input_state(0u, RETRO_DEVICE_POINTER, index,
                                 RETRO_DEVICE_ID_POINTER_Y);
      bool offscreen = arc_input_state(0u, RETRO_DEVICE_POINTER, index,
                                       RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN) != 0;
      if (!pressed || offscreen || x[index] == -0x8000 || y[index] == -0x8000)
      {
         arc_touch_reset();
         arc_touch.blocked_until_zero = true;
         return;
      }
   }
   if (arc_touch.blocked_until_zero) return;

   if (!arc_touch.count && count == 1u)
   {
      arc_touch.contact[0] = arc_touch_start(x[0], y[0], arc_guest_time_us);
      arc_touch.count = 1u;
      arc_touch.candidate = true;
      return;
   }
   if (!arc_touch.count && count == 2u)
   {
      arc_touch.contact[0] = arc_touch_start(x[0], y[0], arc_guest_time_us);
      arc_touch.contact[1] = arc_touch_start(x[1], y[1], arc_guest_time_us);
      arc_touch.count = 2u;
      arc_touch.two_finger = true;
      arc_touch.candidate = true;
      return;
   }
   if (arc_touch.count == 1u && count == 2u && !arc_touch.two_finger)
   {
      ArcTouchContact existing = arc_touch.contact[0];
      unsigned existing_index = arc_point_distance(existing.x, existing.y, x[1], y[1]) <
                                arc_point_distance(existing.x, existing.y, x[0], y[0]) ? 1u : 0u;
      existing.x = x[existing_index];
      existing.y = y[existing_index];
      arc_touch.contact[existing_index] = existing;
      arc_touch.contact[1u - existing_index] =
         arc_touch_start(x[1u - existing_index], y[1u - existing_index], arc_guest_time_us);
      arc_touch.two_finger = true;
      arc_touch.count = 2u;
      arc_touch.candidate = arc_touch.candidate &&
         arc_guest_time_us - existing.start_us <= UINT64_C(80000);
      return;
   }
   if (arc_touch.count == 2u && count == 1u)
   {
      unsigned remaining = arc_point_distance(arc_touch.contact[1].x, arc_touch.contact[1].y,
                                               x[0], y[0]) <
                           arc_point_distance(arc_touch.contact[0].x, arc_touch.contact[0].y,
                                               x[0], y[0]) ? 1u : 0u;
      unsigned lifted = 1u - remaining;
      arc_touch.candidate = arc_touch.candidate &&
         arc_touch_tap_valid(&arc_touch.contact[lifted], arc_guest_time_us);
      arc_touch.contact[0] = arc_touch.contact[remaining];
      arc_touch.contact[0].x = x[0];
      arc_touch.contact[0].y = y[0];
      arc_touch.count = 1u;
      arc_touch.one_lifted = true;
      return;
   }
   if (arc_touch.count != count)
   {
      arc_touch_reset();
      arc_touch.blocked_until_zero = true;
      return;
   }

   if (count == 2u)
   {
      uint64_t straight = arc_point_distance(arc_touch.contact[0].x, arc_touch.contact[0].y,
                                             x[0], y[0]) +
                          arc_point_distance(arc_touch.contact[1].x, arc_touch.contact[1].y,
                                             x[1], y[1]);
      uint64_t crossed = arc_point_distance(arc_touch.contact[0].x, arc_touch.contact[0].y,
                                            x[1], y[1]) +
                         arc_point_distance(arc_touch.contact[1].x, arc_touch.contact[1].y,
                                            x[0], y[0]);
      if (crossed < straight)
      {
         ArcTouchContact swap = arc_touch.contact[0];
         arc_touch.contact[0] = arc_touch.contact[1];
         arc_touch.contact[1] = swap;
      }
   }
   int64_t raw_x = 0;
   int64_t raw_y = 0;
   for (unsigned index = 0u; index < count; ++index)
   {
      raw_x += (int64_t)x[index] - arc_touch.contact[index].x;
      raw_y += (int64_t)y[index] - arc_touch.contact[index].y;
      arc_touch.contact[index].x = x[index];
      arc_touch.contact[index].y = y[index];
      if (arc_touch_distance_squared(&arc_touch.contact[index], x[index], y[index]) >
          UINT64_C(655) * UINT64_C(655))
         arc_touch.candidate = false;
   }
   arc_touch_motion(raw_x, raw_y, count);
}

static void arc_poll_inputs(void)
{
   static const unsigned ids[] = {
      RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y,
      RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT
   };
   if (!arc_input_state) return;
   arc_boot_input = false;
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      arc_left_sticks[port].polled = false;
   ArcErrorBox error_box;
   arc_engine_error_snapshot(&error_box);
   bool rpc_error=arc_engine_is_rpc();
   bool error_ready = rpc_error ? error_box.answer_available : error_box.stage == ARC_ERROR_READY;
   bool error_hint_open = rpc_error ? error_box.recognised_open : error_ready;
   if (error_ready != arc_error_was_ready ||
       (error_ready && error_box.generation != arc_error_seen_generation))
   {
      arc_error_pad_gate = true;
      arc_error_was_ready = error_ready;
   }
   if (error_ready) arc_error_seen_generation = error_box.generation;
   if (arc_chooser_release_gate)
   {
      for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
         arc_left_stick_await(port);
      arc_error_pad_gate = true;
      bool neutral = true;
      unsigned deadzone_percent = arc_core_options.pointer_deadzone;
      if (deadzone_percent < ARC_DEFAULT_POINTER_DEADZONE)
         deadzone_percent = ARC_DEFAULT_POINTER_DEADZONE;
      int analog_deadzone = (int)(INT16_MAX * deadzone_percent / 100u);
      for (unsigned port = 0u; port < 2u; ++port)
      {
         for (unsigned id = 0u; id < 16u; ++id)
            if (arc_input_state(port, RETRO_DEVICE_JOYPAD, 0u, id)) neutral = false;
         for (unsigned stick = 0u; stick < 2u; ++stick)
            for (unsigned axis = 0u; axis < 2u; ++axis)
            {
               int value = arc_input_state(port, RETRO_DEVICE_ANALOG, stick, axis);
               if (value < -analog_deadzone || value > analog_deadzone)
                  neutral = false;
            }
         if (arc_input_state(port, RETRO_DEVICE_MOUSE, 0u,
                             RETRO_DEVICE_ID_MOUSE_LEFT) ||
             arc_input_state(port, RETRO_DEVICE_MOUSE, 0u,
                             RETRO_DEVICE_ID_MOUSE_RIGHT) ||
             arc_input_state(port, RETRO_DEVICE_MOUSE, 0u,
                             RETRO_DEVICE_ID_MOUSE_MIDDLE)) neutral = false;
         (void)arc_input_state(port, RETRO_DEVICE_MOUSE, 0u,
                              RETRO_DEVICE_ID_MOUSE_X);
         (void)arc_input_state(port, RETRO_DEVICE_MOUSE, 0u,
                              RETRO_DEVICE_ID_MOUSE_Y);
      }
      if (arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                          RETRO_DEVICE_ID_POINTER_PRESSED)) neutral = false;
      for (unsigned key = 0u; key < ARC_INPUT_KEY_COUNT; ++key)
         if (arc_keyboard_raw[key] ||
             (arc_keyboard_polling &&
              arc_input_state(0u, RETRO_DEVICE_KEYBOARD, 0u, key)))
            neutral = false;
      arc_input_reset_frontend(&arc_inputs);
      arc_platform_joystick_snapshot(false, NULL);
      arc_stick_x = arc_stick_y = 0;
      if (neutral)
      {
         arc_chooser_release_gate = false;
         memset(arc_keyboard_raw, 0, sizeof(arc_keyboard_raw));
      }
      return;
   }
   bool osk_was_visible = arc_osk.visible;
   arc_poll_osk_controls();
   if (osk_was_visible && !arc_osk.visible) arc_error_pad_gate = true;
   bool error_pad = arc_port_device[0] == ARC_DEVICE_KEYS ||
                    arc_port_device[0] == RETRO_DEVICE_JOYPAD;
   bool error_a = error_pad && arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u,
                                  RETRO_DEVICE_ID_JOYPAD_A) != 0;
   bool error_b = error_pad && arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u,
                                  RETRO_DEVICE_ID_JOYPAD_B) != 0;
   if (!error_a && !error_b) arc_error_pad_gate = false;
   bool error_hint_unknown=rpc_error && error_box.modal_unknown && arc_error_hint_state &&
      arc_error_hint_generation==error_box.generation;
   bool answered=rpc_error && (error_box.pending!=ARC_ERROR_NONE || error_box.consumed!=ARC_ERROR_NONE);
   bool hint_answerable=error_box.answer_available || answered;
   unsigned hint_state=error_hint_unknown?3u:hint_answerable?2u:1u;
   if ((error_hint_open || error_hint_unknown) && !arc_osk.visible &&
       (arc_session == ARC_SESSION_RUNNING || (rpc_error && arc_engine_active())) &&
       (arc_error_hint_generation != error_box.generation ||
        (rpc_error && arc_error_hint_state != hint_state)))
   {
      if(error_hint_unknown)arc_message("Error box state unknown; use keyboard or mouse to respond.",RETRO_LOG_INFO);
      else if(rpc_error && !hint_answerable)arc_message("Error box open; use keyboard or mouse to respond.",RETRO_LOG_INFO);
      else arc_error_hint(&error_box);
      arc_error_hint_generation = error_box.generation;
      arc_error_hint_state = hint_state;
   }
   bool sampled[2][10] = {{false}};
   bool desired[ARC_PAD_PORTS][ARC_INPUT_KEY_COUNT] = {{false}};
   for (unsigned port = 0u; port < 2u; ++port)
      if ((port || !arc_osk.visible) &&
          (arc_port_device[port] == ARC_DEVICE_KEYS ||
          arc_port_device[port] == RETRO_DEVICE_JOYPAD)
         )
         for (unsigned i = 0u; i < 10u; ++i)
         {
            sampled[port][i] = arc_input_state(port, RETRO_DEVICE_JOYPAD, 0u, ids[i]) != 0;
            if (sampled[port][i]) arc_boot_input = true;
         }
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
   {
      unsigned directions = arc_sample_left_stick(port);
      if (!port && arc_osk.visible) continue;
      for (unsigned input = 0u; input < 4u; ++input)
         sampled[port][input] |= (directions & (1u << input)) != 0u;
      if (directions) arc_boot_input = true;
   }
   for (unsigned port = 0u; port < 2u; ++port)
   {
      if ((arc_boot_attempt && arc_boot_eligible) ||
          arc_hd_observe_target || arc_hd_observe_copy)
         for (unsigned id = 10u; id < 16u; ++id)
            if (arc_input_state(port, RETRO_DEVICE_JOYPAD, 0u, id))
               arc_boot_input = true;
      if (arc_pad_neutral_gate[port])
      {
         bool neutral = true;
         for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
            if (sampled[port][input]) neutral = false;
         if (neutral) arc_pad_neutral_gate[port] = false;
         else memset(sampled[port], 0, sizeof(sampled[port]));
      }
      for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
      {
         if (port == 0u && (input == ARC_PAD_A || input == ARC_PAD_B) &&
             (error_ready || arc_error_pad_gate)) continue;
         const ArcInputBinding *binding = &arc_input_resolution.binding[port][input];
         bool joystick_owned = !arc_engine_is_rpc() && arc_loaded && arc_plan.a3010_joystick && input <= ARC_PAD_A &&
                               arc_core_options.pad[port][input].automatic;
         if (!joystick_owned && sampled[port][input] && binding->bound &&
             binding->key < ARC_INPUT_KEY_COUNT)
            desired[port][binding->key] = true;
      }
      memcpy(arc_pad_keys[port], sampled[port], sizeof(sampled[port]));
   }
   if (error_ready && !arc_osk.visible && !arc_pad_neutral_gate[0] &&
       !arc_error_pad_gate && error_a != error_b)
   {
      if (error_a && !arc_error_pad_levels[0])
         (void)arc_engine_error_request(ARC_ERROR_OK);
      else if (error_b && !arc_error_pad_levels[1])
         (void)arc_engine_error_request(ARC_ERROR_CANCEL);
   }
#if defined(ARCDROID_TEST_FAULTS)
   if (!error_ready && arc_error_seen_generation && !arc_error_pad_gate &&
       error_a && !arc_error_pad_levels[0] &&
       arc_input_resolution.binding[0][ARC_PAD_A].bound &&
       desired[0][arc_input_resolution.binding[0][ARC_PAD_A].key] && arc_log)
      arc_log(RETRO_LOG_INFO, "Error box test: mapping restored key=%u\n",
              arc_input_resolution.binding[0][ARC_PAD_A].key);
#endif
   arc_error_pad_levels[0] = error_a;
   arc_error_pad_levels[1] = error_b;
   arc_input_pad_key_sets(&arc_inputs, desired[0], desired[1]);
   bool joystick_levels[ARC_PAD_PORTS][5] = {{false}};
   for (unsigned port = 0; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0; input < 5u; ++input)
         joystick_levels[port][input] = sampled[port][input] &&
            !(port == 0u && input == ARC_PAD_A &&
              (error_ready || arc_error_pad_gate)) &&
            arc_core_options.pad[port][input].automatic;
   arc_platform_joystick_snapshot(!arc_engine_is_rpc() && arc_loaded && arc_plan.a3010_joystick,
                                  joystick_levels);

   bool pad_pointer = !arc_osk.visible &&
      (arc_port_device[0] == ARC_DEVICE_KEYS ||
       arc_port_device[0] == RETRO_DEVICE_JOYPAD);
   unsigned pad_buttons = pad_pointer ?
      (arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u, RETRO_DEVICE_ID_JOYPAD_R) ? 1u : 0u) |
      (arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u, RETRO_DEVICE_ID_JOYPAD_L) ? 2u : 0u) |
      (arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u, RETRO_DEVICE_ID_JOYPAD_L2) ? 4u : 0u) : 0u;
   if (pad_buttons) arc_boot_input = true;
   arc_input_source_buttons(&arc_inputs, ARC_INPUT_SOURCE_PAD0,
                            arc_pad_pointer_buttons | pad_buttons);
   if (!arc_osk.visible) arc_touch_poll();
   else arc_input_source_buttons(&arc_inputs, ARC_INPUT_SOURCE_TOUCH, 0u);

   for (unsigned port = 0u; port < 2u; ++port)
      if (arc_port_device[port] == ARC_DEVICE_MOUSE ||
          arc_port_device[port] == RETRO_DEVICE_MOUSE)
      {
         int dx = arc_input_state(port, RETRO_DEVICE_MOUSE, 0u, RETRO_DEVICE_ID_MOUSE_X);
         int dy = arc_input_state(port, RETRO_DEVICE_MOUSE, 0u, RETRO_DEVICE_ID_MOUSE_Y);
         unsigned buttons = (arc_input_state(port, RETRO_DEVICE_MOUSE, 0u, RETRO_DEVICE_ID_MOUSE_LEFT) ? 1u : 0u) |
                            (arc_input_state(port, RETRO_DEVICE_MOUSE, 0u, RETRO_DEVICE_ID_MOUSE_RIGHT) ? 2u : 0u) |
                            (arc_input_state(port, RETRO_DEVICE_MOUSE, 0u, RETRO_DEVICE_ID_MOUSE_MIDDLE) ? 4u : 0u);
         arc_input_motion(&arc_inputs, dx, dy, false);
         if (dx || dy || buttons) arc_boot_input = true;
         arc_input_source_buttons(&arc_inputs,
            port ? ARC_INPUT_SOURCE_MOUSE1 : ARC_INPUT_SOURCE_MOUSE0, buttons);
#if defined(ARCDROID_TEST_FAULTS)
         if (dx || dy || buttons) ++arc_test_mouse_input_samples;
#endif
      }
   arc_input_source_buttons(&arc_inputs, ARC_INPUT_SOURCE_PAD0, pad_buttons);
   arc_pad_pointer_buttons = pad_buttons;

   unsigned stick_index = arc_core_options.pointer_stick == ARC_POINTER_STICK_LEFT ?
      RETRO_DEVICE_INDEX_ANALOG_LEFT : RETRO_DEVICE_INDEX_ANALOG_RIGHT;
   if (pad_pointer && arc_core_options.pointer_stick != ARC_POINTER_STICK_DISABLED)
   {
      arc_stick_x = arc_input_state(0u, RETRO_DEVICE_ANALOG, stick_index,
                                    RETRO_DEVICE_ID_ANALOG_X);
      arc_stick_y = arc_input_state(0u, RETRO_DEVICE_ANALOG, stick_index,
                                    RETRO_DEVICE_ID_ANALOG_Y);
      arc_stick_precision = arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u,
                                            RETRO_DEVICE_ID_JOYPAD_R2) != 0;
      if (arc_stick_precision) arc_boot_input = true;
   }
   else
   {
      arc_stick_x = arc_stick_y = 0;
      arc_stick_precision = false;
   }
   arc_poll_keyboard_snapshot();
   unsigned deadzone = arc_core_options.pointer_deadzone;
   if (deadzone < ARC_DEFAULT_POINTER_DEADZONE)
      deadzone = ARC_DEFAULT_POINTER_DEADZONE;
   long double dz = (long double)deadzone * 32767.0L / 100.0L;
   if ((arc_boot_attempt && arc_boot_eligible) ||
       arc_hd_observe_target || arc_hd_observe_copy)
      for (unsigned port = 0u; port < 2u; ++port)
         for (unsigned stick = 0u; stick < 2u; ++stick)
         {
            long double x = arc_input_state(port, RETRO_DEVICE_ANALOG, stick,
                                             RETRO_DEVICE_ID_ANALOG_X);
            long double y = arc_input_state(port, RETRO_DEVICE_ANALOG, stick,
                                             RETRO_DEVICE_ID_ANALOG_Y);
            if (x * x + y * y > dz * dz) arc_boot_input = true;
         }
   for (unsigned key = 0u; key < ARC_INPUT_KEY_COUNT; ++key)
      if (arc_keyboard_raw[key] ||
          arc_input_source_level(&arc_inputs, ARC_INPUT_SOURCE_KEYBOARD, key) ||
          arc_input_source_level(&arc_inputs, ARC_INPUT_SOURCE_PAD0, key) ||
          arc_input_source_level(&arc_inputs, ARC_INPUT_SOURCE_PAD1, key) ||
          arc_input_source_level(&arc_inputs, ARC_INPUT_SOURCE_TOUCH, key) ||
          arc_input_source_level(&arc_inputs, ARC_INPUT_SOURCE_MOUSE0, key) ||
          arc_input_source_level(&arc_inputs, ARC_INPUT_SOURCE_MOUSE1, key) ||
          arc_input_source_level(&arc_inputs, ARC_INPUT_SOURCE_OSK, key))
      { arc_boot_input = true; break; }
   if (arc_inputs.effective_buttons || arc_osk_toggle_level)
      arc_boot_input = true;
   if (arc_boot_input && arc_boot_attempt) arc_engine_boot_watch_cancel();
   if (arc_boot_input) arc_engine_launch_outcome_cancel("input");
}

static void arc_integrate_stick(uint64_t elapsed_us)
{
   long double x = arc_stick_x;
   long double y = arc_stick_y;
   long double magnitude = hypotl(x, y);
   long double deadzone = (long double)arc_core_options.pointer_deadzone / 100.0L;
   long double normal = magnitude > 32767.0L ? 1.0L : magnitude / 32767.0L;
   if (normal <= deadzone || !elapsed_us) return;
   long double response = (normal - deadzone) / (1.0L - deadzone);
   response *= response;
   long double speed = arc_core_options.pointer_speed *
      (arc_stick_precision ? 0.25L : 1.0L);
   long double distance = speed * response * elapsed_us / 1000000.0L;
   arc_stick_fraction_x += distance * x / magnitude;
   arc_stick_fraction_y += distance * y / magnitude;
   int64_t dx = (int64_t)truncl(arc_stick_fraction_x);
   int64_t dy = (int64_t)truncl(arc_stick_fraction_y);
   arc_stick_fraction_x -= dx;
   arc_stick_fraction_y -= dy;
   arc_input_motion(&arc_inputs, dx, dy, true);
}

static const struct retro_core_option_v2_definition arc_base_options[] = {
   {"arcdroid_machine", "Machine", NULL, "Automatic machine selection.", NULL,
    NULL, {{"Auto", NULL}, {"A3000", NULL}, {"A3010", NULL}, {"A3020", NULL}, {"A4000", NULL}, {NULL, NULL}}, "Auto"},
   {"arcdroid_cpu", "CPU", NULL, "Automatic CPU selection.", NULL,
    NULL, {{"Auto", NULL}, {"ARM2", NULL}, {"ARM250", NULL}, {NULL, NULL}}, "Auto"},
   {"arcdroid_memory", "Memory", NULL, "Automatic memory selection.", NULL,
    NULL, {{"Auto", NULL}, {"4 MB", NULL}, {NULL, NULL}}, "Auto"},
   {"arcdroid_os", "Operating System", NULL, "Automatic firmware selection.", NULL,
    NULL, {{"Auto", NULL}, {"RISC OS 3.11", NULL}, {"RISC OS 3.10", NULL},
           {"RISC OS 3.19", NULL}, {"RISC OS 3.00", NULL},
           {"RISC OS 2.00", NULL}, {"Arthur 1.20", NULL}, {NULL, NULL}}, "Auto"},
   {"arcdroid_joystick_if", "Joystick Interface", NULL, "Keyboard mapping is the Slice 1 default.", NULL,
    NULL, {{"Auto", NULL}, {"Keyboard", NULL}, {"A3010", NULL}, {NULL, NULL}}, "Auto"},
   {"arcdroid_pointer_mode", "Pointer Mode", NULL, "Use pointer input as a relative trackpad.", NULL,
    NULL, {{"disabled", NULL}, {"relative", NULL}, {NULL, NULL}}, "relative"},
   {"arcdroid_pointer_stick", "Pointer Stick", NULL, "Choose the analog stick that drives pointer velocity.", NULL,
    NULL, {{"right", NULL}, {"left", NULL}, {"disabled", NULL}, {NULL, NULL}}, "right"},
   {"arcdroid_pointer_speed", "Pointer Speed", NULL, "Maximum pointer speed in mickeys per guest second.", NULL,
    NULL, {{"300", NULL}, {"450", NULL}, {"600", NULL}, {"900", NULL}, {"1200", NULL}, {NULL, NULL}}, "600"},
   {"arcdroid_pointer_deadzone", "Pointer Dead Zone", NULL, "Radial analog-stick dead zone percentage.", NULL,
    NULL, {{"0", "0 %"}, {"10", "10 %"}, {"15", "15 %"}, {"20", "20 %"}, {"25", "25 %"}, {NULL, NULL}}, "15"},
   {"arcdroid_launch", "Launch", NULL, "Automatically launch a root application, disable launch automation, or choose a numbered candidate.", NULL,
    NULL, {{"auto", "Auto"}, {"off", "Off"}, {"app1", "Application 1"}, {"app2", "Application 2"},
           {"app3", "Application 3"}, {"app4", "Application 4"}, {"app5", "Application 5"},
           {"app6", "Application 6"}, {"app7", "Application 7"}, {"app8", "Application 8"}, {NULL, NULL}}, "auto"},
   {"arcdroid_launch_chooser", "Launch Chooser", NULL,
    "Remember the selected application, or ask on every eligible launch.", NULL,
    NULL, {{"remember", "Remember"}, {"ask", "Ask every time"},
           {NULL, NULL}}, "remember"},
   {"arcdroid_launch_ramdisc", "Native launch RAM disc", NULL,
    "Request a 32 KiB RAM disc if absent before a native desktop helper launches an application. The RAM disc is volatile.", NULL,
    NULL, {{"Auto", NULL}, {"Disabled", NULL}, {NULL, NULL}}, "Auto"},
   {"arcdroid_jfd_boot", "JFD Boot", NULL,
    "Auto uses the ADFFS package for eligible JFDs, except single-disc JFDs that ArcDroid can write natively, which boot natively to keep saves. Native never uses ADFFS. ADFFS preferred bypasses this rule; saved native changes still take precedence.", NULL,
    NULL, {{"auto", "Auto"}, {"native", "Native"},
           {"adffs", "ADFFS preferred"}, {NULL, NULL}}, "auto"},
   {"arcdroid_jfd_write_protect", "Write-protect Tab", NULL,
    "JFD floppies only. Off lets the game write to its floppy like sliding the tab; changes are kept in ArcDroid's save folder and the original file is never changed. Save this per game through RetroArch's core-option manager. Applied at content load or reset. Disk Writes: Off still blocks writes. With JFD Boot: ADFFS preferred, the disc may run through ADFFS, which does not keep these writes.", NULL,
    NULL, {{"as-imaged", "As imaged"}, {"off", "Off"}, {NULL, NULL}}, "as-imaged"},
   {"arcdroid_adffs_package", "ADFFS Package", NULL,
    "Restrict ADFFS to a verified package profile, or allow an untested package.", NULL,
    NULL, {{"verified", "Verified only"}, {"untested", "Allow untested"}, {NULL, NULL}}, "verified"},
   {"arcdroid_disk_writes", "Disk Writes", NULL, "Choose where floppy writes are stored.", NULL,
    NULL, {{"shadow", NULL}, {"original", NULL}, {"off", NULL}, {NULL, NULL}}, "shadow"},
   {"arcdroid_hard_disc", "Hard Disc", NULL,
    "Attach a persistent hard disc to native A3020 sessions.", NULL,
    NULL, {{"auto", "Auto"}, {"off", "Off"}, {NULL, NULL}}, "auto"},
   {"arcdroid_short_tail", "Short image tail", NULL,
    "Present sectors beyond a short source as missing, or expose the stored shadow bytes (including saved writes) as zero-filled media.", NULL,
    NULL, {{"missing", "Missing"}, {"zero-filled", "Zero-filled"}, {NULL, NULL}}, "missing"},
   {"arcdroid_jfd_omitted", "JFD Omitted Sectors", NULL,
    "Present proven omitted native JFD file-data sectors as zeros. Strict keeps the earlier JFD presentation. Sampled at content load or reset.", NULL,
    NULL, {{"file-data", "File data"}, {"strict", "Strict"}, {NULL, NULL}}, "file-data"},
   {"arcdroid_disc_requests", "Disc Requests", NULL,
    "Automatically insert a uniquely requested disc from a loaded multi-image set.", NULL,
    NULL, {{"auto", "Insert automatically"}, {"hint", "Hint only"},
           {"off", "Off"}, {NULL, NULL}}, "auto"},
   {NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL}
};
enum {
   ARC_BASE_OPTION_COUNT = sizeof(arc_base_options) / sizeof(arc_base_options[0]) - 1u,
   ARC_OPTION_COUNT = ARC_BASE_OPTION_COUNT +
                      ARC_PAD_PORTS * ARC_PAD_INPUT_COUNT + 3u
};
static struct retro_core_option_v2_definition arc_options[ARC_OPTION_COUNT + 1u];
_Static_assert(ARC_BASE_OPTION_COUNT == 20u, "base option count changed");
_Static_assert(sizeof(arc_options) / sizeof(arc_options[0]) == 44u,
               "option destination size changed");
static struct retro_core_option_v2_category arc_option_categories[] = {
   {"pad_mapping", "Pad Mapping", "Map each RetroPad input to a physical Archimedes key."},
   {NULL, NULL, NULL}
};
static struct retro_core_options_v2 arc_options_v2 = {arc_option_categories, arc_options};
static char arc_pad_option_labels[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT][32];
static bool arc_options_built;

static void arc_build_options(void)
{
   if (arc_options_built) return;
   memset(arc_options, 0, sizeof(arc_options));
   memcpy(arc_options, arc_base_options,
          ARC_BASE_OPTION_COUNT * sizeof(arc_base_options[0]));
   unsigned at = ARC_BASE_OPTION_COUNT;
   size_t key_count = 0u;
   const ArcInputKey *keys = arc_input_supported_keys(&key_count);
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
      {
         struct retro_core_option_v2_definition *definition = &arc_options[at++];
         snprintf(arc_pad_option_labels[port][input],
                  sizeof(arc_pad_option_labels[port][input]), "Pad %u %s",
                  port + 1u, arc_pad_input_name(input));
         definition->key = arc_pad_option_names[port][input];
         definition->desc = arc_pad_option_labels[port][input];
         definition->desc_categorized = arc_pad_option_labels[port][input];
         definition->info = "Auto uses the default mapping; Unbound disables this input.";
         definition->category_key = "pad_mapping";
         definition->values[0] = (struct retro_core_option_value){"Auto", NULL};
         definition->values[1] = (struct retro_core_option_value){"Unbound", NULL};
         for (size_t key = 0u; key < key_count && key + 3u < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++key)
            definition->values[key + 2u] =
               (struct retro_core_option_value){keys[key].value, keys[key].label};
         definition->default_value = "Auto";
      }
   struct retro_core_option_v2_definition *osk = &arc_options[at++];
   osk->key = "arcdroid_osk_toggle";
   osk->desc = "On-Screen Keyboard Toggle";
   osk->info = "Choose the RetroPad stick button that opens the on-screen keyboard.";
   osk->category_key = "pad_mapping";
   osk->values[0] = (struct retro_core_option_value){"L3", NULL};
   osk->values[1] = (struct retro_core_option_value){"R3", NULL};
   osk->values[2] = (struct retro_core_option_value){"Off", NULL};
   osk->default_value = "L3";
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
   {
      struct retro_core_option_v2_definition *stick = &arc_options[at++];
      stick->key = arc_left_stick_option_names[port];
      stick->desc = port ? "Pad 2 Left Stick" : "Pad 1 Left Stick";
      stick->desc_categorized = stick->desc;
      stick->info = "The left stick works like the D-pad; Off stops gameplay and on-screen keyboard control, while the launch menu and Pointer Stick keep using it as before.";
      stick->category_key = "pad_mapping";
      stick->values[0] = (struct retro_core_option_value){"dpad", "D-pad"};
      stick->values[1] = (struct retro_core_option_value){"off", "Off"};
      stick->default_value = "dpad";
   }
   arc_options_built = true;
}

static struct retro_input_descriptor arc_input_descriptors[] = {
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Archimedes Up"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Archimedes Down"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Archimedes Left"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Archimedes Right"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Space"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Return"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Left Shift"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Left Control"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Escape"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Tab"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "Mouse Select"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "Mouse Adjust"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "Mouse Menu"},
   {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "Pointer precision (quarter speed)"},
   {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Pointer horizontal velocity"},
   {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Pointer vertical velocity"},
   {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "D-pad Left/Right; pointer velocity (Pointer Stick = left)"},
   {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "D-pad Up/Down; pointer velocity (Pointer Stick = left)"},
   {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Archimedes Up"},
   {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Archimedes Down"},
   {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Archimedes Left"},
   {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Archimedes Right"},
   {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Space"},
   {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Return"},
   {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Left Shift"},
   {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Left Control"},
   {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Escape"},
   {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Tab"},
   {1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "D-pad Left/Right"},
   {1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "D-pad Up/Down"},
   {0, 0, 0, 0, NULL}
};
static char arc_descriptor_labels[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT][64];

static const char *arc_mapping_source_name(ArcMappingSource source)
{
   return source == ARC_MAPPING_SOURCE_OPTION ? "option" : "default";
}

static void arc_refresh_input_descriptors(void)
{
   static const unsigned offsets[ARC_PAD_PORTS] = {0u, 18u};
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
      {
         const ArcInputBinding *binding = &arc_input_resolution.binding[port][input];
         const ArcInputKey *key = binding->bound ? arc_input_key(binding->key) : NULL;
         snprintf(arc_descriptor_labels[port][input],
                  sizeof(arc_descriptor_labels[port][input]), "%s",
                  key ? key->label : "Unbound");
         arc_input_descriptors[offsets[port] + input].description =
            arc_descriptor_labels[port][input];
      }
   if (arc_environment)
      arc_environment(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS,
                      (void *)arc_input_descriptors);
}

static bool arc_resolve_inputs(const ArcCoreOptions *options,
                               bool mapping_change)
{
   ArcInputResolution resolved;
   if (!arc_input_resolve(options->pad, &resolved)) return false;
   bool changed = memcmp(resolved.binding, arc_input_resolution.binding,
                         sizeof(resolved.binding)) != 0;
   if (mapping_change && changed)
   {
      arc_input_clear_source(&arc_inputs, ARC_INPUT_SOURCE_PAD0);
      arc_input_clear_source(&arc_inputs, ARC_INPUT_SOURCE_PAD1);
      memset(arc_pad_keys, 0, sizeof(arc_pad_keys));
      arc_pad_neutral_gate[0] = arc_pad_neutral_gate[1] = true;
   }
   arc_input_resolution = resolved;
   arc_refresh_input_descriptors();
   if (arc_log)
   {
      char line[3072];
      size_t at = (size_t)snprintf(line, sizeof(line), "Input resolution: mapping=%s",
                                  resolved.custom ? "custom" : "default");
      for (unsigned port = 0u; port < ARC_PAD_PORTS && at < sizeof(line); ++port)
         for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT && at < sizeof(line); ++input)
         {
            const ArcInputBinding *binding = &resolved.binding[port][input];
            const ArcInputKey *key = binding->bound ? arc_input_key(binding->key) : NULL;
            int written = snprintf(line + at, sizeof(line) - at, " p%u.%s=%s[%s]",
                                   port + 1u, arc_pad_input_name(input),
                                   key ? key->label : "Unbound",
                                   arc_mapping_source_name(resolved.source[port][input]));
            if (written < 0) break;
            at += (size_t)written < sizeof(line) - at ? (size_t)written : sizeof(line) - at;
         }
      arc_log(RETRO_LOG_INFO, "%s\n", line);
   }
   return true;
}

RETRO_API void retro_set_environment(retro_environment_t callback)
{
   bool no_game = false;
   arc_build_options();
   arc_environment = callback;
   arc_rpc_av_attempted = false;
   if (callback)
   {
      callback(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
      callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, (void *)&arc_options_v2);
   }
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t callback) { arc_video = callback; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t callback) { arc_audio = callback; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t callback) { arc_audio_batch = callback; }
RETRO_API void retro_set_input_poll(retro_input_poll_t callback) { arc_input_poll = callback; }
RETRO_API void retro_set_input_state(retro_input_state_t callback) { arc_input_state = callback; }

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
   if (!info) return;
   memset(info, 0, sizeof(*info));
   info->library_name = "ArcDroid";
   info->library_version = ARCDROID_VERSION;
   info->valid_extensions = "adf|adl|jfd|m3u|zip";
#if defined(ARCDROID_TEST_PLANS)
   info->library_name = "ArcDroid R1 Test";
   info->valid_extensions = "adf|adl|jfd|m3u|zip|arcplan";
#endif
   info->need_fullpath = true;
   info->block_extract = true;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
   if (!info) return;
   memset(info, 0, sizeof(*info));
   info->geometry.base_width = arc_video_width;
   info->geometry.base_height = arc_video_height;
   info->geometry.max_width = arc_engine_is_rpc() ? 2048u : 4096u;
   info->geometry.max_height = 2048u;
   info->geometry.aspect_ratio = 4.0f / 3.0f;
   info->timing.fps = arc_video_rate;
   info->timing.sample_rate = ARC_AUDIO_RATE;
}

#if defined(ARCDROID_TEST_PLANS) && defined(_WIN32)
#include "rpc_crash_locator.h"
#endif
RETRO_API void retro_init(void)
{
   static const struct retro_disk_control_ext_callback disk = {
      arc_disk_set_eject, arc_disk_get_eject, arc_disk_get_index,
      arc_disk_set_index, arc_disk_get_count, arc_disk_replace,
      arc_disk_add, NULL, arc_disk_get_path, arc_disk_get_label
   };
   struct retro_vfs_interface_info vfs = {3u, NULL};
   struct retro_log_callback log = {NULL};
   arc_loaded = false;
   arc_ejected = false;
   arc_image_index = 0u;
   memset(arc_cmos_record, 0, sizeof(arc_cmos_record));
   arc_input_sampling_reset(false);
   arc_port_device[0] = ARC_DEVICE_KEYS;
   arc_port_device[1] = ARC_DEVICE_MOUSE;
   arc_audio_fraction = 0u;
   arc_video_width = ARC_WIDTH;
   arc_video_height = ARC_HEIGHT;
   arc_video_rate = ARC_RATE;
   arc_fault_reported = false;
   arc_vidc_warning_reported = false;
   arc_vidc_transient_logged = false;
   arc_vidc_incomplete_us = 0u;
   arc_audio_warning_reported = false;
   arc_guest_time_us = 0u;
   arc_can_dupe = false;
   arc_keyboard_polling = false;
   arc_osk_free_surface();
   arc_osk_reset(&arc_osk, &arc_inputs);
   arc_osk_present_dirty = false;
   arc_osk_too_narrow_reported = false;
   arc_osk_failure_reported = false;
   arc_chooser_release_gate = false;
   arc_av_transition_pending = false;
   memset(arc_keyboard_raw, 0, sizeof(arc_keyboard_raw));
#if defined(ARCDROID_TEST_FAULTS)
   arc_osk_source_unchanged = true;
   arc_osk_source_checked = false;
   arc_test_stall_kart = false;
#endif
   arc_release_session();
   arc_log = NULL;
   if (arc_environment)
   {
      arc_environment(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, (void *)&disk);
      arc_environment(RETRO_ENVIRONMENT_GET_CAN_DUPE, &arc_can_dupe);
      uint64_t capabilities = 0u;
      if (arc_environment(RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES,
                          &capabilities))
         arc_keyboard_polling =
            (capabilities & (UINT64_C(1) << RETRO_DEVICE_KEYBOARD)) != 0u;
      if (arc_environment(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs))
      {
         arc_vfs = vfs.iface;
         arc_io_set_vfs(arc_vfs);
      }
      if (arc_environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
         arc_log = log.log;
   }
   arc_engine_set_log_callback(arc_engine_log, NULL);
   arc_engine_adf_refusal_callback_set(arc_adf_log_refusal);
}

RETRO_API void retro_deinit(void)
{
   arc_persist_cmos();
   arc_release_session();
   arc_osk_reset(&arc_osk, &arc_inputs);
   arc_osk_free_surface();
   arc_engine_set_log_callback(NULL, NULL);
   arc_engine_adf_refusal_callback_set(NULL);
   arc_log = NULL;
   arc_loaded = false;
   arc_vfs = NULL;
   arc_io_set_vfs(NULL);
   arc_environment = NULL;
   arc_video = NULL;
   arc_audio = NULL;
   arc_audio_batch = NULL;
   arc_input_poll = NULL;
   arc_input_state = NULL;
#if defined(ARCDROID_TEST_PLANS) && defined(_WIN32)
   arc_test_crash_locator_stop();
#endif
}

static bool arc_construct_machine(const ArcMachineDescriptor *machine,
                                  ArcEngineMedia *mounted,
                                  char *error, size_t error_size)
{
   uint8_t payload[ARC_CMOS_PAYLOAD_SIZE];
   uint8_t machine_cmos[ARC_CMOS_MACHINE_SIZE];
   uint8_t locked_cmos_mask[32];
   ArcEngineMedia helper = {arc_helper_path, false, ARC_MEDIA_ADL_640K,
                            ARC_GEOMETRY_640K_L, NULL, NULL, 0u,
                            ARC_MEDIA_EXTENT_UNRESTRICTED};
   ArcEngineMedia carrier = {":arcdroid-memory-0:", false, ARC_MEDIA_ADF_800K,
                             ARC_GEOMETRY_800K_D, NULL,
                             arc_adffs_carrier, ARC_ADFFS_DISC_SIZE,
                             ARC_MEDIA_EXTENT_UNRESTRICTED};
   ArcEngineMedia support = {":arcdroid-memory-1:", false, ARC_MEDIA_ADF_800K,
                             ARC_GEOMETRY_800K_D, NULL,
                             arc_adffs_support, ARC_ADFFS_DISC_SIZE,
                             ARC_MEDIA_EXTENT_UNRESTRICTED};
   if (arc_adffs_active) {
      mounted = &carrier;
      helper = support;
   }
   ArcIdeBackend ide_backend = {0};
   ArcInstallStore *active_hd = arc_hd_store;
   bool attach_hd = machine->ide_capable && active_hd &&
      arc_install_store_state(active_hd) == ARC_INSTALL_STORE_READY;
   if (attach_hd) {
      ide_backend.opaque = active_hd;
      ide_backend.read = arc_install_store_read;
      ide_backend.write = arc_install_store_write;
      ide_backend.failed = arc_install_store_failed;
      ide_backend.sectors = ARC_INSTALL_HD_SECTORS;
      ide_backend.writable = arc_install_store_writable(active_hd);
      ide_backend.unsupported = arc_install_ide_unsupported;
   }
   memcpy(arc_cmos_record, arc_cmos_prepared, sizeof(arc_cmos_record));
   if (!arc_cmos_decode(arc_cmos_prepared, &arc_plan, payload) ||
       !arc_cmos_machine_image(arc_cmos_policy_os(), machine->io, payload,
                               arc_plan.launch_helper, machine_cmos))
      return false;
   if (attach_hd) arc_cmos_machine_attach_ide(machine_cmos);
   if (arc_log) arc_log(RETRO_LOG_INFO,
      "Hard disc: policy=%s attach=%s mode=%s reason=%s store=%s generation=%llu id=%s\n",
      arc_hd_option_auto ? "auto" : "off", attach_hd ? "yes" : "no",
      attach_hd ?
         (arc_install_store_writable(active_hd) ? "writable" : "read-only") : "none",
      arc_hd_reason,
      arc_hd_damaged ? "damaged" : arc_hd_store ?
         (arc_install_store_created(arc_hd_store) ? "created" : "ready") : "none",
      (unsigned long long)arc_install_store_generation(arc_hd_store),
      arc_plan.content_id);
#if defined(ARCDROID_TEST_FAULTS)
   if (arc_test_fail_next_construct)
   {
      arc_test_fail_next_construct = false;
      if (error && error_size)
         snprintf(error, error_size, "Synthetic construction failure.");
      return false;
   }
#endif
   arc_cmos_locked_raw_mask(arc_plan.os, locked_cmos_mask);
   /* A construction replaces every old KART transition, including a pending
    * power-on release. Frontend input is sampled again on the next run. */
   arc_input_reset(&arc_inputs);
   arc_power_on_shift = false;
   if (
       !arc_engine_start(arc_firmware, arc_firmware_size, mounted,
                         arc_plan.launch_helper ? &helper : NULL,
                         machine, arc_plan.os,
                         attach_hd ? &ide_backend : NULL,
                         machine_cmos, locked_cmos_mask,
                         arc_cmos_ignored_write, NULL, error, error_size))
      return false;
   bool riscos3 = arc_cmos_policy_riscos3();
   arc_power_on_shift = arc_cmos_power_on_shift(arc_cmos_policy_os(), machine_cmos);
   arc_engine_power_on_arm(&arc_inputs, arc_power_on_shift);
   if (arc_power_on_shift)
      arc_input_source_key(&arc_inputs, ARC_INPUT_SOURCE_POWER_ON,
                           RETROK_LSHIFT, true, true);
   if (riscos3 && arc_log)
      arc_log(RETRO_LOG_INFO, "Power-on: boot-flag=%s shift=%s\n",
              arc_power_on_shift ? "clear" : "set",
              arc_power_on_shift ? "held" : "none");
   arc_session = ARC_SESSION_RUNNING;
   arc_io_set_write_observer(arc_disc_write_observer);
   arc_disc_option_refresh();
   arc_disc_setup();
   if (arc_boot_attempt && arc_boot_eligible && !arc_adffs_active &&
       !arc_plan.launch_helper && arc_image_index == 0u)
   {
      arc_engine_boot_watch_arm();
      arc_boot_last_state = ARC_BOOT_WATCH_ARMED;
   }
   if ((arc_hd_observe_target || arc_hd_observe_copy) &&
       !(arc_hd_observe_target && arc_hd_install_used) &&
       !(arc_hd_observe_copy && arc_hd_product_used) && attach_hd &&
       arc_install_store_writable(active_hd))
   {
      char command[257];
      int length=arc_hd_observe_copy ?
         snprintf(command,sizeof(command),
            "Run ADFS::4.$.ArcDroid.%s.!Run",arc_hd_target) :
         snprintf(command,sizeof(command),"Run %s.!Run",arc_hd_source);
      if (length > 0 && (size_t)length < sizeof(command) &&
          arc_engine_launch_outcome_arm(command))
         arc_hd_last_outcome_state=ARC_OUTCOME_WAIT;
   }
   return true;
}

static void arc_chooser_reset_input(void)
{
   memset(arc_chooser_pad_levels, 0, sizeof(arc_chooser_pad_levels));
   memset(arc_chooser_keyboard_levels, 0, sizeof(arc_chooser_keyboard_levels));
   arc_chooser_pointer_down = false;
   arc_chooser_pointer_row = -1;
   arc_chooser_input_armed = false;
   arc_input_reset_frontend(&arc_inputs);
   arc_platform_joystick_snapshot(false, NULL);
}

static bool arc_boot_discover_current(ArcLaunchDiscovery *discovery)
{
   ArcMediaEntry *entry = &arc_media[arc_image_index];
   void *raw = NULL;
   size_t size = 0u;
   const ArcJfdImage *jfd = entry->info.kind == ARC_MEDIA_JFD ? &entry->jfd : NULL;
   if (!jfd && !arc_io_read_all(entry->staged, &raw, &size)) return false;
   if (!jfd && entry->effective_extent && size > entry->effective_extent)
      size = entry->effective_extent;
   bool ok = arc_launch_discover(raw, size, &entry->info, jfd, discovery);
   free(raw);
   return ok;
}

static bool arc_boot_same_target(const ArcLaunchCandidate *a,
                                 const ArcLaunchCandidate *b)
{
   return arc_launch_candidate_is_target(b) &&
      a->name.length == b->name.length &&
      !memcmp(a->name.bytes, b->name.bytes, a->name.length) &&
      a->parent.length == b->parent.length &&
      !memcmp(a->parent.bytes, b->parent.bytes, a->parent.length);
}

static bool arc_hd_recover(ArcHdHelperMode mode)
{
   arc_hd_scope_reported=false;
   if (!arc_loaded || arc_session != ARC_SESSION_RUNNING ||
       !arc_hd_store || !arc_install_store_writable(arc_hd_store) ||
       arc_hd_selected >= arc_boot_discovery.candidate_count ||
       !arc_hd_scope_prepare(&arc_boot_discovery,arc_hd_selected,false))
      return false;
   bool allowed=mode == ARC_HD_HELPER_INSTALL ?
      arc_hd_scope.state == ARC_HD_ABSENT :
      arc_hd_scope.state == ARC_HD_STARTED && arc_hd_scope.copy_present &&
      arc_hd_scope.product_present && arc_hd_scope.application_count == 2u;
   if (!allowed) return false;
   char previous_helper[sizeof(arc_helper_path)];
   memcpy(previous_helper,arc_helper_path,sizeof(previous_helper));
   ArcLaunchPlan previous_plan=arc_plan;
   ArcHdHelperMode previous_mode=arc_hd_mode;
   bool previous_target=arc_hd_observe_target;
   bool previous_copy=arc_hd_observe_copy;
   char error[512]={0};
   arc_hd_requested_mode=mode;
   bool published=arc_launch_helper_publish(&arc_boot_discovery,
      arc_hd_selected,error,sizeof(error));
   arc_hd_requested_mode=ARC_HD_HELPER_FLOPPY;
   if (!published)
   {
      memcpy(arc_helper_path,previous_helper,sizeof(previous_helper));
      arc_plan=previous_plan;
      arc_hd_mode=previous_mode;
      arc_hd_observe_target=previous_target;
      arc_hd_observe_copy=previous_copy;
      arc_message(error[0]?error:"Unable to publish the hard-disc helper.",
                  RETRO_LOG_WARN);
      return false;
   }
   arc_persist_cmos();
   memcpy(arc_cmos_prepared,arc_cmos_record,sizeof(arc_cmos_prepared));
   arc_engine_stop();
   arc_session=ARC_SESSION_PREPARED;
   arc_boot_recovery_phase=true;
   arc_boot_recovery_action=mode == ARC_HD_HELPER_INSTALL ? "install" : "product";
   arc_boot_eligible=false;
   arc_boot_attempt=false;
   ArcMediaEntry *entry=&arc_media[arc_image_index];
   if (!arc_refresh_jfd(entry,arc_plan.disk_writes,
                        arc_core_options.jfd_write_policy,error,sizeof(error)))
      goto fallback;
   arc_finish_jfd_preparation(entry,true, arc_core_options.jfd_write_policy);
   ArcLaunchDiscovery refreshed;
   if (!arc_boot_discover_current(&refreshed))
   { snprintf(error,sizeof(error),"Unable to inspect the disc after preparation."); goto fallback; }
   bool found=false;
   for (unsigned i=0u; i<refreshed.candidate_count; ++i)
      if (arc_boot_same_target(&arc_boot_discovery.candidates[arc_hd_selected],
                               &refreshed.candidates[i]))
      { found=true; break; }
   if (!found)
   { snprintf(error,sizeof(error),"The application changed during recovery."); goto fallback; }
   ArcEngineMedia mounted=arc_engine_media(entry);
   if (!arc_construct_machine(arc_machine_descriptor(arc_plan.machine),
                              &mounted,error,sizeof(error))) goto fallback;
   arc_av_transition_pending=true;
   arc_chooser_release_gate=true;
   arc_chooser_reset_input();
   arc_frame_number=0u;
   arc_audio_fraction=0u;
   arc_guest_time_us=0u;
   arc_fault_reported=false;
   memset(arc_frame,0,sizeof(arc_frame));
   arc_launch_log(&arc_boot_discovery,(int)arc_hd_selected,
      mode == ARC_HD_HELPER_INSTALL ? "install" : "product");
   arc_message(mode == ARC_HD_HELPER_INSTALL ?
      "Installing to the hard disc - please wait" :
      "Starting the installed application", RETRO_LOG_INFO);
   return true;
fallback:
   if (arc_log) arc_log(RETRO_LOG_WARN,
      "Launch: recovery=%s reconstruction-failed=%s\n",
      mode == ARC_HD_HELPER_INSTALL ? "install" : "product",error);
   memcpy(arc_helper_path,previous_helper,sizeof(previous_helper));
   arc_plan=previous_plan;
   arc_hd_mode=previous_mode;
   arc_hd_observe_target=false;
   arc_hd_observe_copy=false;
   ArcEngineMedia original=arc_engine_media(entry);
   if (arc_construct_machine(arc_machine_descriptor(arc_plan.machine),
                              &original,error,sizeof(error)))
   {
      arc_message("Hard-disc recovery could not restart; the original launch remains available.",
                  RETRO_LOG_WARN);
      arc_av_transition_pending=true;
      return false;
   }
   /* A failed helper must never be left selected for a later launch. */
   arc_plan.launch_helper=false;
   arc_helper_path[0]=0;
   if (arc_construct_machine(arc_machine_descriptor(arc_plan.machine),
                              &original,error,sizeof(error)))
   {
      arc_message("Hard-disc recovery failed; the normal floppy boot remains available.",
                  RETRO_LOG_WARN);
      arc_av_transition_pending=true;
      return false;
   }
   arc_message(error[0]?error:"Unable to reconstruct the machine.",RETRO_LOG_ERROR);
   arc_loaded=false;
   arc_release_session();
   arc_session=ARC_SESSION_FAILED;
   return false;
}

static bool arc_hd_outcome_boundary(void)
{
   if (arc_hd_reset_cancel_pending) {
      if (arc_log) arc_log(RETRO_LOG_INFO,"Launch outcome: cancelled=reset\n");
      arc_hd_reset_cancel_pending=false;
   }
   if (!arc_hd_observe_target && !arc_hd_observe_copy) return false;
   ArcLaunchOutcome outcome;
   arc_engine_launch_outcome_snapshot(&outcome);
   if (outcome.state == arc_hd_last_outcome_state) return false;
   if (arc_hd_last_outcome_state == ARC_OUTCOME_WAIT && outcome.matched &&
       arc_log)
      arc_log(RETRO_LOG_INFO,"Launch outcome: armed target=%s\n",
         outcome.command+4u);
   arc_hd_last_outcome_state=outcome.state;
   if (outcome.state == ARC_OUTCOME_CANCEL)
   {
      if (arc_log) arc_log(RETRO_LOG_INFO,"Launch outcome: cancelled=%s\n",
                            outcome.cancel_reason ? outcome.cancel_reason : "unknown");
      arc_hd_observe_target=arc_hd_observe_copy=false;
      return false;
   }
   if (outcome.state != ARC_OUTCOME_POLL) return false;
   if (arc_log)
   {
      if (outcome.has_rc)
         arc_log(RETRO_LOG_INFO,
            "Launch outcome: ended rc=%u refused=%u hd-refused=%u close-r1=%u\n",
            outcome.last_rc,outcome.refused_floppy,outcome.refused_hd,
            outcome.close_r1);
      else arc_log(RETRO_LOG_INFO,
            "Launch outcome: ended rc=none refused=%u hd-refused=%u close-r1=%u\n",
            outcome.refused_floppy,outcome.refused_hd,outcome.close_r1);
   }
   bool install=arc_hd_observe_target && outcome.refused_floppy != 0u &&
                !arc_hd_install_used;
   bool product=arc_hd_observe_copy && outcome.failed_exits == 0u &&
                outcome.refused_hd == 0u && !arc_hd_product_used;
   arc_hd_observe_target=arc_hd_observe_copy=false;
   if (install)
   { arc_hd_install_used=true; return arc_hd_recover(ARC_HD_HELPER_INSTALL); }
   if (product)
   { arc_hd_product_used=true; return arc_hd_recover(ARC_HD_HELPER_MARK); }
   return false;
}

static bool arc_boot_recover_target(const ArcLaunchDiscovery *discovery,
                                    unsigned selected)
{
   char error[512] = {0};
   bool chosen = arc_boot_post_chooser;
   ArcLaunchCandidate target = discovery->candidates[selected];
   if (!arc_launch_helper_publish(discovery, selected, error, sizeof(error)))
   {
      arc_boot_recovery_action = "failed";
      arc_launch_log(discovery, -1, error[0] ? error : "helper publication failed");
      arc_message(error[0] ? error : "Unable to publish the launch helper; use the command prompt.",
                  RETRO_LOG_ERROR);
      arc_boot_post_chooser = false;
      arc_session = ARC_SESSION_RUNNING;
      if (chosen)
      {
         arc_av_transition_pending = true;
         arc_chooser_release_gate = true;
         arc_chooser_reset_input();
      }
      return false;
   }
   /* The original machine is still running here, including for a chooser pick. */
   arc_session = ARC_SESSION_RUNNING;
   arc_persist_cmos();
   memcpy(arc_cmos_prepared, arc_cmos_record, sizeof(arc_cmos_prepared));
   arc_boot_post_chooser = false;
   arc_boot_eligible = false;
   arc_boot_attempt = false;
   arc_engine_stop();
   arc_session = ARC_SESSION_PREPARED;
   if (!arc_refresh_jfd(&arc_media[arc_image_index], arc_plan.disk_writes,
                           arc_core_options.jfd_write_policy,
                        error, sizeof(error))) goto failed;
   arc_finish_jfd_preparation(&arc_media[arc_image_index], true,
                               arc_core_options.jfd_write_policy);
   ArcLaunchDiscovery refreshed;
   if (!arc_boot_discover_current(&refreshed))
   { snprintf(error, sizeof(error), "Unable to inspect the disc after recovery preparation."); goto failed; }
   bool found = false;
   for (unsigned i = 0u; i < refreshed.candidate_count; ++i)
      if (arc_boot_same_target(&target, &refreshed.candidates[i]))
      { found = true; break; }
   if (!found)
   { snprintf(error, sizeof(error), "The selected application changed during recovery; reload the content."); goto failed; }
   ArcEngineMedia mounted = arc_engine_media(&arc_media[arc_image_index]);
   if (!arc_construct_machine(arc_machine_descriptor(arc_plan.machine),
                              &mounted, error, sizeof(error))) goto failed;
   arc_av_transition_pending = true;
   arc_chooser_release_gate = true;
   arc_chooser_reset_input();
   arc_frame_number = 0u;
   arc_audio_fraction = 0u;
   arc_guest_time_us = 0u;
   arc_fault_reported = false;
   arc_vidc_warning_reported = false;
   arc_vidc_transient_logged = false;
   arc_vidc_incomplete_us = 0u;
   arc_audio_warning_reported = false;
   memset(arc_frame, 0, sizeof(arc_frame));
   arc_boot_recovery_action = "helper";
   arc_launch_log(discovery, (int)selected,
                  chosen ? "chooser chose target" : "boot outcome helper");
   arc_boot_record_publish();
   return true;
failed:
   arc_boot_recovery_action = "failed";
   arc_launch_log(discovery, -1, error[0] ? error : "reconstruction failed");
   arc_message(error[0] ? error : "Unable to reconstruct the machine; reload the content.",
               RETRO_LOG_ERROR);
   arc_loaded = false;
   arc_release_session();
   arc_session = ARC_SESSION_FAILED;
   return false;
}

static void arc_boot_continue(void)
{
   bool was_chooser = arc_boot_post_chooser;
   arc_boot_post_chooser = false;
   arc_session = ARC_SESSION_RUNNING;
   arc_chooser_release_gate = true;
   arc_chooser_reset_input();
   arc_av_transition_pending = was_chooser;
   arc_boot_recovery_action = "none";
   arc_launch_log(&arc_chooser_discovery, -1, "continued without launching");
}

static void arc_boot_prompt(uint64_t detected_us)
{
   ArcLaunchDiscovery discovery;
   arc_boot_attempt = false;
   arc_boot_eligible = false;
   arc_boot_recovery_phase = true;
   if (!arc_boot_discover_current(&discovery))
      memset(&discovery, 0, sizeof(discovery));
   int selected = -1;
   ArcLaunchRecovery action = arc_launch_recovery_decide(&discovery, &selected);
   if (arc_log)
      arc_log(RETRO_LOG_INFO, "Boot outcome: prompt at %llu us; action=%s\n",
         (unsigned long long)detected_us,
         action == ARC_LAUNCH_RECOVERY_HELPER ? "helper" :
         action == ARC_LAUNCH_RECOVERY_CHOOSER ? "chooser" : "none");
   if (action == ARC_LAUNCH_RECOVERY_HELPER)
   { (void)arc_boot_recover_target(&discovery, (unsigned)selected); return; }
   if (action == ARC_LAUNCH_RECOVERY_CHOOSER)
   {
      bool remembered = false;
      int remembered_target = INT_MIN;
      (void)arc_choice_load(&discovery, &remembered, &remembered_target);
      arc_chooser_discovery = discovery;
      if (remembered && arc_core_options.launch_chooser == ARC_LAUNCH_CHOOSER_REMEMBER)
      {
         if (remembered_target < 0)
         {
            arc_boot_record_publish();
            arc_boot_continue();
            return;
         }
         (void)arc_boot_recover_target(&discovery, (unsigned)remembered_target);
         return;
      }
      if (arc_launch_chooser_init(&arc_chooser, &discovery,
                                  remembered, remembered_target))
      {
         arc_launch_chooser_post_boot(&arc_chooser, &discovery);
         arc_persist_cmos();
         memcpy(arc_cmos_prepared, arc_cmos_record, sizeof(arc_cmos_prepared));
         arc_boot_post_chooser = true;
         arc_session = ARC_SESSION_CHOOSING;
         arc_chooser_reset_input();
         arc_av_transition_pending = true;
         arc_boot_recovery_action = "chooser";
         arc_launch_log(&discovery, -1, "boot outcome chooser");
         arc_boot_record_publish();
         return;
      }
   }
   arc_boot_recovery_action = "none";
   arc_launch_log(&discovery, -1, "no verified application");
   arc_message("Disc startup stopped. No application could be selected automatically.",
               RETRO_LOG_INFO);
}

static bool arc_chooser_confirm(void)
{
   int selected = arc_launch_chooser_selection(&arc_chooser);
   if (selected == INT_MIN) return false;
   if (arc_boot_post_chooser)
   {
      bool remembered = arc_choice_publish(&arc_chooser_discovery, selected);
      if (!remembered)
         arc_message("Launch choice not remembered; starting this time only.",
                     RETRO_LOG_WARN);
      if (selected == -1) { arc_boot_continue(); return true; }
      if (selected == -2)
      {
         arc_hd_requested_mode=ARC_HD_HELPER_PRODUCT;
         bool ok=arc_boot_recover_target(&arc_chooser_discovery,arc_hd_selected);
         arc_hd_requested_mode=ARC_HD_HELPER_FLOPPY;
         return ok;
      }
      return arc_boot_recover_target(&arc_chooser_discovery, (unsigned)selected);
   }
   bool remembered = arc_choice_publish(&arc_chooser_discovery, selected);
   if (!remembered)
      arc_message("Launch choice not remembered; starting this time only.",
                  RETRO_LOG_WARN);
   char error[512] = {0};
   arc_helper_path[0] = '\0';
   arc_plan.launch_helper = false;
   arc_plan.launch_target[0] = '\0';
   arc_plan.launch_target_length = 0u;
   if (selected == -2)
      arc_hd_requested_mode=ARC_HD_HELPER_PRODUCT;
   bool published=selected == -1 ||
      arc_launch_helper_publish(&arc_chooser_discovery,
         selected == -2 ? arc_hd_selected : (unsigned)selected,
         error,sizeof(error));
   arc_hd_requested_mode=ARC_HD_HELPER_FLOPPY;
   if (!published) goto failed;
   arc_launch_log(&arc_chooser_discovery,
                  selected == -2 ? (int)arc_hd_selected : selected,
                  selected == -2 ? "hd-product" : selected < 0 ? (arc_chooser.boot_hit ?
                     "chooser chose disc startup; recovery disabled" :
                     "chooser chose desktop") :
                                 "chooser chose target");
   if (!arc_resolve_inputs(&arc_core_options, true))
   {
      snprintf(error, sizeof(error), "Unable to resolve ArcDroid input options.");
      goto failed;
   }
   ArcEngineMedia mounted = arc_engine_media(&arc_media[arc_image_index]);
   const ArcMachineDescriptor *machine = arc_machine_descriptor(arc_plan.machine);
   if (!arc_construct_machine(machine, &mounted, error, sizeof(error)))
      goto failed;
   arc_chooser_release_gate = true;
   arc_chooser_reset_input();
   arc_frame_number = 0u;
   arc_audio_fraction = 0u;
   arc_guest_time_us = 0u;
   arc_fault_reported = false;
   arc_vidc_warning_reported = false;
   arc_vidc_transient_logged = false;
   arc_vidc_incomplete_us = 0u;
   arc_audio_warning_reported = false;
   arc_av_transition_pending = true;
   return true;
failed:
   if (arc_boot_record_hit) arc_boot_record_invalidate();
   arc_message(error[0] ? error :
      "Unable to start the ArcDroid machine after the launch choice.",
      RETRO_LOG_ERROR);
   arc_loaded = false;
   arc_release_session();
   arc_session = ARC_SESSION_FAILED;
   return false;
}

static void arc_poll_chooser(void)
{
   if (!arc_input_state) return;
   for (unsigned port = 0u; port < 2u; ++port)
   {
      (void)arc_input_state(port, RETRO_DEVICE_MOUSE, 0u,
                           RETRO_DEVICE_ID_MOUSE_X);
      (void)arc_input_state(port, RETRO_DEVICE_MOUSE, 0u,
                           RETRO_DEVICE_ID_MOUSE_Y);
   }
   bool levels[4] = {
      arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u,
                      RETRO_DEVICE_ID_JOYPAD_UP) != 0 ||
      arc_input_state(0u, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
                      RETRO_DEVICE_ID_ANALOG_Y) < -16384,
      arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u,
                      RETRO_DEVICE_ID_JOYPAD_DOWN) != 0 ||
      arc_input_state(0u, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
                      RETRO_DEVICE_ID_ANALOG_Y) > 16384,
      arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u,
                      RETRO_DEVICE_ID_JOYPAD_A) != 0,
      arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u,
                      RETRO_DEVICE_ID_JOYPAD_START) != 0
   };
   bool key[3] = {
      arc_keyboard_raw[RETROK_UP], arc_keyboard_raw[RETROK_DOWN],
      arc_keyboard_raw[RETROK_RETURN]
   };
   if (arc_keyboard_polling)
   {
      key[0] = arc_input_state(0u, RETRO_DEVICE_KEYBOARD, 0u, RETROK_UP) != 0;
      key[1] = arc_input_state(0u, RETRO_DEVICE_KEYBOARD, 0u, RETROK_DOWN) != 0;
      key[2] = arc_input_state(0u, RETRO_DEVICE_KEYBOARD, 0u, RETROK_RETURN) != 0;
      arc_keyboard_raw[RETROK_UP] = key[0];
      arc_keyboard_raw[RETROK_DOWN] = key[1];
      arc_keyboard_raw[RETROK_RETURN] = key[2];
   }
   bool pointer = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                                  RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
   int row = -1;
   if (pointer)
   {
      bool offscreen = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
         RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN) != 0;
      if (!offscreen)
      {
         int raw_x = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                                     RETRO_DEVICE_ID_POINTER_X);
         int raw_y = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                                     RETRO_DEVICE_ID_POINTER_Y);
         if (raw_x != -32768 && raw_y != -32768)
            row = arc_launch_chooser_hit(&arc_chooser,
               (unsigned)arc_pointer_to_pixel(raw_x, ARC_WIDTH),
               (unsigned)arc_pointer_to_pixel(raw_y, ARC_HEIGHT));
      }
   }
   if (!arc_chooser_input_armed)
   {
      bool neutral = !pointer;
      for (unsigned i = 0u; i < 4u; ++i)
         if (levels[i]) neutral = false;
      for (unsigned i = 0u; i < 3u; ++i)
         if (key[i]) neutral = false;
      memcpy(arc_chooser_pad_levels, levels, sizeof(levels));
      memcpy(arc_chooser_keyboard_levels, key, sizeof(key));
      arc_chooser_pointer_down = pointer;
      arc_chooser_pointer_row = -1;
      if (neutral) arc_chooser_input_armed = true;
      return;
   }
   if ((levels[0] && !arc_chooser_pad_levels[0]) ||
       (key[0] && !arc_chooser_keyboard_levels[0]))
      arc_launch_chooser_move(&arc_chooser, -1);
   if ((levels[1] && !arc_chooser_pad_levels[1]) ||
       (key[1] && !arc_chooser_keyboard_levels[1]))
      arc_launch_chooser_move(&arc_chooser, 1);
   bool confirm = (levels[2] && !arc_chooser_pad_levels[2]) ||
                  (levels[3] && !arc_chooser_pad_levels[3]) ||
                  (key[2] && !arc_chooser_keyboard_levels[2]);
   memcpy(arc_chooser_pad_levels, levels, sizeof(levels));
   memcpy(arc_chooser_keyboard_levels, key, sizeof(key));
   if (pointer && !arc_chooser_pointer_down)
   {
      arc_chooser_pointer_row = row;
      if (row >= 0) (void)arc_launch_chooser_focus(&arc_chooser, (unsigned)row);
   }
   else if (pointer && arc_chooser_pointer_down &&
            arc_chooser_pointer_row >= 0 && row != arc_chooser_pointer_row)
      arc_chooser_pointer_row = -1;
   else if (!pointer && arc_chooser_pointer_down)
   {
      if (arc_chooser_pointer_row >= 0)
      {
         (void)arc_launch_chooser_focus(&arc_chooser,
                                        (unsigned)arc_chooser_pointer_row);
         confirm = true;
      }
      arc_chooser_pointer_row = -1;
   }
   arc_chooser_pointer_down = pointer;
   if (confirm) (void)arc_chooser_confirm();
}

static void arc_notify_av_change(unsigned width, unsigned height, double rate)
{
   bool rate_changed = rate < arc_video_rate - 0.01 ||
                       rate > arc_video_rate + 0.01;
   bool geometry_changed = width != arc_video_width ||
                           height != arc_video_height;
   if (rate_changed)
   {
      struct retro_system_av_info av;
      retro_get_system_av_info(&av);
      av.geometry.base_width = width;
      av.geometry.base_height = height;
      av.timing.fps = rate;
      if (arc_environment)
         arc_environment(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
      arc_video_width = width;
      arc_video_height = height;
      arc_video_rate = rate;
   }
   else if (geometry_changed)
   {
      struct retro_game_geometry geometry = {
         width, height, 4096u, 2048u, 4.0f / 3.0f
      };
      if (arc_environment)
         arc_environment(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
      arc_video_width = width;
      arc_video_height = height;
   }
}

static bool arc_load_game(const struct retro_game_info *game, bool negotiate)
{
   enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
   static const struct retro_controller_description controllers[] = {
      {"RetroPad: keys and pointer", ARC_DEVICE_KEYS},
      {"Archimedes mouse", ARC_DEVICE_MOUSE}
   };
   static const struct retro_controller_info ports[] = {
      {controllers, 2u}, {controllers, 2u}, {NULL, 0u}
   };
   static const struct retro_keyboard_callback keyboard = {arc_keyboard};
   const char *system_dir = NULL;
   const char *save_dir = NULL;
   char firmware_dir[4096];
   char error[512] = {0};
   ArcFirmwareSearch search = {0};
   search.priority = 100u;
   search.best_available_priority = 100u;
   if (arc_loaded) arc_persist_cmos();
   arc_loaded = false;
   arc_release_session();
   arc_boot_attempt = true;
   arc_input_sampling_reset(false);
   arc_osk_reset(&arc_osk, &arc_inputs);
   arc_osk_free_surface();
   arc_osk_present_dirty = false;
   arc_osk_too_narrow_reported = false;
   arc_osk_failure_reported = false;
   arc_chooser_release_gate = false;
   arc_av_transition_pending = false;
   memset(arc_keyboard_raw, 0, sizeof(arc_keyboard_raw));
   arc_guest_time_us = 0u;
   memset(arc_frame, 0, sizeof(arc_frame));
   if (!game || !game->path || !game->path[0])
   {
      arc_message("ArcDroid requires an ADF, ADL, JFD, m3u playlist or zip.", RETRO_LOG_ERROR);
      return false;
   }
   if (!arc_environment || (negotiate &&
       !arc_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format)))
      return false;
   if (negotiate)
   {
      arc_environment(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);
      arc_environment(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)arc_input_descriptors);
      arc_environment(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, (void *)&keyboard);
      uint64_t capabilities = 0u;
      arc_keyboard_polling =
         arc_environment(RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES,
                         &capabilities) &&
         (capabilities & (UINT64_C(1) << RETRO_DEVICE_KEYBOARD)) != 0u;
   }
   if (!arc_vfs)
   {
      arc_message("ArcDroid requires the libretro VFS v3 interface.", RETRO_LOG_ERROR);
      return false;
   }
   if (strlen(game->path) >= ARC_MEDIA_PATH_CAPACITY)
   {
      arc_message("Content path is too long.", RETRO_LOG_ERROR);
      return false;
   }
   if (!arc_read_options(&arc_core_options, error, sizeof(error)))
   {
      arc_message(error, RETRO_LOG_ERROR);
      return false;
   }
   arc_preparing_zero_tail = arc_core_options.short_tail_zero;
   if (!arc_environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) ||
       !save_dir || !save_dir[0] || strlen(save_dir) >= sizeof(arc_save_path) ||
       !strncmp(save_dir, "saf://", 6u))
   {
      arc_message("A native writable save directory is required; SAF save directories cannot publish staged media.", RETRO_LOG_ERROR);
      return false;
   }
   strcpy(arc_save_path, save_dir);
   arc_riscpc_notice_shown = false;
   if (!arc_prepare_media_list(game->path, arc_core_options.disk_writes,
                               error, sizeof(error)))
   {
      arc_message(error, RETRO_LOG_ERROR);
      return false;
   }
   arc_image_index = 0u;
   bool has_hd_media = false;
   for (unsigned i = 0u; i < arc_media_count; ++i)
      if (arc_media_is_hd(&arc_media[i])) has_hd_media = true;
   arc_session_machine = arc_core_options.machine_auto ?
      (has_hd_media ? ARC_MACHINE_A3020 : ARC_MACHINE_A3000) :
      arc_core_options.requested_machine;
   if (has_hd_media &&
       arc_machine_descriptor(arc_session_machine)->io != ARC_IO_NEW_82C711)
   {
      snprintf(error, sizeof(error),
         "hd-media-needs-new-io: High-density media requires A3010, A3020 or A4000.");
      arc_message(error, RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   if (!arc_options_resolve(&arc_core_options, arc_session_machine,
                            error, sizeof(error)))
   {
      arc_message(error, RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   if (arc_log)
      arc_log(RETRO_LOG_INFO, "Machine: requested=%s resolved=%s reason=%s\n",
         arc_core_options.machine_auto ? "Auto" :
            arc_machine_descriptor(arc_core_options.requested_machine)->name,
         arc_machine_descriptor(arc_session_machine)->name,
         arc_core_options.machine_auto ?
            (has_hd_media ? "hd-media" : "default") : "option");

   if (!arc_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) ||
       !system_dir || !system_dir[0] ||
       snprintf(firmware_dir, sizeof(firmware_dir), "%s/arcdroid/firmware", system_dir) < 0)
   {
      arc_message("Firmware directory unavailable; expected <system>/arcdroid/firmware.", RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   if (strlen(system_dir) >= sizeof(arc_system_path))
   {
      arc_message("System directory path is too long.", RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   strcpy(arc_system_path, system_dir);
   /* R3a has no authority over the legacy search, options, ownership or plan. */
   size_t rpc_count;
   const ArcRpcFirmwareFingerprint *rpc_table = arc_riscpc_firmware_catalogue(&rpc_count);
#if defined(ARCDROID_TEST_FAULTS)
   if (arc_test_rpc_catalogue) { rpc_table=arc_test_rpc_catalogue; rpc_count=arc_test_rpc_catalogue_count; }
#endif
   ArcRpcCompatibility rpc_discs[ARC_PLAYLIST_MAX_ENTRIES];
   bool rpc_scan_needed = false;
   for (unsigned i=0; i<arc_media_count; ++i) {
      rpc_discs[i] = arc_media[i].compatibility;
      if (arc_riscpc_notice_required(rpc_discs[i])) rpc_scan_needed = true;
   }
   ArcRpcFirmwareInventory rpc_inventory = {0};
   if (rpc_scan_needed) rpc_inventory = arc_riscpc_inventory_scan(arc_vfs,
      firmware_dir, rpc_table, rpc_count);
   ArcRpcSelection rpc_selection = arc_riscpc_select(rpc_discs, arc_media_count,
      &rpc_inventory, ARC_RPC_SELECT_AUTO, ARC_RPC_OS_AUTO);
   if (arc_log) {
      char diagnostic[256];
      arc_riscpc_selection_diagnostic(&rpc_selection, diagnostic, sizeof(diagnostic));
      arc_log(RETRO_LOG_INFO, "%s", diagnostic);
   }
   search.requested_os = arc_core_options.requested_os;
   search.machine = arc_core_options.machine;
   arc_io_walk(firmware_dir, 8u, arc_visit_firmware, &search);
   arc_finish_firmware_search(&search);
   if (!search.match)
   {
      if (search.suspicious_size)
         snprintf(error, sizeof(error), "Unrecognised firmware size %zu bytes; nearest expected size is %zu bytes.",
                  search.suspicious_size, search.nearest_size);
      else
      {
         const ArcMachineDescriptor *requested_machine = arc_machine_descriptor(arc_core_options.machine);
         if (arc_core_options.requested_os != ARC_OS_UNKNOWN)
            snprintf(error, sizeof(error), "Missing %s firmware in <system>/arcdroid/firmware.",
                     arc_os_name(arc_core_options.requested_os));
         else if (requested_machine->io == ARC_IO_NEW_82C711)
            snprintf(error, sizeof(error),
                     "%s requires RISC OS 3.10 or later firmware; found %s",
                     requested_machine->name,
                     search.best_available == ARC_OS_UNKNOWN ? "none" :
                     arc_os_name(search.best_available));
         else
            snprintf(error, sizeof(error), "Missing RISC OS 3.11 firmware in <system>/arcdroid/firmware.");
      }
      arc_message(error, RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   arc_firmware = search.data;
   arc_firmware_size = search.size;
   const ArcMachineDescriptor *launch_machine = arc_machine_descriptor(arc_core_options.machine);
   if (!arc_launch_plan_resolve(&arc_plan, arc_media[arc_image_index].info.kind,
                                search.match->os, arc_content_digest.sha1,
                                launch_machine->name, arc_cpu_name(arc_core_options.cpu),
                                arc_core_options.request_a3010_joystick, error, sizeof(error)))
   {
      arc_message(error, RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   arc_plan.pointer_relative = arc_core_options.pointer_relative;
   arc_plan.disk_writes = arc_core_options.disk_writes;
   if (arc_log)
      arc_log(RETRO_LOG_INFO,
              "Launch options: machine=%s cpu=%s memory=%uKiB os=%s joystick=%s pointer=%s disk_writes=%s\n",
              launch_machine->name, arc_cpu_name(arc_plan.cpu), arc_plan.memory_kib,
              arc_os_name(arc_plan.os),
              arc_plan.a3010_joystick ? "A3010" : "Keyboard",
              arc_plan.pointer_relative ? "relative" : "disabled",
              arc_plan.disk_writes == ARC_DISK_WRITES_SHADOW ? "shadow" :
              arc_plan.disk_writes == ARC_DISK_WRITES_ORIGINAL ? "original" : "off");
   char cmos_dir[4096];
   int cmos_written = snprintf(cmos_dir, sizeof(cmos_dir), "%s/arcdroid/cmos", save_dir);
   int path_written = cmos_written >= 0 && (size_t)cmos_written < sizeof(cmos_dir) ?
      snprintf(arc_cmos_path, sizeof(arc_cmos_path), "%s/%s.%s.%s.bin", cmos_dir,
               launch_machine->name,
               arc_os_slug(arc_plan.os), arc_plan.content_id) : -1;
   if (cmos_written < 0 || (size_t)cmos_written >= sizeof(cmos_dir) ||
       !arc_io_make_path(cmos_dir) || path_written < 0 ||
       (size_t)path_written >= sizeof(arc_cmos_path))
   {
      arc_message("Unable to prepare persistent CMOS storage.", RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   arc_seed_cmos();
   arc_session = ARC_SESSION_PREPARED;
   {
      if (!arc_refresh_jfd(&arc_media[arc_image_index], arc_plan.disk_writes,
                           arc_core_options.jfd_write_policy,
                           error, sizeof(error)))
      {
         arc_message(error, RETRO_LOG_ERROR);
         arc_release_session();
         return false;
      }
      bool adffs = arc_prepare_adffs(true, arc_core_options.jfd_write_policy);
      arc_hd_reconcile();
      arc_finish_jfd_preparation(&arc_media[arc_image_index], !adffs,
                                 arc_core_options.jfd_write_policy);
      bool choosing = !adffs && arc_prepare_launch(&arc_media[arc_image_index], false,
                                                   arc_core_options.launch, false);
      if (choosing)
      {
         arc_session = ARC_SESSION_CHOOSING;
         arc_loaded = true;
         arc_ejected = false;
         arc_chooser_reset_input();
         arc_av_transition_pending = !negotiate;
      }
      else if (!arc_resolve_inputs(&arc_core_options, false))
      {
         arc_message("Unable to resolve ArcDroid input options.", RETRO_LOG_ERROR);
         arc_release_session();
         return false;
      }
      else
      {
         ArcEngineMedia media = arc_engine_media(&arc_media[arc_image_index]);
         if (!arc_construct_machine(launch_machine, &media,
                                    error, sizeof(error)))
         {
            if (arc_boot_record_hit) arc_boot_record_invalidate();
            arc_message(error[0] ? error : "Unable to start the Arculator machine.", RETRO_LOG_ERROR);
            arc_release_session();
            return false;
         }
      }
   }
   arc_loaded = true;
   arc_ejected = false;
   memset(arc_frame, 0, sizeof(arc_frame));
   arc_frame_number = 0u;
   arc_audio_fraction = 0u;
   /* A frontend retains the last advertised A/V state across retro_reset().
    * Keep that state as our comparison baseline for a non-negotiating reload;
    * the first genuinely different completed field will then notify it once. */
   if (negotiate && arc_session == ARC_SESSION_RUNNING)
      (void)arc_engine_get_av_info(&arc_video_width, &arc_video_height, &arc_video_rate);
   else if (negotiate && arc_session == ARC_SESSION_CHOOSING)
   {
      arc_video_width = ARC_WIDTH;
      arc_video_height = ARC_HEIGHT;
      arc_video_rate = ARC_RATE;
   }
   arc_fault_reported = false;
   arc_vidc_warning_reported = false;
   arc_vidc_transient_logged = false;
   arc_vidc_incomplete_us = 0u;
   arc_audio_warning_reported = false;
   if (arc_session == ARC_SESSION_RUNNING &&
       !arc_osk_prepare_surface(arc_video_width, arc_video_height))
      arc_osk_presentation_failure();
   snprintf(error, sizeof(error), "Firmware: %s; CRC32 %08x; SHA-1 %s",
            arc_os_name(search.match->os), search.match->crc32, search.match->sha1);
   arc_message(error, RETRO_LOG_INFO);
   return true;
}

#if defined(ARCDROID_TEST_PLANS)
static bool arc_rpc_plan_media(const char *content,const char *helper,char *error,size_t size)
{
    /* A preparation reset may follow a failed final attachment. The engine
     * already detached its old views; release retained frontend preparations
     * before constructing replacements. Published staging files stay owned. */
    for(unsigned i=0;i<ARC_PLAYLIST_MAX_ENTRIES;++i)arc_jfd_free(&arc_media[i].jfd);
    memset(arc_media,0,sizeof(arc_media));arc_media_count=0;
    arc_jfd_free(&arc_rpc_helper_entry.jfd);memset(&arc_rpc_helper_entry,0,sizeof(arc_rpc_helper_entry));
    arc_folder_set_active=arc_zip_active=false;arc_zip_path[0]=0;arc_zip_adffs_raw_discard();
    arc_loaded=false;
    const char *save=NULL;
    if(!arc_environment || !arc_environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,&save) ||
       !save || !*save || strlen(save)>=sizeof(arc_save_path) ||
       !arc_read_options(&arc_core_options,error,size))return false;
    strcpy(arc_save_path,save);arc_preparing_zero_tail=arc_core_options.short_tail_zero;
    arc_plan.disk_writes=arc_core_options.disk_writes;
    if(*content && !arc_prepare_media_list(content,arc_plan.disk_writes,error,size))return false;
    if(*helper){
        if(!arc_prepare_source(helper,NULL,arc_plan.disk_writes,true,NULL,&arc_rpc_helper_entry,error,size))return false;
        if(arc_rpc_helper_entry.info.kind==ARC_MEDIA_JFD){
            snprintf(error,size,"RPC drive 1 requires raw helper media");return false;
        }
        ArcEngineMedia h=arc_engine_media(&arc_rpc_helper_entry);
        if(!arc_engine_rpc_mount(1,&h,error,size))return false;
    }
    arc_image_index=0;arc_ejected=arc_media_count==0;
    if(arc_media_count){
        ArcMediaEntry *entry=&arc_media[0];
        if(!arc_refresh_jfd(entry,arc_plan.disk_writes,arc_core_options.jfd_write_policy,error,size))return false;
        arc_finish_jfd_preparation(entry,true,arc_core_options.jfd_write_policy);
        ArcEngineMedia m=arc_engine_media(entry);
        if(!arc_engine_media_insert(&m,error,size))return false;
    }
    arc_loaded=true;arc_session=ARC_SESSION_RUNNING;return true;
}
static bool arc_rpc_plan_reset_media(char *error,size_t size)
{
    if(!arc_loaded)return true;
    ArcCoreOptions updated;
    if(!arc_read_options(&updated,error,size))return false;
    /* Retire every borrowed presentation before rebuilding its policy view. */
    arc_engine_rpc_eject(0);arc_engine_rpc_eject(1);
    bool was_ejected=arc_ejected;arc_ejected=true;
    unsigned selected=arc_image_index;
    arc_preparing_zero_tail=updated.short_tail_zero;
    if(updated.disk_writes!=arc_plan.disk_writes){
        if(arc_zip_active){if(!arc_reprepare_zip_media(updated.disk_writes,&selected,error,size))return false;}
        else if(arc_folder_set_active){if(!arc_reprepare_folder_media(updated.disk_writes,&selected,error,size))return false;}
        else for(unsigned i=0;i<arc_media_count;++i){
            if(!arc_media[i].populated)continue;
            ArcMediaEntry next;
            if(!arc_prepare_source(arc_media[i].source,arc_media[i].label,updated.disk_writes,true,NULL,&next,error,size))return false;
            arc_jfd_free(&arc_media[i].jfd);arc_media[i]=next;
        }
        if(arc_rpc_helper_entry.populated){
            ArcMediaEntry next;
            if(!arc_prepare_source(arc_rpc_helper_entry.source,NULL,updated.disk_writes,true,NULL,&next,error,size))return false;
            arc_rpc_helper_entry=next;
        }
    }
    for(unsigned i=0;i<arc_media_count;++i)if(arc_media[i].populated)arc_entry_tail_presentation(&arc_media[i],updated.short_tail_zero);
    if(arc_rpc_helper_entry.populated)arc_entry_tail_presentation(&arc_rpc_helper_entry,updated.short_tail_zero);
    arc_core_options=updated;arc_plan.disk_writes=updated.disk_writes;arc_image_index=selected;
    arc_jfd_tab_pending_notice_shown=false;
    if(arc_rpc_helper_entry.populated){ArcEngineMedia h=arc_engine_media(&arc_rpc_helper_entry);
        if(!arc_engine_rpc_mount(1,&h,error,size))return false;}
    if(!was_ejected && selected<arc_media_count){
        ArcMediaEntry *entry=&arc_media[selected];
        if(!arc_refresh_jfd(entry,updated.disk_writes,updated.jfd_write_policy,error,size))return false;
        arc_finish_jfd_preparation(entry,true,updated.jfd_write_policy);
        ArcEngineMedia m=arc_engine_media(entry);
        if(!arc_engine_media_insert(&m,error,size))return false;
        arc_ejected=false;
    }
    return true;
}
static void arc_rpc_keep_media_options(ArcCoreOptions *updated,bool notify)
{
    if(!arc_loaded)return;
    bool changed=updated->disk_writes!=arc_plan.disk_writes ||
        updated->jfd_write_policy!=arc_core_options.jfd_write_policy ||
        updated->short_tail_zero!=arc_core_options.short_tail_zero ||
        updated->jfd_omitted_strict!=arc_core_options.jfd_omitted_strict;
    if(notify && changed && !arc_jfd_tab_pending_notice_shown){
        arc_message("Disk options change will apply after reset or reload.",RETRO_LOG_INFO);
        arc_jfd_tab_pending_notice_shown=true;
    }
    updated->disk_writes=arc_plan.disk_writes;
    updated->jfd_write_policy=arc_core_options.jfd_write_policy;
    updated->short_tail_zero=arc_core_options.short_tail_zero;
    updated->jfd_omitted_strict=arc_core_options.jfd_omitted_strict;
}
static char arc_plan_terminal[512];
static unsigned arc_plan_terminal_refresh;
static void arc_plan_notice(const char *message,bool error)
{
   /* Pending non-errors are progress; running non-errors are timed advisories.
    * An advisory must not replace a retained terminal status. */
   if(!error && message[0] && arc_test_plan_cmos_phase()==2){arc_message(message,RETRO_LOG_INFO);return;}
   if(error){
      snprintf(arc_plan_terminal,sizeof(arc_plan_terminal),"%s",message);
      if(arc_log)arc_log(RETRO_LOG_ERROR,"%s\n",message);
   }else{arc_plan_terminal[0]=0;arc_plan_terminal_refresh=0;}
   struct retro_message_ext status={message,error?UINT_MAX:message[0]?60000u:1u,
      error?3u:2u,error?RETRO_LOG_ERROR:RETRO_LOG_INFO,
      RETRO_MESSAGE_TARGET_OSD,RETRO_MESSAGE_TYPE_STATUS,-1};
   if(arc_environment && arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT,&status))return;
   if(arc_environment){struct retro_message fallback={message,message[0]?120u:1u};
      (void)arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE,&fallback);}
   if(error)arc_plan_terminal_refresh=60;
}
static void arc_plan_notice_tick(void)
{
   /* Extended terminal status is retained until explicit clear. Ordinary
    * messages expire, so refresh that fallback without repeating the log. */
   if(!arc_plan_terminal_refresh || --arc_plan_terminal_refresh)return;
   struct retro_message fallback={arc_plan_terminal,120u};
   if(arc_environment)(void)arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE,&fallback);
   arc_plan_terminal_refresh=60;
}
static void arc_plan_log(const char *message) { if(arc_log)arc_log(RETRO_LOG_ERROR,"%s\n",message); }
#endif
RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
#if defined(ARCDROID_TEST_PLANS)
   if (game && arc_test_plan_path(game->path)) {
      arc_loaded = false;
      arc_release_session();
      const char *system = NULL, *save = NULL; char error[256]={0};
      enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
      static const struct retro_keyboard_callback keyboard = {arc_keyboard};
      if (!arc_environment || !arc_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format)) return false;
      arc_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system);
      arc_environment(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, (void *)&keyboard);
      arc_environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save);
#if defined(_WIN32)
      if(!arc_test_crash_locator_start(save,arc_log))
         arc_message("RPC test crash locator unavailable",RETRO_LOG_WARN);
#endif
      arc_test_plan_set_notice(arc_plan_notice);
      arc_test_plan_set_log(arc_plan_log);
      const ArcTestPlanMedia callbacks={arc_rpc_plan_media,arc_disk_set_eject,arc_disk_set_index,arc_disk_replace,arc_disk_add};
      arc_test_plan_set_media(&callbacks);
      if (!arc_test_plan_load(game->path, system, save, error, sizeof(error))) {
         arc_message(error, RETRO_LOG_ERROR); arc_release_session(); return false;
      }
      arc_rpc_av_attempted = false; arc_fault_reported = false;
      return true;
   }
#endif
   return arc_load_game(game, true);
}

RETRO_API void retro_unload_game(void)
{
   arc_boot_persist_seed();
   arc_persist_cmos();
   if (arc_engine_active() && arc_log)
   {
      uint64_t underruns = 0u, overruns = 0u;
      arc_engine_audio_stats(&underruns, &overruns);
      arc_log(RETRO_LOG_INFO, "Audio counters: underrun_frames=%llu overrun_frames=%llu cmos_locked_attempts=%u cmos_locked_changes=%u\n",
              (unsigned long long)underruns, (unsigned long long)overruns,
              arc_cmos_ignored_attempts, arc_cmos_ignored_changes);
   }
   arc_loaded = false;
   arc_release_session();
   arc_osk_reset(&arc_osk, &arc_inputs);
   arc_osk_free_surface();
   arc_osk_present_dirty = false;
   arc_input_sampling_reset(false);
   arc_guest_time_us = 0u;
#if defined(ARCDROID_TEST_PLANS) && defined(_WIN32)
   arc_test_crash_locator_stop();
#endif
}
RETRO_API void retro_reset(void)
{
#if defined(ARCDROID_TEST_PLANS)
   if(arc_test_plan_pending()) {
      arc_error_hint_generation=arc_error_seen_generation=0;arc_error_hint_state=0;
      arc_disc_owned=false;arc_disc_previous_index=0;arc_disc_swapped_us=0;
      arc_disc_notice_read=arc_disc_notice_write=0;
      arc_input_reset(&arc_inputs);memset(arc_keyboard_raw,0,sizeof(arc_keyboard_raw));
      arc_input_sampling_reset(false);arc_guest_time_us=0;arc_test_plan_reset();
      arc_rpc_av_attempted=false;return;
   }
#endif
   if(arc_engine_is_rpc() && arc_engine_active()) {
      arc_error_hint_generation=arc_error_seen_generation=0;arc_error_hint_state=0;
      arc_disc_owned=false;arc_disc_previous_index=0;arc_disc_swapped_us=0;
      arc_disc_notice_read=arc_disc_notice_write=0;
#if defined(ARCDROID_TEST_PLANS)
      if(arc_test_plan_cmos_phase())arc_plan_notice("",false);
#endif
      char error[192];
      arc_input_reset(&arc_inputs);
      memset(arc_keyboard_raw,0,sizeof(arc_keyboard_raw));
      arc_input_sampling_reset(false);
      arc_guest_time_us=0;
      if(!arc_engine_reset(error,sizeof(error)))arc_message(error,RETRO_LOG_ERROR);
#if defined(ARCDROID_TEST_FAULTS)
      else {
#if defined(ARCDROID_TEST_PLANS)
         if(!arc_rpc_plan_reset_media(error,sizeof(error)))arc_message(error,RETRO_LOG_ERROR);
#endif
         arc_test_plan_reset_input();
      }
#endif
      arc_rpc_av_attempted=false;return;
   }
   arc_engine_launch_outcome_cancel("reset");
   arc_engine_disc_clear();
   arc_disc_invalidate();
   arc_disc_notice_read = arc_disc_notice_write = 0u;
   if (arc_hd_observe_target || arc_hd_observe_copy) {
      arc_hd_reset_cancel_pending=true;
      arc_hd_observe_target=arc_hd_observe_copy=false;
      arc_hd_last_outcome_state=ARC_OUTCOME_CANCEL;
   }
   arc_boot_record_hit = false;
   arc_boot_stamp_ready = false;
   bool was_boot_chooser = arc_boot_post_chooser;
   uint8_t held_cmos[ARC_CMOS_RECORD_SIZE];
   if (was_boot_chooser)
   {
      memcpy(held_cmos, arc_cmos_prepared, sizeof(held_cmos));
      arc_boot_persist_seed();
   }
   arc_boot_attempt = true;
   arc_boot_post_chooser = false;
   arc_boot_recovery_phase = false;
   if (!arc_loaded) { arc_disc_owned = false; return; }
   arc_chooser_release_gate = false;
   bool osk_session_disabled = arc_osk.disabled;
   arc_osk_reset(&arc_osk, &arc_inputs);
   arc_osk.disabled = osk_session_disabled;
   arc_osk_present_dirty = true;
   char error[512] = {0};
   ArcCoreOptions updated;
   uint8_t *new_firmware = NULL;
   size_t new_firmware_size = 0u;
   const ArcFirmwareFingerprint *firmware_match = NULL;
   ArcDigest frozen_identity = arc_content_digest;
   bool was_ejected = arc_ejected;
   bool was_disc_owned = arc_disc_owned;
   unsigned selected = was_disc_owned ? arc_disc_launch_index : arc_image_index;
   arc_disc_owned = false;
   bool launch_reselected;
   bool torn_down = false;
   ArcSessionState previous_session = arc_session;

   arc_persist_cmos();
   if (!arc_read_options(&updated, error, sizeof(error))) goto failed;
   ArcMachine reset_choice = updated.machine_auto ? arc_session_machine :
      updated.requested_machine;
   if (!arc_options_resolve(&updated, reset_choice, error, sizeof(error))) goto failed;
   for (unsigned i = 0u; i < arc_media_count; ++i)
      if (!arc_machine_accepts_media(reset_choice, &arc_media[i],
                                      error, sizeof(error))) goto failed;
   if (!arc_select_firmware(updated.requested_os, reset_choice, &new_firmware,
                            &new_firmware_size, &firmware_match,
                            error, sizeof(error)))
      goto failed;
   launch_reselected = updated.launch != arc_core_options.launch;

   arc_engine_stop();
   arc_session = ARC_SESSION_PREPARED;
   torn_down = true;
   arc_preparing_zero_tail = updated.short_tail_zero;
   if (updated.disk_writes != arc_core_options.disk_writes)
   {
      if (arc_zip_active)
      {
         if (!arc_reprepare_zip_media(updated.disk_writes, &selected,
                                      error, sizeof(error)))
            goto failed;
      }
      else if (arc_folder_set_active)
      {
         if (!arc_reprepare_folder_media(updated.disk_writes, &selected,
                                         error, sizeof(error)))
            goto failed;
      }
      else
      {
         for (unsigned i = 0u; i < arc_media_count; ++i)
         {
            if (!arc_media[i].populated) continue;
            ArcMediaEntry replacement;
            if (!arc_prepare_source(arc_media[i].source, arc_media[i].label,
                                    updated.disk_writes, true, NULL,
                                    &replacement, error, sizeof(error)))
               goto failed;
            arc_jfd_free(&arc_media[i].jfd);
            arc_media[i] = replacement;
         }
      }
   }
   if (!was_ejected && selected < arc_media_count &&
       !arc_refresh_jfd(&arc_media[selected], updated.disk_writes,
                        updated.jfd_write_policy,
                        error, sizeof(error)))
      goto failed;
   for (unsigned i = 0u; i < arc_media_count; ++i)
      if (arc_media[i].populated)
         arc_entry_tail_presentation(&arc_media[i], updated.short_tail_zero);

   free(arc_firmware);
   arc_firmware = new_firmware;
   new_firmware = NULL;
   arc_firmware_size = new_firmware_size;
   arc_core_options = updated;
   arc_jfd_tab_pending_notice_shown = false;
   arc_session_machine = reset_choice;
   ArcMediaKind plan_media = selected < arc_media_count && arc_media[selected].populated ?
      arc_media[selected].info.kind :
      (arc_media_count ? arc_media[0].info.kind : arc_plan.media);
   const ArcMachineDescriptor *reset_machine = arc_machine_descriptor(updated.machine);
   if (!arc_launch_plan_resolve(&arc_plan, plan_media, firmware_match->os,
                                frozen_identity.sha1, reset_machine->name,
                                arc_cpu_name(updated.cpu), updated.request_a3010_joystick,
                                error, sizeof(error)))
      goto failed;
   arc_plan.pointer_relative = updated.pointer_relative;
   arc_plan.disk_writes = updated.disk_writes;
   arc_content_digest = frozen_identity;

   char cmos_dir[ARC_MEDIA_PATH_CAPACITY];
   int cmos_written = snprintf(cmos_dir, sizeof(cmos_dir), "%s/arcdroid/cmos", arc_save_path);
   int path_written = cmos_written >= 0 && (size_t)cmos_written < sizeof(cmos_dir) ?
      snprintf(arc_cmos_path, sizeof(arc_cmos_path), "%s/%s.%s.%s.bin", cmos_dir,
               reset_machine->name,
               arc_os_slug(arc_plan.os), arc_plan.content_id) : -1;
   if (cmos_written < 0 || (size_t)cmos_written >= sizeof(cmos_dir) ||
       !arc_io_make_path(cmos_dir) || path_written < 0 ||
       (size_t)path_written >= sizeof(arc_cmos_path))
   { snprintf(error, sizeof(error), "Unable to prepare persistent CMOS storage."); goto failed; }
   arc_seed_cmos();
   if (was_boot_chooser)
      memcpy(arc_cmos_prepared, held_cmos, sizeof(held_cmos));
   bool adffs = !was_ejected && selected == 0u &&
      arc_prepare_adffs(false, arc_core_options.jfd_write_policy);
   arc_hd_reconcile();
   if (selected < arc_media_count && !was_ejected)
      arc_finish_jfd_preparation(&arc_media[selected], !adffs,
                                 arc_core_options.jfd_write_policy);
   ArcEngineMedia mounted;
   ArcEngineMedia *mounted_pointer = NULL;
   if (!was_ejected && selected < arc_media_count && arc_media[selected].populated)
   {
      mounted = arc_engine_media(&arc_media[selected]);
      mounted_pointer = &mounted;
   }
   arc_image_index = selected <= arc_media_count ? selected : arc_media_count;
   bool choosing = !adffs && arc_prepare_launch(
      mounted_pointer ? &arc_media[selected] : NULL, was_ejected,
      updated.launch, launch_reselected);
   if (choosing)
   {
      arc_session = ARC_SESSION_CHOOSING;
      arc_chooser_reset_input();
      if (previous_session != ARC_SESSION_CHOOSING)
         arc_av_transition_pending = true;
   }
   else
   {
      if (!arc_resolve_inputs(&arc_core_options, true))
      { snprintf(error, sizeof(error), "Unable to resolve ArcDroid input options."); goto failed; }
      if (!arc_construct_machine(reset_machine, mounted_pointer,
                                 error, sizeof(error))) goto failed;
      if (previous_session == ARC_SESSION_CHOOSING)
         arc_av_transition_pending = true;
   }
   arc_loaded = true;
   arc_ejected = was_ejected;
   arc_input_sampling_reset(false);
   arc_guest_time_us = 0u;
   arc_frame_number = 0u;
   arc_audio_fraction = 0u;
   arc_fault_reported = false;
   arc_vidc_warning_reported = false;
   arc_vidc_transient_logged = false;
   arc_vidc_incomplete_us = 0u;
   arc_audio_warning_reported = false;
   return;
failed:
   free(new_firmware);
   if (arc_boot_record_hit) arc_boot_record_invalidate();
   arc_message(error[0] ? error : "Unable to reset the ArcDroid session.", RETRO_LOG_ERROR);
   if (!torn_down) { arc_disc_owned = was_disc_owned; return; }
   arc_loaded = false;
   arc_release_session();
   arc_input_sampling_reset(false);
}

static void arc_send_video(const void *pixels, unsigned width, unsigned height,
                           size_t pitch)
{
   if (!arc_video) return;
   if (pixels)
   {
      if (!width || width > ARC_PRESENT_MAX_WIDTH || !height ||
          height > ARC_PRESENT_MAX_HEIGHT ||
          pitch < (size_t)width * sizeof(uint32_t) ||
          pitch > ARC_PRESENT_MAX_WIDTH * sizeof(uint32_t)) return;
      for (unsigned row = 0u; row < height; ++row)
         memcpy((uint8_t *)arc_present_frame + (size_t)row * pitch,
                (const uint8_t *)pixels + (size_t)row * pitch,
                (size_t)width * sizeof(uint32_t));
      pixels = arc_present_frame;
      arc_video_pitch = pitch;
   }
   arc_video(pixels, width, height, pitch);
}

static void arc_present(const ArcEngineFrame *frame)
{
#if defined(ARCDROID_TEST_FAULTS)
   arc_test_presented_osk_notice = 0u;
#endif
   if (!arc_video || !frame) return;
   if (!arc_osk.visible)
   {
      bool duplicate = !frame->field_completed && arc_can_dupe &&
                       !arc_osk_present_dirty;
      arc_send_video(duplicate ? NULL : frame->pixels, frame->width,
                     frame->height, frame->pitch);
      arc_osk_present_dirty = false;
      return;
   }
   if (frame->width < 320u)
   {
      arc_osk_close(&arc_osk, &arc_inputs);
      arc_error_pad_gate = true;
      arc_osk_restore_gameplay();
      arc_osk_report_too_narrow();
      arc_send_video(frame->pixels, frame->width, frame->height, frame->pitch);
      arc_osk_present_dirty = false;
      return;
   }
   if (!arc_osk_prepare_surface(frame->width, frame->height) ||
       ((arc_osk.width != frame->width || arc_osk.height != frame->height) &&
        !arc_osk_layout(&arc_osk, frame->width, frame->height)))
   {
      arc_osk_presentation_failure();
      arc_send_video(frame->pixels, frame->width, frame->height, frame->pitch);
      arc_osk_present_dirty = false;
      return;
   }
   if (!frame->field_completed && arc_can_dupe && !arc_osk_present_dirty)
   {
      arc_send_video(NULL, frame->width, frame->height, frame->pitch);
      return;
   }
   if (!frame->pixels || frame->pitch < (size_t)frame->width * sizeof(uint32_t))
   {
      arc_osk_presentation_failure();
      arc_send_video(frame->pixels, frame->width, frame->height, frame->pitch);
      arc_osk_present_dirty = false;
      return;
   }
#if defined(ARCDROID_TEST_FAULTS)
   uint64_t source_before = !arc_osk_source_checked ?
      arc_osk_source_hash(frame) : 0u;
#endif
   for (unsigned row = 0u; row < frame->height; ++row)
      memcpy(arc_osk_surface + (size_t)row * frame->width,
             (const uint8_t *)frame->pixels + (size_t)row * frame->pitch,
             (size_t)frame->width * sizeof(uint32_t));
   const char *header = arc_input_resolution.custom ?
      "Custom controls" : "Default controls";
   if (!arc_osk_render(&arc_osk, arc_osk_surface, frame->width, frame->height,
                       (size_t)frame->width * sizeof(uint32_t), header))
   {
      arc_osk_presentation_failure();
      arc_send_video(frame->pixels, frame->width, frame->height, frame->pitch);
   }
   else
   {
      arc_send_video(arc_osk_surface, frame->width, frame->height,
                     (size_t)frame->width * sizeof(uint32_t));
#if defined(ARCDROID_TEST_FAULTS)
      arc_test_presented_osk_notice = (arc_osk.busy ? 1u : 0u) |
         (arc_osk.interrupted_visible ? 2u : 0u);
#endif
   }
#if defined(ARCDROID_TEST_FAULTS)
   if (!arc_osk_source_checked)
   {
      if (source_before != arc_osk_source_hash(frame)) arc_osk_source_unchanged = false;
      arc_osk_source_checked = true;
   }
#endif
   arc_osk_present_dirty = false;
}

/* RPC callbacks run after the contained engine returns. Cache attempted AV
 * transitions even if rejected/unavailable: a rejection must not cause a driver
 * restart attempt on every frame. A replacement environment callback permits
 * one fresh attempt. Size+rate changes require only the full AV notification. */
static void arc_rpc_present_run(void)
{
   static uint64_t input_generation;
   if(input_generation!=arc_engine_session_generation()) {
      input_generation=arc_engine_session_generation();
      arc_rpc_fault_audio_fraction=0;
      arc_input_reset(&arc_inputs);arc_input_sampling_reset(false);
      memset(arc_keyboard_raw,0,sizeof(arc_keyboard_raw));
      arc_guest_time_us=0;
      char error[192];
      ArcCoreOptions updated;
      if(arc_read_options(&updated,error,sizeof(error))){
#if defined(ARCDROID_TEST_PLANS)
         arc_rpc_keep_media_options(&updated,false);
#endif
         arc_core_options=updated;
      }
      (void)arc_resolve_inputs(&arc_core_options,false);
      arc_disc_setup();
   }

   if (!arc_engine_get_av_info(NULL, NULL, NULL)) {
      arc_rpc_av_attempted = false;
      if (!arc_engine_faulted(NULL)) arc_fault_reported = false;
   }
   arc_engine_set_log_callback(arc_engine_log, NULL);
   bool options_updated=false;
   if(arc_environment && arc_environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE,&options_updated) && options_updated) {
      ArcCoreOptions updated;char error[192];
      if(arc_read_options(&updated,error,sizeof(error)) && arc_resolve_inputs(&updated,true)){
#if defined(ARCDROID_TEST_PLANS)
         arc_rpc_keep_media_options(&updated,true);
#endif
         arc_core_options=updated;
      }else arc_message(error[0]?error:"Unable to resolve RPC input options.",RETRO_LOG_ERROR);
   }
   if (arc_input_poll) arc_input_poll();
   arc_poll_inputs();
#if defined(ARCDROID_TEST_FAULTS)
   arc_test_plan_input(&arc_inputs);
#endif
   ArcEngineFrame frame;
   bool ran = arc_engine_run_frame(&arc_inputs, &frame);
#if defined(ARCDROID_TEST_PLANS)
   arc_test_plan_diagnostics();
#endif
   arc_disc_boundary();
#if defined(ARCDROID_TEST_PLANS)
   arc_test_plan_hd_frame();

#endif
   if (!ran)
   {
      const char *fault = NULL;
      if (!arc_fault_reported && arc_engine_faulted(&fault))
      {
         arc_message(fault && fault[0] ? fault : "The emulated machine faulted.", RETRO_LOG_ERROR);
         arc_fault_reported = true;
      }
      memset(&frame, 0, sizeof(frame));
      frame.pixels = arc_frame; frame.width = ARC_WIDTH; frame.height = ARC_HEIGHT;
      frame.pitch = ARC_WIDTH * sizeof(uint32_t); frame.elapsed_us = 20000u;
   }
   if (frame.field_completed && frame.width && frame.height)
   {
      bool timing = !arc_rpc_av_attempted || fabs(frame.refresh_hz - arc_video_rate) > 0.01;
      bool geometry = frame.width != arc_video_width || frame.height != arc_video_height;
      struct retro_system_av_info av;
      retro_get_system_av_info(&av);
      av.geometry.base_width = frame.width; av.geometry.base_height = frame.height;
      av.timing.fps = frame.refresh_hz;
      bool accepted = true;
      if (timing && arc_environment)
         accepted = arc_environment(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
      else if (geometry && arc_environment)
         accepted = arc_environment(RETRO_ENVIRONMENT_SET_GEOMETRY, &av.geometry);
      if (!accepted && arc_log)
         arc_log(RETRO_LOG_WARN, "RPC frontend rejected %s update; scanout continues.\n", timing ? "AV" : "geometry");
      arc_rpc_av_attempted = true;
      arc_video_width = frame.width; arc_video_height = frame.height;
      /* Retain the last announced rate for hysteresis; compare against it,
       * not the preceding frame, so small cumulative changes are detected. */
      if (timing) arc_video_rate = frame.refresh_hz;
      arc_present(&frame);
   }
   else if (!ran) arc_present(&frame);
   else if (arc_video && arc_can_dupe)
      arc_video(NULL, arc_video_width, arc_video_height, arc_video_pitch);
   size_t silence_remaining = 0;
   if (!ran) {
      double samples = ARC_AUDIO_RATE / arc_video_rate + arc_rpc_fault_audio_fraction;
      size_t whole = (size_t)samples;
      arc_rpc_fault_audio_fraction = samples - (double)whole;
      silence_remaining = whole;
   }
   if(ran) { arc_guest_time_us+=frame.elapsed_us;arc_integrate_stick(frame.elapsed_us); }
   /* Integer engine production is authoritative, including disabled silence.
    * Retire every offer exactly once; never let rejection change guest time. */
   size_t capacity = sizeof(arc_audio_frame) / (2u * sizeof(arc_audio_frame[0]));
   size_t n;
   do {
      /* A slow announced field can exceed the engine queue. Offer silence in
       * frontend-sized chunks so no samples are dropped at that boundary. */
      if (silence_remaining) {
         size_t chunk = silence_remaining < capacity ? silence_remaining : capacity;
         arc_engine_audio_fault_silence(chunk);
         silence_remaining -= chunk;
      }
      while ((n = arc_engine_audio_peek(arc_audio_frame, capacity)) != 0)
      {
      size_t accepted = 0;
      if (arc_audio_batch) accepted = arc_audio_batch(arc_audio_frame, n);
      else if (arc_audio) {
         for (size_t i = 0; i < n; ++i) arc_audio(arc_audio_frame[i*2], arc_audio_frame[i*2+1]);
         accepted = n;
      }
      arc_engine_audio_commit(n, accepted);
      }
   } while (silence_remaining);
}

RETRO_API void retro_run(void)
{
#if defined(ARCDROID_TEST_PLANS)
   arc_plan_notice_tick();
#endif
#if defined(ARCDROID_TEST_PLANS)
   arc_test_plan_hd_begin_frame();
   if(arc_test_plan_pending()) {
      /* Match the bounded 60 Hz capture slices and 48 kHz silence to the
       * announced frontend cadence, including after an Arculator session. */
      arc_notify_av_change(ARC_WIDTH,ARC_HEIGHT,60.0);
      char error[256];if(!arc_test_plan_slice(error,sizeof(error)))arc_message(error,RETRO_LOG_ERROR);
      memset(arc_frame,0,sizeof(arc_frame));
      if(arc_video)arc_video(arc_frame,ARC_WIDTH,ARC_HEIGHT,ARC_WIDTH*sizeof(uint32_t));
      memset(arc_audio_frame,0,1600*sizeof(int16_t));
      if(arc_audio_batch)arc_audio_batch(arc_audio_frame,800);
      else if(arc_audio)for(unsigned i=0;i<800;++i)arc_audio(0,0);
      return;
   }
#endif
   if (arc_engine_is_rpc() && arc_engine_active()) { arc_rpc_present_run(); return; }
   ArcEngineFrame frame;
   size_t count;
   bool frame_ran;
   bool chooser_exit_transition = false;
   bool options_updated = false;
   if (arc_environment &&
       arc_environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &options_updated) &&
       options_updated)
   {
      ArcCoreOptions updated;
      char option_error[160] = {0};
      bool valid_update = arc_read_options(&updated, option_error, sizeof(option_error));
      if (valid_update)
         valid_update = arc_options_resolve(&updated,
            updated.machine_auto ? arc_session_machine : updated.requested_machine,
            option_error, sizeof(option_error));
      if (!valid_update)
         arc_message(option_error, RETRO_LOG_ERROR);
      else if (updated.pointer_relative != arc_core_options.pointer_relative ||
               updated.pointer_stick != arc_core_options.pointer_stick ||
               updated.pointer_speed != arc_core_options.pointer_speed ||
               updated.pointer_deadzone != arc_core_options.pointer_deadzone)
      {
         arc_core_options.pointer_relative = updated.pointer_relative;
         arc_core_options.pointer_stick = updated.pointer_stick;
         arc_core_options.pointer_speed = updated.pointer_speed;
         arc_core_options.pointer_deadzone = updated.pointer_deadzone;
         arc_plan.pointer_relative = updated.pointer_relative;
         arc_touch_reset();
         arc_stick_x = arc_stick_y = 0;
         arc_stick_fraction_x = arc_stick_fraction_y = 0.0L;
      }
      if (valid_update && arc_loaded &&
          updated.jfd_write_policy != arc_core_options.jfd_write_policy &&
          !arc_jfd_tab_pending_notice_shown)
      {
         arc_message("Write-protect Tab change will apply after reset or reload.",
                     RETRO_LOG_INFO);
         arc_jfd_tab_pending_notice_shown = true;
      }
      if (valid_update)
         memcpy(arc_core_options.left_stick_dpad, updated.left_stick_dpad,
                sizeof(updated.left_stick_dpad));
      if (valid_update && (memcmp(updated.pad, arc_core_options.pad, sizeof(updated.pad)) ||
          updated.osk_toggle != arc_core_options.osk_toggle))
      {
         if (!arc_resolve_inputs(&updated, true))
            arc_message("Unable to resolve ArcDroid input options.", RETRO_LOG_ERROR);
         else
         {
            memcpy(arc_core_options.pad, updated.pad, sizeof(updated.pad));
            arc_core_options.osk_toggle = updated.osk_toggle;
         }
      }
   }
   if (arc_input_poll) arc_input_poll();
   if (arc_session == ARC_SESSION_CHOOSING)
   {
      if (arc_av_transition_pending)
      {
         arc_notify_av_change(ARC_WIDTH, ARC_HEIGHT, ARC_RATE);
         arc_av_transition_pending = false;
      }
      arc_poll_chooser();
      if (!arc_launch_chooser_render(&arc_chooser, &arc_chooser_discovery,
                                     arc_frame, ARC_WIDTH, ARC_HEIGHT,
                                     ARC_WIDTH * sizeof(uint32_t)))
         memset(arc_frame, 0, sizeof(arc_frame));
      arc_send_video(arc_frame, ARC_WIDTH, ARC_HEIGHT,
                     ARC_WIDTH * sizeof(uint32_t));
      memset(arc_audio_frame, 0, 960u * 2u * sizeof(arc_audio_frame[0]));
      if (arc_audio_batch) arc_audio_batch(arc_audio_frame, 960u);
      else if (arc_audio)
         for (unsigned i = 0u; i < 960u; ++i) arc_audio(0, 0);
      return;
   }
   if (arc_session == ARC_SESSION_RUNNING && arc_av_transition_pending)
   {
      chooser_exit_transition = true;
      arc_av_transition_pending = false;
   }
   arc_poll_inputs();
   if (arc_input_take_overflow_report(&arc_inputs))
      arc_message("Input queue overflowed; recovered to current input levels.", RETRO_LOG_WARN);
   if (!arc_loaded || arc_session != ARC_SESSION_RUNNING)
   {
      arc_send_video(arc_frame, ARC_WIDTH, ARC_HEIGHT,
                     ARC_WIDTH * sizeof(uint32_t));
      return;
   }
   (void)arc_hd_outcome_boundary();
   if (!arc_loaded || arc_session != ARC_SESSION_RUNNING)
   {
      arc_send_video(arc_frame, ARC_WIDTH, ARC_HEIGHT,
                     ARC_WIDTH * sizeof(uint32_t));
      return;
   }
   arc_disc_boundary();
   ++arc_frame_number;
#if defined(ARCDROID_TEST_FAULTS)
   frame_ran = arc_engine_run_frame(arc_test_stall_kart ? NULL : &arc_inputs,
                                    &frame);
#else
   frame_ran = arc_engine_run_frame(&arc_inputs, &frame);
#endif
   uint64_t power_on_release_us = 0u;
   bool power_on_boot_action = false;
   if (arc_engine_power_on_take_release(&power_on_release_us,
                                        &power_on_boot_action))
   {
      arc_power_on_shift = false;
      if (arc_log)
         arc_log(RETRO_LOG_INFO,
                 "Power-on: shift released at=%llu reason=%s\n",
                 (unsigned long long)power_on_release_us,
                 power_on_boot_action ? "boot-action" : "timer");
   }
   if (frame_ran && arc_boot_attempt && arc_boot_eligible &&
       arc_engine_boot_watch_state() == ARC_BOOT_WATCH_PROMPT)
   {
      arc_boot_prompt(arc_guest_time_us + frame.elapsed_us);
      if (arc_session == ARC_SESSION_CHOOSING)
      {
         arc_notify_av_change(ARC_WIDTH, ARC_HEIGHT, ARC_RATE);
         arc_av_transition_pending = false;
         if (!arc_launch_chooser_render(&arc_chooser, &arc_chooser_discovery,
                                        arc_frame, ARC_WIDTH, ARC_HEIGHT,
                                        ARC_WIDTH * sizeof(uint32_t)))
            memset(arc_frame, 0, sizeof(arc_frame));
         arc_send_video(arc_frame, ARC_WIDTH, ARC_HEIGHT,
                        ARC_WIDTH * sizeof(uint32_t));
         memset(arc_audio_frame, 0, 960u * 2u * sizeof(arc_audio_frame[0]));
         if (arc_audio_batch) arc_audio_batch(arc_audio_frame, 960u);
         else if (arc_audio)
            for (unsigned i = 0u; i < 960u; ++i) arc_audio(0, 0);
         return;
      }
      if (arc_engine_boot_watch_state() != ARC_BOOT_WATCH_PROMPT)
      {
         if (arc_video)
            arc_video(arc_present_frame, arc_video_width, arc_video_height,
                      arc_video_pitch ? arc_video_pitch : frame.pitch);
         memset(arc_audio_frame, 0, 960u * 2u * sizeof(arc_audio_frame[0]));
         if (arc_audio_batch) arc_audio_batch(arc_audio_frame, 960u);
         else if (arc_audio)
            for (unsigned i = 0u; i < 960u; ++i) arc_audio(0, 0);
         return;
      }
   }
   if (arc_boot_attempt && arc_boot_eligible &&
       arc_engine_boot_watch_state() != arc_boot_last_state)
   {
      ArcBootWatchState state = arc_engine_boot_watch_state();
      arc_boot_last_state = state;
      if (state == ARC_BOOT_WATCH_DISABLED || state == ARC_BOOT_WATCH_EXPIRED ||
          state == ARC_BOOT_WATCH_CANCELLED)
      {
         arc_boot_watch_override = state == ARC_BOOT_WATCH_DISABLED ? "disabled" :
            state == ARC_BOOT_WATCH_EXPIRED ? "expired" : "cancelled";
         arc_launch_log(&arc_boot_discovery, -1, "boot observation ended");
         arc_boot_watch_override = NULL;
      }
   }
   if (!frame_ran)
   {
      arc_engine_boot_watch_cancel();
      const char *fault = NULL;
      if (!arc_fault_reported && arc_engine_faulted(&fault))
      {
         arc_message(fault && fault[0] ? fault : "The emulated machine faulted.", RETRO_LOG_ERROR);
         arc_fault_reported = true;
      }
      memset(&frame, 0, sizeof(frame));
      frame.pixels = arc_frame;
      frame.width = ARC_WIDTH;
      frame.height = ARC_HEIGHT;
      frame.pitch = ARC_WIDTH * sizeof(uint32_t);
      frame.elapsed_us = 20000u;
      frame.refresh_hz = ARC_RATE;
   }
   if (frame_ran)
      arc_vidc_episode_update(frame.field_completed, frame.elapsed_us);
   if (frame_ran)
   {
      arc_integrate_stick(frame.elapsed_us);
      arc_guest_time_us += frame.elapsed_us;
   }
   if (chooser_exit_transition)
      arc_notify_av_change(frame.width, frame.height,
                           frame.refresh_hz > 0.0 ? frame.refresh_hz : ARC_RATE);
   if (frame.width != arc_video_width || frame.height != arc_video_height)
   {
      struct retro_game_geometry geometry = {
         frame.width, frame.height, 4096u, 2048u, 4.0f / 3.0f
      };
      if (arc_environment) arc_environment(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
      arc_video_width = frame.width;
      arc_video_height = frame.height;
      if (arc_osk.visible)
      {
         if (frame.width < 320u)
         {
            arc_osk_close(&arc_osk, &arc_inputs);
            arc_error_pad_gate = true;
            arc_osk_restore_gameplay();
            arc_osk_report_too_narrow();
            arc_osk_present_dirty = true;
         }
         else if (!arc_osk_prepare_surface(frame.width, frame.height) ||
             !arc_osk_layout(&arc_osk, frame.width, frame.height))
            arc_osk_presentation_failure();
         else
            arc_osk_present_dirty = true;
      }
   }
   if (frame.refresh_hz > 0.0 &&
       (frame.refresh_hz < arc_video_rate - 0.01 || frame.refresh_hz > arc_video_rate + 0.01))
   {
      struct retro_system_av_info av;
      retro_get_system_av_info(&av);
      av.geometry.base_width = frame.width;
      av.geometry.base_height = frame.height;
      av.timing.fps = frame.refresh_hz;
      if (arc_environment) arc_environment(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
      arc_video_rate = frame.refresh_hz;
   }
   if (frame.field_completed && frame.refresh_hz > 0.0)
      arc_audio_fraction += 48000.0L / (long double)frame.refresh_hz;
   else
      arc_audio_fraction += (long double)frame.elapsed_us * 48000.0L / 1000000.0L;
   count = (size_t)arc_audio_fraction;
   arc_audio_fraction -= (long double)count;
   arc_persist_cmos();
   unsigned old_phase = arc_osk.activation_phase;
   bool old_busy = arc_osk.busy;
   arc_osk_update(&arc_osk, &arc_inputs);
   if (old_phase != arc_osk.activation_phase || old_busy != arc_osk.busy)
      arc_osk_present_dirty = true;
   if (arc_osk_take_interrupted(&arc_osk))
   {
      arc_message("On-screen keyboard input was interrupted; released held keys.",
                  RETRO_LOG_WARN);
      arc_osk_present_dirty = true;
   }
   arc_present(&frame);
   while (count)
   {
      size_t capacity = sizeof(arc_audio_frame) / (2u * sizeof(arc_audio_frame[0]));
      size_t batch = count < capacity ? count : capacity;
      size_t produced = arc_engine_audio(arc_audio_frame, batch);
      if (produced < batch)
         memset(arc_audio_frame + produced * 2u, 0,
                (batch - produced) * 2u * sizeof(arc_audio_frame[0]));
      if (arc_audio_batch) arc_audio_batch(arc_audio_frame, batch);
      else if (arc_audio)
         for (size_t i = 0; i < batch; ++i)
            arc_audio(arc_audio_frame[i * 2u], arc_audio_frame[i * 2u + 1u]);
      count -= batch;
   }
   if (!arc_audio_warning_reported)
   {
      uint64_t underruns = 0u, overruns = 0u;
      arc_engine_audio_stats(&underruns, &overruns);
      if (underruns || overruns)
      {
         char warning[192];
         snprintf(warning, sizeof(warning),
                  "Audio queue pressure: underrun_frames=%llu overrun_frames=%llu; underruns are silenced and oldest overrun samples are dropped.",
                  (unsigned long long)underruns, (unsigned long long)overruns);
         arc_message(warning, RETRO_LOG_WARN);
         arc_audio_warning_reported = true;
      }
   }
}

RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_PAL; }
RETRO_API void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { (void)id; return 0u; }
RETRO_API size_t retro_serialize_size(void) { return 0u; }
RETRO_API bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
RETRO_API bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t count)
{ (void)type; (void)info; (void)count; return false; }
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
   if (port >= 2u) return;
   if (device == RETRO_DEVICE_NONE || device == RETRO_DEVICE_JOYPAD ||
       device == RETRO_DEVICE_MOUSE || device == ARC_DEVICE_KEYS ||
       device == ARC_DEVICE_MOUSE)
   {
      if (device != arc_port_device[port])
      {
         arc_input_clear_source(&arc_inputs,
            port ? ARC_INPUT_SOURCE_PAD1 : ARC_INPUT_SOURCE_PAD0);
         arc_input_clear_source(&arc_inputs,
            port ? ARC_INPUT_SOURCE_MOUSE1 : ARC_INPUT_SOURCE_MOUSE0);
         memset(arc_pad_keys[port], 0, sizeof(arc_pad_keys[port]));
         arc_pad_neutral_gate[port] = true;
         if (!port)
         {
            arc_pad_pointer_buttons = 0u;
            arc_touch_reset();
            arc_stick_x = arc_stick_y = 0;
            arc_stick_fraction_x = arc_stick_fraction_y = 0.0L;
         }
      }
      arc_port_device[port] = device;
   }
}
