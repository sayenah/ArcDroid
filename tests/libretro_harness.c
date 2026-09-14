/* ArcDroid libretro lifecycle/API harness. GPL-2.0-only. */
#include "libretro.h"

#include <assert.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
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
static double reported_rates[16];
static unsigned reported_rate_count;
static unsigned geometry_changes;
static char last_message[512];
static struct retro_keyboard_callback keyboard;
static bool joypad_a;
static int mouse_x, mouse_y;
static unsigned mouse_buttons;
static bool pointer_pressed;
static int pointer_x, pointer_y;

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

static bool environment(unsigned command, void *data)
{
   switch (command)
   {
      case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
         assert(phase == PHASE_ENV && !*(bool *)data); return true;
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
         assert(phase == PHASE_ENV); return true;
      case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
         assert(phase == PHASE_INIT); return true;
      case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
         assert(phase == PHASE_INIT); ((struct retro_vfs_interface_info *)data)->iface = &vfs; return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         assert(phase == PHASE_LOAD && *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888); return true;
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
         assert(phase == PHASE_LOAD); return true;
      case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
         assert(phase == PHASE_LOAD); keyboard = *(struct retro_keyboard_callback *)data; return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = system_dir; return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = save_dir; return true;
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
         assert(phase == PHASE_RUN); ++geometry_changes; return true;
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      {
         const struct retro_system_av_info *av = data;
         assert(phase == PHASE_RUN && av->timing.fps >= 20.0 && av->timing.fps <= 200.0);
         bool seen = false;
         for (unsigned i = 0; i < reported_rate_count; ++i)
            if (reported_rates[i] > av->timing.fps - 0.01 &&
                reported_rates[i] < av->timing.fps + 0.01) seen = true;
         if (!seen && reported_rate_count < 16u)
            reported_rates[reported_rate_count++] = av->timing.fps;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
         snprintf(last_message, sizeof(last_message), "%s", ((struct retro_message_ext *)data)->msg); return true;
      case RETRO_ENVIRONMENT_SET_MESSAGE:
         snprintf(last_message, sizeof(last_message), "%s", ((struct retro_message *)data)->msg); return true;
      default: return false;
   }
}
static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
   assert(data && w > 0u && w <= 4096u && h > 0u && h <= 2048u && pitch >= w * 4u);
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
   ++video_frames;
}
static size_t audio(const int16_t *data, size_t frames)
{
   assert(data && frames <= 2048u);
   for (size_t i = 0; i < frames * 2u; ++i)
      if (data[i]) ++nonzero_audio_samples;
   audio_frames += frames;
   return frames;
}
static void poll(void) {}
static int16_t input(unsigned p, unsigned d, unsigned i, unsigned id)
{
   (void)p; (void)i;
   if (d == RETRO_DEVICE_JOYPAD && id == RETRO_DEVICE_ID_JOYPAD_A) return joypad_a;
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

#define LOAD(name) do { *(void **)(&name) = dlsym(module, #name); assert(name); } while (0)
int main(int argc, char **argv)
{
   assert(argc == 5 || (argc == 6 && strcmp(argv[5], "--real-boot") == 0));
   bool real_boot = argc == 6;
   void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); assert(module);
   char system_path[4096], firmware_path[4096], save_path[4096], renamed[4096], cwd[4096], cwd_after[4096];
   snprintf(system_path, sizeof(system_path), "%s/system", argv[4]);
   snprintf(firmware_path, sizeof(firmware_path), "%s/arcdroid", system_path);
   snprintf(save_path, sizeof(save_path), "%s/save", argv[4]);
   make_directory(argv[4]); make_directory(system_path); make_directory(firmware_path);
   strncat(firmware_path, "/firmware", sizeof(firmware_path) - strlen(firmware_path) - 1u); make_directory(firmware_path);
   make_directory(save_path);
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
   size_t (*retro_serialize_size)(void); LOAD(retro_serialize_size);
   void *(*retro_get_memory_data)(unsigned); LOAD(retro_get_memory_data);
   size_t (*retro_get_memory_size)(unsigned); LOAD(retro_get_memory_size);

   phase = PHASE_ENV; retro_set_environment(environment);
   retro_set_video_refresh(video); retro_set_audio_sample_batch(audio);
   retro_set_input_poll(poll); retro_set_input_state(input);
   phase = PHASE_INIT; retro_init();
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
      for (unsigned frame = 0; frame < 1000u; ++frame) retro_run();
      retro_unload_game();
      retro_deinit();
      assert(open_files == 0u && open_dirs == 0u && hash_file(argv[3]) == source_hash);
      assert(video_frames == 1000u && audio_frames > 0u && video_signature_count > 1u);
      assert(dlclose(module) == 0);
      printf("real_boot_harness: %s; 1000 frames, %u signatures, %llu audio frames, "
             "%llu nonzero samples\n", firmware_message, video_signature_count,
             (unsigned long long)audio_frames,
             (unsigned long long)nonzero_audio_samples);
      return 0;
   }

   char hidden[4096]; snprintf(hidden, sizeof(hidden), "%s/firmware.hidden", argv[4]);
   assert(rename(renamed, hidden) == 0);
   phase = PHASE_LOAD; assert(!retro_load_game(&game)); assert(strstr(last_message, "Missing RISC OS 3.11"));
   char oversized[4096];
   snprintf(oversized, sizeof(oversized), "%s/ROM120-size-check", firmware_path);
   FILE *odd_rom = fopen(oversized, "wb"); assert(odd_rom);
   assert(fseek(odd_rom, 524288, SEEK_SET) == 0 && fputc(0, odd_rom) != EOF && !fclose(odd_rom));
   phase = PHASE_LOAD; assert(!retro_load_game(&game));
   assert(strstr(last_message, "524289") && strstr(last_message, "524288"));
   assert(remove(oversized) == 0);
   assert(rename(hidden, renamed) == 0);

   char corrupt[4096]; snprintf(corrupt, sizeof(corrupt), "%s/corrupt.adf", argv[4]);
   FILE *bad = fopen(corrupt, "wb"); assert(bad); assert(fwrite("bad", 1u, 3u, bad) == 3u); assert(!fclose(bad));
   struct retro_game_info bad_game = {corrupt, NULL, 0u, NULL};
   phase = PHASE_LOAD; assert(!retro_load_game(&bad_game)); assert(strstr(last_message, "geometry"));

   /* The ARMv2 validation ROM reads sector zero through WD1772 and derives
    * its pixels from that payload. A one-byte disc change must reach the guest. */
   char changed[4096]; snprintf(changed, sizeof(changed), "%s/changed.adf", argv[4]);
   copy_file(argv[3], changed);
   FILE *changed_file = fopen(changed, "rb+"); assert(changed_file);
   assert(fseek(changed_file, 64, SEEK_SET) == 0);
   int changed_byte = fgetc(changed_file); assert(changed_byte != EOF);
   assert(fseek(changed_file, 64, SEEK_SET) == 0);
   assert(fputc(changed_byte ^ 0x5a, changed_file) != EOF && !fclose(changed_file));
   struct retro_game_info changed_game = {changed, NULL, 0u, NULL};
   phase = PHASE_LOAD; assert(retro_load_game(&game));
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
   for (unsigned frame = 78u; frame < 100u; ++frame) retro_run();
   bool input_observed = before_input_signature != last_video_signature;
   uint64_t input_signature = last_video_signature;
   retro_unload_game();
   phase = PHASE_LOAD; assert(retro_load_game(&changed_game));
   phase = PHASE_RUN; for (unsigned frame = 0; frame < 60u; ++frame) retro_run();
   uint64_t changed_signature = last_video_signature;
   for (unsigned frame = 60u; frame < 100u; ++frame) retro_run();
   retro_unload_game();
   assert(before_input_signature != changed_signature);
   input_observed = input_observed || before_input_signature != input_signature;
   assert(hash_file(argv[3]) == source_hash);

   uint8_t saved_cmos[256];
   uint8_t final_cmos[256];
   for (unsigned cycle = 0; cycle < 100u; ++cycle)
   {
      phase = PHASE_LOAD; assert(retro_load_game(&game)); assert(keyboard.callback);
      assert(retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) == 256u);
      assert(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
      phase = PHASE_RUN;
      if (cycle == 0u)
      {
         memset(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), 0xa5, 256u);
         retro_run();
         assert(memcmp(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), "ARCCMOS1", 8u) == 0);
         for (unsigned frame = 1u; frame < 100u; ++frame) retro_run();
         memcpy(saved_cmos, retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), sizeof(saved_cmos));
      }
      else
      {
         assert(memcmp(saved_cmos, retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), sizeof(saved_cmos)) == 0);
         for (unsigned frame = 0; frame < 100u; ++frame) retro_run();
      }
      retro_reset(); retro_unload_game();
      assert(open_files == 0u && open_dirs == 0u);
   }
   char cmos_file[4096];
   snprintf(cmos_file, sizeof(cmos_file), "%s/arcdroid/cmos/A3000.test.bin", save_path);
   read_exact(cmos_file, final_cmos, sizeof(final_cmos));
   assert(memcmp(saved_cmos, final_cmos, sizeof(saved_cmos)) == 0);
   retro_deinit();
   assert(open_files == 0u && open_dirs == 0u);
   assert(getcwd(cwd_after, sizeof(cwd_after)) && strcmp(cwd, cwd_after) == 0);
   assert(hash_file(argv[3]) == source_hash);
   assert(video_frames == 10200u && audio_frames > 0u && nonzero_audio_samples > 0u);
   assert(video_signature_count >= 1u && reported_rate_count >= 1u);
   assert(input_observed);
   assert(dlclose(module) == 0);
   printf("libretro_harness: payload signatures %016llx/%016llx; "
          "100 cycles, 10200 frames, %llu audio frames, "
          "%llu nonzero samples, %u frame signatures, %u reported rates, "
          "%u geometry changes, 0 leaked VFS handles\n",
          (unsigned long long)before_input_signature,
          (unsigned long long)changed_signature,
          (unsigned long long)audio_frames,
          (unsigned long long)nonzero_audio_samples, video_signature_count,
          reported_rate_count, geometry_changes);
   printf("reported rates:");
   for (unsigned i = 0; i < reported_rate_count; ++i) printf(" %.6f", reported_rates[i]);
   printf("\n");
   printf("guest input signature change: %s\n", input_observed ? "observed" : "not observed");
   return 0;
}
