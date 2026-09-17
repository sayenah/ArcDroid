/* ArcDroid libretro adapter. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "libretro.h"
#include "arc_io.h"
#include "arcdroid_core.h"
#include "arcdroid_engine.h"
#include "arcdroid_platform.h"
#include "jfd.h"
#include "playlist.h"

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
#define ARC_RATE 50.0
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
static int16_t arc_audio_frame[2048u * 2u];
static uint8_t arc_cmos_record[256];
static uint8_t arc_cmos_last_written[256];
static uint8_t *arc_firmware;
static size_t arc_firmware_size;
static ArcDigest arc_content_digest;
static ArcLaunchPlan arc_plan;
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
static bool arc_can_dupe;
static bool arc_cmos_last_written_valid;
static bool arc_ejected;
static unsigned arc_image_index;
static bool arc_pending_initial;
static unsigned arc_pending_initial_index;
static char arc_pending_initial_path[4096];
static uint64_t arc_guest_time_us;
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
#if defined(ARCDROID_TEST_FAULTS)
static unsigned arc_osk_fail_allocations;
static bool arc_osk_source_unchanged;
static bool arc_osk_source_checked;
static ArcInputProfile arc_test_input_profile;
static bool arc_test_input_profile_registered;
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
   bool created_this_preparation;
   char source[ARC_MEDIA_PATH_CAPACITY];
   char staged[ARC_MEDIA_PATH_CAPACITY];
   char label[ARC_MEDIA_LABEL_CAPACITY];
   ArcMediaInfo info;
   ArcDigest digest;
   ArcJfdImage jfd;
} ArcMediaEntry;

static ArcMediaEntry arc_media[ARC_PLAYLIST_MAX_ENTRIES];
static unsigned arc_media_count;

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
   ArcCpu cpu;
   bool request_a3010_joystick;
   bool pointer_relative;
   ArcPointerStick pointer_stick;
   unsigned pointer_speed;
   unsigned pointer_deadzone;
   ArcDiskWriteMode disk_writes;
   ArcLaunchOption launch;
   char input_profile[64];
   ArcInputOverride pad[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT];
   ArcOskToggle osk_toggle;
} ArcCoreOptions;

static ArcCoreOptions arc_core_options;

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
   const char *launch = arc_option_value("arcdroid_launch", "auto");
   const char *profile = arc_option_value("arcdroid_input_profile", "Auto");
   const char *osk_toggle = arc_option_value("arcdroid_osk_toggle", "L3");
   if (!options) return false;
   memset(options, 0, sizeof(*options));
   const ArcMachineDescriptor *descriptor = arc_machine_from_name(machine);
   if (!descriptor || (strcmp(cpu, "Auto") && strcmp(cpu, "ARM2") && strcmp(cpu, "ARM250")) ||
       (strcmp(memory, "Auto") && strcmp(memory, "4 MB")))
      goto invalid;
   options->machine = descriptor->id;
   options->cpu = !strcmp(cpu, "ARM2") ? ARC_CPU_ARM2 :
                  !strcmp(cpu, "ARM250") ? ARC_CPU_ARM250 : descriptor->cpu;
   if (options->cpu != descriptor->cpu)
   {
      if (error && error_size) snprintf(error, error_size, "%s is incompatible with the %s machine",
                                        cpu, descriptor->name);
      return false;
   }
   if (!strcmp(os, "RISC OS 3.11")) options->requested_os = ARC_OS_RISCOS_311;
   else if (!strcmp(os, "RISC OS 3.10")) options->requested_os = ARC_OS_RISCOS_310;
   else if (!strcmp(os, "RISC OS 3.19")) options->requested_os = ARC_OS_RISCOS_319;
   else if (!strcmp(os, "RISC OS 3.00")) options->requested_os = ARC_OS_RISCOS_300;
   else if (!strcmp(os, "RISC OS 2.00")) options->requested_os = ARC_OS_RISCOS_200;
   else if (!strcmp(os, "Arthur 1.20")) options->requested_os = ARC_OS_ARTHUR_120;
   else if (strcmp(os, "Auto")) goto invalid;
   if (options->requested_os != ARC_OS_UNKNOWN &&
       !arc_machine_supports_os(options->machine, options->requested_os))
   {
      if (error && error_size) snprintf(error, error_size, "%s is not supported on the %s",
         arc_os_name(options->requested_os), descriptor->name);
      return false;
   }
   if (!strcmp(joystick, "A3010")) options->request_a3010_joystick = true;
   else if (strcmp(joystick, "Auto") && strcmp(joystick, "Keyboard")) goto invalid;
   if (!strcmp(pointer, "relative")) options->pointer_relative = true;
   else if (strcmp(pointer, "disabled")) goto invalid;
   if (!strcmp(pointer_stick, "right")) options->pointer_stick = ARC_POINTER_STICK_RIGHT;
   else if (!strcmp(pointer_stick, "left")) options->pointer_stick = ARC_POINTER_STICK_LEFT;
   else if (!strcmp(pointer_stick, "disabled")) options->pointer_stick = ARC_POINTER_STICK_DISABLED;
   else goto invalid;
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
   if (!strcmp(launch, "auto")) options->launch = ARC_LAUNCH_AUTO;
   else if (!strcmp(launch, "off")) options->launch = ARC_LAUNCH_OFF;
   else if (!strncmp(launch, "app", 3u) && launch[3] >= '1' && launch[3] <= '8' && !launch[4])
      options->launch = (ArcLaunchOption)(ARC_LAUNCH_APP1 + launch[3] - '1');
   else goto invalid;
   if (strlen(profile) >= sizeof(options->input_profile)) goto invalid;
   strcpy(options->input_profile, profile);
   if (strcmp(profile, "Auto") && strcmp(profile, "Generic"))
   {
      bool found = false;
      size_t count = 0u;
      const ArcInputProfile *profiles = arc_input_profiles(&count);
      for (size_t i = 0u; i < count; ++i)
         if (!strcmp(profile, profiles[i].id)) found = true;
#if defined(ARCDROID_TEST_FAULTS)
      if (arc_test_input_profile_registered &&
          !strcmp(profile, arc_test_input_profile.id)) found = true;
#endif
      if (!found) goto invalid;
   }
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
               snprintf(error, error_size, "%s requires RISC OS 3.10 or later firmware; found none",
                        d ? d->name : "Machine");
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
   arc_engine_stop();
   free(arc_firmware);
   arc_firmware = NULL;
   arc_firmware_size = 0u;
   for (unsigned i = 0u; i < ARC_PLAYLIST_MAX_ENTRIES; ++i)
      arc_jfd_free(&arc_media[i].jfd);
   memset(arc_media, 0, sizeof(arc_media));
   arc_media_count = 0u;
   arc_image_index = 0u;
   arc_cmos_path[0] = '\0';
   arc_helper_path[0] = '\0';
   arc_cmos_last_written_valid = false;
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
   arc_cmos_normalise_payload(payload, machine->io);
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
   arc_cmos_normalise_payload(payload, machine->io);
   arc_cmos_encode(arc_cmos_record, &arc_plan, payload);
   free(existing);
}

static void arc_persist_cmos(void)
{
   const ArcMachineDescriptor *machine = arc_machine_descriptor(arc_plan.machine);
   uint8_t payload[ARC_CMOS_PAYLOAD_SIZE];
   if (!arc_cmos_path[0]) return;
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

static bool arc_prepare_source(const char *source, const char *playlist_label,
                               ArcDiskWriteMode mode, ArcMediaEntry *entry,
                               char *error, size_t error_size)
{
   void *source_data = NULL;
   size_t source_size = 0u;
   uint8_t *staged_data = NULL;
   memset(entry, 0, sizeof(*entry));
   if (!source || !source[0] || strlen(source) >= sizeof(entry->source) ||
       !arc_io_read_all(source, &source_data, &source_size))
   {
      if (error && error_size) snprintf(error, error_size,
         "unable to read media through the frontend VFS");
      return false;
   }
   if (!arc_media_inspect(source, source_data, source_size, &entry->info,
                          error, error_size))
   { free(source_data); return false; }
   strcpy(entry->source, source);
   if (playlist_label && playlist_label[0])
      strcpy(entry->label, playlist_label);

   const uint8_t *publish_data = source_data;
   size_t publish_size = source_size;
   bool immutable = mode == ARC_DISK_WRITES_OFF;
   const char *extension = "adf";
   if (entry->info.kind == ARC_MEDIA_JFD)
   {
      bool decoded_source = source_size >= 4u && !memcmp(source_data, "JFDI", 4u);
      bool parsed = decoded_source ?
         arc_jfd_parse_owned(source_data, source_size, &entry->jfd, error, error_size) :
         arc_jfd_parse(source_data, source_size, &entry->jfd, error, error_size);
      if (decoded_source) source_data = NULL;
      if (!parsed) { free(source_data); return false; }
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
      entry->digest = arc_digest(entry->jfd.decoded, entry->jfd.decoded_size);
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
      entry->digest = arc_digest(source_data, source_size);
      extension = entry->info.geometry == ARC_GEOMETRY_640K_L ? "adl" : "adf";
      if (entry->info.short_image)
      {
         if (mode == ARC_DISK_WRITES_ORIGINAL)
         {
            if (error && error_size) snprintf(error, error_size,
               "Short image cannot use original writes; choose shadow or off.");
            free(source_data);
            return false;
         }
         staged_data = calloc(1u, entry->info.declared_size);
         if (!staged_data)
         { free(source_data); if (error && error_size) snprintf(error, error_size, "out of memory"); return false; }
         memcpy(staged_data, source_data, source_size);
         publish_data = staged_data;
         publish_size = entry->info.declared_size;
      }
      if (entry->info.recognition == ARC_MEDIA_RECOGNITION_SIZE_FALLBACK &&
          !arc_media_is_recognised_adfs(source_data, source_size))
         arc_message("Media is not a recognised ADFS image; booting anyway.", RETRO_LOG_WARN);
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
      char warning[192];
      snprintf(warning, sizeof(warning), "Short image padded with %zu missing bytes; source remains unchanged.",
               entry->info.padding_required);
      arc_message(warning, RETRO_LOG_WARN);
   }
   /* JFD staging is immutable. Retain its metadata, digest and label here,
    * then re-read and parse only the selected image immediately before an
    * insert. This keeps exactly one decoded JFD buffer live per session. */
   if (entry->info.kind == ARC_MEDIA_JFD) arc_jfd_free(&entry->jfd);
   free(staged_data);
   free(source_data);
   return true;
reject:
   arc_jfd_free(&entry->jfd);
   free(staged_data);
   free(source_data);
   memset(entry, 0, sizeof(*entry));
   return false;
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

static bool arc_prepare_media_list(const char *content_path,
                                   ArcDiskWriteMode mode,
                                   char *error, size_t error_size)
{
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
   if (is_playlist)
   {
      unsigned error_line = 0u;
#if defined(_WIN32)
      const bool windows_host = true;
#else
      const bool windows_host = false;
#endif
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
      if (!arc_prepare_source(source, label, mode, &arc_media[i], error, error_size))
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
      arc_content_digest = arc_media[0].digest;
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

static ArcEngineMedia arc_engine_media(const ArcMediaEntry *entry)
{
   ArcEngineMedia media = {entry->staged,
      entry->info.kind != ARC_MEDIA_JFD && arc_plan.disk_writes != ARC_DISK_WRITES_OFF,
      entry->info.kind, entry->info.geometry,
      entry->info.kind == ARC_MEDIA_JFD ? &entry->jfd : NULL};
   return media;
}

static bool arc_refresh_jfd(ArcMediaEntry *entry, char *error, size_t error_size)
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
   return true;
}

static const char *arc_launch_option_name(ArcLaunchOption option)
{
   static const char *const names[] = {
      "auto", "off", "app1", "app2", "app3", "app4", "app5", "app6", "app7", "app8"
   };
   return (unsigned)option < sizeof(names) / sizeof(names[0]) ? names[option] : "invalid";
}

static bool arc_launch_os_supported(ArcOsClass os)
{
#if defined(ARCDROID_TEST_FAULTS)
   if (os == ARC_OS_TEST) return true;
#endif
   return os == ARC_OS_RISCOS_300 || os == ARC_OS_RISCOS_310 ||
          os == ARC_OS_RISCOS_311 || os == ARC_OS_RISCOS_319;
}

static void arc_launch_sanitise(const char *source, char *output, size_t capacity)
{
   size_t at = 0u;
   if (!capacity) return;
   while (source && *source && at + 1u < capacity)
   {
      unsigned char c = (unsigned char)*source++;
      output[at++] = c >= 0x20u && c <= 0x7eu ? (char)c : '?';
   }
   output[at] = '\0';
}

static void arc_launch_candidates_message(const ArcLaunchDiscovery *discovery,
                                          const char *prefix,
                                          bool applications_only)
{
   char message[512];
   size_t at = (size_t)snprintf(message, sizeof(message), "%s", prefix);
   bool listed = false;
   for (unsigned i = 0u; i < discovery->candidate_count && at < sizeof(message); ++i)
   {
      if (applications_only && !discovery->candidates[i].application) continue;
      char name[11];
      arc_launch_sanitise(discovery->candidates[i].name, name, sizeof(name));
      int written = snprintf(message + at, sizeof(message) - at, "%s%u %s",
                             listed ? " " : "", i + 1u, name);
      if (written < 0 || (size_t)written >= sizeof(message) - at)
      { at = sizeof(message) - 1u; break; }
      at += (size_t)written;
      listed = true;
   }
   if (at < sizeof(message) - 28u)
      snprintf(message + at, sizeof(message) - at, " — set Launch to Application N");
   arc_message(message, RETRO_LOG_WARN);
}

static bool arc_launch_binding(const ArcLaunchDiscovery *discovery,
                               ArcLaunchOption option, unsigned selected,
                               bool reselected, char *reason, size_t reason_size)
{
   char directory[ARC_MEDIA_PATH_CAPACITY];
   char path[ARC_MEDIA_PATH_CAPACITY];
   int d = snprintf(directory, sizeof(directory), "%s/arcdroid/launch", arc_save_path);
   int p = d >= 0 && (size_t)d < sizeof(directory) ?
      snprintf(path, sizeof(path), "%s/%s.bind", directory, arc_plan.content_id) : -1;
   if (d < 0 || (size_t)d >= sizeof(directory) || p < 0 ||
       (size_t)p >= sizeof(path) || !arc_io_make_path(directory))
   { snprintf(reason, reason_size, "binding storage unavailable"); return false; }

   void *existing = NULL;
   size_t existing_size = 0u;
   bool exists = arc_io_read_all(path, &existing, &existing_size);
   const char *option_name = arc_launch_option_name(option);
   const char *target = discovery->candidates[selected].name;
   if (exists && !reselected)
   {
      char bound_option[5] = {0}, bound_target[11] = {0}, bound_fingerprint[41] = {0};
      char text[128];
      char trailing = 0;
      if (existing_size < sizeof(text))
      {
         if (existing_size && existing) memcpy(text, existing, existing_size);
         text[existing_size] = '\0';
      }
      int fields = existing_size < sizeof(text) ?
         sscanf(text, "%4[^\n]\n%10[^\n]\n%40[^\n]\n%c",
                bound_option, bound_target, bound_fingerprint, &trailing) : 0;
      if (fields < 3)
      { free(existing); snprintf(reason, reason_size, "binding is invalid; re-select Launch"); return false; }
      if (!strcmp(bound_option, option_name))
      {
         bool matches = !strcmp(bound_target, target);
         free(existing);
         if (!matches)
         { snprintf(reason, reason_size, "binding changed target; re-select Launch"); return false; }
         return true;
      }
   }
   free(existing);
   char binding[96];
   int size = snprintf(binding, sizeof(binding), "%s\n%s\n%s\n", option_name,
                       target, discovery->candidate_fingerprint);
   if (size < 0 || (size_t)size >= sizeof(binding) ||
       !arc_atomic_publish(path, (const uint8_t *)binding, (size_t)size,
                           NULL, NULL, 0u))
   { snprintf(reason, reason_size, "binding could not be published"); return false; }
   return true;
}

static void arc_launch_log(const ArcLaunchDiscovery *discovery,
                           const char *decision, const char *reason)
{
   if (!arc_log) return;
   char candidates[1024] = "";
   char applications[512] = "";
   char disc_name[11];
   size_t ca = 0u, aa = 0u;
   arc_launch_sanitise(discovery->disc_name, disc_name, sizeof(disc_name));
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
   {
      char name[11];
      arc_launch_sanitise(discovery->candidates[i].name, name, sizeof(name));
      int n = snprintf(candidates + ca, sizeof(candidates) - ca, "%s%u:%s",
                       i ? "," : "", i + 1u, name);
      if (n > 0 && (size_t)n < sizeof(candidates) - ca) ca += (size_t)n;
      if (discovery->candidates[i].application)
      {
         n = snprintf(applications + aa, sizeof(applications) - aa, "%s%u:%s",
                      aa ? "," : "", i + 1u, name);
         if (n > 0 && (size_t)n < sizeof(applications) - aa) aa += (size_t)n;
      }
   }
   arc_log(RETRO_LOG_INFO,
      "Launch: boot=%u disc=\"%s\" candidates=[%s] applications=[%s] decision=%s reason=%s helper=%s\n",
      discovery->boot_option, disc_name, candidates, applications,
      decision, reason, arc_helper_path[0] ? arc_helper_path : "none");
}

static void arc_prepare_launch(ArcMediaEntry *entry, bool ejected,
                               ArcLaunchOption option, bool reselected)
{
   ArcLaunchDiscovery discovery;
   memset(&discovery, 0, sizeof(discovery));
   discovery.target_index = -1;
   discovery.reason = ARC_LAUNCH_REASON_UNREADABLE;
   arc_helper_path[0] = '\0';
   arc_plan.launch_helper = false;
   arc_plan.launch_target[0] = '\0';
   if (ejected || !entry || !entry->populated)
   { arc_launch_log(&discovery, "none", "drive 0 empty"); return; }

   void *raw = NULL;
   size_t raw_size = 0u;
   const ArcJfdImage *jfd = entry->info.kind == ARC_MEDIA_JFD ? &entry->jfd : NULL;
   if (!jfd && !arc_io_read_all(entry->staged, &raw, &raw_size))
   { arc_launch_log(&discovery, "none", "media read failed"); return; }
   (void)arc_launch_discover(raw, raw_size, &entry->info, jfd, &discovery);
   free(raw);
   if (option == ARC_LAUNCH_OFF)
   { arc_launch_log(&discovery, "none", "disabled"); return; }
   if (!arc_launch_os_supported(arc_plan.os))
   { arc_launch_log(&discovery, "none", "OS class unsupported"); return; }

   int selected = discovery.target_index;
   char refusal[160] = {0};
   const char *reason = arc_launch_reason_name(discovery.reason);
   if (option >= ARC_LAUNCH_APP1)
   {
      unsigned ordinal = (unsigned)(option - ARC_LAUNCH_APP1);
      if (!discovery.readable)
         snprintf(refusal, sizeof(refusal), "Application choice refused: inspection incomplete");
      else if (ordinal >= discovery.candidate_count)
         snprintf(refusal, sizeof(refusal), "Application choice refused: candidate %u does not exist", ordinal + 1u);
      else if (!discovery.candidates[ordinal].application)
         snprintf(refusal, sizeof(refusal), "Application choice refused: candidate %u has no !Run", ordinal + 1u);
      else
      {
         selected = (int)ordinal;
         if (!arc_launch_binding(&discovery, option, ordinal, reselected,
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
         arc_launch_log(&discovery, "none", refusal);
         return;
      }
   }
   else if (selected < 0)
   {
      if (discovery.reason == ARC_LAUNCH_REASON_AMBIGUOUS &&
          discovery.application_count >= 2u)
         arc_launch_candidates_message(&discovery, "No unique application: ", true);
      else if (discovery.reason == ARC_LAUNCH_REASON_INSPECTION_INCOMPLETE &&
               discovery.candidate_count)
         arc_launch_candidates_message(&discovery,
            "Application inspection incomplete: ", false);
      arc_launch_log(&discovery, "none", reason);
      return;
   }

   uint8_t *helper = malloc(ARC_LAUNCH_DISC_SIZE);
   if (!helper || !arc_launch_disc_build(discovery.disc_name,
                                         discovery.candidates[selected].name,
                                         helper))
   {
      free(helper);
      arc_launch_log(&discovery, "none", "helper generation failed");
      return;
   }
   char directory[ARC_MEDIA_PATH_CAPACITY];
   int d = snprintf(directory, sizeof(directory), "%s/arcdroid/launch", arc_save_path);
   int p = d >= 0 && (size_t)d < sizeof(directory) ?
      snprintf(arc_helper_path, sizeof(arc_helper_path), "%s/%s.adl",
               directory, arc_plan.content_id) : -1;
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
   if (!arc_helper_path[0])
   {
      arc_launch_log(&discovery, "none", "helper publication failed");
      return;
   }
   arc_plan.launch_helper = true;
   memcpy(arc_plan.launch_target, discovery.candidates[selected].name,
          sizeof(arc_plan.launch_target));
   arc_launch_log(&discovery, arc_plan.launch_target, reason);
}

static bool arc_disk_set_eject(bool ejected)
{
   char error[512] = {0};
   if (!arc_loaded) return false;
   if (ejected == arc_ejected) return true;
   if (ejected)
   {
      if (!arc_engine_media_eject()) return false;
      if (arc_image_index < arc_media_count)
         arc_jfd_free(&arc_media[arc_image_index].jfd);
      arc_ejected = true;
      return true;
   }
   if (arc_image_index < arc_media_count && arc_media[arc_image_index].populated)
   {
      ArcMediaEntry *entry = &arc_media[arc_image_index];
      if (!arc_refresh_jfd(entry, error, sizeof(error)))
      {
         arc_message(error, RETRO_LOG_ERROR);
         return false;
      }
      ArcEngineMedia media = arc_engine_media(entry);
      if (!arc_engine_media_insert(&media, error, sizeof(error)))
      {
         arc_jfd_free(&entry->jfd);
         arc_message(error[0] ? error : "Unable to insert the selected floppy image.", RETRO_LOG_ERROR);
         return false;
      }
   }
   arc_ejected = false;
   return true;
}
static bool arc_disk_get_eject(void) { return arc_ejected; }
static unsigned arc_disk_get_index(void) { return arc_image_index; }
static bool arc_disk_set_index(unsigned index)
{
   if (!arc_loaded || !arc_ejected) return false;
   if (index < arc_media_count && !arc_media[index].populated) return false;
   arc_image_index = index < arc_media_count ? index : arc_media_count;
   return true;
}
static unsigned arc_disk_get_count(void) { return arc_media_count; }
static bool arc_disk_replace(unsigned index, const struct retro_game_info *info)
{
   if (!arc_loaded || !arc_ejected || index >= arc_media_count) return false;
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
   if (!info->path || !arc_prepare_source(info->path, NULL, arc_plan.disk_writes,
                                           &replacement, error, sizeof(error)))
   {
      if (!arc_media[index].populated && index + 1u == arc_media_count)
         --arc_media_count;
      arc_message(error[0] ? error : "Replacement floppy image is invalid.", RETRO_LOG_ERROR);
      return false;
   }
   arc_jfd_free(&arc_media[index].jfd);
   arc_media[index] = replacement;
   return true;
}
static bool arc_disk_add(void)
{
   if (!arc_loaded || arc_media_count >= ARC_PLAYLIST_MAX_ENTRIES) return false;
   memset(&arc_media[arc_media_count++], 0, sizeof(arc_media[0]));
   return true;
}
static bool arc_disk_set_initial(unsigned index, const char *path)
{
   if (!path || strlen(path) >= sizeof(arc_pending_initial_path)) return false;
   arc_pending_initial_index = index;
   strcpy(arc_pending_initial_path, path);
   arc_pending_initial = true;
   return true;
}
static bool arc_disk_get_path(unsigned index, char *path, size_t length)
{
   if (!path || index >= arc_media_count || !arc_media[index].populated) return false;
   size_t needed = strlen(arc_media[index].source) + 1u;
   if (needed > length) return false;
   memcpy(path, arc_media[index].source, needed);
   return true;
}
static bool arc_disk_get_label(unsigned index, char *label, size_t length)
{
   if (!label || index >= arc_media_count || !arc_media[index].populated) return false;
   size_t needed = strlen(arc_media[index].label) + 1u;
   if (needed > length) return false;
   memcpy(label, arc_media[index].label, needed);
   return true;
}

static void arc_keyboard(bool down, unsigned keycode, uint32_t character,
                         uint16_t modifiers)
{
   (void)character;
   (void)modifiers;
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
   else arc_input_reset(&arc_inputs);
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

static void arc_poll_osk_controls(void)
{
   if (!arc_input_state) return;
   if (arc_osk_advance(&arc_osk, arc_guest_time_us))
      arc_osk_present_dirty = true;
   unsigned toggle_id = arc_core_options.osk_toggle == ARC_OSK_TOGGLE_R3 ?
      RETRO_DEVICE_ID_JOYPAD_R3 : RETRO_DEVICE_ID_JOYPAD_L3;
   bool toggle = arc_core_options.osk_toggle != ARC_OSK_TOGGLE_OFF &&
      arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u, toggle_id) != 0;
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
      level[i] = arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u, ids[i]) != 0;
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
   if (!arc_core_options.pointer_relative)
   {
      arc_touch_reset();
      return;
   }
   int reported = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                                  RETRO_DEVICE_ID_POINTER_COUNT);
   unsigned count = reported > 0 ? (unsigned)reported : 0u;
   bool primary_pressed = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                                          RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
   if (primary_pressed && count < 1u) count = 1u;
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
   arc_poll_osk_controls();
   bool sampled[2][10] = {{false}};
   bool desired[ARC_PAD_PORTS][ARC_INPUT_KEY_COUNT] = {{false}};
   for (unsigned port = 0u; port < 2u; ++port)
      if ((port || !arc_osk.visible) &&
          (arc_port_device[port] == ARC_DEVICE_KEYS ||
          arc_port_device[port] == RETRO_DEVICE_JOYPAD)
         )
         for (unsigned i = 0u; i < 10u; ++i)
            sampled[port][i] = arc_input_state(port, RETRO_DEVICE_JOYPAD, 0u, ids[i]) != 0;
   for (unsigned port = 0u; port < 2u; ++port)
   {
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
         const ArcInputBinding *binding = &arc_input_resolution.binding[port][input];
         bool joystick_owned = arc_loaded && arc_plan.a3010_joystick && input <= ARC_PAD_A &&
                               arc_core_options.pad[port][input].automatic;
         if (!joystick_owned && sampled[port][input] && binding->bound &&
             binding->key < ARC_INPUT_KEY_COUNT)
            desired[port][binding->key] = true;
      }
      memcpy(arc_pad_keys[port], sampled[port], sizeof(sampled[port]));
   }
   arc_input_pad_key_sets(&arc_inputs, desired[0], desired[1]);
   bool joystick_levels[ARC_PAD_PORTS][5] = {{false}};
   for (unsigned port = 0; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0; input < 5u; ++input)
         joystick_levels[port][input] = sampled[port][input] &&
            arc_core_options.pad[port][input].automatic;
   arc_platform_joystick_snapshot(arc_loaded && arc_plan.a3010_joystick,
                                  joystick_levels);

   bool pad_pointer = !arc_osk.visible &&
      (arc_port_device[0] == ARC_DEVICE_KEYS ||
       arc_port_device[0] == RETRO_DEVICE_JOYPAD);
   unsigned pad_buttons = pad_pointer ?
      (arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u, RETRO_DEVICE_ID_JOYPAD_R) ? 1u : 0u) |
      (arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u, RETRO_DEVICE_ID_JOYPAD_L) ? 2u : 0u) |
      (arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u, RETRO_DEVICE_ID_JOYPAD_L2) ? 4u : 0u) : 0u;
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
         arc_input_source_buttons(&arc_inputs,
            port ? ARC_INPUT_SOURCE_MOUSE1 : ARC_INPUT_SOURCE_MOUSE0, buttons);
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
   }
   else
   {
      arc_stick_x = arc_stick_y = 0;
      arc_stick_precision = false;
   }
   arc_poll_keyboard_snapshot();
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
   {"arcdroid_disk_writes", "Disk Writes", NULL, "Choose where floppy writes are stored.", NULL,
    NULL, {{"shadow", NULL}, {"original", NULL}, {"off", NULL}, {NULL, NULL}}, "shadow"},
   {NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL}
};
static struct retro_core_option_v2_definition arc_options[34];
static struct retro_core_option_v2_category arc_option_categories[] = {
   {"pad_mapping", "Pad Mapping", "Map each RetroPad input to a physical Archimedes key."},
   {NULL, NULL, NULL}
};
static struct retro_core_options_v2 arc_options_v2 = {arc_option_categories, arc_options};
static char arc_pad_option_labels[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT][32];
static bool arc_options_built;

#if defined(ARCDROID_TEST_FAULTS)
bool arc_libretro_test_register_input_profile(const ArcInputProfile *profile)
{
   char error[128];
   if (arc_options_built || !profile ||
       !arc_input_profiles_validate_table(profile, 1u,
                                          error, sizeof(error)))
      return false;
   size_t shipped_count = 0u;
   const ArcInputProfile *shipped = arc_input_profiles(&shipped_count);
   for (size_t i = 0u; i < shipped_count; ++i)
   {
      if (!strcmp(profile->id, shipped[i].id)) return false;
      for (unsigned digest = 0u; digest < profile->digest_count; ++digest)
         for (unsigned other = 0u; other < shipped[i].digest_count; ++other)
            if (!strcmp(profile->digests[digest], shipped[i].digests[other]))
               return false;
   }
   arc_test_input_profile = *profile;
   arc_test_input_profile_registered = true;
   return true;
}
#endif

static void arc_build_options(void)
{
   if (arc_options_built) return;
   memset(arc_options, 0, sizeof(arc_options));
   memcpy(arc_options, arc_base_options,
          11u * sizeof(struct retro_core_option_v2_definition));
   unsigned at = 11u;
   struct retro_core_option_v2_definition *profile = &arc_options[at++];
   profile->key = "arcdroid_input_profile";
   profile->desc = "Input Profile";
   profile->info = "Use an exact-content profile, the generic mapping, or a selected preset.";
   profile->category_key = "pad_mapping";
   profile->values[0] = (struct retro_core_option_value){"Auto", NULL};
   profile->values[1] = (struct retro_core_option_value){"Generic", NULL};
   size_t profile_count = 0u;
   const ArcInputProfile *profiles = arc_input_profiles(&profile_count);
   for (size_t i = 0u; i < profile_count && i + 3u < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++i)
      profile->values[i + 2u] = (struct retro_core_option_value){profiles[i].id, profiles[i].name};
#if defined(ARCDROID_TEST_FAULTS)
   if (arc_test_input_profile_registered &&
       profile_count + 3u < RETRO_NUM_CORE_OPTION_VALUES_MAX)
      profile->values[profile_count + 2u] = (struct retro_core_option_value){
         arc_test_input_profile.id, arc_test_input_profile.name
      };
#endif
   profile->default_value = "Auto";

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
         definition->info = "Auto uses the resolved profile; Unbound disables this input.";
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
   {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Pointer horizontal velocity (Pointer Stick = left)"},
   {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Pointer vertical velocity (Pointer Stick = left)"},
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
   {0, 0, 0, 0, NULL}
};
static char arc_descriptor_labels[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT][64];

static const char *arc_mapping_source_name(ArcMappingSource source)
{
   switch (source)
   {
      case ARC_MAPPING_SOURCE_PROFILE: return "profile";
      case ARC_MAPPING_SOURCE_PRESET: return "preset";
      case ARC_MAPPING_SOURCE_OPTION: return "option";
      default: return "generic";
   }
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
                               bool mapping_change, bool notify)
{
   ArcInputResolution resolved;
#if defined(ARCDROID_TEST_FAULTS)
   bool use_test_profile = arc_test_input_profile_registered &&
      !strcmp(options->input_profile, arc_test_input_profile.id);
   if (arc_test_input_profile_registered &&
       !strcmp(options->input_profile, "Auto"))
   {
      bool target_matches = !arc_test_input_profile.launch_target ||
         !arc_test_input_profile.launch_target[0] ||
         !strcmp(arc_test_input_profile.launch_target, arc_plan.launch_target);
      for (unsigned digest = 0u;
           target_matches && digest < arc_test_input_profile.digest_count;
           ++digest)
         if (!strcmp(arc_test_input_profile.digests[digest],
                     arc_content_digest.sha1))
            use_test_profile = true;
   }
   bool resolved_ok = use_test_profile ?
      arc_input_resolve_table(&arc_test_input_profile, 1u,
                              arc_content_digest.sha1, arc_plan.launch_target,
                              options->input_profile, options->pad, &resolved) :
      arc_input_resolve(arc_content_digest.sha1, arc_plan.launch_target,
                        options->input_profile, options->pad, &resolved);
#else
   bool resolved_ok = arc_input_resolve(arc_content_digest.sha1,
                                        arc_plan.launch_target,
                                        options->input_profile, options->pad,
                                        &resolved);
#endif
   if (!resolved_ok)
      return false;
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
      size_t at = (size_t)snprintf(line, sizeof(line), "Input resolution: profile=%s%s",
                                  resolved.profile_name,
                                  resolved.custom ? " custom" : "");
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
   if (notify)
   {
      bool applied = false;
      for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
         for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
            if (resolved.source[port][input] == ARC_MAPPING_SOURCE_PROFILE ||
                resolved.source[port][input] == ARC_MAPPING_SOURCE_PRESET)
               applied = true;
      if (applied)
      {
         char message[128];
         snprintf(message, sizeof(message), "Controls: %s", resolved.profile_name);
         arc_message(message, RETRO_LOG_INFO);
      }
   }
   return true;
}

RETRO_API void retro_set_environment(retro_environment_t callback)
{
   bool no_game = false;
   arc_build_options();
   arc_environment = callback;
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
   info->valid_extensions = "adf|adl|jfd|m3u";
   info->need_fullpath = true;
   info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
   if (!info) return;
   memset(info, 0, sizeof(*info));
   info->geometry.base_width = arc_video_width;
   info->geometry.base_height = arc_video_height;
   info->geometry.max_width = 4096u;
   info->geometry.max_height = 2048u;
   info->geometry.aspect_ratio = 4.0f / 3.0f;
   info->timing.fps = arc_video_rate;
   info->timing.sample_rate = ARC_AUDIO_RATE;
}

RETRO_API void retro_init(void)
{
   static const struct retro_disk_control_ext_callback disk = {
      arc_disk_set_eject, arc_disk_get_eject, arc_disk_get_index,
      arc_disk_set_index, arc_disk_get_count, arc_disk_replace,
      arc_disk_add, arc_disk_set_initial, arc_disk_get_path, arc_disk_get_label
   };
   struct retro_vfs_interface_info vfs = {3u, NULL};
   struct retro_log_callback log = {NULL};
   arc_loaded = false;
   arc_ejected = false;
   arc_image_index = 0u;
   arc_pending_initial = false;
   arc_pending_initial_index = 0u;
   arc_pending_initial_path[0] = '\0';
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
}

RETRO_API void retro_deinit(void)
{
   arc_persist_cmos();
   arc_release_session();
   arc_osk_reset(&arc_osk, &arc_inputs);
   arc_osk_free_surface();
   arc_engine_set_log_callback(NULL, NULL);
   arc_log = NULL;
   arc_loaded = false;
   arc_pending_initial = false;
   arc_pending_initial_path[0] = '\0';
   arc_vfs = NULL;
   arc_io_set_vfs(NULL);
   arc_environment = NULL;
   arc_video = NULL;
   arc_audio = NULL;
   arc_audio_batch = NULL;
   arc_input_poll = NULL;
   arc_input_state = NULL;
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
   bool pending_initial = arc_pending_initial;
   unsigned pending_index = arc_pending_initial_index;
   char pending_path[sizeof(arc_pending_initial_path)];
   snprintf(pending_path, sizeof(pending_path), "%s", arc_pending_initial_path);
   arc_pending_initial = false;
   arc_pending_initial_path[0] = '\0';
   if (arc_loaded) arc_persist_cmos();
   arc_loaded = false;
   arc_release_session();
   arc_input_sampling_reset(false);
   arc_osk_reset(&arc_osk, &arc_inputs);
   arc_osk_free_surface();
   arc_osk_present_dirty = false;
   arc_osk_too_narrow_reported = false;
   arc_osk_failure_reported = false;
   arc_guest_time_us = 0u;
   memset(arc_frame, 0, sizeof(arc_frame));
   if (!game || !game->path || !game->path[0])
   {
      arc_message("ArcDroid requires an ADF, ADL, JFD or m3u playlist.", RETRO_LOG_ERROR);
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
   if (!arc_environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) ||
       !save_dir || !save_dir[0] || strlen(save_dir) >= sizeof(arc_save_path) ||
       !strncmp(save_dir, "saf://", 6u))
   {
      arc_message("A native writable save directory is required; SAF save directories cannot publish staged media.", RETRO_LOG_ERROR);
      return false;
   }
   strcpy(arc_save_path, save_dir);
   if (!arc_prepare_media_list(game->path, arc_core_options.disk_writes,
                               error, sizeof(error)))
   {
      arc_message(error, RETRO_LOG_ERROR);
      return false;
   }
   arc_image_index = 0u;
   if (pending_initial && pending_index < arc_media_count &&
       arc_media[pending_index].populated &&
       !strcmp(arc_media[pending_index].source, pending_path))
      arc_image_index = pending_index;

   if (!arc_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) ||
       !system_dir || !system_dir[0] ||
       snprintf(firmware_dir, sizeof(firmware_dir), "%s/arcdroid/firmware", system_dir) < 0)
   {
      arc_message("Firmware directory unavailable; expected <system>/arcdroid/firmware.", RETRO_LOG_ERROR);
      arc_release_session();
      return false;
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
            snprintf(error, sizeof(error), "%s requires RISC OS 3.10 or later firmware; found none",
                     requested_machine->name);
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
   {
      uint8_t payload[ARC_CMOS_PAYLOAD_SIZE];
      uint8_t machine_cmos[ARC_CMOS_MACHINE_SIZE];
      if (!arc_refresh_jfd(&arc_media[arc_image_index], error, sizeof(error)))
      {
         arc_message(error, RETRO_LOG_ERROR);
         arc_release_session();
         return false;
      }
      arc_prepare_launch(&arc_media[arc_image_index], false,
                         arc_core_options.launch, false);
      if (!arc_resolve_inputs(&arc_core_options, false, true))
      {
         arc_message("Unable to resolve ArcDroid input options.", RETRO_LOG_ERROR);
         arc_release_session();
         return false;
      }
      ArcEngineMedia media = arc_engine_media(&arc_media[arc_image_index]);
      ArcEngineMedia helper = {arc_helper_path, false, ARC_MEDIA_ADL_640K,
                               ARC_GEOMETRY_640K_L, NULL};
      if (!arc_cmos_decode(arc_cmos_record, &arc_plan, payload) ||
          !arc_cmos_machine_image(arc_plan.os, launch_machine->io, payload,
                                  arc_plan.launch_helper, machine_cmos) ||
          !arc_engine_start(arc_firmware, arc_firmware_size,
                            &media,
                            arc_plan.launch_helper ? &helper : NULL,
                            launch_machine,
                            machine_cmos, error, sizeof(error)))
      {
         arc_message(error[0] ? error : "Unable to start the Arculator machine.", RETRO_LOG_ERROR);
         arc_release_session();
         return false;
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
   if (negotiate)
      (void)arc_engine_get_av_info(&arc_video_width, &arc_video_height, &arc_video_rate);
   arc_fault_reported = false;
   arc_vidc_warning_reported = false;
   arc_vidc_transient_logged = false;
   arc_vidc_incomplete_us = 0u;
   arc_audio_warning_reported = false;
   if (!arc_osk_prepare_surface(arc_video_width, arc_video_height))
      arc_osk_presentation_failure();
   snprintf(error, sizeof(error), "Firmware: %s; CRC32 %08x; SHA-1 %s",
            arc_os_name(search.match->os), search.match->crc32, search.match->sha1);
   arc_message(error, RETRO_LOG_INFO);
   return true;
}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
   return arc_load_game(game, true);
}

RETRO_API void retro_unload_game(void)
{
   arc_persist_cmos();
   if (arc_engine_active() && arc_log)
   {
      uint64_t underruns = 0u, overruns = 0u;
      arc_engine_audio_stats(&underruns, &overruns);
      arc_log(RETRO_LOG_INFO, "Audio counters: underrun_frames=%llu overrun_frames=%llu\n",
              (unsigned long long)underruns, (unsigned long long)overruns);
   }
   arc_loaded = false;
   arc_release_session();
   arc_osk_reset(&arc_osk, &arc_inputs);
   arc_osk_free_surface();
   arc_osk_present_dirty = false;
   arc_pending_initial = false;
   arc_pending_initial_path[0] = '\0';
   arc_input_sampling_reset(false);
   arc_guest_time_us = 0u;
}
RETRO_API void retro_reset(void)
{
   if (!arc_loaded) return;
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
   unsigned selected = arc_image_index;
   bool launch_reselected;

   arc_persist_cmos();
   if (!arc_read_options(&updated, error, sizeof(error)) ||
       !arc_select_firmware(updated.requested_os, updated.machine, &new_firmware,
                            &new_firmware_size, &firmware_match,
                            error, sizeof(error)))
      goto failed;
   launch_reselected = updated.launch != arc_core_options.launch;

   arc_engine_stop();
   if (updated.disk_writes != arc_core_options.disk_writes)
   {
      for (unsigned i = 0u; i < arc_media_count; ++i)
      {
         if (!arc_media[i].populated) continue;
         ArcMediaEntry replacement;
         if (!arc_prepare_source(arc_media[i].source, arc_media[i].label,
                                 updated.disk_writes, &replacement,
                                 error, sizeof(error)))
            goto failed;
         arc_jfd_free(&arc_media[i].jfd);
         arc_media[i] = replacement;
      }
   }
   if (!was_ejected && selected < arc_media_count &&
       !arc_refresh_jfd(&arc_media[selected], error, sizeof(error)))
      goto failed;

   free(arc_firmware);
   arc_firmware = new_firmware;
   new_firmware = NULL;
   arc_firmware_size = new_firmware_size;
   arc_core_options = updated;
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
   uint8_t payload[ARC_CMOS_PAYLOAD_SIZE];
   uint8_t machine_cmos[ARC_CMOS_MACHINE_SIZE];
   ArcEngineMedia mounted;
   ArcEngineMedia *mounted_pointer = NULL;
   if (!was_ejected && selected < arc_media_count && arc_media[selected].populated)
   {
      mounted = arc_engine_media(&arc_media[selected]);
      mounted_pointer = &mounted;
   }
   arc_prepare_launch(mounted_pointer ? &arc_media[selected] : NULL, was_ejected,
                      updated.launch, launch_reselected);
   if (!arc_resolve_inputs(&arc_core_options, true, false))
   { snprintf(error, sizeof(error), "Unable to resolve ArcDroid input options."); goto failed; }
   ArcEngineMedia helper = {arc_helper_path, false, ARC_MEDIA_ADL_640K,
                            ARC_GEOMETRY_640K_L, NULL};
   if (!arc_cmos_decode(arc_cmos_record, &arc_plan, payload) ||
       !arc_cmos_machine_image(arc_plan.os, reset_machine->io, payload,
                               arc_plan.launch_helper, machine_cmos) ||
       !arc_engine_start(arc_firmware, arc_firmware_size, mounted_pointer,
                         arc_plan.launch_helper ? &helper : NULL,
                         reset_machine,
                         machine_cmos, error, sizeof(error)))
      goto failed;
   arc_loaded = true;
   arc_ejected = was_ejected;
   arc_image_index = selected <= arc_media_count ? selected : arc_media_count;
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
   arc_message(error[0] ? error : "Unable to reset the ArcDroid session.", RETRO_LOG_ERROR);
   arc_loaded = false;
   arc_release_session();
   arc_input_sampling_reset(false);
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
      arc_video(duplicate ? NULL : frame->pixels, frame->width, frame->height,
                frame->pitch);
      arc_osk_present_dirty = false;
      return;
   }
   if (frame->width < 320u)
   {
      arc_osk_close(&arc_osk, &arc_inputs);
      arc_osk_restore_gameplay();
      arc_osk_report_too_narrow();
      arc_video(frame->pixels, frame->width, frame->height, frame->pitch);
      arc_osk_present_dirty = false;
      return;
   }
   if (!arc_osk_prepare_surface(frame->width, frame->height) ||
       ((arc_osk.width != frame->width || arc_osk.height != frame->height) &&
        !arc_osk_layout(&arc_osk, frame->width, frame->height)))
   {
      arc_osk_presentation_failure();
      arc_video(frame->pixels, frame->width, frame->height, frame->pitch);
      arc_osk_present_dirty = false;
      return;
   }
   if (!frame->field_completed && arc_can_dupe && !arc_osk_present_dirty)
   {
      arc_video(NULL, frame->width, frame->height, frame->pitch);
      return;
   }
   if (!frame->pixels || frame->pitch < (size_t)frame->width * sizeof(uint32_t))
   {
      arc_osk_presentation_failure();
      arc_video(frame->pixels, frame->width, frame->height, frame->pitch);
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
   char profile[128];
   snprintf(profile, sizeof(profile), "%s%s", arc_input_resolution.profile_name,
            arc_input_resolution.custom ? " custom" : "");
   if (!arc_osk_render(&arc_osk, arc_osk_surface, frame->width, frame->height,
                       (size_t)frame->width * sizeof(uint32_t), profile))
   {
      arc_osk_presentation_failure();
      arc_video(frame->pixels, frame->width, frame->height, frame->pitch);
   }
   else
   {
      arc_video(arc_osk_surface, frame->width, frame->height,
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

RETRO_API void retro_run(void)
{
   ArcEngineFrame frame;
   size_t count;
   bool frame_ran;
   bool options_updated = false;
   if (arc_environment &&
       arc_environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &options_updated) &&
       options_updated)
   {
      ArcCoreOptions updated;
      char option_error[160] = {0};
      bool valid_update = arc_read_options(&updated, option_error, sizeof(option_error));
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
      if (valid_update && (strcmp(updated.input_profile, arc_core_options.input_profile) ||
          memcmp(updated.pad, arc_core_options.pad, sizeof(updated.pad)) ||
          updated.osk_toggle != arc_core_options.osk_toggle))
      {
         if (!arc_resolve_inputs(&updated, true, false))
            arc_message("Unable to resolve ArcDroid input options.", RETRO_LOG_ERROR);
         else
         {
            strcpy(arc_core_options.input_profile, updated.input_profile);
            memcpy(arc_core_options.pad, updated.pad, sizeof(updated.pad));
            arc_core_options.osk_toggle = updated.osk_toggle;
         }
      }
   }
   if (arc_input_poll) arc_input_poll();
   arc_poll_inputs();
   if (arc_input_take_overflow_report(&arc_inputs))
      arc_message("Input queue overflowed; recovered to current input levels.", RETRO_LOG_WARN);
   if (!arc_loaded)
   {
      if (arc_video) arc_video(arc_frame, ARC_WIDTH, ARC_HEIGHT, ARC_WIDTH * sizeof(uint32_t));
      return;
   }
   ++arc_frame_number;
#if defined(ARCDROID_TEST_FAULTS)
   frame_ran = arc_engine_run_frame(arc_test_stall_kart ? NULL : &arc_inputs,
                                    &frame);
#else
   frame_ran = arc_engine_run_frame(&arc_inputs, &frame);
#endif
   if (!frame_ran)
   {
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
RETRO_API void *retro_get_memory_data(unsigned id) { return id == RETRO_MEMORY_SAVE_RAM ? arc_cmos_record : NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { return id == RETRO_MEMORY_SAVE_RAM ? sizeof(arc_cmos_record) : 0u; }
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
