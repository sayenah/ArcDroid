/* ArcDroid Windows LoadLibrary/VFS execution harness. GPL-2.0-only. */
#include "libretro.h"

#include <assert.h>
#include <direct.h>
#include <io.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

enum Phase { PHASE_NONE, PHASE_ENV, PHASE_INIT, PHASE_LOAD, PHASE_RUN };
static enum Phase phase;
static bool saw_options, saw_no_game, saw_disk, saw_pixel, saw_controllers;
static bool saw_keyboard, saw_vfs, saw_input_descriptors;
static const char *system_dir, *save_dir;
static unsigned open_files, open_dirs, video_frames;
static uint64_t audio_frames;
static bool saw_adffs_log;

static void check_failed(const char *file, int line, const char *expression)
{
   fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
   fflush(stderr);
   exit(EXIT_FAILURE);
}
#undef assert
#define assert(expression) do { if (!(expression)) \
   check_failed(__FILE__, __LINE__, #expression); } while (0)

struct retro_vfs_file_handle { FILE *file; char *path; unsigned mode; };
struct retro_vfs_dir_handle { HANDLE find; WIN32_FIND_DATAA data; bool first; };

static const char *vfs_get_path(struct retro_vfs_file_handle *h) { return h->path; }
static struct retro_vfs_file_handle *vfs_open(const char *path, unsigned mode, unsigned hints)
{
   (void)hints;
   const char *file_mode = (mode & RETRO_VFS_FILE_ACCESS_WRITE) ?
      ((mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) ? "rb+" : "wb") : "rb";
   FILE *file = fopen(path, file_mode);
   if (!file) return NULL;
   struct retro_vfs_file_handle *handle = calloc(1u, sizeof(*handle));
   assert(handle);
   handle->path = _strdup(path);
   assert(handle->path);
   handle->file = file;
   handle->mode = mode;
   ++open_files;
   return handle;
}
static int vfs_close(struct retro_vfs_file_handle *h)
{
   int result = fclose(h->file);
   free(h->path); free(h); --open_files;
   return result;
}
static int64_t vfs_size(struct retro_vfs_file_handle *h)
{
   __int64 at = _ftelli64(h->file);
   assert(at >= 0 && !_fseeki64(h->file, 0, SEEK_END));
   __int64 size = _ftelli64(h->file);
   assert(size >= 0 && !_fseeki64(h->file, at, SEEK_SET));
   return size;
}
static int64_t vfs_tell(struct retro_vfs_file_handle *h) { return _ftelli64(h->file); }
static int64_t vfs_seek(struct retro_vfs_file_handle *h, int64_t offset, int origin)
{ return _fseeki64(h->file, offset, origin) ? -1 : _ftelli64(h->file); }
static int64_t vfs_read(struct retro_vfs_file_handle *h, void *data, uint64_t size)
{ return (int64_t)fread(data, 1u, (size_t)size, h->file); }
static int64_t vfs_write(struct retro_vfs_file_handle *h, const void *data, uint64_t size)
{ return (int64_t)fwrite(data, 1u, (size_t)size, h->file); }
static int vfs_flush(struct retro_vfs_file_handle *h)
{
   return (h->mode & RETRO_VFS_FILE_ACCESS_WRITE) ? fflush(h->file) : -1;
}
static int vfs_remove(const char *path) { return remove(path); }
static int vfs_rename(const char *from, const char *to) { return rename(from, to); }
static int64_t vfs_truncate(struct retro_vfs_file_handle *h, int64_t size)
{ return _chsize_s(_fileno(h->file), size) ? -1 : 0; }
static int32_t vfs_stat(const char *path, int32_t *size)
{
   WIN32_FILE_ATTRIBUTE_DATA data;
   if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return 0;
   if (size)
   {
      ULARGE_INTEGER value;
      value.HighPart = data.nFileSizeHigh; value.LowPart = data.nFileSizeLow;
      *size = value.QuadPart > INT32_MAX ? INT32_MAX : (int32_t)value.QuadPart;
   }
   return RETRO_VFS_STAT_IS_VALID |
      ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? RETRO_VFS_STAT_IS_DIRECTORY : 0);
}
static int vfs_mkdir(const char *path)
{
   if (CreateDirectoryA(path, NULL)) return 0;
   return GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
}
static struct retro_vfs_dir_handle *vfs_opendir(const char *path, bool include_hidden)
{
   (void)include_hidden;
   char pattern[4096];
   snprintf(pattern, sizeof(pattern), "%s/*", path);
   struct retro_vfs_dir_handle *handle = calloc(1u, sizeof(*handle));
   assert(handle);
   handle->find = FindFirstFileA(pattern, &handle->data);
   if (handle->find == INVALID_HANDLE_VALUE) { free(handle); return NULL; }
   handle->first = true; ++open_dirs;
   return handle;
}
static bool vfs_readdir(struct retro_vfs_dir_handle *h)
{
   if (h->first) { h->first = false; return true; }
   return FindNextFileA(h->find, &h->data) != 0;
}
static const char *vfs_dirent_name(struct retro_vfs_dir_handle *h) { return h->data.cFileName; }
static bool vfs_dirent_is_dir(struct retro_vfs_dir_handle *h)
{ return (h->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
static int vfs_closedir(struct retro_vfs_dir_handle *h)
{
   BOOL result = FindClose(h->find);
   free(h); --open_dirs;
   return result ? 0 : -1;
}

static struct retro_vfs_interface vfs = {
   .get_path = vfs_get_path, .open = vfs_open, .close = vfs_close,
   .size = vfs_size, .tell = vfs_tell, .seek = vfs_seek, .read = vfs_read,
   .write = vfs_write, .flush = vfs_flush, .remove = vfs_remove,
   .rename = vfs_rename, .truncate = vfs_truncate, .stat = vfs_stat,
   .mkdir = vfs_mkdir, .opendir = vfs_opendir, .readdir = vfs_readdir,
   .dirent_get_name = vfs_dirent_name, .dirent_is_dir = vfs_dirent_is_dir,
   .closedir = vfs_closedir
};

static void frontend_log(enum retro_log_level level, const char *format, ...)
{
   (void)level;
   va_list args;
   va_start(args, format);
   va_list copy;
   va_copy(copy, args);
   char line[512];
   vsnprintf(line, sizeof(line), format, copy);
   va_end(copy);
   if (strstr(line, "JFD boot: path=adffs")) saw_adffs_log = true;
   vfprintf(stderr, format, args);
   va_end(args);
}

static bool environment(unsigned command, void *data)
{
   switch (command)
   {
      case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
         assert(phase == PHASE_ENV && !*(bool *)data); saw_no_game = true; return true;
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      {
         const struct retro_core_options_v2 *options = data;
         unsigned count = 0u;
         const struct retro_core_option_v2_definition *hard_disc = NULL;
         const struct retro_core_option_v2_definition *short_tail = NULL;
         const struct retro_core_option_v2_definition *ramdisc = NULL;
         const struct retro_core_option_v2_definition *jfd_omitted = NULL;
         assert(phase == PHASE_ENV && options->categories &&
                !strcmp(options->categories[0].key, "pad_mapping"));
         while (options->definitions[count].key)
         {
            assert(strcmp(options->definitions[count].key,
                          "arcdroid_install_saves") != 0);
            assert(strcmp(options->definitions[count].key,
                          "arcdroid_input_profile") != 0);
            if (!strcmp(options->definitions[count].key, "arcdroid_hard_disc"))
            {
               assert(!hard_disc);
               hard_disc = &options->definitions[count];
            }
            if (!strcmp(options->definitions[count].key, "arcdroid_short_tail"))
            {
               assert(!short_tail);
               short_tail = &options->definitions[count];
            }
            if (!strcmp(options->definitions[count].key, "arcdroid_launch_ramdisc"))
            {
               assert(!ramdisc);
               ramdisc = &options->definitions[count];
            }
            if (!strcmp(options->definitions[count].key, "arcdroid_jfd_omitted"))
            {
               assert(!jfd_omitted);
               jfd_omitted = &options->definitions[count];
            }
            ++count;
         }
         assert(count == 39u);
         assert(hard_disc && !hard_disc->category_key &&
                hard_disc->default_value &&
                !strcmp(hard_disc->default_value, "auto") &&
                hard_disc->values[0].value &&
                !strcmp(hard_disc->values[0].value, "auto") &&
                hard_disc->values[1].value &&
                !strcmp(hard_disc->values[1].value, "off") &&
                !hard_disc->values[2].value);
         assert(jfd_omitted && !jfd_omitted->category_key &&
                jfd_omitted->default_value &&
                !strcmp(jfd_omitted->default_value, "file-data") &&
                !strcmp(jfd_omitted->values[0].value, "file-data") &&
                !strcmp(jfd_omitted->values[1].value, "strict") &&
                !jfd_omitted->values[2].value);
         assert(ramdisc && !ramdisc->category_key &&
                !strcmp(ramdisc->default_value, "Auto") &&
                !strcmp(ramdisc->values[0].value, "Auto") &&
                !strcmp(ramdisc->values[1].value, "Disabled") &&
                !ramdisc->values[2].value);
         assert(short_tail && !short_tail->category_key &&
                short_tail->default_value &&
                !strcmp(short_tail->default_value, "missing"));
         assert(short_tail->values[0].value &&
                !strcmp(short_tail->values[0].value, "missing") &&
                short_tail->values[0].label &&
                !strcmp(short_tail->values[0].label, "Missing"));
         assert(short_tail->values[1].value &&
                !strcmp(short_tail->values[1].value, "zero-filled") &&
                short_tail->values[1].label &&
                !strcmp(short_tail->values[1].label, "Zero-filled") &&
                !short_tail->values[2].value);
         saw_options = true;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
         assert(phase == PHASE_INIT); saw_disk = true; return true;
      case RETRO_ENVIRONMENT_GET_CAN_DUPE:
         assert(phase == PHASE_INIT); *(bool *)data = false; return true;
      case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
         assert(phase == PHASE_INIT);
         ((struct retro_vfs_interface_info *)data)->iface = &vfs;
         saw_vfs = true; return true;
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         assert(phase == PHASE_INIT);
         ((struct retro_log_callback *)data)->log = frontend_log; return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         assert(phase == PHASE_LOAD && *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888);
         saw_pixel = true; return true;
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
         assert(phase == PHASE_LOAD); saw_controllers = true; return true;
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      {
         const struct retro_input_descriptor *descriptors = data;
         unsigned count = 0u;
         assert(phase == PHASE_LOAD);
         while (descriptors[count].description) ++count;
         assert(count == 28u);
         saw_input_descriptors = true;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
         assert(phase == PHASE_LOAD); saw_keyboard = true; return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char **)data = system_dir; return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = save_dir; return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *variable = data;
         assert(strcmp(variable->key, "arcdroid_input_profile") != 0);
         if (!strcmp(variable->key, "arcdroid_pointer_mode")) variable->value = "relative";
         else if (!strcmp(variable->key, "arcdroid_pointer_stick")) variable->value = "right";
         else if (!strcmp(variable->key, "arcdroid_pointer_speed")) variable->value = "600";
         else if (!strcmp(variable->key, "arcdroid_pointer_deadzone")) variable->value = "15";
         else if (!strcmp(variable->key, "arcdroid_disk_writes")) variable->value = "shadow";
         else if (!strcmp(variable->key, "arcdroid_hard_disc")) variable->value = "auto";
         else if (!strcmp(variable->key, "arcdroid_short_tail")) variable->value = "missing";
         else if (!strcmp(variable->key, "arcdroid_jfd_omitted")) variable->value = "file-data";
         else if (!strcmp(variable->key, "arcdroid_launch")) variable->value = "auto";
         else if (!strcmp(variable->key, "arcdroid_launch_chooser")) variable->value = "remember";
         else if (!strcmp(variable->key, "arcdroid_jfd_boot")) variable->value = "auto";
         else if (!strcmp(variable->key, "arcdroid_adffs_package")) variable->value = "verified";
         else if (!strcmp(variable->key, "arcdroid_osk_toggle")) variable->value = "L3";
         else variable->value = "Auto";
         return true;
      }
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
         assert(phase == PHASE_RUN); return true;
      case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
      case RETRO_ENVIRONMENT_SET_MESSAGE:
         return true;
      default: return false;
   }
}

static void video(const void *data, unsigned width, unsigned height, size_t pitch)
{ assert(phase == PHASE_RUN && data && width && height && pitch >= width * 4u); ++video_frames; }
static size_t audio_batch(const int16_t *data, size_t frames)
{ assert(data); audio_frames += frames; return frames; }
static void input_poll(void) {}
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{ (void)port; (void)device; (void)index; (void)id; return 0; }

static void make_directory(const char *path)
{ if (!CreateDirectoryA(path, NULL)) assert(GetLastError() == ERROR_ALREADY_EXISTS); }
static void join_path(char *output, size_t size, const char *left, const char *right)
{ int result = snprintf(output, size, "%s/%s", left, right); assert(result > 0 && (size_t)result < size); }

#define LOAD(type, name) type name = (type)GetProcAddress(module, #name); assert(name)
typedef void (*SetEnvironmentFn)(retro_environment_t);
typedef void (*SetVideoFn)(retro_video_refresh_t);
typedef void (*SetAudioBatchFn)(retro_audio_sample_batch_t);
typedef void (*SetInputPollFn)(retro_input_poll_t);
typedef void (*SetInputStateFn)(retro_input_state_t);
typedef void (*VoidFn)(void);
typedef bool (*LoadGameFn)(const struct retro_game_info *);

int main(int argc, char **argv)
{
   assert(argc == 5 || argc == 7 ||
          (argc == 6 && !strcmp(argv[5], "--seed-product")));
   bool seed_product=argc == 6;
   char system_path[4096], arcdroid_path[4096], firmware_path[4096];
   char save_path[4096], firmware_file[4096], adffs_path[4096], package_file[4096];
   make_directory(argv[4]);
   join_path(system_path, sizeof(system_path), argv[4], "system"); make_directory(system_path);
   join_path(arcdroid_path, sizeof(arcdroid_path), system_path, "arcdroid"); make_directory(arcdroid_path);
   join_path(firmware_path, sizeof(firmware_path), arcdroid_path, "firmware"); make_directory(firmware_path);
   if (argc == 7)
   {
      join_path(adffs_path, sizeof(adffs_path), arcdroid_path, "adffs");
      make_directory(adffs_path);
   }
   join_path(save_path, sizeof(save_path), argv[4], "save"); make_directory(save_path);
   join_path(firmware_file, sizeof(firmware_file), firmware_path, "renamed-test-firmware.data");
   assert(CopyFileA(argv[2], firmware_file, FALSE));
   if (argc == 7)
   {
      join_path(package_file, sizeof(package_file), adffs_path, "synthetic.zip");
      assert(CopyFileA(argv[5], package_file, FALSE));
   }
   system_dir = system_path; save_dir = save_path;

   HMODULE module = LoadLibraryA(argv[1]);
   assert(module);
   LOAD(SetEnvironmentFn, retro_set_environment);
   LOAD(SetVideoFn, retro_set_video_refresh);
   LOAD(SetAudioBatchFn, retro_set_audio_sample_batch);
   LOAD(SetInputPollFn, retro_set_input_poll);
   LOAD(SetInputStateFn, retro_set_input_state);
   LOAD(VoidFn, retro_init);
   LOAD(VoidFn, retro_deinit);
   LOAD(VoidFn, retro_run);
   LOAD(VoidFn, retro_unload_game);
   LOAD(LoadGameFn, retro_load_game);

   phase = PHASE_ENV;
   retro_set_environment(environment);
   retro_set_video_refresh(video);
   retro_set_audio_sample_batch(audio_batch);
   retro_set_input_poll(input_poll);
   retro_set_input_state(input_state);
   phase = PHASE_INIT; retro_init();
   struct retro_game_info game = {argv[3], NULL, 0u, NULL};
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   if (seed_product)
   {
      bool (*seed)(char[41]) = (bool (*)(char[41]))GetProcAddress(module,
         "arc_libretro_test_seed_hd_product");
      char seeded_id[41];
      assert(seed && seed(seeded_id));
      printf("Seeded product: id=%s\n",seeded_id);
   }
   phase = PHASE_RUN;
   for (unsigned frame = 0u; frame < 100u; ++frame) retro_run();
   retro_unload_game();
   if (argc == 7)
   {
      struct retro_game_info adffs_game = {argv[6], NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_game));
      assert(saw_adffs_log);
      retro_unload_game();
   }
   retro_deinit();
   assert(saw_options && saw_no_game && saw_disk && saw_pixel && saw_controllers &&
          saw_keyboard && saw_vfs && saw_input_descriptors);
   assert(video_frames == 100u && audio_frames > 0u);
   assert(open_files == 0u && open_dirs == 0u);
   assert(FreeLibrary(module));
   printf("windows_load_harness: DLL/VFS loaded content; %u frames, %llu audio frames, 0 leaked handles\n",
          video_frames, (unsigned long long)audio_frames);
   return 0;
}
