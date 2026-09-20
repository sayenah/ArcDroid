/* ArcDroid libretro lifecycle/API harness. GPL-2.0-only. */
#include "libretro.h"
#include "arcdroid_core.h"
#include "arcdroid_engine.h"
#include "e_format_fixture.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ARCDROID_ADDRESS_SANITIZER 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(ARCDROID_ADDRESS_SANITIZER)
#define ARCDROID_ADDRESS_SANITIZER 1
#endif

enum Phase { PHASE_NONE, PHASE_ENV, PHASE_INIT, PHASE_LOAD, PHASE_RUN };
static enum Phase phase;
static const char *system_dir;
static const char *save_dir;
static unsigned open_files;
static unsigned open_dirs;
static unsigned video_frames;
static uint64_t audio_frames;
static uint64_t nonzero_audio_samples;
static uint64_t video_signatures[16];
static unsigned video_signature_count;
static uint64_t last_video_signature;
static unsigned video_signature_last_change_frame;
static bool last_video_was_duplicate;
static uint64_t video_trace_signature = UINT64_C(1469598103934665603);
static double reported_rates[16];
static unsigned reported_rate_count;
static unsigned geometry_changes;
static unsigned redundant_geometry_changes;
static unsigned geometry_width = 640u;
static unsigned geometry_height = 480u;
static double current_reported_rate = 50.0;
static double last_notified_rate = 50.0;
static bool measure_av;
static long double measured_expected_audio;
static uint64_t measured_actual_audio;
static unsigned measured_video_frames;
static bool av_rate_changed_during_measure;
static unsigned measured_oracle_mode;
static bool measured_oracle_valid;
static char last_message[512];
static unsigned vidc_osd_ext_calls;
static unsigned vidc_osd_legacy_calls;
static unsigned vidc_info_log_lines;
static unsigned vidc_warning_log_lines;
static struct retro_keyboard_callback keyboard;
static bool joypad_a;
static bool joypad_buttons[2][16];
static int16_t analog_state[2][2];
static int mouse_x, mouse_y;
static unsigned mouse_buttons;
static int mouse_port_x[2], mouse_port_y[2];
static unsigned mouse_port_buttons[2];
static bool pointer_pressed;
static int pointer_x, pointer_y;
static bool indexed_pointer_mode;
static unsigned pointer_count;
static bool pointer_contact_pressed[3];
static bool pointer_offscreen[3];
static int pointer_contact_x[3], pointer_contact_y[3];
static bool real_boot_mode;
static char evidence_path[4096];
static char pending_screenshot[256];
static FILE *evidence_log;
static unsigned fdc_command_count;
static unsigned fdc_read_count;
static unsigned kart_traffic_count;
static unsigned cmos_read_count;
static unsigned cmos_write_count;
static unsigned real_boot_frames = 3000u;
static struct retro_disk_control_ext_callback disk_control;
static bool can_dupe;
static bool keyboard_poll_capability;
static bool keyboard_levels[ARC_INPUT_KEY_COUNT];
static bool legacy_only_options;
static unsigned set_variable_calls;
static bool saw_input_descriptors;
static bool saw_elite_port1_descriptors;
static unsigned option_query_count;
static unsigned cmos_write_opens;
static unsigned adf_sector_reads;
static unsigned disc_load_count;
static unsigned av_notifications;
static unsigned joypad_port;
static const char *option_machine = "Auto";
static const char *option_cpu = "Auto";
static const char *option_memory = "Auto";
static const char *option_os = "Auto";
static const char *option_joystick = "Auto";
static const char *option_pointer = "relative";
static const char *option_pointer_stick = "right";
static const char *option_pointer_speed = "600";
static const char *option_pointer_deadzone = "15";
static const char *option_writes = "shadow";
static const char *option_launch = "auto";
static const char *option_input_profile = "Auto";
static const char *option_osk_toggle = "L3";
static const char *option_pad[2][10];
static bool variable_update;
static bool saw_adfs_advisory;
static bool saw_audio_counters;
static unsigned launch_options_seen;
static unsigned duplicate_video_frames;
static uint64_t sampled_underruns;
static uint64_t sampled_overruns;
static unsigned input_overflow_log_lines;
static unsigned short_warning_log_lines;
static unsigned short_warning_osd_calls;
static unsigned launch_log_lines;
static unsigned skipped_sibling_log_lines;
static unsigned skipped_sibling_names;
static unsigned launch_warning_osd_calls;
static unsigned flush_fault_log_lines;
static unsigned flush_fault_osd_calls;
static unsigned osk_failure_osd_calls;
static unsigned osk_narrow_osd_calls;
static unsigned profile_osd_calls;
static bool expect_test_profile;
static char application_profile_sha1[41];
static char playlist_profile_sha1[41];
static char last_launch_log[2048];
static char last_launch_warning[512];
static char last_flush_fault[512];
static char last_input_resolution[3072];

static bool signature_is_in(const uint64_t *signatures, unsigned count, uint64_t signature)
{
   for (unsigned i = 0u; i < count; ++i)
      if (signatures[i] == signature) return true;
   return false;
}

static void signature_add(uint64_t *signatures, unsigned *count, uint64_t signature)
{
   if (!signature_is_in(signatures, *count, signature) && *count < 16u)
      signatures[(*count)++] = signature;
}

static size_t heap_bytes_in_use(void)
{
#if defined(ARCDROID_ADDRESS_SANITIZER)
   /* ASan quarantine deliberately retains freed allocations. Its leak check
    * replaces allocator live-byte accounting in the instrumented build. */
   return 0u;
#elif defined(__APPLE__)
   malloc_statistics_t statistics;
   malloc_zone_statistics(malloc_default_zone(), &statistics);
   return statistics.size_in_use;
#elif defined(__GLIBC__)
   return (size_t)mallinfo2().uordblks;
#else
#error "The lifecycle allocation check needs platform malloc statistics"
#endif
}

struct retro_vfs_file_handle { FILE *file; char *path; unsigned mode; };
struct retro_vfs_dir_handle { DIR *dir; struct dirent *entry; char path[4096]; };
static const char *fault_open_substring;
static const char *fault_read_substring;
static const char *fault_write_substring;
static const char *fault_flush_substring;
static const char *fault_close_substring;
static const char *fault_rename_substring;
static const char *fault_opendir_substring;
static const char *fault_closedir_substring;
static char fault_rename_destination[4096];

static bool fault_once(const char *path, const char **substring)
{
   if (!*substring || !path || !strstr(path, *substring)) return false;
   *substring = NULL;
   return true;
}

static char *copy_string(const char *s)
{
   size_t n = strlen(s) + 1u;
   char *result = malloc(n);
   assert(result);
   memcpy(result, s, n);
   return result;
}
static const char *vfs_get_path(struct retro_vfs_file_handle *h) { return h->path; }
static struct retro_vfs_file_handle *vfs_open(const char *path, unsigned mode, unsigned hints)
{
   (void)hints;
   if (fault_once(path, &fault_open_substring)) return NULL;
   const char *m = (mode & RETRO_VFS_FILE_ACCESS_WRITE) ?
      ((mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) ? "rb+" : "wb") : "rb";
   FILE *f = fopen(path, m);
   if (!f) return NULL;
   struct retro_vfs_file_handle *h = calloc(1u, sizeof(*h));
   assert(h);
   h->file = f; h->path = copy_string(path); h->mode = mode; ++open_files;
   if ((mode & RETRO_VFS_FILE_ACCESS_WRITE) && strstr(path, "/arcdroid/cmos/"))
      ++cmos_write_opens;
   return h;
}
static int vfs_close(struct retro_vfs_file_handle *h)
{
   bool fail = fault_once(h->path, &fault_close_substring);
   int result = fclose(h->file); free(h->path); free(h); --open_files;
   return fail ? -1 : result;
}
static int64_t vfs_size(struct retro_vfs_file_handle *h)
{
   long at = ftell(h->file); assert(at >= 0); assert(fseek(h->file, 0, SEEK_END) == 0);
   long size = ftell(h->file); assert(size >= 0); assert(fseek(h->file, at, SEEK_SET) == 0); return size;
}
static int64_t vfs_tell(struct retro_vfs_file_handle *h) { return ftell(h->file); }
static int64_t vfs_seek(struct retro_vfs_file_handle *h, int64_t o, int origin) { return fseek(h->file, (long)o, origin) ? -1 : ftell(h->file); }
static int64_t vfs_read(struct retro_vfs_file_handle *h, void *s, uint64_t n)
{
   if (fault_once(h->path, &fault_read_substring) && n) --n;
   return (int64_t)fread(s, 1u, (size_t)n, h->file);
}
static int64_t vfs_write(struct retro_vfs_file_handle *h, const void *s, uint64_t n)
{
   if (fault_once(h->path, &fault_write_substring) && n) --n;
   return (int64_t)fwrite(s, 1u, (size_t)n, h->file);
}
static int vfs_flush(struct retro_vfs_file_handle *h)
{
   if (fault_once(h->path, &fault_flush_substring)) return -1;
   if (!(h->mode & RETRO_VFS_FILE_ACCESS_WRITE)) return -1;
   return fflush(h->file);
}
static int vfs_remove(const char *p) { return remove(p); }
static int vfs_rename(const char *a, const char *b)
{
   if (fault_once(b, &fault_rename_substring))
   {
      snprintf(fault_rename_destination, sizeof(fault_rename_destination), "%s", b);
      FILE *race = fopen(b, "wb");
      if (race) fclose(race);
      return -1;
   }
   return rename(a, b);
}
static int64_t vfs_truncate(struct retro_vfs_file_handle *h, int64_t n) { return ftruncate(fileno(h->file), n); }
static int32_t vfs_stat(const char *p, int32_t *size)
{
   struct stat st;
   if (stat(p, &st)) return 0;
   if (size) *size = st.st_size > INT32_MAX ? INT32_MAX : (int32_t)st.st_size;
   return RETRO_VFS_STAT_IS_VALID | (S_ISDIR(st.st_mode) ? RETRO_VFS_STAT_IS_DIRECTORY : 0);
}
static int vfs_mkdir(const char *p) { return mkdir(p, 0755); }
static struct retro_vfs_dir_handle *vfs_opendir(const char *p, bool hidden)
{
   (void)hidden;
   if (fault_once(p, &fault_opendir_substring)) return NULL;
   DIR *d = opendir(p); if (!d) return NULL;
   struct retro_vfs_dir_handle *h = calloc(1u, sizeof(*h)); assert(h);
   h->dir = d; snprintf(h->path, sizeof(h->path), "%s", p); ++open_dirs; return h;
}
static bool vfs_readdir(struct retro_vfs_dir_handle *h) { h->entry = readdir(h->dir); return h->entry != NULL; }
static const char *vfs_dirent_name(struct retro_vfs_dir_handle *h) { return h->entry ? h->entry->d_name : NULL; }
static bool vfs_dirent_is_dir(struct retro_vfs_dir_handle *h)
{
   char p[4096];
   struct stat st;
   snprintf(p, sizeof(p), "%s/%s", h->path, h->entry->d_name);
   return !stat(p, &st) && S_ISDIR(st.st_mode);
}
static int vfs_closedir(struct retro_vfs_dir_handle *h)
{
   bool fail = fault_once(h->path, &fault_closedir_substring);
   int result = closedir(h->dir);
   free(h);
   --open_dirs;
   return fail ? -1 : result;
}

static struct retro_vfs_interface vfs = {
   vfs_get_path, vfs_open, vfs_close, vfs_size, vfs_tell, vfs_seek, vfs_read,
   vfs_write, vfs_flush, vfs_remove, vfs_rename, vfs_truncate, vfs_stat,
   vfs_mkdir, vfs_opendir, vfs_readdir, vfs_dirent_name, vfs_dirent_is_dir,
   vfs_closedir, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void frontend_log(enum retro_log_level level, const char *format, ...)
{
   char message[2048];
   va_list arguments;
   (void)level;
   va_start(arguments, format);
   vsnprintf(message, sizeof(message), format, arguments);
   va_end(arguments);
   if (strstr(message, "VIDC"))
   {
      if (level <= RETRO_LOG_INFO) ++vidc_info_log_lines;
      else ++vidc_warning_log_lines;
   }
   if (strstr(message, "FDC command="))
   {
      ++fdc_command_count;
      if (strstr(message, "type=8")) ++fdc_read_count;
   }
   if (strstr(message, "ADFS Read sector")) ++adf_sector_reads;
   if (strstr(message, "disc_load 0 ")) ++disc_load_count;
   if (strstr(message, "KART ")) ++kart_traffic_count;
   if (strstr(message, "CMOS read ")) ++cmos_read_count;
   if (strstr(message, "CMOS write ")) ++cmos_write_count;
   if (strstr(message, "not a recognised ADFS image")) saw_adfs_advisory = true;
   if (strstr(message, "Input queue overflowed")) ++input_overflow_log_lines;
   if (!strncmp(message, "Input resolution:", 17u))
      snprintf(last_input_resolution, sizeof(last_input_resolution), "%s", message);
   if (strstr(message, "Short image padded with")) ++short_warning_log_lines;
   if (strstr(message, "Unable to flush the staged floppy image"))
   {
      ++flush_fault_log_lines;
      snprintf(last_flush_fault, sizeof(last_flush_fault), "%s", message);
   }
   if (!strncmp(message, "Launch:", 7u))
   {
      ++launch_log_lines;
      snprintf(last_launch_log, sizeof(last_launch_log), "%s", message);
   }
   if (!strncmp(message, "Launch sibling boot skipped:",
                sizeof("Launch sibling boot skipped:") - 1u))
   {
      ++skipped_sibling_log_lines;
      if (strstr(message, "leaf=!Bad|Name")) skipped_sibling_names |= 1u;
      if (strstr(message, "leaf=!Twin")) skipped_sibling_names |= 2u;
      if (strstr(message, "leaf=!tWIN")) skipped_sibling_names |= 4u;
   }
   if (strstr(message, "Launch options:"))
   {
      if (strstr(message, "pointer=relative")) launch_options_seen |= 1u;
      if (strstr(message, "pointer=disabled")) launch_options_seen |= 2u;
      if (strstr(message, "disk_writes=shadow")) launch_options_seen |= 4u;
      if (strstr(message, "disk_writes=original")) launch_options_seen |= 8u;
      if (strstr(message, "disk_writes=off")) launch_options_seen |= 16u;
   }
   unsigned long long underruns = 0u, overruns = 0u;
   if (sscanf(message, "Audio counters: underrun_frames=%llu overrun_frames=%llu",
              &underruns, &overruns) == 2)
   {
      saw_audio_counters = true;
   }
   if (evidence_log)
   {
      fputs(message, evidence_log);
      fflush(evidence_log);
   }
}

static bool environment(unsigned command, void *data)
{
   switch (command)
   {
      case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
         assert(phase == PHASE_ENV && !*(bool *)data); return true;
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      {
         assert(phase == PHASE_ENV);
         const struct retro_core_options_v2 *options = data;
         unsigned count = 0u;
         while (options->definitions[count].key) ++count;
         assert(count == 33u && options->categories &&
                !strcmp(options->categories[0].key, "pad_mapping") &&
                !strcmp(options->categories[0].desc, "Pad Mapping"));
         bool found_profile = false, found_osk = false;
         unsigned pads = 0u;
         const char *categorized_pad_labels[ARC_PAD_PORTS * ARC_PAD_INPUT_COUNT] = {0};
         for (unsigned n = 0u; n < count; ++n)
         {
            const struct retro_core_option_v2_definition *definition =
               &options->definitions[n];
            if (!strcmp(definition->key, "arcdroid_input_profile"))
            {
               found_profile = true;
               unsigned value_count = 0u;
               while (definition->values[value_count].value) ++value_count;
               assert(value_count == (expect_test_profile ? 4u : 3u) &&
                      !strcmp(definition->values[0].value, "Auto") &&
                      !strcmp(definition->values[1].value, "Generic") &&
                      !strcmp(definition->values[2].value, "elite") &&
                      !strcmp(definition->values[2].label,
                              "Elite (Hybrid Technology)"));
               if (expect_test_profile)
                  assert(!strcmp(definition->values[3].value, "test-profile") &&
                         !strcmp(definition->values[3].label,
                                 "Generated Test Profile"));
            }
            if (!strcmp(definition->key, "arcdroid_osk_toggle")) found_osk = true;
            if (!strncmp(definition->key, "arcdroid_pad", 12u))
            {
               assert(!strcmp(definition->category_key, "pad_mapping") &&
                      !strcmp(definition->values[0].value, "Auto") &&
                      !strcmp(definition->values[1].value, "Unbound") &&
                      definition->desc_categorized &&
                      !strcmp(definition->desc, definition->desc_categorized) &&
                      !strncmp(definition->desc_categorized, "Pad ", 4u));
               for (unsigned previous = 0u; previous < pads; ++previous)
                  assert(strcmp(categorized_pad_labels[previous],
                                definition->desc_categorized));
               categorized_pad_labels[pads++] = definition->desc_categorized;
            }
         }
         assert(found_profile && found_osk && pads == 20u);
         return true;
      }
      case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
         assert(phase == PHASE_INIT);
         disk_control = *(struct retro_disk_control_ext_callback *)data;
         return true;
      case RETRO_ENVIRONMENT_GET_CAN_DUPE:
         assert(phase == PHASE_INIT); *(bool *)data = can_dupe; return true;
      case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
         assert(phase == PHASE_INIT || phase == PHASE_LOAD);
         if (!keyboard_poll_capability) return false;
         *(uint64_t *)data = UINT64_C(1) << RETRO_DEVICE_KEYBOARD;
         return true;
      case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
         assert(phase == PHASE_INIT); ((struct retro_vfs_interface_info *)data)->iface = &vfs; return true;
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         assert(phase == PHASE_INIT);
         ((struct retro_log_callback *)data)->log = frontend_log;
         return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         assert(phase == PHASE_LOAD && *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888);
         geometry_width = 640u; geometry_height = 480u;
         current_reported_rate = last_notified_rate = 50.0;
         return true;
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
      {
         assert(phase == PHASE_LOAD || phase == PHASE_RUN);
         const struct retro_controller_info *ports = data;
         assert(ports[0].num_types == 2u && ports[1].num_types == 2u);
         assert(!strcmp(ports[0].types[0].desc, "RetroPad: keys and pointer"));
         return true;
      }
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      {
         assert(phase == PHASE_LOAD || phase == PHASE_RUN);
         const struct retro_input_descriptor *descriptors = data;
         static const unsigned profile_ids[ARC_PAD_INPUT_COUNT] = {
            RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN,
            RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
            RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B,
            RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y,
            RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT
         };
         static const char *profile_labels[ARC_PAD_INPUT_COUNT] = {
            "S", "X", ",", ".", "Space", "/", "A", "M", "F1", "T"
         };
         bool elite_port1[ARC_PAD_INPUT_COUNT] = {false};
         bool port0 = false, port1 = false, select = false, adjust = false;
         bool menu = false, precision = false, right_x = false, right_y = false;
         bool left_x = false, left_y = false;
         for (; descriptors->description; ++descriptors)
         {
            if (descriptors->port == 0u) port0 = true;
            if (descriptors->port == 1u) port1 = true;
            if (descriptors->port == 1u &&
                descriptors->device == RETRO_DEVICE_JOYPAD &&
                !strcmp(option_input_profile, "elite"))
               for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
                  if (descriptors->id == profile_ids[input] &&
                      !strcmp(descriptors->description, profile_labels[input]))
                     elite_port1[input] = true;
            if (descriptors->port == 0u && descriptors->device == RETRO_DEVICE_JOYPAD)
            {
               if (descriptors->id == RETRO_DEVICE_ID_JOYPAD_R) select = true;
               if (descriptors->id == RETRO_DEVICE_ID_JOYPAD_L) adjust = true;
               if (descriptors->id == RETRO_DEVICE_ID_JOYPAD_L2) menu = true;
               if (descriptors->id == RETRO_DEVICE_ID_JOYPAD_R2) precision = true;
            }
            if (descriptors->port == 0u && descriptors->device == RETRO_DEVICE_ANALOG &&
                descriptors->index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
            {
               if (descriptors->id == RETRO_DEVICE_ID_ANALOG_X) right_x = true;
               if (descriptors->id == RETRO_DEVICE_ID_ANALOG_Y) right_y = true;
            }
            if (descriptors->port == 0u && descriptors->device == RETRO_DEVICE_ANALOG &&
                descriptors->index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
            {
               bool labelled = strstr(descriptors->description,
                                      "Pointer Stick = left") != NULL;
               if (descriptors->id == RETRO_DEVICE_ID_ANALOG_X) left_x = labelled;
               if (descriptors->id == RETRO_DEVICE_ID_ANALOG_Y) left_y = labelled;
            }
         }
         saw_input_descriptors = port0 && port1 && select && adjust && menu &&
                                 precision && right_x && right_y && left_x && left_y;
         if (!strcmp(option_input_profile, "elite"))
         {
            saw_elite_port1_descriptors = true;
            for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
               saw_elite_port1_descriptors = saw_elite_port1_descriptors &&
                  elite_port1[input];
         }
         return true;
      }
      case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
         assert(phase == PHASE_LOAD); keyboard = *(struct retro_keyboard_callback *)data; return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = system_dir; return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = save_dir; return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *variable = data;
         ++option_query_count;
         if (legacy_only_options &&
             (!strcmp(variable->key, "arcdroid_input_profile") ||
              !strcmp(variable->key, "arcdroid_osk_toggle") ||
              !strncmp(variable->key, "arcdroid_pad", 12u)))
            return false;
         if (!strcmp(variable->key, "arcdroid_machine")) variable->value = option_machine;
         else if (!strcmp(variable->key, "arcdroid_cpu")) variable->value = option_cpu;
         else if (!strcmp(variable->key, "arcdroid_memory")) variable->value = option_memory;
         else if (!strcmp(variable->key, "arcdroid_os")) variable->value = option_os;
         else if (!strcmp(variable->key, "arcdroid_joystick_if")) variable->value = option_joystick;
         else if (!strcmp(variable->key, "arcdroid_pointer_mode")) variable->value = option_pointer;
         else if (!strcmp(variable->key, "arcdroid_pointer_stick")) variable->value = option_pointer_stick;
         else if (!strcmp(variable->key, "arcdroid_pointer_speed")) variable->value = option_pointer_speed;
         else if (!strcmp(variable->key, "arcdroid_pointer_deadzone")) variable->value = option_pointer_deadzone;
         else if (!strcmp(variable->key, "arcdroid_disk_writes")) variable->value = option_writes;
         else if (!strcmp(variable->key, "arcdroid_launch")) variable->value = option_launch;
         else if (!strcmp(variable->key, "arcdroid_input_profile")) variable->value = option_input_profile;
         else if (!strcmp(variable->key, "arcdroid_osk_toggle")) variable->value = option_osk_toggle;
         else
         {
            bool found = false;
            static const char *names[10] = {"up","down","left","right","a","b","x","y","start","select"};
            for (unsigned port = 0u; port < 2u; ++port)
               for (unsigned input_index = 0u; input_index < 10u; ++input_index)
               {
                  char key[32];
                  snprintf(key, sizeof(key), "arcdroid_pad%u_%s", port + 1u,
                           names[input_index]);
                  if (!strcmp(variable->key, key))
                  {
                     variable->value = option_pad[port][input_index] ?
                        option_pad[port][input_index] : "Auto";
                     found = true;
                  }
               }
            if (!found) return false;
         }
         return true;
      }
      case RETRO_ENVIRONMENT_SET_VARIABLE:
         ++set_variable_calls;
         return false;
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool *)data = variable_update;
         variable_update = false;
         return true;
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
      {
         const struct retro_game_geometry *geometry = data;
         assert(phase == PHASE_RUN);
         if (geometry->base_width == geometry_width &&
             geometry->base_height == geometry_height)
            ++redundant_geometry_changes;
         geometry_width = geometry->base_width;
         geometry_height = geometry->base_height;
         ++geometry_changes;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      {
         const struct retro_system_av_info *av = data;
         assert(phase == PHASE_RUN && av->timing.fps >= 20.0 && av->timing.fps <= 200.0);
         assert(av->timing.fps < last_notified_rate - 0.01 ||
                av->timing.fps > last_notified_rate + 0.01);
         if (measure_av) av_rate_changed_during_measure = true;
         current_reported_rate = last_notified_rate = av->timing.fps;
         ++av_notifications;
         bool seen = false;
         for (unsigned i = 0; i < reported_rate_count; ++i)
            if (reported_rates[i] > av->timing.fps - 0.01 &&
                reported_rates[i] < av->timing.fps + 0.01) seen = true;
         if (!seen && reported_rate_count < 16u)
            reported_rates[reported_rate_count++] = av->timing.fps;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
         snprintf(last_message, sizeof(last_message), "%s", ((struct retro_message_ext *)data)->msg);
         if (strstr(last_message, "VIDC")) ++vidc_osd_ext_calls;
         if (strstr(last_message, "Short image padded with")) ++short_warning_osd_calls;
         if (strstr(last_message, "Unable to flush the staged floppy image"))
         {
            ++flush_fault_osd_calls;
            snprintf(last_flush_fault, sizeof(last_flush_fault), "%s", last_message);
         }
         if (strstr(last_message, "On-screen keyboard disabled:")) ++osk_failure_osd_calls;
         if (strstr(last_message, "unavailable below 320")) ++osk_narrow_osd_calls;
         if (!strncmp(last_message, "Controls: ", 10u)) ++profile_osd_calls;
         if (strstr(last_message, "Application choice refused") ||
             strstr(last_message, "No unique application") ||
             strstr(last_message, "Application inspection incomplete"))
         {
            ++launch_warning_osd_calls;
            snprintf(last_launch_warning, sizeof(last_launch_warning), "%s", last_message);
         }
         return true;
      case RETRO_ENVIRONMENT_SET_MESSAGE:
         snprintf(last_message, sizeof(last_message), "%s", ((struct retro_message *)data)->msg);
         if (strstr(last_message, "VIDC")) ++vidc_osd_legacy_calls;
         if (strstr(last_message, "Short image padded with")) ++short_warning_osd_calls;
         if (strstr(last_message, "Unable to flush the staged floppy image"))
         {
            ++flush_fault_osd_calls;
            snprintf(last_flush_fault, sizeof(last_flush_fault), "%s", last_message);
         }
         if (!strncmp(last_message, "Controls: ", 10u)) ++profile_osd_calls;
         if (strstr(last_message, "Application choice refused") ||
             strstr(last_message, "No unique application") ||
             strstr(last_message, "Application inspection incomplete"))
         {
            ++launch_warning_osd_calls;
            snprintf(last_launch_warning, sizeof(last_launch_warning), "%s", last_message);
         }
         return true;
      default: return false;
   }
}
static void write_ppm(const char *path, const void *data, unsigned w,
                      unsigned h, size_t pitch)
{
   FILE *ppm = fopen(path, "wb");
   assert(ppm);
   fprintf(ppm, "P6\n%u %u\n255\n", w, h);
   const uint8_t *pixels = data;
   for (unsigned y = 0; y < h; ++y, pixels += pitch)
      for (unsigned x = 0; x < w; ++x)
      {
         uint32_t pixel;
         uint8_t rgb[3];
         memcpy(&pixel, pixels + x * 4u, sizeof(pixel));
         rgb[0] = (uint8_t)(pixel >> 16);
         rgb[1] = (uint8_t)(pixel >> 8);
         rgb[2] = (uint8_t)pixel;
         assert(fwrite(rgb, 1u, sizeof(rgb), ppm) == sizeof(rgb));
      }
   assert(!fclose(ppm));
}

static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
   assert((data || can_dupe) && w > 0u && w <= 4096u && h > 0u && h <= 2048u && pitch >= w * 4u);
   last_video_was_duplicate = data == NULL;
   if (!data) { ++duplicate_video_frames; ++video_frames; return; }
   assert(w == geometry_width && h == geometry_height);
   if (measure_av)
   {
      /* Independent test-ROM timing oracle. generate_fixtures.py programs a
       * 1024-pixel line at 16 MHz and alternates 312/400 total lines. The
       * woven renderer exposes those modes as 640x512 and 640x256. Never use
       * the core's SET_SYSTEM_AV_INFO value to compute the expected samples. */
      unsigned oracle_mode = h == 512u ? 1u : (h == 256u ? 2u : 0u);
      long double field_period = oracle_mode == 1u ? 0.019968L : 0.0256L;
      if (!oracle_mode || (measured_oracle_mode && measured_oracle_mode != oracle_mode))
         measured_oracle_valid = false;
      else
      {
         measured_oracle_mode = oracle_mode;
         measured_expected_audio += 48000.0L * field_period;
         ++measured_video_frames;
      }
   }
   uint64_t hash = UINT64_C(1469598103934665603);
   const uint8_t *row = data;
   for (unsigned y = 0; y < h; ++y, row += pitch)
      for (unsigned x = 0; x < w * 4u; ++x)
      {
         hash ^= row[x];
         hash *= UINT64_C(1099511628211);
      }
   bool seen = false;
   for (unsigned i = 0; i < video_signature_count; ++i)
      if (video_signatures[i] == hash) seen = true;
   if (!seen && video_signature_count < 16u)
      video_signatures[video_signature_count++] = hash;
   if (hash != last_video_signature)
      video_signature_last_change_frame = video_frames + 1u;
   last_video_signature = hash;
   video_trace_signature ^= hash;
   video_trace_signature *= UINT64_C(1099511628211);
   ++video_frames;
   if (pending_screenshot[0])
   {
      char path[4096];
      snprintf(path, sizeof(path), "%s/%s.ppm", evidence_path,
               pending_screenshot);
      write_ppm(path, data, w, h, pitch);
      pending_screenshot[0] = '\0';
   }
   if (real_boot_mode && (video_frames == 100u || video_frames == 300u ||
                          video_frames == 600u || video_frames == 1000u ||
                          video_frames == 1500u || video_frames == 2000u ||
                          video_frames == real_boot_frames))
   {
      char path[4096];
      snprintf(path, sizeof(path), "%s/field-%04u.ppm", evidence_path, video_frames);
      write_ppm(path, data, w, h, pitch);
   }
}
static size_t audio(const int16_t *data, size_t frames)
{
   assert(data && frames <= 2048u);
   for (size_t i = 0; i < frames * 2u; ++i)
      if (data[i]) ++nonzero_audio_samples;
   audio_frames += frames;
   if (measure_av) measured_actual_audio += frames;
   return frames;
}
static void poll(void) {}
static int16_t input(unsigned p, unsigned d, unsigned i, unsigned id)
{
   if (d == RETRO_DEVICE_KEYBOARD && id < ARC_INPUT_KEY_COUNT)
      return keyboard_levels[id];
   if (d == RETRO_DEVICE_JOYPAD && p < 2u && id < 16u)
      return joypad_buttons[p][id] ||
         (p == joypad_port && id == RETRO_DEVICE_ID_JOYPAD_A && joypad_a);
   if (d == RETRO_DEVICE_ANALOG && p < 2u && i < 2u && id < 2u)
      return analog_state[i][id];
   if (d == RETRO_DEVICE_MOUSE)
   {
      if (id == RETRO_DEVICE_ID_MOUSE_X) { int value = p < 2u ? mouse_port_x[p] : 0; if (p < 2u) mouse_port_x[p] = 0; if (!value) { value = mouse_x; mouse_x = 0; } return (int16_t)value; }
      if (id == RETRO_DEVICE_ID_MOUSE_Y) { int value = p < 2u ? mouse_port_y[p] : 0; if (p < 2u) mouse_port_y[p] = 0; if (!value) { value = mouse_y; mouse_y = 0; } return (int16_t)value; }
      unsigned buttons = (p < 2u ? mouse_port_buttons[p] : 0u) | mouse_buttons;
      if (id == RETRO_DEVICE_ID_MOUSE_LEFT) return (buttons & 1u) != 0u;
      if (id == RETRO_DEVICE_ID_MOUSE_RIGHT) return (buttons & 2u) != 0u;
      if (id == RETRO_DEVICE_ID_MOUSE_MIDDLE) return (buttons & 4u) != 0u;
   }
   if (d == RETRO_DEVICE_POINTER)
   {
      if (indexed_pointer_mode)
      {
         if (id == RETRO_DEVICE_ID_POINTER_COUNT) return (int16_t)pointer_count;
         if (i >= 3u) return 0;
         if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return pointer_contact_pressed[i];
         if (id == RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN) return pointer_offscreen[i];
         if (id == RETRO_DEVICE_ID_POINTER_X) return (int16_t)pointer_contact_x[i];
         if (id == RETRO_DEVICE_ID_POINTER_Y) return (int16_t)pointer_contact_y[i];
      }
      if (id == RETRO_DEVICE_ID_POINTER_COUNT) return 0;
      if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return pointer_pressed;
      if (id == RETRO_DEVICE_ID_POINTER_X) return (int16_t)pointer_x;
      if (id == RETRO_DEVICE_ID_POINTER_Y) return (int16_t)pointer_y;
   }
   return 0;
}

static void make_directory(const char *path) { if (mkdir(path, 0755) && errno != EEXIST) abort(); }
static void copy_file(const char *from, const char *to)
{
   FILE *a = fopen(from, "rb"), *b = fopen(to, "wb"); assert(a && b);
   uint8_t buffer[16384]; size_t n;
   while ((n = fread(buffer, 1u, sizeof(buffer), a))) assert(fwrite(buffer, 1u, n, b) == n);
   assert(!ferror(a)); assert(!fclose(a)); assert(!fclose(b));
}

static void read_exact(const char *path, void *data, size_t size)
{
   FILE *file = fopen(path, "rb"); assert(file);
   assert(fread(data, 1u, size, file) == size && fgetc(file) == EOF && !fclose(file));
}

static size_t read_bounded(const char *path, void *data, size_t capacity)
{
   FILE *file = fopen(path, "rb");
   assert(file);
   size_t size = fread(data, 1u, capacity, file);
   assert(!ferror(file) && fgetc(file) == EOF && !fclose(file));
   return size;
}

static void write_exact(const char *path, const void *data, size_t size)
{
   FILE *file = fopen(path, "wb");
   assert(file && fwrite(data, 1u, size, file) == size && !fclose(file));
}

static uint64_t hash_file(const char *path)
{
   FILE *f = fopen(path, "rb"); assert(f);
   uint64_t hash = UINT64_C(1469598103934665603);
   int byte;
   while ((byte = fgetc(f)) != EOF) { hash ^= (uint8_t)byte; hash *= UINT64_C(1099511628211); }
   assert(!ferror(f) && !fclose(f)); return hash;
}

static uint32_t hash_file_prefix(const char *path, size_t size)
{
   FILE *file = fopen(path, "rb");
   assert(file);
   uint32_t hash = UINT32_C(2166136261);
   for (size_t index = 0u; index < size; ++index)
   {
      int byte = fgetc(file);
      assert(byte != EOF);
      hash = (hash ^ (uint8_t)byte) * UINT32_C(16777619);
   }
   assert(!fclose(file));
   return hash;
}

static void make_sized_file(const char *path, size_t size)
{
   FILE *file = fopen(path, "wb");
   assert(file && size > 0u);
   assert(fseek(file, (long)(size - 1u), SEEK_SET) == 0);
   assert(fputc(0, file) != EOF && !fclose(file));
}

static void make_e_format_file(const char *path, bool corrupt_zone,
                               bool truncated_record)
{
   size_t size = ARC_TEST_E_FORMAT_SIZE;
   uint8_t *image = calloc(1u, size);
   assert(image);
   assert(arc_test_make_e_format(image, size));
   uint8_t *record = image + 4u;
   if (truncated_record) memset(record + 16u, 0, 44u);
   image[0] = arc_test_e_format_zone_check(image);
   if (corrupt_zone) image[0] ^= 1u;
   FILE *file = fopen(path, "wb");
   assert(file && fwrite(image, 1u, size, file) == size && !fclose(file));
   free(image);
}

static ArcDigest digest_file(const char *path)
{
   FILE *file = fopen(path, "rb");
   assert(file && fseek(file, 0, SEEK_END) == 0);
   long length = ftell(file);
   assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
   uint8_t *bytes = malloc((size_t)length);
   assert(bytes && fread(bytes, 1u, (size_t)length, file) == (size_t)length);
   assert(!fclose(file));
   ArcDigest digest = arc_digest(bytes, (size_t)length);
   free(bytes);
   return digest;
}

static unsigned digest_hex_nibble(char value)
{
   if (value >= '0' && value <= '9') return (unsigned)(value - '0');
   assert(value >= 'a' && value <= 'f');
   return (unsigned)(value - 'a' + 10);
}

static ArcDigest digest_playlist_pair(const char *first, const char *second)
{
   static const char domain[] = "arcdroid-playlist-v1";
   ArcDigest members[2] = {digest_file(first), digest_file(second)};
   uint8_t identity[sizeof(domain) - 1u + 4u + 40u];
   size_t at = 0u;
   memcpy(identity + at, domain, sizeof(domain) - 1u);
   at += sizeof(domain) - 1u;
   identity[at++] = 2u;
   identity[at++] = 0u;
   identity[at++] = 0u;
   identity[at++] = 0u;
   for (unsigned member = 0u; member < 2u; ++member)
      for (unsigned byte = 0u; byte < 20u; ++byte)
         identity[at++] = (uint8_t)((digest_hex_nibble(members[member].sha1[byte * 2u]) << 4u) |
                                    digest_hex_nibble(members[member].sha1[byte * 2u + 1u]));
   return arc_digest(identity, at);
}

static void staged_media_path(char output[4096], const char *save,
                              const char *source, const char *leaf)
{
   ArcDigest digest = digest_file(source);
   int written = snprintf(output, 4096u, "%s/arcdroid/media/%s/%s",
                          save, digest.sha1, leaf);
   assert(written > 0 && written < 4096);
}

static void remove_first_new_directory_entry(const char *path)
{
   FILE *file = fopen(path, "r+b");
   uint8_t entries[3u * 26u];
   assert(file && fseek(file, 1024 + 5, SEEK_SET) == 0);
   assert(fread(entries, 1u, sizeof(entries), file) == sizeof(entries));
   memmove(entries, entries + 26u, 52u);
   memset(entries + 52u, 0, 26u);
   assert(fseek(file, 1024 + 5, SEEK_SET) == 0);
   assert(fwrite(entries, 1u, sizeof(entries), file) == sizeof(entries));
   assert(!fflush(file) && !fclose(file));
}

static void assert_zero_padded_copy(const char *source, const char *staged,
                                    size_t declared)
{
   struct stat source_status, staged_status;
   assert(stat(source, &source_status) == 0 && stat(staged, &staged_status) == 0);
   assert(source_status.st_size >= 0 && staged_status.st_size == (off_t)declared &&
          source_status.st_size < staged_status.st_size);
   size_t source_size = (size_t)source_status.st_size;
   uint8_t *source_bytes = malloc(source_size);
   uint8_t *staged_bytes = malloc(declared);
   assert(source_bytes && staged_bytes);
   read_exact(source, source_bytes, source_size);
   read_exact(staged, staged_bytes, declared);
   assert(!memcmp(source_bytes, staged_bytes, source_size));
   for (size_t i = source_size; i < declared; ++i) assert(staged_bytes[i] == 0u);
   assert(hash_file(source) != hash_file(staged));
   free(source_bytes);
   free(staged_bytes);
}

static unsigned modified_shadow_count(const char *save, uint64_t first, uint64_t second)
{
   char root[4096], candidate[4096];
   unsigned count = 0u;
   snprintf(root, sizeof(root), "%s/arcdroid/media", save);
   DIR *directory = opendir(root);
   assert(directory);
   struct dirent *entry;
   while ((entry = readdir(directory)) != NULL)
   {
      if (entry->d_name[0] == '.') continue;
      for (unsigned kind = 0; kind < 2u; ++kind)
      {
         struct stat status;
         snprintf(candidate, sizeof(candidate), "%s/%s/disc.%s", root,
                  entry->d_name, kind ? "adl" : "adf");
         if (!stat(candidate, &status) && S_ISREG(status.st_mode))
         {
            uint64_t hash = hash_file(candidate);
            if (hash != first && hash != second) ++count;
         }
      }
   }
   assert(!closedir(directory));
   return count;
}

typedef struct {
   void (*run)(void);
   bool (*visible)(void);
   bool (*focus_key)(unsigned);
   bool (*activation_active)(void);
   unsigned (*modifier_state)(unsigned);
   void (*force_present)(void);
} OskScriptApi;

static void print_usage(FILE *stream, const char *program)
{
   fprintf(stream,
      "Usage:\n"
      "  %s CORE ROM CONTENT DATA\n"
      "  %s CORE ROM CONTENT DATA --newio-smoke PLAYLIST DISC1 DISC2 APP\n"
      "  %s CORE ROM CONTENT DATA --real-boot "
      "[--machine A3000|A3010|A3020|A4000] "
      "[--joystick Keyboard|A3010] [--osk-script FILE]\n"
      "  %s CORE ROM CONTENT DATA MEDIA_ROM PLAYLIST DISC1 DISC2 JFD JFD2 FIXTURES APP\n"
      "\n"
      "OSK script grammar (one command per line; # begins a comment):\n"
      "  wait N                              run N emulated fields\n"
      "  open | close                        set OSK visibility\n"
      "  tap RETROK_NAME                     tap a verified OSK key\n"
      "  modifier RETROK_NAME off|oneshot|locked\n"
      "                                      set a sticky modifier state\n"
      "  screenshot NAME                     capture the following presented field\n"
      "Screenshots are written as DATA/evidence/NAME.ppm. NAME may contain\n"
      "letters, digits, dot, underscore, or hyphen (but not . or ..).\n",
      program, program, program, program);
}

static bool script_key(const char *name, unsigned *key)
{
   static const struct { const char *name; unsigned key; } named[] = {
#define SCRIPT_KEY(name) {"RETROK_" #name, RETROK_##name}
      SCRIPT_KEY(BACKSPACE), SCRIPT_KEY(TAB), SCRIPT_KEY(RETURN),
      SCRIPT_KEY(PAUSE), SCRIPT_KEY(ESCAPE), SCRIPT_KEY(SPACE),
      SCRIPT_KEY(QUOTE), SCRIPT_KEY(COMMA), SCRIPT_KEY(MINUS),
      SCRIPT_KEY(PERIOD), SCRIPT_KEY(SLASH), SCRIPT_KEY(SEMICOLON),
      SCRIPT_KEY(EQUALS), SCRIPT_KEY(LEFTBRACKET), SCRIPT_KEY(BACKSLASH),
      SCRIPT_KEY(RIGHTBRACKET), SCRIPT_KEY(BACKQUOTE), SCRIPT_KEY(DELETE),
      SCRIPT_KEY(KP_PERIOD), SCRIPT_KEY(KP_DIVIDE),
      SCRIPT_KEY(KP_MULTIPLY), SCRIPT_KEY(KP_MINUS), SCRIPT_KEY(KP_PLUS),
      SCRIPT_KEY(KP_ENTER), SCRIPT_KEY(UP), SCRIPT_KEY(DOWN),
      SCRIPT_KEY(RIGHT), SCRIPT_KEY(LEFT), SCRIPT_KEY(INSERT),
      SCRIPT_KEY(HOME), SCRIPT_KEY(END), SCRIPT_KEY(PAGEUP),
      SCRIPT_KEY(PAGEDOWN), SCRIPT_KEY(NUMLOCK), SCRIPT_KEY(CAPSLOCK),
      SCRIPT_KEY(SCROLLOCK), SCRIPT_KEY(RSHIFT), SCRIPT_KEY(LSHIFT),
      SCRIPT_KEY(RCTRL), SCRIPT_KEY(LCTRL), SCRIPT_KEY(RALT),
      SCRIPT_KEY(LALT)
#undef SCRIPT_KEY
   };
   if (!name || !key || strncmp(name, "RETROK_", 7u)) return false;
   const char *suffix = name + 7u;
   if (suffix[0] && !suffix[1] && isalpha((unsigned char)suffix[0]))
   {
      *key = (unsigned)tolower((unsigned char)suffix[0]);
      return arc_input_key(*key) != NULL;
   }
   if (suffix[0] && !suffix[1] && isdigit((unsigned char)suffix[0]))
   {
      *key = (unsigned)suffix[0];
      return arc_input_key(*key) != NULL;
   }
   char *end = NULL;
   if (suffix[0] == 'F')
   {
      unsigned long number = strtoul(suffix + 1u, &end, 10);
      if (end && !*end && number >= 1u && number <= 12u)
      {
         *key = RETROK_F1 + (unsigned)number - 1u;
         return true;
      }
   }
   if (!strncmp(suffix, "KP", 2u) && suffix[2] && !suffix[3] &&
       isdigit((unsigned char)suffix[2]))
   {
      *key = RETROK_KP0 + (unsigned)(suffix[2] - '0');
      return true;
   }
   for (size_t i = 0u; i < sizeof(named) / sizeof(named[0]); ++i)
      if (!strcmp(name, named[i].name))
      {
         *key = named[i].key;
         return true;
      }
   return false;
}

static void script_button(OskScriptApi *api, unsigned button)
{
   joypad_buttons[0][button] = true;
   api->run();
   joypad_buttons[0][button] = false;
   api->run();
}

static bool script_name(const char *name)
{
   if (!name || !name[0] || strlen(name) >= sizeof(pending_screenshot) ||
       !strcmp(name, ".") || !strcmp(name, ".."))
      return false;
   for (const unsigned char *cursor = (const unsigned char *)name;
        *cursor; ++cursor)
      if (!isalnum(*cursor) && *cursor != '.' && *cursor != '_' &&
          *cursor != '-') return false;
   return true;
}

static bool run_osk_script(const char *path, OskScriptApi *api)
{
   FILE *script = fopen(path, "rb");
   if (!script)
   {
      fprintf(stderr, "libretro_harness: cannot open OSK script %s: %s\n",
              path, strerror(errno));
      return false;
   }
   char line[512];
   unsigned line_number = 0u;
   bool ok = true;
   while (ok && fgets(line, sizeof(line), script))
   {
      ++line_number;
      if (!strchr(line, '\n') && !feof(script))
      {
         fprintf(stderr, "libretro_harness: OSK script line %u is too long\n",
                 line_number);
         ok = false;
         break;
      }
      char *comment = strchr(line, '#');
      if (comment) *comment = '\0';
      char *save = NULL;
      char *command = strtok_r(line, " \t\r\n", &save);
      if (!command) continue;
      char *first = strtok_r(NULL, " \t\r\n", &save);
      char *second = strtok_r(NULL, " \t\r\n", &save);
      char *extra = strtok_r(NULL, " \t\r\n", &save);
      unsigned key = 0u;
      if (!strcmp(command, "wait") && first && !second)
      {
         char *end = NULL;
         unsigned long fields = strtoul(first, &end, 10);
         if (!first[0] || !end || *end || fields > 1000000u) ok = false;
         else for (unsigned long field = 0u; field < fields; ++field) api->run();
      }
      else if ((!strcmp(command, "open") || !strcmp(command, "close")) &&
               !first)
      {
         bool wanted = !strcmp(command, "open");
         if (api->visible() != wanted)
            script_button(api, RETRO_DEVICE_ID_JOYPAD_L3);
         ok = api->visible() == wanted;
      }
      else if (!strcmp(command, "tap") && first && !second &&
               script_key(first, &key) && api->visible() &&
               api->focus_key(key))
      {
         script_button(api, RETRO_DEVICE_ID_JOYPAD_A);
         unsigned fields = 0u;
         while (api->activation_active() && fields++ < 300u) api->run();
         ok = !api->activation_active();
      }
      else if (!strcmp(command, "modifier") && first && second && !extra &&
               script_key(first, &key) && arc_input_is_modifier(key) &&
               api->visible() && api->focus_key(key))
      {
         unsigned wanted = !strcmp(second, "off") ? ARC_OSK_MOD_OFF :
            !strcmp(second, "oneshot") ? ARC_OSK_MOD_ONESHOT :
            !strcmp(second, "locked") ? ARC_OSK_MOD_LOCKED : UINT_MAX;
         unsigned attempts = 0u;
         while (wanted != UINT_MAX && api->modifier_state(key) != wanted &&
                attempts++ < 3u)
            script_button(api, RETRO_DEVICE_ID_JOYPAD_A);
         ok = wanted != UINT_MAX && api->modifier_state(key) == wanted;
      }
      else if (!strcmp(command, "screenshot") && first && !second &&
               script_name(first))
      {
         snprintf(pending_screenshot, sizeof(pending_screenshot), "%s", first);
         api->force_present();
         api->run();
         ok = pending_screenshot[0] == '\0';
      }
      else
         ok = false;
      if (!ok)
         fprintf(stderr, "libretro_harness: invalid or failed OSK script line %u\n",
                 line_number);
   }
   if (ferror(script)) ok = false;
   if (fclose(script)) ok = false;
   return ok;
}

#define LOAD(name) do { *(void **)(&name) = dlsym(module, #name); assert(name); } while (0)
int main(int argc, char **argv)
{
   if (argc == 2 && !strcmp(argv[1], "--help"))
   {
      print_usage(stdout, argv[0]);
      return 0;
   }
   bool real_boot = false;
   bool newio_smoke = false;
   const char *osk_script = NULL;
   if (argc == 10 && !strcmp(argv[5], "--newio-smoke"))
      newio_smoke = true;
   else if (argc >= 6 && !strcmp(argv[5], "--real-boot"))
   {
      real_boot = true;
      bool machine_seen = false;
      bool joystick_seen = false;
      bool osk_script_seen = false;
      for (int argument = 6; argument < argc; ++argument)
      {
         if (!strcmp(argv[argument], "--machine") && !machine_seen &&
             argument + 1 < argc)
         {
            const char *machine = argv[++argument];
            if (strcmp(machine, "A3000") && strcmp(machine, "A3010") &&
                strcmp(machine, "A3020") && strcmp(machine, "A4000"))
            {
               print_usage(stderr, argv[0]);
               return 2;
            }
            option_machine = machine;
            machine_seen = true;
         }
         else if (!strcmp(argv[argument], "--joystick") && !joystick_seen &&
                  argument + 1 < argc)
         {
            const char *joystick = argv[++argument];
            if (strcmp(joystick, "Keyboard") && strcmp(joystick, "A3010"))
            {
               print_usage(stderr, argv[0]);
               return 2;
            }
            option_joystick = joystick;
            joystick_seen = true;
         }
         else if (!strcmp(argv[argument], "--osk-script") &&
                  !osk_script_seen && argument + 1 < argc)
         {
            osk_script = argv[++argument];
            osk_script_seen = true;
         }
         else
         {
            print_usage(stderr, argv[0]);
            return 2;
         }
      }
   }
   else if (argc != 5 && argc != 13)
   {
      print_usage(stderr, argv[0]);
      return 2;
   }
   bool media_tests = argc == 13;
   real_boot_mode = real_boot;
   void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!module)
   {
      fprintf(stderr, "libretro_harness: dlopen failed: %s\n", dlerror());
      return 2;
   }
   char system_path[4096], firmware_path[4096], save_path[4096], renamed[4096], cwd[4096], cwd_after[4096];
   snprintf(system_path, sizeof(system_path), "%s/system", argv[4]);
   snprintf(firmware_path, sizeof(firmware_path), "%s/arcdroid", system_path);
   snprintf(save_path, sizeof(save_path), "%s/save", argv[4]);
   make_directory(argv[4]); make_directory(system_path); make_directory(firmware_path);
   strncat(firmware_path, "/firmware", sizeof(firmware_path) - strlen(firmware_path) - 1u); make_directory(firmware_path);
   make_directory(save_path);
   if (real_boot)
   {
      snprintf(evidence_path, sizeof(evidence_path), "%s/evidence", argv[4]);
      make_directory(evidence_path);
      char log_path[4096];
      snprintf(log_path, sizeof(log_path), "%s/real-boot.log", evidence_path);
      evidence_log = fopen(log_path, "wb");
      assert(evidence_log);
   }
   snprintf(renamed, sizeof(renamed), "%s/renamed-firmware.data", firmware_path);
   char stale[4096]; snprintf(stale, sizeof(stale), "%s.hidden", renamed); (void)remove(stale);
   copy_file(argv[2], renamed);
   system_dir = system_path; save_dir = save_path;
   assert(getcwd(cwd, sizeof(cwd)));

   void (*retro_set_environment)(retro_environment_t); LOAD(retro_set_environment);
   void (*retro_set_video_refresh)(retro_video_refresh_t); LOAD(retro_set_video_refresh);
   void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t); LOAD(retro_set_audio_sample_batch);
   void (*retro_set_input_poll)(retro_input_poll_t); LOAD(retro_set_input_poll);
   void (*retro_set_input_state)(retro_input_state_t); LOAD(retro_set_input_state);
   void (*retro_init)(void); LOAD(retro_init);
   void (*retro_deinit)(void); LOAD(retro_deinit);
   bool (*retro_load_game)(const struct retro_game_info *); LOAD(retro_load_game);
   void (*retro_unload_game)(void); LOAD(retro_unload_game);
   void (*retro_reset)(void); LOAD(retro_reset);
   void (*retro_run)(void); LOAD(retro_run);
   void (*retro_get_system_av_info)(struct retro_system_av_info *); LOAD(retro_get_system_av_info);
   void (*retro_set_controller_port_device)(unsigned, unsigned); LOAD(retro_set_controller_port_device);
   size_t (*retro_serialize_size)(void); LOAD(retro_serialize_size);
   void *(*retro_get_memory_data)(unsigned); LOAD(retro_get_memory_data);
   size_t (*retro_get_memory_size)(unsigned); LOAD(retro_get_memory_size);
   void (*arc_engine_audio_stats)(uint64_t *, uint64_t *) = NULL;
   bool (*arc_engine_media_insert)(const ArcEngineMedia *, char *, size_t) = NULL;
   void (*arc_engine_test_set_vidc_noncompleting)(bool) = NULL;
   bool (*arc_engine_test_drive_media)(unsigned, bool *, char[41]) = NULL;
   bool (*arc_engine_test_cmos_image)(uint8_t[ARC_CMOS_MACHINE_SIZE]) = NULL;
   bool (*arc_engine_test_cmos_write)(unsigned, uint8_t) = NULL;
   bool (*arc_engine_test_newio)(unsigned *, unsigned *, unsigned *, unsigned *, uint8_t *) = NULL;
   bool (*arc_engine_test_arm2_swp_undefined)(void) = NULL;
   bool (*arc_engine_test_newio_dir)(bool, uint8_t *) = NULL;
   bool (*arc_engine_test_newio_read_start)(unsigned, uint8_t) = NULL;
   bool (*arc_engine_test_newio_result)(uint8_t[7], bool *) = NULL;
   bool (*arc_engine_test_newio_read_finish)(size_t, uint32_t *, uint8_t[7]) = NULL;
   bool (*arc_engine_test_joystick)(unsigned, uint8_t *) = NULL;
   bool (*arc_engine_test_ioeb_clock)(unsigned) = NULL;
   unsigned (*arc_engine_test_vidc_clock)(void) = NULL;
   void (*arc_libretro_test_fail_osk_allocations)(unsigned) = NULL;
   void (*arc_libretro_test_drop_osk_surface)(void) = NULL;
   bool (*arc_libretro_test_osk_source_unchanged)(void) = NULL;
   bool (*arc_libretro_test_register_input_profile)(const ArcInputProfile *) = NULL;
   void (*arc_libretro_test_probe_osk_geometry)(unsigned, unsigned) = NULL;
   bool (*arc_libretro_test_osk_focus_key)(unsigned) = NULL;
   bool (*arc_libretro_test_osk_key_center)(unsigned, unsigned *, unsigned *) = NULL;
   void (*arc_libretro_test_stall_kart)(bool) = NULL;
   void (*arc_libretro_test_fill_input_queue)(void) = NULL;
   unsigned (*arc_libretro_test_input_pending)(void) = NULL;
   bool (*arc_libretro_test_osk_visible)(void) = NULL;
   bool (*arc_libretro_test_osk_busy)(void) = NULL;
   bool (*arc_libretro_test_osk_activation_active)(void) = NULL;
   unsigned (*arc_libretro_test_osk_modifier_state)(unsigned) = NULL;
   void (*arc_libretro_test_force_present)(void) = NULL;
   unsigned (*arc_libretro_test_presented_osk_notice)(void) = NULL;
   void (*arc_libretro_test_force_input_reconcile)(void) = NULL;
   void (*arc_platform_test_input_events_reset)(void) = NULL;
   size_t (*arc_platform_test_input_events)(ArcKartEvent *, size_t) = NULL;
   if (real_boot || newio_smoke)
      *(void **)(&arc_engine_test_newio) = dlsym(module, "arc_engine_test_newio");
   if (!real_boot && !newio_smoke)
   {
      *(void **)(&arc_engine_audio_stats) = dlsym(module, "arc_engine_audio_stats");
      *(void **)(&arc_engine_media_insert) = dlsym(module, "arc_engine_media_insert");
      *(void **)(&arc_engine_test_set_vidc_noncompleting) =
         dlsym(module, "arc_engine_test_set_vidc_noncompleting");
      *(void **)(&arc_engine_test_drive_media) =
         dlsym(module, "arc_engine_test_drive_media");
      *(void **)(&arc_engine_test_cmos_image) =
         dlsym(module, "arc_engine_test_cmos_image");
      *(void **)(&arc_engine_test_cmos_write) =
         dlsym(module, "arc_engine_test_cmos_write");
      *(void **)(&arc_libretro_test_fail_osk_allocations) =
         dlsym(module, "arc_libretro_test_fail_osk_allocations");
      *(void **)(&arc_libretro_test_drop_osk_surface) =
         dlsym(module, "arc_libretro_test_drop_osk_surface");
      *(void **)(&arc_libretro_test_osk_source_unchanged) =
         dlsym(module, "arc_libretro_test_osk_source_unchanged");
      *(void **)(&arc_libretro_test_register_input_profile) =
         dlsym(module, "arc_libretro_test_register_input_profile");
      *(void **)(&arc_libretro_test_probe_osk_geometry) =
         dlsym(module, "arc_libretro_test_probe_osk_geometry");
      *(void **)(&arc_libretro_test_osk_focus_key) =
         dlsym(module, "arc_libretro_test_osk_focus_key");
      *(void **)(&arc_libretro_test_osk_key_center) =
         dlsym(module, "arc_libretro_test_osk_key_center");
      *(void **)(&arc_libretro_test_stall_kart) =
         dlsym(module, "arc_libretro_test_stall_kart");
      *(void **)(&arc_libretro_test_fill_input_queue) =
         dlsym(module, "arc_libretro_test_fill_input_queue");
      *(void **)(&arc_libretro_test_input_pending) =
         dlsym(module, "arc_libretro_test_input_pending");
      *(void **)(&arc_libretro_test_osk_visible) =
         dlsym(module, "arc_libretro_test_osk_visible");
      *(void **)(&arc_libretro_test_osk_busy) =
         dlsym(module, "arc_libretro_test_osk_busy");
      *(void **)(&arc_libretro_test_osk_activation_active) =
         dlsym(module, "arc_libretro_test_osk_activation_active");
      *(void **)(&arc_libretro_test_osk_modifier_state) =
         dlsym(module, "arc_libretro_test_osk_modifier_state");
      *(void **)(&arc_libretro_test_force_present) =
         dlsym(module, "arc_libretro_test_force_present");
      *(void **)(&arc_libretro_test_presented_osk_notice) =
         dlsym(module, "arc_libretro_test_presented_osk_notice");
      *(void **)(&arc_libretro_test_force_input_reconcile) =
         dlsym(module, "arc_libretro_test_force_input_reconcile");
      *(void **)(&arc_platform_test_input_events_reset) =
         dlsym(module, "arc_platform_test_input_events_reset");
      *(void **)(&arc_platform_test_input_events) =
         dlsym(module, "arc_platform_test_input_events");
      assert(arc_engine_audio_stats && arc_engine_media_insert &&
             arc_engine_test_set_vidc_noncompleting &&
             arc_engine_test_drive_media && arc_engine_test_cmos_image &&
             arc_engine_test_cmos_write && arc_libretro_test_fail_osk_allocations &&
             arc_libretro_test_drop_osk_surface &&
             arc_libretro_test_osk_source_unchanged &&
             arc_libretro_test_register_input_profile &&
             arc_libretro_test_probe_osk_geometry &&
             arc_libretro_test_osk_focus_key &&
             arc_libretro_test_osk_key_center &&
             arc_libretro_test_stall_kart &&
             arc_libretro_test_fill_input_queue &&
             arc_libretro_test_input_pending &&
             arc_libretro_test_osk_visible && arc_libretro_test_osk_busy &&
             arc_libretro_test_osk_activation_active &&
             arc_libretro_test_osk_modifier_state &&
             arc_libretro_test_force_present &&
             arc_libretro_test_presented_osk_notice &&
             arc_libretro_test_force_input_reconcile &&
             arc_platform_test_input_events_reset && arc_platform_test_input_events);
      ArcDigest application_profile_digest = digest_file(argv[12]);
      ArcDigest playlist_profile_digest = digest_playlist_pair(argv[7], argv[8]);
      memcpy(application_profile_sha1, application_profile_digest.sha1,
             sizeof(application_profile_sha1));
      memcpy(playlist_profile_sha1, playlist_profile_digest.sha1,
             sizeof(playlist_profile_sha1));
      ArcInputProfile test_profile = {
         .id = "test-profile",
         .name = "Generated Test Profile",
         .digests = {application_profile_sha1, playlist_profile_sha1},
         .digest_count = 2u,
         .has_port1 = true,
         .evidence = "project-authored harness registration"
      };
      for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
         memcpy(test_profile.mapping[port], arc_input_generic_mapping(),
                sizeof(test_profile.mapping[port]));
      assert(arc_libretro_test_register_input_profile(&test_profile));
      expect_test_profile = true;
   }
   else if (newio_smoke)
   {
      *(void **)(&arc_engine_test_cmos_image) =
         dlsym(module, "arc_engine_test_cmos_image");
      *(void **)(&arc_engine_test_cmos_write) =
         dlsym(module, "arc_engine_test_cmos_write");
      *(void **)(&arc_engine_test_drive_media) =
         dlsym(module, "arc_engine_test_drive_media");
      *(void **)(&arc_engine_test_newio_dir) =
         dlsym(module, "arc_engine_test_newio_dir");
      *(void **)(&arc_engine_test_newio_read_start) =
         dlsym(module, "arc_engine_test_newio_read_start");
      *(void **)(&arc_engine_test_newio_result) =
         dlsym(module, "arc_engine_test_newio_result");
      *(void **)(&arc_engine_test_newio_read_finish) =
         dlsym(module, "arc_engine_test_newio_read_finish");
      *(void **)(&arc_engine_test_arm2_swp_undefined) =
         dlsym(module, "arc_engine_test_arm2_swp_undefined");
      *(void **)(&arc_engine_test_joystick) = dlsym(module, "arc_engine_test_joystick");
      *(void **)(&arc_engine_test_ioeb_clock) = dlsym(module, "arc_engine_test_ioeb_clock");
      *(void **)(&arc_engine_test_vidc_clock) = dlsym(module, "arc_engine_test_vidc_clock");
      assert(arc_engine_test_cmos_image && arc_engine_test_cmos_write &&
             arc_engine_test_newio && arc_engine_test_drive_media &&
             arc_engine_test_newio_dir && arc_engine_test_newio_read_start &&
             arc_engine_test_newio_result && arc_engine_test_newio_read_finish &&
             arc_engine_test_arm2_swp_undefined &&
             arc_engine_test_joystick &&
             arc_engine_test_ioeb_clock && arc_engine_test_vidc_clock);
   }
   else if (osk_script)
   {
      *(void **)(&arc_libretro_test_osk_focus_key) =
         dlsym(module, "arc_libretro_test_osk_focus_key");
      *(void **)(&arc_libretro_test_osk_visible) =
         dlsym(module, "arc_libretro_test_osk_visible");
      *(void **)(&arc_libretro_test_osk_activation_active) =
         dlsym(module, "arc_libretro_test_osk_activation_active");
      *(void **)(&arc_libretro_test_osk_modifier_state) =
         dlsym(module, "arc_libretro_test_osk_modifier_state");
      *(void **)(&arc_libretro_test_force_present) =
         dlsym(module, "arc_libretro_test_force_present");
      if (!arc_libretro_test_osk_focus_key || !arc_libretro_test_osk_visible ||
          !arc_libretro_test_osk_activation_active ||
          !arc_libretro_test_osk_modifier_state ||
          !arc_libretro_test_force_present)
      {
         fprintf(stderr, "libretro_harness: --osk-script requires the test core\n");
         assert(dlclose(module) == 0);
         return 2;
      }
   }

   phase = PHASE_ENV; retro_set_environment(environment);
   retro_set_video_refresh(video); retro_set_audio_sample_batch(audio);
   retro_set_input_poll(poll); retro_set_input_state(input);
   phase = PHASE_INIT; retro_init();
   assert(disk_control.set_eject_state && disk_control.set_image_index &&
          disk_control.replace_image_index && disk_control.set_initial_image);
   assert(retro_serialize_size() == 0u);
   struct retro_game_info game = {argv[3], NULL, 0u, NULL};
   uint64_t source_hash = hash_file(argv[3]);

   if (newio_smoke)
   {
      static const char *const machines[] = {"A3010", "A3020", "A4000"};
      static const char *const cycle_machines[] = {
         "A3000", "A3010", "A3020", "A4000"
      };
      ArcDigest source_digest = digest_file(argv[3]);
      char machine_cmos_path[4][4096];
      for (unsigned index = 0u; index < 4u; ++index)
      {
         snprintf(machine_cmos_path[index], sizeof(machine_cmos_path[index]),
                  "%s/arcdroid/cmos/%s.test.%s.bin", save_path,
                  cycle_machines[index], source_digest.sha1);
         (void)remove(machine_cmos_path[index]);
      }

      /* Option and firmware diagnostics are resolved before touching a live machine. */
      option_machine = "A3010";
      option_cpu = "ARM2";
      phase = PHASE_LOAD;
      assert(!retro_load_game(&game) && strstr(last_message, "ARM2 is incompatible with the A3010"));
      option_cpu = "ARM250";
      option_os = "RISC OS 3.00";
      assert(!retro_load_game(&game) && strstr(last_message, "RISC OS 3.00 is not supported on the A3010"));
      option_os = "RISC OS 3.11";
      assert(!retro_load_game(&game) && strstr(last_message, "Missing RISC OS 3.11 firmware"));
      option_os = "Auto";
      char newio_hidden[4096];
      snprintf(newio_hidden, sizeof(newio_hidden), "%s/newio-firmware.hidden", argv[4]);
      assert(rename(renamed, newio_hidden) == 0);
      assert(!retro_load_game(&game) &&
             strstr(last_message, "A3010 requires RISC OS 3.10 or later firmware; found none"));
      assert(rename(newio_hidden, renamed) == 0);

      /* A rejected runtime change leaves the A3000 session running; a valid
       * A3010 change then recreates it and executes the New-IO branch. */
      option_machine = "A3000";
      option_cpu = "ARM2";
      option_joystick = "Keyboard";
      assert(retro_load_game(&game));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 60u; ++frame) retro_run();
      assert(arc_engine_test_arm2_swp_undefined());
      assert(arc_engine_test_cmos_write(216u, 0xa0u));
      retro_reset();
      retro_run();
      uint64_t live_audio = audio_frames;
      assert(rename(renamed, newio_hidden) == 0);
      retro_reset();
      assert(strstr(last_message,
                    "Missing RISC OS 3.11 firmware in <system>/arcdroid/firmware."));
      retro_run();
      assert(audio_frames > live_audio && rename(newio_hidden, renamed) == 0);
      live_audio = audio_frames;
      option_machine = "A3010";
      option_cpu = "ARM2";
      retro_reset();
      assert(strstr(last_message, "ARM2 is incompatible with the A3010"));
      retro_run();
      assert(audio_frames > live_audio);
      option_cpu = "ARM250";
      option_joystick = "A3010";
      retro_reset();
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      unsigned transition_commands = 0u, transition_reads = 0u;
      unsigned transition_success = 0u, transition_ide = 0u;
      uint8_t transition_id = 0u;
      assert(arc_engine_test_newio(&transition_commands, &transition_reads,
                                   &transition_success, &transition_ide,
                                   &transition_id));
      assert(transition_id == 0x05u && transition_success >= 1u);

      /* Reset-time firmware selection uses the same machine-specific wording
       * as initial load and rejects before disturbing the live session. */
      live_audio = audio_frames;
      assert(rename(renamed, newio_hidden) == 0);
      retro_reset();
      assert(strstr(last_message,
                    "A3010 requires RISC OS 3.10 or later firmware; found none"));
      retro_run();
      assert(audio_frames > live_audio &&
             arc_engine_test_newio(NULL, NULL, NULL, NULL, &transition_id) &&
             transition_id == 0x05u && rename(newio_hidden, renamed) == 0);

      /* Joystick/machine compatibility is validated before teardown. Keep
       * the A3010 machine, mounted media, and active control routing intact. */
      unsigned preserved_count = disk_control.get_num_images();
      unsigned preserved_index = disk_control.get_image_index();
      bool preserved_ejected = disk_control.get_eject_state();
      option_machine = "A3020";
      option_cpu = "ARM250";
      retro_reset();
      assert(strstr(last_message,
                    "A3010 joystick interface is incompatible with the A3020 machine"));
      assert(disk_control.get_num_images() == preserved_count &&
             disk_control.get_image_index() == preserved_index &&
             disk_control.get_eject_state() == preserved_ejected &&
             arc_engine_test_newio(NULL, NULL, NULL, NULL, &transition_id) &&
             transition_id == 0x05u);
      retro_set_controller_port_device(0u, RETRO_DEVICE_JOYPAD);
      retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
      retro_run();
      uint8_t preserved_joystick = 0u;
      assert(arc_engine_test_joystick(0u, &preserved_joystick) &&
             preserved_joystick == 0x6fu);
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
      retro_run();
      option_machine = "A3010";

      uint8_t a3000_record[ARC_CMOS_RECORD_SIZE];
      read_exact(machine_cmos_path[0], a3000_record, sizeof(a3000_record));
      assert(a3000_record[216] == 0xa0u);

      for (unsigned index = 0u; index < 3u; ++index)
      {
         option_machine = machines[index];
         option_cpu = "ARM250";
         option_joystick = index == 0u ? "A3010" : "Keyboard";
         retro_set_controller_port_device(0u, RETRO_DEVICE_JOYPAD);
         retro_set_controller_port_device(1u, RETRO_DEVICE_JOYPAD);
         if (index)
         {
            phase = PHASE_LOAD;
            assert(retro_load_game(&game));
         }
         assert(arc_engine_test_cmos_write(216u, (uint8_t)(0xa1u + index)));
         unsigned video_before = video_frames;
         uint64_t audio_before = audio_frames;
         phase = PHASE_RUN;
         for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
         unsigned commands = 0u, reads = 0u, read_success = 0u, ide_accesses = 0u;
         uint8_t device_id = 0u;
         assert(arc_engine_test_newio(&commands, &reads, &read_success,
                                      &ide_accesses, &device_id));
         assert(device_id == 0x05u && commands >= 5u && reads >= 1u &&
                read_success >= 1u && ide_accesses >= 18u);
         assert(video_frames > video_before && audio_frames > audio_before);
         if (index == 0u)
         {
            struct retro_system_av_info baseline, clocked;
            retro_get_system_av_info(&baseline);
            assert(arc_engine_test_ioeb_clock(1u));
            uint64_t clock_audio = audio_frames;
            for (unsigned frame = 0u; frame < 12u; ++frame) retro_run();
            retro_get_system_av_info(&clocked);
            double clock1_rate = clocked.timing.fps;
            assert(arc_engine_test_vidc_clock() == 25175u &&
                   clocked.timing.fps != baseline.timing.fps &&
                   audio_frames > clock_audio);
            assert(arc_engine_test_ioeb_clock(2u));
            for (unsigned frame = 0u; frame < 12u; ++frame) retro_run();
            retro_get_system_av_info(&clocked);
            assert(arc_engine_test_vidc_clock() == 36000u &&
                   clocked.timing.fps != baseline.timing.fps &&
                   clocked.timing.fps != clock1_rate);
            assert(arc_engine_test_ioeb_clock(0u));
            bool restored = false;
            for (unsigned frame = 0u; frame < 60u && !restored; ++frame)
            {
               retro_run();
               retro_get_system_av_info(&clocked);
               double delta = clocked.timing.fps - baseline.timing.fps;
               restored = delta > -0.01 && delta < 0.01;
            }
            assert(arc_engine_test_vidc_clock() == 24000u && restored);
            uint8_t joystick = 0u;
            retro_run(); /* release the controller-device neutral gate */
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_UP] = true;
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
            joypad_buttons[1][RETRO_DEVICE_ID_JOYPAD_RIGHT] = true;
            retro_run();
            assert(arc_engine_test_joystick(0u, &joystick) && joystick == 0x6eu);
            assert(arc_engine_test_joystick(1u, &joystick) && joystick == 0x77u);
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_UP] = false;
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
            joypad_buttons[1][RETRO_DEVICE_ID_JOYPAD_RIGHT] = false;
            retro_run();
            assert(arc_engine_test_joystick(0u, &joystick) && joystick == 0x7fu);
            assert(arc_engine_test_joystick(1u, &joystick) && joystick == 0x7fu);

            /* Explicit overrides suppress only their joystick action. */
            option_pad[0][ARC_PAD_UP] = "Unbound";
            variable_update = true;
            retro_run();
            retro_run(); /* release the mapping-change neutral gate */
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_UP] = true;
            retro_run();
            assert(arc_engine_test_joystick(0u, &joystick) && joystick == 0x7fu);
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_UP] = false;
            option_pad[0][ARC_PAD_UP] = NULL;
            variable_update = true;
            retro_run();

            /* The modal OSK neutralises port 0 while leaving its own controls live. */
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = true;
            retro_run();
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = false;
            retro_run();
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_UP] = true;
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
            retro_run();
            assert(arc_engine_test_joystick(0u, &joystick) && joystick == 0x7fu);
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_UP] = false;
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = true;
            retro_run();
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = false;
            retro_run();

            /* Device disconnect and machine reset both release held levels. */
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
            retro_run();
            assert(arc_engine_test_joystick(0u, &joystick) && joystick == 0x6fu);
            retro_set_controller_port_device(0u, RETRO_DEVICE_NONE);
            retro_run();
            assert(arc_engine_test_joystick(0u, &joystick) && joystick == 0x7fu);
            retro_set_controller_port_device(0u, RETRO_DEVICE_JOYPAD);
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
            retro_run();
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
            retro_run();
            retro_reset();
            assert(arc_engine_test_joystick(0u, &joystick) && joystick == 0x7fu);
            joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
         }
         retro_unload_game();
         assert(open_files == 0u && open_dirs == 0u);
         uint8_t machine_record[ARC_CMOS_RECORD_SIZE];
         read_exact(machine_cmos_path[index + 1u], machine_record,
                    sizeof(machine_record));
         assert(machine_record[216] == (uint8_t)(0xa1u + index));
      }

      /* Complete the machine cycle and prove that New-IO persistence never
       * rewrites or substitutes the established A3000 record. */
      option_machine = "A3000";
      option_cpu = "ARM2";
      option_joystick = "Keyboard";
      phase = PHASE_LOAD;
      assert(retro_load_game(&game));
      uint8_t restored_a3000[ARC_CMOS_MACHINE_SIZE];
      assert(arc_engine_test_cmos_image(restored_a3000) &&
             restored_a3000[216] == 0xa0u);
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 60u; ++frame) retro_run();
      assert(arc_engine_test_arm2_swp_undefined());
      retro_unload_game();
      uint8_t final_a3000_record[ARC_CMOS_RECORD_SIZE];
      read_exact(machine_cmos_path[0], final_a3000_record,
                 sizeof(final_a3000_record));
      assert(!memcmp(a3000_record, final_a3000_record,
                     sizeof(a3000_record)));

      /* Exercise New-IO media replacement through libretro disk control, not
       * controller-only fixture plumbing. The inserted playlist member raises
       * DIR until the first real track-changing seek. */
      option_machine = "A3010";
      option_cpu = "ARM250";
      option_joystick = "Keyboard";
      option_launch = "off";
      struct retro_game_info newio_playlist = {argv[6], NULL, 0u, NULL};
      phase = PHASE_LOAD;
      assert(retro_load_game(&newio_playlist));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      uint8_t dir = 0u;
      assert(disk_control.get_num_images() == 2u &&
             disk_control.get_image_index() == 0u);
      assert(arc_engine_test_newio_dir(true, &dir) && !(dir & 0x80u));
      assert(disk_control.set_eject_state(true));
      assert(arc_engine_test_newio_dir(false, &dir) && (dir & 0x80u));
      assert(disk_control.set_image_index(1u));
      assert(disk_control.set_eject_state(false));
      assert(arc_engine_test_newio_dir(false, &dir) && (dir & 0x80u));
      assert(arc_engine_test_newio_dir(true, &dir) && !(dir & 0x80u));

      /* An active read is cancelled once. Reinsertion before result
       * consumption preserves that result, after which the newly selected
       * member can be read successfully by the controller. */
      assert(arc_engine_test_newio_read_start(0u, 3u));
      assert(disk_control.set_eject_state(true));
      assert(disk_control.set_eject_state(false));
      uint8_t media_result[7];
      bool stale_fiq = true;
      assert(arc_engine_test_newio_result(media_result, &stale_fiq) &&
             (media_result[0] & 0x40u) && media_result[1] == 0x04u &&
             !stale_fiq);
      assert(arc_engine_test_newio_read_start(0u, 3u));
      uint32_t guest_disc_signature = 0u;
      assert(arc_engine_test_newio_read_finish(1024u, &guest_disc_signature,
                                               media_result) &&
             !(media_result[0] & 0xc0u) &&
             guest_disc_signature == hash_file_prefix(argv[8], 1024u));
      retro_unload_game();

      /* The launch helper occupies drive 1. Ejecting frontend drive 0 while
       * its read is active must leave the helper transfer undisturbed. */
      option_launch = "auto";
      struct retro_game_info newio_application = {argv[9], NULL, 0u, NULL};
      phase = PHASE_LOAD;
      assert(retro_load_game(&newio_application));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      assert(arc_engine_test_drive_media(1u, NULL, NULL));
      assert(arc_engine_test_newio_read_start(1u, 1u));
      assert(disk_control.set_eject_state(true));
      uint32_t helper_signature = 0u;
      assert(arc_engine_test_newio_read_finish(256u, &helper_signature,
                                               media_result) &&
             !(media_result[0] & 0xc0u));
      retro_unload_game();
      option_launch = "auto";

      retro_deinit();
      assert(hash_file(argv[3]) == source_hash);
      assert(dlclose(module) == 0);
      printf("newio_libretro_harness: A3010/A3020/A4000 executed 82C711 guest "
             "path; inserted disc signature %08x\n", guest_disc_signature);
      return 0;
   }

   if (real_boot)
   {
      phase = PHASE_LOAD;
      if (!retro_load_game(&game))
      {
         fprintf(stderr, "real_boot_harness: load failed: %s\n", last_message);
         return 2;
      }
      char firmware_message[sizeof(last_message)];
      snprintf(firmware_message, sizeof(firmware_message), "%s", last_message);
      phase = PHASE_RUN;
      bool script_ok = true;
      if (osk_script)
      {
         OskScriptApi script_api = {
            retro_run, arc_libretro_test_osk_visible,
            arc_libretro_test_osk_focus_key,
            arc_libretro_test_osk_activation_active,
            arc_libretro_test_osk_modifier_state,
            arc_libretro_test_force_present
         };
         script_ok = run_osk_script(osk_script, &script_api);
      }
      else
         for (unsigned frame = 0; frame < real_boot_frames; ++frame) retro_run();
      unsigned newio_commands = 0u;
      unsigned newio_reads = 0u;
      unsigned newio_successes = 0u;
      unsigned newio_ide_accesses = 0u;
      uint8_t newio_id = 0u;
      bool newio_evidence = false;
      bool newio_machine = !strcmp(option_machine, "A3010") ||
         !strcmp(option_machine, "A3020") || !strcmp(option_machine, "A4000");
      if (newio_machine && arc_engine_test_newio)
      {
         newio_evidence = arc_engine_test_newio(
            &newio_commands, &newio_reads, &newio_successes,
            &newio_ide_accesses, &newio_id);
         assert(newio_evidence);
      }
      retro_unload_game();
      retro_deinit();
      assert(!fclose(evidence_log));
      evidence_log = NULL;
      assert(open_files == 0u && open_dirs == 0u && hash_file(argv[3]) == source_hash);
      if (!script_ok)
      {
         assert(dlclose(module) == 0);
         return 2;
      }
      assert((osk_script ? video_frames > 0u : video_frames == real_boot_frames) &&
             audio_frames > 0u && video_signature_count > 0u);
      assert(vidc_osd_ext_calls == 0u && vidc_osd_legacy_calls == 0u);
      assert(dlclose(module) == 0);
      printf("real_boot_harness: %s; %u frames, %u signatures, trace %016llx, "
             "final %016llx, "
             "%llu audio frames, %llu nonzero samples; FDC commands %u (reads %u), "
             "KART traffic %u, CMOS reads %u writes %u; "
             "VIDC OSD ext/legacy %u/%u, transient logs %u; boot-settle frame %u",
             firmware_message, video_frames, video_signature_count,
             (unsigned long long)video_trace_signature,
             (unsigned long long)last_video_signature,
             (unsigned long long)audio_frames,
             (unsigned long long)nonzero_audio_samples,
             fdc_command_count, fdc_read_count, kart_traffic_count,
             cmos_read_count, cmos_write_count, vidc_osd_ext_calls,
             vidc_osd_legacy_calls, vidc_info_log_lines,
             video_signature_last_change_frame);
      if (newio_evidence)
         printf("; IOEB id 0x%02x; FDC commands %u reads %u successes %u; "
                "IDE accesses %u", newio_id, newio_commands, newio_reads,
                newio_successes, newio_ide_accesses);
      printf("; evidence %s\n", evidence_path);
      return 0;
   }

   char hidden[4096]; snprintf(hidden, sizeof(hidden), "%s/firmware.hidden", argv[4]);
   assert(rename(renamed, hidden) == 0);
   phase = PHASE_LOAD; assert(!retro_load_game(&game)); assert(strstr(last_message, "Missing RISC OS 3.11"));
   option_os = "RISC OS 3.10";
   phase = PHASE_LOAD; assert(!retro_load_game(&game)); assert(strstr(last_message, "Missing RISC OS 3.10"));
   option_os = "Auto";
   const size_t diagnostic_sizes[] = {524289u, 2097151u, 2097153u};
   const size_t expected_sizes[] = {524288u, 2097152u, 2097152u};
   char oversized[4096];
   snprintf(oversized, sizeof(oversized), "%s/firmware-size-check", firmware_path);
   for (unsigned index = 0u; index < 3u; ++index)
   {
      make_sized_file(oversized, diagnostic_sizes[index]);
      phase = PHASE_LOAD; assert(!retro_load_game(&game));
      char actual[32], expected[32];
      snprintf(actual, sizeof(actual), "%zu", diagnostic_sizes[index]);
      snprintf(expected, sizeof(expected), "%zu", expected_sizes[index]);
      assert(strstr(last_message, actual) && strstr(last_message, expected));
      assert(remove(oversized) == 0);
   }
   assert(rename(hidden, renamed) == 0);

   /* A legacy per-content option file has no values for the 22 Slice 3a
    * keys. Missing GET_VARIABLE entries must select defaults and still load. */
   legacy_only_options = true;
   last_input_resolution[0] = '\0';
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   assert(strstr(last_input_resolution, "profile=Generic") &&
          strstr(last_input_resolution, "p1.Up=Cursor Up[generic]") &&
          strstr(last_input_resolution, "p2.Select=Tab[generic]"));
   retro_unload_game();
   legacy_only_options = false;

   char corrupt[4096]; snprintf(corrupt, sizeof(corrupt), "%s/corrupt.adf", argv[4]);
   FILE *bad = fopen(corrupt, "wb"); assert(bad); assert(fwrite("bad", 1u, 3u, bad) == 3u); assert(!fclose(bad));
   struct retro_game_info bad_game = {corrupt, NULL, 0u, NULL};
   phase = PHASE_LOAD; assert(!retro_load_game(&bad_game)); assert(strstr(last_message, "geometry"));

   char zero_adl[4096]; snprintf(zero_adl, sizeof(zero_adl), "%s/zero-filled.adl", argv[4]);
   make_sized_file(zero_adl, 819200u);
   struct retro_game_info zero_game = {zero_adl, NULL, 0u, NULL};
   saw_adfs_advisory = false;
   phase = PHASE_LOAD; assert(retro_load_game(&zero_game));
   assert(saw_adfs_advisory);
   retro_unload_game();

   char e_format[4096], e_bad_zone[4096], e_short_record[4096];
   snprintf(e_format, sizeof(e_format), "%s/e-format.adf", argv[4]);
   snprintf(e_bad_zone, sizeof(e_bad_zone), "%s/e-format-bad-zone.adf", argv[4]);
   snprintf(e_short_record, sizeof(e_short_record), "%s/e-format-short-record.adf", argv[4]);
   make_e_format_file(e_format, false, false);
   make_e_format_file(e_bad_zone, true, false);
   make_e_format_file(e_short_record, false, true);
   struct retro_game_info e_game = {e_format, NULL, 0u, NULL};
   struct retro_game_info e_bad_game = {e_bad_zone, NULL, 0u, NULL};
   struct retro_game_info e_short_game = {e_short_record, NULL, 0u, NULL};
   saw_adfs_advisory = false;
   phase = PHASE_LOAD; assert(retro_load_game(&e_game));
   assert(!saw_adfs_advisory);
   retro_unload_game();
   saw_adfs_advisory = false;
   phase = PHASE_LOAD; assert(retro_load_game(&e_bad_game));
   assert(saw_adfs_advisory);
   retro_unload_game();
   saw_adfs_advisory = false;
   phase = PHASE_LOAD; assert(retro_load_game(&e_short_game));
   assert(saw_adfs_advisory);
   retro_unload_game();

   char high_adl[4096]; snprintf(high_adl, sizeof(high_adl), "%s/high-density.adl", argv[4]);
   make_sized_file(high_adl, 1638400u);
   struct retro_game_info high_game = {high_adl, NULL, 0u, NULL};
   phase = PHASE_LOAD; assert(!retro_load_game(&high_game));
   assert(strstr(last_message, "unsupported by the Slice 1"));

   option_joystick = "A3010";
   phase = PHASE_LOAD; assert(!retro_load_game(&game));
   assert(strstr(last_message, "A3010 joystick interface"));
   option_joystick = "Keyboard";

   /* The first New-IO creation faults before c82c711_fdc_init(); unwinding
    * must treat the never-initialised controller as inert. */
   option_machine = "A3010"; option_cpu = "ARM250";
   assert(setenv("ARCDROID_TEST_FAIL_VIDEO_ALLOC", "1", 1) == 0);
   phase = PHASE_LOAD; assert(!retro_load_game(&game));
   assert(strstr(last_message, "video surface") && open_files == 0u && open_dirs == 0u);
   assert(unsetenv("ARCDROID_TEST_FAIL_VIDEO_ALLOC") == 0);
   option_machine = "A3000"; option_cpu = "ARM2"; option_memory = "4 MB";

   /* The ARMv2 validation ROM reads sector zero through WD1772 and derives
    * its pixels from that payload. A one-byte disc change must reach the guest. */
   char changed[4096]; snprintf(changed, sizeof(changed), "%s/changed.adf", argv[4]);
   copy_file(argv[3], changed);
   FILE *changed_file = fopen(changed, "rb+"); assert(changed_file);
   assert(fseek(changed_file, 64, SEEK_SET) == 0);
   int changed_byte = fgetc(changed_file); assert(changed_byte != EOF);
   assert(fseek(changed_file, 64, SEEK_SET) == 0);
   assert(fputc(changed_byte ^ 0x5a, changed_file) != EOF && !fclose(changed_file));
   uint64_t changed_source_hash = hash_file(changed);
   struct retro_game_info changed_game = {changed, NULL, 0u, NULL};
   unsigned profile_messages_before = profile_osd_calls;
   option_input_profile = "test-profile";
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   assert(profile_osd_calls == profile_messages_before + 1u &&
          strstr(last_input_resolution, "profile=Generated Test Profile") &&
          strstr(last_input_resolution, "[preset]"));
   option_input_profile = "Auto";
   variable_update = true;
   assert(saw_input_descriptors && option_query_count >= 10u);
   struct retro_system_av_info loaded_av;
   retro_get_system_av_info(&loaded_av);
   assert(loaded_av.geometry.base_width > 0u && loaded_av.geometry.base_height > 0u &&
          loaded_av.timing.fps >= 20.0 && loaded_av.timing.fps <= 200.0);
   retro_set_controller_port_device(0u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1));
   retro_set_controller_port_device(1u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1));

   /* Exercise the engine's defensive insert-over-insert path directly. A live
    * drive handle must be replaced, not leaked alongside the new image. */
   unsigned handles_before_direct_insert = open_files;
   unsigned loads_before_direct_insert = disc_load_count;
   char insert_error[256] = {0};
   ArcEngineMedia direct_media = {
      argv[3], false, ARC_MEDIA_ADF_800K, ARC_GEOMETRY_800K_E, NULL
   };
   assert(arc_engine_media_insert(&direct_media, insert_error, sizeof(insert_error)));
   assert(open_files == handles_before_direct_insert);
   assert(disc_load_count == loads_before_direct_insert + 1u);

   unsigned reads_before_eject = adf_sector_reads;
   assert(disk_control.set_eject_state(true));
   phase = PHASE_RUN; for (unsigned frame = 0; frame < 30u; ++frame) retro_run();
   assert(adf_sector_reads == reads_before_eject);
   assert(!disk_control.replace_image_index(0u, &bad_game));
   assert(disk_control.set_image_index(1u));
   assert(disk_control.set_eject_state(false));
   for (unsigned frame = 0; frame < 10u; ++frame) retro_run();
   assert(adf_sector_reads == reads_before_eject);
   assert(disk_control.set_eject_state(true));
   assert(disk_control.set_image_index(0u));
   assert(disk_control.replace_image_index(0u, &game));
   unsigned loads_before_insert = disc_load_count;
   assert(disk_control.set_eject_state(false));
   assert(disc_load_count == loads_before_insert + 1u);
   phase = PHASE_RUN; retro_reset();
   for (unsigned frame = 0; frame < 60u; ++frame) retro_run();
   assert(adf_sector_reads > reads_before_eject);

   phase = PHASE_RUN; for (unsigned frame = 0; frame < 60u; ++frame) retro_run();
   ArcKartEvent input_events[512];

   /* Profiles without an explicit port-1 map mirror port 0. This covers
    * handhelds whose built-in controls are split across frontend ports. */
   retro_set_controller_port_device(1u,
      RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1));
   retro_run();
   option_input_profile = "elite";
   variable_update = true;
   retro_run();
   assert(saw_elite_port1_descriptors &&
          strstr(last_input_resolution, "profile=Elite (Hybrid Technology)") &&
          strstr(last_input_resolution, "p2.Start=F1[preset]"));
   arc_platform_test_input_events_reset();
   joypad_buttons[1][RETRO_DEVICE_ID_JOYPAD_START] = true;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   joypad_buttons[1][RETRO_DEVICE_ID_JOYPAD_START] = false;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   size_t mirrored_input_event_count =
      arc_platform_test_input_events(input_events, 512u);
   unsigned mirrored_f1_edges = 0u, generic_escape_edges = 0u;
   for (size_t i = 0u; i < mirrored_input_event_count; ++i)
      if (input_events[i].type == ARC_INPUT_EVENT_KEY)
      {
         if (input_events[i].code == RETROK_F1)
         {
            assert(input_events[i].down == (mirrored_f1_edges == 0u));
            ++mirrored_f1_edges;
         }
         if (input_events[i].code == RETROK_ESCAPE) ++generic_escape_edges;
      }
   assert(mirrored_f1_edges == 2u && generic_escape_edges == 0u);
   option_input_profile = "Auto";
   variable_update = true;
   retro_run();
   retro_set_controller_port_device(1u,
      RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1));

   /* The generated ROM's multiply/XOR receive fold is order-sensitive. Equal
    * event sets in modifier-first and key-first order paint distinct frames. */
   retro_reset();
   for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
   keyboard.callback(true, RETROK_LSHIFT, 0u, 0u);
   keyboard.callback(true, RETROK_a, 0u, 0u);
   keyboard.callback(false, RETROK_a, 0u, 0u);
   keyboard.callback(false, RETROK_LSHIFT, 0u, 0u);
   for (unsigned frame = 0u; frame < 100u; ++frame) retro_run();
   uint64_t modifier_first_signature = last_video_signature;
   retro_reset();
   for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
   keyboard.callback(true, RETROK_a, 0u, 0u);
   keyboard.callback(true, RETROK_LSHIFT, 0u, 0u);
   keyboard.callback(false, RETROK_LSHIFT, 0u, 0u);
   keyboard.callback(false, RETROK_a, 0u, 0u);
   for (unsigned frame = 0u; frame < 100u; ++frame) retro_run();
   uint64_t key_first_signature = last_video_signature;
   assert(modifier_first_signature != key_first_signature);

   /* Exercise the same order-sensitive guest path through the OSK. A sticky
    * Shift chord followed by a plain key has the same KART event multiset as
    * the reversed activation order, but must paint a different guest result. */
   retro_reset();
   for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = false; retro_run();
   assert(arc_libretro_test_osk_focus_key(RETROK_LSHIFT));
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
   assert(arc_libretro_test_osk_focus_key(RETROK_a));
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
   for (unsigned frame = 0u;
        frame < 120u && arc_platform_test_input_events(input_events, 512u) < 4u;
        ++frame) retro_run();
   assert(arc_libretro_test_osk_focus_key(RETROK_a));
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
   for (unsigned frame = 0u;
        frame < 120u && arc_platform_test_input_events(input_events, 512u) < 6u;
        ++frame) retro_run();
   size_t osk_modifier_first_count =
      arc_platform_test_input_events(input_events, 512u);
   assert(osk_modifier_first_count == 6u);
   assert(input_events[0].code == RETROK_LSHIFT && input_events[0].down &&
          input_events[1].code == RETROK_a && input_events[1].down &&
          input_events[2].code == RETROK_a && !input_events[2].down &&
          input_events[3].code == RETROK_LSHIFT && !input_events[3].down &&
          input_events[4].code == RETROK_a && input_events[4].down &&
          input_events[5].code == RETROK_a && !input_events[5].down);
   for (unsigned edge = 1u; edge < 4u; ++edge)
      assert(input_events[edge].guest_us - input_events[edge - 1u].guest_us >=
             UINT64_C(60000));
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_B] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_B] = false;
   for (unsigned frame = 0u; frame < 100u; ++frame) retro_run();
   uint64_t osk_modifier_first_signature = last_video_signature;

   retro_reset();
   for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = false; retro_run();
   assert(arc_libretro_test_osk_focus_key(RETROK_a));
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
   for (unsigned frame = 0u;
        frame < 120u && arc_platform_test_input_events(input_events, 512u) < 2u;
        ++frame) retro_run();
   for (unsigned frame = 0u; frame < 4u; ++frame) retro_run();
   assert(arc_libretro_test_osk_focus_key(RETROK_LSHIFT));
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
   assert(arc_libretro_test_osk_focus_key(RETROK_a));
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
   for (unsigned frame = 0u;
        frame < 120u && arc_platform_test_input_events(input_events, 512u) < 6u;
        ++frame) retro_run();
   size_t osk_plain_first_count = arc_platform_test_input_events(input_events, 512u);
   assert(osk_plain_first_count == 6u);
   assert(input_events[0].code == RETROK_a && input_events[0].down &&
          input_events[1].code == RETROK_a && !input_events[1].down &&
          input_events[2].code == RETROK_LSHIFT && input_events[2].down &&
          input_events[3].code == RETROK_a && input_events[3].down &&
          input_events[4].code == RETROK_a && !input_events[4].down &&
          input_events[5].code == RETROK_LSHIFT && !input_events[5].down);
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_B] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_B] = false;
   for (unsigned frame = 0u; frame < 100u; ++frame) retro_run();
   uint64_t osk_plain_first_signature = last_video_signature;
   assert(osk_modifier_first_signature != osk_plain_first_signature);

   /* Keyboard callbacks may contain a complete transition pair between two
    * frontend polls. Delivery is acknowledged by the real KART handshake. */
   arc_platform_test_input_events_reset();
   keyboard.callback(true, RETROK_a, 0u, 0u);
   keyboard.callback(true, RETROK_a, 0u, 0u);
   keyboard.callback(true, RETROK_UNKNOWN, 'x', 0u);
   keyboard.callback(false, RETROK_a, 0u, 0u);
   for (unsigned frame = 0u;
        frame < 80u && arc_platform_test_input_events(input_events, 512u) < 2u;
        ++frame) retro_run();
   size_t input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 2u);
   assert(input_events[0].type == ARC_INPUT_EVENT_KEY &&
          input_events[0].code == RETROK_a && input_events[0].down);
   assert(input_events[1].type == ARC_INPUT_EVENT_KEY &&
          input_events[1].code == RETROK_a && !input_events[1].down);
   assert(input_events[1].guest_us >= input_events[0].guest_us);

   arc_platform_test_input_events_reset();
   keyboard.callback(true, RETROK_MENU, 0u, 0u);
   keyboard.callback(true, RETROK_LMETA, 0u, 0u);
   keyboard.callback(false, RETROK_MENU, 0u, 0u);
   keyboard.callback(false, RETROK_LMETA, 0u, 0u);
   for (unsigned frame = 0u;
        frame < 80u && arc_platform_test_input_events(input_events, 512u) < 2u;
        ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 2u && input_events[0].type == ARC_INPUT_EVENT_BUTTON &&
          input_events[0].code == 4u && input_events[0].down &&
          input_events[1].code == 4u && !input_events[1].down);

   /* Simultaneous sampled changes press modifiers first and release them last. */
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_X] = true;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_X] = false;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   for (unsigned frame = 0u;
        frame < 120u && arc_platform_test_input_events(input_events, 512u) < 4u;
        ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count >= 4u);
   assert(input_events[0].code == RETROK_LSHIFT && input_events[0].down);
   assert(input_events[1].code == RETROK_SPACE && input_events[1].down);
   assert(input_events[2].code == RETROK_SPACE && !input_events[2].down);
   assert(input_events[3].code == RETROK_LSHIFT && !input_events[3].down);

   /* Modifier ordering is global across both sampled RetroPad sources. */
   retro_set_controller_port_device(1u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1));
   retro_run(); /* Device changes require one observed neutral sample. */
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
   joypad_buttons[1][RETRO_DEVICE_ID_JOYPAD_X] = true;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   joypad_buttons[1][RETRO_DEVICE_ID_JOYPAD_X] = false;
   for (unsigned frame = 0u; frame < 80u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 4u &&
          input_events[0].code == RETROK_LSHIFT && input_events[0].down &&
          input_events[1].code == RETROK_SPACE && input_events[1].down &&
          input_events[2].code == RETROK_SPACE && !input_events[2].down &&
          input_events[3].code == RETROK_LSHIFT && !input_events[3].down);
   retro_set_controller_port_device(1u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1));

   /* Per-input values round-trip by key. Two pad inputs mapped to one key
    * aggregate into one effective hold, and live remapping uses a neutral gate. */
   option_pad[0][ARC_PAD_B] = "space";
   variable_update = true;
   retro_run();
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_B] = true;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_B] = false;
   for (unsigned frame = 0u; frame < 80u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 2u && input_events[0].code == RETROK_SPACE &&
          input_events[0].down && input_events[1].code == RETROK_SPACE &&
          !input_events[1].down);

   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
   retro_run();
   option_pad[0][ARC_PAD_A] = "return";
   variable_update = true;
   retro_run();
   for (unsigned frame = 0u; frame < 40u; ++frame) retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   for (unsigned frame = 0u; frame < 80u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   unsigned return_edges = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
      if (input_events[i].code == RETROK_RETURN) ++return_edges;
   assert(input_event_count == 4u && input_events[0].code == RETROK_SPACE &&
          input_events[0].down && input_events[1].code == RETROK_SPACE &&
          !input_events[1].down && return_edges == 2u);
   option_pad[0][ARC_PAD_A] = NULL;
   option_pad[0][ARC_PAD_B] = NULL;
   variable_update = true;
   retro_run();

   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = true;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L] = true;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L2] = true;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = false;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L] = false;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L2] = false;
   for (unsigned frame = 0u; frame < 80u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 6u);
   for (unsigned i = 0u; i < 3u; ++i)
   {
      assert(input_events[i].type == ARC_INPUT_EVENT_BUTTON && input_events[i].down &&
             input_events[i].code == (1u << i));
      assert(input_events[i + 3u].type == ARC_INPUT_EVENT_BUTTON &&
             !input_events[i + 3u].down && input_events[i + 3u].code == (1u << i));
   }

   /* Pad pointer buttons hold through motion; full stick motion is bounded and
    * stops at release with no more than its one queued velocity packet. */
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = true;
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 32767;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   size_t events_before_stick_release = arc_platform_test_input_events(input_events, 512u);
   unsigned motion_before_stick_release = 0u;
   for (size_t i = 0u; i < events_before_stick_release; ++i)
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION)
         ++motion_before_stick_release;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = false;
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 0;
   for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   unsigned select_down = 0u, select_up = 0u, motion_packets = 0u;
   int64_t stick_total_x = 0;
   uint64_t stick_first_us = 0u, stick_last_us = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type == ARC_INPUT_EVENT_BUTTON && input_events[i].code == 1u)
         input_events[i].down ? ++select_down : ++select_up;
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION)
      {
         assert(input_events[i].dx >= -63 && input_events[i].dx <= 63);
         if (!stick_first_us) stick_first_us = input_events[i].guest_us;
         stick_last_us = input_events[i].guest_us;
         stick_total_x += input_events[i].dx;
         ++motion_packets;
      }
   }
   assert(select_down == 1u && select_up == 1u && motion_packets > 0u && stick_total_x > 0 &&
          motion_packets <= motion_before_stick_release + 1u);

   /* R2 is a digital precision modifier. The same full-deflection sampling
    * window produces one quarter of the normal guest-time velocity (allowing
    * one mickey of fractional end-point rounding). */
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R2] = true;
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 32767;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 0;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R2] = false;
   for (unsigned frame = 0u; frame < 60u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   int64_t precision_total_x = 0;
   uint64_t precision_first_us = 0u, precision_last_us = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION)
      {
         if (!precision_first_us) precision_first_us = input_events[i].guest_us;
         precision_last_us = input_events[i].guest_us;
         precision_total_x += input_events[i].dx;
      }
   assert(precision_total_x > 0 && stick_last_us > stick_first_us &&
          precision_last_us > precision_first_us);
   long double rate_ratio =
      (long double)stick_total_x * (precision_last_us - precision_first_us) /
      ((long double)precision_total_x * (stick_last_us - stick_first_us));
   assert(rate_ratio > 3.5L && rate_ratio < 4.5L);

   /* The dead zone is radial, slow post-dead-zone motion retains fractions,
    * response is monotonic, and a saturated diagonal remains normalised. */
   arc_platform_test_input_events_reset();
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 4000;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 0;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   assert(arc_platform_test_input_events(input_events, 512u) == 0u);

   int64_t response_totals[3] = {0, 0, 0};
   uint64_t response_first_us[3] = {0u, 0u, 0u};
   uint64_t response_last_us[3] = {0u, 0u, 0u};
   bool saw_short_field = false, saw_long_field = false;
   const int16_t response_samples[3] = {9000, 20000, 32767};
   for (unsigned sample = 0u; sample < 3u; ++sample)
   {
      arc_platform_test_input_events_reset();
      analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] =
         response_samples[sample];
      for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
      analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 0;
      for (unsigned frame = 0u; frame < 30u; ++frame) retro_run();
      input_event_count = arc_platform_test_input_events(input_events, 512u);
      for (size_t i = 0u; i < input_event_count; ++i)
         if (input_events[i].type == ARC_INPUT_EVENT_MOTION)
         {
            if (!response_first_us[sample])
               response_first_us[sample] = input_events[i].guest_us;
            if (sample == 2u && response_last_us[sample])
            {
               uint64_t delta = input_events[i].guest_us -
                                response_last_us[sample];
               if (delta <= UINT64_C(20000)) saw_short_field = true;
               if (delta >= UINT64_C(25000)) saw_long_field = true;
            }
            response_last_us[sample] = input_events[i].guest_us;
            response_totals[sample] += input_events[i].dx;
         }
   }
   assert(response_totals[0] > 0 && response_totals[0] < response_totals[1] &&
          response_totals[1] < response_totals[2]);

   arc_platform_test_input_events_reset();
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 32767;
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_Y] = 32767;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 0;
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_Y] = 0;
   for (unsigned frame = 0u; frame < 40u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   int64_t diagonal_x = 0, diagonal_y = 0;
   uint64_t diagonal_first_us = 0u, diagonal_last_us = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION)
      {
         if (!diagonal_first_us) diagonal_first_us = input_events[i].guest_us;
         diagonal_last_us = input_events[i].guest_us;
         diagonal_x += input_events[i].dx;
         diagonal_y += input_events[i].dy;
         if (i > 0u)
         {
            uint64_t delta = input_events[i].guest_us - input_events[i - 1u].guest_us;
            if (delta <= UINT64_C(20000)) saw_short_field = true;
            if (delta >= UINT64_C(25000)) saw_long_field = true;
         }
   }
   assert(response_last_us[2] > response_first_us[2] &&
          diagonal_last_us > diagonal_first_us);
   long double diagonal_axis_ratio =
      (long double)diagonal_x * (response_last_us[2] - response_first_us[2]) /
      ((long double)response_totals[2] * (diagonal_last_us - diagonal_first_us));
   assert(diagonal_x > 0 && diagonal_y > 0 &&
          diagonal_x - diagonal_y >= -1 && diagonal_x - diagonal_y <= 1 &&
          diagonal_axis_ratio > 0.6L && diagonal_axis_ratio < 0.8L);
   assert(saw_short_field && saw_long_field);

   arc_platform_test_input_events_reset();
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = -32768;
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_Y] = 32767;
   for (unsigned frame = 0u; frame < 8u; ++frame) retro_run();
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 0;
   analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_Y] = 0;
   for (unsigned frame = 0u; frame < 30u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   int64_t opposite_x = 0, opposite_y = 0;
   for (size_t i = 0u; i < input_event_count; ++i)
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION)
      {
         opposite_x += input_events[i].dx; opposite_y += input_events[i].dy;
      }
   assert(opposite_x < 0 && opposite_y > 0);

   /* The selectable left stick works immediately after an option update. */
   arc_platform_test_input_events_reset();
   option_pointer_stick = "left";
   variable_update = true;
   analog_state[RETRO_DEVICE_INDEX_ANALOG_LEFT][RETRO_DEVICE_ID_ANALOG_Y] = -32768;
   for (unsigned frame = 0u; frame < 12u; ++frame) retro_run();
   analog_state[RETRO_DEVICE_INDEX_ANALOG_LEFT][RETRO_DEVICE_ID_ANALOG_Y] = 0;
   for (unsigned frame = 0u; frame < 30u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   int64_t left_total_y = 0;
   for (size_t i = 0u; i < input_event_count; ++i)
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION) left_total_y += input_events[i].dy;
   assert(left_total_y < 0);
   option_pointer_stick = "right";
   variable_update = true;
   retro_run();

   /* Indexed touch hover, one-finger tap and two-finger tap. */
   indexed_pointer_mode = true;
   arc_platform_test_input_events_reset();
   pointer_count = 1u; pointer_contact_pressed[0] = true;
   pointer_contact_x[0] = 1000; pointer_contact_y[0] = 1000; retro_run();
   unsigned touch_width = geometry_width, touch_height = geometry_height;
   pointer_contact_x[0] = 5000; pointer_contact_y[0] = -3000; retro_run();
   pointer_count = 0u; pointer_contact_pressed[0] = false; retro_run();
   for (unsigned frame = 0u; frame < 30u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   bool touch_moved = false, touch_clicked = false;
   int64_t touch_total_x = 0, touch_total_y = 0;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION)
      {
         touch_moved = true;
         touch_total_x += input_events[i].dx; touch_total_y += input_events[i].dy;
      }
      if (input_events[i].type == ARC_INPUT_EVENT_BUTTON) touch_clicked = true;
   }
   assert(touch_moved && !touch_clicked &&
          touch_total_x == (int64_t)(4000u * touch_width / 65534u) &&
          touch_total_y == -(int64_t)(4000u * touch_height / 65534u));

   arc_platform_test_input_events_reset();
   pointer_count = 1u; pointer_contact_pressed[0] = true;
   pointer_contact_x[0] = 0; pointer_contact_y[0] = 0; retro_run();
   touch_width = geometry_width;
   for (unsigned sample = 1u; sample <= 5u; ++sample)
   {
      pointer_contact_x[0] = (int)(sample * 80u);
      retro_run();
   }
   pointer_count = 0u; pointer_contact_pressed[0] = false; retro_run();
   for (unsigned frame = 0u; frame < 30u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   int64_t fractional_touch_x = 0;
   unsigned fractional_touch_edges = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION)
         fractional_touch_x += input_events[i].dx;
      if (input_events[i].type == ARC_INPUT_EVENT_BUTTON && input_events[i].code == 1u)
         ++fractional_touch_edges;
   }
   assert(fractional_touch_x == (int64_t)(400u * touch_width / 65534u) &&
          fractional_touch_x > 0 && fractional_touch_edges == 2u);

   arc_platform_test_input_events_reset();
   pointer_count = 1u; pointer_contact_pressed[0] = true;
   pointer_contact_x[0] = 2000; pointer_contact_y[0] = 2000; retro_run();
   pointer_count = 0u; pointer_contact_pressed[0] = false; retro_run();
   for (unsigned frame = 0u; frame < 30u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 2u && input_events[0].type == ARC_INPUT_EVENT_BUTTON &&
          input_events[0].code == 1u && input_events[0].down &&
          input_events[1].code == 1u && !input_events[1].down);

   arc_platform_test_input_events_reset();
   pointer_count = 1u; pointer_contact_pressed[0] = true;
   pointer_contact_x[0] = -1000; pointer_contact_y[0] = -1000; retro_run();
   pointer_count = 2u; pointer_contact_pressed[1] = true;
   pointer_contact_x[1] = 1000; pointer_contact_y[1] = 1000; retro_run();
   pointer_count = 0u; pointer_contact_pressed[0] = pointer_contact_pressed[1] = false;
   retro_run();
   for (unsigned frame = 0u; frame < 30u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 2u && input_events[0].type == ARC_INPUT_EVENT_BUTTON &&
          input_events[0].code == 4u && input_events[0].down &&
          input_events[1].code == 4u && !input_events[1].down);

   /* Offscreen interruption cancels recognition and leaves no held button. */
   arc_platform_test_input_events_reset();
   pointer_count = 1u; pointer_contact_pressed[0] = true;
   pointer_contact_x[0] = 0; pointer_contact_y[0] = 0; retro_run();
   pointer_offscreen[0] = true; retro_run();
   pointer_count = 0u; pointer_contact_pressed[0] = false;
   pointer_offscreen[0] = false; retro_run();
   for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
   assert(arc_platform_test_input_events(input_events, 512u) == 0u);

   /* The one-percent radial jitter boundary is inclusive; the next raw unit,
    * a >250 ms hold, an interrupted contact, and an extra contact all cancel. */
   arc_platform_test_input_events_reset();
   pointer_count = 1u; pointer_contact_pressed[0] = true;
   pointer_contact_x[0] = 0; pointer_contact_y[0] = 0; retro_run();
   pointer_contact_x[0] = 655; retro_run();
   pointer_count = 0u; pointer_contact_pressed[0] = false; retro_run();
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   unsigned boundary_select_edges = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
      if (input_events[i].type == ARC_INPUT_EVENT_BUTTON && input_events[i].code == 1u)
         ++boundary_select_edges;
   assert(boundary_select_edges == 2u);

   arc_platform_test_input_events_reset();
   pointer_count = 1u; pointer_contact_pressed[0] = true;
   pointer_contact_x[0] = 0; retro_run(); pointer_contact_x[0] = 656; retro_run();
   pointer_count = 0u; pointer_contact_pressed[0] = false; retro_run();
   for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   for (size_t i = 0u; i < input_event_count; ++i)
      assert(input_events[i].type != ARC_INPUT_EVENT_BUTTON);

   arc_platform_test_input_events_reset();
   pointer_count = 1u; pointer_contact_pressed[0] = true;
   pointer_contact_x[0] = 4000; pointer_contact_y[0] = 4000; retro_run();
   for (unsigned frame = 0u; frame < 14u; ++frame) retro_run();
   pointer_count = 0u; pointer_contact_pressed[0] = false; retro_run();
   for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
   assert(arc_platform_test_input_events(input_events, 512u) == 0u);

   arc_platform_test_input_events_reset();
   pointer_count = 1u; pointer_contact_pressed[0] = true; retro_run();
   pointer_contact_pressed[0] = false; retro_run();
   pointer_count = 0u; retro_run();
   pointer_count = 3u;
   pointer_contact_pressed[0] = pointer_contact_pressed[1] = pointer_contact_pressed[2] = true;
   retro_run();
   pointer_count = 0u;
   pointer_contact_pressed[0] = pointer_contact_pressed[1] = pointer_contact_pressed[2] = false;
   retro_run();
   for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
   assert(arc_platform_test_input_events(input_events, 512u) == 0u);

   /* A late second finger is not a two-finger tap. Packed-index changes are
    * rebased by nearest-contact identity and create neither jump nor Select. */
   arc_platform_test_input_events_reset();
   pointer_count = 1u; pointer_contact_pressed[0] = true;
   pointer_contact_x[0] = -2000; pointer_contact_y[0] = -2000; retro_run();
   for (unsigned frame = 0u; frame < 5u; ++frame) retro_run();
   pointer_count = 2u; pointer_contact_pressed[1] = true;
   pointer_contact_x[1] = 2000; pointer_contact_y[1] = 2000; retro_run();
   pointer_count = 0u; pointer_contact_pressed[0] = pointer_contact_pressed[1] = false;
   retro_run();
   for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
   assert(arc_platform_test_input_events(input_events, 512u) == 0u);

   arc_platform_test_input_events_reset();
   pointer_count = 2u; pointer_contact_pressed[0] = pointer_contact_pressed[1] = true;
   pointer_contact_x[0] = -10000; pointer_contact_y[0] = 0;
   pointer_contact_x[1] = 10000; pointer_contact_y[1] = 0; retro_run();
   pointer_contact_x[0] = 10000; pointer_contact_x[1] = -10000; retro_run();
   pointer_count = 1u; pointer_contact_pressed[1] = false;
   pointer_contact_x[0] = -10000; retro_run();
   pointer_count = 0u; pointer_contact_pressed[0] = false; retro_run();
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   unsigned swapped_menu_edges = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type == ARC_INPUT_EVENT_BUTTON)
      {
         assert(input_events[i].code == 4u);
         ++swapped_menu_edges;
      }
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION)
         assert(input_events[i].dx == 0 && input_events[i].dy == 0);
   }
   assert(swapped_menu_edges == 2u);
   indexed_pointer_mode = false;

   /* The non-indexed frontend path has the same pointer contract (the path
    * used whether RetroArch's overlay is absent or supplies index zero). */
   arc_platform_test_input_events_reset();
   pointer_pressed = true; pointer_x = 1234; pointer_y = -1234; retro_run();
   pointer_pressed = false; retro_run();
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 2u && input_events[0].type == ARC_INPUT_EVENT_BUTTON &&
          input_events[0].code == 1u && input_events[0].down && !input_events[1].down);

   /* Physical displacement survives ±63 packet splitting, with motion before
    * its simultaneous click and drag motion before release. */
   arc_platform_test_input_events_reset();
   mouse_port_x[1] = 150; mouse_port_y[1] = -90; mouse_port_buttons[1] = 1u;
   retro_run();
   mouse_port_x[1] = 70; mouse_port_y[1] = -80; mouse_port_buttons[1] = 0u;
   retro_run();
   for (unsigned frame = 0u; frame < 80u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   int64_t physical_x = 0, physical_y = 0;
   size_t physical_down_index = SIZE_MAX, physical_up_index = SIZE_MAX;
   size_t last_motion_index = SIZE_MAX;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION)
      {
         assert(input_events[i].dx >= -63 && input_events[i].dx <= 63 &&
                input_events[i].dy >= -63 && input_events[i].dy <= 63);
         physical_x += input_events[i].dx; physical_y += input_events[i].dy;
         last_motion_index = i;
      }
      if (input_events[i].type == ARC_INPUT_EVENT_BUTTON && input_events[i].code == 1u)
      {
         if (input_events[i].down) physical_down_index = i;
         else physical_up_index = i;
      }
   }
   assert(physical_x == 220 && physical_y == -170 &&
          physical_down_index != SIZE_MAX && physical_up_index != SIZE_MAX &&
          physical_down_index < last_motion_index && last_motion_index < physical_up_index);

   /* Touch motion remains active while a pad source holds Select. Releasing
    * the pad after motion yields one merged hold/release pair and no tap. */
   indexed_pointer_mode = true;
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = true;
   pointer_count = 1u; pointer_contact_pressed[0] = true;
   pointer_contact_x[0] = 0; pointer_contact_y[0] = 0; retro_run();
   pointer_contact_x[0] = 5000; pointer_contact_y[0] = 3000; retro_run();
   pointer_count = 0u; pointer_contact_pressed[0] = false; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = false; retro_run();
   for (unsigned frame = 0u; frame < 50u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   unsigned mixed_select_edges = 0u;
   bool mixed_motion = false;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION) mixed_motion = true;
      if (input_events[i].type == ARC_INPUT_EVENT_BUTTON && input_events[i].code == 1u)
         ++mixed_select_edges;
   }
   assert(mixed_motion && mixed_select_edges == 2u);
   indexed_pointer_mode = false;

   /* One hundred deterministic pseudo-random effective transitions from the
    * callback, pad, and physical-mouse sources retain exact order while KART
    * is busy. Overlapped Select ownership suppresses the intermediate edges. */
   arc_platform_test_input_events_reset();
   unsigned random_state = 0x31415926u;
   ArcInputEvent random_expected[100];
   unsigned random_expected_count = 0u;
   bool random_levels[8] = {false};
   for (unsigned cycle = 0u; cycle < 20u; ++cycle)
   {
      for (unsigned key_event = 0u; key_event < 3u; ++key_event)
      {
         random_state = random_state * 1664525u + 1013904223u;
         unsigned slot = random_state >> 29u;
         random_levels[slot] = !random_levels[slot];
         keyboard.callback(random_levels[slot], RETROK_a + slot, 0u, 0u);
         random_expected[random_expected_count++] = (ArcInputEvent){
            ARC_INPUT_EVENT_KEY, RETROK_a + slot, random_levels[slot], false, 0, 0
         };
      }
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = true;
      random_expected[random_expected_count++] = (ArcInputEvent){
         ARC_INPUT_EVENT_BUTTON, 1u, true, false, 0, 0
      };
      retro_run();
      mouse_port_buttons[1] = 1u;
      retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = false;
      retro_run();
      mouse_port_buttons[1] = 0u;
      random_expected[random_expected_count++] = (ArcInputEvent){
         ARC_INPUT_EVENT_BUTTON, 1u, false, false, 0, 0
      };
      retro_run();
   }
   assert(random_expected_count == 100u);
   for (unsigned frame = 0u;
        frame < 1200u && arc_platform_test_input_events(input_events, 512u) < 100u;
        ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 100u);
   for (unsigned i = 0u; i < 100u; ++i)
      assert(input_events[i].type == random_expected[i].type &&
             input_events[i].code == random_expected[i].code &&
             input_events[i].down == random_expected[i].down);
   for (unsigned slot = 0u; slot < 8u; ++slot)
      if (random_levels[slot]) keyboard.callback(false, RETROK_a + slot, 0u, 0u);
   for (unsigned frame = 0u; frame < 80u; ++frame) retro_run();

   /* Overflow is reported once and converges to the sampled released level. */
   arc_platform_test_input_events_reset();
   unsigned overflow_before = input_overflow_log_lines;
   for (unsigned i = 0u; i < 301u; ++i)
      keyboard.callback((i & 1u) == 0u, RETROK_c, 0u, 0u);
   for (unsigned frame = 0u; frame < 900u; ++frame) retro_run();
   assert(input_overflow_log_lines == overflow_before + 1u);
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count > 0u && input_events[input_event_count - 1u].type == ARC_INPUT_EVENT_KEY &&
          input_events[input_event_count - 1u].code == RETROK_c &&
          input_events[input_event_count - 1u].down);
   keyboard.callback(false, RETROK_c, 0u, 0u);
   for (unsigned frame = 0u; frame < 30u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(!input_events[input_event_count - 1u].down);

   /* Reset and full reload discard pending transitions, clear engine levels,
    * and then resample held frontend sources without leaving a stuck release. */
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = true;
   keyboard.callback(true, RETROK_d, 0u, 0u);
   keyboard.callback(false, RETROK_d, 0u, 0u);
   retro_reset();
   arc_platform_test_input_events_reset();
   for (unsigned frame = 0u; frame < 40u; ++frame) retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = false;
   for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   bool reset_key_down = false, reset_key_up = false;
   bool reset_button_down = false, reset_button_up = false;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type == ARC_INPUT_EVENT_KEY && input_events[i].code == RETROK_SPACE)
         input_events[i].down ? (reset_key_down = true) : (reset_key_up = true);
      if (input_events[i].type == ARC_INPUT_EVENT_BUTTON && input_events[i].code == 1u)
         input_events[i].down ? (reset_button_down = true) : (reset_button_up = true);
   }
   assert(reset_key_down && reset_key_up && reset_button_down && reset_button_up);

   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L] = true;
   keyboard.callback(true, RETROK_e, 0u, 0u);
   keyboard.callback(false, RETROK_e, 0u, 0u);
   retro_unload_game();
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   phase = PHASE_RUN;
   arc_platform_test_input_events_reset();
   /* These callback edges arrive before the generated guest completes its
    * KART reset handshake, so they also pin keyboard-disabled retention. */
   keyboard.callback(true, RETROK_f, 0u, 0u);
   keyboard.callback(false, RETROK_f, 0u, 0u);
   for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L] = false;
   for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   unsigned adjust_edges = 0u;
   unsigned startup_key_edges = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type == ARC_INPUT_EVENT_BUTTON && input_events[i].code == 2u)
         ++adjust_edges;
      if (input_events[i].type == ARC_INPUT_EVENT_KEY && input_events[i].code == RETROK_f)
         ++startup_key_edges;
   }
   assert(adjust_edges == 2u && startup_key_edges == 2u);

   uint64_t before_input_signature = last_video_signature;
   keyboard.callback(true, RETROK_a, 0u, 0u);
   joypad_a = true; mouse_x = 150; mouse_y = -90; mouse_buttons = 1u;
   for (unsigned frame = 0; frame < 15u; ++frame) retro_run();
   keyboard.callback(false, RETROK_a, 0u, 0u);
   joypad_a = false; mouse_buttons = 0u;
   pointer_pressed = true; pointer_x = 1000; pointer_y = 1000; retro_run();
   pointer_x = 1025; pointer_y = 970; retro_run();
   pointer_pressed = false; retro_run();

   uint64_t port1_baseline[16] = {0};
   unsigned port1_baseline_count = 0u;
   for (unsigned frame = 0u; frame < 40u; ++frame)
   {
      retro_run();
      signature_add(port1_baseline, &port1_baseline_count, last_video_signature);
   }
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = true;
   for (unsigned frame = 0u; frame < 30u; ++frame) retro_run();
   retro_set_controller_port_device(0u, RETRO_DEVICE_NONE);
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R] = false;
   for (unsigned frame = 0u; frame < 40u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   unsigned reassigned_space_edges = 0u, reassigned_select_edges = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type == ARC_INPUT_EVENT_KEY && input_events[i].code == RETROK_SPACE)
         ++reassigned_space_edges;
      if (input_events[i].type == ARC_INPUT_EVENT_BUTTON && input_events[i].code == 1u)
         ++reassigned_select_edges;
   }
   assert(reassigned_space_edges == 2u && reassigned_select_edges == 2u);
   retro_set_controller_port_device(1u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1));
   retro_run(); /* Clear the port-1 neutral gate before the new press. */
   joypad_port = 1u; joypad_a = true;
   bool port1_guest_observed = false;
   for (unsigned frame = 0u; frame < 20u; ++frame)
   {
      retro_run();
      if (!signature_is_in(port1_baseline, port1_baseline_count, last_video_signature))
         port1_guest_observed = true;
   }
   joypad_a = false;
   assert(port1_guest_observed);
   retro_set_controller_port_device(0u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1));
   retro_set_controller_port_device(1u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1));
   joypad_port = 0u;
   bool input_observed = before_input_signature != last_video_signature;

   /* Without the advertised keyboard capability, a polled-up level cannot
    * release a key that is still held by the callback source. */
   arc_platform_test_input_events_reset();
   keyboard_levels[RETROK_a] = false;
   keyboard.callback(true, RETROK_a, 0u, 0u);
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   unsigned capability_off_a_edges = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
      if (input_events[i].type == ARC_INPUT_EVENT_KEY &&
          input_events[i].code == RETROK_a)
      {
         ++capability_off_a_edges;
         assert(input_events[i].down);
      }
   assert(capability_off_a_edges == 1u);
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   capability_off_a_edges = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
      if (input_events[i].type == ARC_INPUT_EVENT_KEY &&
          input_events[i].code == RETROK_a)
         ++capability_off_a_edges;
   assert(capability_off_a_edges == 1u);
   keyboard.callback(false, RETROK_a, 0u, 0u);
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   capability_off_a_edges = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
      if (input_events[i].type == ARC_INPUT_EVENT_KEY &&
          input_events[i].code == RETROK_a)
      {
         ++capability_off_a_edges;
         if (capability_off_a_edges == 2u) assert(!input_events[i].down);
      }
   assert(capability_off_a_edges == 2u);

   /* A frontend-advertised keyboard snapshot repairs both missing edges. The
    * two Menu aliases remain one merged latch, and a callback tap is retained. */
   keyboard_poll_capability = true;
   retro_unload_game();
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   phase = PHASE_RUN;
   arc_platform_test_input_events_reset();
   keyboard_levels[RETROK_a] = true;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   keyboard_levels[RETROK_a] = false;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   keyboard_levels[RETROK_MENU] = true;
   keyboard_levels[RETROK_LMETA] = true;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   keyboard_levels[RETROK_MENU] = false;
   for (unsigned frame = 0u; frame < 5u; ++frame) retro_run();
   keyboard_levels[RETROK_LMETA] = false;
   keyboard.callback(true, RETROK_b, 0u, 0u);
   keyboard.callback(false, RETROK_b, 0u, 0u);
   for (unsigned frame = 0u; frame < 40u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   unsigned snapshot_a = 0u, snapshot_b = 0u, snapshot_menu = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type == ARC_INPUT_EVENT_KEY && input_events[i].code == RETROK_a)
         ++snapshot_a;
      if (input_events[i].type == ARC_INPUT_EVENT_KEY && input_events[i].code == RETROK_b)
         ++snapshot_b;
      if (input_events[i].type == ARC_INPUT_EVENT_BUTTON && input_events[i].code == 4u)
         ++snapshot_menu;
   }
   assert(snapshot_a == 2u && snapshot_b == 2u && snapshot_menu == 2u);
   keyboard_poll_capability = false;
   retro_unload_game();
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   phase = PHASE_RUN;

   for (unsigned frame = 78u; frame < 100u; ++frame) retro_run();
   uint64_t measured_expected_total = 0u;
   uint64_t measured_actual_total = 0u;
   uint64_t measured_max_difference = 0u;
   unsigned measured_window_modes[2] = {0u, 0u};
   unsigned measured_windows = 0u;
   unsigned av_probe_frames = 0u;
   for (unsigned attempt = 0; attempt < 100u && measured_windows < 2u; ++attempt)
   {
      measured_expected_audio = 0.0L;
      measured_actual_audio = 0u;
      measured_video_frames = 0u;
      av_rate_changed_during_measure = false;
      measured_oracle_mode = 0u;
      measured_oracle_valid = true;
      measure_av = true;
      for (unsigned frame = 0; frame < 32u; ++frame) retro_run();
      measure_av = false;
      av_probe_frames += 32u;
      if (av_rate_changed_during_measure || !measured_oracle_valid ||
          measured_video_frames != 32u) continue;
      bool duplicate_rate = false;
      for (unsigned i = 0; i < measured_windows; ++i)
         if (measured_window_modes[i] == measured_oracle_mode) duplicate_rate = true;
      if (duplicate_rate) continue;
      double oracle_rate = measured_oracle_mode == 1u ?
         1.0 / 0.019968 : 1.0 / 0.0256;
      assert(current_reported_rate > oracle_rate - 0.0001 &&
             current_reported_rate < oracle_rate + 0.0001);
      uint64_t window_expected = (uint64_t)(measured_expected_audio + 0.5L);
      uint64_t window_difference = measured_actual_audio > window_expected ?
         measured_actual_audio - window_expected : window_expected - measured_actual_audio;
      assert(window_difference <= 1u);
      measured_expected_total += window_expected;
      measured_actual_total += measured_actual_audio;
      if (window_difference > measured_max_difference)
         measured_max_difference = window_difference;
      measured_window_modes[measured_windows++] = measured_oracle_mode;
   }
   assert(measured_windows == 2u);
   retro_unload_game();
   phase = PHASE_LOAD; assert(retro_load_game(&changed_game));
   phase = PHASE_RUN; for (unsigned frame = 0; frame < 60u; ++frame) retro_run();
   uint64_t changed_signature = last_video_signature;
   for (unsigned frame = 60u; frame < 100u; ++frame) retro_run();
   retro_unload_game();
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   phase = PHASE_RUN; retro_run();
   unsigned blank_before = video_frames;
   unsigned fault_vidc_info_before = vidc_info_log_lines;
   unsigned fault_vidc_warning_before = vidc_warning_log_lines;
   unsigned fault_vidc_osd_before = vidc_osd_ext_calls + vidc_osd_legacy_calls;
   phase = PHASE_LOAD; assert(!retro_load_game(&bad_game));
   phase = PHASE_RUN; retro_run();
   assert(video_frames == blank_before + 1u);
   assert(vidc_info_log_lines == fault_vidc_info_before &&
          vidc_warning_log_lines == fault_vidc_warning_before &&
          vidc_osd_ext_calls + vidc_osd_legacy_calls == fault_vidc_osd_before);
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   retro_unload_game();
   assert(before_input_signature != changed_signature);
   assert(hash_file(argv[3]) == source_hash);
   assert(hash_file(changed) == changed_source_hash);
   assert(modified_shadow_count(save_path, source_hash, changed_source_hash) >= 2u);

   char original_write[4096];
   snprintf(original_write, sizeof(original_write), "%s/original-write.adf", argv[4]);
   copy_file(argv[3], original_write);
   uint64_t original_before = hash_file(original_write);
   struct retro_game_info original_game = {original_write, NULL, 0u, NULL};
   option_writes = "original";
   phase = PHASE_LOAD; assert(retro_load_game(&original_game));
   phase = PHASE_RUN; for (unsigned frame = 0u; frame < 100u; ++frame) retro_run();
   retro_unload_game();
   assert(hash_file(original_write) != original_before);

   char write_off[4096];
   snprintf(write_off, sizeof(write_off), "%s/write-off.adf", argv[4]);
   copy_file(argv[3], write_off);
   uint64_t off_before = hash_file(write_off);
   struct retro_game_info off_game = {write_off, NULL, 0u, NULL};
   option_writes = "off";
   option_pointer = "disabled";
   phase = PHASE_LOAD; assert(retro_load_game(&off_game));
   phase = PHASE_RUN;
   uint64_t pointer_disabled_baseline[16] = {0};
   unsigned pointer_disabled_baseline_count = 0u;
   for (unsigned frame = 0u; frame < 100u; ++frame)
   {
      retro_run();
      signature_add(pointer_disabled_baseline, &pointer_disabled_baseline_count,
                    last_video_signature);
   }
   pointer_pressed = true; pointer_x = -20000; pointer_y = -16000; retro_run();
   assert(signature_is_in(pointer_disabled_baseline, pointer_disabled_baseline_count,
                          last_video_signature));
   for (unsigned frame = 0u; frame < 12u; ++frame)
   {
      pointer_x += 2000; pointer_y += 1500; retro_run();
      assert(signature_is_in(pointer_disabled_baseline, pointer_disabled_baseline_count,
                             last_video_signature));
   }
   pointer_pressed = false; retro_run();
   assert(signature_is_in(pointer_disabled_baseline, pointer_disabled_baseline_count,
                          last_video_signature));
   retro_unload_game();
   assert(hash_file(write_off) == off_before);

   option_writes = "shadow";
   option_pointer = "relative";
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   unsigned preserved_count = disk_control.get_num_images();
   unsigned preserved_index = disk_control.get_image_index();
   bool preserved_ejected = disk_control.get_eject_state();
   bool preserved_write_protected = false;
   char preserved_media_digest[41];
   uint8_t preserved_cmos_identity[16];
   assert(arc_engine_test_drive_media(0u, &preserved_write_protected,
                                      preserved_media_digest));
   assert(retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) == ARC_CMOS_RECORD_SIZE);
   memcpy(preserved_cmos_identity,
          retro_get_memory_data(RETRO_MEMORY_SAVE_RAM),
          sizeof(preserved_cmos_identity));
   unsigned video_before_rejected_reset = video_frames;
   uint64_t audio_before_rejected_reset = audio_frames;
   unsigned queries_before_reset = option_query_count;
   option_joystick = "A3010";
   phase = PHASE_RUN; retro_reset();
   assert(option_query_count >= queries_before_reset + 10u &&
          strstr(last_message, "A3010 joystick interface"));
   for (unsigned frame = 0u; frame < 60u; ++frame) retro_run();
   bool current_write_protected = false;
   char current_media_digest[41];
   assert(video_frames > video_before_rejected_reset &&
          audio_frames > audio_before_rejected_reset &&
          disk_control.get_num_images() == preserved_count &&
          disk_control.get_image_index() == preserved_index &&
          disk_control.get_eject_state() == preserved_ejected &&
          arc_engine_test_drive_media(0u, &current_write_protected,
                                      current_media_digest) &&
          current_write_protected == preserved_write_protected &&
          !strcmp(current_media_digest, preserved_media_digest) &&
          !memcmp(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM),
                  preserved_cmos_identity, sizeof(preserved_cmos_identity)));
   option_joystick = "Keyboard";
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   retro_unload_game();
   assert((launch_options_seen & 31u) == 31u);

   uint8_t saved_cmos[256];
   uint8_t final_cmos[256];
   {
      ArcLaunchPlan real_plan;
      uint8_t real_default[ARC_CMOS_PAYLOAD_SIZE];
      uint8_t real_record[ARC_CMOS_RECORD_SIZE];
      uint8_t restored[ARC_CMOS_PAYLOAD_SIZE];
      char error[128];
      const char id[] = "0123456789012345678901234567890123456789";
      assert(arc_launch_plan_default(&real_plan, ARC_MEDIA_ADF_800K,
             ARC_OS_RISCOS_311, id, false, error, sizeof(error)));
      assert(arc_cmos_default(ARC_OS_RISCOS_311, ARC_IO_OLD_WD1770, real_default));
      assert(arc_cmos_encode(real_record, &real_plan, real_default));
      memset(real_record, 0xa5, sizeof(real_record));
      assert(!arc_cmos_decode(real_record, &real_plan, restored));
      assert(arc_cmos_default(ARC_OS_RISCOS_311, ARC_IO_OLD_WD1770, restored));
      bool any_nonzero = false;
      for (unsigned i = 0u; i < sizeof(restored); ++i)
         any_nonzero = any_nonzero || restored[i] != 0u;
      assert(any_nonzero);
   }
   size_t heap_baseline = 0u;
   size_t heap_maximum_delta = 0u;
   for (unsigned cycle = 0; cycle < 100u; ++cycle)
   {
      struct retro_game_info playlist_cycle = {media_tests ? argv[6] : argv[3], NULL, 0u, NULL};
      struct retro_game_info jfd_cycle = {media_tests ? argv[10] : argv[3], NULL, 0u, NULL};
      const struct retro_game_info *cycle_game = !media_tests || cycle % 3u == 0u ? &game :
         (cycle % 3u == 1u ? &playlist_cycle : &jfd_cycle);
      phase = PHASE_LOAD; assert(retro_load_game(cycle_game)); assert(keyboard.callback);
      assert(retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) == 256u);
      assert(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
      phase = PHASE_RUN;
      option_osk_toggle = cycle & 1u ? "R3" : "L3";
      variable_update = true;
      retro_run();
      unsigned lifecycle_toggle = cycle & 1u ? RETRO_DEVICE_ID_JOYPAD_R3 :
                                                RETRO_DEVICE_ID_JOYPAD_L3;
      joypad_buttons[0][lifecycle_toggle] = true; retro_run();
      joypad_buttons[0][lifecycle_toggle] = false; retro_run();
      if (cycle == 0u)
      {
         memset(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), 0xa5, 256u);
         uint8_t previous_record[256];
         memcpy(previous_record, retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), 256u);
         unsigned record_changes = 0u;
         unsigned writes_before_frames = cmos_write_opens;
         retro_run();
         assert(memcmp(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), "ARCCMOS2", 8u) == 0);
         if (memcmp(previous_record, retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), 256u))
         {
            ++record_changes;
            memcpy(previous_record, retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), 256u);
         }
         for (unsigned frame = 1u; frame < 100u; ++frame)
         {
            retro_run();
            if (memcmp(previous_record, retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), 256u))
            {
               ++record_changes;
               memcpy(previous_record, retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), 256u);
            }
         }
         assert(cmos_write_opens - writes_before_frames <= record_changes);
         memcpy(saved_cmos, retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), sizeof(saved_cmos));
      }
      else
      {
         if (cycle % 3u == 0u)
            assert(memcmp(saved_cmos, retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), sizeof(saved_cmos)) == 0);
         else
            assert(!memcmp(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), "ARCCMOS2", 8u));
         for (unsigned frame = 0; frame < 100u; ++frame) retro_run();
      }
      if (cycle == 0u)
      {
         /* Sample the live queue before reset clears it. One 50 ms engine
          * delivery window bounds startup priming; steady state must not overrun. */
         arc_engine_audio_stats(&sampled_underruns, &sampled_overruns);
         assert(sampled_underruns <= 2400u && sampled_overruns == 0u);
      }
      retro_reset(); retro_unload_game();
      assert(open_files == 0u && open_dirs == 0u);
      size_t heap_now = heap_bytes_in_use();
      if (cycle == 0u)
         heap_baseline = heap_now;
      else if (heap_now > heap_baseline && heap_now - heap_baseline > heap_maximum_delta)
         heap_maximum_delta = heap_now - heap_baseline;
   }
   option_osk_toggle = "L3";
   char cmos_file[4096];
   ArcDigest source_digest = digest_file(argv[3]);
   snprintf(cmos_file, sizeof(cmos_file), "%s/arcdroid/cmos/A3000.test.%s.bin",
            save_path, source_digest.sha1);
   read_exact(cmos_file, final_cmos, sizeof(final_cmos));
   assert(memcmp(saved_cmos, final_cmos, sizeof(saved_cmos)) == 0);
   retro_deinit();

   /* Negotiate frontend duplication in a second pass. The generated ROM's
    * 25,000-iteration invalid-VIDC wait is about 50 ms of guest time at its
    * 8 MHz ARM2 (the loop executes from four-cycle ROM), far below the
    * adapter's two-second persistent-warning threshold. */
   can_dupe = true;
   unsigned duplicates_before = duplicate_video_frames;
   unsigned vidc_osd_before = vidc_osd_ext_calls + vidc_osd_legacy_calls;
   unsigned vidc_logs_before = vidc_info_log_lines;
   phase = PHASE_ENV; retro_set_environment(environment);
   retro_set_video_refresh(video); retro_set_audio_sample_batch(audio);
   retro_set_input_poll(poll); retro_set_input_state(input);
   phase = PHASE_INIT; retro_init();
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   phase = PHASE_RUN;
   bool completed_before_invalid = false;
   bool saw_short_invalid_episode = false;
   bool in_incomplete_episode = false;
   unsigned transient_episodes = 0u;
   for (unsigned frame = 0u; frame < 120u && !saw_short_invalid_episode; ++frame)
   {
      unsigned frame_duplicates_before = duplicate_video_frames;
      retro_run();
      bool incomplete = duplicate_video_frames != frame_duplicates_before;
      assert(vidc_osd_ext_calls + vidc_osd_legacy_calls == vidc_osd_before);
      if (incomplete && !in_incomplete_episode)
      {
         ++transient_episodes;
         in_incomplete_episode = true;
      }
      else if (!incomplete)
      {
         if (completed_before_invalid && in_incomplete_episode)
            saw_short_invalid_episode = true;
         completed_before_invalid = true;
         in_incomplete_episode = false;
      }
   }
   assert(completed_before_invalid && saw_short_invalid_episode);
   assert(duplicate_video_frames > duplicates_before);
   assert(vidc_info_log_lines - vidc_logs_before == transient_episodes);

   /* The host-only fault hook suppresses field completion while retaining the
    * bounded guest-time step. A sustained episode warns once, a completed
    * field re-arms it, and a second sustained episode warns once again. */
   unsigned persistent_osd_before = vidc_osd_ext_calls + vidc_osd_legacy_calls;
   unsigned persistent_info_before = vidc_info_log_lines;
   unsigned persistent_warning_before = vidc_warning_log_lines;
   assert(persistent_osd_before == 0u);
   arc_engine_test_set_vidc_noncompleting(true);
   for (unsigned frame = 0u; frame < 40u; ++frame) retro_run();
   assert(vidc_osd_ext_calls + vidc_osd_legacy_calls == persistent_osd_before);
   assert(vidc_info_log_lines == persistent_info_before + 1u);
   assert(vidc_warning_log_lines == persistent_warning_before);
   for (unsigned frame = 0u; frame < 140u; ++frame) retro_run();
   assert(vidc_osd_ext_calls + vidc_osd_legacy_calls == persistent_osd_before + 1u);
   assert(vidc_info_log_lines == persistent_info_before + 1u);
   assert(vidc_warning_log_lines == persistent_warning_before + 1u);
   for (unsigned frame = 0u; frame < 60u; ++frame) retro_run();
   assert(vidc_osd_ext_calls + vidc_osd_legacy_calls == persistent_osd_before + 1u);
   arc_engine_test_set_vidc_noncompleting(false);
   unsigned recovery_duplicates = duplicate_video_frames;
   retro_run();
   assert(duplicate_video_frames == recovery_duplicates);
   arc_engine_test_set_vidc_noncompleting(true);
   for (unsigned frame = 0u; frame < 40u; ++frame) retro_run();
   assert(vidc_osd_ext_calls + vidc_osd_legacy_calls == persistent_osd_before + 1u);
   assert(vidc_info_log_lines == persistent_info_before + 2u);
   assert(vidc_warning_log_lines == persistent_warning_before + 1u);
   for (unsigned frame = 0u; frame < 140u; ++frame) retro_run();
   assert(vidc_osd_ext_calls + vidc_osd_legacy_calls == persistent_osd_before + 2u);
   assert(vidc_info_log_lines == persistent_info_before + 2u);
   assert(vidc_warning_log_lines == persistent_warning_before + 2u);
   for (unsigned frame = 0u; frame < 60u; ++frame) retro_run();
   assert(vidc_osd_ext_calls + vidc_osd_legacy_calls == persistent_osd_before + 2u);
   arc_engine_test_set_vidc_noncompleting(false);

   /* Stall KART consumption while real frontend samples enqueue a pad chord
    * and two physical-mouse deltas. Entering the modal OSK preserves those
    * accepted events; they drain once KART resumes. */
   for (unsigned frame = 0u;
        frame < 100u && arc_libretro_test_input_pending(); ++frame)
      retro_run();
   assert(arc_libretro_test_input_pending() == 0u);
   arc_platform_test_input_events_reset();
   arc_libretro_test_stall_kart(true);
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
   mouse_port_x[1] = 4; mouse_port_y[1] = -3;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   mouse_port_x[1] = 5; mouse_port_y[1] = 2;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = false; retro_run();
   assert(arc_libretro_test_osk_visible() &&
          arc_libretro_test_input_pending() >= 4u);
   arc_libretro_test_stall_kart(false);
   for (unsigned frame = 0u;
        frame < 200u && arc_libretro_test_input_pending(); ++frame)
      retro_run();
   assert(arc_libretro_test_input_pending() == 0u);
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   unsigned stalled_space_edges = 0u;
   int64_t stalled_mouse_x = 0, stalled_mouse_y = 0;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type == ARC_INPUT_EVENT_KEY &&
          input_events[i].code == RETROK_SPACE)
         ++stalled_space_edges;
      if (input_events[i].type == ARC_INPUT_EVENT_MOTION)
      {
         stalled_mouse_x += input_events[i].dx;
         stalled_mouse_y += input_events[i].dy;
      }
   }
   assert(stalled_space_edges == 2u && stalled_mouse_x == 9 &&
          stalled_mouse_y == -1);

   /* Relayout a visible OSK across geometry tiers, then fill the stalled
    * input FIFO so activation reports BUSY without beginning a partial key. */
   arc_libretro_test_probe_osk_geometry(320u, 256u);
   assert(arc_libretro_test_osk_visible());
   assert(arc_libretro_test_osk_focus_key(RETROK_ESCAPE));
   retro_run();
   assert(arc_libretro_test_osk_visible());
   arc_libretro_test_stall_kart(true);
   arc_libretro_test_fill_input_queue();
   assert(arc_libretro_test_input_pending() == ARC_INPUT_FIFO_CAPACITY);
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   assert(arc_libretro_test_osk_busy());
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
   arc_libretro_test_stall_kart(false);
   for (unsigned frame = 0u;
        frame < 600u && arc_libretro_test_input_pending(); ++frame)
      retro_run();
   assert(arc_libretro_test_input_pending() == 0u);
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   for (unsigned frame = 0u;
        frame < 120u && arc_platform_test_input_events(input_events, 512u) < 2u;
        ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(!arc_libretro_test_osk_busy() && input_event_count == 2u &&
          input_events[0].code == RETROK_ESCAPE && input_events[0].down &&
          input_events[1].code == RETROK_ESCAPE && !input_events[1].down);

   /* Five 20 ms fields separate the two tap-down edges. The second arrives
    * while the first activation is outstanding and must be delivered once. */
   arc_platform_test_input_events_reset();
   assert(arc_libretro_test_osk_focus_key(RETROK_ESCAPE));
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
   for (unsigned field = 0u; field < 3u; ++field) retro_run();
   assert(arc_libretro_test_osk_activation_active());
   assert(arc_libretro_test_osk_focus_key(RETROK_F1));
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
   for (unsigned frame = 0u;
        frame < 120u && arc_platform_test_input_events(input_events, 512u) < 4u;
        ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 4u &&
          input_events[0].code == RETROK_ESCAPE && input_events[0].down &&
          input_events[1].code == RETROK_ESCAPE && !input_events[1].down &&
          input_events[2].code == RETROK_F1 && input_events[2].down &&
          input_events[3].code == RETROK_F1 && !input_events[3].down);
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_START] = true; retro_run();
   assert(!arc_libretro_test_osk_visible());
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_START] = false; retro_run();

   /* The in-core OSK is modal only for pad 0 and touch. Its activation edges
    * remain consumption-acknowledged, while duplicate fields resume only once
    * the complete composed image is unchanged. */
   uint64_t before_osk_signature = last_video_signature;
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = true;
   retro_run();
   assert(!last_video_was_duplicate);
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = false;
   retro_run();
   assert(arc_libretro_test_osk_focus_key(RETROK_a));
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
   retro_run();
   for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_RIGHT] = true;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_RIGHT] = false;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_UP] = true;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_UP] = false;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_X] = true;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_X] = false;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_Y] = true;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_Y] = false;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   unsigned osk_touch_x = 0u, osk_touch_y = 0u;
   assert(arc_libretro_test_osk_key_center(RETROK_a, &osk_touch_x,
                                           &osk_touch_y));
   pointer_x = (int)((uint64_t)osk_touch_x * 65536u / geometry_width) - 32768;
   pointer_y = (int)((uint64_t)osk_touch_y * 65536u / geometry_height) - 32768;
   pointer_pressed = true; retro_run();
   pointer_pressed = false; retro_run();
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   pointer_pressed = true; retro_run();
   pointer_x = 30000; retro_run();
   pointer_pressed = false; retro_run();
   pointer_x = (int)((uint64_t)osk_touch_x * 65536u / geometry_width) - 32768;
   pointer_pressed = true; retro_run();
   pointer_offscreen[0] = true; retro_run();
   pointer_pressed = false; retro_run();
   pointer_offscreen[0] = false; retro_run();
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_SELECT] = true;
   retro_run();
   assert(!last_video_was_duplicate);
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_SELECT] = false;
   for (unsigned frame = 0u; frame < 80u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   unsigned osk_a_edges = 0u, osk_b_edges = 0u;
   unsigned osk_backspace_edges = 0u, osk_return_edges = 0u;
   unsigned modal_direction_edges = 0u;
   for (size_t i = 0u; i < input_event_count; ++i)
   {
      if (input_events[i].type != ARC_INPUT_EVENT_KEY) continue;
      if (input_events[i].code == RETROK_a) ++osk_a_edges;
      if (input_events[i].code == RETROK_b) ++osk_b_edges;
      if (input_events[i].code == RETROK_BACKSPACE) ++osk_backspace_edges;
      if (input_events[i].code == RETROK_RETURN) ++osk_return_edges;
      if (input_events[i].code == RETROK_UP ||
          input_events[i].code == RETROK_RIGHT) ++modal_direction_edges;
   }
   assert(osk_a_edges == 8u && osk_b_edges == 0u &&
          osk_backspace_edges == 2u && osk_return_edges == 2u &&
          modal_direction_edges == 0u);
   assert(last_video_signature != before_osk_signature);
   assert(arc_libretro_test_osk_source_unchanged());
   /* A generation change during a held activation reaches the compositor and
    * remains on an actually presented frame 20 fields later (about 400 ms). */
   assert(arc_libretro_test_osk_focus_key(RETROK_a));
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   arc_libretro_test_force_input_reconcile();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   for (unsigned frame = 0u; frame < 20u; ++frame) retro_run();
   assert((arc_libretro_test_presented_osk_notice() & 2u) != 0u);
   for (unsigned frame = 0u;
        frame < 300u && arc_libretro_test_input_pending(); ++frame)
      retro_run();
   assert(arc_libretro_test_input_pending() == 0u);
   unsigned narrow_before = osk_narrow_osd_calls;
   arc_libretro_test_probe_osk_geometry(256u, 256u);
   arc_libretro_test_probe_osk_geometry(256u, 256u);
   assert(osk_narrow_osd_calls == narrow_before + 1u);
   arc_engine_test_set_vidc_noncompleting(true);
   bool osk_duplicate = false;
   for (unsigned frame = 0u; frame < 10u && !osk_duplicate; ++frame)
   { retro_run(); osk_duplicate = last_video_was_duplicate; }
   assert(osk_duplicate);
   /* Closing the modal OSK while its pointer contact is still held must not
    * hand that contact to gameplay as motion or a click. */
   arc_platform_test_input_events_reset();
   pointer_pressed = true;
   pointer_x = 1000;
   pointer_y = 1000;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_B] = true;
   retro_run();
   assert(!last_video_was_duplicate);
   arc_engine_test_set_vidc_noncompleting(false);
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_B] = false;
   pointer_x = 6000;
   pointer_y = -4000;
   for (unsigned frame = 0u; frame < 4u; ++frame) retro_run();
   pointer_pressed = false;
   retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 0u);
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
   retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   for (unsigned frame = 0u; frame < 40u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 2u && input_events[0].code == RETROK_SPACE &&
          input_events[0].down && !input_events[1].down);

   option_osk_toggle = "Off";
   variable_update = true;
   arc_engine_test_set_vidc_noncompleting(true);
   for (unsigned frame = 0u; frame < 10u && !last_video_was_duplicate; ++frame)
      retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = true; retro_run();
   assert(last_video_was_duplicate);
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = false; retro_run();
   option_osk_toggle = "R3";
   variable_update = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R3] = true; retro_run();
   assert(!last_video_was_duplicate);
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_R3] = false; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_START] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_START] = false; retro_run();
   arc_engine_test_set_vidc_noncompleting(false);
   option_osk_toggle = "L3";
   variable_update = true; retro_run();
   retro_unload_game();

   /* Allocation faults disable only the OSK. Initial failure leaves gameplay
    * live; a forced growth failure during activation recovers its key release
    * and presents a clean non-duplicated guest frame. */
   unsigned osk_failures = osk_failure_osd_calls;
   arc_libretro_test_fail_osk_allocations(1u);
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   phase = PHASE_RUN;
   assert(osk_failure_osd_calls == osk_failures + 1u);
   retro_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = false; retro_run();
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   for (unsigned frame = 0u; frame < 40u; ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 2u && input_events[0].code == RETROK_SPACE);
   retro_unload_game();

   phase = PHASE_LOAD; assert(retro_load_game(&game));
   phase = PHASE_RUN;
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_L3] = false; retro_run();
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   arc_libretro_test_drop_osk_surface();
   arc_libretro_test_fail_osk_allocations(1u);
   retro_run();
   assert(osk_failure_osd_calls == osk_failures + 2u && !last_video_was_duplicate);
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   for (unsigned frame = 0u;
        frame < 120u && arc_platform_test_input_events(input_events, 512u) < 2u;
        ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 2u &&
          input_events[0].code == RETROK_a && input_events[0].down &&
          input_events[1].code == RETROK_a && !input_events[1].down);
   arc_platform_test_input_events_reset();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
   joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
   for (unsigned frame = 0u;
        frame < 120u && arc_platform_test_input_events(input_events, 512u) < 2u;
        ++frame) retro_run();
   input_event_count = arc_platform_test_input_events(input_events, 512u);
   assert(input_event_count == 2u &&
          input_events[0].code == RETROK_SPACE && input_events[0].down &&
          input_events[1].code == RETROK_SPACE && !input_events[1].down);
   retro_unload_game();

   if (media_tests)
   {
      /* Switch to the separately fingerprinted, project-authored media ROM.
       * No firmware or game bytes are present in these fixtures. */
      copy_file(argv[5], renamed);
      struct retro_game_info playlist_game = {argv[6], NULL, 0u, NULL};
      struct retro_game_info jfd_game = {argv[10], NULL, 0u, NULL};
      uint64_t disc1_hash = hash_file(argv[7]);
      uint64_t disc2_hash = hash_file(argv[8]);
      char image_path[4096], image_label[128];
      char fault_save_path[4096];
      snprintf(fault_save_path, sizeof(fault_save_path), "%s/fault-save-%ld",
               argv[4], (long)getpid());
      make_directory(fault_save_path);
      save_dir = fault_save_path;

      /* One-shot VFS failures exercise every staging publication boundary.
       * The inaccessible second entry also verifies rollback of the first
       * entry's newly created persistent copy. */
      fault_open_substring = "arcdroid-media-disc2.adf";
      phase = PHASE_LOAD; assert(!retro_load_game(&playlist_game));
      assert(!fault_open_substring && open_files == 0u);
      fault_read_substring = "arcdroid-media-disc1.adf";
      phase = PHASE_LOAD; assert(!retro_load_game(&playlist_game));
      assert(!fault_read_substring && open_files == 0u);
      fault_write_substring = ".tmp";
      phase = PHASE_LOAD; assert(!retro_load_game(&playlist_game));
      assert(!fault_write_substring && open_files == 0u);
      fault_flush_substring = ".tmp";
      phase = PHASE_LOAD; assert(!retro_load_game(&playlist_game));
      assert(!fault_flush_substring && open_files == 0u);
      fault_close_substring = ".tmp";
      phase = PHASE_LOAD; assert(!retro_load_game(&playlist_game));
      assert(!fault_close_substring && open_files == 0u);
      fault_rename_substring = "/disc.adf";
      fault_rename_destination[0] = '\0';
      phase = PHASE_LOAD; assert(!retro_load_game(&playlist_game));
      assert(!fault_rename_substring && fault_rename_destination[0] &&
             open_files == 0u);
      assert(remove(fault_rename_destination) == 0);
      fault_rename_destination[0] = '\0';
      assert(hash_file(argv[7]) == disc1_hash && hash_file(argv[8]) == disc2_hash);
      save_dir = save_path;

      /* A member image does not inherit the digest of its playlist set. */
      option_input_profile = "Auto";
      option_writes = "off";
      option_launch = "off";
      struct retro_game_info playlist_member_game = {argv[7], NULL, 0u, NULL};
      last_input_resolution[0] = '\0';
      phase = PHASE_LOAD; assert(retro_load_game(&playlist_member_game));
      assert(strstr(last_input_resolution, "profile=Generic") &&
             strstr(last_input_resolution, "[generic]"));
      retro_unload_game();

      assert(!disk_control.set_initial_image(0u, NULL));
      char *overlong_initial = malloc(4097u);
      assert(overlong_initial);
      memset(overlong_initial, 'x', 4096u); overlong_initial[4096] = '\0';
      assert(!disk_control.set_initial_image(0u, overlong_initial));
      free(overlong_initial);

      assert(disk_control.set_initial_image(1u, argv[8]));
      last_input_resolution[0] = '\0';
      phase = PHASE_LOAD; assert(retro_load_game(&playlist_game));
      assert(strstr(last_input_resolution, "profile=Generated Test Profile") &&
             strstr(last_input_resolution, "[profile]"));
      assert(disk_control.get_num_images() == 2u &&
             disk_control.get_image_index() == 1u);
      assert(disk_control.get_image_path(1u, image_path, sizeof(image_path)) &&
             !strcmp(image_path, argv[8]));
      retro_unload_game();

      /* The request is consumed exactly once. */
      phase = PHASE_LOAD; assert(retro_load_game(&playlist_game));
      assert(disk_control.get_image_index() == 0u);
      assert(disk_control.get_image_label(0u, image_label, sizeof(image_label)) &&
             !strcmp(image_label, "arcdroid-media-disc1"));
      assert(disk_control.get_image_label(1u, image_label, sizeof(image_label)) &&
             !strcmp(image_label, "arcdroid-media-disc2"));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      uint64_t disc1_signature = last_video_signature;
      assert(!disk_control.set_image_index(1u));
      assert(disk_control.set_eject_state(true));
      assert(disk_control.set_image_index(1u));
      last_input_resolution[0] = '\0';
      retro_reset();
      assert(disk_control.get_eject_state() &&
             disk_control.get_image_index() == 1u &&
             strstr(last_input_resolution, "profile=Generated Test Profile") &&
             strstr(last_input_resolution, "[profile]"));
      assert(disk_control.set_eject_state(false));
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      assert(last_video_signature != disc1_signature);

      assert(disk_control.set_eject_state(true));
      assert(disk_control.set_image_index(2u));
      assert(disk_control.get_image_index() == disk_control.get_num_images());
      assert(!disk_control.get_image_path(2u, image_path, sizeof(image_path)));
      assert(disk_control.set_eject_state(false));
      retro_reset();
      assert(disk_control.get_num_images() == 2u &&
             disk_control.get_image_index() == 2u &&
             !disk_control.get_eject_state());

      assert(disk_control.set_eject_state(true));
      assert(disk_control.set_image_index(0u));
      assert(disk_control.add_image_index());
      assert(disk_control.get_num_images() == 3u &&
             !disk_control.set_image_index(2u));
      assert(!disk_control.replace_image_index(2u, &bad_game));
      assert(disk_control.get_num_images() == 2u);
      assert(disk_control.add_image_index());
      struct retro_game_info append_game = {argv[7], NULL, 0u, NULL};
      assert(disk_control.replace_image_index(2u, &append_game));
      assert(disk_control.get_num_images() == 3u);
      assert(!disk_control.replace_image_index(2u, &bad_game));
      assert(disk_control.get_num_images() == 3u);

      assert(disk_control.set_image_index(2u));
      assert(disk_control.replace_image_index(0u, NULL));
      assert(disk_control.get_num_images() == 2u &&
             disk_control.get_image_index() == 1u);
      assert(disk_control.replace_image_index(1u, NULL));
      assert(disk_control.get_num_images() == 1u &&
             disk_control.get_image_index() == 1u);
      assert(disk_control.set_image_index(0u));
      assert(disk_control.add_image_index());
      assert(disk_control.replace_image_index(1u, &append_game));
      assert(disk_control.replace_image_index(1u, NULL));
      assert(disk_control.get_image_index() == 0u);
      assert(disk_control.add_image_index());
      assert(disk_control.replace_image_index(1u, &append_game));
      assert(disk_control.set_eject_state(false));
      retro_reset();
      assert(disk_control.get_num_images() == 2u &&
             disk_control.get_image_index() == 0u &&
             !disk_control.get_eject_state());
      assert(disk_control.set_eject_state(true));
      retro_reset();
      assert(disk_control.get_num_images() == 2u &&
             disk_control.get_image_index() == 0u &&
             disk_control.get_eject_state());
      assert(disk_control.replace_image_index(1u, NULL));
      assert(disk_control.replace_image_index(0u, NULL));
      assert(disk_control.get_num_images() == 0u &&
             disk_control.get_image_index() == 0u &&
             disk_control.get_eject_state());
      assert(disk_control.set_eject_state(false));
      retro_reset();
      assert(disk_control.get_num_images() == 0u &&
             disk_control.get_image_index() == 0u &&
             !disk_control.get_eject_state());
      retro_unload_game();

      /* A pending request is cleared by a failed load. */
      assert(disk_control.set_initial_image(1u, argv[8]));
      phase = PHASE_LOAD; assert(!retro_load_game(&bad_game));
      phase = PHASE_LOAD; assert(retro_load_game(&playlist_game));
      assert(disk_control.get_image_index() == 0u);
      retro_unload_game();

      phase = PHASE_LOAD; assert(retro_load_game(&jfd_game));
      assert(disk_control.get_num_images() == 1u);
      assert(disk_control.get_image_label(0u, image_label, sizeof(image_label)) &&
             !strcmp(image_label, "ArcDroid synthetic JFD"));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 250u; ++frame) retro_run();
      retro_unload_game();

      /* Every accepted short-image geometry is staged as a source-identical
       * prefix followed by zero padding. The warning is one log line and one
       * OSD notification per load, and the content identity remains that of
       * the unpadded source. */
      static const struct {
         const char *name;
         size_t declared;
         const char *leaf;
      } short_valid[] = {
         {"short-family/320k-one-sector.adf", 327680u, "disc-readonly.adf"},
         {"short-family/320k-cap.adf", 327680u, "disc-readonly.adf"},
         {"short-family/640k-l-one-sector.adl", 655360u, "disc-readonly.adl"},
         {"short-family/640k-l-cap.adl", 655360u, "disc-readonly.adl"},
         {"short-family/800k-d-one-sector.adf", 819200u, "disc-readonly.adf"},
         {"short-family/800k-d-cap.adf", 819200u, "disc-readonly.adf"},
         {"short-family/800k-e-one-sector.adf", 819200u, "disc-readonly.adf"},
         {"short-family/800k-e-cap.adf", 819200u, "disc-readonly.adf"},
         {"short-e-missing-1024.adf", 819200u, "disc-readonly.adf"},
         {"short-e-missing-10240.adf", 819200u, "disc-readonly.adf"},
      };
      option_writes = "off";
      for (size_t i = 0u; i < sizeof(short_valid) / sizeof(short_valid[0]); ++i)
      {
         char source[4096], staged[4096];
         int written = snprintf(source, sizeof(source), "%s/%s", argv[11],
                                short_valid[i].name);
         assert(written > 0 && (size_t)written < sizeof(source));
         uint64_t original = hash_file(source);
         unsigned logs = short_warning_log_lines;
         unsigned osd = short_warning_osd_calls;
         struct retro_game_info short_game = {source, NULL, 0u, NULL};
         phase = PHASE_LOAD; assert(retro_load_game(&short_game));
         assert(short_warning_log_lines == logs + 1u &&
                short_warning_osd_calls == osd + 1u);
         staged_media_path(staged, save_path, source, short_valid[i].leaf);
         assert_zero_padded_copy(source, staged, short_valid[i].declared);
         retro_unload_game();
         assert(hash_file(source) == original);
      }

      /* A shadow write survives retained-session reset; read-only staging does
       * not change, and original mode refuses all short images. */
      char short_source[4096], short_staged[4096];
      snprintf(short_source, sizeof(short_source), "%s/short-family/800k-e-one-sector.adf",
               argv[11]);
      struct retro_game_info short_game = {short_source, NULL, 0u, NULL};
      option_writes = "shadow";
      staged_media_path(short_staged, save_path, short_source, "disc.adf");
      (void)remove(short_staged);
      phase = PHASE_LOAD; assert(retro_load_game(&short_game));
      uint64_t shadow_before = hash_file(short_staged);
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 250u; ++frame) retro_run();
      uint64_t shadow_after = hash_file(short_staged);
      assert(shadow_after != shadow_before);
      retro_reset();
      assert(hash_file(short_staged) == shadow_after);
      retro_unload_game();

      option_writes = "off";
      phase = PHASE_LOAD; assert(retro_load_game(&short_game));
      staged_media_path(short_staged, save_path, short_source, "disc-readonly.adf");
      uint64_t readonly_before = hash_file(short_staged);
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 250u; ++frame) retro_run();
      retro_unload_game();
      assert(hash_file(short_staged) == readonly_before);
      option_writes = "original";
      phase = PHASE_LOAD; assert(!retro_load_game(&short_game));
      assert(strstr(last_message, "Short image cannot use original writes"));

      option_writes = "shadow";
      static const char *short_invalid[] = {
         "short-e-missing-10241.adf", "short-e-missing-11264.adf",
         "short-e-missing-513.adf",
         "short-family/320k-cap-plus-one.adf",
         "short-family/320k-cap-plus-sector.adf",
         "short-family/320k-non-aligned.adf",
         "short-family/320k-bad-descriptor.adf",
         "short-family/320k-bad-checksum.adf",
         "short-family/640k-l-cap-plus-one.adl",
         "short-family/640k-l-cap-plus-sector.adl",
         "short-family/640k-l-non-aligned.adl",
         "short-family/640k-l-bad-descriptor.adl",
         "short-family/640k-l-bad-checksum.adl",
         "short-family/800k-d-cap-plus-one.adf",
         "short-family/800k-d-cap-plus-sector.adf",
         "short-family/800k-d-non-aligned.adf",
         "short-family/800k-d-bad-descriptor.adf",
         "short-family/800k-d-bad-checksum.adf",
         "short-family/800k-e-cap-plus-one.adf",
         "short-family/800k-e-cap-plus-sector.adf",
         "short-family/800k-e-non-aligned.adf",
         "short-family/800k-e-bad-descriptor.adf",
         "short-family/800k-e-bad-checksum.adf",
      };
      for (size_t i = 0u; i < sizeof(short_invalid) / sizeof(short_invalid[0]); ++i)
      {
         char source[4096];
         int written = snprintf(source, sizeof(source), "%s/%s", argv[11],
                                short_invalid[i]);
         assert(written > 0 && (size_t)written < sizeof(source));
         struct retro_game_info invalid_short = {source, NULL, 0u, NULL};
         phase = PHASE_LOAD;
         if (retro_load_game(&invalid_short))
         {
            fprintf(stderr, "unexpected short-image acceptance: %s\n", short_invalid[i]);
            abort();
         }
         assert(strstr(last_message, "short by") ||
                strstr(last_message, "unsupported raw image geometry"));
      }

      /* Test-target-only probes verify the helper mount and launch CMOS
       * overlay without adding observability to the shipping core. */
      struct retro_game_info application_game = {argv[12], NULL, 0u, NULL};
      ArcDigest application_digest = digest_file(argv[12]);
      char application_cmos_file[4096];
      snprintf(application_cmos_file, sizeof(application_cmos_file),
               "%s/arcdroid/cmos/A3000.test.%s.bin",
               save_path, application_digest.sha1);
      (void)remove(application_cmos_file);

      /* Auto profiles are keyed by prepared content identity, never by path
       * or disc title. The altered copy leaves its ADFS title bytes untouched
       * while changing one unrelated byte near the end of the image. */
      char application_copy[4096], application_changed[4096];
      snprintf(application_copy, sizeof(application_copy),
               "%s/application-renamed.adl", argv[4]);
      snprintf(application_changed, sizeof(application_changed),
               "%s/application-same-title-changed.adl", argv[4]);
      copy_file(argv[12], application_copy);
      copy_file(argv[12], application_changed);
      FILE *application_changed_file = fopen(application_changed, "rb+");
      assert(application_changed_file &&
             fseek(application_changed_file, -1L, SEEK_END) == 0);
      int application_tail = fgetc(application_changed_file);
      assert(application_tail != EOF &&
             fseek(application_changed_file, -1L, SEEK_END) == 0 &&
             fputc(application_tail ^ 0x5a, application_changed_file) != EOF &&
             !fclose(application_changed_file));
      struct retro_game_info application_copy_game = {
         application_copy, NULL, 0u, NULL
      };
      struct retro_game_info application_changed_game = {
         application_changed, NULL, 0u, NULL
      };
      option_input_profile = "Auto";
      option_writes = "off";
      option_launch = "off";
      last_input_resolution[0] = '\0';
      phase = PHASE_LOAD; assert(retro_load_game(&application_game));
      assert(strstr(last_input_resolution, "profile=Generated Test Profile") &&
             strstr(last_input_resolution, "[profile]"));
      retro_unload_game();
      last_input_resolution[0] = '\0';
      phase = PHASE_LOAD; assert(retro_load_game(&application_copy_game));
      assert(strstr(last_input_resolution, "profile=Generated Test Profile") &&
             strstr(last_input_resolution, "[profile]"));
      retro_unload_game();
      last_input_resolution[0] = '\0';
      phase = PHASE_LOAD; assert(retro_load_game(&application_changed_game));
      assert(strstr(last_input_resolution, "profile=Generic") &&
             strstr(last_input_resolution, "[generic]"));
      retro_unload_game();
      assert(remove(application_copy) == 0 && remove(application_changed) == 0);

      char flush_save_path[4096];
      snprintf(flush_save_path, sizeof(flush_save_path), "%s/flush-save-%ld",
               argv[4], (long)getpid());
      make_directory(flush_save_path);
      save_dir = flush_save_path;

      /* Android's bionic rejects fflush on read-only streams. Use a fresh
       * writable drive-0 shadow so any failure in this launch session is
       * isolated to the read-only helper in drive 1. */
      option_writes = "shadow";
      option_launch = "auto";
      unsigned flush_logs = flush_fault_log_lines;
      unsigned flush_messages = flush_fault_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&application_game));
      assert(arc_engine_test_drive_media(1u, NULL, NULL));
      unsigned completed_before = video_frames - duplicate_video_frames;
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 520u; ++frame) retro_run();
      unsigned launch_completed =
         video_frames - duplicate_video_frames - completed_before;
      if (launch_completed < 500u)
         fprintf(stderr, "launch-helper completed fields=%u flush logs/OSD=%u/%u fault=\"%s\"\n",
                 launch_completed, flush_fault_log_lines - flush_logs,
                 flush_fault_osd_calls - flush_messages, last_flush_fault);
      assert(launch_completed >= 500u);
      assert(flush_fault_log_lines == flush_logs &&
             flush_fault_osd_calls == flush_messages);
      uint64_t launch_signature = last_video_signature;
      retro_unload_game();

      /* With the same source mounted directly and read-only, complete the
       * same field count and retain the established final-signature equality. */
      option_writes = "off";
      option_launch = "off";
      phase = PHASE_LOAD; assert(retro_load_game(&application_game));
      assert(!arc_engine_test_drive_media(1u, NULL, NULL));
      completed_before = video_frames - duplicate_video_frames;
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 520u; ++frame) retro_run();
      unsigned direct_completed =
         video_frames - duplicate_video_frames - completed_before;
      for (unsigned frame = 0u;
           frame < 200u && direct_completed >= 500u &&
           last_video_signature != launch_signature;
           ++frame)
      {
         retro_run();
         direct_completed = video_frames - duplicate_video_frames - completed_before;
      }
      assert(direct_completed >= 500u && last_video_signature == launch_signature);
      assert(flush_fault_log_lines == flush_logs &&
             flush_fault_osd_calls == flush_messages);
      retro_unload_game();

      /* Write-policy-off also opens an ordinary generated ADF read-only. */
      option_writes = "off";
      option_launch = "off";
      struct retro_game_info readonly_adf_game = {argv[3], NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&readonly_adf_game));
      completed_before = video_frames - duplicate_video_frames;
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 520u; ++frame) retro_run();
      assert(video_frames - duplicate_video_frames - completed_before >= 500u);
      assert(flush_fault_log_lines == flush_logs &&
             flush_fault_osd_calls == flush_messages);
      retro_unload_game();

      /* A real flush error on a writable shadow remains a contained fatal. */
      option_writes = "shadow";
      phase = PHASE_LOAD; assert(retro_load_game(&readonly_adf_game));
      fault_flush_substring = "/disc.adf";
      phase = PHASE_RUN; retro_run();
      assert(!fault_flush_substring &&
             flush_fault_log_lines == flush_logs + 1u &&
             flush_fault_osd_calls == flush_messages + 1u &&
             strstr(last_flush_fault, "Unable to flush the staged floppy image"));
      retro_run();
      assert(flush_fault_log_lines == flush_logs + 1u &&
             flush_fault_osd_calls == flush_messages + 1u);
      retro_unload_game();

      save_dir = save_path;
      option_writes = "off";
      option_launch = "auto";
      unsigned decisions = launch_log_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&application_game));
      assert(launch_log_lines == decisions + 1u &&
             strstr(last_launch_log, "Launch:") &&
             strstr(last_launch_log, "decision=!Demo") &&
             strstr(last_launch_log, "reason=single application"));
      bool write_protected = false;
      char mounted_digest[41];
      assert(arc_engine_test_drive_media(1u, &write_protected, mounted_digest));
      assert(write_protected);
      char helper_file[4096];
      snprintf(helper_file, sizeof(helper_file), "%s/arcdroid/launch/%s.adl",
               save_path, application_digest.sha1);
      ArcDigest helper_digest = digest_file(helper_file);
      assert(!strcmp(mounted_digest, helper_digest.sha1));
      uint8_t machine_cmos[ARC_CMOS_MACHINE_SIZE];
      assert(arc_engine_test_cmos_image(machine_cmos));
      assert((machine_cmos[75] & 7u) == 1u && (machine_cmos[199] & 7u) == 2u);

      /* The helper-only overlay never enters either persistent surface. A
       * separate guest-side CMOS mutation must survive the same sync. */
      assert(arc_engine_test_cmos_write(16u + 42u, 0x5au));
      phase = PHASE_RUN;
      retro_run();
      const uint8_t *launch_save_ram =
         retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
      assert(launch_save_ram && !memcmp(launch_save_ram, "ARCCMOS2", 8u));
      assert((launch_save_ram[16u + 59u] & 7u) == 0u &&
             (launch_save_ram[16u + 183u] & 7u) == 1u &&
             launch_save_ram[16u + 42u] == 0x5au);
      uint8_t launch_cmos_file[ARC_CMOS_RECORD_SIZE];
      read_exact(application_cmos_file, launch_cmos_file,
                 sizeof(launch_cmos_file));
      assert(!memcmp(launch_cmos_file, launch_save_ram,
                     sizeof(launch_cmos_file)));

      /* Eject and replace within the first 200 frontend frames. The helper is
       * retained, hidden from disk control, and no launch decision runs until
       * the explicit machine restart. */
      char ambiguous_path[4096];
      snprintf(ambiguous_path, sizeof(ambiguous_path),
               "%s/launch-family/ambiguous-l.adl", argv[11]);
      struct retro_game_info ambiguous_replacement = {ambiguous_path, NULL, 0u, NULL};
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 50u; ++frame) retro_run();
      assert(disk_control.get_num_images() == 1u && disk_control.set_eject_state(true));
      assert(arc_engine_test_drive_media(1u, &write_protected, mounted_digest));
      assert(disk_control.replace_image_index(0u, &ambiguous_replacement));
      assert(disk_control.set_image_index(0u) && disk_control.set_eject_state(false));
      for (unsigned frame = 50u; frame < 200u; ++frame) retro_run();
      assert(launch_log_lines == decisions + 1u &&
             arc_engine_test_drive_media(1u, &write_protected, mounted_digest));
      retro_reset();
      assert(launch_log_lines == decisions + 2u &&
             strstr(last_launch_log, "reason=ambiguous") &&
             !arc_engine_test_drive_media(1u, NULL, NULL));
      option_launch = "off";
      retro_reset();
      assert(!arc_engine_test_drive_media(1u, NULL, NULL));
      assert(arc_engine_test_cmos_image(machine_cmos));
      assert((machine_cmos[75] & 7u) == 0u && (machine_cmos[199] & 7u) == 1u);
      retro_unload_game();
      launch_save_ram = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
      assert(launch_save_ram && (launch_save_ram[16u + 59u] & 7u) == 0u &&
             (launch_save_ram[16u + 183u] & 7u) == 1u &&
             launch_save_ram[16u + 42u] == 0x5au);
      read_exact(application_cmos_file, launch_cmos_file,
                 sizeof(launch_cmos_file));
      assert(!memcmp(launch_cmos_file, launch_save_ram,
                     sizeof(launch_cmos_file)));

      /* R-1 diagnostics are emitted once for each omitted sibling during a
       * load. The escaped log rendering must preserve the identity of the
       * raw leaf while the helper remains mounted for the valid target. */
      option_launch = "auto";
      char sibling_pipe_path[4096];
      snprintf(sibling_pipe_path, sizeof(sibling_pipe_path),
               "%s/launch-family/sibling-pipe-l.adl", argv[11]);
      struct retro_game_info sibling_pipe_game = {
         sibling_pipe_path, NULL, 0u, NULL
      };
      unsigned skipped_logs = skipped_sibling_log_lines;
      unsigned skipped_names = skipped_sibling_names;
      phase = PHASE_LOAD; assert(retro_load_game(&sibling_pipe_game));
      assert(arc_engine_test_drive_media(1u, &write_protected, mounted_digest) &&
             strstr(last_launch_log, "decision=!Qaventra") &&
             skipped_sibling_log_lines == skipped_logs + 1u &&
             (skipped_sibling_names & ~skipped_names) == 1u);
      retro_unload_game();

      char duplicate_siblings_path[4096];
      snprintf(duplicate_siblings_path, sizeof(duplicate_siblings_path),
               "%s/launch-family/duplicate-siblings-l.adl", argv[11]);
      struct retro_game_info duplicate_siblings_game = {
         duplicate_siblings_path, NULL, 0u, NULL
      };
      skipped_logs = skipped_sibling_log_lines;
      skipped_names = skipped_sibling_names;
      phase = PHASE_LOAD; assert(retro_load_game(&duplicate_siblings_game));
      assert(arc_engine_test_drive_media(1u, &write_protected, mounted_digest) &&
             strstr(last_launch_log, "decision=!Qaventra") &&
             skipped_sibling_log_lines == skipped_logs + 2u &&
             (skipped_sibling_names & ~skipped_names) == 6u);
      retro_unload_game();

      /* Explicit ordinals override a non-zero FileCore boot option, refuse a
       * non-application/beyond-list choice, and bind the resolved raw target. */
      char explicit_source[4096], explicit_staged[4096], explicit_binding[4096];
      snprintf(explicit_source, sizeof(explicit_source),
               "%s/launch-family/explicit-d.adf", argv[11]);
      struct retro_game_info explicit_game = {explicit_source, NULL, 0u, NULL};
      option_writes = "shadow";
      staged_media_path(explicit_staged, save_path, explicit_source, "disc.adf");
      ArcDigest explicit_digest = digest_file(explicit_source);
      snprintf(explicit_binding, sizeof(explicit_binding),
               "%s/arcdroid/launch/%s.bind", save_path, explicit_digest.sha1);
      (void)remove(explicit_staged);
      (void)remove(explicit_binding);
      unsigned warnings = launch_warning_osd_calls;
      option_launch = "app1";
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "candidate 1 has no !Run"));
      option_launch = "app2";
      retro_reset();
      assert(arc_engine_test_drive_media(1u, &write_protected, mounted_digest) &&
             strstr(last_launch_log, "boot=3") &&
             strstr(last_launch_log, "decision=!Demo") &&
             strstr(last_launch_log, "disc !Boot skipped"));
      uint8_t binding_text[ARC_LAUNCH_BINDING_MAX_SIZE];
      size_t binding_size = read_bounded(explicit_binding, binding_text,
                                         sizeof(binding_text));
      ArcLaunchBinding parsed_binding;
      assert(arc_launch_binding_parse(binding_text, binding_size,
                                      &parsed_binding) &&
             parsed_binding.version == 2u && parsed_binding.ordinal == 1u &&
             parsed_binding.target.length == 5u &&
             !memcmp(parsed_binding.target.bytes, "!Demo", 5u));
      retro_unload_game();

      /* An unchanged ordinal/target accepts the existing binding without
       * rewriting it. */
      option_launch = "app2";
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(arc_engine_test_drive_media(1u, NULL, NULL) &&
             strstr(last_launch_log, "decision=!Demo"));
      uint8_t binding_check[ARC_LAUNCH_BINDING_MAX_SIZE];
      size_t binding_check_size = read_bounded(explicit_binding, binding_check,
                                               sizeof(binding_check));
      assert(binding_check_size == binding_size &&
             !memcmp(binding_check, binding_text, binding_size));
      retro_unload_game();

      /* A persistent shadow edit removes the earlier candidate. The same raw
       * target moved from ordinal 2 to ordinal 1 and remains accepted, without
       * rewriting the old ordinal or candidate fingerprint. */
      remove_first_new_directory_entry(explicit_staged);
      option_launch = "app1";
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(arc_engine_test_drive_media(1u, NULL, NULL) &&
             strstr(last_launch_log, "decision=!Demo"));
      binding_check_size = read_bounded(explicit_binding, binding_check,
                                        sizeof(binding_check));
      assert(binding_check_size == binding_size &&
             !memcmp(binding_check, binding_text, binding_size));
      retro_unload_game();

      /* The old ordinal now resolves to !Other. Same ordinal/different target
       * refuses, and the existing binding is preserved. */
      option_launch = "app2";
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "binding changed target"));
      binding_check_size = read_bounded(explicit_binding, binding_check,
                                        sizeof(binding_check));
      assert(binding_check_size == binding_size &&
             !memcmp(binding_check, binding_text, binding_size));
      option_launch = "off";
      retro_reset();
      option_launch = "app2";
      retro_reset();
      assert(arc_engine_test_drive_media(1u, &write_protected, mounted_digest) &&
             strstr(last_launch_log, "decision=!Other"));
      size_t other_binding_size = read_bounded(explicit_binding, binding_text,
                                               sizeof(binding_text));
      assert(arc_launch_binding_parse(binding_text, other_binding_size,
                                      &parsed_binding) &&
             parsed_binding.version == 2u && parsed_binding.ordinal == 1u &&
             parsed_binding.target.length == 6u &&
             !memcmp(parsed_binding.target.bytes, "!Other", 6u));
      retro_unload_game();

      /* A different ordinal and different raw target is also refused. An
       * actual reselection is the only event that replaces the valid binding. */
      option_launch = "app1";
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "binding changed target"));
      option_launch = "off";
      retro_reset();
      option_launch = "app1";
      retro_reset();
      assert(arc_engine_test_drive_media(1u, NULL, NULL) &&
             strstr(last_launch_log, "decision=!Demo"));
      binding_size = read_bounded(explicit_binding, binding_text,
                                  sizeof(binding_text));
      assert(arc_launch_binding_parse(binding_text, binding_size,
                                      &parsed_binding) &&
             parsed_binding.version == 2u && parsed_binding.ordinal == 0u &&
             parsed_binding.target.length == 5u &&
             !memcmp(parsed_binding.target.bytes, "!Demo", 5u));
      retro_unload_game();

      /* Legacy v1 is accepted byte-for-byte and is migrated only after an
       * actual option reselection. */
      static const uint8_t legacy_binding[] =
         "app1\n!Demo\n0000000000000000000000000000000000000000\n";
      write_exact(explicit_binding, legacy_binding, sizeof(legacy_binding) - 1u);
      option_launch = "app1";
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(arc_engine_test_drive_media(1u, NULL, NULL));
      binding_check_size = read_bounded(explicit_binding, binding_check,
                                        sizeof(binding_check));
      assert(binding_check_size == sizeof(legacy_binding) - 1u &&
             !memcmp(binding_check, legacy_binding, binding_check_size));
      option_launch = "off";
      retro_reset();
      option_launch = "app1";
      retro_reset();
      assert(arc_engine_test_drive_media(1u, NULL, NULL));
      binding_size = read_bounded(explicit_binding, binding_text,
                                  sizeof(binding_text));
      assert(arc_launch_binding_parse(binding_text, binding_size,
                                      &parsed_binding) &&
             parsed_binding.version == 2u && parsed_binding.ordinal == 0u &&
             parsed_binding.target.length == 5u &&
             !memcmp(parsed_binding.target.bytes, "!Demo", 5u));
      retro_unload_game();

      /* Empty, malformed, and trailing-content bindings all refuse and remain
       * byte-for-byte unchanged. */
      static const uint8_t malformed_v2[] =
         "v2\n1\n2144656d6f\n000000000000000000000000000000000000000g\n";
      static const uint8_t trailing_v2[] =
         "v2\n1\n2144656d6f\n0000000000000000000000000000000000000000\nX";
      static const uint8_t trailing_v1[] =
         "app1\n!Demo\n0000000000000000000000000000000000000000\nX";
      struct InvalidBinding {
         const uint8_t *data;
         size_t size;
      } invalid_bindings[] = {
         {(const uint8_t *)"", 0u},
         {malformed_v2, sizeof(malformed_v2) - 1u},
         {trailing_v2, sizeof(trailing_v2) - 1u},
         {trailing_v1, sizeof(trailing_v1) - 1u},
      };
      for (size_t invalid = 0u;
           invalid < sizeof(invalid_bindings) / sizeof(invalid_bindings[0]);
           ++invalid)
      {
         write_exact(explicit_binding, invalid_bindings[invalid].data,
                     invalid_bindings[invalid].size);
         warnings = launch_warning_osd_calls;
         option_launch = "app1";
         phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
         assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
                launch_warning_osd_calls == warnings + 1u &&
                strstr(last_launch_warning, "binding is invalid"));
         binding_check_size = read_bounded(explicit_binding, binding_check,
                                           sizeof(binding_check));
         assert(binding_check_size == invalid_bindings[invalid].size &&
                !memcmp(binding_check, invalid_bindings[invalid].data,
                        binding_check_size));
         retro_unload_game();
      }

      /* Read and directory-listing failures are not interpreted as absence;
       * neither one modifies the existing valid binding. */
      write_exact(explicit_binding, binding_text, binding_size);
      fault_read_substring = ".bind";
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(!fault_read_substring &&
             !arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "binding could not be read"));
      binding_check_size = read_bounded(explicit_binding, binding_check,
                                        sizeof(binding_check));
      assert(binding_check_size == binding_size &&
             !memcmp(binding_check, binding_text, binding_size));
      retro_unload_game();

      fault_opendir_substring = "/arcdroid/launch";
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(!fault_opendir_substring &&
             !arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "binding directory could not be listed"));
      binding_check_size = read_bounded(explicit_binding, binding_check,
                                        sizeof(binding_check));
      assert(binding_check_size == binding_size &&
             !memcmp(binding_check, binding_text, binding_size));
      retro_unload_game();

      fault_closedir_substring = "/arcdroid/launch";
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(!fault_closedir_substring &&
             !arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "binding directory could not be listed"));
      binding_check_size = read_bounded(explicit_binding, binding_check,
                                        sizeof(binding_check));
      assert(binding_check_size == binding_size &&
             !memcmp(binding_check, binding_text, binding_size));
      retro_unload_game();

      /* Atomic publish failure during reselection preserves the existing
       * binding and refuses the launch. */
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(arc_engine_test_drive_media(1u, NULL, NULL));
      option_launch = "app2";
      fault_write_substring = ".tmp";
      warnings = launch_warning_osd_calls;
      retro_reset();
      assert(!fault_write_substring &&
             !arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "binding could not be published"));
      binding_check_size = read_bounded(explicit_binding, binding_check,
                                        sizeof(binding_check));
      assert(binding_check_size == binding_size &&
             !memcmp(binding_check, binding_text, binding_size));
      retro_unload_game();

      option_launch = "app1";
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(arc_engine_test_drive_media(1u, NULL, NULL));
      option_launch = "app2";
      fault_rename_substring = ".previous";
      warnings = launch_warning_osd_calls;
      retro_reset();
      assert(!fault_rename_substring &&
             !arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "binding could not be published"));
      binding_check_size = read_bounded(explicit_binding, binding_check,
                                        sizeof(binding_check));
      assert(binding_check_size == binding_size &&
             !memcmp(binding_check, binding_text, binding_size));
      retro_unload_game();

      /* Only a successful directory listing can establish absence. A listing
       * failure creates nothing; the next successful load publishes v2. */
      assert(remove(explicit_binding) == 0);
      option_launch = "app1";
      fault_opendir_substring = "/arcdroid/launch";
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(!fault_opendir_substring &&
             !arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "binding directory could not be listed"));
      struct stat binding_status;
      assert(stat(explicit_binding, &binding_status) != 0);
      retro_unload_game();
      phase = PHASE_LOAD; assert(retro_load_game(&explicit_game));
      assert(arc_engine_test_drive_media(1u, NULL, NULL));
      binding_size = read_bounded(explicit_binding, binding_text,
                                  sizeof(binding_text));
      assert(arc_launch_binding_parse(binding_text, binding_size,
                                      &parsed_binding) &&
             parsed_binding.version == 2u && parsed_binding.ordinal == 0u &&
             parsed_binding.target.length == 5u &&
             !memcmp(parsed_binding.target.bytes, "!Demo", 5u));
      retro_unload_game();
      assert(remove(explicit_binding) == 0);

      char booted_path[4096];
      snprintf(booted_path, sizeof(booted_path),
               "%s/launch-family/boot-3-l.adl", argv[11]);
      struct retro_game_info booted_game = {booted_path, NULL, 0u, NULL};
      option_writes = "off";
      option_launch = "app1";
      phase = PHASE_LOAD; assert(retro_load_game(&booted_game));
      assert(arc_engine_test_drive_media(1u, NULL, NULL) &&
             strstr(last_launch_log, "boot=3") &&
             strstr(last_launch_log, "disc !Boot skipped"));
      retro_reset();
      assert(arc_engine_test_drive_media(1u, &write_protected, mounted_digest) &&
             write_protected);
      retro_unload_game();

      option_launch = "app8";
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&application_game));
      assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "candidate 8 does not exist"));
      retro_unload_game();

      option_launch = "auto";
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&booted_game));
      assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings &&
             strstr(last_launch_log, "reason=disc boots itself"));
      retro_unload_game();

      char no_run_path[4096];
      snprintf(no_run_path, sizeof(no_run_path),
               "%s/launch-family/no-run-d.adf", argv[11]);
      struct retro_game_info no_run_game = {no_run_path, NULL, 0u, NULL};
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&no_run_game));
      assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings &&
             strstr(last_launch_log, "reason=no applications"));
      retro_unload_game();

      option_launch = "auto";
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&ambiguous_replacement));
      assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             !strstr(last_launch_warning, "1 !Docs") &&
             strstr(last_launch_warning, "2 !Foo") &&
             strstr(last_launch_warning, "3 !Bar"));
      retro_unload_game();

      char incomplete_path[4096];
      snprintf(incomplete_path, sizeof(incomplete_path),
               "%s/launch-family/incomplete-l.adl", argv[11]);
      struct retro_game_info incomplete_game = {incomplete_path, NULL, 0u, NULL};
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&incomplete_game));
      assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "inspection incomplete") &&
             strstr(last_launch_warning, "1 !Broken"));
      retro_unload_game();

      char attr_name_path[4096];
      snprintf(attr_name_path, sizeof(attr_name_path),
               "%s/launch-family/attr-88-d.adf", argv[11]);
      struct retro_game_info attr_name_game = {attr_name_path, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&attr_name_game));
      assert(arc_engine_test_drive_media(1u, &write_protected, mounted_digest) &&
             write_protected && strstr(last_launch_log, "ignored-attribute-bits=yes"));
      retro_unload_game();

      char top_bit_path[4096];
      snprintf(top_bit_path, sizeof(top_bit_path),
               "%s/launch-family/top-bit-d.adf", argv[11]);
      struct retro_game_info top_bit_game = {top_bit_path, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&top_bit_game));
      assert(arc_engine_test_drive_media(1u, &write_protected, mounted_digest) &&
             write_protected && strstr(last_launch_log, "decision=!Z\\xF6rvex") &&
             strstr(last_launch_log, "reason=single application"));
      retro_unload_game();

      char unsafe_name_path[4096];
      snprintf(unsafe_name_path, sizeof(unsafe_name_path),
               "%s/launch-family/unsafe-disc-name-l.adl", argv[11]);
      struct retro_game_info unsafe_name_game = {unsafe_name_path, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&unsafe_name_game));
      assert(strstr(last_launch_log, "disc=\"Bad\\x0AName\"") &&
             !arc_engine_test_drive_media(1u, NULL, NULL));
      retro_unload_game();

      /* JFD remains drive 0 while the independently discovered raw helper is
       * drive 1. The helper is never reflected in the frontend disk count. */
      char launch_jfd_path[4096];
      snprintf(launch_jfd_path, sizeof(launch_jfd_path),
               "%s/launch-family/single-l.jfd", argv[11]);
      struct retro_game_info launch_jfd_game = {launch_jfd_path, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&launch_jfd_game));
      assert(disk_control.get_num_images() == 1u &&
             arc_engine_test_drive_media(1u, &write_protected, mounted_digest) &&
             write_protected);
      retro_unload_game();

      /* Repeated helper preparation reuses the deterministic published image
       * and closes every VFS handle on teardown. */
      option_writes = "off";
      uint64_t stable_helper = hash_file(helper_file);
      decisions = launch_log_lines;
      for (unsigned cycle = 0u; cycle < 100u; ++cycle)
      {
         phase = PHASE_LOAD; assert(retro_load_game(&application_game));
         assert(arc_engine_test_drive_media(1u, &write_protected, mounted_digest));
         retro_unload_game();
         assert(open_files == 0u && open_dirs == 0u && hash_file(helper_file) == stable_helper);
      }
      assert(launch_log_lines == decisions + 100u);
      option_launch = "auto";
      option_writes = "shadow";
      assert(hash_file(argv[7]) == disc1_hash && hash_file(argv[8]) == disc2_hash);
   }
   retro_deinit();
   can_dupe = false;
   assert(open_files == 0u && open_dirs == 0u);
   assert(getcwd(cwd_after, sizeof(cwd_after)) && strcmp(cwd, cwd_after) == 0);
   assert(hash_file(argv[3]) == source_hash);
   assert(modified_shadow_count(save_path, source_hash, changed_source_hash) >= 2u);
   assert(video_frames >= 10200u + av_probe_frames && audio_frames > 0u && nonzero_audio_samples > 0u);
   assert(heap_maximum_delta <= 65536u);
   assert(redundant_geometry_changes == 0u);
   assert(av_notifications > 0u);
   assert(video_signature_count >= 1u && reported_rate_count >= 1u);
   assert(flush_fault_log_lines == 1u && flush_fault_osd_calls == 1u);
   assert(input_observed);
   assert(vidc_osd_ext_calls + vidc_osd_legacy_calls == persistent_osd_before + 2u);
   assert(saw_audio_counters);
   assert(set_variable_calls == 0u);
   assert(dlclose(module) == 0);
   printf("libretro_harness: payload signatures %016llx/%016llx; "
          "100 cycles, %u frames, %llu audio frames, "
          "%llu nonzero samples, %u frame signatures, %u reported rates, "
          "%u geometry changes (0 redundant), A/V windows actual %llu expected %llu "
          "(max delta %llu), ",
          (unsigned long long)before_input_signature,
          (unsigned long long)changed_signature,
          video_frames,
          (unsigned long long)audio_frames,
          (unsigned long long)nonzero_audio_samples, video_signature_count,
          reported_rate_count, geometry_changes,
          (unsigned long long)measured_actual_total,
          (unsigned long long)measured_expected_total,
          (unsigned long long)measured_max_difference);
#if defined(ARCDROID_ADDRESS_SANITIZER)
   printf("heap accounting disabled under ASan, 0 leaked VFS handles\n");
#else
   printf("heap peak delta %zu bytes (limit 65536), 0 leaked VFS handles\n",
          heap_maximum_delta);
#endif
   printf("reported rates:");
   for (unsigned i = 0; i < reported_rate_count; ++i) printf(" %.6f", reported_rates[i]);
   printf("\n");
   printf("guest input signature change: %s\n", input_observed ? "observed" : "not observed");
   printf("audio queue counters: underrun=%llu overrun=%llu\n",
          (unsigned long long)sampled_underruns,
          (unsigned long long)sampled_overruns);
   printf("negotiated duplicate frames: %u\n", duplicate_video_frames);
   printf("VIDC messages: OSD ext/legacy=%u/%u, transient info logs=%u, warning logs=%u\n",
          vidc_osd_ext_calls, vidc_osd_legacy_calls,
          vidc_info_log_lines, vidc_warning_log_lines);
   return 0;
}
