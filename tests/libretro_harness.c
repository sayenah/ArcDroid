/* ArcDroid libretro lifecycle/API harness. GPL-2.0-only. */
#include "libretro.h"
#include "arcdroid_core.h"
#include "arcdroid_engine.h"
#include "boot_observation.h"
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

#define ASSERT_NO_SAVE_RAM()                                                   \
   do {                                                                        \
      assert(retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) == 0u);             \
      assert(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) == NULL);           \
   } while (0)

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
static uint32_t last_video_pixel;
static unsigned video_signature_last_change_frame;
static bool last_video_was_duplicate;
static bool probe_cached_video;
static const uint8_t *cached_video;
static unsigned cached_video_height;
static size_t cached_video_pitch;
static uint8_t cached_video_copy[4096u * 2048u * sizeof(uint32_t)];
static unsigned cached_video_reads;
static unsigned cached_video_av_reads, cached_video_geometry_reads;
static unsigned cached_video_duplicate_reads;
static uint8_t cached_video_sample;
static void read_cached_video(void)
{
   if (!probe_cached_video || !cached_video) return;
   size_t bytes = (size_t)cached_video_height * cached_video_pitch;
   assert(bytes && bytes <= sizeof(cached_video_copy));
   memcpy(cached_video_copy, cached_video, bytes);
   cached_video_sample ^= cached_video_copy[cached_video_reads % bytes];
   ++cached_video_reads;
}
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
static char machine_resolution_log[128];
static char hard_disc_policy_log[256];
static unsigned hard_disc_unavailable_notices;
static unsigned hard_disc_damaged_notices;
static unsigned hard_disc_readonly_notices;
static unsigned hard_disc_write_failure_notices;
static unsigned hard_disc_formatter_mismatch_logs;
static unsigned machine_resolution_lines;
static unsigned riscpc_log_count;
static unsigned cmos_healing_log_count;
static unsigned power_on_construction_lines;
static unsigned power_on_clear_lines;
static unsigned kart_shift_down_lines;
static unsigned kart_shift_up_lines;
static unsigned cmos_ignored_log_count;
static unsigned cmos_summary_attempts;
static unsigned cmos_summary_changes;

static bool cmos_test_ide_read(void *opaque, uint32_t lba, uint32_t count,
                               uint8_t *out)
{
   (void)opaque; (void)lba;
   memset(out, 0, (size_t)count * ARC_IDE_SECTOR_SIZE);
   return true;
}
static unsigned riscpc_osd_count;
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
static unsigned real_boot_current_frame;
static char evidence_path[4096];
static char pending_screenshot[256];
static FILE *evidence_log;
static unsigned fdc_command_count;
static unsigned fdc_read_count;
static unsigned kart_traffic_count;
static unsigned cmos_read_count;
static unsigned cmos_write_count;
static unsigned cmos_locked_debug_write_count;
static unsigned real_boot_frames = 3000u;
static bool real_boot_cancel_input;
static bool real_boot_cancel_shift;
static const char *real_boot_user_shift;
static bool real_boot_incomplete_fields;
static bool real_boot_reset_once;
static unsigned real_boot_reset_frame = 150u;
static bool real_boot_chooser_pick;
static unsigned real_boot_chooser_pick_row;
static bool real_boot_chooser_continue;
static bool real_boot_chooser_hold_confirm;
static bool real_boot_chooser_reset;
static bool real_boot_chooser_unload;
static bool real_boot_cmos_write_fail;
static const char *real_boot_package;
static bool real_boot_expect_disabled;
static bool real_boot_fail_construct;
static bool real_boot_fail_hd_recover;
static void (*inject_hd_construct_failure)(void);
static bool real_boot_hd_collision;
static bool (*inject_hd_preflight_collision)(void);
static bool real_boot_seed_product;
static bool real_boot_seed_record;
static bool real_boot_expect_record_hit;
static unsigned real_boot_seed_started;
static bool real_boot_arm_hd_copy;
static int real_boot_hd_fail_at = -1;
static bool real_boot_hd_fail_after_install;
static bool real_boot_rearm_after_install;
static bool real_boot_cmos_sentinel;
static bool real_boot_cmos_ramdisc;
static int real_boot_hd_write_mode = -1;
static struct retro_disk_control_ext_callback disk_control;
static bool can_dupe;
static bool keyboard_poll_capability;
static bool keyboard_levels[ARC_INPUT_KEY_COUNT];
static bool legacy_only_options;
static unsigned set_variable_calls;
static bool saw_input_descriptors;
static unsigned option_query_count;
static const char *stale_install_option_value;
static unsigned stale_install_option_queries;
static unsigned cmos_write_opens;
static const char *observed_vfs_path;
static unsigned observed_vfs_read_opens;
static unsigned observed_vfs_write_opens;
static unsigned observed_vfs_reads;
static unsigned observed_vfs_hash_opens;
static unsigned install_prefix_accesses;
static void count_install_prefix(const char *path)
{
   if (path && strstr(path, "/arcdroid/install") &&
       (strstr(path, "/arcdroid/install")[17] == '/' ||
        strstr(path, "/arcdroid/install")[17] == '\0'))
      ++install_prefix_accesses;
}
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
/* Legacy controller fixtures expect an absent IDE bus; native IDE cases
 * explicitly select Auto below. The core option definition still defaults
 * to Auto for frontends. */
static const char *option_hard_disc = "off";
static const char *option_short_tail = "missing";
static const char *option_jfd_omitted = "file-data";
static const char *option_launch_ramdisc = "Auto";
static bool real_boot_ramdisc_toggle_reset;
static bool hd_guest_logging;
static const char *option_launch = "auto";
static const char *option_launch_chooser = "remember";
static const char *option_jfd_boot = "auto";
static const char *option_adffs_package = "verified";
static const char *stale_input_option_value = "Auto";
static unsigned stale_input_option_queries;
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
static unsigned short_info_lines;
static char short_notice_text[256];
static char short_info_text[256];
static unsigned launch_log_lines;
static unsigned jfd_boot_log_lines;
static unsigned jfd_quarantine_log_lines;
static unsigned jfd_quarantine_notice_calls;
static char last_jfd_quarantine_log[160];
static char last_jfd_boot_log[512];
static unsigned adffs_notice_calls;
static unsigned jfd_write_log_lines;
static unsigned jfd_refusal_log_lines;
static char last_jfd_refusal_log[512];
static char last_jfd_writes_log[512];
static char last_jfd_identification_log[512];
static char last_jfd_file_data_log[512];
static unsigned jfd_file_data_log_lines;
static unsigned jfd_failed_status_calls;
static unsigned jfd_damaged_notice_calls;
static unsigned jfd_off_notice_calls;
static unsigned jfd_native_notice_calls;
static unsigned jfd_original_notice_calls;
static char last_adffs_notice[512];
static bool adffs_log_leak;
static unsigned adffs_package_log_lines;
static char last_adffs_package_log[512];
static unsigned skipped_sibling_log_lines;
static unsigned skipped_sibling_names;
static unsigned launch_warning_osd_calls;
static unsigned flush_fault_log_lines;
static unsigned flush_fault_osd_calls;
static unsigned folder_set_log_lines;
static unsigned folder_set_omission_log_lines;
static unsigned folder_set_warning_osd_calls;
static unsigned zip_summary_log_lines;
static unsigned zip_order_log_lines;
static unsigned zip_omission_log_lines;
static unsigned zip_summary_osd_calls;
static unsigned zip_fallback_osd_calls;
static char last_zip_summary[2048];
static char last_zip_order[18000];
static char last_zip_omission[2048];
static char last_zip_fallback[1152];
static unsigned total_osd_calls;
static unsigned osk_failure_osd_calls;
static unsigned osk_narrow_osd_calls;
static unsigned controls_osd_calls;
static char application_identity_sha1[41];
static char playlist_identity_sha1[41];
static char nested_identity_sha1[41];
static char last_launch_log[2048];
static unsigned ramdisc_publication_logs;
static char last_launch_warning[512];
static char last_flush_fault[512];
static char last_input_resolution[3072];
static unsigned input_resolution_log_calls;
static char effective_descriptor_labels[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT][64];

static const unsigned mapping_input_ids[ARC_PAD_INPUT_COUNT] = {
   RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN,
   RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
   RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B,
   RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y,
   RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT
};

static const char *mapping_label(unsigned input, unsigned port)
{
   const char *value = option_pad[port][input];
   if (!value || !strcmp(value, "Auto"))
   {
      const ArcInputBinding *generic = arc_input_generic_mapping();
      const ArcInputKey *key = arc_input_key(generic[input].key);
      assert(generic[input].bound && key);
      return key->label;
   }
   if (!strcmp(value, "Unbound")) return "Unbound";
   const ArcInputKey *key = arc_input_key_value(value);
   assert(key);
   return key->label;
}

static void assert_effective_descriptors(void)
{
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
         assert(!strcmp(effective_descriptor_labels[port][input],
                        mapping_label(input, port)));
}

static void assert_input_resolution(void)
{
   char expected[3072];
   bool custom = false;
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
         if (option_pad[port][input] && strcmp(option_pad[port][input], "Auto"))
            custom = true;
   size_t at = (size_t)snprintf(expected, sizeof(expected),
      "Input resolution: mapping=%s", custom ? "custom" : "default");
   for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
      {
         const char *value = option_pad[port][input];
         bool overridden = value && strcmp(value, "Auto");
         int written = snprintf(expected + at, sizeof(expected) - at,
            " p%u.%s=%s[%s]", port + 1u, arc_pad_input_name(input),
            mapping_label(input, port), overridden ? "option" : "default");
         assert(written > 0 && (size_t)written < sizeof(expected) - at);
         at += (size_t)written;
      }
   assert(at + 1u < sizeof(expected));
   expected[at++] = '\n';
   expected[at] = '\0';
   assert(!strcmp(last_input_resolution, expected));
}


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
static const char *fault_remove_substring;
static const char *fault_flush_substring;
static const char *fault_close_substring;
static const char *fault_rename_substring;
static unsigned fault_rename_skip_matches;
static const char *fault_mkdir_substring;
static const char *fault_opendir_substring;
static const char *fault_closedir_substring;
static const char *observed_opendir_path;
static unsigned observed_opendir_calls;
static const char *aliased_vfs_file_path;
static const char *aliased_vfs_file_source;
static const char *aliased_vfs_directory_path;
static const char *aliased_vfs_directory_source;
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
   count_install_prefix(path);
   if (fault_once(path, &fault_open_substring)) return NULL;
   /* Count attempted opens before the host fopen: a forbidden pseudo-path
    * access must remain observable even when no literal sibling exists. */
   if (observed_vfs_path && !strcmp(path, observed_vfs_path))
   {
      if (mode & RETRO_VFS_FILE_ACCESS_WRITE) ++observed_vfs_write_opens;
      else ++observed_vfs_read_opens;
   }
   if (strchr(path, '#')) ++observed_vfs_hash_opens;
   const char *m = (mode & RETRO_VFS_FILE_ACCESS_WRITE) ?
      ((mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) ? "rb+" : "wb") : "rb";
   const char *native_path = aliased_vfs_file_path &&
      !strcmp(path, aliased_vfs_file_path) ? aliased_vfs_file_source : path;
   FILE *f = fopen(native_path, m);
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
   if (observed_vfs_path && !strcmp(h->path, observed_vfs_path))
      ++observed_vfs_reads;
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
static int vfs_remove(const char *p)
{ count_install_prefix(p); return fault_once(p, &fault_remove_substring) ? -1 : remove(p); }
static int vfs_rename(const char *a, const char *b)
{
   count_install_prefix(a); count_install_prefix(b);
   if (fault_rename_substring && b &&
       strstr(b, fault_rename_substring) && fault_rename_skip_matches)
   {
      --fault_rename_skip_matches;
      return rename(a, b);
   }
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
   count_install_prefix(p);
   struct stat st;
   if (stat(p, &st)) return 0;
   if (size) *size = st.st_size > INT32_MAX ? INT32_MAX : (int32_t)st.st_size;
   return RETRO_VFS_STAT_IS_VALID | (S_ISDIR(st.st_mode) ? RETRO_VFS_STAT_IS_DIRECTORY : 0);
}
static int vfs_mkdir(const char *p)
{ count_install_prefix(p); return fault_once(p, &fault_mkdir_substring) ? -1 : mkdir(p, 0755); }
static struct retro_vfs_dir_handle *vfs_opendir(const char *p, bool hidden)
{
   (void)hidden;
   count_install_prefix(p);
   if (observed_opendir_path && !strcmp(p, observed_opendir_path))
      ++observed_opendir_calls;
   if (fault_once(p, &fault_opendir_substring)) return NULL;
   const char *native_path = aliased_vfs_directory_path &&
      !strcmp(p, aliased_vfs_directory_path) ? aliased_vfs_directory_source : p;
   DIR *d = opendir(native_path); if (!d) return NULL;
   struct retro_vfs_dir_handle *h = calloc(1u, sizeof(*h)); assert(h);
   h->dir = d; snprintf(h->path, sizeof(h->path), "%s", native_path);
   ++open_dirs; return h;
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
   if (real_boot_fail_hd_recover && inject_hd_construct_failure &&
       !strncmp(message,"Launch outcome: ended ",22u))
   {
      real_boot_fail_hd_recover=false;
      inject_hd_construct_failure();
   }
   if (real_boot_hd_collision && inject_hd_preflight_collision &&
       !strncmp(message,"Launch outcome: ended ",22u))
   {
      real_boot_hd_collision=false;
      assert(inject_hd_preflight_collision());
   }
   if (!strncmp(message, "RAM disc: ", 10u))
   { assert(level == RETRO_LOG_INFO); ++ramdisc_publication_logs; }
   if (hd_guest_logging &&
       (strstr(message,"Hard disc:") ||
        strstr(message,"JFD boot:") ||
        strstr(message,"Machine:") ||
        (getenv("ARCDROID_TEST_POWER_ON_SHIFT") &&
         strstr(message,"Power-on:")) || level >= RETRO_LOG_ERROR))
      fprintf(stderr,"%s",message);
   if (!strncmp(message, "Hard disc: policy=", 18u))
      snprintf(hard_disc_policy_log, sizeof(hard_disc_policy_log), "%s", message);
   if (strstr(message, "Hard disc: store-create=formatter-hash-mismatch"))
      ++hard_disc_formatter_mismatch_logs;
   if (!strncmp(message, "Machine: requested=", 19u))
   {
      ++machine_resolution_lines;
      snprintf(machine_resolution_log, sizeof(machine_resolution_log), "%s", message);
   }
   if (!strncmp(message, "JFD native quarantine:", 22u))
   {
      assert(level == RETRO_LOG_WARN);
      ++jfd_quarantine_log_lines;
      snprintf(last_jfd_quarantine_log, sizeof(last_jfd_quarantine_log),
               "%s", message);
   }
   if (strstr(message, "flagged for RiscPC-class machines"))
      ++riscpc_log_count;
   if (strstr(message, "ArcFixture") || strstr(message, "ArcDroid synthetic ADFFS"))
      adffs_log_leak = true;
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
   if (strstr(message, "KART device->guest values=C4,CC")) ++kart_shift_down_lines;
   if (strstr(message, "KART device->guest values=D4,DC")) ++kart_shift_up_lines;
   if (strstr(message, "CMOS read ")) ++cmos_read_count;
   if (strstr(message, "CMOS write ")) ++cmos_write_count;
   if (strstr(message, "CMOS write addr=ED ")) ++cmos_locked_debug_write_count;
   if (strstr(message, "CMOS: cleared ")) { assert(level == RETRO_LOG_INFO); ++cmos_healing_log_count; }
   if (strstr(message, "Power-on: boot-flag="))
   { assert(level == RETRO_LOG_INFO); ++power_on_construction_lines; }
   if (strstr(message, "Power-on: boot-flag=clear shift=held"))
   { assert(level == RETRO_LOG_INFO); ++power_on_clear_lines; }
   if (strstr(message, "CMOS: guest write to unplug byte "))
   { assert(level == RETRO_LOG_INFO); ++cmos_ignored_log_count; }
   if (strstr(message, "not a recognised ADFS image")) saw_adfs_advisory = true;
   if (strstr(message, "Input queue overflowed")) ++input_overflow_log_lines;
   if (!strncmp(message, "Input resolution:", 17u))
   {
      assert(!strstr(message, "profile=") && !strstr(message, "[profile]") &&
             !strstr(message, "[preset]"));
      ++input_resolution_log_calls;
      snprintf(last_input_resolution, sizeof(last_input_resolution), "%s", message);
   }
   if (strstr(message, "Image is missing its last "))
   {
      ++short_warning_log_lines;
      snprintf(short_notice_text, sizeof(short_notice_text), "%s", message);
   }
   if (strstr(message, "Short image: geometry="))
   {
      ++short_info_lines;
      snprintf(short_info_text, sizeof(short_info_text), "%s", message);
   }
   if (strstr(message, "Unable to flush the staged floppy image"))
   {
      ++flush_fault_log_lines;
      snprintf(last_flush_fault, sizeof(last_flush_fault), "%s", message);
   }
   if (strstr(message, "Folder disc set: VFS directory=")) ++folder_set_log_lines;
   if (strstr(message, "Folder disc set sibling omitted:"))
      ++folder_set_omission_log_lines;
   if (!strncmp(message, "Zip content: file=", sizeof("Zip content: file=") - 1u))
   {
      ++zip_summary_log_lines;
      snprintf(last_zip_summary, sizeof(last_zip_summary), "%s", message);
   }
   if (!strncmp(message, "Zip content order:",
                sizeof("Zip content order:") - 1u))
   {
      ++zip_order_log_lines;
      snprintf(last_zip_order, sizeof(last_zip_order), "%s", message);
   }
   if (!strncmp(message, "Zip member omitted:",
                sizeof("Zip member omitted:") - 1u))
   {
      ++zip_omission_log_lines;
      snprintf(last_zip_omission, sizeof(last_zip_omission), "%s", message);
   }
   if (!strncmp(message, "Launch:", 7u))
   {
      ++launch_log_lines;
      snprintf(last_launch_log, sizeof(last_launch_log), "%s", message);
   }
   if (!strncmp(message, "JFD boot:", 9u))
   {
      ++jfd_boot_log_lines;
      snprintf(last_jfd_boot_log, sizeof(last_jfd_boot_log), "%s", message);
   }
   if (!strncmp(message, "JFD write:", 10u)) ++jfd_write_log_lines;
   if (!strncmp(message, "JFD write refused:", 18u)) {
      ++jfd_refusal_log_lines;
      snprintf(last_jfd_refusal_log, sizeof(last_jfd_refusal_log), "%s", message);
   }
   if (!strncmp(message, "JFD writes:", 11u))
      snprintf(last_jfd_writes_log, sizeof(last_jfd_writes_log), "%s", message);
   if (!strncmp(message, "JFD identification:", 19u))
      snprintf(last_jfd_identification_log,
               sizeof(last_jfd_identification_log), "%s", message);
   if (!strncmp(message, "JFD file-data:", 14u))
   {
      ++jfd_file_data_log_lines;
      snprintf(last_jfd_file_data_log,sizeof(last_jfd_file_data_log),"%s",message);
   }
   if (strstr(message, "JFD content is never written in place"))
      ++jfd_original_notice_calls;
   if (!strncmp(message, "ADFFS package:", 14u))
   {
      ++adffs_package_log_lines;
      snprintf(last_adffs_package_log, sizeof(last_adffs_package_log), "%s", message);
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
      const char *cmos_summary = strstr(message, "cmos_locked_attempts=");
      if (cmos_summary)
         (void)sscanf(cmos_summary, "cmos_locked_attempts=%u cmos_locked_changes=%u",
                      &cmos_summary_attempts, &cmos_summary_changes);
   }
   if (evidence_log)
   {
      fputs(message, evidence_log);
      if (real_boot_mode && strstr(message, "KART device->guest values=D4,DC"))
         fprintf(evidence_log, "Power-on probe frame=%u event=up\n",
                 real_boot_current_frame);
      if (real_boot_mode && strstr(message, "Power-on: shift released at="))
         fprintf(evidence_log, "Power-on probe frame=%u event=release\n",
                 real_boot_current_frame);
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
         assert(count == 39u && options->categories &&
                !strcmp(options->categories[0].key, "pad_mapping") &&
                !strcmp(options->categories[0].desc, "Pad Mapping"));
         bool found_osk = false, found_chooser = false;
         bool found_short_tail = false, found_ramdisc = false;
         bool found_jfd_omitted = false, found_hard_disc = false;
         bool found_jfd_boot = false, found_adffs_package = false;
         unsigned pads = 0u;
         const char *categorized_pad_labels[ARC_PAD_PORTS * ARC_PAD_INPUT_COUNT] = {0};
         for (unsigned n = 0u; n < count; ++n)
         {
            const struct retro_core_option_v2_definition *definition =
               &options->definitions[n];
            assert(strcmp(definition->key, "arcdroid_install_saves") != 0);
            assert(strcmp(definition->key, "arcdroid_input_profile"));
            if (!strcmp(definition->key, "arcdroid_osk_toggle")) found_osk = true;
            if (!strcmp(definition->key, "arcdroid_short_tail"))
            {
               found_short_tail = true;
               assert(!strcmp(definition->default_value, "missing") &&
                      !strcmp(definition->values[0].value, "missing") &&
                      !strcmp(definition->values[0].label, "Missing") &&
                      !strcmp(definition->values[1].value, "zero-filled") &&
                      !strcmp(definition->values[1].label, "Zero-filled"));
            }
            if (!strcmp(definition->key, "arcdroid_jfd_omitted"))
            {
               found_jfd_omitted = true;
               assert(!strcmp(definition->desc,"JFD Omitted Sectors") &&
                      !strcmp(definition->default_value,"file-data") &&
                      !strcmp(definition->values[0].value,"file-data") &&
                      !strcmp(definition->values[1].value,"strict") &&
                      !definition->values[2].value &&
                      strstr(definition->info,"load or reset"));
            }
            if (!strcmp(definition->key, "arcdroid_hard_disc"))
            {
               found_hard_disc = true;
               assert(!strcmp(definition->desc, "Hard Disc") &&
                      !strcmp(definition->default_value, "auto") &&
                      !strcmp(definition->values[0].value, "auto") &&
                      !strcmp(definition->values[1].value, "off"));
            }
            if (!strcmp(definition->key, "arcdroid_launch_ramdisc"))
            {
               found_ramdisc = true;
               assert(!strcmp(definition->default_value, "Auto") &&
                      !strcmp(definition->values[0].value, "Auto") &&
                      !strcmp(definition->values[1].value, "Disabled") &&
                      !definition->values[2].value);
            }
            if (!strcmp(definition->key, "arcdroid_launch_chooser"))
            {
               found_chooser = true;
               assert(!strcmp(definition->default_value, "remember") &&
                      !strcmp(definition->values[0].value, "remember") &&
                      !strcmp(definition->values[1].value, "ask"));
            }
            if (!strcmp(definition->key, "arcdroid_jfd_boot"))
            {
               found_jfd_boot = true;
               assert(!strcmp(definition->info,
                      "Auto uses the ADFFS package for eligible JFDs, except single-disc JFDs that ArcDroid can write natively, which boot natively to keep saves. Native never uses ADFFS. ADFFS preferred bypasses this rule; saved native changes still take precedence.") &&
                      !strcmp(definition->default_value, "auto") &&
                      !strcmp(definition->values[0].value, "auto") &&
                      !strcmp(definition->values[1].value, "native") &&
                      !strcmp(definition->values[2].value, "adffs") &&
                      !strcmp(definition->values[2].label, "ADFFS preferred"));
            }
            if (!strcmp(definition->key, "arcdroid_adffs_package"))
            {
               found_adffs_package = true;
               assert(!strcmp(definition->default_value, "verified") &&
                      !strcmp(definition->values[0].value, "verified") &&
                      !strcmp(definition->values[1].value, "untested"));
            }
            if (!strncmp(definition->key, "arcdroid_pad", 12u))
            {
               assert(!strcmp(definition->info,
                      "Auto uses the default mapping; Unbound disables this input.") &&
                      !strcmp(definition->category_key, "pad_mapping") &&
                      !strcmp(definition->values[0].value, "Auto") &&
                      !strcmp(definition->values[1].value, "Unbound") &&
                      definition->desc_categorized &&
                      !strcmp(definition->desc, definition->desc_categorized) &&
                      !strncmp(definition->desc_categorized, "Pad ", 4u));
               for (unsigned previous = 0u; previous < pads; ++previous)
                  assert(strcmp(categorized_pad_labels[previous],
                                definition->desc_categorized));
               static const char *input_names[ARC_PAD_INPUT_COUNT] = {
                  "up", "down", "left", "right", "a", "b", "x", "y", "start", "select"
               };
               char expected_key[32];
               snprintf(expected_key, sizeof(expected_key), "arcdroid_pad%u_%s",
                        pads / ARC_PAD_INPUT_COUNT + 1u,
                        input_names[pads % ARC_PAD_INPUT_COUNT]);
               assert(!strcmp(definition->key, expected_key));
               size_t key_count = 0u;
               const ArcInputKey *keys = arc_input_supported_keys(&key_count);
               for (size_t key = 0u; key < key_count; ++key)
                  assert(definition->values[key + 2u].value &&
                         !strcmp(definition->values[key + 2u].value, keys[key].value) &&
                         !strcmp(definition->values[key + 2u].label, keys[key].label));
               assert(!definition->values[key_count + 2u].value);
               categorized_pad_labels[pads++] = definition->desc_categorized;
            }
         }
         assert(found_osk && found_chooser && found_short_tail && found_jfd_omitted && found_hard_disc && found_ramdisc && found_jfd_boot &&
                found_adffs_package && pads == 20u);
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
         bool mapped[ARC_PAD_PORTS][ARC_PAD_INPUT_COUNT] = {{false}};
         bool port0 = false, port1 = false, select = false, adjust = false;
         bool menu = false, precision = false, right_x = false, right_y = false;
         bool left_x = false, left_y = false;
         for (; descriptors->description; ++descriptors)
         {
            if (descriptors->port < ARC_PAD_PORTS &&
                descriptors->device == RETRO_DEVICE_JOYPAD)
               for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
                  if (descriptors->id == mapping_input_ids[input])
                  {
                     snprintf(effective_descriptor_labels[descriptors->port][input],
                              sizeof(effective_descriptor_labels[0][0]), "%s",
                              descriptors->description);
                     mapped[descriptors->port][input] = true;
                  }
            if (descriptors->port == 0u) port0 = true;
            if (descriptors->port == 1u) port1 = true;
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
         for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
            for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
               assert(mapped[port][input]);
         saw_input_descriptors = port0 && port1 && select && adjust && menu &&
                                 precision && right_x && right_y && left_x && left_y;
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
         if (!strcmp(variable->key, "arcdroid_install_saves")) {
            ++stale_install_option_queries;
            variable->value = stale_install_option_value;
            return true;
         }
         if (!strcmp(variable->key, "arcdroid_input_profile"))
         {
            ++stale_input_option_queries;
            variable->value = stale_input_option_value;
            return true;
         }
         if (legacy_only_options &&
             (!strcmp(variable->key, "arcdroid_osk_toggle") ||
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
         else if (!strcmp(variable->key, "arcdroid_hard_disc")) variable->value = option_hard_disc;
         else if (!strcmp(variable->key, "arcdroid_short_tail")) variable->value = option_short_tail;
         else if (!strcmp(variable->key, "arcdroid_jfd_omitted")) variable->value = option_jfd_omitted;
         else if (!strcmp(variable->key, "arcdroid_launch_ramdisc"))
            variable->value = option_launch_ramdisc;
         else if (!strcmp(variable->key, "arcdroid_launch")) variable->value = option_launch;
         else if (!strcmp(variable->key, "arcdroid_launch_chooser"))
            variable->value = option_launch_chooser;
         else if (!strcmp(variable->key, "arcdroid_jfd_boot"))
            variable->value = option_jfd_boot;
         else if (!strcmp(variable->key, "arcdroid_adffs_package"))
            variable->value = option_adffs_package;
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
         read_cached_video();
         if (probe_cached_video && cached_video) ++cached_video_geometry_reads;
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
         read_cached_video();
         if (probe_cached_video && cached_video) ++cached_video_av_reads;
         const struct retro_system_av_info *av = data;
         assert(phase == PHASE_RUN && av->timing.fps >= 20.0 && av->timing.fps <= 200.0);
         assert(av->timing.fps < last_notified_rate - 0.01 ||
                av->timing.fps > last_notified_rate + 0.01);
         if (measure_av) av_rate_changed_during_measure = true;
         geometry_width = av->geometry.base_width;
         geometry_height = av->geometry.base_height;
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
         ++total_osd_calls;
         snprintf(last_message, sizeof(last_message), "%s", ((struct retro_message_ext *)data)->msg);
         if (!strncmp(last_message, "Hard disc unavailable:", 22u))
            ++hard_disc_unavailable_notices;
         if (!strncmp(last_message, "Saved hard disc is damaged;", 27u))
            ++hard_disc_damaged_notices;
         if (!strncmp(last_message, "Hard disc is read-only;", 23u))
            ++hard_disc_readonly_notices;
         if (!strncmp(last_message,
                      "Hard disc saves are no longer possible this session:", 52u))
            ++hard_disc_write_failure_notices;
         if (strstr(last_message, "quarantine")) ++jfd_quarantine_notice_calls;
         if (strstr(last_message, "can no longer be saved this session"))
         {
            const struct retro_message_ext *message = data;
            assert(message->type == RETRO_MESSAGE_TYPE_STATUS &&
                   message->duration >= 3600000u &&
                   message->target == RETRO_MESSAGE_TARGET_OSD);
            ++jfd_failed_status_calls;
         }
         if (strstr(last_message, "saved disc-change file is damaged"))
            ++jfd_damaged_notice_calls;
         if (strstr(last_message, "hidden while Disk Writes is Off"))
            ++jfd_off_notice_calls;
         if (strstr(last_message, "booting this JFD natively"))
            ++jfd_native_notice_calls;
         if (strstr(last_message, "flagged for RiscPC-class machines"))
            ++riscpc_osd_count;
         if (strstr(last_message, "ADFFS boot unavailable") ||
             strstr(last_message, "JASPP boot script") ||
             strstr(last_message, "may boot better with ADFFS"))
         {
            ++adffs_notice_calls;
            snprintf(last_adffs_notice, sizeof(last_adffs_notice), "%s", last_message);
         }
         if (!strncmp(last_message, "Folder disc set", 15u))
            ++folder_set_warning_osd_calls;
         if (!strncmp(last_message, "Zip content loaded with", 23u))
            ++zip_summary_osd_calls;
         if (!strncmp(last_message, "Zip preferred image ", 20u))
         {
            ++zip_fallback_osd_calls;
            snprintf(last_zip_fallback, sizeof(last_zip_fallback), "%s",
                     last_message);
         }
         if (strstr(last_message, "VIDC")) ++vidc_osd_ext_calls;
         if (strstr(last_message, "Image is missing its last ")) ++short_warning_osd_calls;
         if (strstr(last_message, "Unable to flush the staged floppy image"))
         {
            ++flush_fault_osd_calls;
            snprintf(last_flush_fault, sizeof(last_flush_fault), "%s", last_message);
         }
         if (strstr(last_message, "On-screen keyboard disabled:")) ++osk_failure_osd_calls;
         if (strstr(last_message, "unavailable below 320")) ++osk_narrow_osd_calls;
         if (!strncmp(last_message, "Controls: ", 10u)) ++controls_osd_calls;
         if (strstr(last_message, "Application choice refused") ||
             strstr(last_message, "No unique application") ||
             strstr(last_message, "Application inspection incomplete"))
         {
            ++launch_warning_osd_calls;
            snprintf(last_launch_warning, sizeof(last_launch_warning), "%s", last_message);
         }
         return true;
      case RETRO_ENVIRONMENT_SET_MESSAGE:
         ++total_osd_calls;
         snprintf(last_message, sizeof(last_message), "%s", ((struct retro_message *)data)->msg);
         if (strstr(last_message, "flagged for RiscPC-class machines"))
            ++riscpc_osd_count;
         if (strstr(last_message, "ADFFS boot unavailable") ||
             strstr(last_message, "JASPP boot script") ||
             strstr(last_message, "may boot better with ADFFS"))
         {
            ++adffs_notice_calls;
            snprintf(last_adffs_notice, sizeof(last_adffs_notice), "%s", last_message);
         }
         if (!strncmp(last_message, "Folder disc set", 15u))
            ++folder_set_warning_osd_calls;
         if (!strncmp(last_message, "Zip content loaded with", 23u))
            ++zip_summary_osd_calls;
         if (!strncmp(last_message, "Zip preferred image ", 20u))
         {
            ++zip_fallback_osd_calls;
            snprintf(last_zip_fallback, sizeof(last_zip_fallback), "%s",
                     last_message);
         }
         if (strstr(last_message, "VIDC")) ++vidc_osd_legacy_calls;
         if (strstr(last_message, "Image is missing its last ")) ++short_warning_osd_calls;
         if (strstr(last_message, "Unable to flush the staged floppy image"))
         {
            ++flush_fault_osd_calls;
            snprintf(last_flush_fault, sizeof(last_flush_fault), "%s", last_message);
         }
         if (!strncmp(last_message, "Controls: ", 10u)) ++controls_osd_calls;
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
   if (!data) {
      read_cached_video();
      if (probe_cached_video && cached_video) ++cached_video_duplicate_reads;
      ++duplicate_video_frames; ++video_frames; return;
   }
   if (probe_cached_video) {
      cached_video = data;
      cached_video_height = h;
      cached_video_pitch = pitch;
   }
   memcpy(&last_video_pixel, data, sizeof(last_video_pixel));
   last_video_pixel &= 0x00ffffffu;
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

static void make_boundary_paths(size_t parent_size, char parent[4096],
                                char content[4096])
{
   static const char leaf[] = "Bound.adf";
   assert(parent_size > sizeof(leaf) + 1u);
   size_t filler = parent_size - sizeof(leaf) - 1u;
   parent[0] = '/';
   memset(parent + 1u, 'p', filler);
   parent[1u + filler] = '/';
   memcpy(parent + 2u + filler, leaf, sizeof(leaf) - 1u);
   parent[parent_size] = '\0';
   int written = snprintf(content, 4096u, "%s/%s", parent, leaf);
   assert(written > 0 && written < 4096);
}

static void read_exact(const char *path, void *data, size_t size)
{
   FILE *file = fopen(path, "rb"); assert(file);
   assert(fread(data, 1u, size, file) == size && fgetc(file) == EOF && !fclose(file));
}

static uint8_t *read_alloc(const char *path, size_t *size)
{
   struct stat status;
   assert(!stat(path, &status) && status.st_size >= 0);
   *size = (size_t)status.st_size;
   uint8_t *data = malloc(*size ? *size : 1u);
   assert(data);
   read_exact(path, data, *size);
   return data;
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

static bool file_exists(const char *path)
{
   struct stat status;
   return path && !stat(path, &status) && S_ISREG(status.st_mode);
}

static uint64_t hash_file(const char *path)
{
   FILE *f = fopen(path, "rb"); assert(f);
   uint64_t hash = UINT64_C(1469598103934665603);
   int byte;
   while ((byte = fgetc(f)) != EOF) { hash ^= (uint8_t)byte; hash *= UINT64_C(1099511628211); }
   assert(!ferror(f) && !fclose(f)); return hash;
}

typedef struct {
   uint64_t sum;
   uint64_t exclusive_or;
   unsigned files;
} TreeSnapshot;

static uint64_t hash_text(const char *text)
{
   uint64_t hash = UINT64_C(1469598103934665603);
   for (; *text; ++text)
   {
      hash ^= (uint8_t)*text;
      hash *= UINT64_C(1099511628211);
   }
   return hash;
}

static void snapshot_walk(const char *path, const char *relative,
                          bool ignore_cmos, TreeSnapshot *snapshot)
{
   DIR *directory = opendir(path);
   assert(directory);
   struct dirent *entry;
   while ((entry = readdir(directory)) != NULL)
   {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
      char child[4096], child_relative[4096];
      int child_n = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
      int relative_n = relative[0] ?
         snprintf(child_relative, sizeof(child_relative), "%s/%s", relative, entry->d_name) :
         snprintf(child_relative, sizeof(child_relative), "%s", entry->d_name);
      assert(child_n > 0 && (size_t)child_n < sizeof(child) &&
             relative_n > 0 && (size_t)relative_n < sizeof(child_relative));
      struct stat status;
      assert(stat(child, &status) == 0);
      if (S_ISDIR(status.st_mode))
         snapshot_walk(child, child_relative, ignore_cmos, snapshot);
      else if (S_ISREG(status.st_mode))
      {
         if (ignore_cmos && !strncmp(child_relative, "arcdroid/cmos/", 14u))
            continue;
         uint64_t value = hash_text(child_relative) ^ hash_file(child) ^
                          (uint64_t)status.st_size;
         snapshot->sum += value;
         snapshot->exclusive_or ^= value * UINT64_C(0x9e3779b97f4a7c15);
         ++snapshot->files;
      }
   }
   assert(closedir(directory) == 0);
}

static TreeSnapshot snapshot_tree_filtered(const char *path, bool ignore_cmos)
{
   TreeSnapshot snapshot = {0};
   snapshot_walk(path, "", ignore_cmos, &snapshot);
   return snapshot;
}

static TreeSnapshot snapshot_tree(const char *path)
{ return snapshot_tree_filtered(path, false); }

static bool same_snapshot(TreeSnapshot a, TreeSnapshot b)
{
   return a.sum == b.sum && a.exclusive_or == b.exclusive_or &&
          a.files == b.files;
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

static uint32_t hash_file_segment(const char *path, size_t offset, size_t size)
{
   FILE *file = fopen(path, "rb");
   assert(file && fseek(file, (long)offset, SEEK_SET) == 0);
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
   unsigned (*mouse_samples)(void);
} OskScriptApi;

static void print_usage(FILE *stream, const char *program)
{
   fprintf(stream,
      "Usage:\n"
      "  %s CORE ROM CONTENT DATA\n"
      "  %s CORE ROM CONTENT DATA --newio-smoke PLAYLIST DISC1 DISC2 APP\n"
      "  %s CORE SAVE_ROM RW_JFD DATA --jfd-writes FIXTURES\n"
      "  %s CORE ROM CONTENT DATA --real-boot "
      "[--machine A3000|A3010|A3020|A4000] "
      "[--joystick Keyboard|A3010] "
      "[--osk-script FILE]\n"
      "  %s --self-test-parser\n"
      "  %s CORE ROM CONTENT DATA MEDIA_ROM PLAYLIST DISC1 DISC2 JFD JFD2 "
      "FIXTURES APP ADFFS_PACKAGE ADFFS_JFD ZIP_FIXTURES\n"
      "Append --cached-frame-probe to make the frontend reread its cached frame on A/V changes and duplicate frames.\n"
      "\n"
      "OSK script grammar (one command per line; # begins a comment):\n"
      "  wait N                              run N emulated fields\n"
      "  open | close                        set OSK visibility\n"
      "  tap RETROK_NAME                     tap a verified OSK key\n"
      "  modifier RETROK_NAME off|oneshot|locked\n"
      "                                      set a sticky modifier state\n"
      "  screenshot NAME                     capture the following presented field\n"
      "  move DX DY                          send one libretro mouse delta\n"
      "  click MASK HOLD                     hold mouse buttons for HOLD fields\n"
      "                                      MASK: 1 Select, 2 Adjust, 4 Menu\n"
      "  pad BUTTON FIELDS                   hold port 0 RetroPad id 0-15 for FIELDS fields, then release for one field\n"
      "  drag MASK DX DY FIELDS             hold mouse mask 0-7 and send DX DY each field, then release for one field\n"
      "Screenshots are written as DATA/evidence/NAME.ppm. NAME may contain\n"
      "letters, digits, dot, underscore, or hyphen (but not . or ..).\n",
      program, program, program, program, program, program);
}

static bool script_unsigned(const char *word, unsigned long maximum,
                            bool allow_zero, unsigned long *value)
{
   if (!word || !*word || !value) return false;
   for (const unsigned char *p = (const unsigned char *)word; *p; ++p)
      if (!isdigit(*p)) return false;
   char *end = NULL;
   errno = 0;
   unsigned long parsed = strtoul(word, &end, 10);
   if (errno || !end || *end || parsed > maximum ||
       (!allow_zero && !parsed)) return false;
   *value = parsed;
   return true;
}

static bool script_delta(const char *word, long *value)
{
   if (!word || !*word || !value) return false;
   const unsigned char *p = (const unsigned char *)word;
   if (*p == '-' || *p == '+') ++p;
   if (!*p) return false;
   for (; *p; ++p) if (!isdigit(*p)) return false;
   char *end = NULL;
   errno = 0;
   long parsed = strtol(word, &end, 10);
   if (errno || !end || *end || parsed < INT16_MIN ||
       parsed > INT16_MAX) return false;
   *value = parsed;
   return true;
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
   /* Small-step desktop clicks and in-game clicks register in the private
    * guest probe. These commands also verify core input delivery in CI. */
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
      char *fourth = strtok_r(NULL, " \t\r\n", &save);
      char *fifth = strtok_r(NULL, " \t\r\n", &save);
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
      else if (!strcmp(command, "move") && first && second && !extra)
      {
         char *end_x = NULL, *end_y = NULL;
         long dx = strtol(first, &end_x, 10);
         long dy = strtol(second, &end_y, 10);
         if (!first[0] || !second[0] || !end_x || *end_x || !end_y || *end_y ||
             dx < INT16_MIN || dx > INT16_MAX ||
             dy < INT16_MIN || dy > INT16_MAX || (!dx && !dy)) ok = false;
         else
         {
            unsigned before = api->mouse_samples();
            mouse_port_x[1] = (int)dx;
            mouse_port_y[1] = (int)dy;
            api->run();
            ok = api->mouse_samples() > before &&
                 !mouse_port_x[1] && !mouse_port_y[1];
         }
      }
      else if (!strcmp(command, "click") && first && second && !extra)
      {
         char *end_mask = NULL, *end_hold = NULL;
         unsigned long mask = strtoul(first, &end_mask, 10);
         unsigned long hold = strtoul(second, &end_hold, 10);
         if (!first[0] || !second[0] || !end_mask || *end_mask ||
             !end_hold || *end_hold || !mask || mask > 7u ||
             !hold || hold > 1000000u) ok = false;
         else
         {
            unsigned before = api->mouse_samples();
            mouse_port_buttons[1] = (unsigned)mask;
            for (unsigned long field = 0u; field < hold; ++field) api->run();
            mouse_port_buttons[1] = 0u;
            api->run();
            ok = api->mouse_samples() >= before + hold;
         }
      }
      else if (!strcmp(command, "pad") && first && second && !extra)
      {
         unsigned long button, fields;
         if (!script_unsigned(first, 15u, true, &button) ||
             !script_unsigned(second, 1000000u, false, &fields)) ok = false;
         else
         {
            joypad_buttons[0][button] = true;
            for (unsigned long field = 0u; field < fields; ++field) api->run();
            joypad_buttons[0][button] = false;
            api->run();
         }
      }
      else if (!strcmp(command, "drag") && first && second && extra &&
               fourth && !fifth)
      {
         unsigned long mask, fields;
         long dx, dy;
         if (!script_unsigned(first, 7u, true, &mask) ||
             !script_delta(second, &dx) || !script_delta(extra, &dy) ||
             !script_unsigned(fourth, 1000000u, false, &fields)) ok = false;
         else
         {
            unsigned before = api->mouse_samples();
            mouse_port_buttons[1] = (unsigned)mask;
            for (unsigned long field = 0u; field < fields; ++field)
            {
               mouse_port_x[1] = (int)dx;
               mouse_port_y[1] = (int)dy;
               api->run();
            }
            mouse_port_buttons[1] = 0u;
            mouse_port_x[1] = mouse_port_y[1] = 0;
            api->run();
            ok = api->mouse_samples() >= before + fields;
         }
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

static bool parse_real_boot_options(int count, char *const args[],
                                    const char **osk_script)
{
   bool machine_seen = false, joystick_seen = false;
   bool osk_script_seen = false;
   for (int argument = 0; argument < count; ++argument)
   {
      if (!strcmp(args[argument], "--machine") && !machine_seen &&
          argument + 1 < count)
      {
         const char *machine = args[++argument];
         if (strcmp(machine, "A3000") && strcmp(machine, "A3010") &&
             strcmp(machine, "A3020") && strcmp(machine, "A4000")) return false;
         option_machine = machine;
         machine_seen = true;
      }
      else if (!strcmp(args[argument], "--joystick") && !joystick_seen &&
               argument + 1 < count)
      {
         const char *joystick = args[++argument];
         if (strcmp(joystick, "Keyboard") && strcmp(joystick, "A3010"))
            return false;
         option_joystick = joystick;
         joystick_seen = true;
      }
      else if (!strcmp(args[argument], "--osk-script") &&
               !osk_script_seen && argument + 1 < count)
      {
         *osk_script = args[++argument];
         osk_script_seen = true;
      }
      else if (!strcmp(args[argument], "--boot-cancel-input") &&
               !real_boot_cancel_input)
         real_boot_cancel_input = true;
      else if (!strcmp(args[argument], "--boot-cancel-shift") &&
               !real_boot_cancel_shift)
         real_boot_cancel_shift = true;
      else if (!strcmp(args[argument], "--boot-user-shift") &&
               !real_boot_user_shift && argument + 1 < count)
      {
         real_boot_user_shift = args[++argument];
         if (strcmp(real_boot_user_shift, "early") &&
             strcmp(real_boot_user_shift, "late")) return false;
      }
      else if (!strcmp(args[argument], "--boot-incomplete-fields") &&
               !real_boot_incomplete_fields)
         real_boot_incomplete_fields = true;
      else if (!strcmp(args[argument], "--boot-reset-once") &&
               !real_boot_reset_once)
         real_boot_reset_once = true;
      else if (!strcmp(args[argument], "--boot-reset-at") &&
               argument + 1 < count)
      {
         char *end = NULL;
         unsigned long value = strtoul(args[++argument], &end, 10);
         if (!end || *end || value < 1u || value > 149u) return false;
         real_boot_reset_frame = (unsigned)value;
      }
      else if (!strcmp(args[argument], "--boot-ramdisc") && argument + 1 < count)
      {
         const char *value = args[++argument];
         if (strcmp(value, "Auto") && strcmp(value, "Disabled")) return false;
         option_launch_ramdisc = value;
      }
      else if (!strcmp(args[argument], "--boot-ramdisc-toggle-reset") &&
               !real_boot_ramdisc_toggle_reset)
         real_boot_ramdisc_toggle_reset = true;
      else if (!strcmp(args[argument], "--boot-chooser-pick") &&
               !real_boot_chooser_pick && !real_boot_chooser_continue)
         real_boot_chooser_pick = true;
      else if (!strcmp(args[argument], "--boot-chooser-pick-row") &&
               !real_boot_chooser_pick && !real_boot_chooser_continue &&
               argument + 1 < count)
      {
         const char *row=args[++argument];
         if (strcmp(row,"0") && strcmp(row,"1") && strcmp(row,"2"))
            return false;
         real_boot_chooser_pick=true;
         real_boot_chooser_pick_row=(unsigned)(row[0]-'0');
      }
      else if (!strcmp(args[argument], "--boot-chooser-continue") &&
               !real_boot_chooser_continue && !real_boot_chooser_pick)
         real_boot_chooser_continue = true;
      else if (!strcmp(args[argument], "--boot-chooser-hold-confirm") &&
               !real_boot_chooser_hold_confirm)
         real_boot_chooser_hold_confirm = true;
      else if (!strcmp(args[argument], "--boot-chooser-reset") &&
               !real_boot_chooser_reset && !real_boot_chooser_unload)
         real_boot_chooser_reset = true;
      else if (!strcmp(args[argument], "--boot-chooser-unload") &&
               !real_boot_chooser_unload && !real_boot_chooser_reset)
         real_boot_chooser_unload = true;
      else if (!strcmp(args[argument], "--boot-cmos-write-fail") &&
               !real_boot_cmos_write_fail)
         real_boot_cmos_write_fail = true;
      else if (!strcmp(args[argument], "--boot-package") &&
               !real_boot_package && argument + 1 < count)
         real_boot_package = args[++argument];
      else if (!strcmp(args[argument], "--boot-expect-disabled") &&
               !real_boot_expect_disabled)
         real_boot_expect_disabled = true;
      else if (!strcmp(args[argument], "--boot-fail-construct") &&
               !real_boot_fail_construct)
         real_boot_fail_construct = true;
      else if (!strcmp(args[argument], "--boot-fail-hd-recover") &&
               !real_boot_fail_hd_recover)
         real_boot_fail_hd_recover = true;
      else if (!strcmp(args[argument], "--boot-hd-preflight-collision") &&
               !real_boot_hd_collision)
         real_boot_hd_collision = true;
      else if (!strcmp(args[argument], "--boot-fail-record-write") &&
               !fault_write_substring)
         fault_write_substring = ".boot.tmp";
      else if (!strcmp(args[argument], "--boot-fail-record-remove") &&
               !fault_remove_substring)
         fault_remove_substring = ".boot";
      else if (!strcmp(args[argument], "--boot-cmos-sentinel") &&
               !real_boot_cmos_sentinel)
         real_boot_cmos_sentinel = true;
      else if (!strcmp(args[argument], "--boot-cmos-ramdisc") &&
               !real_boot_cmos_ramdisc)
         real_boot_cmos_ramdisc = true;
      else if (!strcmp(args[argument], "--boot-chooser-ask"))
         option_launch_chooser = "ask";
      else if (!strcmp(args[argument], "--boot-frames") &&
               argument + 1 < count)
      {
         char *end = NULL;
         unsigned long value = strtoul(args[++argument], &end, 10);
         if (!end || *end || value < 1u || value > 3000u) return false;
         real_boot_frames = (unsigned)value;
      }
      else if (!strcmp(args[argument], "--boot-disk-writes") &&
               argument + 1 < count)
      {
         const char *value = args[++argument];
         if (strcmp(value, "shadow") && strcmp(value, "off") &&
             strcmp(value, "original")) return false;
         option_writes = value;
      }
      else if (!strcmp(args[argument], "--boot-jfd-boot") &&
               argument + 1 < count)
      {
         const char *value = args[++argument];
         if (strcmp(value, "auto") && strcmp(value, "native") &&
             strcmp(value, "adffs")) return false;
         option_jfd_boot = value;
      }
      else if (!strcmp(args[argument], "--boot-cpu") && argument + 1 < count)
      {
         const char *value = args[++argument];
         if (strcmp(value, "ARM2") && strcmp(value, "ARM250")) return false;
         option_cpu = value;
      }
      else if (!strcmp(args[argument], "--boot-hard-disc-auto"))
         option_hard_disc = "auto";
      else if (!strcmp(args[argument], "--boot-seed-product"))
         real_boot_seed_product = true;
      else if (!strcmp(args[argument], "--boot-seed-record"))
         real_boot_seed_record = true;
      else if (!strcmp(args[argument], "--boot-expect-record-hit"))
         real_boot_expect_record_hit = true;
      else if (!strcmp(args[argument], "--boot-seed-started") &&
               !real_boot_seed_started && argument + 1 < count)
      {
         const char *value=args[++argument];
         real_boot_seed_started=!strcmp(value,"product") ? 1u :
            !strcmp(value,"copy") ? 2u :
            !strcmp(value,"extra") ? 3u : 0u;
         if (!real_boot_seed_started) return false;
      }
      else if (!strcmp(args[argument], "--boot-arm-hd-copy") &&
               !real_boot_arm_hd_copy)
         real_boot_arm_hd_copy=true;
      else if (!strcmp(args[argument], "--boot-hd-fail-at") &&
               real_boot_hd_fail_at < 0 && argument + 1 < count)
      {
         char *end=NULL;
         unsigned long value=strtoul(args[++argument],&end,10);
         if (!end || *end || value > 2999u) return false;
         real_boot_hd_fail_at=(int)value;
      }
      else if (!strcmp(args[argument], "--boot-hd-fail-after-install") &&
               !real_boot_hd_fail_after_install)
         real_boot_hd_fail_after_install=true;
      else if (!strcmp(args[argument], "--boot-rearm-after-install") &&
               !real_boot_rearm_after_install)
         real_boot_rearm_after_install=true;
      else if (!strcmp(args[argument], "--boot-hd-write") &&
               real_boot_hd_write_mode < 0 && argument + 1 < count)
      {
         const char *value = args[++argument];
         real_boot_hd_write_mode = !strcmp(value, "identical") ? 0 :
            !strcmp(value, "restore") ? 1 :
            !strcmp(value, "failed") ? 2 :
            !strcmp(value, "state-change") ? 3 :
            !strcmp(value, "compact-neutral") ? 4 : -1;
         if (real_boot_hd_write_mode < 0) return false;
      }
      else if (!strcmp(args[argument], "--boot-launch-off"))
         option_launch = "off";
      else if (!strcmp(args[argument], "--boot-launch-app1"))
         option_launch = "app1";
      else return false;
   }
   return true;
}

typedef struct {
   uint16_t pad;
   unsigned mouse_buttons;
   int dx, dy;
} ParserFrame;
static ParserFrame parser_frames[8];
static unsigned parser_frame_count;
static void parser_test_run(void)
{
   assert(parser_frame_count < sizeof(parser_frames) / sizeof(parser_frames[0]));
   ParserFrame *frame = &parser_frames[parser_frame_count++];
   for (unsigned button = 0u; button < 16u; ++button)
      if (joypad_buttons[0][button]) frame->pad |= (uint16_t)(1u << button);
   frame->mouse_buttons = mouse_port_buttons[1];
   frame->dx = mouse_port_x[1]; frame->dy = mouse_port_y[1];
   mouse_port_x[1] = mouse_port_y[1] = 0;
}
static unsigned parser_test_samples(void) { return parser_frame_count; }
static void parser_test_line(const char *line, bool expected)
{
   char path[] = "/tmp/arcdroid-osk-parser-XXXXXX";
   int descriptor = mkstemp(path);
   assert(descriptor >= 0);
   FILE *file = fdopen(descriptor, "wb");
   assert(file && fputs(line, file) >= 0 && fputc('\n', file) != EOF &&
          !fclose(file));
   memset(parser_frames, 0, sizeof(parser_frames));
   parser_frame_count = 0u;
   OskScriptApi api = {0};
   api.run = parser_test_run;
   api.mouse_samples = parser_test_samples;
   assert(run_osk_script(path, &api) == expected);
   assert(!unlink(path));
   assert(!joypad_buttons[0][5] && !mouse_port_buttons[1] &&
          !mouse_port_x[1] && !mouse_port_y[1]);
   if (!expected) assert(parser_frame_count == 0u);
}
static void parser_self_test(void)
{
   const char *script = NULL;
   parser_test_line("pad 5 3", true);
   assert(parser_frame_count == 4u);
   for (unsigned i = 0u; i < 3u; ++i)
      assert(parser_frames[i].pad == (1u << 5));
   assert(!parser_frames[3].pad);
   parser_test_line("drag 3 -12 7 2", true);
   assert(parser_frame_count == 3u);
   for (unsigned i = 0u; i < 2u; ++i)
      assert(parser_frames[i].mouse_buttons == 3u &&
             parser_frames[i].dx == -12 && parser_frames[i].dy == 7);
   assert(!parser_frames[2].mouse_buttons &&
          !parser_frames[2].dx && !parser_frames[2].dy);
   parser_test_line("drag 0 -32768 32767 1", true);
   assert(parser_frame_count == 2u &&
          parser_frames[0].dx == INT16_MIN &&
          parser_frames[0].dy == INT16_MAX);
   static const char *const invalid_lines[] = {
      "pad 16 1", "pad -1 1", "pad five 1", "pad 5 0",
      "pad 5 1x", "pad 5 1 extra", "drag 8 1 1 1",
      "drag 1 32768 0 1", "drag 1 1x 0 1", "drag 1 1 1 0",
      "drag 1 1 1 1 extra"
   };
   for (unsigned i = 0u; i < sizeof(invalid_lines) / sizeof(invalid_lines[0]); ++i)
      parser_test_line(invalid_lines[i], false);
   puts("libretro_harness: parser self-test passed");
}

#define LOAD(name) do { *(void **)(&name) = dlsym(module, #name); assert(name); } while (0)
int main(int argc, char **argv)
{
   if (argc > 2 && !strcmp(argv[argc - 1], "--cached-frame-probe")) {
      probe_cached_video = true;
      --argc;
   }
   if (argc == 2 && !strcmp(argv[1], "--help"))
   {
      print_usage(stdout, argv[0]);
      return 0;
   }
   if (argc == 2 && !strcmp(argv[1], "--self-test-parser"))
   {
      parser_self_test();
      return 0;
   }
   bool real_boot = false;
   bool input_layout_probe = false;
   bool helper_stress = false;
   bool newio_smoke = false;
   bool jfd_writes = false;
   bool jfd_create_step = false;
   bool jfd_merge_step = false;
   bool jfd_allocation_step = false;
   bool jfd_identification_step = false;
   bool jfd_file_data_step = false;
   bool jfd_file_data_set = false;
   bool jfd_file_data_reasons = false;
   bool jfd_file_data_recovery = false;
   bool jfd_file_data_unavailable = false;
   bool jfd_allocation_set = false;
   bool native_hd_guest = false;
   bool cmos_exerciser = false;
   bool short_tail_guest = false;
   bool short_tail_newio_guest = false;
   bool short_tail_discovery = false;
   bool jfd_quarantine_guest = false, jfd_quarantine_newio = false;
   bool jfd_quarantine_density = false;
   bool jfd_quarantine_adffs = false;
   char jfd_content_path[4096] = {0}, jfd_fixture_path[4096] = {0};
   const char *osk_script = NULL;
   bool power_on_cmos_guest = argc == 7 && !strcmp(argv[5], "--power-on-cmos-guest");
   if (power_on_cmos_guest)
   {
      assert(getenv("ARCDROID_TEST_POWER_ON_SHIFT") &&
             !strcmp(getenv("ARCDROID_TEST_POWER_ON_SHIFT"), "1"));
   }
   else if (argc == 6 && !strcmp(argv[5], "--input-layout-probe"))
      input_layout_probe = true;
   else if (argc == 17 && !strcmp(argv[16], "--helper-stress"))
      helper_stress = true;
   else if (argc == 11 && !strcmp(argv[5], "--newio-smoke"))
      newio_smoke = true;
   else if (argc == 7 && !strcmp(argv[5], "--native-hd-guest"))
      native_hd_guest = true;
   else if (argc == 7 && !strcmp(argv[5], "--jfd-writes"))
      jfd_writes = true;
   else if (argc == 9 && !strcmp(argv[5], "--jfd-create-step"))
      jfd_create_step = true;
   else if (argc == 9 && !strcmp(argv[5], "--jfd-merge-step"))
      jfd_merge_step = true;
   else if (argc == 9 && !strcmp(argv[5], "--jfd-allocation-step"))
      jfd_allocation_step = true;
   else if (argc == 9 && !strcmp(argv[5], "--jfd-identification-step"))
      jfd_identification_step = true;
   else if (argc == 6 && !strcmp(argv[5], "--jfd-file-data-step"))
      jfd_file_data_step = true;
   else if (argc == 6 && !strcmp(argv[5], "--jfd-file-data-set"))
      jfd_file_data_set = true;
   else if (argc == 7 && !strcmp(argv[5], "--jfd-file-data-reasons"))
      jfd_file_data_reasons = true;
   else if (argc == 6 && !strcmp(argv[5], "--jfd-file-data-recovery"))
      jfd_file_data_recovery = true;
   else if (argc == 6 && !strcmp(argv[5], "--jfd-file-data-unavailable"))
      jfd_file_data_unavailable = true;
   else if (argc == 7 && !strcmp(argv[5], "--jfd-allocation-set"))
      jfd_allocation_set = true;
   else if (argc == 6 && !strcmp(argv[5], "--cmos-exerciser"))
      cmos_exerciser = true;
   else if (argc == 6 && !strcmp(argv[5], "--short-tail-guest"))
      short_tail_guest = true;
   else if (argc == 6 && !strcmp(argv[5], "--short-tail-newio-guest"))
      short_tail_guest = short_tail_newio_guest = true;
   else if (argc == 7 && !strcmp(argv[5], "--short-tail-discovery"))
      short_tail_discovery = true;
   else if (argc == 6 && (!strcmp(argv[5], "--jfd-quarantine-wd") ||
                           !strcmp(argv[5], "--jfd-quarantine-newio")))
   {
      jfd_quarantine_guest = true;
      jfd_quarantine_newio = !strcmp(argv[5], "--jfd-quarantine-newio");
   }
   else if (argc == 7 && !strcmp(argv[5], "--jfd-quarantine-density"))
      jfd_quarantine_density = true;
   else if (argc == 7 && !strcmp(argv[5], "--jfd-quarantine-adffs"))
      jfd_quarantine_adffs = true;
   else if (argc >= 6 && !strcmp(argv[5], "--real-boot"))
   {
      real_boot = true;
      if (!parse_real_boot_options(argc - 6, argv + 6, &osk_script))
      {
         print_usage(stderr, argv[0]);
         return 2;
      }
   }
   else if (argc != 5 && argc != 16)
   {
      print_usage(stderr, argv[0]);
      return 2;
   }
   bool media_tests = !real_boot && (argc == 16 || helper_stress);
   if (jfd_writes)
   {
      assert(strlen(argv[3]) < sizeof(jfd_content_path) &&
             strlen(argv[6]) < sizeof(jfd_fixture_path));
      strcpy(jfd_content_path, argv[3]);
      strcpy(jfd_fixture_path, argv[6]);
   }
   real_boot_mode = real_boot;
   void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!module)
   {
      fprintf(stderr, "libretro_harness: dlopen failed: %s\n", dlerror());
      return 2;
   }
   char system_path[4096], firmware_path[4096], save_path[4096], renamed[4096], cwd[4096], cwd_after[4096];
   char jfd_run_path[4096], cmos_run_path[4096];
   const char *data_root = argv[4];
   if (jfd_writes || jfd_file_data_recovery || jfd_file_data_unavailable)
   {
      make_directory(argv[4]);
      assert(snprintf(jfd_run_path, sizeof(jfd_run_path), "%s/jfd-run-XXXXXX", argv[4]) < (int)sizeof(jfd_run_path));
      assert(mkdtemp(jfd_run_path));
      data_root = jfd_run_path;
   }
   if (cmos_exerciser || power_on_cmos_guest)
   {
      make_directory(argv[4]);
      assert(snprintf(cmos_run_path, sizeof(cmos_run_path), "%s/cmos-run-XXXXXX", argv[4]) < (int)sizeof(cmos_run_path));
      assert(mkdtemp(cmos_run_path));
      data_root = cmos_run_path;
   }
   snprintf(system_path, sizeof(system_path), "%s/system", data_root);
   snprintf(firmware_path, sizeof(firmware_path), "%s/arcdroid", system_path);
   snprintf(save_path, sizeof(save_path), "%s/save", data_root);
   make_directory(argv[4]); make_directory(system_path); make_directory(firmware_path);
   strncat(firmware_path, "/firmware", sizeof(firmware_path) - strlen(firmware_path) - 1u); make_directory(firmware_path);
   make_directory(save_path);
   if (media_tests || real_boot_package || jfd_quarantine_adffs)
   {
      char adffs_dir[4096], package_path[4096];
      snprintf(adffs_dir, sizeof(adffs_dir), "%s/arcdroid/adffs", system_path);
      make_directory(adffs_dir);
      snprintf(package_path, sizeof(package_path), "%s/synthetic.zip", adffs_dir);
      copy_file(jfd_quarantine_adffs ? argv[6] :
                real_boot_package ? real_boot_package : argv[13], package_path);
   }
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
   void (*retro_get_system_info)(struct retro_system_info *); LOAD(retro_get_system_info);
   bool (*retro_load_game)(const struct retro_game_info *); LOAD(retro_load_game);
   void (*retro_unload_game)(void); LOAD(retro_unload_game);
   void (*retro_reset)(void); LOAD(retro_reset);
   void (*retro_run)(void); LOAD(retro_run);
   void (*retro_cheat_reset)(void); LOAD(retro_cheat_reset);
   void (*retro_cheat_set)(unsigned, bool, const char *); LOAD(retro_cheat_set);
   void (*retro_get_system_av_info)(struct retro_system_av_info *); LOAD(retro_get_system_av_info);
   void (*retro_set_controller_port_device)(unsigned, unsigned); LOAD(retro_set_controller_port_device);
   size_t (*retro_serialize_size)(void); LOAD(retro_serialize_size);
   void *(*retro_get_memory_data)(unsigned); LOAD(retro_get_memory_data);
   size_t (*retro_get_memory_size)(unsigned); LOAD(retro_get_memory_size);
   void (*arc_engine_audio_stats)(uint64_t *, uint64_t *) = NULL;
   bool (*arc_engine_media_insert)(const ArcEngineMedia *, char *, size_t) = NULL;
   void (*arc_engine_test_set_vidc_noncompleting)(bool) = NULL;
   bool (*arc_engine_test_drive_media)(unsigned, bool *, char[41]) = NULL;
   bool (*arc_libretro_test_jfd_commit)(unsigned, size_t, uint8_t) = NULL;
   bool (*arc_libretro_test_jfd_formatted_reads)(unsigned) = NULL;
   void (*arc_libretro_test_jfd_wp_layout_supported)(bool) = NULL;
   uint8_t (*arc_libretro_test_jfd_probe_mask)(unsigned, unsigned) = NULL;
   bool (*arc_libretro_test_jfd_created_byte)(unsigned, unsigned,
                                              unsigned, unsigned,
                                              uint8_t *) = NULL;
   bool (*arc_libretro_test_jfd_presented)(unsigned, unsigned,
                                            uint32_t *) = NULL;
   uint8_t (*arc_libretro_test_jfd_slot_state)(unsigned, unsigned, unsigned,
                                               uint32_t *, uint32_t *) = NULL;
   bool (*arc_engine_test_helper_media_valid)(ArcMediaKind,
                                               ArcMediaGeometry) = NULL;
   bool (*arc_engine_test_cmos_image)(uint8_t[ARC_CMOS_MACHINE_SIZE]) = NULL;
   bool (*arc_engine_test_cmos_write)(unsigned, uint8_t) = NULL;
   void (*arc_engine_test_boot_watch_snapshot)(ArcBootWatch *) = NULL;
   void (*arc_libretro_test_fail_next_construct)(void) = NULL;
   bool (*arc_engine_test_cmos_bus_write)(unsigned, uint8_t) = NULL;
   bool (*arc_engine_reset)(char *, size_t) = NULL;
   bool (*arc_engine_start)(const uint8_t *, size_t, const ArcEngineMedia *,
                            const ArcEngineMedia *, const ArcMachineDescriptor *,
                            const ArcIdeBackend *,
                            const uint8_t *, const uint8_t *,
                            ArcEngineCmosIgnoredCallback, void *, char *, size_t) = NULL;
   void (*arc_engine_stop)(void) = NULL;
   bool (*arc_engine_test_newio)(unsigned *, unsigned *, unsigned *, unsigned *, uint8_t *) = NULL;
   bool (*arc_engine_test_arm2_swp_undefined)(void) = NULL;
   bool (*arc_engine_test_newio_dir)(bool, uint8_t *) = NULL;
   bool (*arc_engine_test_newio_read_start)(unsigned, uint8_t) = NULL;
   bool (*arc_engine_test_newio_result)(uint8_t[7], bool *) = NULL;
   bool (*arc_engine_test_newio_read_finish)(size_t, uint32_t *, uint8_t[7]) = NULL;
   bool (*arc_engine_test_joystick)(unsigned, uint8_t *) = NULL;
   bool (*arc_engine_test_ioeb_clock)(unsigned) = NULL;
   unsigned (*arc_libretro_test_mouse_input_samples)(void) = NULL;
   unsigned (*arc_engine_test_vidc_clock)(void) = NULL;
   void (*arc_libretro_test_fail_osk_allocations)(unsigned) = NULL;
   void (*arc_libretro_test_drop_osk_surface)(void) = NULL;
   bool (*arc_libretro_test_osk_source_unchanged)(void) = NULL;
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
   const char *(*arc_libretro_test_content_id)(void) = NULL;
   const char *(*arc_libretro_test_staged_path)(unsigned) = NULL;
   bool (*arc_libretro_test_boot_media_digest)(char[41]) = NULL;
   bool (*arc_libretro_test_boot_stamp)(ArcBootObservation *) = NULL;
   bool (*arc_libretro_test_boot_start_stamp)(ArcBootObservation *) = NULL;
   bool (*arc_libretro_test_boot_record_hit)(void) = NULL;
   bool (*arc_libretro_test_hd_exhaust_journal)(void) = NULL;
   bool (*arc_libretro_test_hd_observation_write)(unsigned) = NULL;
   bool (*arc_libretro_test_seed_hd_product)(char[41]) = NULL;
   bool (*arc_libretro_test_seed_hd_started)(unsigned,bool,char[41]) = NULL;
   bool (*arc_libretro_test_repeat_hd_outcome)(void) = NULL;
   void (*arc_install_store_test_corrupt_native_format)(bool) = NULL;
   const char *(*arc_libretro_test_cmos_path)(void) = NULL;
   uint8_t (*arc_libretro_test_prepared_cmos_byte)(unsigned) = NULL;
   const char *(*arc_libretro_test_choice_path)(void) = NULL;
   void (*arc_zip_test_fail_member_allocations)(unsigned) = NULL;
   unsigned (*arc_libretro_test_session_state)(void) = NULL;
   unsigned (*arc_libretro_test_chooser_focus)(void) = NULL;
   unsigned (*arc_libretro_test_chooser_rows)(void) = NULL;
   void (*arc_libretro_test_force_input_reconcile)(void) = NULL;
   void (*arc_libretro_test_adffs_accept_test_os)(bool) = NULL;
   void (*arc_libretro_test_fail_adffs_content_read)(bool) = NULL;
   bool (*arc_libretro_test_adffs_content_read_failure_pending)(void) = NULL;
   bool (*arc_libretro_test_adffs_released)(void) = NULL;
   bool (*arc_libretro_test_adffs_carrier_matches)(const void *, size_t) = NULL;
   void (*arc_platform_test_input_events_reset)(void) = NULL;
   size_t (*arc_platform_test_input_events)(ArcKartEvent *, size_t) = NULL;
   if (real_boot || newio_smoke || native_hd_guest || media_tests)
      *(void **)(&arc_engine_test_newio) = dlsym(module, "arc_engine_test_newio");
   if (native_hd_guest) {
      *(void **)(&arc_libretro_test_boot_stamp) =
         dlsym(module, "arc_libretro_test_boot_stamp");
      *(void **)(&arc_libretro_test_hd_exhaust_journal) =
         dlsym(module, "arc_libretro_test_hd_exhaust_journal");
      *(void **)(&arc_libretro_test_hd_observation_write) =
         dlsym(module, "arc_libretro_test_hd_observation_write");
      *(void **)(&arc_install_store_test_corrupt_native_format) =
         dlsym(module, "arc_install_store_test_corrupt_native_format");
      assert(arc_libretro_test_boot_stamp &&
             arc_libretro_test_hd_exhaust_journal &&
             arc_libretro_test_hd_observation_write &&
             arc_install_store_test_corrupt_native_format);
   }
   if (real_boot_hd_write_mode >= 0 || real_boot_hd_fail_at >= 0 ||
       real_boot_hd_fail_after_install) {
      *(void **)(&arc_libretro_test_hd_observation_write) =
         dlsym(module, "arc_libretro_test_hd_observation_write");
      assert(arc_libretro_test_hd_observation_write);
   }
   if (real_boot_seed_product) {
      *(void **)(&arc_libretro_test_seed_hd_product) =
         dlsym(module,"arc_libretro_test_seed_hd_product");
      assert(arc_libretro_test_seed_hd_product);
   }
   if (real_boot_seed_record) {
      *(void **)(&arc_libretro_test_boot_start_stamp) =
         dlsym(module,"arc_libretro_test_boot_start_stamp");
      *(void **)(&arc_libretro_test_content_id) =
         dlsym(module,"arc_libretro_test_content_id");
      assert(arc_libretro_test_boot_start_stamp && arc_libretro_test_content_id);
   }
   if (real_boot_expect_record_hit) {
      *(void **)(&arc_libretro_test_boot_record_hit) =
         dlsym(module,"arc_libretro_test_boot_record_hit");
      assert(arc_libretro_test_boot_record_hit);
   }
   if (real_boot_seed_started) {
      *(void **)(&arc_libretro_test_seed_hd_started) =
         dlsym(module,"arc_libretro_test_seed_hd_started");
      assert(arc_libretro_test_seed_hd_started);
   }
   if (real_boot_rearm_after_install) {
      *(void **)(&arc_libretro_test_repeat_hd_outcome) =
         dlsym(module,"arc_libretro_test_repeat_hd_outcome");
      assert(arc_libretro_test_repeat_hd_outcome);
   }
   if (real_boot_hd_collision) {
      *(void **)(&inject_hd_preflight_collision) =
         dlsym(module,"arc_libretro_test_hd_preflight_collision");
      assert(inject_hd_preflight_collision);
   }
   if (native_hd_guest) {
      *(void **)(&arc_engine_test_cmos_image) =
         dlsym(module, "arc_engine_test_cmos_image");
      *(void **)(&arc_libretro_test_content_id) =
         dlsym(module, "arc_libretro_test_content_id");
      assert(arc_engine_test_cmos_image && arc_libretro_test_content_id);
   }
   if (real_boot)
   {
      if (real_boot_incomplete_fields)
      {
         *(void **)(&arc_engine_test_set_vidc_noncompleting) =
            dlsym(module, "arc_engine_test_set_vidc_noncompleting");
         assert(arc_engine_test_set_vidc_noncompleting);
      }
      *(void **)(&arc_engine_test_boot_watch_snapshot) =
         dlsym(module, "arc_engine_test_boot_watch_snapshot");
      assert(arc_engine_test_boot_watch_snapshot);
      if (real_boot_fail_construct || real_boot_fail_hd_recover)
      {
         *(void **)(&arc_libretro_test_fail_next_construct) =
            dlsym(module, "arc_libretro_test_fail_next_construct");
         assert(arc_libretro_test_fail_next_construct);
         if (real_boot_fail_hd_recover)
            inject_hd_construct_failure=arc_libretro_test_fail_next_construct;
      }
      if (real_boot_chooser_reset || real_boot_chooser_unload ||
          real_boot_cmos_write_fail)
      {
         *(void **)(&arc_libretro_test_prepared_cmos_byte) =
            dlsym(module, "arc_libretro_test_prepared_cmos_byte");
         *(void **)(&arc_libretro_test_cmos_path) =
            dlsym(module, "arc_libretro_test_cmos_path");
         *(void **)(&arc_libretro_test_session_state) =
            dlsym(module, "arc_libretro_test_session_state");
         assert(arc_libretro_test_prepared_cmos_byte &&
                arc_libretro_test_cmos_path && arc_libretro_test_session_state);
      }
   }
   if (real_boot && (real_boot_cmos_sentinel || real_boot_cmos_ramdisc))
   {
      *(void **)(&arc_engine_test_cmos_write) =
         dlsym(module, "arc_engine_test_cmos_write");
      *(void **)(&arc_engine_test_cmos_image) =
         dlsym(module, "arc_engine_test_cmos_image");
   }
   if (cmos_exerciser || power_on_cmos_guest)
   {
      *(void **)(&arc_engine_test_cmos_image) = dlsym(module, "arc_engine_test_cmos_image");
      *(void **)(&arc_engine_test_cmos_write) = dlsym(module, "arc_engine_test_cmos_write");
      *(void **)(&arc_engine_test_cmos_bus_write) = dlsym(module, "arc_engine_test_cmos_bus_write");
      *(void **)(&arc_engine_reset) = dlsym(module, "arc_engine_reset");
      *(void **)(&arc_engine_start) = dlsym(module, "arc_engine_start");
      *(void **)(&arc_engine_stop) = dlsym(module, "arc_engine_stop");
      *(void **)(&arc_libretro_test_cmos_path) = dlsym(module, "arc_libretro_test_cmos_path");
      assert(arc_engine_test_cmos_image && arc_engine_test_cmos_write &&
             arc_engine_test_cmos_bus_write &&
             arc_engine_reset && arc_engine_start && arc_engine_stop &&
             arc_libretro_test_cmos_path);
   }
   if (jfd_quarantine_guest)
   {
      *(void **)(&arc_engine_test_drive_media) =
         dlsym(module, "arc_engine_test_drive_media");
      assert(arc_engine_test_drive_media);
   }
   if (jfd_quarantine_adffs)
   {
      *(void **)(&arc_libretro_test_adffs_accept_test_os) =
         dlsym(module, "arc_libretro_test_adffs_accept_test_os");
      *(void **)(&arc_libretro_test_adffs_released) =
         dlsym(module, "arc_libretro_test_adffs_released");
      *(void **)(&arc_libretro_test_adffs_carrier_matches) =
         dlsym(module, "arc_libretro_test_adffs_carrier_matches");
      assert(arc_libretro_test_adffs_accept_test_os &&
             arc_libretro_test_adffs_released &&
             arc_libretro_test_adffs_carrier_matches);
   }
   if (short_tail_guest || short_tail_discovery || jfd_file_data_step ||
       jfd_file_data_unavailable)
   {
      *(void **)(&arc_libretro_test_staged_path) =
         dlsym(module, "arc_libretro_test_staged_path");
      assert(arc_libretro_test_staged_path);
      *(void **)(&arc_libretro_test_boot_media_digest) =
         dlsym(module, "arc_libretro_test_boot_media_digest");
      assert(arc_libretro_test_boot_media_digest);
   }
   if (input_layout_probe)
   {
      *(void **)(&arc_platform_test_input_events_reset) =
         dlsym(module, "arc_platform_test_input_events_reset");
      *(void **)(&arc_platform_test_input_events) =
         dlsym(module, "arc_platform_test_input_events");
      assert(arc_platform_test_input_events_reset && arc_platform_test_input_events);
   }
   else if (!cmos_exerciser && !power_on_cmos_guest && !native_hd_guest && !real_boot && !newio_smoke && !jfd_writes && !jfd_create_step &&
       !jfd_identification_step && !jfd_file_data_step && !jfd_file_data_set &&
       !jfd_file_data_reasons &&
       !jfd_file_data_recovery && !jfd_file_data_unavailable &&
       !jfd_merge_step && !jfd_allocation_step && !jfd_allocation_set &&
       !short_tail_guest && !short_tail_discovery &&
       !jfd_quarantine_guest && !jfd_quarantine_density &&
       !jfd_quarantine_adffs)
   {
      *(void **)(&arc_engine_audio_stats) = dlsym(module, "arc_engine_audio_stats");
      *(void **)(&arc_engine_media_insert) = dlsym(module, "arc_engine_media_insert");
      *(void **)(&arc_engine_test_set_vidc_noncompleting) =
         dlsym(module, "arc_engine_test_set_vidc_noncompleting");
      *(void **)(&arc_engine_test_drive_media) =
         dlsym(module, "arc_engine_test_drive_media");
      *(void **)(&arc_engine_test_helper_media_valid) =
         dlsym(module, "arc_engine_test_helper_media_valid");
      *(void **)(&arc_engine_test_cmos_image) =
         dlsym(module, "arc_engine_test_cmos_image");
      *(void **)(&arc_engine_test_cmos_write) =
         dlsym(module, "arc_engine_test_cmos_write");
      *(void **)(&arc_engine_test_cmos_bus_write) =
         dlsym(module, "arc_engine_test_cmos_bus_write");
      *(void **)(&arc_engine_reset) = dlsym(module, "arc_engine_reset");
      *(void **)(&arc_libretro_test_fail_osk_allocations) =
         dlsym(module, "arc_libretro_test_fail_osk_allocations");
      *(void **)(&arc_libretro_test_drop_osk_surface) =
         dlsym(module, "arc_libretro_test_drop_osk_surface");
      *(void **)(&arc_libretro_test_osk_source_unchanged) =
         dlsym(module, "arc_libretro_test_osk_source_unchanged");
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
      *(void **)(&arc_libretro_test_mouse_input_samples) =
         dlsym(module, "arc_libretro_test_mouse_input_samples");
      *(void **)(&arc_libretro_test_presented_osk_notice) =
         dlsym(module, "arc_libretro_test_presented_osk_notice");
      *(void **)(&arc_libretro_test_content_id) =
         dlsym(module, "arc_libretro_test_content_id");
      *(void **)(&arc_libretro_test_staged_path) =
         dlsym(module, "arc_libretro_test_staged_path");
      *(void **)(&arc_libretro_test_cmos_path) =
         dlsym(module, "arc_libretro_test_cmos_path");
      *(void **)(&arc_libretro_test_choice_path) =
         dlsym(module, "arc_libretro_test_choice_path");
      *(void **)(&arc_zip_test_fail_member_allocations) =
         dlsym(module, "arc_zip_test_fail_member_allocations");
      *(void **)(&arc_libretro_test_session_state) =
         dlsym(module, "arc_libretro_test_session_state");
      *(void **)(&arc_libretro_test_chooser_focus) =
         dlsym(module, "arc_libretro_test_chooser_focus");
      *(void **)(&arc_libretro_test_chooser_rows) =
         dlsym(module, "arc_libretro_test_chooser_rows");
      *(void **)(&arc_libretro_test_force_input_reconcile) =
         dlsym(module, "arc_libretro_test_force_input_reconcile");
      *(void **)(&arc_libretro_test_adffs_accept_test_os) =
         dlsym(module, "arc_libretro_test_adffs_accept_test_os");
      *(void **)(&arc_libretro_test_fail_adffs_content_read) =
         dlsym(module, "arc_libretro_test_fail_adffs_content_read");
      *(void **)(&arc_libretro_test_adffs_content_read_failure_pending) =
         dlsym(module, "arc_libretro_test_adffs_content_read_failure_pending");
      *(void **)(&arc_libretro_test_adffs_carrier_matches) =
         dlsym(module, "arc_libretro_test_adffs_carrier_matches");
      *(void **)(&arc_libretro_test_adffs_released) =
         dlsym(module, "arc_libretro_test_adffs_released");
      *(void **)(&arc_platform_test_input_events_reset) =
         dlsym(module, "arc_platform_test_input_events_reset");
      *(void **)(&arc_platform_test_input_events) =
         dlsym(module, "arc_platform_test_input_events");
      assert(arc_engine_audio_stats && arc_engine_media_insert &&
             arc_engine_test_set_vidc_noncompleting &&
             arc_engine_test_drive_media && arc_engine_test_helper_media_valid &&
             arc_engine_test_cmos_image &&
             arc_engine_test_cmos_write && arc_engine_test_cmos_bus_write &&
             arc_engine_reset && arc_libretro_test_fail_osk_allocations &&
             arc_libretro_test_drop_osk_surface &&
             arc_libretro_test_osk_source_unchanged &&
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
             arc_libretro_test_content_id &&
             arc_libretro_test_staged_path && arc_libretro_test_cmos_path &&
             arc_libretro_test_choice_path &&
             arc_zip_test_fail_member_allocations &&
             arc_libretro_test_session_state &&
             arc_libretro_test_chooser_focus &&
             arc_libretro_test_chooser_rows &&
             arc_libretro_test_force_input_reconcile &&
             arc_libretro_test_adffs_accept_test_os &&
             arc_libretro_test_fail_adffs_content_read &&
             arc_libretro_test_adffs_content_read_failure_pending &&
             arc_libretro_test_adffs_released &&
             arc_libretro_test_adffs_carrier_matches &&
             arc_platform_test_input_events_reset && arc_platform_test_input_events);
      assert(arc_engine_test_helper_media_valid(ARC_MEDIA_ADL_640K,
                                                 ARC_GEOMETRY_640K_L) &&
             arc_engine_test_helper_media_valid(ARC_MEDIA_ADF_800K,
                                                 ARC_GEOMETRY_800K_D) &&
             !arc_engine_test_helper_media_valid(ARC_MEDIA_ADL_640K,
                                                  ARC_GEOMETRY_800K_D) &&
             !arc_engine_test_helper_media_valid(ARC_MEDIA_ADF_800K,
                                                  ARC_GEOMETRY_640K_L));
      ArcDigest application_identity_digest = digest_file(argv[12]);
      ArcDigest playlist_identity_digest = digest_playlist_pair(argv[7], argv[8]);
      memcpy(application_identity_sha1, application_identity_digest.sha1,
             sizeof(application_identity_sha1));
      memcpy(playlist_identity_sha1, playlist_identity_digest.sha1,
             sizeof(playlist_identity_sha1));
      char nested_identity_path[4096];
      snprintf(nested_identity_path, sizeof(nested_identity_path),
               "%s/arcdroid-nested-chooser-l.adl", argv[11]);
      ArcDigest nested_identity_digest = digest_file(nested_identity_path);
      memcpy(nested_identity_sha1, nested_identity_digest.sha1,
             sizeof(nested_identity_sha1));
   }
   else if (jfd_file_data_step || jfd_file_data_set || jfd_file_data_unavailable ||
            jfd_writes || jfd_create_step || jfd_merge_step ||
            jfd_identification_step ||
            jfd_allocation_step || jfd_allocation_set)
   {
      *(void **)(&arc_libretro_test_jfd_formatted_reads) =
         dlsym(module, "arc_libretro_test_jfd_formatted_reads");
      *(void **)(&arc_libretro_test_jfd_probe_mask) =
         dlsym(module, "arc_libretro_test_jfd_probe_mask");
      *(void **)(&arc_libretro_test_jfd_created_byte) =
         dlsym(module, "arc_libretro_test_jfd_created_byte");
      *(void **)(&arc_libretro_test_jfd_presented) =
         dlsym(module, "arc_libretro_test_jfd_presented");
      *(void **)(&arc_libretro_test_jfd_slot_state) =
         dlsym(module, "arc_libretro_test_jfd_slot_state");
      *(void **)(&arc_libretro_test_jfd_commit) =
         dlsym(module, "arc_libretro_test_jfd_commit");
      *(void **)(&arc_engine_test_drive_media) =
         dlsym(module, "arc_engine_test_drive_media");
      *(void **)(&arc_libretro_test_adffs_accept_test_os) =
         dlsym(module, "arc_libretro_test_adffs_accept_test_os");
      *(void **)(&arc_libretro_test_fail_adffs_content_read) =
         dlsym(module, "arc_libretro_test_fail_adffs_content_read");
      *(void **)(&arc_libretro_test_adffs_content_read_failure_pending) =
         dlsym(module, "arc_libretro_test_adffs_content_read_failure_pending");
      *(void **)(&arc_libretro_test_content_id) =
         dlsym(module, "arc_libretro_test_content_id");
      *(void **)(&arc_libretro_test_staged_path) =
         dlsym(module, "arc_libretro_test_staged_path");
      *(void **)(&arc_libretro_test_cmos_path) =
         dlsym(module, "arc_libretro_test_cmos_path");
      *(void **)(&arc_libretro_test_choice_path) =
         dlsym(module, "arc_libretro_test_choice_path");
      *(void **)(&arc_libretro_test_session_state) =
         dlsym(module, "arc_libretro_test_session_state");
      *(void **)(&arc_libretro_test_chooser_rows) =
         dlsym(module, "arc_libretro_test_chooser_rows");
      assert(arc_libretro_test_jfd_formatted_reads &&
             arc_libretro_test_jfd_probe_mask &&
             arc_libretro_test_jfd_created_byte &&
             arc_libretro_test_jfd_presented &&
             arc_libretro_test_jfd_slot_state &&
             arc_libretro_test_jfd_commit && arc_engine_test_drive_media &&
             arc_libretro_test_adffs_accept_test_os &&
             arc_libretro_test_fail_adffs_content_read &&
             arc_libretro_test_adffs_content_read_failure_pending &&
             arc_libretro_test_content_id && arc_libretro_test_staged_path &&
             arc_libretro_test_cmos_path && arc_libretro_test_choice_path &&
             arc_libretro_test_session_state && arc_libretro_test_chooser_rows);
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
      *(void **)(&arc_libretro_test_mouse_input_samples) =
         dlsym(module, "arc_libretro_test_mouse_input_samples");
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
          !arc_libretro_test_force_present ||
          !arc_libretro_test_mouse_input_samples)
      {
         fprintf(stderr, "libretro_harness: --osk-script requires the test core\n");
         assert(dlclose(module) == 0);
         return 2;
      }
   }

   phase = PHASE_ENV; retro_set_environment(environment);
   struct retro_system_info system_info;
   memset(&system_info, 0, sizeof(system_info));
   retro_get_system_info(&system_info);
   assert(system_info.valid_extensions &&
          !strcmp(system_info.valid_extensions, "adf|adl|jfd|m3u|zip") &&
          system_info.need_fullpath && system_info.block_extract);
   retro_set_video_refresh(video); retro_set_audio_sample_batch(audio);
   retro_set_input_poll(poll); retro_set_input_state(input);
   phase = PHASE_INIT; retro_init();
   assert(disk_control.set_eject_state && disk_control.get_eject_state &&
          disk_control.get_image_index && disk_control.set_image_index &&
          disk_control.get_num_images && disk_control.replace_image_index &&
          disk_control.add_image_index && !disk_control.set_initial_image &&
          disk_control.get_image_path && disk_control.get_image_label);
   assert(retro_serialize_size() == 0u);
   struct retro_game_info game = {jfd_writes ? jfd_content_path : argv[3], NULL, 0u, NULL};
   uint64_t source_hash = hash_file(game.path);
   if (helper_stress)
   {
      struct retro_game_info application_game = {argv[12], NULL, 0u, NULL};
      ArcDigest application_digest = digest_file(argv[12]);
      char helper_file[4096];
      snprintf(helper_file, sizeof(helper_file), "%s/arcdroid/launch/%s.adl",
               save_path, application_digest.sha1);
      option_launch = "app1";
      option_writes = "off";
      phase = PHASE_LOAD; assert(retro_load_game(&application_game));
      bool write_protected = false;
      assert(arc_engine_test_drive_media(1u, &write_protected, NULL) &&
             write_protected && file_exists(helper_file));
      retro_unload_game();
      uint64_t stable_helper = hash_file(helper_file);
      unsigned decisions = launch_log_lines;
      for (unsigned cycle = 0u; cycle < 100u; ++cycle)
      {
         phase = PHASE_LOAD; assert(retro_load_game(&application_game));
         assert(arc_engine_test_drive_media(1u, &write_protected, NULL) &&
                write_protected);
         retro_unload_game();
         assert(open_files == 0u && open_dirs == 0u &&
                hash_file(helper_file) == stable_helper);
      }
      assert(launch_log_lines == decisions + 100u);
      retro_deinit();
      assert(open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      puts("helper-stress: passed");
      return 0;
   }

   if (input_layout_probe)
   {
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      phase = PHASE_RUN;
      assert_input_resolution();
      assert_effective_descriptors();
      ArcKartEvent input_events[512];
      for (unsigned frame = 0u; frame < 120u; ++frame) retro_run();
      /* A player can reproduce the same ten bindings on either independent port. */
      static const char *layout_values[ARC_PAD_INPUT_COUNT] = {
         "s", "x", "comma", "period", "space", "slash", "a", "m", "f1", "t"
      };
      static const unsigned layout_keys[ARC_PAD_INPUT_COUNT] = {
         RETROK_s, RETROK_x, RETROK_COMMA, RETROK_PERIOD, RETROK_SPACE,
         RETROK_SLASH, RETROK_a, RETROK_m, RETROK_F1, RETROK_t
      };
      retro_set_controller_port_device(1u,
         RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1));
      retro_run();
      for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
         for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
            option_pad[port][input] = layout_values[input];
      unsigned notices_before_layout = controls_osd_calls;
      variable_update = true;
      retro_run();
      assert_input_resolution();
      assert_effective_descriptors();
      assert(controls_osd_calls == notices_before_layout);
      retro_run(); /* Both ports have reported neutral after the mapping change. */
      for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
      {
         arc_platform_test_input_events_reset();
         for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
            joypad_buttons[port][mapping_input_ids[input]] = true;
         retro_run();
         for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
            joypad_buttons[port][mapping_input_ids[input]] = false;
         for (unsigned frame = 0u;
              frame < 160u && arc_platform_test_input_events(input_events, 512u) <
                 2u * ARC_PAD_INPUT_COUNT;
              ++frame) retro_run();
         size_t count = arc_platform_test_input_events(input_events, 512u);
         assert(count == 2u * ARC_PAD_INPUT_COUNT);
         for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
         {
            unsigned downs = 0u, ups = 0u;
            for (size_t event = 0u; event < count; ++event)
               if (input_events[event].type == ARC_INPUT_EVENT_KEY &&
                   input_events[event].code == layout_keys[input])
                  input_events[event].down ? ++downs : ++ups;
            assert(downs == 1u && ups == 1u);
         }
      }
      for (unsigned port = 0u; port < ARC_PAD_PORTS; ++port)
         for (unsigned input = 0u; input < ARC_PAD_INPUT_COUNT; ++input)
            option_pad[port][input] = NULL;
      variable_update = true;
      retro_run();
      assert_input_resolution();
      assert_effective_descriptors();
      retro_reset();
      assert_input_resolution();
      assert_effective_descriptors();
      retro_unload_game();
      retro_deinit();
      assert(stale_input_option_queries == 0u &&
             hash_file(game.path) == source_hash &&
             open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      puts("input-layout-probe: passed");
      return 0;
   }


   if (jfd_quarantine_density)
   {
      option_machine = "Auto";
      option_launch = "off";
      option_writes = "off";
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(strstr(machine_resolution_log, "resolved=A3000") &&
             jfd_quarantine_log_lines == 1u &&
             strstr(last_jfd_identification_log, "timing=none captured=none") &&
             strstr(last_jfd_identification_log, "reason=quarantined"));
      struct retro_game_info second = {argv[6], NULL, 0u, NULL};
      assert(disk_control.set_eject_state(true));
      assert(!disk_control.replace_image_index(0u, &second) &&
             strstr(last_message, "hd-media-needs-new-io"));
      assert(disk_control.set_eject_state(false));
      retro_reset();
      assert(strstr(machine_resolution_log, "resolved=A3000") &&
             disk_control.get_num_images() == 1u);
      retro_unload_game();
      phase = PHASE_LOAD; assert(retro_load_game(&second));
      assert(strstr(machine_resolution_log, "resolved=A3020") &&
             jfd_quarantine_log_lines >= 2u);
      retro_unload_game();
      option_machine = "A3000"; variable_update = true;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      retro_unload_game();
      phase = PHASE_LOAD;
      assert(!retro_load_game(&second));
      retro_deinit();
      assert(open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      puts("jfd-quarantine-density: passed");
      return 0;
   }

   if (jfd_quarantine_adffs)
   {
      size_t source_size = 0u;
      uint8_t *source = read_alloc(game.path, &source_size);
      option_machine = "A3000";
      option_launch = "auto";
      option_jfd_boot = "adffs";
      arc_libretro_test_adffs_accept_test_os(true);
      unsigned warnings = jfd_quarantine_log_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(jfd_file_data_log_lines==0u); /* ADFFS has no native R1' preparation. */
      bool routed = strstr(last_jfd_boot_log, "path=adffs") != NULL;
      bool bytes_match = arc_libretro_test_adffs_carrier_matches(source,
                                                                 source_size);
      if (!routed || !bytes_match || jfd_quarantine_log_lines != warnings + 1u)
         fprintf(stderr, "quarantine ADFFS route=%s warning=%u carrier=%d size=%zu\n",
                 last_jfd_boot_log, jfd_quarantine_log_lines - warnings,
                 bytes_match, source_size);
      assert(routed && bytes_match && jfd_quarantine_log_lines == warnings + 1u);
      assert(!arc_libretro_test_adffs_released() &&
             disk_control.get_num_images() == 1u);
      retro_unload_game();
      assert(arc_libretro_test_adffs_released() &&
             disk_control.get_num_images() == 0u);
      retro_deinit(); free(source);
      assert(open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      puts("jfd-quarantine-adffs: passed");
      return 0;
   }

   if (jfd_quarantine_guest)
   {
      option_machine = jfd_quarantine_newio ? "A3020" : "A3000";
      option_launch = "off";
      option_writes = "off";
      unsigned warnings = jfd_quarantine_log_lines;
      unsigned omitted = zip_omission_log_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(jfd_quarantine_log_lines == warnings + 1u &&
             zip_omission_log_lines == omitted &&
             jfd_quarantine_notice_calls == 0u &&
             strstr(last_jfd_quarantine_log, "sides=1 first=158:octal-density"));
      if (strstr(game.path, ".zip"))
      {
         char image_path[4096];
         assert(disk_control.get_num_images() == 2u &&
                disk_control.get_image_path(0u, image_path, sizeof(image_path)) &&
                strstr(image_path, "quarantine-guest.zip#Disc1.jfd") &&
                disk_control.get_image_path(1u, image_path, sizeof(image_path)) &&
                strstr(image_path, "quarantine-guest.zip#Disc2.jfd") &&
                strstr(last_zip_order, "1=\"Disc1.jfd\" 2=\"Disc2.jfd\""));
      }
      bool protected_before = false;
      char mounted_digest[41], reset_digest[41];
      assert(arc_engine_test_drive_media(0u, &protected_before,
                                         mounted_digest));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 900u &&
             last_video_pixel != 0x006a0000u; ++frame) retro_run();
      if (last_video_pixel != 0x006a0000u)
         fprintf(stderr, "quarantine guest pixel=%08x machine=%s boot=%s\n",
                 last_video_pixel, machine_resolution_log, last_jfd_boot_log);
      assert(last_video_pixel == 0x006a0000u);
      retro_reset(); last_video_pixel = 0u;
      bool protected_after = false;
      assert(jfd_quarantine_log_lines == warnings + 2u &&
             zip_omission_log_lines == omitted &&
             disk_control.get_num_images() ==
                (strstr(game.path, ".zip") ? 2u : 1u) &&
             arc_engine_test_drive_media(0u, &protected_after, reset_digest) &&
             protected_before == protected_after &&
             !strcmp(mounted_digest, reset_digest));
      if (!jfd_quarantine_newio)
      {
         for (unsigned frame = 0u; frame < 900u &&
                last_video_pixel != 0x006a0000u; ++frame) retro_run();
         assert(last_video_pixel == 0x006a0000u);
      }
      assert(hash_file(game.path) == source_hash);
      retro_unload_game(); retro_deinit();
      assert(open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      puts("jfd-quarantine-guest: passed");
      return 0;
   }

   if (short_tail_discovery)
   {
      option_machine = "A3000";
      option_launch = "auto";
      option_writes = "shadow";
      option_short_tail = "missing";
      char staged[4096];
      staged_media_path(staged, save_path, game.path, "disc.adf");
      (void)remove(staged);
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(!strcmp(staged, arc_libretro_test_staged_path(0u)) &&
             strstr(last_launch_log, "reason=inspection incomplete") &&
             strstr(last_launch_log, "!Demo:unknown"));
      retro_unload_game();
      uint8_t *full = malloc(819200u);
      assert(full);
      read_exact(argv[6], full, 819200u);
      FILE *shadow = fopen(staged, "r+b");
      assert(shadow && fseek(shadow, 818176L, SEEK_SET) == 0 &&
             fwrite(full + 818176u, 1u, 1024u, shadow) == 1024u &&
             fflush(shadow) == 0 && fclose(shadow) == 0);
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(strstr(last_launch_log, "reason=inspection incomplete"));
      char missing_stamp[41], zero_stamp[41], restored_stamp[41];
      assert(arc_libretro_test_boot_media_digest(missing_stamp));
      retro_unload_game();
      option_short_tail = "zero-filled";
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(arc_libretro_test_boot_media_digest(zero_stamp) &&
             strcmp(missing_stamp, zero_stamp));
      assert(strstr(last_launch_log, "decision=!Demo") &&
             strstr(last_launch_log, "scan=complete") &&
             strstr(short_notice_text, "presented zero-filled") &&
             strstr(short_info_text, "presentation=zero-filled"));
      assert(disk_control.set_eject_state(true));
      option_short_tail = "invalid";
      assert(!disk_control.set_eject_state(false) &&
             disk_control.get_eject_state() &&
             arc_libretro_test_boot_media_digest(restored_stamp) &&
             !strcmp(zero_stamp, restored_stamp));
      option_short_tail = "missing";
      assert(disk_control.set_eject_state(false) &&
             arc_libretro_test_boot_media_digest(restored_stamp) &&
             !strcmp(missing_stamp, restored_stamp));
      assert(disk_control.set_eject_state(true));
      option_short_tail = "zero-filled";
      assert(disk_control.set_eject_state(false) &&
             arc_libretro_test_boot_media_digest(restored_stamp) &&
             !strcmp(zero_stamp, restored_stamp));
      option_short_tail = "missing"; variable_update = true;
      retro_reset();
      assert(arc_libretro_test_boot_media_digest(restored_stamp) &&
             !strcmp(missing_stamp, restored_stamp));
      assert(strstr(last_launch_log, "reason=inspection incomplete"));
      uint8_t *backing = malloc(819200u);
      assert(backing);
      read_exact(staged, backing, 819200u);
      assert(!memcmp(backing + 818176u, full + 818176u, 1024u) &&
             hash_file(game.path) == source_hash);
      free(backing); free(full);
      retro_unload_game(); retro_deinit();
      assert(open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      puts("short-tail-discovery: passed");
      return 0;
   }

   if (short_tail_guest)
   {
      const size_t present_offset = 159u * 5120u + 3u * 1024u;
      const size_t declared = 819200u;
      option_machine = short_tail_newio_guest ? "A3020" : "A3000";
      /* This older guest verifies the absent IDE bus before its FDC probe. */
      if (short_tail_newio_guest) option_hard_disc = "off";
      option_launch = "off";
      option_writes = "shadow";
      option_short_tail = "missing";
      char staged[4096];
      staged_media_path(staged, save_path, game.path, "disc.adf");
      (void)remove(staged); /* this dedicated test starts with a new shadow */
      unsigned warnings = short_warning_log_lines;
      unsigned infos = short_info_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(short_warning_log_lines == warnings + 1u &&
             short_info_lines == infos + 1u &&
             strstr(short_notice_text, "presented as absent") &&
             strstr(short_info_text, "effective=818176") &&
             strstr(short_info_text, "absent-sectors=1"));
      assert(!strcmp(staged, arc_libretro_test_staged_path(0u)));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 900u && last_video_pixel != 0x006a0000u; ++frame)
         retro_run();
      if (last_video_pixel != 0x006a0000u)
         fprintf(stderr, "short-tail newio=%d pixel=%08x hd=%s\n",
                 short_tail_newio_guest, last_video_pixel, hard_disc_policy_log);
      assert(last_video_pixel == 0x006a0000u);
      uint8_t *backing = malloc(declared);
      assert(backing);
      read_exact(staged, backing, declared);
      for (size_t i = 0u; i < 1024u; ++i)
         assert(backing[present_offset + i] == 0x5au && backing[declared - 1024u + i] == 0u);
      retro_reset();
      last_video_pixel = 0u;
      for (unsigned frame = 0u; frame < 900u && last_video_pixel != 0x006a0000u; ++frame)
         retro_run();
      assert(last_video_pixel == 0x006a0000u);
      retro_unload_game();
      /* A preexisting declared-size shadow retains its tail across switches. */
      FILE *shadow = fopen(staged, "r+b");
      assert(shadow && fseek(shadow, (long)(declared - 1024u), SEEK_SET) == 0);
      memset(backing, 0x7bu, 1024u);
      assert(fwrite(backing, 1u, 1024u, shadow) == 1024u &&
             fflush(shadow) == 0 && fclose(shadow) == 0);
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      phase = PHASE_RUN; last_video_pixel = 0u;
      for (unsigned frame = 0u; frame < 900u && last_video_pixel != 0x006a0000u; ++frame)
         retro_run();
      assert(last_video_pixel == 0x006a0000u);
      option_short_tail = "zero-filled"; variable_update = true;
      retro_reset();
      last_video_pixel = 0u;
      for (unsigned frame = 0u; frame < 900u && last_video_pixel != 0x0000006au; ++frame)
         retro_run();
      assert(last_video_pixel == 0x0000006au);
      option_short_tail = "missing"; variable_update = true;
      retro_reset();
      last_video_pixel = 0u;
      for (unsigned frame = 0u; frame < 900u && last_video_pixel != 0x006a0000u; ++frame)
         retro_run();
      assert(last_video_pixel == 0x006a0000u);
      read_exact(staged, backing, declared);
      for (size_t i = 0u; i < 1024u; ++i)
         assert(backing[declared - 1024u + i] == 0x7bu);
      assert(hash_file(game.path) == source_hash);
      free(backing);
      retro_unload_game();
      retro_deinit();
      assert(open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      puts("short-tail-guest: passed");
      return 0;
   }

   if (power_on_cmos_guest)
   {
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 35u; ++frame) retro_run();
      uint8_t image[ARC_CMOS_MACHINE_SIZE], record[ARC_CMOS_RECORD_SIZE];
      assert(arc_engine_test_cmos_image(image));
      assert((image[0x50u] & 0x10u) && image[0x58u] == 0x77u &&
             image[0x59u] == 0xa5u);
      assert(power_on_construction_lines == 1u); /* guest reset did not arm */
      char record_path[4096];
      snprintf(record_path, sizeof(record_path), "%s", arc_libretro_test_cmos_path());
      read_exact(record_path, record, sizeof(record));
      assert(!(record[0x50u] & 0x10u) && record[0x58u] == 0x77u);
      retro_unload_game();
      record[0x50u] |= 0x10u; /* pre-slice forced flag in a valid record */
      write_exact(record_path, record, sizeof(record));
      copy_file(argv[6], renamed); /* KART-capable authored ROM for healed boot */
      unsigned clear_before = power_on_clear_lines;
      unsigned down_before = kart_shift_down_lines;
      unsigned up_before = kart_shift_up_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 130u; ++frame) retro_run();
      read_exact(record_path, record, sizeof(record));
      assert(!(record[0x50u] & 0x10u) && cmos_healing_log_count >= 1u);
      assert(power_on_construction_lines == 2u);
      assert(power_on_clear_lines == clear_before + 1u);
      assert(kart_shift_down_lines == down_before + 1u &&
             kart_shift_up_lines == up_before + 1u);
      retro_unload_game();
      retro_deinit();
      assert(open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      puts("power-on-cmos-guest: passed");
      return 0;
   }
   if (cmos_exerciser)
   {
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 30u; ++frame) retro_run();
      uint8_t image[ARC_CMOS_MACHINE_SIZE], record[ARC_CMOS_RECORD_SIZE];
      assert(arc_engine_test_cmos_image(image));
      assert(image[0xedu] == 0u && image[0x58u] == 0x77u &&
             image[0x59u] == 0xa5u && image[0x3fu] == 0x42u);
      assert(cmos_ignored_log_count == 1u);
      unsigned locked_writes_before_reset = cmos_locked_debug_write_count;
      char record_path[4096];
      snprintf(record_path, sizeof(record_path), "%s", arc_libretro_test_cmos_path());
      read_exact(record_path, record, sizeof(record));
      assert(record[0xedu] == 0u && record[0x58u] == 0x77u && record[0x59u] == 0xa5u);
      retro_reset();
      for (unsigned frame = 0u; frame < 5u; ++frame) retro_run();
      assert(cmos_locked_debug_write_count > locked_writes_before_reset);
      assert(cmos_ignored_log_count == 1u);
      retro_unload_game();
      assert(cmos_summary_attempts >= 1u && cmos_summary_changes >= 1u);
      unsigned locked_writes_before_reload = cmos_locked_debug_write_count;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 5u; ++frame) retro_run();
      assert(cmos_locked_debug_write_count > locked_writes_before_reload);
      assert(arc_engine_test_cmos_image(image) && image[0xedu] == 0u &&
             image[0x58u] == 0x77u && image[0x59u] == 0xa5u);
      assert(cmos_ignored_log_count == 2u);
      retro_unload_game();
      char os2_fixture[4096], os2_firmware[4096];
      const char *separator = strrchr(argv[2], '/');
      assert(separator &&
             snprintf(os2_fixture, sizeof(os2_fixture), "%.*s/arcdroid-unsupported-launch-test.rom",
                      (int)(separator - argv[2]), argv[2]) < (int)sizeof(os2_fixture) &&
             snprintf(os2_firmware, sizeof(os2_firmware), "%s/os2-test.rom",
                      firmware_path) < (int)sizeof(os2_firmware));
      copy_file(os2_fixture, os2_firmware);
      option_os = "RISC OS 2.00";
      option_machine = "A3000";
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(arc_engine_test_cmos_write(0xedu, 0x61u));
      phase = PHASE_RUN; retro_run();
      char os2_record_path[4096];
      snprintf(os2_record_path, sizeof(os2_record_path), "%s", arc_libretro_test_cmos_path());
      read_exact(os2_record_path, record, sizeof(record));
      assert(record[0xedu] == 0x61u && strcmp(os2_record_path, record_path) != 0);
      retro_reset();
      assert(arc_engine_test_cmos_image(image) && image[0xedu] == 0x61u);
      retro_unload_game();
      assert(remove(os2_firmware) == 0);
      option_os = "Auto";
      option_machine = "Auto";
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(arc_engine_test_cmos_image(image) && image[0xedu] == 0u);
      retro_unload_game();
      uint8_t *firmware = malloc(524288u);
      assert(firmware);
      read_exact(argv[2], firmware, 524288u);
      uint8_t empty_mask[32] = {0}, protected_mask[32] = {0};
      arc_cmos_locked_raw_mask(ARC_OS_RISCOS_200, empty_mask);
      arc_cmos_locked_raw_mask(ARC_OS_TEST, protected_mask);
      memset(image, 0, sizeof(image));
      char failure[128] = {0};
      assert(!arc_engine_start(firmware, 1u, NULL, NULL,
                               arc_machine_descriptor(ARC_MACHINE_A3000),
                               NULL, image, protected_mask, NULL, NULL,
                               failure, sizeof(failure)));
      assert(arc_engine_start(firmware, 524288u, NULL, NULL,
                              arc_machine_descriptor(ARC_MACHINE_A3000),
                              NULL, image, empty_mask, NULL, NULL,
                              failure, sizeof(failure)));
      assert(arc_engine_test_cmos_bus_write(0xedu, 0x44u));
      assert(arc_engine_test_cmos_image(image) && image[0xedu] == 0x44u);
      arc_engine_stop();
      memset(image, 0, sizeof(image));
      image[199u] = 0x41u; /* one attached IDE disc, one floppy */
      ArcIdeBackend ide = {0};
      ide.read = cmos_test_ide_read;
      ide.sectors = 4096u;
      assert(arc_engine_start(firmware, 524288u, NULL, NULL,
                              arc_machine_descriptor(ARC_MACHINE_A3020),
                              &ide, image, protected_mask, NULL, NULL,
                              failure, sizeof(failure)));
      assert(arc_engine_test_cmos_bus_write(0xedu, 0x95u));
      assert(arc_engine_test_cmos_image(image) && image[0xedu] == 0u &&
             (image[199u] & 0xc0u) == 0x40u);
      assert(arc_engine_reset(failure, sizeof(failure)));
      assert(arc_engine_test_cmos_bus_write(0xedu, 0x96u));
      assert(arc_engine_test_cmos_image(image) && image[0xedu] == 0u &&
             (image[199u] & 0xc0u) == 0x40u);
      assert(!arc_engine_start(firmware, 1u, NULL, NULL,
                               arc_machine_descriptor(ARC_MACHINE_A3020),
                               &ide, image, protected_mask, NULL, NULL,
                               failure, sizeof(failure)));
      memset(image, 0, sizeof(image));
      assert(arc_engine_start(firmware, 524288u, NULL, NULL,
                              arc_machine_descriptor(ARC_MACHINE_A3000),
                              NULL, image, empty_mask, NULL, NULL,
                              failure, sizeof(failure)));
      assert(arc_engine_test_cmos_bus_write(0xedu, 0x47u));
      assert(arc_engine_test_cmos_image(image) && image[0xedu] == 0x47u);
      arc_engine_stop();
      free(firmware);
      retro_deinit();
      assert(dlclose(module) == 0);
      puts("libretro_harness: CMOS guest exerciser passed");
      return 0;
   }

   if (native_hd_guest)
   {
      hd_guest_logging = true;
      assert(!strcmp(argv[6], "first") || !strcmp(argv[6], "recover") ||
             !strcmp(argv[6], "reload") || !strcmp(argv[6], "stress") ||
             !strcmp(argv[6], "stress-pause") ||
             !strcmp(argv[6], "compact-pause") ||
             !strcmp(argv[6], "off") || !strcmp(argv[6], "read-only") ||
             !strcmp(argv[6], "a3010") || !strcmp(argv[6], "a4000") ||
             !strcmp(argv[6], "dd-a3020") ||
             !strcmp(argv[6], "create-fail") ||
             !strcmp(argv[6], "create-recover") ||
             !strcmp(argv[6], "off-to-auto") ||
             !strcmp(argv[6], "auto-to-off") ||
             !strcmp(argv[6], "eject-reset") ||
             !strcmp(argv[6], "disk-index-reset") ||
             !strcmp(argv[6], "damaged") ||
             !strcmp(argv[6], "append-fail") ||
             !strcmp(argv[6], "journal-cap") ||
             !strcmp(argv[6], "probe-fail") ||
             !strcmp(argv[6], "formatter-fail") ||
             !strcmp(argv[6], "notice-rearm") ||
             !strcmp(argv[6], "compact-neutral"));
      bool absent = !strcmp(argv[6], "off") || !strcmp(argv[6], "a3010") ||
                    !strcmp(argv[6], "a4000") ||
                    !strcmp(argv[6], "create-fail") ||
                    !strcmp(argv[6], "probe-fail") ||
                    !strcmp(argv[6], "formatter-fail") ||
                    !strcmp(argv[6], "off-to-auto") ||
                    !strcmp(argv[6], "damaged");
      option_machine = !strcmp(argv[6], "a3010") ? "A3010" :
                       !strcmp(argv[6], "a4000") ? "A4000" :
                       !strcmp(argv[6], "dd-a3020") ? "A3020" : "Auto";
      option_cpu = (absent && strcmp(argv[6], "off")) ||
                   !strcmp(argv[6], "dd-a3020") ? "ARM250" : "Auto";
      option_launch = "off";
      option_writes = !strcmp(argv[6], "read-only") ? "off" : "shadow";
      option_hard_disc = !strcmp(argv[6], "off") ||
                         !strcmp(argv[6], "off-to-auto") ? "off" : "auto";
      if (!strcmp(argv[6], "create-fail") ||
          !strcmp(argv[6], "create-recover"))
         fault_mkdir_substring = "/arcdroid/hd";
      if (!strcmp(argv[6], "probe-fail"))
         fault_close_substring = "rwprobe.tmp";
      if (!strcmp(argv[6], "formatter-fail"))
         arc_install_store_test_corrupt_native_format(true);
      install_prefix_accesses = 0u;
      phase = PHASE_LOAD;
      bool loaded = retro_load_game(&game);
      if (!loaded) fprintf(stderr, "native hard disc load: %s\n", last_message);
      assert(loaded);
      if (!strcmp(argv[6], "create-fail") ||
          !strcmp(argv[6], "create-recover") ||
          !strcmp(argv[6], "probe-fail") ||
          !strcmp(argv[6], "formatter-fail"))
         assert(hard_disc_unavailable_notices == 1u &&
                strstr(hard_disc_policy_log,
                   !strcmp(argv[6], "probe-fail") ?
                     "attach=no mode=none reason=unavailable:write-probe-failed" :
                   !strcmp(argv[6], "formatter-fail") ?
                     "attach=no mode=none reason=unavailable:create-or-publication-failed" :
                     "attach=no mode=none reason=unavailable:directory-create-failed"));
      if (!strcmp(argv[6], "formatter-fail")) {
         char manifest[4096];
         uint8_t formatter_cmos[ARC_CMOS_MACHINE_SIZE];
         snprintf(manifest, sizeof(manifest), "%s/arcdroid/hd/%s/ready.a",
                  save_path, arc_libretro_test_content_id());
         assert(hard_disc_formatter_mismatch_logs == 1u &&
                !file_exists(manifest) &&
                arc_engine_test_cmos_image(formatter_cmos) &&
                ((formatter_cmos[arc_cmos_raw_address(135u)] >> 6u) & 3u) == 0u &&
                formatter_cmos[arc_cmos_raw_address(189u)] == 0u);
         retro_unload_game(); retro_deinit();
         assert(open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
         puts("native hard disc guest formatter-fail: no publication or attachment");
         return 0;
      }
      if (!strcmp(argv[6], "notice-rearm")) {
         assert(arc_libretro_test_hd_observation_write(3u));
         assert(hard_disc_write_failure_notices == 1u);
         assert(!arc_libretro_test_hd_observation_write(3u) &&
                hard_disc_write_failure_notices == 1u);
         retro_reset();
         assert(strstr(hard_disc_policy_log, "attach=yes mode=writable") &&
                arc_libretro_test_hd_observation_write(3u) &&
                hard_disc_write_failure_notices == 2u);
         retro_unload_game(); retro_deinit();
         assert(open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
         puts("native hard disc guest notice-rearm: one notice per failure episode");
         return 0;
      }
      if (!strcmp(argv[6], "compact-neutral")) {
         ArcBootObservation written = {0}, compacted = {0};
         char journal0[4096], journal1[4096], ready1[4096];
         struct stat old_journal, new_journal;
         snprintf(journal0, sizeof(journal0), "%s/arcdroid/hd/%s/journal.g0",
                  save_path, arc_libretro_test_content_id());
         snprintf(journal1, sizeof(journal1), "%s/arcdroid/hd/%s/journal.g1",
                  save_path, arc_libretro_test_content_id());
         snprintf(ready1, sizeof(ready1), "%s/arcdroid/hd/%s/ready.b",
                  save_path, arc_libretro_test_content_id());
         assert(arc_libretro_test_hd_observation_write(1u) &&
                arc_libretro_test_boot_stamp(&written) &&
                arc_libretro_test_hd_observation_write(4u) &&
                !stat(journal0, &old_journal) &&
                old_journal.st_size > 1024 * 1024);
         retro_reset();
         assert(arc_libretro_test_boot_stamp(&compacted) &&
                !strcmp(written.hard_disc, compacted.hard_disc) &&
                file_exists(ready1) && !stat(journal1, &new_journal) &&
                new_journal.st_size == 124 &&
                old_journal.st_size != new_journal.st_size);
         retro_unload_game(); retro_deinit();
         assert(open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
         puts("native hard disc guest compact-neutral: equal digest across generation and journal change");
         return 0;
      }
      if (!strcmp(argv[6], "create-recover") ||
          !strcmp(argv[6], "off-to-auto") ||
          !strcmp(argv[6], "auto-to-off")) {
         if (!strcmp(argv[6], "off-to-auto")) option_hard_disc = "auto";
         if (!strcmp(argv[6], "auto-to-off")) option_hard_disc = "off";
         variable_update = true;
         retro_reset();
         absent = !strcmp(argv[6], "auto-to-off");
         assert(strstr(hard_disc_policy_log,
                absent ? "attach=no mode=none reason=option-off" :
                         "attach=yes mode=writable reason=attached"));
      }
      if (!strcmp(argv[6], "eject-reset") ||
          !strcmp(argv[6], "disk-index-reset")) {
         assert(disk_control.set_eject_state(true));
         if (!strcmp(argv[6], "disk-index-reset")) {
            assert(disk_control.get_num_images() == 2u &&
                   disk_control.set_image_index(1u));
            assert(disk_control.set_eject_state(false));
         }
         retro_reset();
         assert(strstr(hard_disc_policy_log,
                       "attach=yes mode=writable reason=attached"));
      }
      if (!absent) {
         char manifest_a[4096], manifest_b[4096];
         snprintf(manifest_a, sizeof(manifest_a),
            "%s/arcdroid/hd/%s/ready.a", save_path,
            arc_libretro_test_content_id());
         snprintf(manifest_b, sizeof(manifest_b),
            "%s/arcdroid/hd/%s/ready.b", save_path,
            arc_libretro_test_content_id());
         assert(file_exists(manifest_a) || file_exists(manifest_b));
      }
      if (!strcmp(argv[6], "damaged"))
         assert(hard_disc_damaged_notices == 1u &&
                strstr(hard_disc_policy_log, "attach=no mode=none reason=damaged"));
      if (!strcmp(argv[6], "read-only"))
         assert(hard_disc_readonly_notices == 1u &&
                strstr(hard_disc_policy_log, "attach=yes mode=read-only"));
      ArcBootObservation before_stamp = {0}, after_stamp = {0};
      assert(arc_libretro_test_boot_stamp(&before_stamp) &&
             before_stamp.hard_disc_policy ==
                (unsigned)!!strcmp(option_hard_disc, "off") &&
             before_stamp.hard_disc_attached == (unsigned)!absent &&
             before_stamp.hard_disc_writable ==
                (unsigned)(!absent && !!strcmp(argv[6], "read-only")));
      if (!strcmp(argv[6], "append-fail"))
         fault_write_substring = "/arcdroid/hd/";
      if (!strcmp(argv[6], "journal-cap"))
         assert(arc_libretro_test_hd_exhaust_journal() &&
                hard_disc_write_failure_notices == 1u);
      uint8_t cmos[ARC_CMOS_MACHINE_SIZE];
      assert(arc_engine_test_cmos_image(cmos) &&
             ((cmos[arc_cmos_raw_address(135u)] >> 6u) & 3u) ==
                (absent ? 0u : 1u) &&
             cmos[arc_cmos_raw_address(189u)] ==
                (absent ? 0u : 0x0au));
      phase = PHASE_RUN;
      unsigned steady = 0u;
      for (unsigned frame = 0u; frame < 1800u && steady < 8u; ++frame) {
         retro_run();
         bool stress_write = !strcmp(argv[6], "stress") ||
                             !strcmp(argv[6], "stress-pause");
         steady = (!stress_write || last_video_pixel == 0x00009400u) &&
            video_frames > 30u &&
            video_frames - video_signature_last_change_frame > 8u ?
            steady + 1u : 0u;
      }
      if (steady != 8u)
      {
         unsigned ide_accesses = 0u;
         (void)arc_engine_test_newio(NULL, NULL, NULL, &ide_accesses, NULL);
         fprintf(stderr, "native hard disc guest phase=%s pixel=%08x frames=%u ide=%u\n",
                 argv[6], last_video_pixel, video_frames, ide_accesses);
      }
      assert(steady == 8u);
      if (!strcmp(argv[6], "stress") ||
          !strcmp(argv[6], "stress-pause"))
         assert(last_video_pixel == 0x00009400u);
      if (!strcmp(argv[6], "append-fail") ||
          !strcmp(argv[6], "journal-cap"))
         assert(hard_disc_write_failure_notices == 1u &&
                strstr(hard_disc_policy_log, "attach=yes mode=writable"));
      assert(arc_libretro_test_boot_stamp(&after_stamp));
      if (!strcmp(argv[6], "append-fail") ||
          !strcmp(argv[6], "journal-cap"))
         assert(after_stamp.hard_disc_attached &&
                !after_stamp.hard_disc_writable);
      if (!strcmp(argv[6], "first") || !strcmp(argv[6], "stress") ||
          !strcmp(argv[6], "stress-pause") ||
          !strcmp(argv[6], "recover") ||
          !strcmp(argv[6], "dd-a3020") ||
          !strcmp(argv[6], "create-recover") ||
          !strcmp(argv[6], "off-to-auto") ||
          !strcmp(argv[6], "eject-reset") ||
          !strcmp(argv[6], "disk-index-reset"))
         assert(strcmp(before_stamp.hard_disc, after_stamp.hard_disc));
      else
         assert(!strcmp(before_stamp.hard_disc, after_stamp.hard_disc));
      if (!strcmp(argv[6], "first") || !strcmp(argv[6], "dd-a3020")) {
         char journal_path[4096];
         snprintf(journal_path, sizeof(journal_path),
            "%s/arcdroid/hd/%s/journal.g0", save_path,
            arc_libretro_test_content_id());
         assert(file_exists(journal_path));
      }
      if (!strcmp(argv[6], "stress-pause") ||
          !strcmp(argv[6], "compact-pause")) {
         fprintf(stderr, "native guest pause pixel=%08x frames=%u\n",
                 last_video_pixel, video_frames);
         (void)fgetc(stdin);
      }
      retro_unload_game(); retro_deinit();
      assert(install_prefix_accesses == 0u && open_files == 0u &&
             open_dirs == 0u && dlclose(module) == 0);
      printf("native hard disc guest %s: pixel=%08x signature=%016llx frames=%u\n",
             argv[6], last_video_pixel,
             (unsigned long long)last_video_signature, video_frames);
      return 0;
   }

   if (jfd_allocation_set)
   {
      option_machine = argv[6];
      option_cpu = !strcmp(option_machine, "A3020") ? "ARM250" : "ARM2";
      option_launch = "off"; option_jfd_boot = "native";
      option_writes = "shadow"; variable_update = true;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(disk_control.get_num_images() == 2u &&
             !arc_libretro_test_jfd_probe_mask(0u, 0u) &&
             !arc_libretro_test_jfd_probe_mask(1u, 0u));
      phase = PHASE_RUN;
      for (unsigned selected = 0u; selected < 2u; ++selected)
      {
         unsigned steady = 0u;
         for (unsigned frame = 0u; frame < 900u && steady < 8u; ++frame)
         {
            retro_run();
            steady = last_video_pixel == 0x00001500u ? steady + 1u : 0u;
         }
         assert(steady == 8u &&
                arc_libretro_test_jfd_probe_mask(selected, 0u) == (1u << 3u) &&
                !arc_libretro_test_jfd_probe_mask(1u - selected, 0u));
         assert(disk_control.set_eject_state(true) &&
                disk_control.set_image_index(1u - selected) &&
                disk_control.set_eject_state(false) &&
                !arc_libretro_test_jfd_probe_mask(1u - selected, 0u));
         retro_reset();
         last_video_pixel = 0u;
      }
      assert(disk_control.get_image_index() == 0u &&
             !arc_libretro_test_jfd_probe_mask(0u, 0u));
      option_writes = "off"; variable_update = true;
      retro_reset();
      read_cached_video(); /* Frontend may retain the pre-reset frame. */
      bool protected = false;
      assert(!arc_libretro_test_jfd_probe_mask(0u, 0u) &&
             arc_engine_test_drive_media(0u, &protected, NULL) && protected);
      option_writes = "shadow"; variable_update = true;
      retro_reset();
      read_cached_video();
      assert(!arc_libretro_test_jfd_probe_mask(0u, 0u) &&
             arc_engine_test_drive_media(0u, &protected, NULL) && !protected);
      retro_unload_game(); retro_deinit();
      assert(hash_file(game.path) == source_hash &&
             open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      printf("jfd_allocation_set: machine=%s two images, swap, reset, Off/on passed\n",
             option_machine);
      return 0;
   }

   if (jfd_file_data_recovery)
   {
      option_machine="A3000"; option_cpu="ARM2";
      option_launch="auto"; option_jfd_boot="native";
      option_writes="shadow"; option_jfd_omitted="file-data";
      phase=PHASE_LOAD; assert(retro_load_game(&game));
      assert(jfd_file_data_log_lines==1u &&
             strstr(last_jfd_file_data_log,"reason=presented"));
      option_jfd_omitted="strict"; variable_update=true;
      phase=PHASE_RUN;
      for (unsigned frame=0u;frame<1800u && jfd_file_data_log_lines<2u;++frame)
         retro_run();
      if (jfd_file_data_log_lines!=2u)
         fprintf(stderr,"recovery log count=%u last=%s launch=%s boot=%s\n",
                 jfd_file_data_log_lines,last_jfd_file_data_log,
                 last_launch_log,last_jfd_boot_log);
      assert(jfd_file_data_log_lines==2u &&
             strstr(last_launch_log,"recovery=helper") &&
             !strcmp(last_jfd_file_data_log,
               "JFD file-data: image=1 policy=1 class=wp-flag format=D copies=1/1 presented=1 skipped-sides=0 incomplete-dirs=0 reason=presented\n"));
      retro_unload_game(); retro_deinit();
      assert(open_files==0u && open_dirs==0u && dlclose(module)==0);
      puts("jfd-file-data-recovery: passed");
      return 0;
   }

   if (jfd_file_data_reasons)
   {
      static const struct { const char *name,*expected; } cases[] = {
         {"d-no-valid-map.jfd", "JFD file-data: image=1 policy=1 class=wp-flag format=D copies=0/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=no-valid-map\n"},
         {"d-root-invalid.jfd", "JFD file-data: image=1 policy=1 class=wp-flag format=D copies=1/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=root-invalid\n"},
         {"d-invalid-object.jfd", "JFD file-data: image=1 policy=1 class=wp-flag format=D copies=1/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=object-invalid\n"},
         {"d-depth-17.jfd", "JFD file-data: image=1 policy=1 class=wp-flag format=D copies=1/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=limit\n"},
         {"d-zero-length.jfd", "JFD file-data: image=1 policy=1 class=wp-flag format=D copies=1/1 presented=0 skipped-sides=0 incomplete-dirs=0 reason=none-eligible\n"},
         {"e-conflict.jfd", "JFD file-data: image=1 policy=1 class=wp-flag format=E copies=2/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=map-conflict\n"},
         {"e-object-conflict.jfd", "JFD file-data: image=1 policy=1 class=wp-flag format=E copies=2/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=map-conflict\n"},
         {"other-format.jfd", "JFD file-data: image=1 policy=1 class=wp-flag format=other copies=0/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=format\n"}
      };
      option_machine="A3000"; option_cpu="ARM2";
      option_launch="off"; option_jfd_boot="native";
      option_writes="shadow"; option_jfd_omitted="file-data";
      for (unsigned i=0u;i<sizeof(cases)/sizeof(cases[0]);++i)
      {
         char path[4096];
         int n=snprintf(path,sizeof(path),"%s/%s",argv[6],cases[i].name);
         assert(n>0 && (size_t)n<sizeof(path));
         game.path=path;
         unsigned before=jfd_file_data_log_lines;
         phase=PHASE_LOAD; assert(retro_load_game(&game));
         if (strcmp(last_jfd_file_data_log,cases[i].expected))
            fprintf(stderr,"%s: %s",cases[i].name,last_jfd_file_data_log);
         assert(jfd_file_data_log_lines==before+1u &&
                !strcmp(last_jfd_file_data_log,cases[i].expected));
         retro_unload_game();
      }
      retro_deinit();
      assert(open_files==0u && open_dirs==0u && dlclose(module)==0);
      puts("jfd-file-data-reasons: passed");
      return 0;
   }

   if (jfd_file_data_unavailable)
   {
      *(void **)(&arc_libretro_test_jfd_wp_layout_supported) =
         dlsym(module,"arc_libretro_test_jfd_wp_layout_supported");
      assert(arc_libretro_test_jfd_wp_layout_supported);
      option_machine="A3000"; option_cpu="ARM2";
      option_launch="off"; option_jfd_boot="native";
      option_writes="shadow"; option_jfd_omitted="file-data";
      phase=PHASE_LOAD; assert(retro_load_game(&game));
      assert(!strcmp(last_jfd_file_data_log,
         "JFD file-data: image=1 policy=1 class=writable format=D copies=0/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=writable-c11c\n"));
      char writable_stamp[41], unavailable_stamp[41], restored_stamp[41];
      assert(arc_libretro_test_boot_media_digest(writable_stamp));
      retro_unload_game();
      char jfd_path[4096];
      int path_length=snprintf(jfd_path,sizeof(jfd_path),"%s/arcdroid/jfd",save_path);
      assert(path_length>0 && (size_t)path_length<sizeof(jfd_path));
      assert(!rmdir(jfd_path));
      FILE *blocker=fopen(jfd_path,"wb"); assert(blocker && !fclose(blocker));
      phase=PHASE_LOAD; assert(retro_load_game(&game));
      assert(!strcmp(last_jfd_file_data_log,
         "JFD file-data: image=1 policy=1 class=store-unavailable format=D copies=1/1 presented=1 skipped-sides=0 incomplete-dirs=0 reason=presented\n") &&
         arc_libretro_test_jfd_slot_state(0u,1u,1u,NULL,NULL)==3u);
      assert(arc_libretro_test_boot_media_digest(unavailable_stamp) &&
             strcmp(writable_stamp,unavailable_stamp));
      retro_unload_game();
      option_writes="off";
      phase=PHASE_LOAD; assert(retro_load_game(&game));
      assert(!strcmp(last_jfd_file_data_log,
         "JFD file-data: image=1 policy=1 class=disk-writes-off format=D copies=1/1 presented=1 skipped-sides=0 incomplete-dirs=0 reason=presented\n"));
      retro_unload_game();
      option_writes="shadow";
      const char *rw_path=game.path;
      char sibling[4096];
      char *basename;
      int sibling_length=snprintf(sibling,sizeof(sibling),"%s",rw_path);
      assert(sibling_length>0 && (size_t)sibling_length<sizeof(sibling));
      basename=strrchr(sibling,'/'); assert(basename);
      ++basename;
      assert((size_t)(basename-sibling)+sizeof("d-layout-unsupported-rw.jfd") <=
             sizeof(sibling));
      strcpy(basename,"d-layout-unsupported-rw.jfd");
      game.path=sibling;
      phase=PHASE_LOAD; assert(retro_load_game(&game));
      assert(!strcmp(last_jfd_file_data_log,
         "JFD file-data: image=1 policy=1 class=layout-unsupported format=D copies=1/1 presented=1 skipped-sides=0 incomplete-dirs=0 reason=presented\n"));
      retro_unload_game();
      strcpy(basename,"d-terminal-wp-rw.jfd");
      game.path=sibling;
      arc_libretro_test_jfd_wp_layout_supported(true);
      phase=PHASE_LOAD; assert(retro_load_game(&game));
      assert(!strcmp(last_jfd_file_data_log,
         "JFD file-data: image=1 policy=1 class=wp-flag format=D copies=1/1 presented=1 skipped-sides=0 incomplete-dirs=0 reason=presented\n"));
      retro_unload_game();
      arc_libretro_test_jfd_wp_layout_supported(false);
      game.path=rw_path;
      assert(!remove(jfd_path));
      phase=PHASE_LOAD; assert(retro_load_game(&game));
      assert(!strcmp(last_jfd_file_data_log,
         "JFD file-data: image=1 policy=1 class=writable format=D copies=0/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=writable-c11c\n"));
      assert(arc_libretro_test_boot_media_digest(restored_stamp) &&
             !strcmp(writable_stamp,restored_stamp));
      retro_unload_game(); retro_deinit();
      assert(open_files==0u && open_dirs==0u && dlclose(module)==0);
      puts("jfd-file-data-unavailable: passed");
      return 0;
   }

   if (jfd_file_data_set)
   {
      option_machine="A3000"; option_cpu="ARM2";
      option_launch="off"; option_jfd_boot="native";
      option_writes="shadow"; option_jfd_omitted="file-data";
      char rw_source[4096];
      snprintf(rw_source,sizeof(rw_source),"%s",game.path);
      char *slash=strrchr(rw_source,'/'); assert(slash);
      snprintf(slash+1u,sizeof(rw_source)-(size_t)(slash+1u-rw_source),
               "d-terminal-rw.jfd");
      ArcDigest rw_digest=digest_file(rw_source);
      char slot_a[4096],slot_b[4096],slot_tmp[4096];
      snprintf(slot_a,sizeof(slot_a),"%s/arcdroid/jfd/%s.a",save_path,rw_digest.sha1);
      snprintf(slot_b,sizeof(slot_b),"%s/arcdroid/jfd/%s.b",save_path,rw_digest.sha1);
      snprintf(slot_tmp,sizeof(slot_tmp),"%s/arcdroid/jfd/%s.tmp",save_path,rw_digest.sha1);
      /* A prior aborted run must not supply records or damage this run. */
      (void)remove(slot_a); (void)remove(slot_b); (void)remove(slot_tmp);
      phase=PHASE_LOAD; assert(retro_load_game(&game));
      assert(disk_control.get_num_images()==2u &&
             arc_libretro_test_jfd_slot_state(0u,1u,1u,NULL,NULL)==3u &&
             arc_libretro_test_jfd_slot_state(1u,1u,1u,NULL,NULL)==0u &&
             jfd_file_data_log_lines==1u &&
             !strcmp(last_jfd_file_data_log,
               "JFD file-data: image=1 policy=1 class=wp-flag format=D copies=1/1 presented=1 skipped-sides=0 incomplete-dirs=0 reason=presented\n"));
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_image_index(1u) &&
             disk_control.set_eject_state(false));
      assert(arc_libretro_test_jfd_slot_state(0u,1u,1u,NULL,NULL)==0u &&
             arc_libretro_test_jfd_slot_state(1u,1u,1u,NULL,NULL)==7u &&
             jfd_file_data_log_lines==2u &&
             !strcmp(last_jfd_file_data_log,
               "JFD file-data: image=2 policy=1 class=writable format=D copies=0/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=writable-c11c\n"));
      assert(arc_libretro_test_jfd_commit(1u,4u,0x44u));
      retro_unload_game();
      const char *damaged_slot=file_exists(slot_a)?slot_a:slot_b;
      FILE *damage=fopen(damaged_slot,"r+b"); assert(damage);
      int first=fgetc(damage);
      assert(first>=0 && !fseek(damage,0L,SEEK_SET) &&
             fputc(first^1,damage)>=0 && !fclose(damage));
      phase=PHASE_LOAD; assert(retro_load_game(&game));
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_image_index(1u) &&
             disk_control.set_eject_state(false));
      assert(jfd_file_data_log_lines==4u &&
             !strcmp(last_jfd_file_data_log,
               "JFD file-data: image=2 policy=1 class=damaged-store format=D copies=0/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=damaged-store\n"));
      retro_unload_game();
      assert(!remove(damaged_slot));
      retro_deinit();
      assert(open_files==0u && open_dirs==0u && dlclose(module)==0);
      puts("jfd-file-data-set: passed");
      return 0;
   }

   if (jfd_file_data_step)
   {
      option_machine="A3000"; option_cpu="ARM2";
      option_launch="off"; option_jfd_boot="native";
      option_writes="shadow"; option_jfd_omitted="file-data";
      phase=PHASE_LOAD; assert(retro_load_game(&game));
      assert(jfd_file_data_log_lines==1u &&
             !strcmp(last_jfd_file_data_log,
               "JFD file-data: image=1 policy=1 class=wp-flag format=D copies=1/1 presented=1 skipped-sides=0 incomplete-dirs=0 reason=presented\n") &&
             arc_libretro_test_jfd_slot_state(0u,1u,1u,NULL,NULL)==3u);
      char file_stamp[41],strict_stamp[41],again_stamp[41];
      assert(arc_libretro_test_boot_media_digest(file_stamp));
      option_jfd_omitted="strict"; variable_update=true;
      phase=PHASE_RUN; retro_run();
      assert(arc_libretro_test_jfd_slot_state(0u,1u,1u,NULL,NULL)==3u &&
             arc_libretro_test_boot_media_digest(again_stamp) &&
             !strcmp(file_stamp,again_stamp));
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_eject_state(false));
      assert(jfd_file_data_log_lines==2u &&
             arc_libretro_test_jfd_slot_state(0u,1u,1u,NULL,NULL)==3u);
      assert(disk_control.set_eject_state(true));
      retro_reset();
      assert(jfd_file_data_log_lines==2u);
      assert(disk_control.set_eject_state(false));
      assert(jfd_file_data_log_lines==3u &&
             !strcmp(last_jfd_file_data_log,
               "JFD file-data: image=1 policy=1 class=wp-flag format=D copies=0/0 presented=0 skipped-sides=0 incomplete-dirs=0 reason=option-strict\n") &&
             arc_libretro_test_jfd_slot_state(0u,1u,1u,NULL,NULL)==0u);
      assert(arc_libretro_test_boot_media_digest(strict_stamp) &&
             strcmp(file_stamp,strict_stamp));
      option_jfd_omitted="file-data"; variable_update=true;
      retro_reset();
      assert(jfd_file_data_log_lines==4u &&
             arc_libretro_test_jfd_slot_state(0u,1u,1u,NULL,NULL)==3u);
      assert(arc_libretro_test_boot_media_digest(again_stamp) &&
             !strcmp(file_stamp,again_stamp));
      option_writes="off"; variable_update=true;
      retro_reset();
      assert(jfd_file_data_log_lines==5u &&
             !strcmp(last_jfd_file_data_log,
               "JFD file-data: image=1 policy=1 class=disk-writes-off format=D copies=1/1 presented=1 skipped-sides=0 incomplete-dirs=0 reason=presented\n") &&
             arc_libretro_test_jfd_slot_state(0u,1u,1u,NULL,NULL)==3u);
      retro_unload_game(); retro_deinit();
      assert(open_files==0u && open_dirs==0u && dlclose(module)==0);
      puts("jfd-file-data-step: passed");
      return 0;
   }

   if (jfd_identification_step)
   {
      option_machine = argv[6];
      option_cpu = !strcmp(option_machine, "A3020") ? "ARM250" : "ARM2";
      option_launch = "off"; option_jfd_boot = "native";
      const char *mode = argv[8];
      bool probe_write = !strcmp(mode, "probe-write");
      bool veto = !strcmp(mode, "veto");
      bool claim = !strcmp(mode, "claim");
      bool write_slot = !strcmp(mode, "write");
      bool readback = !strcmp(mode, "readback");
      bool failure = !strcmp(mode, "failed");
      bool off = !strcmp(mode, "off");
      bool damaged = !strcmp(mode, "damaged");
      assert(probe_write || veto || claim || write_slot || readback ||
             failure || off || damaged);
      option_writes = off ? "off" : "shadow";
      variable_update = true;
      uint32_t expected = (uint32_t)strtoul(argv[7], NULL, 16);
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(strstr(last_jfd_identification_log,
                    "format=D schedule=5 timing=untimed captured=0,1,2,4 presented=3 basis=free maps=1 reason=presented"));
      uint32_t identification_time = 0u, position = 0u, creation_time = 0u;
      assert(arc_libretro_test_jfd_presented(0u, 3u,
                                             &identification_time));
      uint8_t state = arc_libretro_test_jfd_slot_state(
         0u, 0u, 3u, &position, &creation_time);
      if (state != (off ? 9u : damaged ? 1u : write_slot || failure ? 7u :
                    readback ? 15u : 5u) || position != identification_time)
         fprintf(stderr, "identification %s %s: state=%u pos=%u id=%u creation=%u\n",
                 option_machine, mode, state, position, identification_time,
                 creation_time);
      assert(position == identification_time &&
             ((!off && !damaged) ? creation_time == identification_time : true) &&
             state == (off ? 9u : damaged ? 1u : write_slot || failure ? 7u :
                       readback ? 15u : 5u));
      bool protected = true;
      assert(arc_engine_test_drive_media(0u, &protected, NULL) &&
             protected == (off || damaged));
      uint8_t byte = 0xffu;
      assert(arc_libretro_test_jfd_created_byte(0u, 0u, 3u, 0u,
                                                 &byte) ==
             (write_slot || failure || readback));
      if (write_slot || failure) assert(byte == 0u);
      if (readback) assert(byte == 0x5au);
      if (damaged) assert(jfd_damaged_notice_calls == 1u);
      if (failure) fault_rename_substring = "/arcdroid/jfd/";
      unsigned failed_before = jfd_failed_status_calls;
      ArcDigest digest = digest_file(game.path);
      char a[4096], b[4096];
      snprintf(a, sizeof(a), "%s/arcdroid/jfd/%s.a", save_path, digest.sha1);
      snprintf(b, sizeof(b), "%s/arcdroid/jfd/%s.b", save_path, digest.sha1);
      if (!off && !damaged)
      {
         phase = PHASE_RUN;
         unsigned steady = 0u;
         for (unsigned frame = 0u; frame < 900u && steady < 8u; ++frame)
         {
            retro_run();
            steady = last_video_pixel == expected ? steady + 1u : 0u;
         }
         if (steady < 8u)
            fprintf(stderr, "identification %s %s: got %08x expected %08x\n",
                    option_machine, mode, last_video_pixel, expected);
         assert(steady == 8u);
         state = arc_libretro_test_jfd_slot_state(
            0u, 0u, 3u, &position, &creation_time);
         if (state != (claim ? 7u : failure ? 3u :
                       write_slot || readback ? 15u : 5u))
            fprintf(stderr, "identification %s %s after: state=%u pos=%u id=%u creation=%u\n",
                    option_machine, mode, state, position,
                    identification_time, creation_time);
         assert(position == identification_time &&
                creation_time == identification_time &&
                state == (claim ? 7u : failure ? 3u :
                          write_slot || readback ? 15u : 5u));
         if (probe_write || veto)
            assert(jfd_refusal_log_lines == 1u &&
                   strstr(last_jfd_refusal_log, probe_write ?
                          "reason=not-found" : "reason=probed-unformatted") &&
                   strstr(last_jfd_refusal_log,
                          probe_write ? "sector=3" : "sector=0") &&
                   arc_libretro_test_jfd_probe_mask(0u, 0u) == (1u << 3u) &&
                   !file_exists(a) && !file_exists(b) &&
                   jfd_failed_status_calls == failed_before);
         else
            assert(file_exists(a) || file_exists(b));
         if (failure)
         {
            assert(!fault_rename_substring &&
                   jfd_failed_status_calls == failed_before + 1u &&
                   arc_libretro_test_jfd_created_byte(0u, 0u, 3u, 0u,
                                                       &byte) && byte == 0u);
         }
         if (write_slot)
         {
            assert(arc_libretro_test_jfd_created_byte(0u, 0u, 3u, 0u,
                                                       &byte) && byte == 0x5au);
            retro_reset();
            assert(arc_libretro_test_jfd_slot_state(
                       0u, 0u, 3u, &position, NULL) == 15u &&
                   position == identification_time);
            assert(disk_control.set_eject_state(true) &&
                   disk_control.set_eject_state(false) &&
                   arc_libretro_test_jfd_slot_state(
                      0u, 0u, 3u, &position, NULL) == 15u &&
                   position == identification_time);
         }
      }
      else assert((file_exists(a) || file_exists(b)) &&
                  jfd_failed_status_calls == failed_before);
      retro_unload_game(); retro_deinit();
      assert(hash_file(game.path) == source_hash &&
             open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      printf("jfd_identification_step: machine=%s mode=%s outcome=%08x\n",
             option_machine, mode, expected);
      return 0;
   }

   if (jfd_allocation_step)
   {
      option_machine = argv[6];
      option_cpu = !strcmp(option_machine, "A3020") ? "ARM250" : "ARM2";
      option_launch = "off"; option_jfd_boot = "native";
      option_writes = "shadow"; variable_update = true;
      uint32_t expected = (uint32_t)strtoul(argv[7], NULL, 16);
      bool veto = !strcmp(argv[8], "veto");
      bool claim = !strcmp(argv[8], "claim");
      bool read_only = !strcmp(argv[8], "read");
      assert(veto || claim || read_only);
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      bool protected = true;
      uint8_t zero = 0xffu;
      assert(arc_engine_test_drive_media(0u, &protected, NULL) &&
             !protected &&
             arc_libretro_test_jfd_created_byte(0u, 0u, 3u, 0u,
                                                &zero) == read_only);
      unsigned steady = 0u;
      unsigned first_expected_frame = 900u;
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 900u && steady < 8u; ++frame)
      {
         retro_run();
         if (last_video_pixel == expected && first_expected_frame == 900u)
            first_expected_frame = frame;
         steady = last_video_pixel == expected ? steady + 1u : 0u;
      }
      if (steady < 8u)
         fprintf(stderr, "allocation %s %s: got %08x expected %08x\n",
                 option_machine, argv[8], last_video_pixel, expected);
      assert(steady == 8u);
      /* The WD veto probe needs two terminal five-index searches. Its final
       * result cannot be captured by the Windows script's fixed two-second
       * startup delay; wait for the completed video mode there instead. */
      if (veto && !strcmp(option_machine, "A3000"))
         assert(first_expected_frame >= 100u);
      ArcDigest digest = digest_file(game.path);
      char a[4096], b[4096];
      snprintf(a, sizeof(a), "%s/arcdroid/jfd/%s.a", save_path, digest.sha1);
      snprintf(b, sizeof(b), "%s/arcdroid/jfd/%s.b", save_path, digest.sha1);
      assert((file_exists(a) || file_exists(b)) == !veto &&
             arc_libretro_test_jfd_created_byte(0u, 0u, 3u, 0u,
                                                &zero) == !veto);
      if (!veto) assert(zero == 0u);
      else assert(jfd_refusal_log_lines == 1u &&
                  strstr(last_jfd_refusal_log,
                         "reason=probed-unformatted"));
      if (veto)
      {
         retro_reset();
         zero = 0xffu;
         assert(!arc_libretro_test_jfd_created_byte(0u, 0u, 3u, 0u,
                                                    &zero));
         last_video_pixel = 0u; steady = 0u;
         for (unsigned frame = 0u; frame < 900u && steady < 8u; ++frame)
         {
            retro_run();
            steady = last_video_pixel == expected ? steady + 1u : 0u;
         }
         assert(steady == 8u && jfd_refusal_log_lines == 1u &&
                !file_exists(a) && !file_exists(b));
      }
      retro_unload_game(); retro_deinit();
      assert(hash_file(game.path) == source_hash &&
             open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      printf("jfd_allocation_step: machine=%s mode=%s outcome=%08x\n",
             option_machine, argv[8], expected);
      return 0;
   }

   if (jfd_create_step || jfd_merge_step)
   {
      option_machine = argv[6];
      option_cpu = !strcmp(option_machine, "A3020") ? "ARM250" : "ARM2";
      option_launch = "off"; option_jfd_boot = "native";
      bool off_to_on = jfd_merge_step && !strcmp(argv[8], "off-to-on");
      bool damaged = jfd_merge_step && !strcmp(argv[8], "damaged");
      bool failure = jfd_merge_step && !strcmp(argv[8], "failed");
      bool advance = jfd_merge_step && !strcmp(argv[8], "advance");
      bool pre_read = jfd_merge_step &&
                      (!strcmp(argv[8], "pre-read") ||
                       !strcmp(argv[8], "pre-read-saved"));
      option_writes = off_to_on ? "off" : "shadow";
      variable_update = true;
      uint32_t expected = (uint32_t)strtoul(argv[7], NULL, 16);
      bool expect_slot = !strcmp(argv[8], "slot") || off_to_on ||
                         damaged || failure || advance ||
                         !strcmp(argv[8], "pre-read-saved");
      bool protected = true;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(arc_engine_test_drive_media(0u, &protected, NULL) &&
             protected == ((!expect_slot && !pre_read && !failure) ||
                           off_to_on || damaged));
      if (jfd_merge_step)
         assert(arc_libretro_test_jfd_formatted_reads(0u) ==
                (!protected && !damaged));
      if (pre_read)
      {
         uint8_t absent = 0xffu;
         assert(!arc_libretro_test_jfd_created_byte(0u, 0u, 1u, 0u,
                                                    &absent));
      }
      if (damaged)
      {
         assert(jfd_damaged_notice_calls == 1u);
         for (unsigned offset = 0u; offset < 1024u; ++offset)
         {
            uint8_t byte = 0xffu;
            assert(arc_libretro_test_jfd_created_byte(0u, 0u, 3u,
                                                       offset, &byte) &&
                   byte == (offset < 128u ? 0x5au : 0u));
         }
         uint8_t absent = 0xffu;
         assert(!arc_libretro_test_jfd_created_byte(0u, 0u, 1u, 0u,
                                                    &absent));
      }
      if (advance)
      {
         assert(arc_libretro_test_jfd_commit(0u, 1u, 0x31u));
         for (unsigned offset = 0u; offset < 1024u; ++offset)
         {
            uint8_t byte = 0xffu;
            assert(arc_libretro_test_jfd_created_byte(0u, 0u, 3u,
                                                       offset, &byte) &&
                   byte == (offset < 128u ? 0x5au : 0u));
         }
      }
      unsigned failed_notices_before = jfd_failed_status_calls;
      if (failure)
      {
         /* The simulated rename race leaves an empty destination file, but
          * no valid generation. The prior allocation view remains visible. */
         fault_rename_skip_matches = 0u;
         fault_rename_substring = "/arcdroid/jfd/";
      }
      phase = PHASE_RUN;
      unsigned steady = 0u;
      if (off_to_on)
      {
         for (unsigned frame = 0u; frame < 800u && steady < 8u; ++frame)
         {
            retro_run();
            steady = last_video_pixel == 0x0000006au ? steady + 1u : 0u;
         }
         assert(steady == 8u && !arc_libretro_test_jfd_formatted_reads(0u));
         ArcDigest off_digest = digest_file(game.path);
         char off_a[4096], off_b[4096];
         snprintf(off_a, sizeof(off_a), "%s/arcdroid/jfd/%s.a", save_path,
                  off_digest.sha1);
         snprintf(off_b, sizeof(off_b), "%s/arcdroid/jfd/%s.b", save_path,
                  off_digest.sha1);
         assert(!file_exists(off_a) && !file_exists(off_b));
         option_writes = "shadow"; variable_update = true;
         retro_reset();
         assert(arc_libretro_test_jfd_formatted_reads(0u));
         assert(arc_engine_test_drive_media(0u, &protected, NULL) &&
                !protected);
         uint8_t absent = 0xffu;
         assert(arc_libretro_test_jfd_created_byte(0u, 0u, 3u, 0u,
                                                   &absent) && absent == 0u);
         steady = 0u;
      }
      for (unsigned frame = 0u; frame < 800u && steady < 8u; ++frame)
      {
         retro_run();
         steady = last_video_pixel == expected ? steady + 1u : 0u;
      }
      if (last_video_pixel != expected || steady < 8u)
         fprintf(stderr, "creation %s %s: got %08x expected %08x\n",
                 option_machine, argv[8], last_video_pixel, expected);
      assert(last_video_pixel == expected && steady == 8u);
      if (jfd_merge_step)
         assert(arc_libretro_test_jfd_formatted_reads(0u) ==
                (!protected && !damaged));
      if (failure)
      {
         assert(!fault_rename_substring && !fault_rename_skip_matches &&
                jfd_failed_status_calls == failed_notices_before + 1u &&
                arc_libretro_test_jfd_formatted_reads(0u));
         for (unsigned offset = 0u; offset < 1024u; ++offset)
         {
            uint8_t byte = 0xffu;
            assert(arc_libretro_test_jfd_created_byte(0u, 0u, 3u,
                                                       offset, &byte) && !byte);
         }
      }
      if (jfd_merge_step && expect_slot)
         for (unsigned offset = 0u; offset < 1024u; ++offset)
         {
            uint8_t byte = 0xffu;
            assert(arc_libretro_test_jfd_created_byte(0u, 0u, 3u,
                                                       offset, &byte) &&
                   byte == (failure ? 0u : offset < 128u ? 0x5au : 0u));
         }
      ArcDigest digest = digest_file(game.path);
      char a[4096], b[4096];
      snprintf(a, sizeof(a), "%s/arcdroid/jfd/%s.a", save_path, digest.sha1);
      snprintf(b, sizeof(b), "%s/arcdroid/jfd/%s.b", save_path, digest.sha1);
      assert((file_exists(a) || file_exists(b)) == expect_slot);
      if (failure)
      {
         uint64_t slot_a_hash = file_exists(a) ? hash_file(a) : 0u;
         uint64_t slot_b_hash = file_exists(b) ? hash_file(b) : 0u;
         assert(!arc_libretro_test_jfd_commit(0u, 0u, 0x7cu) &&
                arc_libretro_test_jfd_formatted_reads(0u) &&
                (file_exists(a) ? hash_file(a) : 0u) == slot_a_hash &&
                (file_exists(b) ? hash_file(b) : 0u) == slot_b_hash);
      }
      if (expect_slot && expected == 0x006a0000u &&
          (jfd_merge_step || !strcmp(option_machine, "A3000")))
      {
         retro_reset();
         if (jfd_merge_step)
            assert(arc_libretro_test_jfd_formatted_reads(0u));
         last_video_pixel = 0u;
         steady = 0u;
         for (unsigned frame = 0u; frame < 800u && steady < 8u; ++frame)
         {
            retro_run();
            steady = last_video_pixel == 0x00001500u ? steady + 1u : 0u;
         }
         assert(steady == 8u);
         assert(disk_control.set_eject_state(true) &&
                disk_control.set_eject_state(false));
         if (jfd_merge_step)
            assert(arc_libretro_test_jfd_formatted_reads(0u));
         retro_reset();
         if (jfd_merge_step)
            assert(arc_libretro_test_jfd_formatted_reads(0u));
         last_video_pixel = 0u;
         steady = 0u;
         for (unsigned frame = 0u; frame < 800u && steady < 8u; ++frame)
         {
            retro_run();
            steady = last_video_pixel == 0x00001500u ? steady + 1u : 0u;
         }
         assert(steady == 8u);
      }
      if (!expect_slot && !jfd_merge_step)
      {
         const char *why = (strstr(game.path, "wp-create") ||
                            strstr(game.path, "wp-merge")) ?
                           "wp-flag" : "layout-unsupported";
         char expected_refusal[160];
         snprintf(expected_refusal, sizeof(expected_refusal),
                  "image=1 track=0 side=0 sector=0 reason=%s", why);
         assert(jfd_refusal_log_lines == 1u &&
                strstr(last_jfd_refusal_log, expected_refusal));
         if (!strcmp(option_machine, "A3000") &&
             strstr(game.path, "nonmatching"))
         {
            retro_reset();
            last_video_pixel = 0u;
            steady = 0u;
            for (unsigned frame = 0u; frame < 800u && steady < 8u; ++frame)
            {
               retro_run();
               steady = last_video_pixel == expected ? steady + 1u : 0u;
            }
            assert(steady == 8u && jfd_refusal_log_lines == 1u);
            assert(disk_control.set_eject_state(true) &&
                   disk_control.set_eject_state(false));
            retro_reset();
            last_video_pixel = 0u;
            steady = 0u;
            for (unsigned frame = 0u; frame < 800u && steady < 8u; ++frame)
            {
               retro_run();
               steady = last_video_pixel == expected ? steady + 1u : 0u;
            }
            assert(steady == 8u && jfd_refusal_log_lines == 1u);
         }
      }
      retro_unload_game(); retro_deinit();
      assert(hash_file(game.path) == source_hash &&
             open_files == 0u && open_dirs == 0u && dlclose(module) == 0);
      printf("jfd_creation_step: machine=%s outcome=%08x slot=%u\n",
             option_machine, expected, expect_slot);
      return 0;
   }

   if (jfd_writes)
   {
      /* The test ROM publishes one steady result after the controller ends:
       * pristine+accepted, saved-data-before-write+accepted, pristine+WP,
       * or other error. These are the same palette colours Windows checks. */
      const uint32_t jfd_first = 0x006a0000u;
      const uint32_t jfd_saved = 0x00001500u;
      const uint32_t jfd_protected = 0x00006a00u;
      const uint32_t jfd_other = 0x0000006au;
      char wp[4096], neither[4096], legacy[4096], gzip[4096];
      char zipped[4096], mixed[4096], slot_a[4096], slot_b[4096];
      snprintf(wp, sizeof(wp), "%s/wp-d.jfd", jfd_fixture_path);
      snprintf(neither, sizeof(neither), "%s/neither-d.jfd", jfd_fixture_path);
      snprintf(legacy, sizeof(legacy), "%s/legacy-d.jfd", jfd_fixture_path);
      snprintf(gzip, sizeof(gzip), "%s/rw-d-gzip.jfd", jfd_fixture_path);
      snprintf(zipped, sizeof(zipped), "%s/rw-d.zip", jfd_fixture_path);
      snprintf(mixed, sizeof(mixed), "%s/mixed.m3u", jfd_fixture_path);
      char jfd_store_directory[4096];
      snprintf(jfd_store_directory, sizeof(jfd_store_directory),
               "%s/arcdroid/jfd", save_path);
      static const char *const excluded_reasons[] = {
         "wp-flag", "no-flag", "legacy-header"
      };
      const char *const excluded_paths[] = {wp, neither, legacy};
      option_machine = "A3000";
      option_launch = "off";
      option_jfd_boot = "native";
      option_writes = "shadow";
      fault_mkdir_substring = "/arcdroid/jfd";
      for (unsigned i = 0u; i < 3u; ++i)
      {
         struct retro_game_info excluded = {excluded_paths[i], NULL, 0u, NULL};
         phase = PHASE_LOAD; assert(retro_load_game(&excluded));
         assert(strstr(last_jfd_writes_log, excluded_reasons[i]) &&
                !file_exists(jfd_store_directory) && fault_mkdir_substring);
         retro_unload_game();
      }
      fault_mkdir_substring = NULL;
      char sparse_d[4096], allocated_e[4096], sparse_f[4096],
           mixed_f[4096], unresolved_f[4096], sparse_slot_a[4096];
      snprintf(sparse_d, sizeof(sparse_d),
               "%s/../jfd-identification/sparse-d-timed.jfd", jfd_fixture_path);
      snprintf(allocated_e, sizeof(allocated_e),
               "%s/../jfd-identification/sparse-e-allocated.jfd", jfd_fixture_path);
      snprintf(sparse_f, sizeof(sparse_f),
               "%s/../jfd-identification/sparse-f-timed.jfd", jfd_fixture_path);
      snprintf(mixed_f, sizeof(mixed_f),
               "%s/../jfd-identification/sparse-f-mixed-basis.jfd", jfd_fixture_path);
      snprintf(unresolved_f, sizeof(unresolved_f),
               "%s/../jfd-identification/sparse-f-root-unresolved.jfd", jfd_fixture_path);
      uint64_t sparse_hash = hash_file(sparse_d);
      ArcDigest sparse_digest = digest_file(sparse_d);
      snprintf(sparse_slot_a, sizeof(sparse_slot_a),
               "%s/arcdroid/jfd/%s.a", save_path, sparse_digest.sha1);
      struct retro_game_info sparse_game = {sparse_d, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&sparse_game));
      uint32_t position = 0u;
      uint8_t omitted_byte = 0u;
      assert(strstr(last_jfd_identification_log,
                    "native=yes format=D schedule=5 timing=timed captured=0,1,2 presented=3,4 basis=free maps=1 reason=presented") &&
             arc_libretro_test_jfd_presented(0u, 3u, &position) &&
             position == 116352u &&
             arc_libretro_test_jfd_presented(0u, 4u, NULL) &&
             !arc_libretro_test_jfd_presented(0u, 2u, NULL) &&
             !arc_libretro_test_jfd_created_byte(0u, 0u, 3u, 0u,
                                                   &omitted_byte) &&
             !file_exists(sparse_slot_a));
      retro_reset();
      assert(arc_libretro_test_jfd_presented(0u, 3u, &position) &&
             position == 116352u);
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_eject_state(false));
      assert(arc_libretro_test_jfd_presented(0u, 3u, &position) &&
             position == 116352u && !file_exists(sparse_slot_a));
      retro_unload_game();
      option_writes = "off"; variable_update = true;
      phase = PHASE_LOAD; assert(retro_load_game(&sparse_game));
      assert(arc_libretro_test_jfd_presented(0u, 3u, NULL) &&
             strstr(last_jfd_identification_log, "reason=presented"));
      retro_unload_game();
      option_writes = "shadow"; variable_update = true;
      struct retro_game_info allocated_game = {allocated_e, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&allocated_game));
      assert(strstr(last_jfd_identification_log,
                    "format=E schedule=5 timing=timed captured=0,1,2,4 presented=none basis=none maps=3 reason=not-free") &&
             !arc_libretro_test_jfd_presented(0u, 3u, NULL));
      retro_unload_game();
      option_machine = "A3020"; variable_update = true;
      struct retro_game_info prefix_game = {sparse_f, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&prefix_game));
      assert(strstr(last_jfd_identification_log,
                    "format=F schedule=10 timing=timed captured=0,3,4,5,6,7,8,9 presented=1,2 basis=prefix maps=3 reason=presented") &&
             arc_libretro_test_jfd_presented(0u, 1u, NULL) &&
             arc_libretro_test_jfd_presented(0u, 2u, NULL));
      retro_unload_game();
      struct retro_game_info mixed_basis_game = {mixed_f, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&mixed_basis_game));
      assert(strstr(last_jfd_identification_log,
                    "presented=1,2,4 basis=mixed maps=3 reason=presented"));
      retro_unload_game();
      struct retro_game_info unresolved_game = {unresolved_f, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&unresolved_game));
      assert(strstr(last_jfd_identification_log,
                    "presented=none basis=none maps=3 reason=metadata-unresolved"));
      retro_unload_game();
      option_machine = "A3000"; variable_update = true;
      assert(hash_file(sparse_d) == sparse_hash && !file_exists(sparse_slot_a));
      uint64_t wp_source_hash = hash_file(wp);
      uint64_t zip_source_hash = hash_file(zipped);
      uint64_t gzip_source_hash = hash_file(gzip);
      ArcDigest digest = digest_file(jfd_content_path);
      snprintf(slot_a, sizeof(slot_a), "%s/arcdroid/jfd/%s.a", save_path, digest.sha1);
      snprintf(slot_b, sizeof(slot_b), "%s/arcdroid/jfd/%s.b", save_path, digest.sha1);
      option_machine = "A3000";
      option_launch = "off";
      option_jfd_boot = "native";
      option_writes = "shadow";
      bool write_protected = true;
      phase = PHASE_LOAD;
      if (!retro_load_game(&game))
      { fprintf(stderr, "JFD load failed: %s\n", last_message); abort(); }
      char content_id_before[64], cmos_path_before[4096], choice_path_before[4096];
      snprintf(content_id_before, sizeof(content_id_before), "%s", arc_libretro_test_content_id());
      snprintf(cmos_path_before, sizeof(cmos_path_before), "%s", arc_libretro_test_cmos_path());
      snprintf(choice_path_before, sizeof(choice_path_before), "%s", arc_libretro_test_choice_path());
      uint64_t staged_hash_before = hash_file(arc_libretro_test_staged_path(0u));
      assert(arc_engine_test_drive_media(0u, &write_protected, NULL) &&
             !write_protected);
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 600u; ++frame) retro_run();
      if (last_video_pixel != jfd_first)
         fprintf(stderr, "JFD first outcome: %08x\n", last_video_pixel);
      assert(last_video_pixel == jfd_first);
      assert(file_exists(slot_a) || file_exists(slot_b));
      retro_unload_game();
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(!strcmp(content_id_before, arc_libretro_test_content_id()) &&
             !strcmp(cmos_path_before, arc_libretro_test_cmos_path()) &&
             !strcmp(choice_path_before, arc_libretro_test_choice_path()) &&
             hash_file(arc_libretro_test_staged_path(0u)) == staged_hash_before);
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 600u; ++frame) retro_run();
      assert(last_video_pixel == jfd_saved);
      retro_reset();
      for (unsigned frame = 0u; frame < 600u; ++frame) retro_run();
      assert(last_video_pixel == jfd_saved);
      retro_unload_game();
      option_writes = "off"; variable_update = true;
      unsigned off_notices_before = jfd_off_notice_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(arc_engine_test_drive_media(0u, &write_protected, NULL) &&
             write_protected && jfd_off_notice_calls == off_notices_before + 1u);
      retro_reset();
      assert(jfd_off_notice_calls == off_notices_before + 1u);
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_eject_state(false) &&
             jfd_off_notice_calls == off_notices_before + 1u);
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 600u; ++frame) retro_run();
      assert(last_video_pixel == jfd_protected);
      retro_unload_game();
      option_writes = "original"; variable_update = true;
      unsigned original_notice_before = jfd_original_notice_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(arc_engine_test_drive_media(0u, &write_protected, NULL) &&
             !write_protected &&
             jfd_original_notice_calls == original_notice_before + 1u);
      retro_unload_game();
      option_writes = "shadow"; variable_update = true;
      for (unsigned i = 0u; i < 4u; ++i)
      {
         const char *path = i == 0u ? wp : i == 1u ? neither :
                            i == 2u ? legacy : gzip;
         struct retro_game_info candidate = {path, NULL, 0u, NULL};
         phase = PHASE_LOAD; assert(retro_load_game(&candidate));
         assert(arc_engine_test_drive_media(0u, &write_protected, NULL) &&
                (i == 3u ? !write_protected : write_protected));
         retro_unload_game();
      }
      struct retro_game_info zip_game = {zipped, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(arc_engine_test_drive_media(0u, &write_protected, NULL) &&
             !write_protected);
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 600u; ++frame) retro_run();
      assert(last_video_pixel == jfd_saved);
      retro_unload_game();
      ArcDigest wp_digest = digest_file(wp);
      char wp_slot_a[4096], wp_slot_b[4096];
      snprintf(wp_slot_a, sizeof(wp_slot_a), "%s/arcdroid/jfd/%s.a",
               save_path, wp_digest.sha1);
      snprintf(wp_slot_b, sizeof(wp_slot_b), "%s/arcdroid/jfd/%s.b",
               save_path, wp_digest.sha1);
      struct retro_game_info wp_game = {wp, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&wp_game));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 600u; ++frame) retro_run();
      assert(last_video_pixel == jfd_protected &&
             !file_exists(wp_slot_a) && !file_exists(wp_slot_b));
      retro_unload_game();

      char missing_jfd[4096], missing_slot_a[4096], missing_slot_b[4096];
      snprintf(missing_jfd, sizeof(missing_jfd), "%s/missing-one-d.jfd",
               jfd_fixture_path);
      ArcDigest missing_digest = digest_file(missing_jfd);
      snprintf(missing_slot_a, sizeof(missing_slot_a),
               "%s/arcdroid/jfd/%s.a", save_path, missing_digest.sha1);
      snprintf(missing_slot_b, sizeof(missing_slot_b),
               "%s/arcdroid/jfd/%s.b", save_path, missing_digest.sha1);
      struct retro_game_info missing_game = {missing_jfd, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&missing_game));
      assert(arc_engine_test_drive_media(0u, &write_protected, NULL) &&
             write_protected &&
             strstr(last_jfd_writes_log, "reason=layout-unsupported"));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 600u; ++frame) retro_run();
      /* This legacy probe first READs the missing sector and never reaches a
       * write. RNF stays visible and the image gate has published no slot. */
      assert(last_video_pixel == jfd_other &&
             !file_exists(missing_slot_a) && !file_exists(missing_slot_b));
      retro_unload_game();

      char adffs_jfd[4096], package_source[4096], package_directory[4096];
      char package_path[4096], adffs_slot_a[4096], adffs_slot_b[4096];
      snprintf(adffs_jfd, sizeof(adffs_jfd), "%s/adffs-rw.jfd", jfd_fixture_path);
      snprintf(package_source, sizeof(package_source), "%s/../adffs/synthetic.zip", jfd_fixture_path);
      snprintf(package_directory, sizeof(package_directory), "%s/arcdroid", system_path);
      make_directory(package_directory);
      snprintf(package_directory, sizeof(package_directory), "%s/arcdroid/adffs", system_path);
      make_directory(package_directory);
      snprintf(package_path, sizeof(package_path), "%s/A-valid.ZIP", package_directory);
      copy_file(package_source, package_path);
      uint64_t adffs_source_hash = hash_file(adffs_jfd);
      ArcDigest adffs_digest = digest_file(adffs_jfd);
      snprintf(adffs_slot_a, sizeof(adffs_slot_a), "%s/arcdroid/jfd/%s.a", save_path, adffs_digest.sha1);
      snprintf(adffs_slot_b, sizeof(adffs_slot_b), "%s/arcdroid/jfd/%s.b", save_path, adffs_digest.sha1);
      struct retro_game_info adffs_game = {adffs_jfd, NULL, 0u, NULL};
      arc_libretro_test_adffs_accept_test_os(true);
      option_jfd_boot = "adffs"; option_launch = "auto"; variable_update = true;
      fault_opendir_substring = "/arcdroid/jfd";
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_game));
      assert(strstr(last_jfd_boot_log, "JFD boot: path=adffs") &&
             strstr(last_jfd_writes_log, "reason=not-native") &&
             strstr(last_jfd_identification_log,
                    "native=no format=none schedule=0") &&
             strstr(last_jfd_identification_log, "reason=not-native") &&
             !arc_libretro_test_jfd_presented(0u, 3u, NULL) &&
             !fault_opendir_substring &&
             !arc_libretro_test_jfd_formatted_reads(0u));
      retro_unload_game();
      option_jfd_boot = "native"; option_launch = "off"; variable_update = true;
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_game));
      assert(!arc_libretro_test_jfd_formatted_reads(0u));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 600u; ++frame) retro_run();
      assert(last_video_pixel == jfd_first &&
             (file_exists(adffs_slot_a) || file_exists(adffs_slot_b)) &&
             !arc_libretro_test_jfd_formatted_reads(0u));
      retro_unload_game();
      option_jfd_boot = "adffs"; option_launch = "auto"; variable_update = true;
      assert(remove(package_path) == 0);
      unsigned native_notices_before = jfd_native_notice_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_game));
      assert(strstr(last_jfd_boot_log, "JFD boot: path=native reason=no-package") &&
             jfd_native_notice_calls == native_notices_before);
      retro_unload_game();
      copy_file(package_source, package_path);
      arc_libretro_test_fail_adffs_content_read(true);
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_game));
      assert(strstr(last_jfd_boot_log,
                    "JFD boot: path=native reason=content-unreadable") &&
             jfd_native_notice_calls == native_notices_before &&
             !arc_libretro_test_adffs_content_read_failure_pending());
      retro_unload_game();
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_game));
      assert(strstr(last_jfd_boot_log, "JFD boot: path=native reason=native-saves") &&
             jfd_native_notice_calls == native_notices_before + 1u &&
             hash_file(adffs_jfd) == adffs_source_hash);
      retro_reset();
      assert(jfd_native_notice_calls == native_notices_before + 1u);
      retro_unload_game();
      option_jfd_boot = "adffs"; variable_update = true;
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_game));
      assert(strstr(last_jfd_boot_log, "JFD boot: path=native reason=native-saves") &&
             hash_file(adffs_jfd) == adffs_source_hash);
      retro_unload_game();
      option_jfd_boot = "auto"; variable_update = true;

      assert(remove(package_path) == 0);
      option_jfd_boot = "native"; option_launch = "off"; variable_update = true;
      struct retro_game_info mixed_game = {mixed, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&mixed_game));
      assert(disk_control.get_num_images() == 3u);
      assert(disk_control.set_eject_state(true));
      assert(disk_control.set_image_index(1u));
      assert(disk_control.set_eject_state(false));
      assert(arc_engine_test_drive_media(0u, &write_protected, NULL) &&
             !write_protected);
      assert(disk_control.set_eject_state(true));
      assert(disk_control.set_image_index(2u));
      assert(disk_control.set_eject_state(false));
      assert(arc_engine_test_drive_media(0u, &write_protected, NULL) &&
             write_protected);
      retro_unload_game();
      char folder_jfd[4096];
      snprintf(folder_jfd, sizeof(folder_jfd),
               "%s/SaveSet.jfd/SaveSet.jfd", jfd_fixture_path);
      struct retro_game_info folder_game = {folder_jfd, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&folder_game));
      assert(disk_control.get_num_images() == 2u &&
             arc_engine_test_drive_media(0u, &write_protected, NULL) &&
             !write_protected);
      assert(disk_control.set_eject_state(true));
      assert(disk_control.set_image_index(1u));
      assert(disk_control.set_eject_state(false));
      assert(arc_engine_test_drive_media(0u, &write_protected, NULL) &&
             write_protected);
      assert(disk_control.set_eject_state(true));
      assert(disk_control.set_image_index(0u));
      assert(disk_control.set_eject_state(false));
      assert(arc_engine_test_drive_media(0u, &write_protected, NULL) &&
             !write_protected);
      retro_unload_game();
      for (unsigned cycle = 0u; cycle < 100u; ++cycle)
      {
         unsigned writes_before = jfd_write_log_lines;
         phase = PHASE_LOAD; assert(retro_load_game(&game));
         phase = PHASE_RUN;
         for (unsigned frame = 0u; frame < 100u &&
              jfd_write_log_lines == writes_before; ++frame) retro_run();
         assert(jfd_write_log_lines > writes_before);
         retro_unload_game();
         assert(open_files == 0u && open_dirs == 0u);
      }

      unsigned failed_notices_before = jfd_failed_status_calls;
      fault_rename_substring = "/arcdroid/jfd/";
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 600u &&
           jfd_failed_status_calls == failed_notices_before; ++frame) retro_run();
      assert(!fault_rename_substring &&
             jfd_failed_status_calls == failed_notices_before + 1u);
      retro_unload_game();

      const char *damaged_slot = file_exists(slot_a) ? slot_a : slot_b;
      FILE *damage_file = fopen(damaged_slot, "r+b");
      assert(damage_file);
      int first_byte = fgetc(damage_file);
      assert(first_byte >= 0 && fseek(damage_file, 0L, SEEK_SET) == 0 &&
             fputc(first_byte ^ 1, damage_file) >= 0 && fclose(damage_file) == 0);
      unsigned damaged_notices_before = jfd_damaged_notice_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(jfd_damaged_notice_calls == damaged_notices_before + 1u);
      retro_reset();
      assert(jfd_damaged_notice_calls == damaged_notices_before + 1u);
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_eject_state(false) &&
             jfd_damaged_notice_calls == damaged_notices_before + 1u);
      retro_unload_game();

      /* Guest writes must preserve launch-record bytes when the candidates
       * are unchanged, and must use the existing target/choice guards when
       * a root entry changes. Both writes go through WD1770 and the JFD store. */
      for (unsigned rename_case = 0u; rename_case < 2u; ++rename_case)
      {
         char launch_jfd[4096], launch_rom[4096], binding[4096];
         char launch_slot_a[4096], launch_slot_b[4096];
         const char *stem = rename_case ? "launch-renamed" : "launch-unchanged";
         snprintf(launch_jfd, sizeof(launch_jfd), "%s/%s-d.jfd",
                  jfd_fixture_path, stem);
         snprintf(launch_rom, sizeof(launch_rom),
                  "%s/../arcdroid-jfd-directory-%s.rom", jfd_fixture_path,
                  rename_case ? "rename" : "benign");
         uint64_t launch_source_hash = hash_file(launch_jfd);
         ArcDigest launch_digest = digest_file(launch_jfd);
         snprintf(binding, sizeof(binding), "%s/arcdroid/launch/%s.bind",
                  save_path, launch_digest.sha1);
         snprintf(launch_slot_a, sizeof(launch_slot_a),
                  "%s/arcdroid/jfd/%s.a", save_path, launch_digest.sha1);
         snprintf(launch_slot_b, sizeof(launch_slot_b),
                  "%s/arcdroid/jfd/%s.b", save_path, launch_digest.sha1);
         struct retro_game_info launch_game = {launch_jfd, NULL, 0u, NULL};
         copy_file(launch_rom, renamed);
         option_launch = "app1"; option_writes = "shadow";
         variable_update = true;
         phase = PHASE_LOAD;
         if (!retro_load_game(&launch_game))
         { fprintf(stderr, "JFD launch fixture: %s\n", last_message); abort(); }
         assert(arc_libretro_test_session_state() == 3u &&
                arc_engine_test_drive_media(1u, NULL, NULL) &&
                strstr(last_launch_log, "decision=!Alpha"));
         uint8_t binding_before[ARC_LAUNCH_BINDING_MAX_SIZE];
         size_t binding_size = read_bounded(binding, binding_before,
                                            sizeof(binding_before));
         retro_unload_game();

         option_launch = "auto"; option_launch_chooser = "remember";
         variable_update = true;
         phase = PHASE_LOAD; assert(retro_load_game(&launch_game));
         assert(arc_libretro_test_session_state() == 2u &&
                arc_libretro_test_chooser_rows() == 3u);
         phase = PHASE_RUN;
         retro_run();
         keyboard.callback(true, RETROK_RETURN, 0u, 0u); retro_run();
         keyboard.callback(false, RETROK_RETURN, 0u, 0u); retro_run();
         assert(arc_libretro_test_session_state() == 3u &&
                !file_exists(launch_slot_a) && !file_exists(launch_slot_b));
         char choice[4096];
         snprintf(choice, sizeof(choice), "%s", arc_libretro_test_choice_path());
         uint8_t choice_before[ARC_LAUNCH_CHOICE_MAX_SIZE];
         size_t choice_size = read_bounded(choice, choice_before,
                                           sizeof(choice_before));
         unsigned writes_before = jfd_write_log_lines;
         for (unsigned frame = 0u; frame < 600u; ++frame) retro_run();
         assert(jfd_write_log_lines > writes_before &&
                (file_exists(launch_slot_a) || file_exists(launch_slot_b)));
         retro_unload_game();

         option_launch = "app1"; variable_update = true;
         phase = PHASE_LOAD; assert(retro_load_game(&launch_game));
         if (rename_case)
            assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
                   strstr(last_launch_warning, "binding changed target") &&
                   strstr(last_launch_log, "!Alphb"));
         else
            assert(arc_engine_test_drive_media(1u, NULL, NULL) &&
                   strstr(last_launch_log, "decision=!Alpha"));
         uint8_t record_check[ARC_LAUNCH_BINDING_MAX_SIZE];
         assert(read_bounded(binding, record_check, sizeof(record_check)) ==
                   binding_size &&
                !memcmp(record_check, binding_before, binding_size));
         retro_unload_game();

         option_launch = "auto"; variable_update = true;
         phase = PHASE_LOAD; assert(retro_load_game(&launch_game));
         if (rename_case)
            assert(arc_libretro_test_session_state() == 2u &&
                   arc_libretro_test_chooser_rows() == 3u);
         else
            assert(arc_libretro_test_session_state() == 3u &&
                   strstr(last_launch_log, "decision=!Alpha"));
         uint8_t choice_check[ARC_LAUNCH_CHOICE_MAX_SIZE];
         assert(read_bounded(choice, choice_check, sizeof(choice_check)) ==
                   choice_size &&
                !memcmp(choice_check, choice_before, choice_size));
         if (rename_case)
         {
            phase = PHASE_RUN;
            retro_run();
            keyboard.callback(true, RETROK_RETURN, 0u, 0u); retro_run();
            keyboard.callback(false, RETROK_RETURN, 0u, 0u); retro_run();
            assert(arc_libretro_test_session_state() == 3u &&
                   strstr(last_launch_log, "decision=!Alphb") &&
                   (read_bounded(choice, choice_check, sizeof(choice_check)) !=
                      choice_size ||
                    memcmp(choice_check, choice_before, choice_size)));
         }
         retro_unload_game();
         assert(hash_file(launch_jfd) == launch_source_hash &&
                open_files == 0u && open_dirs == 0u);
      }

      /* The separately fingerprinted New-IO ROM performs the same complete
       * read, write and result classification through the A3020 controller. */
      char newio_rom[4096], newio_rw[4096], newio_wp[4096];
      char newio_slot_a[4096], newio_slot_b[4096];
      snprintf(newio_rom, sizeof(newio_rom),
               "%s/../arcdroid-jfd-newio-save-test.rom", jfd_fixture_path);
      snprintf(newio_rw, sizeof(newio_rw), "%s/rw-newio-d.jfd", jfd_fixture_path);
      snprintf(newio_wp, sizeof(newio_wp), "%s/wp-newio-d.jfd", jfd_fixture_path);
      uint64_t newio_rw_hash = hash_file(newio_rw);
      uint64_t newio_wp_hash = hash_file(newio_wp);
      ArcDigest newio_digest = digest_file(newio_rw);
      snprintf(newio_slot_a, sizeof(newio_slot_a), "%s/arcdroid/jfd/%s.a",
               save_path, newio_digest.sha1);
      snprintf(newio_slot_b, sizeof(newio_slot_b), "%s/arcdroid/jfd/%s.b",
               save_path, newio_digest.sha1);
      copy_file(newio_rom, renamed);
      option_machine = "A3020";
      option_cpu = "ARM250";
      option_writes = "shadow";
      option_jfd_boot = "native";
      option_launch = "off";
      variable_update = true;
      struct retro_game_info newio_game = {newio_rw, NULL, 0u, NULL};
      for (unsigned pass = 0u; pass < 2u; ++pass)
      {
         phase = PHASE_LOAD; assert(retro_load_game(&newio_game));
         assert(arc_engine_test_drive_media(0u, &write_protected, NULL) &&
                !write_protected);
         phase = PHASE_RUN;
         for (unsigned frame = 0u; frame < 600u; ++frame) retro_run();
         assert(last_video_pixel == (pass ? jfd_saved : jfd_first) &&
                (file_exists(newio_slot_a) || file_exists(newio_slot_b)));
         retro_unload_game();
      }
      ArcDigest newio_wp_digest = digest_file(newio_wp);
      char newio_wp_slot_a[4096], newio_wp_slot_b[4096];
      snprintf(newio_wp_slot_a, sizeof(newio_wp_slot_a), "%s/arcdroid/jfd/%s.a",
               save_path, newio_wp_digest.sha1);
      snprintf(newio_wp_slot_b, sizeof(newio_wp_slot_b), "%s/arcdroid/jfd/%s.b",
               save_path, newio_wp_digest.sha1);
      struct retro_game_info newio_wp_game = {newio_wp, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&newio_wp_game));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 600u; ++frame) retro_run();
      assert(last_video_pixel == jfd_protected &&
             !file_exists(newio_wp_slot_a) && !file_exists(newio_wp_slot_b) &&
             hash_file(newio_rw) == newio_rw_hash &&
             hash_file(newio_wp) == newio_wp_hash);
      retro_unload_game();
      retro_deinit();
      assert(hash_file(jfd_content_path) == source_hash &&
             hash_file(wp) == wp_source_hash &&
             hash_file(zipped) == zip_source_hash &&
             hash_file(gzip) == gzip_source_hash &&
             open_files == 0u && open_dirs == 0u);
      assert(dlclose(module) == 0);
      puts("jfd_writes_harness: outcome states, persistence, launch records and lifecycle passed");
      return 0;
   }

   if (newio_smoke)
   {
      option_hard_disc = "off"; /* This ROM asserts the legacy absent IDE bus. */
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
      read_cached_video(); /* A3000 -> A3010 keeps the frontend cache. */
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

      /* F media selects A3020 after the whole media list is prepared. */
      struct retro_game_info f_game = {argv[10], NULL, 0u, NULL};
      option_machine = "Auto";
      option_cpu = "Auto";
      option_joystick = "Keyboard";
      option_launch = "off";
      option_hard_disc = "off"; /* This ROM checks for an absent IDE bus. */
      phase = PHASE_LOAD;
      assert(retro_load_game(&f_game));
      assert(strstr(machine_resolution_log,
                    "requested=Auto resolved=A3020 reason=hd-media"));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 60u; ++frame) retro_run();
      option_machine = "A3000";
      variable_update = true;
      retro_run();
      assert(arc_engine_test_newio(NULL, NULL, NULL, NULL, &transition_id) &&
             transition_id == 0x05u);
      option_machine = "Auto";
      variable_update = true;
      retro_run();
      assert(arc_engine_test_newio_read_start(0u, 3u));
      uint32_t f_signature = 0u;
      assert(arc_engine_test_newio_read_finish(1024u, &f_signature,
                                               media_result) &&
             !(media_result[0] & 0xc0u) &&
             f_signature == hash_file_segment(argv[10], 3u * 1024u, 1024u));
      retro_reset();
      assert(arc_engine_test_newio(NULL, NULL, NULL, NULL, &transition_id) &&
             transition_id == 0x05u);
      retro_unload_game();
      option_machine = "A3000";
      phase = PHASE_LOAD;
      assert(!retro_load_game(&f_game) &&
             strstr(last_message, "High-density media requires A3010, A3020 or A4000"));
      option_machine = "Auto";
      phase = PHASE_LOAD;
      assert(retro_load_game(&game));
      assert(strstr(machine_resolution_log,
                    "requested=Auto resolved=A3000 reason=default"));
      assert(disk_control.set_eject_state(true));
      struct retro_game_info hd_replacement = {argv[10], NULL, 0u, NULL};
      char original_path[4096];
      assert(disk_control.get_image_path(0u, original_path,
                                         sizeof(original_path)));
      assert(!disk_control.replace_image_index(0u, &hd_replacement) &&
             strstr(last_message, "hd-media-needs-new-io"));
      char retained_path[4096];
      assert(disk_control.get_image_path(0u, retained_path,
                                         sizeof(retained_path)) &&
             !strcmp(retained_path, original_path));
      assert(disk_control.add_image_index() && disk_control.get_num_images() == 2u);
      assert(!disk_control.replace_image_index(1u, &hd_replacement) &&
             strstr(last_message, "hd-media-needs-new-io") &&
             disk_control.get_num_images() == 1u);
      char refused_source[4096];
      assert(snprintf(refused_source, sizeof(refused_source),
                      "%s/unique-refused-hd.adf", argv[4]) <
             (int)sizeof(refused_source));
      make_sized_file(refused_source, 1638400u);
      ArcDigest refused_digest = digest_file(refused_source);
      char refused_staged[4096];
      assert(snprintf(refused_staged, sizeof(refused_staged),
                      "%s/arcdroid/media/%s/disc.adf", save_dir,
                      refused_digest.sha1) < (int)sizeof(refused_staged));
      assert(access(refused_staged, F_OK) != 0);
      struct retro_game_info refused_game = {refused_source, NULL, 0u, NULL};
      assert(!disk_control.replace_image_index(0u, &refused_game) &&
             strstr(last_message, "hd-media-needs-new-io") &&
             access(refused_staged, F_OK) != 0);
      retro_unload_game();

      char f_directory[4096];
      snprintf(f_directory, sizeof(f_directory), "%s", argv[10]);
      char *separator = strrchr(f_directory, '/');
      assert(separator);
      *separator = '\0';
      static const char *const f_sets[] = {
         "f-boot.m3u", "f-sibling.m3u", "f-boot.zip", "f-sibling.zip",
         "Fset.adf/Fset.adf", "DDset.adf/DDset.adf"
      };
      for (unsigned set = 0u; set < 6u; ++set)
      {
         char set_path[4096];
         assert(snprintf(set_path, sizeof(set_path), "%s/%s",
                         f_directory, f_sets[set]) < (int)sizeof(set_path));
         struct retro_game_info set_game = {set_path, NULL, 0u, NULL};
         phase = PHASE_LOAD;
         bool set_loaded = retro_load_game(&set_game);
         if (!set_loaded)
            fprintf(stderr, "F set %s failed: %s\n", f_sets[set], last_message);
         assert(set_loaded);
         assert(strstr(machine_resolution_log,
                       "requested=Auto resolved=A3020 reason=hd-media") &&
                disk_control.get_num_images() == 2u);
         if (set & 1u)
         {
            assert(disk_control.set_eject_state(true));
            assert(disk_control.set_image_index(1u));
            assert(disk_control.set_eject_state(false));
            assert(arc_engine_test_newio(NULL, NULL, NULL, NULL, &transition_id) &&
                   transition_id == 0x05u);
            assert(disk_control.set_eject_state(true));
            assert(disk_control.replace_image_index(1u, NULL));
            retro_reset();
            assert(arc_engine_test_newio(NULL, NULL, NULL, NULL, &transition_id) &&
                   transition_id == 0x05u);
         }
         retro_unload_game();
      }
      char zip_member_path[4096];
      assert(snprintf(zip_member_path, sizeof(zip_member_path),
                      "%s/f-boot.zip#Disc2.adf", f_directory) <
             (int)sizeof(zip_member_path));
      struct retro_game_info member_game = {zip_member_path, NULL, 0u, NULL};
      phase = PHASE_LOAD;
      assert(retro_load_game(&member_game));
      assert(strstr(machine_resolution_log,
                    "requested=Auto resolved=A3020 reason=hd-media"));
      char boot_path[4096];
      assert(disk_control.get_image_path(0u, boot_path, sizeof(boot_path)) &&
             strstr(boot_path, "Disc1.adf"));
      retro_unload_game();

      static const char *const f_launch[] = {
         "blank-boot-name-f.adf", "valid-f.jfd"
      };
      option_launch = "auto";
      for (unsigned item = 0u; item < 2u; ++item)
      {
         char launch_path[4096];
         assert(snprintf(launch_path, sizeof(launch_path), "%s/%s",
                         f_directory, f_launch[item]) < (int)sizeof(launch_path));
         struct retro_game_info launch_game = {launch_path, NULL, 0u, NULL};
         phase = PHASE_LOAD;
         assert(retro_load_game(&launch_game));
         assert(strstr(last_launch_log, "disc=\"ArcFTest\"") &&
                strstr(last_launch_log, "decision=!Demo") &&
                strstr(last_launch_log, "reason=single application"));
         assert(arc_engine_test_drive_media(1u, NULL, NULL));
         retro_unload_game();
      }
      option_launch = "off";

      option_cpu = "ARM2";
      phase = PHASE_LOAD;
      assert(!retro_load_game(&f_game) &&
             strstr(last_message, "ARM2 is incompatible with the A3020"));
      option_cpu = "Auto";
      option_os = "RISC OS 3.00";
      assert(!retro_load_game(&f_game) &&
             strstr(last_message, "RISC OS 3.00 is not supported on the A3020"));
      option_os = "Auto";
      option_joystick = "A3010";
      assert(!retro_load_game(&f_game) &&
             strstr(last_message, "A3010 joystick interface is incompatible with the A3020"));
      option_joystick = "Keyboard";
      assert(rename(renamed, newio_hidden) == 0);
      assert(!retro_load_game(&f_game) &&
             strstr(last_message, "A3020 requires RISC OS 3.10 or later firmware"));
      assert(rename(newio_hidden, renamed) == 0);

      static const char *const compatibility[] = {
         "riscpc-f.jfd", "arm250-f.jfd", "arm3-f.jfd",
         "riscos2-f.jfd", "riscos31-f.jfd", "legacy-f.jfd",
         "zero-cpu-f.jfd", "zero-os-f.jfd", "write-protected-f.jfd"
      };
      for (unsigned item = 0u; item < 9u; ++item)
      {
         char jfd_path[4096];
         assert(snprintf(jfd_path, sizeof(jfd_path), "%s/%s",
                         f_directory, compatibility[item]) <
                (int)sizeof(jfd_path));
         struct retro_game_info jfd_game = {jfd_path, NULL, 0u, NULL};
         unsigned logs_before = riscpc_log_count;
         unsigned osd_before = riscpc_osd_count;
         phase = PHASE_LOAD;
         assert(retro_load_game(&jfd_game));
         assert(riscpc_log_count == logs_before + (item == 0u) &&
                riscpc_osd_count == osd_before + (item == 0u));
         retro_unload_game();
      }

      retro_deinit();
      assert(hash_file(argv[3]) == source_hash);
      assert(dlclose(module) == 0);
      printf("newio_libretro_harness: A3010/A3020/A4000 executed 82C711 guest "
             "path; inserted disc signature %08x\n", guest_disc_signature);
      return 0;
   }

   if (real_boot)
   {
      if (real_boot_fail_construct)
         arc_libretro_test_fail_next_construct();
      phase = PHASE_LOAD;
      if (!retro_load_game(&game))
      {
         fprintf(stderr, "real_boot_harness: load failed: %s\n", last_message);
         return 2;
      }
      if (real_boot_expect_record_hit)
      {
         assert(arc_libretro_test_boot_record_hit());
         puts("boot record hit verified");
      }
      if (real_boot_seed_product)
      {
         char seeded_id[41];
         assert(arc_libretro_test_seed_hd_product(seeded_id));
      }
      if (real_boot_seed_record)
      {
         ArcBootObservation stamp={0};
         uint8_t record[ARC_BOOT_OBSERVATION_SIZE];
         char path[4096], directory[4096];
         int parent=snprintf(directory,sizeof(directory),"%s/arcdroid",
                             save_path);
         assert(parent>0 && (size_t)parent<sizeof(directory));
         make_directory(directory);
         parent=snprintf(directory,sizeof(directory),"%s/arcdroid/launch",
                         save_path);
         assert(parent>0 && (size_t)parent<sizeof(directory));
         make_directory(directory);
         int length=snprintf(path,sizeof(path),"%s/arcdroid/launch/%s.boot",
                             save_path,arc_libretro_test_content_id());
         assert(length>0 && (size_t)length<sizeof(path) &&
                arc_libretro_test_boot_start_stamp(&stamp) &&
                arc_boot_observation_format(&stamp,record,sizeof(record)));
         write_exact(path,record,sizeof(record));
      }
      if (real_boot_seed_started)
      {
         char seeded_id[41];
         assert(arc_libretro_test_seed_hd_started(real_boot_seed_started,
                                                  real_boot_arm_hd_copy,seeded_id));
      }
      if (real_boot_hd_write_mode >= 0)
         assert(arc_libretro_test_hd_observation_write(
                   (unsigned)real_boot_hd_write_mode));
      if (real_boot_incomplete_fields)
         arc_engine_test_set_vidc_noncompleting(true);
      char firmware_message[sizeof(last_message)];
      snprintf(firmware_message, sizeof(firmware_message), "%s", last_message);
      phase = PHASE_RUN;
      bool script_ok = true;
      bool boot_helper_cmos_verified = false;
      bool boot_chooser_seed_verified = false;
      bool chooser_held_confirm_seen = false;
      if (osk_script)
      {
         OskScriptApi script_api = {
            retro_run, arc_libretro_test_osk_visible,
            arc_libretro_test_osk_focus_key,
            arc_libretro_test_osk_activation_active,
            arc_libretro_test_osk_modifier_state,
            arc_libretro_test_force_present,
            arc_libretro_test_mouse_input_samples
         };
         script_ok = run_osk_script(osk_script, &script_api);
      }
      else
         for (unsigned frame = 0; frame < real_boot_frames; ++frame)
         {
            real_boot_current_frame = frame;
            if (real_boot_cmos_sentinel && frame == 10u)
               assert(arc_engine_test_cmos_write &&
                      arc_engine_test_cmos_write(216u, 0x5au));
            if (real_boot_cmos_ramdisc && frame == 10u)
               assert(arc_engine_test_cmos_write &&
                      arc_engine_test_cmos_write(208u, 0x20u));
            if (real_boot_cmos_write_fail && frame == 20u)
               make_directory(arc_libretro_test_cmos_path());
            if (real_boot_cancel_input)
               joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = frame == 10u;
            if (real_boot_hd_fail_at == (int)frame ||
                (real_boot_hd_fail_after_install &&
                 strstr(last_launch_log,"recovery=install")))
            {
               assert(arc_libretro_test_hd_observation_write(3u));
               real_boot_hd_fail_at=-1;
               real_boot_hd_fail_after_install=false;
            }
            if (real_boot_rearm_after_install &&
                strstr(last_launch_log,"recovery=install")) {
               assert(arc_libretro_test_repeat_hd_outcome());
               real_boot_rearm_after_install=false;
            }
            if (real_boot_cancel_shift && (frame == 10u || frame == 11u))
               keyboard.callback(frame == 10u, RETROK_LSHIFT, 0u, 0u);
            if (real_boot_user_shift &&
                (frame == 5u || frame == (!strcmp(real_boot_user_shift, "early") ? 30u : 90u)))
               keyboard.callback(frame == 5u, RETROK_LSHIFT, 0u, 0u);
            if (real_boot_reset_once && frame == real_boot_reset_frame) retro_reset();
            if (real_boot_ramdisc_toggle_reset && frame == 150u)
            {
               option_launch_ramdisc = !strcmp(option_launch_ramdisc, "Auto") ?
                  "Disabled" : "Auto";
               variable_update = true;
               retro_reset();
            }
            if ((real_boot_chooser_reset || real_boot_chooser_unload) &&
                frame == 150u)
            {
               assert(arc_libretro_test_session_state() == 2u);
               assert(arc_libretro_test_prepared_cmos_byte(216u) == 0x5au);
               boot_chooser_seed_verified = true;
               if (real_boot_chooser_reset)
               {
                  retro_reset();
                  assert(arc_libretro_test_prepared_cmos_byte(216u) == 0x5au);
               }
               else retro_unload_game();
            }
            if (real_boot_chooser_pick || real_boot_chooser_continue)
            {
               unsigned confirm_frame = real_boot_chooser_hold_confirm ? 220u : 120u;
               unsigned moves=real_boot_chooser_continue ? 2u :
                              real_boot_chooser_pick_row;
               joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_DOWN] =
                  frame >= confirm_frame && frame < confirm_frame+2u*moves &&
                  ((frame-confirm_frame)&1u)==0u;
               joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] =
                  real_boot_chooser_hold_confirm ?
                  (frame >= confirm_frame + 2u*moves &&
                   frame < confirm_frame + 24u) :
                  frame == confirm_frame + 2u*moves;
            }
            unsigned constructions_before_run = power_on_construction_lines;
            retro_run();
            if (real_boot_chooser_hold_confirm && real_boot_chooser_pick &&
                power_on_construction_lines > constructions_before_run &&
                joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A])
               chooser_held_confirm_seen = true;
            if (real_boot_expect_disabled)
            {
               ArcBootWatch watch;
               arc_engine_test_boot_watch_snapshot(&watch);
               assert(watch.state == ARC_BOOT_WATCH_DISABLED);
            }
            if (real_boot_cmos_sentinel && arc_engine_test_cmos_image)
            {
               uint8_t image[ARC_CMOS_MACHINE_SIZE];
               if (arc_engine_test_cmos_image(image) &&
                   (image[75] & 7u) == 1u && (image[199] & 7u) == 2u)
               {
                  unsigned checksum = 0u;
                  for (unsigned byte = 0u; byte < 239u; ++byte)
                  {
                     unsigned raw = byte + 0x40u;
                     if (raw > 255u) raw -= 240u;
                     checksum += image[raw];
                  }
                  assert(image[63] == (uint8_t)(checksum + 1u));
                  boot_helper_cmos_verified = true;
               }
            }
         }
      if (boot_helper_cmos_verified)
         puts("boot helper CMOS fields and checksum verified");
      if (real_boot_chooser_hold_confirm && real_boot_chooser_pick)
         assert(chooser_held_confirm_seen);
      if (real_boot_cmos_ramdisc)
      {
         uint8_t image[ARC_CMOS_MACHINE_SIZE];
         assert(arc_engine_test_cmos_image &&
                arc_engine_test_cmos_image(image) && image[208u] == 0x20u);
         puts("reconstructed guest RAM-disc CMOS byte preserved");
      }
      if (boot_chooser_seed_verified)
         printf("boot chooser CMOS seed preserved on %s%s\n",
                real_boot_chooser_reset ? "reset" : "unload",
                real_boot_cmos_write_fail ? " after failed write" : "");
      if (real_boot_expect_disabled)
         puts("boot observer disabled for all frames");
      if (arc_engine_test_boot_watch_snapshot)
      {
         ArcBootWatch watch;
         arc_engine_test_boot_watch_snapshot(&watch);
         printf("boot observer: state=%u armed_us=%llu read_us=%llu event_us=%llu pending=%u\n",
                (unsigned)watch.state, (unsigned long long)watch.armed_us,
                (unsigned long long)watch.read_us,
                (unsigned long long)watch.last_event_us,
                watch.pending ? 1u : 0u);
      }
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
      if (!real_boot_incomplete_fields && !real_boot_package)
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
   assert_input_resolution();
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
   phase = PHASE_LOAD; assert(retro_load_game(&high_game));
   assert(strstr(machine_resolution_log,
                 "requested=Auto resolved=A3020 reason=hd-media"));
   retro_unload_game();

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
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   phase = PHASE_RUN;
   assert(saw_input_descriptors && option_query_count >= 10u);
   assert_input_resolution();
   assert_effective_descriptors();
   const char *stale_answers[] = {"eli" "te", "Generic", "Auto", "garbage"};
   unsigned input_lines_before_stale = input_resolution_log_calls;
   unsigned notices_before_stale = controls_osd_calls;
   unsigned all_notices_before_stale = total_osd_calls;
   for (unsigned answer = 0u; answer < 4u; ++answer)
   {
      stale_input_option_value = stale_answers[answer];
      variable_update = true;
      retro_run();
      assert(input_resolution_log_calls == input_lines_before_stale &&
             controls_osd_calls == notices_before_stale &&
             total_osd_calls == all_notices_before_stale &&
             stale_input_option_queries == 0u);
      assert_input_resolution();
      assert_effective_descriptors();
   }
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
      argv[3], false, ARC_MEDIA_ADF_800K, ARC_GEOMETRY_800K_E, NULL,
      NULL, 0u, ARC_MEDIA_EXTENT_UNRESTRICTED
   };
   ArcEngineMedia invalid_extent = direct_media;
   invalid_extent.effective_extent = 513u;
   assert(!arc_engine_media_insert(&invalid_extent, insert_error, sizeof(insert_error)));
   invalid_extent.effective_extent = 819200u + 1024u;
   assert(!arc_engine_media_insert(&invalid_extent, insert_error, sizeof(insert_error)));
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
   ASSERT_NO_SAVE_RAM();
   unsigned preserved_count = disk_control.get_num_images();
   unsigned preserved_index = disk_control.get_image_index();
   bool preserved_ejected = disk_control.get_eject_state();
   bool preserved_write_protected = false;
   char preserved_media_digest[41];
   char preserved_cmos_path[4096];
   uint8_t preserved_cmos_identity[16];
   uint8_t preserved_cmos_record[ARC_CMOS_RECORD_SIZE];
   assert(arc_engine_test_drive_media(0u, &preserved_write_protected,
                                      preserved_media_digest));
   snprintf(preserved_cmos_path, sizeof(preserved_cmos_path), "%s",
            arc_libretro_test_cmos_path());
   read_exact(preserved_cmos_path, preserved_cmos_record,
              sizeof(preserved_cmos_record));
   memcpy(preserved_cmos_identity, preserved_cmos_record,
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
   read_exact(preserved_cmos_path, preserved_cmos_record,
              sizeof(preserved_cmos_record));
   assert(video_frames > video_before_rejected_reset &&
          audio_frames > audio_before_rejected_reset &&
          disk_control.get_num_images() == preserved_count &&
          disk_control.get_image_index() == preserved_index &&
          disk_control.get_eject_state() == preserved_ejected &&
          arc_engine_test_drive_media(0u, &current_write_protected,
                                      current_media_digest) &&
          current_write_protected == preserved_write_protected &&
          !strcmp(current_media_digest, preserved_media_digest) &&
          !memcmp(preserved_cmos_record,
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
   uint8_t lifecycle_mutation = 0u;
   for (unsigned cycle = 0; cycle < 100u; ++cycle)
   {
      struct retro_game_info playlist_cycle = {media_tests ? argv[6] : argv[3], NULL, 0u, NULL};
      struct retro_game_info jfd_cycle = {media_tests ? argv[10] : argv[3], NULL, 0u, NULL};
      const struct retro_game_info *cycle_game = !media_tests || cycle % 3u == 0u ? &game :
         (cycle % 3u == 1u ? &playlist_cycle : &jfd_cycle);
      phase = PHASE_LOAD; assert(retro_load_game(cycle_game)); assert(keyboard.callback);
      ASSERT_NO_SAVE_RAM();
      phase = PHASE_RUN;
      option_osk_toggle = cycle & 1u ? "R3" : "L3";
      variable_update = true;
      retro_run();
      unsigned lifecycle_toggle = cycle & 1u ? RETRO_DEVICE_ID_JOYPAD_R3 :
                                                RETRO_DEVICE_ID_JOYPAD_L3;
      joypad_buttons[0][lifecycle_toggle] = true; retro_run();
      joypad_buttons[0][lifecycle_toggle] = false; retro_run();
      char cycle_cmos_file[4096];
      snprintf(cycle_cmos_file, sizeof(cycle_cmos_file), "%s",
               arc_libretro_test_cmos_path());
      if (cycle == 0u)
      {
         uint8_t previous_record[256];
         uint8_t current_record[256];
         read_exact(cycle_cmos_file, previous_record, sizeof(previous_record));
         unsigned record_changes = 0u;
         unsigned writes_before_frames = cmos_write_opens;
         lifecycle_mutation = (uint8_t)(previous_record[216u] ^ 0xffu);
         assert(arc_engine_test_cmos_write(216u, lifecycle_mutation));
         retro_run();
         read_exact(cycle_cmos_file, current_record, sizeof(current_record));
         assert(!memcmp(current_record, "ARCCMOS2", 8u) &&
                current_record[216u] == lifecycle_mutation);
         if (memcmp(previous_record, current_record, sizeof(previous_record)))
         {
            ++record_changes;
            memcpy(previous_record, current_record, sizeof(previous_record));
         }
         for (unsigned frame = 1u; frame < 100u; ++frame)
         {
            retro_run();
            read_exact(cycle_cmos_file, current_record, sizeof(current_record));
            if (memcmp(previous_record, current_record, sizeof(previous_record)))
            {
               ++record_changes;
               memcpy(previous_record, current_record, sizeof(previous_record));
            }
         }
         assert(cmos_write_opens - writes_before_frames <= record_changes);
      }
      else
      {
         uint8_t reloaded_cmos[ARC_CMOS_RECORD_SIZE];
         read_exact(cycle_cmos_file, reloaded_cmos, sizeof(reloaded_cmos));
         if (cycle % 3u == 0u)
         {
            assert(!memcmp(saved_cmos, reloaded_cmos, sizeof(saved_cmos)));
            uint8_t reloaded_machine_cmos[ARC_CMOS_MACHINE_SIZE];
            assert(arc_engine_test_cmos_image(reloaded_machine_cmos) &&
                   reloaded_machine_cmos[216u] == lifecycle_mutation);
         }
         else
            assert(!memcmp(reloaded_cmos, "ARCCMOS2", 8u));
         for (unsigned frame = 0; frame < 100u; ++frame) retro_run();
      }
      if (cycle == 0u)
      {
         /* Sample the live queue before reset clears it. One 50 ms engine
          * delivery window bounds startup priming; steady state must not overrun. */
         arc_engine_audio_stats(&sampled_underruns, &sampled_overruns);
         assert(sampled_underruns <= 2400u && sampled_overruns == 0u);
      }
      retro_reset(); read_cached_video();
      retro_unload_game(); read_cached_video();
      if (cycle == 0u)
      {
         read_exact(cycle_cmos_file, saved_cmos, sizeof(saved_cmos));
         assert(!memcmp(saved_cmos, "ARCCMOS2", 8u) &&
                saved_cmos[216u] == lifecycle_mutation);
      }
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
   /* A valid old record keeps its identity and unrelated settings while the
    * locked ROM-module byte heals before the test OS can execute. */
   final_cmos[0xedu] = 0x5au;
   write_exact(cmos_file, final_cmos, sizeof(final_cmos));
   unsigned heal_before = cmos_healing_log_count;
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   uint8_t healed_image[ARC_CMOS_MACHINE_SIZE];
   assert(arc_engine_test_cmos_image(healed_image) && healed_image[0xedu] == 0u);
   assert(cmos_healing_log_count == heal_before + 1u);
   assert(arc_engine_test_cmos_write(0xedu, 0xa5u));
   phase = PHASE_RUN; retro_run();
   uint8_t healed_record[ARC_CMOS_RECORD_SIZE];
   read_exact(cmos_file, healed_record, sizeof(healed_record));
   assert(healed_record[0xedu] == 0u && healed_record[216u] == lifecycle_mutation);
   assert(!memcmp(healed_record, final_cmos, 16u));
   retro_reset();
   assert(arc_engine_test_cmos_image(healed_image) && healed_image[0xedu] == 0u);
   assert(cmos_healing_log_count == heal_before + 1u);
   retro_unload_game();
   phase = PHASE_LOAD; assert(retro_load_game(&game));
   assert(arc_engine_test_cmos_image(healed_image) && healed_image[0xedu] == 0u);
   assert(cmos_healing_log_count == heal_before + 1u);
   retro_unload_game();
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
      option_writes = "shadow";
      option_launch = "off";
      struct retro_game_info playlist_member_game = {argv[7], NULL, 0u, NULL};
      last_input_resolution[0] = '\0';
      phase = PHASE_LOAD; assert(retro_load_game(&playlist_member_game));
      ASSERT_NO_SAVE_RAM();
      assert_input_resolution();
      ArcDigest standalone_identity = digest_file(argv[7]);
      assert(!strcmp(arc_libretro_test_content_id(), standalone_identity.sha1) &&
             strcmp(standalone_identity.sha1, playlist_identity_sha1));
      char standalone_staged[4096], standalone_cmos[4096], standalone_choice[4096];
      snprintf(standalone_staged, sizeof(standalone_staged), "%s",
               arc_libretro_test_staged_path(0u));
      snprintf(standalone_cmos, sizeof(standalone_cmos), "%s",
               arc_libretro_test_cmos_path());
      snprintf(standalone_choice, sizeof(standalone_choice), "%s",
               arc_libretro_test_choice_path());
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      uint64_t standalone_signature = last_video_signature;
      assert(arc_engine_test_cmos_write(16u + 42u, 0x5au));
      retro_run();
      uint8_t standalone_cmos_record[ARC_CMOS_RECORD_SIZE];
      read_exact(standalone_cmos, standalone_cmos_record,
                 sizeof(standalone_cmos_record));
      assert(!memcmp(standalone_cmos_record, "ARCCMOS2", 8u) &&
             standalone_cmos_record[16u + 42u] == 0x5au);
      retro_unload_game();

      ArcDigest previous_natural_identity = digest_file(argv[8]);
      char previous_natural_shadow[4096], previous_natural_cmos[4096];
      snprintf(previous_natural_shadow, sizeof(previous_natural_shadow),
               "%s/arcdroid/media/%s/disc.adf", save_path,
               previous_natural_identity.sha1);
      const char *standalone_id_at = strstr(standalone_cmos,
                                            standalone_identity.sha1);
      assert(standalone_id_at);
      size_t cmos_prefix = (size_t)(standalone_id_at - standalone_cmos);
      int previous_cmos_length = snprintf(
         previous_natural_cmos, sizeof(previous_natural_cmos), "%.*s%s%s",
         (int)cmos_prefix, standalone_cmos, previous_natural_identity.sha1,
         standalone_id_at + strlen(standalone_identity.sha1));
      assert(previous_cmos_length > 0 &&
             (size_t)previous_cmos_length < sizeof(previous_natural_cmos));
      uint8_t previous_natural_record[ARC_CMOS_RECORD_SIZE];
      memcpy(previous_natural_record, standalone_cmos_record,
             sizeof(previous_natural_record));
      previous_natural_record[16u + 43u] = 0x6bu;
      write_exact(previous_natural_cmos, previous_natural_record,
                  sizeof(previous_natural_record));
      uint64_t previous_natural_cmos_hash = hash_file(previous_natural_cmos);

      char zip_single[4096], zip_multi[4096], zip_natural[4096];
      char zip_partial[4096], zip_partial_multiple[4096];
      char zip_only_corrupt[4096], zip_no_media[4096], zip_too_many[4096];
      char zip_unsupported[4096], zip_playlist[4096], zip_member_playlist[4096];
      char zip_single_jfd[4096], zip_two_jfd[4096], zip_replaced[4096];
      char zip_too_large_jfd[4096], zip_decoded_too_large_jfd[4096];
      char zip_second_replaced[4096], zip_reset_multi[4096];
      snprintf(zip_single, sizeof(zip_single), "%s/single.zip", argv[15]);
      snprintf(zip_multi, sizeof(zip_multi), "%s/multi.zip", argv[15]);
      snprintf(zip_natural, sizeof(zip_natural), "%s/natural.zip", argv[15]);
      snprintf(zip_partial, sizeof(zip_partial), "%s/partial-first.zip", argv[15]);
      snprintf(zip_partial_multiple, sizeof(zip_partial_multiple),
               "%s/partial-multiple.zip", argv[15]);
      snprintf(zip_only_corrupt, sizeof(zip_only_corrupt), "%s/only-corrupt.zip", argv[15]);
      snprintf(zip_no_media, sizeof(zip_no_media), "%s/no-media.zip", argv[15]);
      snprintf(zip_too_many, sizeof(zip_too_many), "%s/too-many.zip", argv[15]);
      snprintf(zip_unsupported, sizeof(zip_unsupported), "%s/unsupported.7z", argv[15]);
      snprintf(zip_playlist, sizeof(zip_playlist), "%s/zip-entry.m3u", argv[15]);
      snprintf(zip_member_playlist, sizeof(zip_member_playlist),
               "%s/zip-member-entry.m3u", argv[15]);
      snprintf(zip_single_jfd, sizeof(zip_single_jfd), "%s/single-jfd.zip", argv[15]);
      snprintf(zip_two_jfd, sizeof(zip_two_jfd), "%s/two-jfd.zip", argv[15]);
      snprintf(zip_too_large_jfd, sizeof(zip_too_large_jfd),
               "%s/too-large-jfd.zip", argv[15]);
      snprintf(zip_decoded_too_large_jfd, sizeof(zip_decoded_too_large_jfd),
               "%s/decoded-too-large-jfd.zip", argv[15]);
      snprintf(zip_replaced, sizeof(zip_replaced), "%s/multi-replaced.zip", argv[15]);
      snprintf(zip_second_replaced, sizeof(zip_second_replaced),
               "%s/multi-second-replaced.zip", argv[15]);
      snprintf(zip_reset_multi, sizeof(zip_reset_multi), "%s/reset-multi.zip",
               argv[15]);

      struct retro_game_info zip_game = {zip_single, NULL, 0u, NULL};
      unsigned zip_summaries = zip_summary_log_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      ASSERT_NO_SAVE_RAM();
      assert(zip_summary_log_lines == zip_summaries + 1u &&
             strstr(last_zip_summary, "entries=1 images=1 prepared=1 omitted=0") &&
             strstr(last_zip_summary,
                    "rule=disc1 candidates=1 preferred=\"Disc1.adf\" boot=\"Disc1.adf\" fallback=no") &&
             !strcmp(arc_libretro_test_content_id(), standalone_identity.sha1) &&
             !strcmp(arc_libretro_test_staged_path(0u), standalone_staged) &&
             !strcmp(arc_libretro_test_cmos_path(), standalone_cmos) &&
             !strcmp(arc_libretro_test_choice_path(), standalone_choice));
      char expected_zip_path[4096];
      snprintf(expected_zip_path, sizeof(expected_zip_path), "%s#Disc1.adf", zip_single);
      assert(disk_control.get_num_images() == 1u &&
             disk_control.get_image_path(0u, image_path, sizeof(image_path)) &&
             !strcmp(image_path, expected_zip_path) &&
             disk_control.get_image_label(0u, image_label, sizeof(image_label)) &&
             !strcmp(image_label, "Disc1"));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      assert(last_video_signature == standalone_signature);
      retro_unload_game();

      char selected_path[4096];
      snprintf(selected_path, sizeof(selected_path), "%s#Disc1.adf", zip_single);
      struct retro_game_info selected_game = {selected_path, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&selected_game));
      assert(!strcmp(arc_libretro_test_content_id(), standalone_identity.sha1) &&
             strstr(last_zip_summary, "selected=\"Disc1.adf\""));
      retro_unload_game();

      static const char *const selected_members[] = {
         "multi.adf", "multi (Disk 2).adf"
      };
      for (unsigned member_index = 0u; member_index < 2u; ++member_index)
      {
         snprintf(selected_path, sizeof(selected_path), "%s#%s", zip_multi,
                  selected_members[member_index]);
         selected_game.path = selected_path;
         phase = PHASE_LOAD; assert(retro_load_game(&selected_game));
         assert(!strcmp(arc_libretro_test_content_id(), standalone_identity.sha1) &&
                disk_control.get_num_images() == 2u &&
                strstr(last_zip_summary, selected_members[member_index]));
         for (unsigned image = 0u; image < 2u; ++image)
         {
            snprintf(expected_zip_path, sizeof(expected_zip_path), "%s#%s",
                     zip_multi, selected_members[image]);
            assert(disk_control.get_image_path(image, image_path,
                                               sizeof(image_path)) &&
                   !strcmp(image_path, expected_zip_path));
         }
         retro_unload_game();
      }

      snprintf(selected_path, sizeof(selected_path), "%s#readme.txt", zip_no_media);
      selected_game.path = selected_path;
      phase = PHASE_LOAD; assert(!retro_load_game(&selected_game));
      assert(strstr(last_message, "readme.txt is not a supported disc image in no-media.zip"));
      snprintf(selected_path, sizeof(selected_path), "%s#Absent.adf", zip_single);
      selected_game.path = selected_path;
      phase = PHASE_LOAD; assert(!retro_load_game(&selected_game));
      assert(strstr(last_message, "Absent.adf is not a supported disc image in single.zip"));
      snprintf(selected_path, sizeof(selected_path), "%s#", zip_single);
      selected_game.path = selected_path;
      phase = PHASE_LOAD; assert(!retro_load_game(&selected_game));
      assert(!strcmp(last_message,
                     "Empty zip member is not a supported disc image in single.zip"));
      snprintf(selected_path, sizeof(selected_path), "%s#y.adf", zip_unsupported);
      selected_game.path = selected_path;
      phase = PHASE_LOAD; assert(!retro_load_game(&selected_game));
      assert(!strcmp(last_message,
                     "Unsupported archive container; ArcDroid supports .zip files only."));

      zip_game.path = zip_multi;
      zip_summaries = zip_summary_log_lines;
      unsigned zip_orders = zip_order_log_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      ASSERT_NO_SAVE_RAM();
      uint8_t zip_cmos_record[ARC_CMOS_RECORD_SIZE];
      uint8_t zip_machine_cmos[ARC_CMOS_MACHINE_SIZE];
      read_exact(standalone_cmos, zip_cmos_record, sizeof(zip_cmos_record));
      assert(zip_summary_log_lines == zip_summaries + 1u &&
             zip_order_log_lines == zip_orders + 1u &&
             strstr(last_zip_summary,
                    "rule=name candidates=1 preferred=\"multi.adf\" boot=\"multi.adf\" fallback=no") &&
             !strcmp(last_zip_order,
                     "Zip content order: 1=\"multi.adf\" 2=\"multi (Disk 2).adf\"\n") &&
             !strcmp(arc_libretro_test_content_id(), standalone_identity.sha1) &&
             !strcmp(arc_libretro_test_cmos_path(), standalone_cmos) &&
             !memcmp(zip_cmos_record, "ARCCMOS2", 8u) &&
             zip_cmos_record[16u + 42u] == 0x5au &&
             arc_engine_test_cmos_image(zip_machine_cmos) &&
             zip_machine_cmos[16u + 42u] == 0x5au &&
             !strcmp(arc_libretro_test_staged_path(0u), standalone_staged) &&
             !strcmp(arc_libretro_test_staged_path(1u), previous_natural_shadow) &&
             disk_control.get_num_images() == 2u &&
             disk_control.get_image_path(0u, image_path, sizeof(image_path)) &&
             strstr(image_path, "multi.zip#multi.adf") &&
             disk_control.get_image_label(0u, image_label, sizeof(image_label)) &&
             !strcmp(image_label, "multi") &&
             disk_control.get_image_path(1u, image_path, sizeof(image_path)) &&
             strstr(image_path, "multi.zip#multi (Disk 2).adf") &&
             disk_control.get_image_label(1u, image_label, sizeof(image_label)) &&
             !strcmp(image_label, "multi (Disk 2)"));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      uint64_t zip_disc1_signature = last_video_signature;
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_image_index(1u) &&
             disk_control.set_eject_state(false));
      char zip_mounted_digest[41];
      ArcDigest disc2_digest = digest_file(argv[8]);
      assert(arc_engine_test_drive_media(0u, NULL, zip_mounted_digest) &&
             !strcmp(zip_mounted_digest, disc2_digest.sha1));
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      assert(last_video_signature != zip_disc1_signature);
      assert(disk_control.set_eject_state(true));
      struct retro_game_info zip_replacement = {zip_single, NULL, 0u, NULL};
      assert(!disk_control.replace_image_index(0u, &zip_replacement) &&
             disk_control.get_num_images() == 2u);
      assert(disk_control.add_image_index() &&
             !disk_control.replace_image_index(2u, &zip_replacement) &&
             disk_control.get_num_images() == 2u &&
             disk_control.set_image_index(0u) && disk_control.set_eject_state(false));
      retro_unload_game();
      assert(hash_file(previous_natural_cmos) == previous_natural_cmos_hash);

      zip_game.path = zip_natural;
      zip_summaries = zip_summary_log_lines;
      zip_orders = zip_order_log_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(zip_summary_log_lines == zip_summaries + 1u &&
             zip_order_log_lines == zip_orders + 1u &&
             strstr(last_zip_summary,
                    "rule=order candidates=2 preferred=\"Alpha.adf\" boot=\"Alpha.adf\" fallback=no") &&
             !strcmp(last_zip_order,
                     "Zip content order: 1=\"Alpha.adf\" 2=\"Beta.adf\"\n") &&
             !strcmp(arc_libretro_test_content_id(), standalone_identity.sha1) &&
             disk_control.get_num_images() == 2u &&
             disk_control.get_image_path(0u, image_path, sizeof(image_path)) &&
             strstr(image_path, "natural.zip#Alpha.adf") &&
             disk_control.get_image_label(0u, image_label, sizeof(image_label)) &&
             !strcmp(image_label, "Alpha") &&
             disk_control.get_image_path(1u, image_path, sizeof(image_path)) &&
             strstr(image_path, "natural.zip#Beta.adf") &&
             disk_control.get_image_label(1u, image_label, sizeof(image_label)) &&
             !strcmp(image_label, "Beta"));
      retro_unload_game();

      /* R1-2: zip-reset-selected-plain-slot. A plain disk-control append has
       * no zip_member string, so reset compaction must map its original slot
       * by index instead of matching the first empty string. */
      option_writes = "off";
      variable_update = true;
      zip_game.path = zip_multi;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(disk_control.set_eject_state(true) &&
             disk_control.add_image_index());
      struct retro_game_info zip_plain_append = {argv[7], NULL, 0u, NULL};
      assert(disk_control.replace_image_index(2u, &zip_plain_append) &&
             disk_control.set_image_index(2u));
      option_writes = "shadow";
      variable_update = true;
      retro_reset();
      assert(disk_control.get_num_images() == 3u &&
             disk_control.get_image_index() == 2u &&
             disk_control.get_image_path(0u, image_path, sizeof(image_path)) &&
             strstr(image_path, "multi.zip#multi.adf") &&
             disk_control.get_image_path(1u, image_path, sizeof(image_path)) &&
             strstr(image_path, "multi.zip#multi (Disk 2).adf") &&
             disk_control.get_image_path(2u, image_path, sizeof(image_path)) &&
             !strcmp(image_path, argv[7]));
      retro_unload_game();
      option_writes = "off";
      variable_update = true;

      unsigned omission_before = zip_omission_log_lines;
      unsigned fallback_messages = zip_fallback_osd_calls;
      unsigned count_messages = zip_summary_osd_calls;
      zip_orders = zip_order_log_lines;
      zip_game.path = zip_partial;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(disk_control.get_num_images() == 1u);
      assert(zip_omission_log_lines == omission_before + 1u);
      assert(zip_order_log_lines == zip_orders + 1u);
      assert(strstr(last_zip_omission, "member=Game \" (Disk 1).adf"));
      assert(strstr(last_zip_summary,
                    "prepared=1 omitted=1 rule=disc1 candidates=1 preferred=\"Game \\\" (Disk 1).adf\" boot=\"Disc2.adf\" fallback=yes"));
      assert(!strcmp(last_zip_order,
                     "Zip content order: 1=\"Disc2.adf\"\n"));
      assert(zip_fallback_osd_calls == fallback_messages + 1u);
      assert(zip_summary_osd_calls == count_messages);
      assert(!strcmp(last_zip_fallback,
                     "Zip preferred image \"Game \\\" (Disk 1).adf\" could not be used; started \"Disc2.adf\" instead (0 other images omitted, 1 media notice)."));
      assert(!strcmp(arc_libretro_test_content_id(),
                     previous_natural_identity.sha1));
      retro_unload_game();

      omission_before = zip_omission_log_lines;
      fallback_messages = zip_fallback_osd_calls;
      count_messages = zip_summary_osd_calls;
      zip_orders = zip_order_log_lines;
      zip_game.path = zip_partial_multiple;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(disk_control.get_num_images() == 1u &&
             zip_omission_log_lines == omission_before + 2u &&
             zip_order_log_lines == zip_orders + 1u &&
             strstr(last_zip_summary,
                    "prepared=1 omitted=2 rule=disc1 candidates=1 preferred=\"Game (Disk 1).adf\" boot=\"Survivor.adf\" fallback=yes") &&
             !strcmp(last_zip_order,
                     "Zip content order: 1=\"Survivor.adf\"\n") &&
             zip_fallback_osd_calls == fallback_messages + 1u &&
             zip_summary_osd_calls == count_messages &&
             !strcmp(last_zip_fallback,
                     "Zip preferred image \"Game (Disk 1).adf\" could not be used; started \"Survivor.adf\" instead (1 other image omitted, 1 media notice).") &&
             !strcmp(arc_libretro_test_content_id(),
                     previous_natural_identity.sha1));
      retro_unload_game();

      zip_summaries = zip_summary_log_lines;
      zip_orders = zip_order_log_lines;
      zip_game.path = zip_only_corrupt;
      phase = PHASE_LOAD; assert(!retro_load_game(&zip_game));
      assert(strstr(last_message, "No usable disc image in only-corrupt.zip") &&
             zip_summary_log_lines == zip_summaries + 1u &&
             zip_order_log_lines == zip_orders &&
             strstr(last_zip_summary,
                    "prepared=0 omitted=1 rule=disc1 candidates=1 preferred=\"Disc1.adf\" boot=none fallback=yes"));
      zip_orders = zip_order_log_lines;
      zip_game.path = zip_no_media;
      phase = PHASE_LOAD; assert(!retro_load_game(&zip_game));
      assert(!strcmp(last_message,
                     "No supported disc image (adf, adl, jfd) in no-media.zip") &&
             zip_order_log_lines == zip_orders);
      zip_orders = zip_order_log_lines;
      zip_game.path = zip_too_many;
      phase = PHASE_LOAD; assert(!retro_load_game(&zip_game));
      assert(!strcmp(last_message,
                     "Zip too-many.zip contains more than 32 supported disc images") &&
             zip_order_log_lines == zip_orders);
      zip_game.path = zip_unsupported;
      phase = PHASE_LOAD; assert(!retro_load_game(&zip_game));
      assert(!strcmp(last_message,
                     "Unsupported archive container; ArcDroid supports .zip files only."));
      struct retro_game_info zip_playlist_game = {zip_playlist, NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(!retro_load_game(&zip_playlist_game));
      assert(strstr(last_message, "zip entries are unsupported in playlists"));
      zip_playlist_game.path = zip_member_playlist;
      phase = PHASE_LOAD; assert(!retro_load_game(&zip_playlist_game));
      assert(strstr(last_message, "zip entries are unsupported in playlists"));
      arc_zip_test_fail_member_allocations(1u);
      zip_game.path = zip_single;
      phase = PHASE_LOAD; assert(!retro_load_game(&zip_game));
      assert(strstr(last_message, "No usable disc image in single.zip") &&
             open_files == 0u && open_dirs == 0u);

      uint64_t zip_hash = hash_file(zip_single);
      option_writes = "off";
      zip_game.path = zip_single;
      observed_vfs_path = zip_single;
      observed_vfs_read_opens = observed_vfs_write_opens = observed_vfs_reads = 0u;
      variable_update = true;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(observed_vfs_read_opens == 1u && observed_vfs_write_opens == 0u);
      option_writes = "original"; variable_update = true; retro_reset();
      assert(arc_libretro_test_session_state() != 0u &&
             strcmp(arc_libretro_test_staged_path(0u), zip_single));
      char shadow_path[4096];
      snprintf(shadow_path, sizeof(shadow_path), "%s",
               arc_libretro_test_staged_path(0u));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      uint64_t shadow_hash = hash_file(shadow_path);
      option_writes = "off"; variable_update = true; retro_reset();
      assert(arc_libretro_test_session_state() != 0u &&
             hash_file(shadow_path) == shadow_hash);
      retro_unload_game();
      assert(hash_file(zip_single) == zip_hash && observed_vfs_write_opens == 0u);
      observed_vfs_path = NULL;

      char mutable_zip[4096];
      snprintf(mutable_zip, sizeof(mutable_zip), "%s/reset-mutable.zip", argv[4]);
      copy_file(zip_reset_multi, mutable_zip);
      zip_game.path = mutable_zip;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      copy_file(zip_second_replaced, mutable_zip);
      option_writes = "shadow"; variable_update = true; retro_reset();
      assert(arc_libretro_test_session_state() != 0u &&
             disk_control.get_num_images() == 1u &&
             strstr(last_zip_omission, "member=Disc2.adf") &&
             strstr(last_zip_omission, "phase=reset"));
      retro_unload_game();
      copy_file(zip_reset_multi, mutable_zip);
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      copy_file(zip_replaced, mutable_zip);
      option_writes = "off"; variable_update = true; retro_reset();
      assert(strstr(last_message, "Zip reset verification failed for Disc1.adf") &&
             arc_libretro_test_session_state() == 0u && open_files == 0u);
      copy_file(zip_reset_multi, mutable_zip);
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(remove(mutable_zip) == 0);
      option_writes = "shadow"; variable_update = true; retro_reset();
      assert(strstr(last_message, "Unable to re-open the zip during reset") &&
             arc_libretro_test_session_state() == 0u && open_files == 0u);

      option_writes = "off";
      option_jfd_boot = "auto";
      option_launch = "auto";
      struct retro_game_info direct_adffs_game = {argv[14], NULL, 0u, NULL};
      phase = PHASE_LOAD; assert(retro_load_game(&direct_adffs_game));
      ASSERT_NO_SAVE_RAM();
      char direct_jfd_identity[41];
      snprintf(direct_jfd_identity, sizeof(direct_jfd_identity), "%s",
               arc_libretro_test_content_id());
      assert(strstr(last_jfd_boot_log, "JFD boot: path=adffs"));
      retro_unload_game();
      size_t eligible_size = 0u;
      uint8_t *eligible_bytes = read_alloc(argv[14], &eligible_size);

      /* R2-1a: zip-adffs-plain-replacement-stale-raw. A plain slot replacing
       * a native-fallback zip member must use the real file, never the
       * retained bytes associated with Game.jfd. */
      observed_vfs_hash_opens = 0u;
      zip_game.path = zip_decoded_too_large_jfd;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=decoded-too-large\n") &&
             disk_control.set_eject_state(true) &&
             disk_control.replace_image_index(0u, &direct_adffs_game) &&
             disk_control.set_eject_state(false));
      retro_reset();
      assert(strstr(last_jfd_boot_log, "JFD boot: path=adffs") &&
             arc_libretro_test_adffs_carrier_matches(eligible_bytes,
                                                     eligible_size) &&
             disk_control.get_image_path(0u, image_path, sizeof(image_path)) &&
             !strcmp(image_path, argv[14]) &&
             observed_vfs_hash_opens == 0u);
      retro_unload_game();

      /* R2-1b: zip-adffs-plain-replacement-too-large. The pending read fault
       * proves the plain-file size gate exits before attempting the read. */
      char plain_too_large_jfd[4096];
      snprintf(plain_too_large_jfd, sizeof(plain_too_large_jfd),
               "%s/adffs/too-large.jfd", argv[11]);
      struct retro_game_info too_large_replacement = {
         plain_too_large_jfd, NULL, 0u, NULL
      };
      observed_vfs_hash_opens = 0u;
      zip_game.path = zip_decoded_too_large_jfd;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(disk_control.set_eject_state(true) &&
             disk_control.replace_image_index(0u, &too_large_replacement) &&
             disk_control.set_eject_state(false));
      arc_libretro_test_fail_adffs_content_read(true);
      retro_reset();
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=too-large\n") &&
             arc_libretro_test_adffs_content_read_failure_pending() &&
             observed_vfs_hash_opens == 0u);
      arc_libretro_test_fail_adffs_content_read(false);
      retro_unload_game();

      /* R2-1c: zip-adffs-plain-replacement-reprepare. Native mode permits
       * replacing an otherwise eligible single-JFD zip. Both an ordinary
       * reset and zip re-preparation after a writes change use the plain file. */
      option_jfd_boot = "native";
      variable_update = true;
      observed_vfs_hash_opens = 0u;
      zip_game.path = zip_single_jfd;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=option\n") &&
             disk_control.set_eject_state(true) &&
             disk_control.replace_image_index(0u, &direct_adffs_game) &&
             disk_control.set_eject_state(false));
      option_jfd_boot = "auto";
      variable_update = true;
      retro_reset();
      assert(strstr(last_jfd_boot_log, "JFD boot: path=adffs") &&
             arc_libretro_test_adffs_carrier_matches(eligible_bytes,
                                                     eligible_size));
      option_writes = "shadow";
      variable_update = true;
      retro_reset();
      assert(strstr(last_jfd_boot_log, "JFD boot: path=adffs") &&
             arc_libretro_test_adffs_carrier_matches(eligible_bytes,
                                                     eligible_size) &&
             disk_control.get_image_path(0u, image_path, sizeof(image_path)) &&
             !strcmp(image_path, argv[14]) &&
             observed_vfs_hash_opens == 0u);
      retro_unload_game();
      option_writes = "off";
      variable_update = true;

      zip_game.path = zip_single_jfd;
      observed_vfs_path = zip_single_jfd;
      observed_vfs_read_opens = observed_vfs_write_opens = observed_vfs_reads = 0u;
      observed_vfs_hash_opens = 0u;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      unsigned reads_after_load = observed_vfs_reads;
      assert(strstr(last_jfd_boot_log, "JFD boot: path=adffs") &&
             !strcmp(arc_libretro_test_content_id(), direct_jfd_identity) &&
             arc_libretro_test_adffs_carrier_matches(eligible_bytes, eligible_size) &&
             disk_control.get_num_images() == 1u &&
             !disk_control.set_eject_state(true) &&
             !disk_control.add_image_index() &&
             !disk_control.replace_image_index(0u, &zip_replacement));
      /* R1-1a: zip-adffs-retained-three-resets. */
      for (unsigned reset = 0u; reset < 3u; ++reset)
      {
         unsigned boot_lines = jfd_boot_log_lines;
         retro_reset();
         assert(jfd_boot_log_lines == boot_lines + 1u &&
                strstr(last_jfd_boot_log, "JFD boot: path=adffs") &&
                arc_libretro_test_adffs_carrier_matches(eligible_bytes,
                                                        eligible_size));
      }
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
      assert(observed_vfs_reads == reads_after_load && observed_vfs_write_opens == 0u);
      assert(observed_vfs_hash_opens == 0u);
      retro_unload_game();
      observed_vfs_path = NULL;

      /* R1-1b: zip-adffs-hostile-pseudo-path. The literal sibling is valid
       * host content, making any accidental pseudo-path open observable. */
      char hostile_zip[4096], hostile_pseudo[4096];
      snprintf(hostile_zip, sizeof(hostile_zip), "%s/hostile-single-jfd.zip",
               argv[4]);
      snprintf(hostile_pseudo, sizeof(hostile_pseudo), "%s#Game.jfd",
               hostile_zip);
      copy_file(zip_single_jfd, hostile_zip);
      static const char hostile_bytes[] = "Invented hostile sibling!";
      assert(sizeof(hostile_bytes) - 1u == 25u);
      write_exact(hostile_pseudo, hostile_bytes, sizeof(hostile_bytes) - 1u);
      zip_game.path = hostile_zip;
      observed_vfs_path = hostile_pseudo;
      observed_vfs_read_opens = observed_vfs_write_opens = observed_vfs_reads = 0u;
      observed_vfs_hash_opens = 0u;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(strstr(last_jfd_boot_log, "JFD boot: path=adffs") &&
             arc_libretro_test_adffs_carrier_matches(eligible_bytes, eligible_size));
      for (unsigned reset = 0u; reset < 2u; ++reset)
      {
         retro_reset();
         assert(strstr(last_jfd_boot_log, "JFD boot: path=adffs") &&
                arc_libretro_test_adffs_carrier_matches(eligible_bytes,
                                                        eligible_size));
      }
      assert(observed_vfs_read_opens == 0u &&
             observed_vfs_write_opens == 0u &&
             observed_vfs_hash_opens == 0u);
      retro_unload_game();
      observed_vfs_path = NULL;
      assert(remove(hostile_pseudo) == 0 && remove(hostile_zip) == 0);

      /* R1-1c: zip-adffs-carrier-too-large. */
      char zip_jfd_pseudo[4096];
      snprintf(zip_jfd_pseudo, sizeof(zip_jfd_pseudo), "%s#Game.jfd",
               zip_too_large_jfd);
      observed_vfs_path = zip_jfd_pseudo;
      observed_vfs_read_opens = observed_vfs_write_opens = observed_vfs_reads = 0u;
      zip_game.path = zip_too_large_jfd;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=too-large\n") &&
             observed_vfs_read_opens == 0u &&
             observed_vfs_write_opens == 0u);
      retro_unload_game();
      observed_vfs_path = NULL;

      /* R1-1d: zip-adffs-native-reinsert-retains-reason. decoded-too-large
       * is decided after the retained raw member has been acquired. */
      snprintf(zip_jfd_pseudo, sizeof(zip_jfd_pseudo), "%s#Game.jfd",
               zip_decoded_too_large_jfd);
      observed_vfs_path = zip_jfd_pseudo;
      observed_vfs_read_opens = observed_vfs_write_opens = observed_vfs_reads = 0u;
      zip_game.path = zip_decoded_too_large_jfd;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=decoded-too-large\n"));
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_eject_state(false));
      unsigned reinsert_boot_lines = jfd_boot_log_lines;
      retro_reset();
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=decoded-too-large\n") &&
             jfd_boot_log_lines == reinsert_boot_lines + 1u &&
             observed_vfs_read_opens == 0u &&
             observed_vfs_write_opens == 0u);
      retro_unload_game();
      observed_vfs_path = NULL;
      free(eligible_bytes);

      zip_game.path = zip_two_jfd;
      snprintf(zip_jfd_pseudo, sizeof(zip_jfd_pseudo), "%s#Disc1.jfd",
               zip_two_jfd);
      observed_vfs_path = zip_jfd_pseudo;
      observed_vfs_read_opens = observed_vfs_write_opens = observed_vfs_reads = 0u;
      phase = PHASE_LOAD; assert(retro_load_game(&zip_game));
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=multi-image\n"));
      /* R1-1e: zip-adffs-missing-retained-bytes. Reducing a multi-image zip
       * through disk control cannot invent retained bytes or open a member
       * pseudo-path; the single remaining JFD falls back deterministically. */
      assert(disk_control.set_eject_state(true) &&
             disk_control.replace_image_index(1u, NULL) &&
             disk_control.set_eject_state(false));
      retro_reset();
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=content-unreadable\n") &&
             observed_vfs_read_opens == 0u &&
             observed_vfs_write_opens == 0u);
      retro_unload_game();
      observed_vfs_path = NULL;

      option_launch = "off";
      option_writes = "off";

      char folder_directory[4096], folder_selected[4096], folder_disc2[4096];
      char folder_adl[4096], folder_short[4096], folder_duplicate[4096];
      char folder_jfd[4096], folder_rejected[4096];
      snprintf(folder_directory, sizeof(folder_directory), "%s/Qaventra.adf", argv[11]);
      snprintf(folder_selected, sizeof(folder_selected), "%s/Qaventra.adf", folder_directory);
      snprintf(folder_disc2, sizeof(folder_disc2), "%s/Qaventra 2.adf", folder_directory);
      snprintf(folder_adl, sizeof(folder_adl), "%s/Qaventra 3.adl", folder_directory);
      snprintf(folder_rejected, sizeof(folder_rejected), "%s/Qaventra 4.jfd", folder_directory);
      snprintf(folder_short, sizeof(folder_short), "%s/Qaventra 5.adl", folder_directory);
      snprintf(folder_duplicate, sizeof(folder_duplicate), "%s/Qaventra 9.adf", folder_directory);
      snprintf(folder_jfd, sizeof(folder_jfd), "%s/Qaventra 10.JFD", folder_directory);
      struct retro_game_info folder_game = {folder_selected, NULL, 0u, NULL};
      ArcDigest folder_identity = digest_file(folder_selected);
      assert(!strcmp(folder_identity.sha1, standalone_identity.sha1));

      /* Both members of a dedicated generated folder set have distinct
       * launch candidates. A direct load proves the later fixture's candidate
       * before the folder load proves that discovery inspects only image 1. */
      char launch_folder_directory[4096], launch_folder_selected[4096];
      char launch_folder_disc2[4096];
      snprintf(launch_folder_directory, sizeof(launch_folder_directory),
               "%s/Virelai.adf", argv[11]);
      snprintf(launch_folder_selected, sizeof(launch_folder_selected),
               "%s/Virelai.adf", launch_folder_directory);
      snprintf(launch_folder_disc2, sizeof(launch_folder_disc2),
               "%s/Virelai 2.adf", launch_folder_directory);
      struct retro_game_info later_folder_game = {
         launch_folder_disc2, NULL, 0u, NULL
      };
      unsigned launch_decisions = launch_log_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&later_folder_game));
      assert(launch_log_lines == launch_decisions + 1u &&
             strstr(last_launch_log, "candidates=[1:!Nulvane:app]") &&
             !strstr(last_launch_log, "!Virelai"));
      retro_unload_game();
      struct retro_game_info launch_folder_game = {
         launch_folder_selected, NULL, 0u, NULL
      };
      launch_decisions = launch_log_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&launch_folder_game));
      assert(disk_control.get_num_images() == 2u &&
             disk_control.get_image_index() == 0u &&
             !disk_control.get_eject_state() &&
             launch_log_lines == launch_decisions + 1u &&
             strstr(last_launch_log, "candidates=[1:!Virelai:app]") &&
             !strstr(last_launch_log, "!Nulvane"));
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_image_index(1u) &&
             disk_control.set_eject_state(false) &&
             disk_control.get_image_index() == 1u &&
             !disk_control.get_eject_state());
      retro_unload_game();
      launch_decisions = launch_log_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&launch_folder_game));
      assert(disk_control.get_num_images() == 2u &&
             disk_control.get_image_index() == 0u &&
             !disk_control.get_eject_state() &&
             launch_log_lines == launch_decisions + 1u &&
             strstr(last_launch_log, "candidates=[1:!Virelai:app]") &&
             !strstr(last_launch_log, "!Nulvane"));
      retro_unload_game();

      unsigned folder_messages = folder_set_warning_osd_calls;
      unsigned folder_logs = folder_set_log_lines;
      unsigned omission_logs = folder_set_omission_log_lines;
      unsigned all_messages = total_osd_calls;
      unsigned short_messages = short_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&folder_game));
      ASSERT_NO_SAVE_RAM();
      assert(!strcmp(arc_libretro_test_content_id(), standalone_identity.sha1));
      assert(folder_set_warning_osd_calls == folder_messages + 1u &&
             folder_set_log_lines == folder_logs + 1u &&
             folder_set_omission_log_lines == omission_logs + 1u &&
             short_warning_osd_calls == short_messages &&
             total_osd_calls == all_messages + 3u);
      assert(disk_control.get_num_images() == 6u &&
             disk_control.get_image_index() == 0u);
      const char *expected_folder_paths[6] = {
         folder_selected, folder_disc2, folder_adl, folder_short,
         folder_duplicate, folder_jfd
      };
      static const char *const folder_labels[] = {
         "Qaventra", "Qaventra 2", "Qaventra 3", "Qaventra 5",
         "Qaventra 9", "ArcDroid folder JFD"
      };
      for (unsigned index = 0u; index < 6u; ++index)
      {
         assert(disk_control.get_image_path(index, image_path, sizeof(image_path)) &&
                !strcmp(image_path, expected_folder_paths[index]));
         assert(disk_control.get_image_label(index, image_label, sizeof(image_label)) &&
                !strcmp(image_label, folder_labels[index]));
      }
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      uint64_t folder_disc1_signature = last_video_signature;
      assert(disk_control.set_eject_state(true));
      assert(disk_control.set_image_index(1u));
      assert(disk_control.set_eject_state(false));
      char folder_mounted_digest[41];
      ArcDigest folder_disc2_digest = digest_file(folder_disc2);
      assert(arc_engine_test_drive_media(0u, NULL, folder_mounted_digest) &&
             !strcmp(folder_mounted_digest, folder_disc2_digest.sha1));
      for (unsigned frame = 0u; frame < 180u; ++frame) retro_run();
      assert(last_video_signature != folder_disc1_signature);
      assert(disk_control.set_eject_state(true));
      assert(disk_control.set_image_index(0u));
      assert(disk_control.add_image_index());
      struct retro_game_info folder_append = {argv[7], NULL, 0u, NULL};
      assert(disk_control.replace_image_index(6u, &folder_append) &&
             disk_control.get_num_images() == 7u);
      assert(disk_control.replace_image_index(6u, NULL) &&
             disk_control.get_num_images() == 6u);
      assert(disk_control.set_eject_state(false));
      retro_unload_game();

      /* Original mode omits the short sibling and starts on image 1. Every
       * retained callback path is its source path, and preparing/unloading
       * the set removes none of those source files. */
      const char *folder_sources[] = {
         folder_selected, folder_disc2, folder_adl, folder_rejected,
         folder_short, folder_duplicate, folder_jfd
      };
      uint64_t folder_source_hashes[sizeof(folder_sources) / sizeof(folder_sources[0])];
      for (unsigned i = 0u;
           i < sizeof(folder_sources) / sizeof(folder_sources[0]); ++i)
         folder_source_hashes[i] = hash_file(folder_sources[i]);
      option_writes = "original";
      folder_messages = folder_set_warning_osd_calls;
      omission_logs = folder_set_omission_log_lines;
      all_messages = total_osd_calls;
      short_messages = short_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&folder_game));
      assert(disk_control.get_num_images() == 5u &&
             disk_control.get_image_index() == 0u &&
             folder_set_warning_osd_calls == folder_messages + 1u &&
             folder_set_omission_log_lines == omission_logs + 2u &&
             short_warning_osd_calls == short_messages &&
             total_osd_calls == all_messages + 3u);
      const char *original_paths[] = {
         folder_selected, folder_disc2, folder_adl, folder_duplicate, folder_jfd
      };
      for (unsigned i = 0u; i < 5u; ++i)
         assert(disk_control.get_image_path(i, image_path, sizeof(image_path)) &&
                !strcmp(image_path, original_paths[i]));
      for (unsigned i = 0u; i < 4u; ++i)
      {
         if (i)
            assert(disk_control.set_eject_state(true) &&
                   disk_control.set_image_index(i) &&
                   disk_control.set_eject_state(false));
         ArcDigest source_digest = digest_file(original_paths[i]);
         assert(arc_engine_test_drive_media(0u, NULL, folder_mounted_digest) &&
                !strcmp(folder_mounted_digest, source_digest.sha1));
      }
      retro_unload_game();
      for (unsigned i = 0u;
           i < sizeof(folder_sources) / sizeof(folder_sources[0]); ++i)
         assert(hash_file(folder_sources[i]) == folder_source_hashes[i]);

      /* R-1: changing a live folder set from shadow to original omits the
       * short sibling instead of tearing down the session. Because that was
       * the selected path, selection falls back to the full index-0 image. */
      char reset_directory[4096], reset_selected[4096], reset_short[4096];
      snprintf(reset_directory, sizeof(reset_directory),
               "%s/Remora.adf", argv[4]);
      make_directory(reset_directory);
      snprintf(reset_selected, sizeof(reset_selected),
               "%s/Remora.adf", reset_directory);
      snprintf(reset_short, sizeof(reset_short),
               "%s/Remora 2.adl", reset_directory);
      copy_file(folder_selected, reset_selected);
      copy_file(folder_short, reset_short);
      struct retro_game_info reset_folder_game = {reset_selected, NULL, 0u, NULL};
      ArcDigest reset_selected_digest = digest_file(reset_selected);
      option_writes = "shadow";
      phase = PHASE_LOAD; assert(retro_load_game(&reset_folder_game));
      assert(disk_control.get_num_images() == 2u &&
             disk_control.set_eject_state(true) &&
             disk_control.set_image_index(1u) &&
             disk_control.set_eject_state(false) &&
             arc_engine_test_drive_media(0u, NULL, folder_mounted_digest));
      folder_messages = folder_set_warning_osd_calls;
      omission_logs = folder_set_omission_log_lines;
      all_messages = total_osd_calls;
      short_messages = short_warning_osd_calls;
      option_writes = "original";
      phase = PHASE_RUN; retro_reset();
      assert(disk_control.get_num_images() == 1u &&
             disk_control.get_image_index() == 0u &&
             !disk_control.get_eject_state() &&
             !strcmp(arc_libretro_test_content_id(), reset_selected_digest.sha1) &&
             folder_set_warning_osd_calls == folder_messages + 1u &&
             folder_set_omission_log_lines == omission_logs + 1u &&
             short_warning_osd_calls == short_messages &&
             total_osd_calls == all_messages + 2u &&
             arc_engine_test_drive_media(0u, NULL, folder_mounted_digest) &&
             !strcmp(folder_mounted_digest, reset_selected_digest.sha1));
      unsigned folder_reset_video = video_frames;
      uint64_t folder_reset_audio = audio_frames;
      for (unsigned frame = 0u; frame < 60u; ++frame) retro_run();
      assert(video_frames > folder_reset_video && audio_frames > folder_reset_audio);
      retro_unload_game();

      /* More than 31 qualifying siblings is exercised through the VFS and
       * complete load path, not only the pure membership unit. */
      char overflow_directory[4096], overflow_selected[4096], overflow_member[4096];
      snprintf(overflow_directory, sizeof(overflow_directory),
               "%s/Orivane.adf", argv[4]);
      make_directory(overflow_directory);
      snprintf(overflow_selected, sizeof(overflow_selected),
               "%s/Orivane.adf", overflow_directory);
      copy_file(argv[7], overflow_selected);
      for (unsigned i = 1u; i <= 32u; ++i)
      {
         snprintf(overflow_member, sizeof(overflow_member),
                  "%s/Orivane %02u.adf", overflow_directory, i);
         copy_file(argv[8], overflow_member);
      }
      struct retro_game_info overflow_game = {overflow_selected, NULL, 0u, NULL};
      option_writes = "off";
      folder_messages = folder_set_warning_osd_calls;
      all_messages = total_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&overflow_game));
      assert(disk_control.get_num_images() == 1u &&
             disk_control.get_image_index() == 0u &&
             folder_set_warning_osd_calls == folder_messages + 1u &&
             total_osd_calls == all_messages + 3u);
      retro_unload_game();

      /* First create and mutate index 0's writable shadow while discovery is
       * unavailable. The next load rejects one sibling and fault-injects a
       * second sibling's publish. Both are omitted with one summary, while a
       * duplicate-digest sibling shares index 0's existing shadow safely. */
      save_dir = fault_save_path;
      option_writes = "shadow";
      fault_opendir_substring = folder_directory;
      phase = PHASE_LOAD; assert(retro_load_game(&folder_game));
      assert(!fault_opendir_substring && disk_control.get_num_images() == 1u);
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 250u; ++frame) retro_run();
      retro_unload_game();
      char selected_shadow[4096];
      staged_media_path(selected_shadow, fault_save_path, folder_selected, "disc.adf");
      uint64_t selected_shadow_hash = hash_file(selected_shadow);
      folder_messages = folder_set_warning_osd_calls;
      omission_logs = folder_set_omission_log_lines;
      fault_rename_substring = "/disc.adf";
      fault_rename_destination[0] = '\0';
      phase = PHASE_LOAD; assert(retro_load_game(&folder_game));
      assert(!fault_rename_substring && fault_rename_destination[0] &&
             disk_control.get_num_images() == 5u &&
             disk_control.get_image_index() == 0u &&
             hash_file(selected_shadow) == selected_shadow_hash &&
             folder_set_warning_osd_calls == folder_messages + 1u &&
             folder_set_omission_log_lines == omission_logs + 2u);
      retro_unload_game();
      assert(hash_file(selected_shadow) == selected_shadow_hash);
      assert(remove(fault_rename_destination) == 0);
      fault_rename_destination[0] = '\0';
      save_dir = save_path;

      option_writes = "off";
      folder_messages = folder_set_warning_osd_calls;
      fault_opendir_substring = folder_directory;
      phase = PHASE_LOAD; assert(retro_load_game(&folder_game));
      assert(!fault_opendir_substring && disk_control.get_num_images() == 1u &&
             folder_set_warning_osd_calls == folder_messages + 1u);
      retro_unload_game();

      /* The 1022-byte parent is never passed to VFS opendir. The adjacent
       * 1021-byte boundary is enumerated exactly once. */
      char boundary_native[4096], boundary_parent[4096], boundary_content[4096];
      snprintf(boundary_native, sizeof(boundary_native), "%s/boundary-vfs", argv[4]);
      make_directory(boundary_native);
      make_boundary_paths(1021u, boundary_parent, boundary_content);
      struct retro_game_info boundary_game = {boundary_content, NULL, 0u, NULL};
      aliased_vfs_file_path = boundary_content;
      aliased_vfs_file_source = argv[7];
      aliased_vfs_directory_path = boundary_parent;
      aliased_vfs_directory_source = boundary_native;
      observed_opendir_path = boundary_parent;
      observed_opendir_calls = 0u;
      phase = PHASE_LOAD; assert(retro_load_game(&boundary_game));
      assert(observed_opendir_calls == 1u && disk_control.get_num_images() == 1u);
      retro_unload_game();

      make_boundary_paths(1022u, boundary_parent, boundary_content);
      boundary_game.path = boundary_content;
      aliased_vfs_file_path = boundary_content;
      aliased_vfs_directory_path = boundary_parent;
      observed_opendir_path = boundary_parent;
      observed_opendir_calls = 0u;
      folder_messages = folder_set_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&boundary_game));
      assert(observed_opendir_calls == 0u && disk_control.get_num_images() == 1u &&
             folder_set_warning_osd_calls == folder_messages + 1u);
      retro_unload_game();
      observed_opendir_path = NULL;
      aliased_vfs_file_path = NULL;
      aliased_vfs_file_source = NULL;
      aliased_vfs_directory_path = NULL;
      aliased_vfs_directory_source = NULL;

      last_input_resolution[0] = '\0';
      phase = PHASE_LOAD; assert(retro_load_game(&playlist_game));
      assert(!strcmp(arc_libretro_test_content_id(), playlist_identity_sha1));
      assert_input_resolution();
      assert(disk_control.get_num_images() == 2u &&
             disk_control.get_image_index() == 0u &&
             !disk_control.get_eject_state());
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_image_index(1u) &&
             disk_control.set_eject_state(false) &&
             disk_control.get_image_index() == 1u &&
             !disk_control.get_eject_state());
      assert(!strcmp(arc_libretro_test_content_id(), playlist_identity_sha1));
      retro_unload_game();

      /* Unload/reload in one process cannot carry a valid later index into
       * the new playlist session. */
      phase = PHASE_LOAD; assert(retro_load_game(&playlist_game));
      assert(disk_control.get_num_images() == 2u &&
             disk_control.get_image_index() == 0u &&
             !disk_control.get_eject_state());
      retro_unload_game();

      /* Every successful playlist load starts on image 1. */
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
             !strcmp(arc_libretro_test_content_id(), playlist_identity_sha1));
      assert_input_resolution();
      assert_effective_descriptors();
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

      /* A failed load cannot affect the next session's initial index. */
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

      /* The stored layout stays declared-size with a source-identical prefix.
       * Missing presentation is recorded separately from these backing bytes. */
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
         {"short-family/800k-d-cap-plus-sector.adf", 819200u, "disc-readonly.adf"},
         {"short-family/800k-e-one-sector.adf", 819200u, "disc-readonly.adf"},
         {"short-family/800k-e-cap.adf", 819200u, "disc-readonly.adf"},
         {"short-e-missing-1024.adf", 819200u, "disc-readonly.adf"},
         {"short-e-missing-10240.adf", 819200u, "disc-readonly.adf"},
         {"short-e-missing-20480.adf", 819200u, "disc-readonly.adf"},
         {"short-family/800k-e-free-map.adf", 819200u, "disc-readonly.adf"},
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
         unsigned infos = short_info_lines;
         struct retro_game_info short_game = {source, NULL, 0u, NULL};
         phase = PHASE_LOAD; assert(retro_load_game(&short_game));
         assert(short_warning_log_lines == logs + 1u &&
                short_warning_osd_calls == osd + 1u &&
                short_info_lines == infos + 1u &&
                strstr(short_notice_text, "presented as absent") &&
                strstr(short_info_text, "presentation=missing"));
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
         "short-e-missing-20481.adf", "short-e-missing-21504.adf",
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
         "short-family/800k-e-allocated-tail.adf",
         "short-family/800k-e-conflicting-maps.adf",
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

      /* Slice 5c generated media contains two independently observable
       * applications. First capture each explicit-application oracle for
       * helper bytes, machine CMOS and final guest video. */
      char chooser_rom[4096];
      snprintf(chooser_rom, sizeof(chooser_rom),
               "%s/arcdroid-chooser-test.rom", argv[11]);
      copy_file(chooser_rom, renamed);
      char chooser_source[4096];
      snprintf(chooser_source, sizeof(chooser_source),
               "%s/arcdroid-chooser-l.adl", argv[11]);
      struct retro_game_info chooser_game = {chooser_source, NULL, 0u, NULL};
      ArcDigest chooser_digest = digest_file(chooser_source);
      char chooser_helper[4096], chooser_binding[4096], chooser_choice[4096];
      char chooser_cmos[4096];
      snprintf(chooser_helper, sizeof(chooser_helper),
               "%s/arcdroid/launch/%s.adl", save_path, chooser_digest.sha1);
      snprintf(chooser_binding, sizeof(chooser_binding),
               "%s/arcdroid/launch/%s.bind", save_path, chooser_digest.sha1);
      snprintf(chooser_choice, sizeof(chooser_choice),
               "%s/arcdroid/launch/%s.choice", save_path, chooser_digest.sha1);
      snprintf(chooser_cmos, sizeof(chooser_cmos),
               "%s/arcdroid/cmos/A3000.test.%s.bin",
               save_path, chooser_digest.sha1);
      (void)remove(chooser_helper); (void)remove(chooser_binding);
      (void)remove(chooser_choice); (void)remove(chooser_cmos);
      uint8_t *expected_helper[2] = {
         malloc(ARC_LAUNCH_DISC_SIZE), malloc(ARC_LAUNCH_DISC_SIZE)
      };
      assert(expected_helper[0] && expected_helper[1]);
      uint64_t expected_guest_signature[2];
      uint8_t clean_machine_cmos[ARC_CMOS_MACHINE_SIZE];
      uint8_t chooser_machine_cmos[ARC_CMOS_MACHINE_SIZE];
      uint8_t clean_persisted_cmos[ARC_CMOS_RECORD_SIZE];
      option_writes = "off";
      option_launch_chooser = "remember";
      for (unsigned choice = 0u; choice < 2u; ++choice)
      {
         (void)remove(chooser_helper); (void)remove(chooser_binding);
         (void)remove(chooser_cmos);
         option_launch = choice ? "app2" : "app1";
         phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
         assert(arc_libretro_test_session_state() == 3u &&
                arc_engine_test_drive_media(1u, NULL, NULL));
         read_exact(chooser_helper, expected_helper[choice],
                    ARC_LAUNCH_DISC_SIZE);
         if (!choice)
            assert(arc_engine_test_cmos_image(clean_machine_cmos));
         ASSERT_NO_SAVE_RAM();
         unsigned completed = video_frames - duplicate_video_frames;
         phase = PHASE_RUN;
         for (unsigned frame = 0u; frame < 6u; ++frame) retro_run();
         if (!choice)
            read_exact(chooser_cmos, clean_persisted_cmos,
                       sizeof(clean_persisted_cmos));
         for (unsigned frame = 6u; frame < 520u; ++frame) retro_run();
         assert(video_frames - duplicate_video_frames - completed >= 500u);
         expected_guest_signature[choice] = last_video_signature;
         retro_unload_game();
      }
      assert(expected_guest_signature[0] != expected_guest_signature[1]);
      (void)remove(chooser_helper); (void)remove(chooser_binding);
      (void)remove(chooser_choice); (void)remove(chooser_cmos);

      /* Auto enters a core-owned, completely pre-boot state. Reset remains in
       * that state and no guest or persistent surface advances. */
      option_launch = "auto";
      unsigned chooser_fdc = fdc_command_count;
      unsigned chooser_kart = kart_traffic_count;
      unsigned chooser_cmos_reads = cmos_read_count;
      unsigned chooser_cmos_writes = cmos_write_count;
      unsigned chooser_cmos_opens = cmos_write_opens;
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      assert(arc_libretro_test_session_state() == 2u &&
             arc_libretro_test_chooser_rows() == 3u &&
             arc_libretro_test_chooser_focus() == 0u &&
             retro_serialize_size() == 0u &&
             disk_control.get_num_images() == 1u &&
             disk_control.get_image_index() == 0u &&
             !disk_control.get_eject_state() &&
             !disk_control.set_eject_state(true) &&
             !disk_control.set_image_index(0u) &&
             !disk_control.add_image_index() &&
             !disk_control.replace_image_index(0u, NULL) &&
             !arc_engine_test_drive_media(0u, NULL, NULL) &&
             !arc_engine_test_cmos_image(clean_machine_cmos) &&
             !file_exists(chooser_helper) && !file_exists(chooser_cmos) &&
             !file_exists(chooser_choice));
      retro_cheat_set(0u, true, "invented-code");
      retro_cheat_reset();
      assert(arc_libretro_test_session_state() == 2u);
      unsigned chooser_video = video_frames;
      uint64_t chooser_audio = audio_frames;
      uint64_t chooser_nonzero = nonzero_audio_samples;
      unsigned chooser_av = av_notifications;
      unsigned chooser_geometry = geometry_changes;
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 5u; ++frame) retro_run();
      assert(video_frames == chooser_video + 5u &&
             audio_frames == chooser_audio + 5u * 960u &&
             nonzero_audio_samples == chooser_nonzero &&
             av_notifications == chooser_av &&
             geometry_changes == chooser_geometry &&
             geometry_width == 640u && geometry_height == 480u &&
             current_reported_rate == 50.0 &&
             fdc_command_count == chooser_fdc &&
             kart_traffic_count == chooser_kart &&
             cmos_read_count == chooser_cmos_reads &&
             cmos_write_count == chooser_cmos_writes &&
             cmos_write_opens == chooser_cmos_opens &&
             !file_exists(chooser_helper) && !file_exists(chooser_cmos));
      retro_reset();
      assert(arc_libretro_test_session_state() == 2u &&
             arc_libretro_test_chooser_focus() == 0u &&
             !file_exists(chooser_helper) && !file_exists(chooser_cmos));
      read_cached_video();
      retro_run();

      /* No frontend SAVE_RAM surface can contaminate the private prepared
       * seed. Every held input source is gated across confirmation. */
      ASSERT_NO_SAVE_RAM();
      arc_platform_test_input_events_reset();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
      joypad_buttons[1][RETRO_DEVICE_ID_JOYPAD_RIGHT] = true;
      keyboard.callback(true, RETROK_a, 0u, 0u);
      mouse_buttons = 1u;
      analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 300;
      mouse_x = 200; mouse_y = -100;
      pointer_x = (int)((100u * 65535u) / 640u) - 32768;
      pointer_y = (int)((100u * 65535u) / 480u) - 32768;
      pointer_pressed = true;
      unsigned chooser_exit_av = av_notifications;
      unsigned chooser_exit_geometry = geometry_changes;
      retro_run();
      assert(arc_libretro_test_session_state() == 3u &&
             arc_engine_test_cmos_image(chooser_machine_cmos) &&
             !memcmp(chooser_machine_cmos, clean_machine_cmos,
                     sizeof(chooser_machine_cmos)) &&
             file_exists(chooser_choice));
      read_cached_video(); /* Chooser frame before the new machine's frame. */
      ASSERT_NO_SAVE_RAM();
      uint8_t actual_helper[ARC_LAUNCH_DISC_SIZE];
      read_exact(chooser_helper, actual_helper, sizeof(actual_helper));
      assert(!memcmp(actual_helper, expected_helper[0], sizeof(actual_helper)));
      for (unsigned frame = 0u; frame < 4u; ++frame) retro_run();
      assert(arc_platform_test_input_events(NULL, 0u) == 0u);
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
      joypad_buttons[1][RETRO_DEVICE_ID_JOYPAD_RIGHT] = false;
      keyboard.callback(false, RETROK_a, 0u, 0u);
      mouse_buttons = 0u;
      pointer_pressed = false;
      retro_run();
      assert(arc_platform_test_input_events(NULL, 0u) == 0u &&
             av_notifications == chooser_exit_av &&
             geometry_changes == chooser_exit_geometry + 1u &&
             geometry_width == 640u && geometry_height != 480u &&
             current_reported_rate == 50.0);
      retro_run();
      assert(arc_platform_test_input_events(NULL, 0u) == 0u);

      uint8_t persisted_clean[ARC_CMOS_RECORD_SIZE];
      read_exact(chooser_cmos, persisted_clean, sizeof(persisted_clean));
      assert(!memcmp(persisted_clean, clean_persisted_cmos,
                     sizeof(persisted_clean)));
      unsigned completed = video_frames - duplicate_video_frames;
      for (unsigned frame = 0u; frame < 520u; ++frame) retro_run();
      assert(video_frames - duplicate_video_frames - completed >= 500u &&
             last_video_signature == expected_guest_signature[0] &&
             arc_platform_test_input_events(NULL, 0u) == 0u);

      analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 0;
      uint8_t remembered_app1[ARC_LAUNCH_CHOICE_MAX_SIZE];
      size_t remembered_app1_size = read_bounded(
         chooser_choice, remembered_app1, sizeof(remembered_app1));
      retro_unload_game();

      /* Remember replays byte-identically without visiting the chooser. */
      (void)remove(chooser_helper);
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      assert(arc_libretro_test_session_state() == 3u &&
             arc_engine_test_drive_media(1u, NULL, NULL));
      uint8_t replay_record[ARC_LAUNCH_CHOICE_MAX_SIZE];
      assert(read_bounded(chooser_choice, replay_record, sizeof(replay_record)) ==
             remembered_app1_size &&
             !memcmp(replay_record, remembered_app1, remembered_app1_size));
      read_exact(chooser_helper, actual_helper, sizeof(actual_helper));
      assert(!memcmp(actual_helper, expected_helper[0], sizeof(actual_helper)));
      retro_unload_game();

      /* This firmware completes the KART handshake, so the mouse event counter
       * is a positive control for the exact option set used below. */
      char gate_source[4096];
      snprintf(gate_source, sizeof(gate_source),
               "%s/launch-family/chooser-resource-pair-d.adf", argv[11]);
      char gate_shadow[4096];
      staged_media_path(gate_shadow, save_path, gate_source, "disc.adf");
      (void)remove(gate_shadow);
      char gate_playlist[4096];
      snprintf(gate_playlist, sizeof(gate_playlist),
               "%s/chooser-release-gate.m3u", argv[4]);
      FILE *gate_file = fopen(gate_playlist, "wb");
      assert(gate_file && fprintf(gate_file, "%s\n%s\n", gate_source, argv[3]) > 0 &&
             !fclose(gate_file));
      struct retro_game_info gate_game = {gate_playlist, NULL, 0u, NULL};
      copy_file(argv[2], renamed);
      option_writes = "shadow";
      option_launch = "app1";
      option_launch_chooser = "remember";
      unsigned publications_before_load = ramdisc_publication_logs;
      phase = PHASE_LOAD; assert(retro_load_game(&gate_game));
      phase = PHASE_RUN;
      unsigned publications_before_swap = ramdisc_publication_logs;
      assert(publications_before_swap > publications_before_load);
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_image_index(1u) &&
             disk_control.set_eject_state(false));
      assert(ramdisc_publication_logs == publications_before_swap);
      for (unsigned frame = 0u; frame < 520u; ++frame) retro_run();
      arc_platform_test_input_events_reset();
      mouse_buttons = 1u;
      for (unsigned frame = 0u;
           frame < 120u && arc_platform_test_input_events(input_events, 512u) < 1u;
           ++frame) retro_run();
      mouse_buttons = 0u;
      for (unsigned frame = 0u;
           frame < 120u && arc_platform_test_input_events(input_events, 512u) < 2u;
           ++frame) retro_run();
      assert(arc_platform_test_input_events(input_events, 512u) >= 2u);
      assert(disk_control.set_eject_state(true) &&
             disk_control.set_image_index(0u) &&
             disk_control.set_eject_state(false));
      assert(ramdisc_publication_logs == publications_before_swap);

      /* A running guest at another refresh rate announces exactly one rate
       * change when reset enters the 50 Hz chooser. A/Return held across that
       * reset cannot confirm until both are released and Return is pressed
       * again. A 300-unit resting offset then clears the post-pick gate within
       * a few runs; the held mouse produces no guest event, while its later
       * re-press demonstrably does. */
      assert(current_reported_rate < 49.99 || current_reported_rate > 50.01);
      option_launch = "auto";
      option_launch_chooser = "ask";
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
      keyboard.callback(true, RETROK_RETURN, 0u, 0u);
      unsigned reset_to_chooser_av = av_notifications;
      retro_reset();
      arc_platform_test_input_events_reset();
      retro_run();
      assert(arc_libretro_test_session_state() == 2u &&
             av_notifications == reset_to_chooser_av + 1u &&
             current_reported_rate == 50.0);
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
      keyboard.callback(false, RETROK_RETURN, 0u, 0u);
      retro_run();
      keyboard.callback(true, RETROK_RETURN, 0u, 0u);
      mouse_buttons = 1u;
      analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 300;
      retro_run();
      assert(arc_libretro_test_session_state() == 3u);
      keyboard.callback(false, RETROK_RETURN, 0u, 0u);
      mouse_buttons = 0u;
      arc_platform_test_input_events_reset();
      for (unsigned frame = 0u; frame < 160u; ++frame) retro_run();
      assert(arc_platform_test_input_events(NULL, 0u) == 0u);
      mouse_buttons = 1u;
      for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
      mouse_buttons = 0u;
      for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
      assert(arc_platform_test_input_events(input_events, 512u) >= 2u);

      /* A second confirmation with an axis outside the deadzone keeps the
       * release gate closed even when the held mouse is released/re-pressed. */
      copy_file(gate_source, gate_shadow);
      option_launch_chooser = "ask";
      retro_reset();
      phase = PHASE_RUN;
      retro_run();
      assert(arc_libretro_test_session_state() == 2u);
      analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 12000;
      mouse_buttons = 1u;
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
      retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
      assert(arc_libretro_test_session_state() == 3u);
      arc_platform_test_input_events_reset();
      for (unsigned frame = 0u; frame < 160u; ++frame) retro_run();
      mouse_buttons = 0u; retro_run();
      mouse_buttons = 1u;
      for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
      assert(arc_platform_test_input_events(NULL, 0u) == 0u);

      option_launch = "off";
      retro_reset();
      mouse_buttons = 0u;
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 160u; ++frame) retro_run();
      arc_platform_test_input_events_reset();
      mouse_buttons = 1u;
      for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
      mouse_buttons = 0u;
      for (unsigned frame = 0u; frame < 10u; ++frame) retro_run();
      assert(arc_platform_test_input_events(input_events, 512u) >= 2u);
      analog_state[RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = 0;
      option_launch = "auto";
      option_launch_chooser = "remember";
      option_writes = "off";
      retro_unload_game();
      copy_file(chooser_rom, renamed);
      assert(remove(gate_playlist) == 0);

      /* Ask focuses the remembered row. A touch tap chooses the other app;
       * the private CMOS seed remains isolated from frontend persistence. */
      option_launch_chooser = "ask";
      (void)remove(chooser_helper); (void)remove(chooser_cmos);
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      assert(arc_libretro_test_session_state() == 2u &&
             arc_libretro_test_chooser_focus() == 0u);
      ASSERT_NO_SAVE_RAM();
      phase = PHASE_RUN;
      retro_run();
      pointer_x = (int)((100u * 65535u) / 640u) - 32768;
      pointer_y = (int)((122u * 65535u) / 480u) - 32768;
      pointer_pressed = true; retro_run();
      assert(arc_libretro_test_chooser_focus() == 1u &&
             arc_libretro_test_session_state() == 2u);
      pointer_pressed = false; retro_run();
      assert(arc_libretro_test_session_state() == 3u &&
             arc_engine_test_cmos_image(chooser_machine_cmos) &&
             !memcmp(chooser_machine_cmos, clean_machine_cmos,
                     sizeof(chooser_machine_cmos)));
      ASSERT_NO_SAVE_RAM();
      read_exact(chooser_helper, actual_helper, sizeof(actual_helper));
      assert(!memcmp(actual_helper, expected_helper[1], sizeof(actual_helper)));
      completed = video_frames - duplicate_video_frames;
      for (unsigned frame = 0u; frame < 520u; ++frame)
      {
         retro_run();
         if (frame == 0u)
         {
            read_exact(chooser_cmos, persisted_clean, sizeof(persisted_clean));
            assert(!memcmp(persisted_clean, clean_persisted_cmos,
                           sizeof(persisted_clean)));
         }
      }
      assert(video_frames - duplicate_video_frames - completed >= 500u &&
             last_video_signature == expected_guest_signature[1]);
      uint8_t remembered_app2[ARC_LAUNCH_CHOICE_MAX_SIZE];
      size_t remembered_app2_size = read_bounded(
         chooser_choice, remembered_app2, sizeof(remembered_app2));
      assert(remembered_app2_size != remembered_app1_size ||
             memcmp(remembered_app2, remembered_app1, remembered_app1_size));
      retro_unload_game();

      /* A release has no reliable coordinates on a real touchscreen. The
       * row captured while pressed confirms for each release-frame encoding. */
      (void)remove(chooser_helper);
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      phase = PHASE_RUN;
      retro_run();
      pointer_x = (int)((100u * 65535u) / 640u) - 32768;
      pointer_y = (int)((102u * 65535u) / 480u) - 32768;
      pointer_pressed = true; retro_run();
      assert(arc_libretro_test_chooser_focus() == 0u);
      pointer_x = pointer_y = -32768;
      pointer_pressed = false; retro_run();
      assert(arc_libretro_test_session_state() == 3u);
      read_exact(chooser_helper, actual_helper, sizeof(actual_helper));
      assert(!memcmp(actual_helper, expected_helper[0], sizeof(actual_helper)));
      retro_unload_game();

      (void)remove(chooser_helper);
      indexed_pointer_mode = true;
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      phase = PHASE_RUN;
      retro_run();
      pointer_count = 1u; pointer_contact_pressed[0] = true;
      pointer_contact_x[0] = (int)((100u * 65535u) / 640u) - 32768;
      pointer_contact_y[0] = (int)((122u * 65535u) / 480u) - 32768;
      retro_run();
      assert(arc_libretro_test_chooser_focus() == 1u);
      pointer_count = 0u; pointer_contact_pressed[0] = false;
      pointer_offscreen[0] = true; retro_run();
      assert(arc_libretro_test_session_state() == 3u);
      read_exact(chooser_helper, actual_helper, sizeof(actual_helper));
      assert(!memcmp(actual_helper, expected_helper[1], sizeof(actual_helper)));
      pointer_offscreen[0] = false;
      indexed_pointer_mode = false;
      retro_unload_game();

      (void)remove(chooser_helper);
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      phase = PHASE_RUN;
      retro_run();
      pointer_x = (int)((100u * 65535u) / 640u) - 32768;
      pointer_y = (int)((122u * 65535u) / 480u) - 32768;
      pointer_pressed = true; retro_run();
      assert(arc_libretro_test_chooser_focus() == 1u);
      pointer_x = pointer_y = 0;
      pointer_pressed = false; retro_run();
      assert(arc_libretro_test_session_state() == 3u);
      read_exact(chooser_helper, actual_helper, sizeof(actual_helper));
      assert(!memcmp(actual_helper, expected_helper[1], sizeof(actual_helper)));
      retro_unload_game();

      /* Leaving the captured row while still down permanently cancels that
       * contact, whether it crosses another row or leaves the list. */
      (void)remove(chooser_helper);
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      phase = PHASE_RUN;
      retro_run();
      pointer_x = (int)((100u * 65535u) / 640u) - 32768;
      pointer_y = (int)((122u * 65535u) / 480u) - 32768;
      pointer_pressed = true; retro_run();
      pointer_y = (int)((102u * 65535u) / 480u) - 32768;
      retro_run();
      pointer_pressed = false; pointer_x = pointer_y = -32768; retro_run();
      assert(arc_libretro_test_session_state() == 2u &&
             !file_exists(chooser_helper));
      retro_unload_game();

      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      phase = PHASE_RUN;
      retro_run();
      pointer_x = (int)((100u * 65535u) / 640u) - 32768;
      pointer_y = (int)((122u * 65535u) / 480u) - 32768;
      pointer_pressed = true; retro_run();
      pointer_x = (int)((10u * 65535u) / 640u) - 32768;
      retro_run();
      pointer_pressed = false; pointer_x = pointer_y = 0; retro_run();
      assert(arc_libretro_test_session_state() == 2u &&
             !file_exists(chooser_helper));
      retro_unload_game();

      option_launch_chooser = "remember";
      (void)remove(chooser_helper);
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      assert(arc_libretro_test_session_state() == 3u);
      read_exact(chooser_helper, actual_helper, sizeof(actual_helper));
      assert(!memcmp(actual_helper, expected_helper[1], sizeof(actual_helper)) &&
             read_bounded(chooser_choice, replay_record, sizeof(replay_record)) ==
                remembered_app2_size &&
             !memcmp(replay_record, remembered_app2, remembered_app2_size));
      retro_unload_game();

      /* Keyboard navigation confirms a real application and then Desktop.
       * Desktop is persisted and replayed as an ordinary no-helper launch. */
      option_launch_chooser = "ask";
      (void)remove(chooser_helper);
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      assert(arc_libretro_test_chooser_focus() == 1u);
      phase = PHASE_RUN;
      retro_run();
      keyboard.callback(true, RETROK_UP, 0u, 0u); retro_run();
      keyboard.callback(false, RETROK_UP, 0u, 0u); retro_run();
      keyboard.callback(true, RETROK_RETURN, 0u, 0u); retro_run();
      keyboard.callback(false, RETROK_RETURN, 0u, 0u); retro_run();
      assert(arc_libretro_test_session_state() == 3u);
      read_exact(chooser_helper, actual_helper, sizeof(actual_helper));
      assert(!memcmp(actual_helper, expected_helper[0], sizeof(actual_helper)));
      completed = video_frames - duplicate_video_frames;
      for (unsigned frame = 0u; frame < 520u; ++frame) retro_run();
      assert(video_frames - duplicate_video_frames - completed >= 500u &&
             last_video_signature == expected_guest_signature[0]);
      retro_unload_game();

      (void)remove(chooser_helper);
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      assert(arc_libretro_test_chooser_focus() == 0u);
      phase = PHASE_RUN;
      retro_run();
      for (unsigned row = 0u; row < 2u; ++row)
      {
         keyboard.callback(true, RETROK_DOWN, 0u, 0u); retro_run();
         keyboard.callback(false, RETROK_DOWN, 0u, 0u); retro_run();
      }
      assert(arc_libretro_test_chooser_focus() == 2u);
      keyboard.callback(true, RETROK_RETURN, 0u, 0u); retro_run();
      keyboard.callback(false, RETROK_RETURN, 0u, 0u); retro_run();
      assert(arc_libretro_test_session_state() == 3u &&
             !arc_engine_test_drive_media(1u, NULL, NULL) &&
             !file_exists(chooser_helper));
      uint8_t remembered_desktop[ARC_LAUNCH_CHOICE_MAX_SIZE];
      size_t remembered_desktop_size = read_bounded(
         chooser_choice, remembered_desktop, sizeof(remembered_desktop));
      retro_unload_game();
      option_launch_chooser = "remember";
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      assert(arc_libretro_test_session_state() == 3u &&
             !arc_engine_test_drive_media(1u, NULL, NULL) &&
             read_bounded(chooser_choice, replay_record, sizeof(replay_record)) ==
                remembered_desktop_size &&
             !memcmp(replay_record, remembered_desktop,
                     remembered_desktop_size));
      retro_unload_game();

      /* Malformed, oversized, trailing and unmatched records are never
       * repaired merely by loading or unloading the chooser. */
      static const uint8_t choice_invalid[] = "not-a-choice\n";
      static const uint8_t choice_trailing[] =
         "v1\napp\n214e756c76616e65\n"
         "0000000000000000000000000000000000000000\nX";
      static const uint8_t choice_unmatched[] =
         "v1\napp\n21556e6b6e6f776e\n"
         "0000000000000000000000000000000000000000\n";
      static const uint8_t choice_legacy_install[] =
         "v2\ninstall\nF00123\nrev=1\nvariant=game\n"
         "set=project-authored-retired\n";
      uint8_t choice_oversized[ARC_LAUNCH_CHOICE_MAX_SIZE + 1u];
      memset(choice_oversized, 'x', sizeof(choice_oversized));
      const struct { const uint8_t *data; size_t size; } bad_choices[] = {
         {choice_invalid, sizeof(choice_invalid) - 1u},
         {choice_oversized, sizeof(choice_oversized)},
         {choice_trailing, sizeof(choice_trailing) - 1u},
         {choice_unmatched, sizeof(choice_unmatched) - 1u},
         {choice_legacy_install, sizeof(choice_legacy_install) - 1u},
      };
      option_launch_chooser = "remember";
      for (unsigned bad_choice = 0u;
           bad_choice < sizeof(bad_choices) / sizeof(bad_choices[0]);
           ++bad_choice)
      {
         write_exact(chooser_choice, bad_choices[bad_choice].data,
                     bad_choices[bad_choice].size);
         uint64_t unchanged = hash_file(chooser_choice);
         (void)remove(chooser_helper);
         phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
         assert(arc_libretro_test_session_state() == 2u &&
                hash_file(chooser_choice) == unchanged);
         retro_unload_game();
         assert(hash_file(chooser_choice) == unchanged);
      }
      uint64_t retired_choice_hash = hash_file(chooser_choice);
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      assert(arc_libretro_test_session_state() == 2u &&
             hash_file(chooser_choice) == retired_choice_hash);
      phase = PHASE_RUN; retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
      assert(arc_libretro_test_session_state() == 3u &&
             hash_file(chooser_choice) != retired_choice_hash);
      size_t native_choice_size = read_bounded(chooser_choice, replay_record,
                                              sizeof(replay_record));
      ArcLaunchChoiceRecord native_choice;
      assert(arc_launch_choice_parse(replay_record, native_choice_size,
                                     &native_choice));
      retro_unload_game();
      write_exact(chooser_choice, choice_unmatched,
                  sizeof(choice_unmatched) - 1u);
      uint64_t unmatched_hash = hash_file(chooser_choice);
      fault_read_substring = ".choice";
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      assert(!fault_read_substring && arc_libretro_test_session_state() == 2u &&
             hash_file(chooser_choice) == unmatched_hash);
      retro_unload_game();

      /* A failed write and an interrupted replacement both preserve the old
       * record, warn visibly, and still start the selected application. */
      write_exact(chooser_choice, choice_unmatched,
                  sizeof(choice_unmatched) - 1u);
      unmatched_hash = hash_file(chooser_choice);
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      fault_write_substring = ".choice.tmp";
      phase = PHASE_RUN;
      retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
      assert(!fault_write_substring && arc_libretro_test_session_state() == 3u &&
             strstr(last_message, "choice not remembered") &&
             hash_file(chooser_choice) == unmatched_hash &&
             arc_engine_test_drive_media(1u, NULL, NULL));
      retro_unload_game();

      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      fault_rename_substring = ".choice.previous";
      phase = PHASE_RUN;
      retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
      assert(!fault_rename_substring && arc_libretro_test_session_state() == 3u &&
             strstr(last_message, "choice not remembered") &&
             hash_file(chooser_choice) == unmatched_hash &&
             arc_engine_test_drive_media(1u, NULL, NULL));
      retro_unload_game();

      /* A retry after the interrupted replace installs a parseable record. */
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      phase = PHASE_RUN;
      retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false; retro_run();
      assert(arc_libretro_test_session_state() == 3u &&
             hash_file(chooser_choice) != unmatched_hash);
      ArcLaunchChoiceRecord installed_choice;
      size_t installed_size = read_bounded(
         chooser_choice, replay_record, sizeof(replay_record));
      assert(arc_launch_choice_parse(replay_record, installed_size,
                                     &installed_choice));
      retro_unload_game();

      /* A construction failure after confirmation is reported and leaves an
       * explicit failed session, not a half-started chooser or guest. */
      write_exact(chooser_choice, choice_invalid, sizeof(choice_invalid) - 1u);
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      assert(setenv("ARCDROID_TEST_FAIL_VIDEO_ALLOC", "1", 1) == 0);
      phase = PHASE_RUN;
      retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true; retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
      assert(unsetenv("ARCDROID_TEST_FAIL_VIDEO_ALLOC") == 0 &&
             arc_libretro_test_session_state() == 4u &&
             strstr(last_message, "video surface"));
      ASSERT_NO_SAVE_RAM();
      retro_unload_game();

      free(expected_helper[0]); free(expected_helper[1]);
      copy_file(argv[5], renamed);
      option_launch_chooser = "remember";
      option_launch = "auto";

      /* Resource directories remain diagnostic/boot records, but the one
       * game beside them is Application 1 and launches directly. Historical
       * v2 and v1 bindings retain their exact target guard without rewrite. */
      char resource_source[4096];
      snprintf(resource_source, sizeof(resource_source),
               "%s/launch-family/chooser-resources-d.adf", argv[11]);
      struct retro_game_info resource_game = {
         resource_source, NULL, 0u, NULL
      };
      ArcDigest resource_digest = digest_file(resource_source);
      char resource_binding[4096], resource_choice[4096];
      snprintf(resource_binding, sizeof(resource_binding),
               "%s/arcdroid/launch/%s.bind", save_path, resource_digest.sha1);
      snprintf(resource_choice, sizeof(resource_choice),
               "%s/arcdroid/launch/%s.choice", save_path, resource_digest.sha1);
      ArcGuestName virelai = {{'!', 'V', 'i', 'r', 'e', 'l', 'a', 'i'}, 8u};
      char zero_fingerprint[41];
      memset(zero_fingerprint, '0', 40u); zero_fingerprint[40] = '\0';
      uint8_t old_v2[ARC_LAUNCH_BINDING_MAX_SIZE];
      size_t old_v2_size = 0u;
      assert(arc_launch_binding_format(3u, &virelai, zero_fingerprint,
                                       old_v2, sizeof(old_v2), &old_v2_size));
      write_exact(resource_binding, old_v2, old_v2_size);
      uint64_t old_binding_hash = hash_file(resource_binding);
      option_launch = "app1";
      phase = PHASE_LOAD; assert(retro_load_game(&resource_game));
      assert(arc_libretro_test_session_state() == 3u &&
             arc_engine_test_drive_media(1u, NULL, NULL) &&
             hash_file(resource_binding) == old_binding_hash &&
             strstr(last_launch_log, "1:!System:resource") &&
             strstr(last_launch_log, "2:!Fonts:resource") &&
             strstr(last_launch_log, "3:!Scrap:resource") &&
             strstr(last_launch_log, "applications=[1:!Virelai]"));
      retro_unload_game();
      static const uint8_t old_v1[] =
         "app4\n!Virelai\n0000000000000000000000000000000000000000\n";
      write_exact(resource_binding, old_v1, sizeof(old_v1) - 1u);
      old_binding_hash = hash_file(resource_binding);
      phase = PHASE_LOAD; assert(retro_load_game(&resource_game));
      assert(arc_libretro_test_session_state() == 3u &&
             arc_engine_test_drive_media(1u, NULL, NULL) &&
             hash_file(resource_binding) == old_binding_hash);
      retro_unload_game();
      assert(remove(resource_binding) == 0);
      option_launch = "auto";
      fault_open_substring = ".choice";
      phase = PHASE_LOAD; assert(retro_load_game(&resource_game));
      assert(fault_open_substring && arc_libretro_test_session_state() == 3u &&
             arc_engine_test_drive_media(1u, NULL, NULL) &&
             strstr(last_launch_log, "reason=single application"));
      fault_open_substring = NULL;
      retro_unload_game();
      (void)remove(resource_choice);

      /* Three boot resources beside two verified applications produce only
       * the two contiguous application rows followed by Desktop. */
      char resource_pair_source[4096];
      snprintf(resource_pair_source, sizeof(resource_pair_source),
               "%s/launch-family/chooser-resource-pair-d.adf", argv[11]);
      struct retro_game_info resource_pair_game = {
         resource_pair_source, NULL, 0u, NULL
      };
      ArcDigest resource_pair_digest = digest_file(resource_pair_source);
      char resource_pair_choice[4096];
      snprintf(resource_pair_choice, sizeof(resource_pair_choice),
               "%s/arcdroid/launch/%s.choice", save_path,
               resource_pair_digest.sha1);
      (void)remove(resource_pair_choice);
      phase = PHASE_LOAD; assert(retro_load_game(&resource_pair_game));
      assert(arc_libretro_test_session_state() == 2u &&
             arc_libretro_test_chooser_rows() == 3u &&
             arc_libretro_test_chooser_focus() == 0u &&
             strstr(last_launch_log, "1:!System:resource") &&
             strstr(last_launch_log, "2:!Fonts:resource") &&
             strstr(last_launch_log, "3:!Scrap:resource") &&
             strstr(last_launch_log,
                    "applications=[1:!Nulvane,2:!Qaventra]"));
      retro_unload_game();

      /* The shared eligibility gate prevents record IO for direct targets,
       * self-boot media, off/appN, unsupported firmware and later images. */
      static const uint8_t choice_sentinel[] = "choice-sentinel";
      char application_choice[4096];
      snprintf(application_choice, sizeof(application_choice),
               "%s/arcdroid/launch/%s.choice", save_path,
               application_digest.sha1);
      write_exact(application_choice, choice_sentinel,
                  sizeof(choice_sentinel) - 1u);
      fault_open_substring = ".choice";
      fault_write_substring = ".choice";
      option_launch = "auto";
      phase = PHASE_LOAD; assert(retro_load_game(&application_game));
      assert(fault_open_substring && fault_write_substring &&
             arc_libretro_test_session_state() == 3u);
      retro_unload_game();
      fault_open_substring = fault_write_substring = NULL;
      uint8_t sentinel_read[sizeof(choice_sentinel) - 1u];
      read_exact(application_choice, sentinel_read, sizeof(sentinel_read));
      assert(!memcmp(sentinel_read, choice_sentinel, sizeof(sentinel_read)));

      char booted_choice_source[4096];
      snprintf(booted_choice_source, sizeof(booted_choice_source),
               "%s/launch-family/boot-file-3-l.adl", argv[11]);
      struct retro_game_info booted_choice_game = {
         booted_choice_source, NULL, 0u, NULL
      };
      fault_open_substring = ".choice";
      fault_write_substring = ".choice";
      phase = PHASE_LOAD; assert(retro_load_game(&booted_choice_game));
      assert(fault_open_substring && fault_write_substring &&
             arc_libretro_test_session_state() == 3u &&
             strstr(last_launch_log, "disc boots itself"));
      retro_unload_game();
      fault_open_substring = fault_write_substring = NULL;

      for (unsigned explicit_mode = 0u; explicit_mode < 2u; ++explicit_mode)
      {
         option_launch = explicit_mode ? "app1" : "off";
         fault_open_substring = ".choice";
         fault_write_substring = ".choice";
         phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
         assert(fault_open_substring && fault_write_substring &&
                arc_libretro_test_session_state() == 3u);
         retro_unload_game();
         fault_open_substring = fault_write_substring = NULL;
         (void)remove(chooser_binding);
      }

      option_launch = "auto";
      option_os = "RISC OS 2.00";
      char unsupported_launch_rom[4096];
      snprintf(unsupported_launch_rom, sizeof(unsupported_launch_rom),
               "%s/arcdroid-unsupported-launch-test.rom", argv[11]);
      copy_file(unsupported_launch_rom, renamed);
      fault_open_substring = ".choice";
      fault_write_substring = ".choice";
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      assert(fault_open_substring && fault_write_substring &&
             arc_libretro_test_session_state() == 3u &&
             strstr(last_launch_log, "OS class unsupported"));
      retro_unload_game();
      fault_open_substring = fault_write_substring = NULL;
      option_os = "Auto";
      copy_file(argv[5], renamed);

      char incomplete_choice_source[4096];
      snprintf(incomplete_choice_source, sizeof(incomplete_choice_source),
               "%s/launch-family/chooser-incomplete-d.adf", argv[11]);
      struct retro_game_info incomplete_choice_game = {
         incomplete_choice_source, NULL, 0u, NULL
      };
      phase = PHASE_LOAD; assert(retro_load_game(&incomplete_choice_game));
      assert(arc_libretro_test_session_state() == 2u &&
             arc_libretro_test_chooser_rows() == 2u);
      phase = PHASE_RUN;
      retro_run();
      assert(strstr(last_launch_log, "chooser shown"));
      retro_unload_game();

      /* Live option updates do not alter the pre-boot state; reset applies
       * them and, with Launch=Off, still does no choice-record IO. */
      write_exact(chooser_choice, choice_invalid, sizeof(choice_invalid) - 1u);
      option_launch = "auto";
      phase = PHASE_LOAD; assert(retro_load_game(&chooser_game));
      option_launch = "off";
      variable_update = true;
      fault_open_substring = ".choice";
      phase = PHASE_RUN;
      retro_run();
      assert(arc_libretro_test_session_state() == 2u && fault_open_substring);
      retro_reset();
      assert(arc_libretro_test_session_state() == 3u && fault_open_substring);
      fault_open_substring = NULL;
      retro_unload_game();

      char later_playlist[4096];
      snprintf(later_playlist, sizeof(later_playlist),
               "%s/chooser-later.m3u", argv[4]);
      FILE *later_file = fopen(later_playlist, "wb");
      assert(later_file && fprintf(later_file, "%s\n%s\n",
                                  argv[12], resource_pair_source) > 0 &&
             !fclose(later_file));
      struct retro_game_info later_game = {later_playlist, NULL, 0u, NULL};
      option_launch = "off";
      phase = PHASE_LOAD; assert(retro_load_game(&later_game));
      assert(disk_control.get_num_images() == 2u &&
             disk_control.set_eject_state(true) &&
             disk_control.set_image_index(1u) &&
             disk_control.set_eject_state(false));
      option_launch = "auto";
      variable_update = true;
      fault_open_substring = ".choice";
      fault_write_substring = ".choice";
      last_launch_warning[0] = '\0';
      retro_reset();
      assert(arc_libretro_test_session_state() == 3u &&
             disk_control.get_image_index() == 1u &&
             fault_open_substring && fault_write_substring &&
             strstr(last_launch_warning,
                    "No unique application: 1 !Nulvane 2 !Qaventra") &&
             !strstr(last_launch_warning, "!System") &&
             !strstr(last_launch_warning, "!Fonts") &&
             !strstr(last_launch_warning, "!Scrap"));
      fault_open_substring = fault_write_substring = NULL;
      retro_unload_game();
      assert(remove(later_playlist) == 0);
      option_launch = "auto";

      /* A renamed copy has the same prepared identity; changing one byte
       * changes that identity without relying on any input behavior. */
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
      option_writes = "off";
      option_launch = "off";
      ArcDigest copy_identity = digest_file(application_copy);
      ArcDigest changed_identity = digest_file(application_changed);
      assert(!strcmp(copy_identity.sha1, application_identity_sha1) &&
             strcmp(changed_identity.sha1, application_identity_sha1));
      phase = PHASE_LOAD; assert(retro_load_game(&application_game));
      assert(!strcmp(arc_libretro_test_content_id(), application_identity_sha1));
      assert_input_resolution();
      char content_independent_resolution[3072];
      snprintf(content_independent_resolution, sizeof(content_independent_resolution),
               "%s", last_input_resolution);
      retro_unload_game();
      phase = PHASE_LOAD; assert(retro_load_game(&application_copy_game));
      assert(!strcmp(arc_libretro_test_content_id(), copy_identity.sha1) &&
             !strcmp(last_input_resolution, content_independent_resolution));
      retro_unload_game();
      phase = PHASE_LOAD; assert(retro_load_game(&application_changed_game));
      assert(!strcmp(arc_libretro_test_content_id(), changed_identity.sha1) &&
             !strcmp(last_input_resolution, content_independent_resolution));
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
      uint8_t launch_cmos_file[ARC_CMOS_RECORD_SIZE];
      read_exact(application_cmos_file, launch_cmos_file,
                 sizeof(launch_cmos_file));
      assert(!memcmp(launch_cmos_file, "ARCCMOS2", 8u) &&
             (launch_cmos_file[16u + 59u] & 7u) == 0u &&
             (launch_cmos_file[16u + 183u] & 7u) == 1u &&
             launch_cmos_file[16u + 42u] == 0x5au);
      ASSERT_NO_SAVE_RAM();

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
             strstr(last_launch_log, "chooser shown") &&
             !arc_engine_test_drive_media(1u, NULL, NULL));
      option_launch = "off";
      retro_reset();
      assert(!arc_engine_test_drive_media(1u, NULL, NULL));
      assert(arc_engine_test_cmos_image(machine_cmos));
      assert((machine_cmos[75] & 7u) == 0u && (machine_cmos[199] & 7u) == 1u);
      retro_unload_game();
      uint8_t launch_cmos_after_reset[ARC_CMOS_RECORD_SIZE];
      read_exact(application_cmos_file, launch_cmos_after_reset,
                 sizeof(launch_cmos_after_reset));
      assert(!memcmp(launch_cmos_after_reset, "ARCCMOS2", 8u) &&
             (launch_cmos_after_reset[16u + 59u] & 7u) == 0u &&
             (launch_cmos_after_reset[16u + 183u] & 7u) == 1u &&
             launch_cmos_after_reset[16u + 42u] == 0x5au);
      ASSERT_NO_SAVE_RAM();

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
               "%s/launch-family/boot-file-3-l.adl", argv[11]);
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

      /* The synthetic ROM reads the published helper and delivers the
       * selected executable. It does not interpret FileSwitch commands. */
      copy_file(chooser_rom, renamed);
      char g1_source[4096];
      snprintf(g1_source, sizeof(g1_source),
               "%s/arcdroid-g1-chooser-l.adl", argv[11]);
      struct retro_game_info g1_game = {g1_source, NULL, 0u, NULL};
      option_launch = "auto";
      phase = PHASE_LOAD; assert(retro_load_game(&g1_game));
      assert(arc_engine_test_drive_media(1u, NULL, NULL) &&
             strstr(last_launch_log, "boot=3") &&
             strstr(last_launch_log, "root-boot=no") &&
             strstr(last_launch_log, "decision=!Nulvane"));
      phase = PHASE_RUN;
      for (unsigned frame = 0u; frame < 520u; ++frame) retro_run();
      assert(last_video_signature == expected_guest_signature[0]);
      retro_unload_game();
      for (unsigned nested_kind = 0u; nested_kind < 2u; ++nested_kind)
      {
         if (nested_kind)
         {
            /* The D-format ROM renders the helper target's visual signature;
             * the checkpoint disc exercises FileSwitch. */
            char d_chooser_rom[4096];
            snprintf(d_chooser_rom, sizeof(d_chooser_rom),
                     "%s/arcdroid-chooser-d-test.rom", argv[11]);
            copy_file(d_chooser_rom, renamed);
         }
         char nested_source[4096], nested_helper[4096], nested_choice[4096];
         char nested_binding[4096];
         snprintf(nested_source, sizeof(nested_source), "%s/%s", argv[11],
                  nested_kind ? "arcdroid-extended-chooser-d.adf" :
                                "arcdroid-nested-chooser-l.adl");
         struct retro_game_info nested_game = {nested_source, NULL, 0u, NULL};
         ArcDigest nested_digest = digest_file(nested_source);
         snprintf(nested_helper, sizeof(nested_helper),
                  "%s/arcdroid/launch/%s.adl", save_path, nested_digest.sha1);
         snprintf(nested_choice, sizeof(nested_choice),
                  "%s/arcdroid/launch/%s.choice", save_path, nested_digest.sha1);
         snprintf(nested_binding, sizeof(nested_binding),
                  "%s/arcdroid/launch/%s.bind", save_path, nested_digest.sha1);
         (void)remove(nested_helper); (void)remove(nested_choice);
         (void)remove(nested_binding);
         option_launch = "app1";
         phase = PHASE_LOAD; assert(retro_load_game(&nested_game));
         assert(arc_engine_test_drive_media(1u, NULL, NULL) &&
                strstr(last_launch_log, "scope=nested") &&
                strstr(last_launch_log, nested_kind ?
                    "decision=Arc\\xA0Games.!Nulvane" :
                    "decision=Folder.!Nulvane") &&
                strstr(last_launch_log, nested_kind ?
                    "address=Arc\\xA0Games.!Nulvane" :
                    "address=Folder.!Nulvane"));
         assert(!strcmp(arc_libretro_test_content_id(), nested_digest.sha1));
         assert_input_resolution();
         char target_independent_resolution[3072];
         snprintf(target_independent_resolution, sizeof(target_independent_resolution),
                  "%s", last_input_resolution);
         if (!nested_kind) assert(!strcmp(nested_digest.sha1, nested_identity_sha1));
         uint8_t nested_record[ARC_LAUNCH_BINDING_MAX_SIZE];
         size_t nested_size = read_bounded(nested_binding, nested_record,
                                           sizeof(nested_record));
         ArcLaunchBinding nested_parsed;
         assert(arc_launch_binding_parse(nested_record, nested_size,
                                         &nested_parsed) &&
                nested_parsed.version == 3u && nested_parsed.ordinal == 0u);
         size_t nested_media_size = 0u;
         uint8_t *nested_media = read_alloc(nested_source, &nested_media_size);
         ArcMediaInfo nested_info;
         ArcLaunchDiscovery nested_discovery;
         uint8_t *nested_expected = malloc(ARC_LAUNCH_DISC_SIZE);
         uint8_t *nested_actual = malloc(ARC_LAUNCH_DISC_SIZE);
         char nested_error[160] = {0};
         assert(nested_media && nested_expected && nested_actual &&
                arc_media_inspect(nested_source, nested_media,
                    nested_media_size, &nested_info, nested_error,
                    sizeof(nested_error)) &&
                arc_launch_discover(nested_media, nested_media_size,
                    &nested_info, NULL, &nested_discovery) &&
                arc_launch_disc_build_ramdisc(&nested_discovery, 0u, true, nested_expected,
                                      nested_error, sizeof(nested_error)));
         read_exact(nested_helper, nested_actual, ARC_LAUNCH_DISC_SIZE);
         assert(!memcmp(nested_expected, nested_actual,
                        ARC_LAUNCH_DISC_SIZE));
         free(nested_media); free(nested_expected); free(nested_actual);
         phase = PHASE_RUN;
         for (unsigned frame = 0u; frame < 520u; ++frame) retro_run();
         uint64_t extended_signature = last_video_signature;
         if (!nested_kind)
            assert(last_video_signature == expected_guest_signature[0]);
         retro_unload_game();

         if (nested_kind)
         {
            (void)remove(nested_binding);
            option_launch = "app2";
            phase = PHASE_LOAD; assert(retro_load_game(&nested_game));
            assert(strstr(last_launch_log,
                          "decision=Arc\\xA0Games.!Qaventra") &&
                   strstr(last_launch_log,
                          "address=Arc\\xA0Games.!Qaventra") &&
                   !strcmp(last_input_resolution, target_independent_resolution) &&
                   !strcmp(arc_libretro_test_content_id(), nested_digest.sha1));
            phase = PHASE_RUN;
            for (unsigned frame = 0u; frame < 520u; ++frame) retro_run();
            assert(last_video_signature != extended_signature);
            retro_unload_game();
            (void)remove(nested_binding);
            write_exact(nested_binding, nested_record, nested_size);
         }

         option_launch = "auto";
         phase = PHASE_LOAD; assert(retro_load_game(&nested_game));
         assert(arc_libretro_test_session_state() == 2u &&
                arc_libretro_test_chooser_rows() == 3u &&
                strstr(last_launch_log, "chooser shown"));
         phase = PHASE_RUN; retro_run();
         joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
         retro_run();
         joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
         assert(arc_libretro_test_session_state() == 3u &&
                arc_engine_test_drive_media(1u, NULL, NULL));
         ArcLaunchChoiceRecord nested_choice_record;
         uint8_t nested_choice_data[ARC_LAUNCH_CHOICE_MAX_SIZE];
         nested_size = read_bounded(nested_choice, nested_choice_data,
                                    sizeof(nested_choice_data));
         assert(arc_launch_choice_parse(nested_choice_data, nested_size,
                                        &nested_choice_record) &&
                nested_choice_record.version == 3u &&
                nested_choice_record.parent.length != 0u);
         retro_unload_game();
         phase = PHASE_LOAD; assert(retro_load_game(&nested_game));
         assert(arc_libretro_test_session_state() == 3u &&
                strstr(last_launch_log, "chooser remembered target"));
         retro_unload_game();

         option_launch_chooser = "ask";
         phase = PHASE_LOAD; assert(retro_load_game(&nested_game));
         assert(arc_libretro_test_session_state() == 2u &&
                arc_libretro_test_chooser_focus() == 0u);
         retro_unload_game();
         option_launch_chooser = "remember";

         /* A valid v3 record whose raw parent changed cannot replay. */
         nested_choice_data[7] = nested_choice_data[7] == '4' ? '5' : '4';
         write_exact(nested_choice, nested_choice_data, nested_size);
         phase = PHASE_LOAD; assert(retro_load_game(&nested_game));
         assert(arc_libretro_test_session_state() == 2u);
         retro_unload_game();
         nested_choice_data[7] = nested_choice_data[7] == '4' ? '5' : '4';
         write_exact(nested_choice, nested_choice_data, nested_size);

         /* A v3 binding records both the selected ordinal and identity. */
         option_launch = "app1";
         phase = PHASE_LOAD; assert(retro_load_game(&nested_game));
         assert(arc_engine_test_drive_media(1u, NULL, NULL));
         retro_unload_game();
         nested_size = read_bounded(nested_binding, nested_record,
                                    sizeof(nested_record));
         assert(nested_size > 4u && nested_record[3] == '1');
         nested_record[3] = '2';
         write_exact(nested_binding, nested_record, nested_size);
         phase = PHASE_LOAD; assert(retro_load_game(&nested_game));
         assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
                strstr(last_launch_log, "binding changed target"));
         retro_unload_game();
         nested_record[3] = '1';
         write_exact(nested_binding, nested_record, nested_size);
         option_launch = "auto";

         (void)remove(nested_choice);
         phase = PHASE_LOAD; assert(retro_load_game(&nested_game));
         assert(arc_libretro_test_session_state() == 2u);
         phase = PHASE_RUN; retro_run();
         for (unsigned row = 0u; row < 2u; ++row)
         {
            keyboard.callback(true, RETROK_DOWN, 0u, 0u); retro_run();
            keyboard.callback(false, RETROK_DOWN, 0u, 0u); retro_run();
         }
         assert(arc_libretro_test_chooser_focus() == 2u);
         keyboard.callback(true, RETROK_RETURN, 0u, 0u); retro_run();
         keyboard.callback(false, RETROK_RETURN, 0u, 0u); retro_run();
         assert(arc_libretro_test_session_state() == 3u &&
                !arc_engine_test_drive_media(1u, NULL, NULL));
         nested_size = read_bounded(nested_choice, nested_choice_data,
                                    sizeof(nested_choice_data));
         assert(arc_launch_choice_parse(nested_choice_data, nested_size,
                                        &nested_choice_record) &&
                nested_choice_record.version == 3u &&
                nested_choice_record.desktop);
         retro_unload_game();
         phase = PHASE_LOAD; assert(retro_load_game(&nested_game));
         assert(arc_libretro_test_session_state() == 3u &&
                !arc_engine_test_drive_media(1u, NULL, NULL) &&
                strstr(last_launch_log, "chooser remembered desktop"));
         retro_unload_game();
         option_launch = "off";
         phase = PHASE_LOAD; assert(retro_load_game(&nested_game));
         assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
                strstr(last_launch_log, "reason=disabled"));
         retro_unload_game();
      }
      option_launch = "auto";
      copy_file(argv[5], renamed);

      char incomplete_nested_source[4096], incomplete_nested_choice[4096];
      char incomplete_nested_binding[4096];
      snprintf(incomplete_nested_source, sizeof(incomplete_nested_source),
               "%s/launch-family/nested-unreadable-d.adf", argv[11]);
      struct retro_game_info incomplete_nested_game = {
         incomplete_nested_source, NULL, 0u, NULL
      };
      ArcDigest incomplete_nested_digest = digest_file(incomplete_nested_source);
      snprintf(incomplete_nested_choice, sizeof(incomplete_nested_choice),
               "%s/arcdroid/launch/%s.choice", save_path,
               incomplete_nested_digest.sha1);
      snprintf(incomplete_nested_binding, sizeof(incomplete_nested_binding),
               "%s/arcdroid/launch/%s.bind", save_path,
               incomplete_nested_digest.sha1);
      (void)remove(incomplete_nested_choice);
      (void)remove(incomplete_nested_binding);
      option_launch = "app1";
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&incomplete_nested_game));
      assert(arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "inspection incomplete") &&
             strstr(last_launch_log, "scan=incomplete"));
      retro_unload_game();
      option_launch = "auto";
      phase = PHASE_LOAD; assert(retro_load_game(&incomplete_nested_game));
      assert(arc_libretro_test_session_state() == 2u &&
             arc_libretro_test_chooser_rows() == 2u);
      phase = PHASE_RUN; retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = true;
      retro_run();
      joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = false;
      assert(arc_libretro_test_session_state() == 3u &&
             file_exists(incomplete_nested_choice));
      retro_unload_game();
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&incomplete_nested_game));
      assert(arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "inspection incomplete") &&
             strstr(last_launch_log, "chooser remembered target"));
      retro_unload_game();

      char no_target_nested_source[4096];
      snprintf(no_target_nested_source, sizeof(no_target_nested_source),
               "%s/launch-family/nested-no-target-unreadable-d.adf", argv[11]);
      struct retro_game_info no_target_nested_game = {
         no_target_nested_source, NULL, 0u, NULL
      };
      warnings = launch_warning_osd_calls;
      phase = PHASE_LOAD; assert(retro_load_game(&no_target_nested_game));
      assert(!arc_engine_test_drive_media(1u, NULL, NULL) &&
             launch_warning_osd_calls == warnings + 1u &&
             strstr(last_launch_warning, "no verified launch target") &&
             strstr(last_launch_log, "scan=incomplete"));
      retro_unload_game();

      char nested_bound_source[4096];
      snprintf(nested_bound_source, sizeof(nested_bound_source),
               "%s/launch-family/nested-77-d.adf", argv[11]);
      struct retro_game_info nested_bound_game = {
         nested_bound_source, NULL, 0u, NULL
      };
      phase = PHASE_LOAD; assert(retro_load_game(&nested_bound_game));
      assert(arc_libretro_test_session_state() == 2u &&
             strstr(last_launch_log, "candidates=[") &&
             strstr(last_launch_log, "...total=77") &&
             strstr(last_launch_log, "applications=["));
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
             launch_warning_osd_calls == warnings &&
             arc_libretro_test_session_state() == 2u &&
             arc_libretro_test_chooser_rows() == 3u &&
             strstr(last_launch_log, "chooser shown"));
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

      /* Every Slice 6a eligibility exit has a stable diagnostic contract.
       * These generated JFDs and zip files contain only invented bytes. */
      char adffs_fixture[4096], package_path[4096];
      snprintf(package_path, sizeof(package_path), "%s/arcdroid/adffs/synthetic.zip",
               system_path);
#define COPY_ADFFS_FIXTURE(name) do { \
         snprintf(adffs_fixture, sizeof(adffs_fixture), "%s/adffs/%s", argv[11], (name)); \
         copy_file(adffs_fixture, package_path); \
      } while (0)
#define ADFFS_GAME(name, variable) \
      char variable##_path[4096]; \
      snprintf(variable##_path, sizeof(variable##_path), "%s/adffs/%s", argv[11], (name)); \
      struct retro_game_info variable = {variable##_path, NULL, 0u, NULL}
#define EXPECT_NATIVE(variable, why, notices, fragment) do { \
         unsigned before_lines = jfd_boot_log_lines; \
         unsigned before_notices = adffs_notice_calls; \
         char expected_line[512]; \
         snprintf(expected_line, sizeof(expected_line), \
                  "JFD boot: path=native reason=%s\n", (why)); \
         phase = PHASE_LOAD; assert(retro_load_game(&(variable))); \
         if (strcmp(last_jfd_boot_log, expected_line) || \
             adffs_notice_calls != before_notices + (notices)) \
            fprintf(stderr, "native decision expected=%s actual=%s notices=%u/%u\n", \
                    expected_line, last_jfd_boot_log, \
                    adffs_notice_calls - before_notices, (notices)); \
         assert(jfd_boot_log_lines == before_lines + 1u && \
                !strcmp(last_jfd_boot_log, expected_line) && \
                adffs_notice_calls == before_notices + (notices)); \
         if ((fragment) != NULL) assert(strstr(last_adffs_notice, (fragment))); \
         retro_unload_game(); \
      } while (0)
#define EXPECT_ADFFS(variable, line) do { \
         unsigned before_lines = jfd_boot_log_lines; \
         unsigned before_notices = adffs_notice_calls; \
         phase = PHASE_LOAD; assert(retro_load_game(&(variable))); \
         assert(jfd_boot_log_lines == before_lines + 1u && \
                !strcmp(last_jfd_boot_log, (line)) && \
                adffs_notice_calls == before_notices); \
         assert(!arc_libretro_test_adffs_released() && \
                disk_control.get_num_images() == 1u); \
         retro_unload_game(); \
         assert(arc_libretro_test_adffs_released() && \
                disk_control.get_num_images() == 0u); \
      } while (0)

      option_writes = "off";
      option_launch = "auto";
      option_jfd_boot = "auto";
      option_adffs_package = "verified";
      COPY_ADFFS_FIXTURE("synthetic.zip");
      /* The authored JFD has a successful D-076 layout plan and a usable
       * embedded script. Routing depends on its structure, never identity. */
      ADFFS_GAME("writable.jfd", routed_plain);
      ADFFS_GAME("writable-gzip.jfd", routed_gzip);
      ADFFS_GAME("writable.zip", routed_zip);
      ADFFS_GAME("Writable-folder.jfd/Writable-folder.jfd", routed_folder);
      ADFFS_GAME("writable-multi.m3u", routed_multi);
      ArcDigest writable_digest_id = digest_file(routed_plain_path);
      char writable_slot_a[4096], writable_slot_b[4096];
      snprintf(writable_slot_a, sizeof(writable_slot_a),
               "%s/arcdroid/jfd/%s.a", save_path, writable_digest_id.sha1);
      snprintf(writable_slot_b, sizeof(writable_slot_b),
               "%s/arcdroid/jfd/%s.b", save_path, writable_digest_id.sha1);
      /* This harness owns these two generated slots. A prior interrupted
       * run must not turn the fresh-overlay assertions into a reload case. */
      if (file_exists(writable_slot_a)) assert(remove(writable_slot_a) == 0);
      if (file_exists(writable_slot_b)) assert(remove(writable_slot_b) == 0);
      char package_dir[4096];
      snprintf(package_dir, sizeof(package_dir), "%s/arcdroid/adffs", system_path);
      observed_opendir_path = package_dir;
#define EXPECT_WRITABLE(media) do { \
         unsigned before_lines = jfd_boot_log_lines; \
         unsigned package_lookups = observed_opendir_calls; \
         unsigned notices = adffs_notice_calls; \
         phase = PHASE_LOAD; assert(retro_load_game(&(media))); \
         assert(jfd_boot_log_lines == before_lines + 1u && \
                !strcmp(last_jfd_boot_log, \
                        "JFD boot: path=native reason=native-writable\n") && \
                observed_opendir_calls == package_lookups && \
                adffs_notice_calls == notices); \
         retro_unload_game(); \
      } while (0)
      uint8_t routed_cmos[ARC_CMOS_MACHINE_SIZE], without_package_cmos[ARC_CMOS_MACHINE_SIZE];
      char routed_drive[41], without_package_drive[41];
      char routed_cmos_path[4096], routed_staged_path[4096];
      char routed_machine_log[128];
      phase = PHASE_LOAD; assert(retro_load_game(&routed_plain));
      assert(arc_engine_test_cmos_image(routed_cmos) &&
             arc_engine_test_drive_media(0u, NULL, routed_drive));
      snprintf(routed_cmos_path, sizeof(routed_cmos_path), "%s",
               arc_libretro_test_cmos_path());
      snprintf(routed_staged_path, sizeof(routed_staged_path), "%s",
               arc_libretro_test_staged_path(0u));
      snprintf(routed_machine_log, sizeof(routed_machine_log), "%s",
               machine_resolution_log);
      retro_unload_game();
      assert(remove(package_path) == 0);
      option_jfd_boot = "adffs";
      phase = PHASE_LOAD; assert(retro_load_game(&routed_plain));
      assert(strstr(last_jfd_boot_log, "reason=no-package"));
      assert(arc_engine_test_cmos_image(without_package_cmos));
      assert(arc_engine_test_drive_media(0u, NULL, without_package_drive));
      assert(!memcmp(routed_cmos, without_package_cmos, sizeof(routed_cmos)));
      assert(!strcmp(routed_drive, without_package_drive));
      assert(!strcmp(routed_cmos_path, arc_libretro_test_cmos_path()));
      assert(!strcmp(routed_staged_path, arc_libretro_test_staged_path(0u)));
      assert(!strcmp(routed_machine_log, machine_resolution_log));
      retro_unload_game();
      option_jfd_boot = "auto";
      COPY_ADFFS_FIXTURE("synthetic.zip");
      EXPECT_WRITABLE(routed_plain);
      EXPECT_WRITABLE(routed_gzip);
      EXPECT_WRITABLE(routed_zip);
      EXPECT_NATIVE(routed_folder, "multi-image", 0u, NULL);
      /* Folder enumeration failed after recognizing a folder set. The
       * selected image still loads alone, but retains multi-image routing. */
      char routed_folder_parent[4096];
      snprintf(routed_folder_parent, sizeof(routed_folder_parent), "%s",
               routed_folder_path);
      char *routed_folder_slash = strrchr(routed_folder_parent, '/');
      assert(routed_folder_slash);
      *routed_folder_slash = '\0';
      fault_opendir_substring = routed_folder_parent;
      unsigned folder_route_lines = jfd_boot_log_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&routed_folder));
      assert(!fault_opendir_substring &&
             disk_control.get_num_images() == 1u &&
             jfd_boot_log_lines == folder_route_lines + 1u &&
             !strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=multi-image\n"));
      retro_unload_game();
      EXPECT_NATIVE(routed_multi, "multi-image", 0u, NULL);
      ADFFS_GAME("writable-digest.jfd", writable_digest);
      ADFFS_GAME("writable-size.jfd", writable_size);
      ADFFS_GAME("writable-game.jfd", writable_game);
      ADFFS_GAME("writable-disc.jfd", writable_disc);
      ADFFS_GAME("writable-count.jfd", writable_count);
      ADFFS_GAME("writable-wp.jfd", writable_wp);
      ADFFS_GAME("writable-no-flag.jfd", writable_no_flag);
      ADFFS_GAME("writable-os31.jfd", writable_os31);
      ADFFS_GAME("writable-os16-17.jfd", writable_os16_17);
      ADFFS_GAME("writable-os17-22.jfd", writable_os17_22);
      ADFFS_GAME("writable-os2.jfd", writable_os2);
      ADFFS_GAME("writable-os18.jfd", writable_os18);
      ADFFS_GAME("writable-os22.jfd", writable_os22);
      ADFFS_GAME("writable-install.jfd", writable_install);
      ADFFS_GAME("writable-layout-fail.jfd", writable_layout_fail);
      ADFFS_GAME("writable-legacy.jfd", writable_legacy);
      EXPECT_WRITABLE(writable_digest);
      EXPECT_WRITABLE(writable_os31);
      EXPECT_WRITABLE(writable_install);
      EXPECT_WRITABLE(writable_os16_17);
      EXPECT_WRITABLE(writable_os17_22);
      EXPECT_ADFFS(writable_wp,
         "JFD boot: path=adffs script=embedded profile=synthetic-test version=\"Synthetic 1.0\"\n");
      EXPECT_ADFFS(writable_no_flag,
         "JFD boot: path=adffs script=embedded profile=synthetic-test version=\"Synthetic 1.0\"\n");
      EXPECT_ADFFS(writable_os2,
         "JFD boot: path=adffs script=embedded profile=synthetic-test version=\"Synthetic 1.0\"\n");
      EXPECT_ADFFS(writable_os18,
         "JFD boot: path=adffs script=embedded profile=synthetic-test version=\"Synthetic 1.0\"\n");
      EXPECT_ADFFS(writable_os22,
         "JFD boot: path=adffs script=embedded profile=synthetic-test version=\"Synthetic 1.0\"\n");
      EXPECT_ADFFS(writable_layout_fail,
         "JFD boot: path=adffs script=embedded profile=synthetic-test version=\"Synthetic 1.0\"\n");
      EXPECT_NATIVE(writable_legacy, "no-script", 0u, NULL);
      option_machine = "Auto"; option_cpu = "Auto";
      phase = PHASE_LOAD; assert(retro_load_game(&writable_digest));
      assert(strstr(machine_resolution_log,
                    "requested=Auto resolved=A3000 reason=default"));
      unsigned plain_machine_lines = machine_resolution_lines;
      option_jfd_boot = "adffs"; variable_update = true;
      retro_reset();
      assert(arc_libretro_test_session_state() == 3u &&
             strstr(last_jfd_boot_log, "path=adffs") &&
             machine_resolution_lines == plain_machine_lines &&
             !arc_engine_test_newio(NULL, NULL, NULL, NULL, NULL));
      option_jfd_boot = "native"; variable_update = true;
      retro_reset();
      assert(arc_libretro_test_session_state() == 3u &&
             strstr(last_jfd_boot_log, "path=native reason=option") &&
             machine_resolution_lines == plain_machine_lines &&
             !arc_engine_test_newio(NULL, NULL, NULL, NULL, NULL));
      retro_unload_game();
      option_jfd_boot = "auto";
      EXPECT_WRITABLE(writable_size);
      EXPECT_WRITABLE(writable_game);
      option_writes = "shadow"; variable_update = true;
      EXPECT_WRITABLE(writable_game);
      char writable_fault_save[4096];
      snprintf(writable_fault_save, sizeof(writable_fault_save),
               "%s/native-writable-preparation-fault", save_path);
      make_directory(writable_fault_save);
      save_dir = writable_fault_save;
      fault_mkdir_substring = "/arcdroid/jfd";
      phase = PHASE_LOAD; assert(retro_load_game(&writable_digest));
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=native-writable\n") &&
             !fault_mkdir_substring &&
             strstr(last_jfd_writes_log, "state=protected") &&
             strstr(last_jfd_writes_log, "reason=overlay-unreadable"));
      retro_unload_game();
      save_dir = save_path;
      option_writes = "off"; variable_update = true;
      EXPECT_NATIVE(writable_disc, "multi-disc", 0u, NULL);
      EXPECT_NATIVE(writable_count, "multi-disc", 0u, NULL);
      option_jfd_boot = "native";
      EXPECT_NATIVE(routed_plain, "option", 0u, NULL);
      option_jfd_boot = "adffs";
      EXPECT_ADFFS(routed_plain,
         "JFD boot: path=adffs script=embedded profile=synthetic-test version=\"Synthetic 1.0\"\n");
      option_jfd_boot = "auto";
      option_launch = "off";
      EXPECT_NATIVE(routed_plain, "launch-option", 0u, NULL);
      option_launch = "app1";
      EXPECT_NATIVE(routed_plain, "launch-option", 0u, NULL);
      option_launch = "auto";
      arc_libretro_test_adffs_accept_test_os(false);
      EXPECT_NATIVE(routed_plain, "os", 0u, NULL);
      arc_libretro_test_adffs_accept_test_os(true);
      EXPECT_WRITABLE(routed_plain);
      option_jfd_boot = "adffs";
      unsigned option_boot_lines = jfd_boot_log_lines;
      phase = PHASE_LOAD; assert(retro_load_game(&routed_plain));
      assert(strstr(last_jfd_boot_log, "path=adffs"));
      option_jfd_boot = "auto";
      variable_update = true;
      phase = PHASE_RUN; retro_run();
      assert(jfd_boot_log_lines == option_boot_lines + 1u &&
             strstr(last_jfd_boot_log, "path=adffs"));
      unsigned routed_machine_lines = machine_resolution_lines;
      retro_reset();
      assert(strstr(last_jfd_boot_log, "reason=native-writable") &&
             machine_resolution_lines == routed_machine_lines &&
             !arc_engine_test_newio(NULL, NULL, NULL, NULL, NULL));
      retro_unload_game();
      option_jfd_boot = "adffs";
      phase = PHASE_LOAD; assert(retro_load_game(&routed_plain));
      assert(strstr(last_jfd_boot_log, "path=adffs"));
      retro_unload_game();
      option_jfd_boot = "auto";
      EXPECT_WRITABLE(routed_plain);
      assert(remove(package_path) == 0);
      EXPECT_WRITABLE(routed_plain);
      option_jfd_boot = "adffs";
      EXPECT_NATIVE(routed_plain, "no-package", 1u, "carries a JASPP boot script");
      COPY_ADFFS_FIXTURE("bad-crc.zip");
      option_jfd_boot = "auto";
      EXPECT_WRITABLE(routed_plain);
      option_jfd_boot = "adffs";
      EXPECT_NATIVE(routed_plain, "invalid-package:member-crc32-mismatch", 1u,
                    "member-crc32-mismatch");
      COPY_ADFFS_FIXTURE("untested.zip");
      option_jfd_boot = "auto";
      EXPECT_WRITABLE(routed_plain);
      option_jfd_boot = "adffs";
      EXPECT_NATIVE(routed_plain, "untested-package", 1u, "untested-package");
      option_jfd_boot = "auto";
      COPY_ADFFS_FIXTURE("synthetic.zip");
      /* Store state never changes the structural choice. Preferred ADFFS
       * still honours a native save, including with writes disabled. */
      *(void **)(&arc_libretro_test_jfd_commit) =
         dlsym(module, "arc_libretro_test_jfd_commit");
      assert(arc_libretro_test_jfd_commit);
      option_writes = "shadow"; variable_update = true;
      phase = PHASE_LOAD; assert(retro_load_game(&routed_plain));
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=native-writable\n") &&
             arc_libretro_test_jfd_commit(0u, 0u, 0x5au));
      retro_unload_game();
      option_jfd_boot = "adffs"; option_writes = "off";
      variable_update = true;
      EXPECT_NATIVE(routed_plain, "native-saves", 0u, NULL);
      option_jfd_boot = "auto"; variable_update = true;
      EXPECT_WRITABLE(routed_plain);
      const char *writable_slot = file_exists(writable_slot_a) ?
                                  writable_slot_a : writable_slot_b;
      FILE *writable_damage = fopen(writable_slot, "r+b");
      assert(writable_damage);
      int slot_byte = fgetc(writable_damage);
      assert(slot_byte >= 0 && fseek(writable_damage, 0L, SEEK_SET) == 0 &&
             fputc(slot_byte ^ 1, writable_damage) >= 0 &&
             fclose(writable_damage) == 0);
      phase = PHASE_LOAD; assert(retro_load_game(&routed_plain));
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=native-writable\n") &&
             strstr(last_jfd_writes_log, "state=protected") &&
             strstr(last_jfd_writes_log, "reason=overlay-invalid"));
      retro_unload_game();
      option_writes = "off";
      observed_opendir_path = NULL;
#undef EXPECT_WRITABLE
      EXPECT_NATIVE(game, "not-jfd", 0u, NULL);

      ADFFS_GAME("multi.m3u", adffs_multi);
      EXPECT_NATIVE(adffs_multi, "multi-image", 0u, NULL);
      /* Former install fixtures are invented media. A seeded legacy tree is
       * opaque to every frontend lifecycle action. */
      char legacy_root[4096], legacy_ready[4096], legacy_damaged[4096],
           legacy_candidate[4096], legacy_file[4096];
      snprintf(legacy_root, sizeof(legacy_root), "%s/arcdroid", save_path);
      make_directory(legacy_root);
      snprintf(legacy_root, sizeof(legacy_root), "%s/arcdroid/install", save_path);
      make_directory(legacy_root);
      snprintf(legacy_ready, sizeof(legacy_ready), "%s/legacy-ready", legacy_root);
      snprintf(legacy_damaged, sizeof(legacy_damaged), "%s/legacy-damaged", legacy_root);
      snprintf(legacy_candidate, sizeof(legacy_candidate), "%s/legacy-candidate", legacy_root);
      make_directory(legacy_ready); make_directory(legacy_damaged);
      make_directory(legacy_candidate);
      static const uint8_t legacy_bytes[] = {0x41u, 0x49u, 0x4du, 0x46u, 0x00u, 0x7fu};
      snprintf(legacy_file, sizeof(legacy_file), "%s/ready.a", legacy_ready);
      write_exact(legacy_file, legacy_bytes, sizeof(legacy_bytes));
      snprintf(legacy_file, sizeof(legacy_file), "%s/ready.a", legacy_damaged);
      write_exact(legacy_file, legacy_bytes, sizeof(legacy_bytes) - 1u);
      snprintf(legacy_file, sizeof(legacy_file), "%s/disc.g0.hdf.tmp", legacy_candidate);
      write_exact(legacy_file, legacy_bytes, sizeof(legacy_bytes));
      TreeSnapshot legacy_before = snapshot_tree(legacy_root);
      ADFFS_GAME("install.m3u", retired_set);
      ADFFS_GAME("install-first.m3u", retired_incomplete);
      ADFFS_GAME("install.zip", retired_zip);
      ADFFS_GAME("Install.jfd/Install.jfd", retired_folder);
      ADFFS_GAME("install-1.jfd", retired_one);
      ADFFS_GAME("install-single-hd.jfd", retired_hd);
      char retired_duplicate_path[4096], retired_extra_path[4096];
      snprintf(retired_duplicate_path, sizeof(retired_duplicate_path),
               "%s/retired-duplicate.m3u", save_path);
      snprintf(retired_extra_path, sizeof(retired_extra_path),
               "%s/retired-extra.m3u", save_path);
      FILE *retired_playlist = fopen(retired_duplicate_path, "wb");
      assert(retired_playlist &&
             fprintf(retired_playlist, "%s\n%s\n", retired_one_path,
                     retired_one_path) > 0 && fclose(retired_playlist) == 0);
      retired_playlist = fopen(retired_extra_path, "wb");
      assert(retired_playlist &&
             fprintf(retired_playlist, "%s\n%s\n%s\n%s\n",
                     retired_one_path, retired_one_path,
                     retired_hd_path, retired_one_path) > 0 &&
             fclose(retired_playlist) == 0);
      struct retro_game_info retired_duplicate =
         {retired_duplicate_path, NULL, 0u, NULL};
      struct retro_game_info retired_extra =
         {retired_extra_path, NULL, 0u, NULL};
      COPY_ADFFS_FIXTURE("synthetic.zip");
      option_hard_disc = "auto";
      option_machine = "A3020"; option_cpu = "ARM250";
      phase = PHASE_LOAD; assert(retro_load_game(&retired_set));
      char legacy_cmos_path[4096];
      snprintf(legacy_cmos_path, sizeof(legacy_cmos_path), "%s",
               arc_libretro_test_cmos_path());
      retro_unload_game();
      write_exact(legacy_cmos_path, legacy_bytes, sizeof(legacy_bytes));
      uint64_t legacy_cmos_hash = hash_file(legacy_cmos_path);
      option_machine = "Auto"; option_cpu = "Auto";
      observed_opendir_path = package_dir;
      unsigned package_before = observed_opendir_calls;
      install_prefix_accesses = 0u;
      for (unsigned stale = 0u; stale < 2u; ++stale) {
         stale_install_option_value = stale ? "malformed!" : "persistent";
         stale_install_option_queries = 0u;
         phase = PHASE_LOAD; assert(retro_load_game(&retired_set));
         assert(!strcmp(last_jfd_boot_log,
                        "JFD boot: path=native reason=multi-image\n"));
         retro_reset();
         assert(!strcmp(last_jfd_boot_log,
                        "JFD boot: path=native reason=multi-image\n"));
         retro_unload_game();
         phase = PHASE_LOAD; assert(retro_load_game(&retired_hd));
         assert(!strcmp(last_jfd_boot_log,
                        "JFD boot: path=native reason=hard-disc\n"));
         retro_reset();
         retro_unload_game();
         assert(stale_install_option_queries == 0u &&
                hash_file(legacy_cmos_path) == legacy_cmos_hash);
      }
      EXPECT_NATIVE(retired_set, "multi-image", 0u, NULL);
      EXPECT_NATIVE(retired_incomplete, "multi-image", 0u, NULL);
      EXPECT_NATIVE(retired_zip, "multi-image", 0u, NULL);
      EXPECT_NATIVE(retired_folder, "multi-image", 0u, NULL);
      EXPECT_NATIVE(retired_duplicate, "multi-image", 0u, NULL);
      EXPECT_NATIVE(retired_extra, "multi-image", 0u, NULL);
      EXPECT_NATIVE(retired_one, "multi-disc", 0u, NULL);
      size_t retired_variant_size = 0u;
      uint8_t *retired_variant_bytes =
         read_alloc(retired_one_path, &retired_variant_size);
      assert(retired_variant_size > 20u);
      retired_variant_bytes[16] ^= 0x40u; /* header identity is no guard */
      char retired_variant_path[4096];
      snprintf(retired_variant_path, sizeof(retired_variant_path),
               "%s/retired-variant.jfd", save_path);
      write_exact(retired_variant_path, retired_variant_bytes,
                  retired_variant_size);
      free(retired_variant_bytes);
      struct retro_game_info retired_variant =
         {retired_variant_path, NULL, 0u, NULL};
      EXPECT_NATIVE(retired_variant, "multi-disc", 0u, NULL);
      option_jfd_boot = "native"; option_launch = "off";
      EXPECT_NATIVE(retired_set, "multi-image", 0u, NULL);
      EXPECT_NATIVE(retired_one, "multi-disc", 0u, NULL);
      option_jfd_boot = "auto"; option_launch = "auto";
      EXPECT_NATIVE(retired_set, "multi-image", 0u, NULL);
      char with_package_route[256], with_package_machine[256];
      snprintf(with_package_route, sizeof(with_package_route), "%s",
               last_jfd_boot_log);
      snprintf(with_package_machine, sizeof(with_package_machine), "%s",
               machine_resolution_log);
      assert(remove(package_path) == 0);
      EXPECT_NATIVE(retired_set, "multi-image", 0u, NULL);
      assert(!strcmp(with_package_route, last_jfd_boot_log) &&
             !strcmp(with_package_machine, machine_resolution_log));
      COPY_ADFFS_FIXTURE("synthetic.zip");
      assert(observed_opendir_calls == package_before &&
             install_prefix_accesses == 0u &&
             same_snapshot(legacy_before, snapshot_tree(legacy_root)));
      phase = PHASE_LOAD; assert(retro_load_game(&retired_set));
      assert(strstr(machine_resolution_log, "resolved=A3000 reason=default"));
      retro_reset();
      assert(strstr(last_jfd_boot_log, "reason=multi-image"));
      assert(disk_control.set_eject_state(true));
      assert(disk_control.set_image_index(1u));
      assert(disk_control.set_eject_state(false));
      retro_unload_game();
      assert(install_prefix_accesses == 0u &&
             same_snapshot(legacy_before, snapshot_tree(legacy_root)) &&
             hash_file(legacy_cmos_path) == legacy_cmos_hash);
      observed_opendir_path = NULL;
      option_hard_disc = "auto";
      EXPECT_NATIVE(retired_hd, "hard-disc", 0u, NULL);
      option_launch = "off";
      EXPECT_NATIVE(retired_hd, "hard-disc", 0u, NULL);
      option_launch = "auto";
      arc_libretro_test_adffs_accept_test_os(false);
      EXPECT_NATIVE(retired_hd, "hard-disc", 0u, NULL);
      arc_libretro_test_adffs_accept_test_os(true);
      assert(strstr(machine_resolution_log, "resolved=A3020 reason=hd-media"));
      option_hard_disc = "off";
      option_jfd_boot = "adffs";
      EXPECT_ADFFS(retired_hd,
         "JFD boot: path=adffs script=builtin profile=synthetic-test version=\"Synthetic 1.0\"\n");
      option_jfd_boot = "auto";
      option_hard_disc = "auto";
      option_machine = "A3010"; option_cpu = "ARM250";
      EXPECT_ADFFS(retired_hd,
         "JFD boot: path=adffs script=builtin profile=synthetic-test version=\"Synthetic 1.0\"\n");
      option_machine = "Auto"; option_cpu = "Auto";
      assert(install_prefix_accesses == 0u &&
             same_snapshot(legacy_before, snapshot_tree(legacy_root)));
      ADFFS_GAME("disc-two.jfd", adffs_disc_two);
      EXPECT_NATIVE(adffs_disc_two, "multi-disc", 0u, NULL);
      ADFFS_GAME("count-two.jfd", adffs_count_two);
      EXPECT_NATIVE(adffs_count_two, "multi-disc", 0u, NULL);
      ADFFS_GAME("eligible.jfd", adffs_eligible);
      size_t newio_eligible_size = 0u;
      uint8_t *newio_eligible_bytes = read_alloc(adffs_eligible_path,
                                                  &newio_eligible_size);
      static const char *const adffs_newio_machines[] = {
         "A3010", "A3020", "A4000"
      };
      option_cpu = "ARM250";
      variable_update = true;
      for (unsigned machine = 0u; machine < 3u; ++machine) {
         option_machine = adffs_newio_machines[machine];
         unsigned newio_package_lines = adffs_package_log_lines;
         unsigned newio_boot_lines = jfd_boot_log_lines;
         unsigned newio_notices = adffs_notice_calls;
         phase = PHASE_LOAD; assert(retro_load_game(&adffs_eligible));
         if (jfd_boot_log_lines != newio_boot_lines + 1u ||
             strcmp(last_jfd_boot_log,
                    "JFD boot: path=adffs script=builtin profile=synthetic-test version=\"Synthetic 1.0\"\n") ||
             adffs_package_log_lines != newio_package_lines + 1u ||
             adffs_notice_calls != newio_notices ||
             !arc_libretro_test_adffs_carrier_matches(newio_eligible_bytes,
                                                     newio_eligible_size))
            fprintf(stderr, "New-IO ADFFS %s: boot=%s package_delta=%u notice_delta=%u carrier=%d\n",
                    option_machine, last_jfd_boot_log,
                    adffs_package_log_lines - newio_package_lines,
                    adffs_notice_calls - newio_notices,
                    arc_libretro_test_adffs_carrier_matches(newio_eligible_bytes,
                                                            newio_eligible_size));
         assert(jfd_boot_log_lines == newio_boot_lines + 1u &&
                !strcmp(last_jfd_boot_log,
                        "JFD boot: path=adffs script=builtin profile=synthetic-test version=\"Synthetic 1.0\"\n") &&
                adffs_package_log_lines == newio_package_lines + 1u &&
                adffs_notice_calls == newio_notices &&
                arc_libretro_test_adffs_carrier_matches(newio_eligible_bytes,
                                                        newio_eligible_size) &&
                disk_control.get_num_images() == 1u);
         retro_unload_game();
      }
      free(newio_eligible_bytes);
      option_machine = "Auto";
      option_cpu = "Auto";
      variable_update = true;
      ADFFS_GAME("eligible-hd.jfd", adffs_hd);
      observed_opendir_path = package_dir;
      option_hard_disc = "auto";
      option_writes = "shadow";
      ADFFS_GAME("eligible-hd-rw.jfd", adffs_hd_rw);
      option_machine = "A3020"; option_cpu = "ARM250";
      for (unsigned hd_variant = 0u; hd_variant < 2u; ++hd_variant) {
         struct retro_game_info *hd_game = hd_variant ? &adffs_hd_rw : &adffs_hd;
         phase = PHASE_LOAD; assert(retro_load_game(hd_game));
         assert(strstr(machine_resolution_log,
                       "requested=A3020 resolved=A3020 reason=option") &&
                !strcmp(last_jfd_boot_log,
                        "JFD boot: path=native reason=hard-disc\n") &&
                strstr(hard_disc_policy_log,
                       "attach=yes mode=writable reason=attached"));
         retro_unload_game();
      }
      option_machine = "Auto"; option_cpu = "Auto";
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_hd));
      char damaged_hd_manifest[4096], damaged_hd_dir[4096];
      snprintf(damaged_hd_dir, sizeof(damaged_hd_dir),
               "%s/arcdroid/hd/%s", save_path,
               arc_libretro_test_content_id());
      snprintf(damaged_hd_manifest, sizeof(damaged_hd_manifest),
               "%s/ready.a", damaged_hd_dir);
      retro_unload_game();
      FILE *damaged_hd_file = fopen(damaged_hd_manifest, "r+b");
      assert(damaged_hd_file);
      int damaged_hd_byte = fgetc(damaged_hd_file);
      assert(damaged_hd_byte >= 0 &&
             fseek(damaged_hd_file, 20L, SEEK_SET) == 0 &&
             fputc(damaged_hd_byte ^ 1, damaged_hd_file) >= 0 &&
             fclose(damaged_hd_file) == 0);
      TreeSnapshot damaged_hd_bytes = snapshot_tree(damaged_hd_dir);
      option_writes = "off"; variable_update = true;
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_hd));
      assert(!strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=hard-disc\n") &&
             strstr(hard_disc_policy_log,
                    "attach=no mode=none reason=damaged") &&
             same_snapshot(damaged_hd_bytes, snapshot_tree(damaged_hd_dir)));
      retro_unload_game();
      option_writes = "off"; variable_update = true;
      observed_opendir_path = NULL;
      option_cpu = "ARM2";
      variable_update = true;
      option_jfd_boot = "native";
      EXPECT_NATIVE(adffs_eligible, "option", 0u, NULL);
      option_jfd_boot = "auto";
      option_launch = "app1";
      EXPECT_NATIVE(adffs_eligible, "launch-option", 0u, NULL);
      option_launch = "auto";
      arc_libretro_test_adffs_accept_test_os(false);
      EXPECT_NATIVE(adffs_eligible, "os", 0u, NULL);
      arc_libretro_test_adffs_accept_test_os(true);

      char valid_first[4096], valid_second[4096];
      snprintf(valid_first, sizeof(valid_first), "%s/arcdroid/adffs/A-valid.ZIP",
               system_path);
      snprintf(valid_second, sizeof(valid_second), "%s/arcdroid/adffs/b-valid.ZiP",
               system_path);
      assert(remove(package_path) == 0);
      snprintf(adffs_fixture, sizeof(adffs_fixture), "%s/adffs/synthetic.zip", argv[11]);
      copy_file(adffs_fixture, valid_first);
      copy_file(adffs_fixture, valid_second);
      unsigned package_lines = adffs_package_log_lines;
      EXPECT_ADFFS(adffs_eligible,
         "JFD boot: path=adffs script=builtin profile=synthetic-test version=\"Synthetic 1.0\"\n");
      assert(adffs_package_log_lines == package_lines + 1u &&
             !strcmp(last_adffs_package_log, "ADFFS package: file=A-valid.ZIP\n") &&
             remove(valid_first) == 0 && remove(valid_second) == 0);

      ADFFS_GAME("embedded-only.jfd", adffs_embedded);
      EXPECT_NATIVE(adffs_embedded, "no-package", 1u,
                    "carries a JASPP boot script");
      ADFFS_GAME("game-only.jfd", adffs_game_only);
      EXPECT_NATIVE(adffs_game_only, "no-package", 1u,
                    "may boot better with ADFFS");

      COPY_ADFFS_FIXTURE("bad-crc.zip");
      EXPECT_NATIVE(adffs_eligible, "invalid-package:member-crc32-mismatch", 1u,
                    "invalid-package:member-crc32-mismatch");
      COPY_ADFFS_FIXTURE("missing-obey.zip");
      EXPECT_NATIVE(adffs_eligible,
                    "invalid-package:missing-required-member-adffs-obey", 1u,
                    "missing-required-member-adffs-obey");
      COPY_ADFFS_FIXTURE("missing-runtypes.zip");
      EXPECT_NATIVE(adffs_eligible,
                    "invalid-package:missing-required-member-adffs-runtypes",
                    1u, "missing-required-member-adffs-runtypes");
      COPY_ADFFS_FIXTURE("missing-fixobey.zip");
      EXPECT_NATIVE(adffs_eligible,
                    "invalid-package:missing-required-member-adffs-fixobey",
                    1u, "missing-required-member-adffs-fixobey");
      COPY_ADFFS_FIXTURE("untested.zip");
      EXPECT_NATIVE(adffs_eligible, "untested-package", 1u, "untested-package");
      option_adffs_package = "untested";
      EXPECT_ADFFS(adffs_eligible,
         "JFD boot: path=adffs script=builtin profile=untested version=\"Synthetic 1.0\"\n");
      option_adffs_package = "verified";

      COPY_ADFFS_FIXTURE("synthetic.zip");
      ADFFS_GAME("no-script.jfd", adffs_no_script);
      EXPECT_NATIVE(adffs_no_script, "no-script", 0u, NULL);
      ADFFS_GAME("missing-resource.jfd", adffs_missing);
      EXPECT_NATIVE(adffs_missing, "missing-resource:!ADFFS/ExTrAs/Absent/module",
                    1u, "missing-resource:!ADFFS/ExTrAs/Absent/module");
      ADFFS_GAME("comment-reference.jfd", adffs_comment_reference);
      EXPECT_ADFFS(adffs_comment_reference,
         "JFD boot: path=adffs script=embedded profile=synthetic-test version=\"Synthetic 1.0\"\n");
      unsigned unreadable_lines = jfd_boot_log_lines;
      unsigned unreadable_notices = adffs_notice_calls;
      arc_libretro_test_fail_adffs_content_read(true);
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_eligible));
      assert(jfd_boot_log_lines == unreadable_lines + 1u &&
             !strcmp(last_jfd_boot_log,
                     "JFD boot: path=native reason=content-unreadable\n") &&
             adffs_notice_calls == unreadable_notices &&
             !arc_libretro_test_adffs_content_read_failure_pending());
      retro_unload_game();
      ADFFS_GAME("too-large.jfd", adffs_too_large);
      arc_libretro_test_fail_adffs_content_read(true);
      EXPECT_NATIVE(adffs_too_large, "too-large", 1u, "too-large");
      assert(arc_libretro_test_adffs_content_read_failure_pending());
      arc_libretro_test_fail_adffs_content_read(false);
      ADFFS_GAME("closure-too-large.jfd", adffs_closure_too_large);
      EXPECT_NATIVE(adffs_closure_too_large, "too-large", 1u, "too-large");
      ADFFS_GAME("decoded-too-large.jfd", adffs_decoded_too_large);
      EXPECT_NATIVE(adffs_decoded_too_large, "decoded-too-large", 1u,
                    "decoded-too-large");
      ADFFS_GAME("capacity.jfd", adffs_capacity);
      EXPECT_ADFFS(adffs_capacity,
         "JFD boot: path=adffs script=builtin profile=synthetic-test version=\"Synthetic 1.0\"\n");
      ADFFS_GAME("decoded-limit.jfd", adffs_decoded_limit);
      EXPECT_ADFFS(adffs_decoded_limit,
         "JFD boot: path=adffs script=builtin profile=synthetic-test version=\"Synthetic 1.0\"\n");
      EXPECT_ADFFS(adffs_embedded,
         "JFD boot: path=adffs script=embedded profile=synthetic-test version=\"Synthetic 1.0\"\n");
      EXPECT_ADFFS(adffs_eligible,
         "JFD boot: path=adffs script=builtin profile=synthetic-test version=\"Synthetic 1.0\"\n");

#undef EXPECT_ADFFS
#undef EXPECT_NATIVE
#undef ADFFS_GAME
#undef COPY_ADFFS_FIXTURE

      /* Slice 6a: pre-warm every native write mode for this exact content so
       * the following ADFFS sessions may not create any native-path files.
       * The user package and JFD remain byte-identical throughout. */
      struct retro_game_info adffs_game = {argv[14], NULL, 0u, NULL};
      const char *write_modes[] = {"shadow", "original", "off"};
      uint64_t package_hash = hash_file(package_path);
      uint64_t adffs_source_hash = hash_file(argv[14]);
      char adffs_native_save[4096], adffs_memory_save[4096];
      snprintf(adffs_native_save, sizeof(adffs_native_save),
               "%s/adffs-native-save-%ld", argv[4], (long)getpid());
      snprintf(adffs_memory_save, sizeof(adffs_memory_save),
               "%s/adffs-memory-save-%ld", argv[4], (long)getpid());
      make_directory(adffs_native_save);
      make_directory(adffs_memory_save);
      save_dir = adffs_native_save;
      option_jfd_boot = "native";
      option_launch = "auto";
      for (unsigned mode = 0u; mode < 3u; ++mode)
      {
         option_writes = write_modes[mode];
         unsigned boot_lines = jfd_boot_log_lines;
         phase = PHASE_LOAD;
         if (!retro_load_game(&adffs_game))
         {
            fprintf(stderr, "native ADFFS fixture load failed (mode %s): %s\n",
                    write_modes[mode], last_message);
            abort();
         }
         assert(jfd_boot_log_lines == boot_lines + 1u &&
                !strcmp(last_jfd_boot_log, "JFD boot: path=native reason=option\n"));
         retro_reset();
         retro_unload_game();
      }
      /* CMOS is the one native-path write allowed by AC-10; its emulated RTC
       * byte is time-dependent. Compare every other file against a native
       * run of the same content and operations. */
      TreeSnapshot adffs_native_save_result =
         snapshot_tree_filtered(adffs_native_save, true);
      char adffs_fixture_dir[4096];
      snprintf(adffs_fixture_dir, sizeof(adffs_fixture_dir), "%s/adffs", argv[11]);
      TreeSnapshot adffs_system_before = snapshot_tree(system_path);
      TreeSnapshot adffs_content_before = snapshot_tree(adffs_fixture_dir);

      /* Both generated D-format discs are memory-only and immutable for all
       * Disk Writes settings. Reset must rebuild byte-identical images, and
       * every frontend mutation callback is refused without changing them. */
      option_jfd_boot = "auto";
      option_adffs_package = "verified";
      save_dir = adffs_memory_save;
      char carrier_digest[41] = {0}, support_digest[41] = {0};
      for (unsigned mode = 0u; mode < 3u; ++mode)
      {
         option_writes = write_modes[mode];
         unsigned boot_lines = jfd_boot_log_lines;
         phase = PHASE_LOAD; assert(retro_load_game(&adffs_game));
         assert(jfd_boot_log_lines == boot_lines + 1u &&
                !strcmp(last_jfd_boot_log,
                   "JFD boot: path=adffs script=builtin profile=synthetic-test version=\"Synthetic 1.0\"\n") &&
                disk_control.get_num_images() == 1u &&
                disk_control.get_image_index() == 0u &&
                !disk_control.get_eject_state());
         char carrier_now[41], support_now[41];
         bool carrier_write_protected = false;
         bool support_write_protected = false;
         assert(arc_engine_test_drive_media(0u, &carrier_write_protected,
                                            carrier_now));
         assert(carrier_write_protected);
         assert(arc_engine_test_drive_media(1u, &support_write_protected,
                                            support_now));
         assert(support_write_protected);
         if (!mode)
         {
            memcpy(carrier_digest, carrier_now, sizeof(carrier_digest));
            memcpy(support_digest, support_now, sizeof(support_digest));
         }
         else
            assert(!strcmp(carrier_digest, carrier_now) &&
                   !strcmp(support_digest, support_now));
         assert(!disk_control.set_eject_state(true) &&
                !disk_control.set_image_index(0u) &&
                !disk_control.replace_image_index(0u, NULL) &&
                !disk_control.add_image_index());
         char unchanged_carrier[41], unchanged_support[41];
         carrier_write_protected = false;
         support_write_protected = false;
         assert(arc_engine_test_drive_media(0u, &carrier_write_protected,
                                            unchanged_carrier));
         assert(carrier_write_protected);
         assert(arc_engine_test_drive_media(1u, &support_write_protected,
                                            unchanged_support));
         assert(support_write_protected &&
                !strcmp(carrier_now, unchanged_carrier) &&
                !strcmp(support_now, unchanged_support));
         retro_reset();
         carrier_write_protected = false;
         support_write_protected = false;
         assert(arc_engine_test_drive_media(0u, &carrier_write_protected,
                                            unchanged_carrier));
         assert(carrier_write_protected);
         assert(arc_engine_test_drive_media(1u, &support_write_protected,
                                            unchanged_support));
         assert(support_write_protected &&
                !strcmp(carrier_digest, unchanged_carrier) &&
                !strcmp(support_digest, unchanged_support));
         retro_unload_game();
         assert(!arc_engine_test_drive_media(0u, NULL, NULL) &&
                !arc_engine_test_drive_media(1u, NULL, NULL) &&
                open_files == 0u && open_dirs == 0u &&
                hash_file(package_path) == package_hash &&
                hash_file(argv[14]) == adffs_source_hash);
      }
      TreeSnapshot adffs_system_after = snapshot_tree(system_path);
      TreeSnapshot adffs_save_after =
         snapshot_tree_filtered(adffs_memory_save, true);
      TreeSnapshot adffs_content_after = snapshot_tree(adffs_fixture_dir);
      if (!same_snapshot(adffs_system_before, adffs_system_after) ||
          !same_snapshot(adffs_native_save_result, adffs_save_after) ||
          !same_snapshot(adffs_content_before, adffs_content_after) ||
          adffs_log_leak)
      {
         fprintf(stderr,
            "ADFFS host-write snapshot changed: system=%u/%u save=%u/%u content=%u/%u leak=%u\n",
            adffs_system_before.files, adffs_system_after.files,
            adffs_native_save_result.files, adffs_save_after.files,
            adffs_content_before.files, adffs_content_after.files,
            adffs_log_leak ? 1u : 0u);
         abort();
      }

      /* Switching in either direction cannot retain a memory media source. */
      save_dir = save_path;
      option_writes = "off";
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_game));
      retro_unload_game();
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      assert(arc_engine_test_drive_media(0u, &write_protected, mounted_digest) &&
             strcmp(mounted_digest, carrier_digest));
      retro_unload_game();
      phase = PHASE_LOAD; assert(retro_load_game(&game));
      retro_unload_game();
      phase = PHASE_LOAD; assert(retro_load_game(&adffs_game));
      assert(arc_engine_test_drive_media(0u, &write_protected, mounted_digest) &&
             !strcmp(mounted_digest, carrier_digest));
      retro_unload_game();

      option_launch = "auto";
      option_jfd_boot = "auto";
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
   assert(set_variable_calls == 0u && stale_input_option_queries == 0u);
   if (probe_cached_video)
      assert(cached_video_av_reads && cached_video_geometry_reads &&
             cached_video_duplicate_reads);
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
   if (probe_cached_video)
      printf("cached frame rereads: total=%u A/V=%u geometry=%u duplicate=%u\n",
             cached_video_reads, cached_video_av_reads,
             cached_video_geometry_reads, cached_video_duplicate_reads);
   printf("VIDC messages: OSD ext/legacy=%u/%u, transient info logs=%u, warning logs=%u\n",
          vidc_osd_ext_calls, vidc_osd_legacy_calls,
          vidc_info_log_lines, vidc_warning_log_lines);
   return 0;
}
