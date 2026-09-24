/* ArcDroid libretro adapter. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "libretro.h"
#include "arc_io.h"
#include "arcdroid_core.h"
#include "arcdroid_engine.h"
#include "arcdroid_platform.h"
#include "adffs.h"
#include "folder_set.h"
#include "jfd.h"
#include "playlist.h"
#include "zip_content.h"

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
static int16_t arc_audio_frame[2048u * 2u];
static uint8_t arc_cmos_record[256];
static uint8_t arc_cmos_last_written[256];
static uint8_t arc_cmos_prepared[256];
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
typedef enum {
   ARC_SESSION_EMPTY = 0,
   ARC_SESSION_PREPARED,
   ARC_SESSION_CHOOSING,
   ARC_SESSION_RUNNING,
   ARC_SESSION_FAILED
} ArcSessionState;
static ArcSessionState arc_session;
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
#if defined(ARCDROID_TEST_FAULTS)
static bool arc_test_adffs_accept_test_os = true;
static bool arc_test_fail_adffs_content_read;
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
   ArcDigest digest;
   ArcJfdImage jfd;
   char zip_member[256];
   uint32_t zip_crc32;
   uint32_t zip_packed_size;
   uint32_t zip_size;
} ArcMediaEntry;

static ArcMediaEntry arc_media[ARC_PLAYLIST_MAX_ENTRIES];
static unsigned arc_media_count;
#if defined(ARCDROID_TEST_FAULTS)
const char *arc_libretro_test_staged_path(unsigned index)
{
   return index < arc_media_count && arc_media[index].populated ?
      arc_media[index].staged : NULL;
}
const char *arc_libretro_test_cmos_path(void) { return arc_cmos_path; }
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

typedef enum { ARC_JFD_BOOT_AUTO = 0, ARC_JFD_BOOT_NATIVE } ArcJfdBootOption;
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
   ArcLaunchOption launch;
   ArcLaunchChooserOption launch_chooser;
   ArcJfdBootOption jfd_boot;
   ArcAdffsOption adffs_package;
   char input_profile[64];
   ArcInputOverride pad[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT];
   ArcOskToggle osk_toggle;
} ArcCoreOptions;

static ArcCoreOptions arc_core_options;
static ArcMachine arc_session_machine;
static bool arc_riscpc_notice_shown;

static bool arc_media_is_hd(const ArcMediaEntry *entry)
{
   return entry && entry->populated && entry->high_density;
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
   const char *launch = arc_option_value("arcdroid_launch", "auto");
   const char *launch_chooser =
      arc_option_value("arcdroid_launch_chooser", "remember");
   const char *jfd_boot = arc_option_value("arcdroid_jfd_boot", "auto");
   const char *adffs_package = arc_option_value("arcdroid_adffs_package", "verified");
   const char *profile = arc_option_value("arcdroid_input_profile", "Auto");
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
   if (!strcmp(jfd_boot, "auto")) options->jfd_boot = ARC_JFD_BOOT_AUTO;
   else if (!strcmp(jfd_boot, "native")) options->jfd_boot = ARC_JFD_BOOT_NATIVE;
   else goto invalid;
   if (!strcmp(adffs_package, "verified")) options->adffs_package = ARC_ADFFS_VERIFIED_ONLY;
   else if (!strcmp(adffs_package, "untested")) options->adffs_package = ARC_ADFFS_ALLOW_UNTESTED;
   else goto invalid;
   if (!strcmp(launch_chooser, "remember"))
      options->launch_chooser = ARC_LAUNCH_CHOOSER_REMEMBER;
   else if (!strcmp(launch_chooser, "ask"))
      options->launch_chooser = ARC_LAUNCH_CHOOSER_ASK;
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
   arc_chooser_input_armed = false;
   arc_chooser_release_gate = false;
   arc_engine_stop();
   arc_adffs_discard();
   free(arc_firmware);
   arc_firmware = NULL;
   arc_firmware_size = 0u;
   for (unsigned i = 0u; i < ARC_PLAYLIST_MAX_ENTRIES; ++i)
      arc_jfd_free(&arc_media[i].jfd);
   memset(arc_media, 0, sizeof(arc_media));
   arc_media_count = 0u;
   arc_folder_set_active = false;
   arc_zip_active = false;
   arc_zip_path[0] = '\0';
   arc_zip_adffs_raw_discard();
   arc_image_index = 0u;
   arc_cmos_path[0] = '\0';
   arc_helper_path[0] = '\0';
   arc_choice_path[0] = '\0';
   arc_cmos_last_written_valid = false;
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
   memcpy(arc_cmos_prepared, arc_cmos_record, sizeof(arc_cmos_prepared));
   free(existing);
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
      for (size_t i = 0u; i < entry->jfd.sector_count; ++i)
         if (entry->jfd.sectors[i].density == 4u)
            entry->high_density = true;
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
      char warning[192];
      snprintf(warning, sizeof(warning), "Short image padded with %zu missing bytes; source remains unchanged.",
               entry->info.padding_required);
      arc_media_notice(warning, notify_user, notice_reported);
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
      entry->info.kind != ARC_MEDIA_JFD && arc_plan.disk_writes != ARC_DISK_WRITES_OFF,
      entry->info.kind, entry->info.geometry,
      entry->info.kind == ARC_MEDIA_JFD ? &entry->jfd : NULL, NULL, 0u};
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
static bool arc_prepare_adffs(bool notify) {
   arc_adffs_discard();
   ArcMediaEntry *entry = arc_media_count ? &arc_media[0] : NULL;
   const ArcJfdImage *jfd = entry ? &entry->jfd : NULL;
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
   if (!arc_adffs_select(package, script, script_size, &selection, missing)) {
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
   if (arc_log)
      arc_log(RETRO_LOG_INFO,
              "JFD boot: path=adffs script=%s profile=%s version=\"%s\"\n",
              have_builtin ? "builtin" : "embedded",
              package->verified ? package->profile : "untested", package->version);
   arc_adffs_package_free(package);
   free(package);
   arc_adffs_active = true;
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
   unsigned ordinal = 0u;
   for (unsigned i = 0u; i < discovery->candidate_count && at < sizeof(message); ++i)
   {
      if (discovery->candidates[i].resource) continue;
      ++ordinal;
      if (applications_only &&
          discovery->candidates[i].state != ARC_LAUNCH_CANDIDATE_APPLICATION)
         continue;
      char name[4u * ARC_GUEST_NAME_MAX + 1u];
      (void)arc_launch_escape_name(&discovery->candidates[i].name,
                                   name, sizeof(name));
      int written = snprintf(message + at, sizeof(message) - at, "%s%u %s",
                             listed ? " " : "", ordinal, name);
      if (written < 0 || (size_t)written >= sizeof(message) - at)
      { at = sizeof(message) - 1u; break; }
      at += (size_t)written;
      listed = true;
   }
   if (at < sizeof(message) - 28u)
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
      const ArcGuestName *target = &discovery->candidates[selected].name;
      if (!reselected)
      {
         if (binding.target.length != target->length ||
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
   if (!arc_launch_binding_format(ordinal,
          &discovery->candidates[selected].name,
          discovery->candidate_fingerprint,
          binding, sizeof(binding), &binding_size) ||
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
   char decision[4u * ARC_GUEST_NAME_MAX + 1u] = "none";
   size_t ca = 0u, aa = 0u;
   unsigned ordinal = 0u;
   (void)arc_launch_escape_name(&discovery->disc_name,
                                disc_name, sizeof(disc_name));
   if (decision_index >= 0 &&
       (unsigned)decision_index < discovery->candidate_count)
      (void)arc_launch_escape_name(
         &discovery->candidates[decision_index].name,
         decision, sizeof(decision));
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
   {
      char name[4u * ARC_GUEST_NAME_MAX + 1u];
      (void)arc_launch_escape_name(&discovery->candidates[i].name,
                                   name, sizeof(name));
      const char *state = discovery->candidates[i].state == ARC_LAUNCH_CANDIDATE_APPLICATION ? "app" :
                          discovery->candidates[i].state == ARC_LAUNCH_CANDIDATE_UNKNOWN ? "unknown" : "other";
      if (discovery->candidates[i].resource) state = "resource";
      int n = snprintf(candidates + ca, sizeof(candidates) - ca, "%s%u:%s:%s",
                       i ? "," : "", i + 1u, name, state);
      if (n > 0 && (size_t)n < sizeof(candidates) - ca) ca += (size_t)n;
      if (!discovery->candidates[i].resource) ++ordinal;
      if (discovery->candidates[i].state == ARC_LAUNCH_CANDIDATE_APPLICATION &&
          !discovery->candidates[i].resource)
      {
         n = snprintf(applications + aa, sizeof(applications) - aa, "%s%u:%s",
                      aa ? "," : "", ordinal, name);
         if (n > 0 && (size_t)n < sizeof(applications) - aa) aa += (size_t)n;
      }
   }
   arc_log(RETRO_LOG_INFO,
      "Launch: boot=%u disc=\"%s\" candidates=[%s] applications=[%s] decision=%s reason=%s ignored-attribute-bits=%s helper=%s\n",
      discovery->boot_option, disc_name, candidates, applications,
      decision, reason, discovery->ignored_attribute_bits ? "yes" : "no",
      arc_helper_path[0] ? arc_helper_path : "none");
}

static void arc_launch_log_skipped_siblings(
   const ArcLaunchDiscovery *discovery, unsigned target_index)
{
   if (!arc_log || !discovery) return;
   for (unsigned i = 0u; i < discovery->candidate_count; ++i)
   {
      const ArcLaunchCandidate *candidate = &discovery->candidates[i];
      if (i == target_index || !candidate->has_boot ||
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
   if (!arc_launch_choice_format(target, selected < 0,
                                 discovery->candidate_fingerprint,
                                 data, sizeof(data), &size)) return false;
   size_t ignored = 0u;
   bool replacing = arc_io_file_size(arc_choice_path, &ignored);
   return arc_launch_binding_publish(arc_choice_path, replacing, data, size);
}

static bool arc_launch_helper_publish(const ArcLaunchDiscovery *discovery,
                                      unsigned selected, char *refusal,
                                      size_t refusal_size)
{
   uint8_t *helper = malloc(ARC_LAUNCH_DISC_SIZE);
   if (!helper || !arc_launch_disc_build(discovery, selected, helper,
                                         refusal, refusal_size))
   {
      free(helper);
      return false;
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
   if (!arc_helper_path[0]) return false;
   arc_plan.launch_helper = true;
   arc_plan.launch_target_length = discovery->candidates[selected].name.length;
   memcpy(arc_plan.launch_target, discovery->candidates[selected].name.bytes,
          arc_plan.launch_target_length);
   arc_plan.launch_target[arc_plan.launch_target_length] = '\0';
   arc_launch_log_skipped_siblings(discovery, selected);
   return true;
}

static bool arc_prepare_launch(ArcMediaEntry *entry, bool ejected,
                               ArcLaunchOption option, bool reselected)
{
   ArcLaunchDiscovery discovery;
   memset(&discovery, 0, sizeof(discovery));
   discovery.target_index = -1;
   discovery.reason = ARC_LAUNCH_REASON_UNREADABLE;
   arc_helper_path[0] = '\0';
   arc_plan.launch_helper = false;
   arc_plan.launch_target[0] = '\0';
   arc_plan.launch_target_length = 0u;
   if (ejected || !entry || !entry->populated)
   { arc_launch_log(&discovery, -1, "drive 0 empty"); return false; }

   void *raw = NULL;
   size_t raw_size = 0u;
   const ArcJfdImage *jfd = entry->info.kind == ARC_MEDIA_JFD ? &entry->jfd : NULL;
   if (!jfd && !arc_io_read_all(entry->staged, &raw, &raw_size))
   { arc_launch_log(&discovery, -1, "media read failed"); return false; }
   (void)arc_launch_discover(raw, raw_size, &entry->info, jfd, &discovery);
   free(raw);
   if (arc_log && discovery.backup_map_used)
      arc_log(RETRO_LOG_WARN, "FileCore: primary map invalid or unreadable; using backup map.\n");
   if (arc_log && discovery.old_map_clamped)
      arc_log(RETRO_LOG_WARN, "FileCore: declared old-map sector count exceeds physical geometry; clamped.\n");
   if (option == ARC_LAUNCH_OFF)
   { arc_launch_log(&discovery, -1, "disabled"); return false; }
   if (!arc_launch_os_supported(arc_plan.os))
   { arc_launch_log(&discovery, -1, "OS class unsupported"); return false; }

   int selected = discovery.target_index;
   char refusal[160] = {0};
   const char *reason = arc_launch_reason_name(discovery.reason);
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
         snprintf(refusal, sizeof(refusal), "Application choice refused: candidate %u leaf is not launchable", ordinal + 1u);
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
      if (chooser_eligible)
      {
         bool remembered = false;
         int remembered_target = INT_MIN;
         (void)arc_choice_load(&discovery, &remembered, &remembered_target);
         if (remembered &&
             arc_core_options.launch_chooser == ARC_LAUNCH_CHOOSER_REMEMBER)
         {
            if (remembered_target >= 0 &&
                !arc_launch_helper_publish(&discovery,
                   (unsigned)remembered_target, refusal, sizeof(refusal)))
            {
               arc_launch_log(&discovery, -1,
                  refusal[0] ? refusal : "remembered helper publication failed");
               return false;
            }
            arc_launch_log(&discovery, remembered_target,
               remembered_target < 0 ? "chooser remembered desktop" :
                                       "chooser remembered target");
            return false;
         }
         if (arc_launch_chooser_init(&arc_chooser, &discovery, remembered,
                                     remembered_target))
         {
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
               discovery.candidate_count)
         arc_launch_candidates_message(&discovery,
            "Application inspection incomplete: ", false);
      arc_launch_log(&discovery, -1, reason);
      return false;
   }
   if (!arc_launch_helper_publish(&discovery, (unsigned)selected,
                                  refusal, sizeof(refusal)))
   {
      arc_launch_log(&discovery, -1,
         refusal[0] ? refusal : "helper publication failed");
      return false;
   }
   arc_launch_log(&discovery, selected, reason);
   return false;
}

static bool arc_disk_set_eject(bool ejected)
{
   char error[512] = {0};
   if (!arc_loaded || arc_session != ARC_SESSION_RUNNING || arc_adffs_active) return false;
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
      if (!arc_machine_accepts_media(arc_session_machine, entry,
                                     error, sizeof(error)))
      {
         arc_message(error, RETRO_LOG_ERROR);
         return false;
      }
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
static bool arc_disk_get_eject(void) { return arc_adffs_active ? false : arc_ejected; }
static unsigned arc_disk_get_index(void) { return arc_adffs_active ? 0u : arc_image_index; }
static bool arc_disk_set_index(unsigned index)
{
   if (!arc_loaded || arc_session != ARC_SESSION_RUNNING || arc_adffs_active || !arc_ejected) return false;
   if (index < arc_media_count && !arc_media[index].populated) return false;
   arc_image_index = index < arc_media_count ? index : arc_media_count;
   return true;
}
static unsigned arc_disk_get_count(void) { return arc_adffs_active ? 1u : arc_media_count; }
static bool arc_disk_replace(unsigned index, const struct retro_game_info *info)
{
   if (!arc_loaded || arc_session != ARC_SESSION_RUNNING || arc_adffs_active || !arc_ejected ||
       index >= arc_media_count) return false;
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
   if (!arc_machine_accepts_media(arc_session_machine, &replacement,
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
   if (!arc_loaded || arc_session != ARC_SESSION_RUNNING || arc_adffs_active ||
       arc_media_count >= ARC_PLAYLIST_MAX_ENTRIES) return false;
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
   if (arc_chooser_release_gate)
   {
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
      arc_input_reset(&arc_inputs);
      arc_platform_joystick_snapshot(false, NULL);
      arc_stick_x = arc_stick_y = 0;
      if (neutral)
      {
         arc_chooser_release_gate = false;
         memset(arc_keyboard_raw, 0, sizeof(arc_keyboard_raw));
      }
      return;
   }
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
   {"arcdroid_launch_chooser", "Launch Chooser", NULL,
    "Remember the selected application, or ask on every eligible launch.", NULL,
    NULL, {{"remember", "Remember"}, {"ask", "Ask every time"},
           {NULL, NULL}}, "remember"},
   {"arcdroid_jfd_boot", "JFD Boot", NULL,
    "Use a user-supplied ADFFS package for eligible single-disc JFD images.", NULL,
    NULL, {{"auto", "Auto"}, {"native", "Native"}, {NULL, NULL}}, "auto"},
   {"arcdroid_adffs_package", "ADFFS Package", NULL,
    "Restrict ADFFS to a verified package profile, or allow an untested package.", NULL,
    NULL, {{"verified", "Verified only"}, {"untested", "Allow untested"}, {NULL, NULL}}, "verified"},
   {"arcdroid_disk_writes", "Disk Writes", NULL, "Choose where floppy writes are stored.", NULL,
    NULL, {{"shadow", NULL}, {"original", NULL}, {"off", NULL}, {NULL, NULL}}, "shadow"},
   {NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL}
};
static struct retro_core_option_v2_definition arc_options[37];
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
          14u * sizeof(struct retro_core_option_v2_definition));
   unsigned at = 14u;
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
         (strlen(arc_test_input_profile.launch_target) ==
             arc_plan.launch_target_length &&
          !memcmp(arc_test_input_profile.launch_target, arc_plan.launch_target,
                  arc_plan.launch_target_length));
      for (unsigned digest = 0u;
           target_matches && digest < arc_test_input_profile.digest_count;
           ++digest)
         if (!strcmp(arc_test_input_profile.digests[digest],
                     arc_content_digest.sha1))
            use_test_profile = true;
   }
   bool resolved_ok = use_test_profile ?
      arc_input_resolve_table(&arc_test_input_profile, 1u,
                              arc_content_digest.sha1,
                              (const uint8_t *)arc_plan.launch_target,
                              arc_plan.launch_target_length,
                              options->input_profile, options->pad, &resolved) :
      arc_input_resolve(arc_content_digest.sha1,
                        (const uint8_t *)arc_plan.launch_target,
                        arc_plan.launch_target_length,
                        options->input_profile, options->pad, &resolved);
#else
   bool resolved_ok = arc_input_resolve(arc_content_digest.sha1,
                                        (const uint8_t *)arc_plan.launch_target,
                                        arc_plan.launch_target_length,
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
   info->valid_extensions = "adf|adl|jfd|m3u|zip";
   info->need_fullpath = true;
   info->block_extract = true;
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
   arc_vfs = NULL;
   arc_io_set_vfs(NULL);
   arc_environment = NULL;
   arc_video = NULL;
   arc_audio = NULL;
   arc_audio_batch = NULL;
   arc_input_poll = NULL;
   arc_input_state = NULL;
}

static bool arc_construct_machine(const ArcMachineDescriptor *machine,
                                  ArcEngineMedia *mounted,
                                  char *error, size_t error_size)
{
   uint8_t payload[ARC_CMOS_PAYLOAD_SIZE];
   uint8_t machine_cmos[ARC_CMOS_MACHINE_SIZE];
   ArcEngineMedia helper = {arc_helper_path, false, ARC_MEDIA_ADL_640K,
                            ARC_GEOMETRY_640K_L, NULL, NULL, 0u};
   ArcEngineMedia carrier = {":arcdroid-memory-0:", false, ARC_MEDIA_ADF_800K,
                             ARC_GEOMETRY_800K_D, NULL,
                             arc_adffs_carrier, ARC_ADFFS_DISC_SIZE};
   ArcEngineMedia support = {":arcdroid-memory-1:", false, ARC_MEDIA_ADF_800K,
                             ARC_GEOMETRY_800K_D, NULL,
                             arc_adffs_support, ARC_ADFFS_DISC_SIZE};
   if (arc_adffs_active) { mounted = &carrier; helper = support; }
   memcpy(arc_cmos_record, arc_cmos_prepared, sizeof(arc_cmos_record));
   if (!arc_cmos_decode(arc_cmos_prepared, &arc_plan, payload) ||
       !arc_cmos_machine_image(arc_plan.os, machine->io, payload,
                               arc_plan.launch_helper, machine_cmos) ||
       !arc_engine_start(arc_firmware, arc_firmware_size, mounted,
                         arc_plan.launch_helper ? &helper : NULL,
                         machine, machine_cmos, error, error_size))
      return false;
   arc_session = ARC_SESSION_RUNNING;
   return true;
}

static void arc_chooser_reset_input(void)
{
   memset(arc_chooser_pad_levels, 0, sizeof(arc_chooser_pad_levels));
   memset(arc_chooser_keyboard_levels, 0, sizeof(arc_chooser_keyboard_levels));
   arc_chooser_pointer_down = false;
   arc_chooser_pointer_row = -1;
   arc_chooser_input_armed = false;
   arc_input_reset(&arc_inputs);
   arc_platform_joystick_snapshot(false, NULL);
}

static bool arc_chooser_confirm(void)
{
   int selected = arc_launch_chooser_selection(&arc_chooser);
   if (selected == INT_MIN) return false;
   bool remembered = arc_choice_publish(&arc_chooser_discovery, selected);
   if (!remembered)
      arc_message("Launch choice not remembered; starting this time only.",
                  RETRO_LOG_WARN);
   char error[512] = {0};
   arc_helper_path[0] = '\0';
   arc_plan.launch_helper = false;
   arc_plan.launch_target[0] = '\0';
   arc_plan.launch_target_length = 0u;
   if (selected >= 0 &&
       !arc_launch_helper_publish(&arc_chooser_discovery, (unsigned)selected,
                                  error, sizeof(error)))
      goto failed;
   arc_launch_log(&arc_chooser_discovery, selected,
                  selected < 0 ? "chooser chose desktop" :
                                 "chooser chose target");
   if (!arc_resolve_inputs(&arc_core_options, true, false))
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
      if (!arc_refresh_jfd(&arc_media[arc_image_index], error, sizeof(error)))
      {
         arc_message(error, RETRO_LOG_ERROR);
         arc_release_session();
         return false;
      }
      bool adffs = arc_prepare_adffs(true);
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
      else if (!arc_resolve_inputs(&arc_core_options, false, true))
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
   arc_input_sampling_reset(false);
   arc_guest_time_us = 0u;
}
RETRO_API void retro_reset(void)
{
   if (!arc_loaded) return;
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
   unsigned selected = arc_image_index;
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
       !arc_refresh_jfd(&arc_media[selected], error, sizeof(error)))
      goto failed;

   free(arc_firmware);
   arc_firmware = new_firmware;
   new_firmware = NULL;
   arc_firmware_size = new_firmware_size;
   arc_core_options = updated;
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
   bool adffs = !was_ejected && selected == 0u && arc_prepare_adffs(false);
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
      if (!arc_resolve_inputs(&arc_core_options, true, false))
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
   arc_message(error[0] ? error : "Unable to reset the ArcDroid session.", RETRO_LOG_ERROR);
   if (!torn_down) return;
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
      if (arc_video)
         arc_video(arc_frame, ARC_WIDTH, ARC_HEIGHT,
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
