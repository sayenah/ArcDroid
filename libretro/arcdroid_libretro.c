/* ArcDroid libretro adapter. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "libretro.h"
#include "arc_io.h"
#include "arcdroid_core.h"
#include "arcdroid_engine.h"

#include <stdbool.h>
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
static uint8_t *arc_content;
static size_t arc_content_size;
static uint8_t *arc_firmware;
static size_t arc_firmware_size;
static ArcDigest arc_content_digest;
static ArcLaunchPlan arc_plan;
static ArcInputState arc_inputs;
static bool arc_pad_keys[10];
static char arc_cmos_path[4096];
static char arc_staged_path[4096];
static uint64_t arc_frame_number;
static long double arc_audio_fraction;
static unsigned arc_video_width = ARC_WIDTH;
static unsigned arc_video_height = ARC_HEIGHT;
static double arc_video_rate = ARC_RATE;
static bool arc_fault_reported;
static bool arc_vidc_warning_reported;
static bool arc_loaded;
static bool arc_ejected;
static unsigned arc_image_index;
static unsigned arc_initial_index;
static char arc_initial_path[4096];

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
   size_t suspicious_size;
   ArcFirmwarePart parts[ARC_OS_ARTHUR_030 + 1u][4];
} ArcFirmwareSearch;

static unsigned arc_firmware_priority(ArcOsClass os)
{
   switch (os)
   {
      case ARC_OS_TEST: return 0u;
      case ARC_OS_RISCOS_311: return 1u;
      case ARC_OS_RISCOS_310: return 2u;
      case ARC_OS_RISCOS_319: return 3u;
      case ARC_OS_RISCOS_300: return 4u;
      case ARC_OS_RISCOS_200: return 5u;
      case ARC_OS_ARTHUR_120: return 6u;
      default: return 100u;
   }
}

static bool arc_visit_firmware(const char *path, void *opaque)
{
   ArcFirmwareSearch *search = (ArcFirmwareSearch *)opaque;
   void *data = NULL;
   size_t size = 0u;
   if (!arc_io_read_all(path, &data, &size)) return true;
   ArcDigest digest = arc_digest(data, size);
   const ArcFirmwareFingerprint *match = arc_firmware_match(&digest);
   if (match && match->recipe == ARC_RECIPE_COMBINED)
   {
      unsigned priority = arc_firmware_priority(match->os);
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
      ArcFirmwarePart *part = &search->parts[match->os][match->lane];
      free(part->data);
      part->data = data;
      part->size = size;
      part->match = match;
      return true;
   }
   else if (size == 524289u)
      search->suspicious_size = size;
   free(data);
   return true;
}

static void arc_finish_firmware_search(ArcFirmwareSearch *search)
{
   char error[160];
   for (unsigned os = ARC_OS_TEST; os <= ARC_OS_ARTHUR_030; ++os)
   {
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
      unsigned priority = arc_firmware_priority((ArcOsClass)os);
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
   free(arc_content);
   free(arc_firmware);
   arc_content = NULL;
   arc_firmware = NULL;
   arc_content_size = arc_firmware_size = 0u;
   arc_cmos_path[0] = arc_staged_path[0] = '\0';
   memset(&arc_plan, 0, sizeof(arc_plan));
}

static void arc_sync_cmos(void)
{
   uint8_t payload[ARC_CMOS_PAYLOAD_SIZE];
   if (!arc_loaded || !arc_engine_active()) return;
   if (!arc_cmos_decode(arc_cmos_record, &arc_plan, payload))
      memset(payload, 0, sizeof(payload));
   arc_engine_cmos(payload);
   arc_cmos_encode(arc_cmos_record, &arc_plan, payload);
}

static void arc_seed_cmos(void)
{
   uint8_t payload[ARC_CMOS_PAYLOAD_SIZE] = {0};
   void *existing = NULL;
   size_t existing_size = 0u;
   if (arc_cmos_path[0] && arc_io_read_all(arc_cmos_path, &existing, &existing_size) &&
       existing_size == sizeof(arc_cmos_record) &&
       arc_cmos_decode((const uint8_t *)existing, &arc_plan, payload))
      memcpy(arc_cmos_record, existing, sizeof(arc_cmos_record));
   else
   {
      if (!arc_cmos_default(arc_plan.os, payload))
         memset(payload, 0, sizeof(payload));
      arc_cmos_encode(arc_cmos_record, &arc_plan, payload);
   }
   free(existing);
}

static void arc_persist_cmos(void)
{
   uint8_t payload[ARC_CMOS_PAYLOAD_SIZE];
   if (!arc_cmos_path[0]) return;
   arc_sync_cmos();
   if (!arc_cmos_decode(arc_cmos_record, &arc_plan, payload))
   {
      memset(payload, 0, sizeof(payload));
      arc_cmos_encode(arc_cmos_record, &arc_plan, payload);
   }
   arc_io_write_all(arc_cmos_path, arc_cmos_record, sizeof(arc_cmos_record));
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
   if (!arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &ext))
      arc_environment(RETRO_ENVIRONMENT_SET_MESSAGE, &legacy);
}

static bool arc_disk_set_eject(bool ejected)
{
   arc_ejected = ejected;
   return true;
}
static bool arc_disk_get_eject(void) { return arc_ejected; }
static unsigned arc_disk_get_index(void) { return arc_image_index; }
static bool arc_disk_set_index(unsigned index)
{
   if (index > 1u)
      return false;
   arc_image_index = index;
   return true;
}
static unsigned arc_disk_get_count(void) { return 2u; }
static bool arc_disk_replace(unsigned index, const struct retro_game_info *info)
{
   (void)info;
   return index == 0u;
}
static bool arc_disk_add(void) { return false; }
static bool arc_disk_set_initial(unsigned index, const char *path)
{
   arc_initial_index = index;
   if (path)
   {
      size_t length = strlen(path);
      if (length >= sizeof(arc_initial_path))
         return false;
      memcpy(arc_initial_path, path, length + 1u);
   }
   else
      arc_initial_path[0] = '\0';
   return index <= 1u;
}
static bool arc_disk_get_path(unsigned index, char *path, size_t length)
{
   const char *source = index == 0u ? arc_initial_path : "";
   size_t needed = strlen(source) + 1u;
   if (!path || needed > length || index > 1u)
      return false;
   memcpy(path, source, needed);
   return true;
}
static bool arc_disk_get_label(unsigned index, char *label, size_t length)
{
   const char *source = index == 0u ? "Disc 1" : "Empty drive";
   size_t needed = strlen(source) + 1u;
   if (!label || needed > length || index > 1u)
      return false;
   memcpy(label, source, needed);
   return true;
}

static void arc_keyboard(bool down, unsigned keycode, uint32_t character,
                         uint16_t modifiers)
{
   (void)character;
   (void)modifiers;
   arc_input_key(&arc_inputs, keycode, down, false);
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
   static const unsigned keys[] = {
      RETROK_UP, RETROK_DOWN, RETROK_LEFT, RETROK_RIGHT,
      RETROK_SPACE, RETROK_RETURN, RETROK_LSHIFT, RETROK_LCTRL,
      RETROK_ESCAPE, RETROK_TAB
   };
   if (!arc_input_state) return;
   for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
   {
      bool down = arc_input_state(0u, RETRO_DEVICE_JOYPAD, 0u, ids[i]) != 0;
      if (down != arc_pad_keys[i])
      {
         arc_pad_keys[i] = down;
         arc_input_key(&arc_inputs, keys[i], down, true);
      }
   }
   bool pointer = arc_input_state(0u, RETRO_DEVICE_POINTER, 0u,
                                  RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
   if (pointer || arc_inputs.pointer_contact)
      arc_input_pointer(&arc_inputs, pointer,
         arc_input_state(0u, RETRO_DEVICE_POINTER, 0u, RETRO_DEVICE_ID_POINTER_X),
         arc_input_state(0u, RETRO_DEVICE_POINTER, 0u, RETRO_DEVICE_ID_POINTER_Y),
         pointer ? 1u : 0u);
   else
   {
      int dx = arc_input_state(0u, RETRO_DEVICE_MOUSE, 0u, RETRO_DEVICE_ID_MOUSE_X);
      int dy = arc_input_state(0u, RETRO_DEVICE_MOUSE, 0u, RETRO_DEVICE_ID_MOUSE_Y);
      unsigned buttons = (arc_input_state(0u, RETRO_DEVICE_MOUSE, 0u, RETRO_DEVICE_ID_MOUSE_LEFT) ? 1u : 0u) |
                         (arc_input_state(0u, RETRO_DEVICE_MOUSE, 0u, RETRO_DEVICE_ID_MOUSE_RIGHT) ? 2u : 0u) |
                         (arc_input_state(0u, RETRO_DEVICE_MOUSE, 0u, RETRO_DEVICE_ID_MOUSE_MIDDLE) ? 4u : 0u);
      arc_input_mouse_add(&arc_inputs, dx, dy, buttons);
   }
}

static const struct retro_core_option_v2_definition arc_options[] = {
   {"arcdroid_machine", "Machine", NULL, "Automatic machine selection.", NULL,
    NULL, {{"Auto", NULL}, {"A3000", NULL}, {NULL, NULL}}, "Auto"},
   {"arcdroid_cpu", "CPU", NULL, "Automatic CPU selection.", NULL,
    NULL, {{"Auto", NULL}, {"ARM2", NULL}, {NULL, NULL}}, "Auto"},
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
   {"arcdroid_disk_writes", "Disk Writes", NULL, "Choose where floppy writes are stored.", NULL,
    NULL, {{"shadow", NULL}, {"original", NULL}, {"off", NULL}, {NULL, NULL}}, "shadow"},
   {"arcdroid_aspect", "Aspect", NULL, "Select reported display aspect.", NULL,
    NULL, {{"4:3", NULL}, {"native", NULL}, {NULL, NULL}}, "4:3"},
   {"arcdroid_borders", "Borders", NULL, "Select the presented border area.", NULL,
    NULL, {{"auto", NULL}, {"native", NULL}, {"none", NULL}, {NULL, NULL}}, "auto"},
   {NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL}
};
static const struct retro_core_options_v2 arc_options_v2 = {NULL, (struct retro_core_option_v2_definition *)arc_options};

RETRO_API void retro_set_environment(retro_environment_t callback)
{
   bool no_game = false;
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
   info->valid_extensions = "adf|adl";
   info->need_fullpath = true;
   info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
   if (!info) return;
   memset(info, 0, sizeof(*info));
   info->geometry.base_width = ARC_WIDTH;
   info->geometry.base_height = ARC_HEIGHT;
   info->geometry.max_width = 4096u;
   info->geometry.max_height = 2048u;
   info->geometry.aspect_ratio = 4.0f / 3.0f;
   info->timing.fps = ARC_RATE;
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
   arc_image_index = arc_initial_index = 0u;
   arc_initial_path[0] = '\0';
   memset(arc_cmos_record, 0, sizeof(arc_cmos_record));
   arc_input_reset(&arc_inputs);
   memset(arc_pad_keys, 0, sizeof(arc_pad_keys));
   arc_audio_fraction = 0u;
   arc_video_width = ARC_WIDTH;
   arc_video_height = ARC_HEIGHT;
   arc_video_rate = ARC_RATE;
   arc_fault_reported = false;
   arc_vidc_warning_reported = false;
   arc_release_session();
   arc_log = NULL;
   if (arc_environment)
   {
      arc_environment(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, (void *)&disk);
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

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
   enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
   static const struct retro_controller_description controllers[] = {
      {"RetroPad to Archimedes keys", RETRO_DEVICE_JOYPAD},
      {"Archimedes mouse", RETRO_DEVICE_MOUSE}
   };
   static const struct retro_controller_info ports[] = {
      {controllers, 2u}, {controllers, 2u}, {NULL, 0u}
   };
   static const struct retro_keyboard_callback keyboard = {arc_keyboard};
   const char *system_dir = NULL;
   const char *save_dir = NULL;
   char firmware_dir[4096];
   char media_dir[4096];
   char error[512] = {0};
   ArcFirmwareSearch search = {0};
   search.priority = 100u;
   ArcMediaKind media;
   void *content = NULL;
   size_t content_size = 0u;
   size_t staged_size = 0u;
   if (!game || !game->path || !game->path[0])
   {
      arc_message("ArcDroid requires a loose ADF or ADL image.", RETRO_LOG_ERROR);
      return false;
   }
   if (!arc_environment || !arc_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format))
      return false;
   arc_environment(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);
   arc_environment(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, (void *)&keyboard);
   if (!arc_vfs)
   {
      arc_message("ArcDroid requires the libretro VFS v3 interface.", RETRO_LOG_ERROR);
      return false;
   }
   if (strlen(game->path) >= sizeof(arc_initial_path))
   {
      arc_message("Content path is too long.", RETRO_LOG_ERROR);
      return false;
   }
   arc_release_session();
   if (!arc_io_read_all(game->path, &content, &content_size))
   {
      arc_message("Unable to read the selected content through the frontend VFS.", RETRO_LOG_ERROR);
      return false;
   }
   media = arc_media_inspect(game->path, content_size, error, sizeof(error));
   if (media == ARC_MEDIA_INVALID || media == ARC_MEDIA_ADF_HD_UNSUPPORTED)
   {
      free(content);
      arc_message(error, RETRO_LOG_ERROR);
      return false;
   }
   arc_content = (uint8_t *)content;
   arc_content_size = content_size;
   arc_content_digest = arc_digest(content, content_size);

   if (!arc_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) ||
       !system_dir || !system_dir[0] ||
       snprintf(firmware_dir, sizeof(firmware_dir), "%s/arcdroid/firmware", system_dir) < 0)
   {
      arc_message("Firmware directory unavailable; expected <system>/arcdroid/firmware.", RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   arc_io_walk(firmware_dir, 8u, arc_visit_firmware, &search);
   arc_finish_firmware_search(&search);
   if (!search.match)
   {
      if (search.suspicious_size)
         snprintf(error, sizeof(error), "Unrecognised firmware size %zu bytes; nearest expected size is 524288 bytes.", search.suspicious_size);
      else
         snprintf(error, sizeof(error), "Missing RISC OS 3.11 firmware in <system>/arcdroid/firmware.");
      arc_message(error, RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   arc_firmware = search.data;
   arc_firmware_size = search.size;
   if (!arc_launch_plan_default(&arc_plan, media, search.match->os,
                                arc_content_digest.sha1, false,
                                error, sizeof(error)))
   {
      arc_message(error, RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   if (!arc_environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) ||
       !save_dir || !save_dir[0])
   {
      arc_message("Save directory unavailable; ArcDroid cannot stage content safely.", RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   if (snprintf(media_dir, sizeof(media_dir), "%s/arcdroid/media/%s", save_dir,
                arc_content_digest.sha1) < 0 || !arc_io_make_path(media_dir) ||
       snprintf(arc_staged_path, sizeof(arc_staged_path), "%s/disc.%s", media_dir,
                media == ARC_MEDIA_ADL_640K ? "adl" : "adf") < 0 ||
       ((!arc_io_file_size(arc_staged_path, &staged_size) ||
         staged_size != arc_content_size) &&
        !arc_io_write_all(arc_staged_path, arc_content, arc_content_size)))
   {
      arc_message("Unable to stage content under <save>/arcdroid/media.", RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   if (snprintf(media_dir, sizeof(media_dir), "%s/arcdroid/cmos", save_dir) < 0 ||
       !arc_io_make_path(media_dir) ||
       snprintf(arc_cmos_path, sizeof(arc_cmos_path), "%s/A3000.%s.bin", media_dir,
                arc_os_slug(arc_plan.os)) < 0)
   {
      arc_message("Unable to prepare persistent CMOS storage.", RETRO_LOG_ERROR);
      arc_release_session();
      return false;
   }
   arc_seed_cmos();
   {
      uint8_t payload[ARC_CMOS_PAYLOAD_SIZE];
      uint8_t machine_cmos[ARC_CMOS_MACHINE_SIZE];
      if (!arc_cmos_decode(arc_cmos_record, &arc_plan, payload) ||
          !arc_cmos_machine_image(arc_plan.os, payload, machine_cmos) ||
          !arc_engine_start(arc_firmware, arc_firmware_size, arc_staged_path,
                            machine_cmos, error, sizeof(error)))
      {
         arc_message(error[0] ? error : "Unable to start the Arculator machine.", RETRO_LOG_ERROR);
         arc_release_session();
         return false;
      }
   }
   strcpy(arc_initial_path, game->path);
   arc_loaded = true;
   arc_image_index = arc_initial_index;
   memset(arc_frame, 0, sizeof(arc_frame));
   arc_frame_number = 0u;
   arc_audio_fraction = 0u;
   arc_video_width = ARC_WIDTH;
   arc_video_height = ARC_HEIGHT;
   arc_video_rate = ARC_RATE;
   arc_fault_reported = false;
   arc_vidc_warning_reported = false;
   snprintf(error, sizeof(error), "Firmware: %s; CRC32 %08x; SHA-1 %s",
            arc_os_name(search.match->os), search.match->crc32, search.match->sha1);
   arc_message(error, RETRO_LOG_INFO);
   return true;
}

RETRO_API void retro_unload_game(void)
{
   arc_persist_cmos();
   arc_loaded = false;
   arc_release_session();
}
RETRO_API void retro_reset(void)
{
   char error[512] = {0};
   arc_sync_cmos();
   if (arc_loaded && !arc_engine_reset(error, sizeof(error)))
      arc_message(error[0] ? error : "Machine reset failed.", RETRO_LOG_ERROR);
   arc_frame_number = 0u;
   arc_audio_fraction = 0u;
   arc_fault_reported = false;
   arc_vidc_warning_reported = false;
}

RETRO_API void retro_run(void)
{
   ArcEngineFrame frame;
   size_t count;
   if (arc_input_poll) arc_input_poll();
   arc_poll_inputs();
   if (!arc_loaded)
   {
      if (arc_video) arc_video(NULL, ARC_WIDTH, ARC_HEIGHT, ARC_WIDTH * sizeof(uint32_t));
      return;
   }
   ++arc_frame_number;
   if (!arc_engine_run_frame(&arc_inputs, &frame))
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
   if (!frame.field_completed && !arc_vidc_warning_reported)
   {
      arc_message("VIDC program did not complete a field; presenting bounded fallback.",
                  RETRO_LOG_WARN);
      arc_vidc_warning_reported = true;
   }
   if (frame.width != arc_video_width || frame.height != arc_video_height)
   {
      struct retro_game_geometry geometry = {
         frame.width, frame.height, 4096u, 2048u, 4.0f / 3.0f
      };
      if (arc_environment) arc_environment(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
      arc_video_width = frame.width;
      arc_video_height = frame.height;
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
   if (count > sizeof(arc_audio_frame) / (2u * sizeof(arc_audio_frame[0])))
      count = sizeof(arc_audio_frame) / (2u * sizeof(arc_audio_frame[0]));
   size_t produced = arc_engine_audio(arc_audio_frame, count);
   if (produced < count)
      memset(arc_audio_frame + produced * 2u, 0,
             (count - produced) * 2u * sizeof(arc_audio_frame[0]));
   arc_persist_cmos();
   if (arc_video) arc_video(frame.pixels, frame.width, frame.height, frame.pitch);
   if (arc_audio_batch) arc_audio_batch(arc_audio_frame, count);
   else if (arc_audio)
      for (size_t i = 0; i < count; ++i) arc_audio(arc_audio_frame[i * 2u], arc_audio_frame[i * 2u + 1u]);
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
{ (void)port; (void)device; }
