/* ArcDroid libretro adapter. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "libretro.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
static uint32_t arc_frame[ARC_WIDTH * ARC_HEIGHT];
static int16_t arc_audio_frame[960u * 2u];
static uint8_t arc_cmos_record[256];
static bool arc_loaded;
static bool arc_ejected;
static unsigned arc_image_index;
static unsigned arc_initial_index;
static char arc_initial_path[4096];

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
   (void)down;
   (void)keycode;
   (void)character;
   (void)modifiers;
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
   arc_loaded = false;
   arc_ejected = false;
   arc_image_index = arc_initial_index = 0u;
   arc_initial_path[0] = '\0';
   memset(arc_cmos_record, 0, sizeof(arc_cmos_record));
   if (arc_environment)
   {
      arc_environment(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, (void *)&disk);
      if (arc_environment(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs))
         arc_vfs = vfs.iface;
   }
}

RETRO_API void retro_deinit(void)
{
   arc_loaded = false;
   arc_vfs = NULL;
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
   if (!game || !game->path || !game->path[0])
   {
      arc_message("ArcDroid requires a loose ADF or ADL image.", RETRO_LOG_ERROR);
      return false;
   }
   if (!arc_environment || !arc_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format))
      return false;
   arc_environment(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);
   arc_environment(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, (void *)&keyboard);
   if (strlen(game->path) >= sizeof(arc_initial_path))
   {
      arc_message("Content path is too long.", RETRO_LOG_ERROR);
      return false;
   }
   strcpy(arc_initial_path, game->path);
   arc_loaded = true;
   arc_image_index = arc_initial_index;
   memset(arc_frame, 0, sizeof(arc_frame));
   return true;
}

RETRO_API void retro_unload_game(void) { arc_loaded = false; }
RETRO_API void retro_reset(void) { memset(arc_cmos_record + 32u, 0, sizeof(arc_cmos_record) - 32u); }

RETRO_API void retro_run(void)
{
   size_t count = sizeof(arc_audio_frame) / (2u * sizeof(arc_audio_frame[0]));
   if (arc_input_poll) arc_input_poll();
   if (!arc_loaded)
   {
      if (arc_video) arc_video(NULL, ARC_WIDTH, ARC_HEIGHT, ARC_WIDTH * sizeof(uint32_t));
      return;
   }
   if (arc_video) arc_video(arc_frame, ARC_WIDTH, ARC_HEIGHT, ARC_WIDTH * sizeof(uint32_t));
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
