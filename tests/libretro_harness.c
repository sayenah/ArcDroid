/* ArcDroid libretro lifecycle/API harness. GPL-2.0-only. */
#include "libretro.h"
#include "arcdroid_core.h"

#include <assert.h>
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
static char last_message[512];
static bool invalid_vidc_message;
static struct retro_keyboard_callback keyboard;
static bool joypad_a;
static int mouse_x, mouse_y;
static unsigned mouse_buttons;
static bool pointer_pressed;
static int pointer_x, pointer_y;
static bool real_boot_mode;
static char evidence_path[4096];
static FILE *evidence_log;
static unsigned fdc_command_count;
static unsigned fdc_read_count;
static unsigned kart_traffic_count;
static unsigned cmos_read_count;
static unsigned cmos_write_count;
static unsigned real_boot_frames = 3000u;
static struct retro_disk_control_ext_callback disk_control;
static bool can_dupe;
static bool saw_input_descriptors;
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
static const char *option_writes = "shadow";
static bool saw_adfs_advisory;
static bool saw_audio_counters;
static uint64_t logged_underruns;
static uint64_t logged_overruns;
static unsigned launch_options_seen;

static size_t heap_bytes_in_use(void)
{
#if defined(__APPLE__)
   malloc_statistics_t statistics;
   malloc_zone_statistics(malloc_default_zone(), &statistics);
   return statistics.size_in_use;
#elif defined(__GLIBC__)
   return (size_t)mallinfo2().uordblks;
#else
#error "The lifecycle allocation check needs platform malloc statistics"
#endif
}

struct retro_vfs_file_handle { FILE *file; char *path; };
struct retro_vfs_dir_handle { DIR *dir; struct dirent *entry; char path[4096]; };

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
   const char *m = (mode & RETRO_VFS_FILE_ACCESS_WRITE) ?
      ((mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) ? "rb+" : "wb") : "rb";
   FILE *f = fopen(path, m);
   if (!f) return NULL;
   struct retro_vfs_file_handle *h = calloc(1u, sizeof(*h));
   assert(h);
   h->file = f; h->path = copy_string(path); ++open_files;
   if ((mode & RETRO_VFS_FILE_ACCESS_WRITE) && strstr(path, "/arcdroid/cmos/"))
      ++cmos_write_opens;
   return h;
}
static int vfs_close(struct retro_vfs_file_handle *h)
{
   int result = fclose(h->file); free(h->path); free(h); --open_files; return result;
}
static int64_t vfs_size(struct retro_vfs_file_handle *h)
{
   long at = ftell(h->file); assert(at >= 0); assert(fseek(h->file, 0, SEEK_END) == 0);
   long size = ftell(h->file); assert(size >= 0); assert(fseek(h->file, at, SEEK_SET) == 0); return size;
}
static int64_t vfs_tell(struct retro_vfs_file_handle *h) { return ftell(h->file); }
static int64_t vfs_seek(struct retro_vfs_file_handle *h, int64_t o, int origin) { return fseek(h->file, (long)o, origin) ? -1 : ftell(h->file); }
static int64_t vfs_read(struct retro_vfs_file_handle *h, void *s, uint64_t n) { return (int64_t)fread(s, 1u, (size_t)n, h->file); }
static int64_t vfs_write(struct retro_vfs_file_handle *h, const void *s, uint64_t n) { return (int64_t)fwrite(s, 1u, (size_t)n, h->file); }
static int vfs_flush(struct retro_vfs_file_handle *h) { return fflush(h->file); }
static int vfs_remove(const char *p) { return remove(p); }
static int vfs_rename(const char *a, const char *b) { return rename(a, b); }
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
   (void)hidden; DIR *d = opendir(p); if (!d) return NULL;
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
static int vfs_closedir(struct retro_vfs_dir_handle *h) { int r = closedir(h->dir); free(h); --open_dirs; return r; }

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
      logged_underruns = underruns;
      logged_overruns = overruns;
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
         assert(count == 7u);
         return true;
      }
      case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
         assert(phase == PHASE_INIT);
         disk_control = *(struct retro_disk_control_ext_callback *)data;
         return true;
      case RETRO_ENVIRONMENT_GET_CAN_DUPE:
         assert(phase == PHASE_INIT); *(bool *)data = can_dupe; return true;
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
         assert(phase == PHASE_LOAD);
         const struct retro_controller_info *ports = data;
         assert(ports[0].num_types == 2u && ports[1].num_types == 2u);
         return true;
      }
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      {
         assert(phase == PHASE_LOAD);
         const struct retro_input_descriptor *descriptors = data;
         bool port0 = false, port1 = false;
         for (; descriptors->description; ++descriptors)
         {
            if (descriptors->port == 0u) port0 = true;
            if (descriptors->port == 1u) port1 = true;
         }
         saw_input_descriptors = port0 && port1;
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
         if (!strcmp(variable->key, "arcdroid_machine")) variable->value = option_machine;
         else if (!strcmp(variable->key, "arcdroid_cpu")) variable->value = option_cpu;
         else if (!strcmp(variable->key, "arcdroid_memory")) variable->value = option_memory;
         else if (!strcmp(variable->key, "arcdroid_os")) variable->value = option_os;
         else if (!strcmp(variable->key, "arcdroid_joystick_if")) variable->value = option_joystick;
         else if (!strcmp(variable->key, "arcdroid_pointer_mode")) variable->value = option_pointer;
         else if (!strcmp(variable->key, "arcdroid_disk_writes")) variable->value = option_writes;
         else return false;
         return true;
      }
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
         if (strstr(last_message, "VIDC")) invalid_vidc_message = true;
         return true;
      case RETRO_ENVIRONMENT_SET_MESSAGE:
         snprintf(last_message, sizeof(last_message), "%s", ((struct retro_message *)data)->msg);
         if (strstr(last_message, "VIDC")) invalid_vidc_message = true;
         return true;
      default: return false;
   }
}
static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
   assert((data || can_dupe) && w > 0u && w <= 4096u && h > 0u && h <= 2048u && pitch >= w * 4u);
   if (!data) { ++video_frames; return; }
   assert(w == geometry_width && h == geometry_height);
   if (measure_av)
   {
      measured_expected_audio += 48000.0L / (long double)current_reported_rate;
      ++measured_video_frames;
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
   last_video_signature = hash;
   video_trace_signature ^= hash;
   video_trace_signature *= UINT64_C(1099511628211);
   ++video_frames;
   if (real_boot_mode && (video_frames == 100u || video_frames == 300u ||
                          video_frames == 600u || video_frames == 1000u ||
                          video_frames == 1500u || video_frames == 2000u ||
                          video_frames == real_boot_frames))
   {
      char path[4096];
      snprintf(path, sizeof(path), "%s/field-%04u.ppm", evidence_path, video_frames);
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
   (void)i;
   if (d == RETRO_DEVICE_JOYPAD && p == joypad_port && id == RETRO_DEVICE_ID_JOYPAD_A) return joypad_a;
   if (d == RETRO_DEVICE_MOUSE)
   {
      if (id == RETRO_DEVICE_ID_MOUSE_X) { int value = mouse_x; mouse_x = 0; return (int16_t)value; }
      if (id == RETRO_DEVICE_ID_MOUSE_Y) { int value = mouse_y; mouse_y = 0; return (int16_t)value; }
      if (id == RETRO_DEVICE_ID_MOUSE_LEFT) return (mouse_buttons & 1u) != 0u;
      if (id == RETRO_DEVICE_ID_MOUSE_RIGHT) return (mouse_buttons & 2u) != 0u;
   }
   if (d == RETRO_DEVICE_POINTER)
   {
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

static uint64_t hash_file(const char *path)
{
   FILE *f = fopen(path, "rb"); assert(f);
   uint64_t hash = UINT64_C(1469598103934665603);
   int byte;
   while ((byte = fgetc(f)) != EOF) { hash ^= (uint8_t)byte; hash *= UINT64_C(1099511628211); }
   assert(!ferror(f) && !fclose(f)); return hash;
}

static void make_sized_file(const char *path, size_t size)
{
   FILE *file = fopen(path, "wb");
   assert(file && size > 0u);
   assert(fseek(file, (long)(size - 1u), SEEK_SET) == 0);
   assert(fputc(0, file) != EOF && !fclose(file));
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

#define LOAD(name) do { *(void **)(&name) = dlsym(module, #name); assert(name); } while (0)
int main(int argc, char **argv)
{
   assert(argc == 5 || (argc == 6 && strcmp(argv[5], "--real-boot") == 0));
   bool real_boot = argc == 6;
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

   phase = PHASE_ENV; retro_set_environment(environment);
   retro_set_video_refresh(video); retro_set_audio_sample_batch(audio);
   retro_set_input_poll(poll); retro_set_input_state(input);
   phase = PHASE_INIT; retro_init();
   assert(disk_control.set_eject_state && disk_control.set_image_index &&
          disk_control.replace_image_index && disk_control.set_initial_image);
   assert(retro_serialize_size() == 0u);
   struct retro_game_info game = {argv[3], NULL, 0u, NULL};
   uint64_t source_hash = hash_file(argv[3]);

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
      for (unsigned frame = 0; frame < real_boot_frames; ++frame) retro_run();
      retro_unload_game();
      retro_deinit();
      assert(!fclose(evidence_log));
      evidence_log = NULL;
      assert(open_files == 0u && open_dirs == 0u && hash_file(argv[3]) == source_hash);
      assert(video_frames == real_boot_frames && audio_frames > 0u && video_signature_count > 1u);
      assert(dlclose(module) == 0);
      printf("real_boot_harness: %s; %u frames, %u signatures, trace %016llx, "
             "final %016llx, "
             "%llu audio frames, %llu nonzero samples; FDC commands %u (reads %u), "
             "KART traffic %u, CMOS reads %u writes %u; evidence %s\n",
             firmware_message, real_boot_frames, video_signature_count,
             (unsigned long long)video_trace_signature,
             (unsigned long long)last_video_signature,
             (unsigned long long)audio_frames,
             (unsigned long long)nonzero_audio_samples,
             fdc_command_count, fdc_read_count, kart_traffic_count,
             cmos_read_count, cmos_write_count, evidence_path);
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

   char high_adl[4096]; snprintf(high_adl, sizeof(high_adl), "%s/high-density.adl", argv[4]);
   make_sized_file(high_adl, 1638400u);
   struct retro_game_info high_game = {high_adl, NULL, 0u, NULL};
   phase = PHASE_LOAD; assert(!retro_load_game(&high_game));
   assert(strstr(last_message, "unsupported by the Slice 1"));

   option_joystick = "A3010";
   phase = PHASE_LOAD; assert(!retro_load_game(&game));
   assert(strstr(last_message, "A3010 joystick interface"));
   option_joystick = "Keyboard";

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
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   assert(saw_input_descriptors && option_query_count >= 7u);
   struct retro_system_av_info loaded_av;
   retro_get_system_av_info(&loaded_av);
   assert(loaded_av.geometry.base_width > 0u && loaded_av.geometry.base_height > 0u &&
          loaded_av.timing.fps >= 20.0 && loaded_av.timing.fps <= 200.0);
   retro_set_controller_port_device(0u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1));
   retro_set_controller_port_device(1u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1));

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
   uint64_t before_input_signature = last_video_signature;
   keyboard.callback(true, RETROK_a, 0u, 0u);
   joypad_a = true; mouse_x = 150; mouse_y = -90; mouse_buttons = 1u;
   for (unsigned frame = 0; frame < 15u; ++frame) retro_run();
   keyboard.callback(false, RETROK_a, 0u, 0u);
   joypad_a = false; mouse_buttons = 0u;
   pointer_pressed = true; pointer_x = 1000; pointer_y = 1000; retro_run();
   pointer_x = 1025; pointer_y = 970; retro_run();
   pointer_pressed = false; retro_run();
   retro_set_controller_port_device(0u, RETRO_DEVICE_NONE);
   retro_set_controller_port_device(1u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1));
   joypad_port = 1u; joypad_a = true;
   for (unsigned frame = 0u; frame < 4u; ++frame) retro_run();
   joypad_a = false;
   retro_set_controller_port_device(0u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1));
   retro_set_controller_port_device(1u, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1));
   joypad_port = 0u;
   for (unsigned frame = 78u; frame < 100u; ++frame) retro_run();
   uint64_t measured_expected_total = 0u;
   uint64_t measured_actual_total = 0u;
   uint64_t measured_max_difference = 0u;
   double measured_window_rates[2] = {0.0, 0.0};
   unsigned measured_windows = 0u;
   unsigned av_probe_frames = 0u;
   for (unsigned attempt = 0; attempt < 100u && measured_windows < 2u; ++attempt)
   {
      double window_rate = current_reported_rate;
      measured_expected_audio = 0.0L;
      measured_actual_audio = 0u;
      measured_video_frames = 0u;
      av_rate_changed_during_measure = false;
      measure_av = true;
      for (unsigned frame = 0; frame < 32u; ++frame) retro_run();
      measure_av = false;
      av_probe_frames += 32u;
      if (av_rate_changed_during_measure) continue;
      bool duplicate_rate = false;
      for (unsigned i = 0; i < measured_windows; ++i)
         if (measured_window_rates[i] > window_rate - 0.01 &&
             measured_window_rates[i] < window_rate + 0.01) duplicate_rate = true;
      if (duplicate_rate) continue;
      uint64_t window_expected = (uint64_t)(measured_expected_audio + 0.5L);
      uint64_t window_difference = measured_actual_audio > window_expected ?
         measured_actual_audio - window_expected : window_expected - measured_actual_audio;
      assert(window_difference <= 1u);
      measured_expected_total += window_expected;
      measured_actual_total += measured_actual_audio;
      if (window_difference > measured_max_difference)
         measured_max_difference = window_difference;
      measured_window_rates[measured_windows++] = window_rate;
   }
   assert(measured_windows == 2u);
   bool input_observed = before_input_signature != last_video_signature;
   uint64_t input_signature = last_video_signature;
   retro_unload_game();
   phase = PHASE_LOAD; assert(retro_load_game(&changed_game));
   phase = PHASE_RUN; for (unsigned frame = 0; frame < 60u; ++frame) retro_run();
   uint64_t changed_signature = last_video_signature;
   for (unsigned frame = 60u; frame < 100u; ++frame) retro_run();
   retro_unload_game();
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   phase = PHASE_RUN; retro_run();
   unsigned blank_before = video_frames;
   phase = PHASE_LOAD; assert(!retro_load_game(&bad_game));
   phase = PHASE_RUN; retro_run();
   assert(video_frames == blank_before + 1u);
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   retro_unload_game();
   assert(before_input_signature != changed_signature);
   input_observed = input_observed || before_input_signature != input_signature;
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
   phase = PHASE_RUN; for (unsigned frame = 0u; frame < 100u; ++frame) retro_run();
   retro_unload_game();
   assert(hash_file(write_off) == off_before);

   option_writes = "shadow";
   option_pointer = "relative";
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   unsigned queries_before_reset = option_query_count;
   option_joystick = "A3010";
   phase = PHASE_RUN; retro_reset();
   assert(option_query_count >= queries_before_reset + 7u &&
          strstr(last_message, "A3010 joystick interface"));
   retro_run();
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
      assert(arc_cmos_default(ARC_OS_RISCOS_311, real_default));
      assert(arc_cmos_encode(real_record, &real_plan, real_default));
      memset(real_record, 0xa5, sizeof(real_record));
      assert(!arc_cmos_decode(real_record, &real_plan, restored));
      assert(arc_cmos_default(ARC_OS_RISCOS_311, restored));
      bool any_nonzero = false;
      for (unsigned i = 0u; i < sizeof(restored); ++i)
         any_nonzero = any_nonzero || restored[i] != 0u;
      assert(any_nonzero);
   }
   size_t heap_baseline = 0u;
   size_t heap_maximum_delta = 0u;
   for (unsigned cycle = 0; cycle < 100u; ++cycle)
   {
      phase = PHASE_LOAD; assert(retro_load_game(&game)); assert(keyboard.callback);
      assert(retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) == 256u);
      assert(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
      phase = PHASE_RUN;
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
         assert(memcmp(saved_cmos, retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), sizeof(saved_cmos)) == 0);
         for (unsigned frame = 0; frame < 100u; ++frame) retro_run();
      }
      retro_reset(); retro_unload_game();
      assert(open_files == 0u && open_dirs == 0u);
      size_t heap_now = heap_bytes_in_use();
      if (cycle == 0u)
         heap_baseline = heap_now;
      else if (heap_now > heap_baseline && heap_now - heap_baseline > heap_maximum_delta)
         heap_maximum_delta = heap_now - heap_baseline;
   }
   char cmos_file[4096];
   ArcDigest source_digest = digest_file(argv[3]);
   snprintf(cmos_file, sizeof(cmos_file), "%s/arcdroid/cmos/A3000.test.%s.bin",
            save_path, source_digest.sha1);
   read_exact(cmos_file, final_cmos, sizeof(final_cmos));
   assert(memcmp(saved_cmos, final_cmos, sizeof(saved_cmos)) == 0);
   retro_deinit();
   assert(open_files == 0u && open_dirs == 0u);
   assert(getcwd(cwd_after, sizeof(cwd_after)) && strcmp(cwd, cwd_after) == 0);
   assert(hash_file(argv[3]) == source_hash);
   assert(modified_shadow_count(save_path, source_hash, changed_source_hash) >= 2u);
   assert(video_frames >= 10200u + av_probe_frames && audio_frames > 0u && nonzero_audio_samples > 0u);
   assert(heap_maximum_delta <= 65536u);
   assert(redundant_geometry_changes == 0u);
   assert(av_notifications > 0u);
   assert(video_signature_count >= 1u && reported_rate_count >= 1u);
   assert(input_observed);
   assert(invalid_vidc_message);
   assert(saw_audio_counters);
   assert(dlclose(module) == 0);
   printf("libretro_harness: payload signatures %016llx/%016llx; "
          "100 cycles, %u frames, %llu audio frames, "
          "%llu nonzero samples, %u frame signatures, %u reported rates, "
          "%u geometry changes (0 redundant), A/V windows actual %llu expected %llu "
          "(max delta %llu), "
          "heap peak delta %zu bytes (limit 65536), 0 leaked VFS handles\n",
          (unsigned long long)before_input_signature,
          (unsigned long long)changed_signature,
          video_frames,
          (unsigned long long)audio_frames,
          (unsigned long long)nonzero_audio_samples, video_signature_count,
          reported_rate_count, geometry_changes,
          (unsigned long long)measured_actual_total,
          (unsigned long long)measured_expected_total,
          (unsigned long long)measured_max_difference, heap_maximum_delta);
   printf("reported rates:");
   for (unsigned i = 0; i < reported_rate_count; ++i) printf(" %.6f", reported_rates[i]);
   printf("\n");
   printf("guest input signature change: %s\n", input_observed ? "observed" : "not observed");
   printf("audio queue counters: underrun=%llu overrun=%llu\n",
          (unsigned long long)logged_underruns,
          (unsigned long long)logged_overruns);
   return 0;
}
