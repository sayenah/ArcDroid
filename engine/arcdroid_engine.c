/* ArcDroid-owned Arculator session owner. GPL-2.0-only. */
#include "arcdroid_engine.h"
#include "arcdroid_platform.h"

#include "arc.h"
#include "arm.h"
#include "cmos.h"
#include "config.h"
#include "disc.h"
#include "disc_adf.h"
#include "ds2401.h"
#include "ioc.h"
#include "ioeb.h"
#include "joystick.h"
#include "keyboard.h"
#include "mem.h"
#include "memc.h"
#include "plat_input.h"
#include "plat_joystick.h"
#include "plat_sound.h"
#include "plat_video.h"
#include "sound.h"
#include "timer.h"
#include "vidc.h"
#include "video.h"
#include "wd1770.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int romset = ROM_RISCOS_311;
int romset_available_mask;
int firstfull;
int memsize = 4096;
int speed_mhz = 8;
int updatemips;
int inssec;
char exname[512];
char machine[7] = "a3000";
char machine_config_name[256];
char machine_config_file[256];
char hd_fn[2][512];
int hd_spt[2], hd_hpc[2], hd_cyl[2];
int monitor_type = MONITOR_STANDARD;
int machine_type = MACHINE_TYPE_NORMAL;
uint32_t unique_id = UINT32_C(0x41524344);
char joystick_if[16];
char _5th_column_fn[512];
int support_rom_enabled;

static bool arc_active;
static bool arc_fault;
static bool arc_fault_boundary;
static bool arc_machine_live;
static jmp_buf arc_fault_jump;
static char arc_fault_text[512];
static uint8_t *arc_saved_firmware;
static size_t arc_saved_firmware_size;
static char arc_saved_media[4096];
static uint8_t arc_saved_cmos[ARC_CMOS_MACHINE_SIZE];
static double arc_last_refresh = 50.0;
static ArcEngineLogCallback arc_log_callback;
static void *arc_log_opaque;

static void arc_latch_fault(const char *format, va_list arguments)
{
   if (!arc_fault)
   {
      vsnprintf(arc_fault_text, sizeof(arc_fault_text), format, arguments);
      arc_fault = true;
   }
   if (arc_fault_boundary)
      longjmp(arc_fault_jump, 1);
}

void rpclog(const char *format, ...)
{
   char message[1024];
   va_list arguments;
   if (!arc_log_callback || !format) return;
   va_start(arguments, format);
   vsnprintf(message, sizeof(message), format, arguments);
   va_end(arguments);
   arc_log_callback(arc_log_opaque, message);
}

void arc_engine_set_log_callback(ArcEngineLogCallback callback, void *opaque)
{
   arc_log_callback = callback;
   arc_log_opaque = opaque;
}
void fatal(const char *format, ...)
{
   va_list arguments;
   va_start(arguments, format);
   arc_latch_fault(format, arguments);
   va_end(arguments);
}
void error(const char *format, ...)
{
   va_list arguments;
   va_start(arguments, format);
   arc_latch_fault(format, arguments);
   va_end(arguments);
}
void arc_print_error(const char *format, ...)
{
   va_list arguments;
   va_start(arguments, format);
   arc_latch_fault(format, arguments);
   va_end(arguments);
}

void updateins(void) {}

void arc_set_cpu(int cpu, int memc)
{
   static const int memc_speed[] = {8, 8, 12, 16, 20, 24};
   static const int memc1[] = {1, 0, 0, 0, 0, 0};
   if (cpu != 0 || memc < 0 || memc >= (int)(sizeof(memc_speed) / sizeof(memc_speed[0])))
   {
      fatal("Unsupported Slice 1 CPU/MEMC selection");
      return;
   }
   arm_cpu_type = 0;
   memc_type = memc;
   arm_mem_speed = memc_speed[memc];
   arm_cpu_speed = arm_mem_speed;
   memc_is_memc1 = memc1[memc];
   arm_has_swp = 0;
   arm_has_cp15 = 0;
   speed_mhz = arm_cpu_speed;
   ref8m_period = (arm_cpu_speed * 1024) / 8;
   mem_updatetimings();
}

int machine_is_a500(void) { return 0; }
char *config_get_romset_name(int value) { (void)value; return "riscos311"; }
char *config_get_cmos_name(int value, int fdc) { (void)value; (void)fdc; return "riscos3_old"; }
float config_get_float(int global, const char *head, const char *name, float fallback)
{ (void)global; (void)head; (void)name; return fallback; }
int config_get_int(int global, const char *head, const char *name, int fallback)
{ (void)global; (void)head; (void)name; return fallback; }
const char *config_get_string(int global, const char *head, const char *name, const char *fallback)
{ (void)global; (void)head; (void)name; return fallback; }
void config_set_float(int global, const char *head, const char *name, float value)
{ (void)global; (void)head; (void)name; (void)value; }
void config_set_int(int global, const char *head, const char *name, int value)
{ (void)global; (void)head; (void)name; (void)value; }
void config_set_string(int global, const char *head, const char *name, char *value)
{ (void)global; (void)head; (void)name; (void)value; }
int config_free_section(int global, const char *name) { (void)global; (void)name; return 0; }
void add_config_callback(void (*load)(void), void (*save)(void), void (*loaded)(void))
{ (void)load; (void)save; (void)loaded; }
char *get_filename(char *path)
{
   char *slash = path ? strrchr(path, '/') : NULL;
   char *backslash = path ? strrchr(path, '\\') : NULL;
   char *last = slash > backslash ? slash : backslash;
   return last ? last + 1 : path;
}
char *get_extension(char *path)
{
   char *dot = path ? strrchr(path, '.') : NULL;
   return dot ? dot + 1 : path + strlen(path);
}
void append_filename(char *destination, const char *left, const char *right, int size)
{ if (destination && size > 0) snprintf(destination, (size_t)size, "%s%s", left ? left : "", right ? right : ""); }
void append_slash(char *path, int size) { (void)path; (void)size; }
void put_backslash(char *path) { (void)path; }
void config_load(int global, char *path) { (void)global; (void)path; }
void config_save(int global, char *path) { (void)global; (void)path; }
void config_dump(int global) { (void)global; }
void loadconfig(void) {}
void saveconfig(void) {}

static void arc_machine_destroy(void)
{
   if (!arc_machine_live) return;
   for (int drive = 0; drive < 4; ++drive) disc_close(drive);
   sound_dev_close();
   input_close();
   joystick_close();
   closevideo();
   video_renderer_close();
   free(ram); ram = NULL;
   free(rom); rom = NULL;
   free(rom_arcrom); rom_arcrom = NULL;
   free(rom_5th_column); rom_5th_column = NULL;
   timer_reset();
   arc_machine_live = false;
}

static bool arc_machine_create(char *failure, size_t failure_size)
{
   arc_fault = false;
   arc_fault_text[0] = '\0';
   arc_fault_boundary = true;
   if (setjmp(arc_fault_jump))
   {
      arc_fault_boundary = false;
      arc_machine_destroy();
      if (failure && failure_size)
         snprintf(failure, failure_size, "Arculator initialization fault: %s", arc_fault_text);
      return false;
   }

   arm_cpu_type = 0;
   memc_type = 1;
   memsize = 4096;
   fdctype = FDC_WD1770;
   monitor_type = MONITOR_STANDARD;
   machine_type = MACHINE_TYPE_NORMAL;
   display_mode = DISPLAY_MODE_NO_BORDERS;
   dblscan = 1;
   fullborders = noborders = fullscreen = 0;
   support_rom_enabled = 0;
   soundena = 1;
   stereo = 1;
   sound_filter = 0;
   strcpy(machine, "a3000");
   joystick_if[0] = '\0';

   if (!video_renderer_init(NULL))
      fatal("Unable to allocate the headless video surface");
   initvid();
   arc_machine_live = true;
   arc_set_cpu(0, 1);
   timer_reset();
   initmem(memsize);
   if (!ram || !rom || !rom_arcrom || !rom_5th_column)
      fatal("Unable to allocate emulated machine memory");
   memset(rom, 0xff, 0x200000u);
   memcpy(rom, arc_saved_firmware, arc_saved_firmware_size);
   memset(rom_arcrom, 0xff, 0x10000u);
   memset(rom_5th_column, 0xff, 0x20000u);
   initmemc();
   resetarm();
   ioc_reset();
   vidc_reset();
   input_init();
   keyboard_init();
   resetmouse();
   sound_dev_init();
   sound_init();
   reinitvideo();
   disc_init();
   disc_reset();
   adf_init();
   wd1770_reset();
   cmos_init();
   cmos_set_image(arc_saved_cmos, sizeof(arc_saved_cmos));
   ds2401_init();
   joystick_init();
   joystick_if_init();
   ioeb_init();
   disc_load(0, arc_saved_media);
   if (disc_empty(0))
      fatal("The staged floppy image could not be mounted");
   ioc_discchange(0);

   arc_platform_video_reset();
   arc_platform_audio_reset();
   arc_fault_boundary = false;
   arc_last_refresh = 50.0;
   arc_active = true;
   return true;
}

bool arc_engine_start(const uint8_t *firmware, size_t firmware_size,
                      const char *media_path,
                      const uint8_t cmos[ARC_CMOS_MACHINE_SIZE],
                      char *failure, size_t failure_size)
{
   arc_engine_stop();
   if (!firmware || (firmware_size != 524288u && firmware_size != 2097152u) ||
       !media_path || !media_path[0] || !cmos)
   {
      if (failure && failure_size) snprintf(failure, failure_size, "Invalid engine launch plan");
      return false;
   }
   arc_saved_firmware = malloc(firmware_size);
   if (!arc_saved_firmware)
   {
      if (failure && failure_size) snprintf(failure, failure_size, "Unable to retain firmware image");
      return false;
   }
   memcpy(arc_saved_firmware, firmware, firmware_size);
   arc_saved_firmware_size = firmware_size;
   if (strlen(media_path) >= sizeof(arc_saved_media))
   {
      arc_engine_stop();
      if (failure && failure_size) snprintf(failure, failure_size, "Staged media path is too long");
      return false;
   }
   strcpy(arc_saved_media, media_path);
   memcpy(arc_saved_cmos, cmos, sizeof(arc_saved_cmos));
   if (!arc_machine_create(failure, failure_size))
   {
      arc_engine_stop();
      return false;
   }
   return true;
}

bool arc_engine_reset(char *failure, size_t failure_size)
{
   if (!arc_saved_firmware) return false;
   cmos_get_image(arc_saved_cmos, sizeof(arc_saved_cmos));
   arc_machine_destroy();
   return arc_machine_create(failure, failure_size);
}

void arc_engine_stop(void)
{
   if (arc_machine_live)
      cmos_get_image(arc_saved_cmos, sizeof(arc_saved_cmos));
   arc_machine_destroy();
   free(arc_saved_firmware);
   arc_saved_firmware = NULL;
   arc_saved_firmware_size = 0u;
   arc_saved_media[0] = '\0';
   arc_active = false;
   arc_fault_boundary = false;
}

bool arc_engine_run_frame(ArcInputState *input, ArcEngineFrame *frame)
{
   uint64_t start_tsc, start_sequence, sequence;
   double programmed_rate;
   uint64_t maximum_cycles;
   if (!frame || !arc_active || arc_fault) return false;
   memset(frame, 0, sizeof(*frame));
   arc_platform_input_apply(input);
   (void)arc_platform_video_frame(NULL, NULL, NULL, &start_sequence);
   start_tsc = tsc;
   programmed_rate = vidc_get_refresh_rate();
   if (programmed_rate < 20.0 || programmed_rate > 200.0)
      programmed_rate = arc_last_refresh;
   maximum_cycles = (uint64_t)speed_mhz *
      ((uint64_t)(1000000.0 / programmed_rate) + 1u) + 2048u;

   arc_fault_boundary = true;
   if (setjmp(arc_fault_jump))
   {
      arc_fault_boundary = false;
      return false;
   }
   do
   {
      execarm(2048);
      (void)arc_platform_video_frame(NULL, NULL, NULL, &sequence);
   } while (sequence == start_sequence &&
            arc_timestamp_elapsed_cycles(start_tsc, tsc) < maximum_cycles);
   if (adf_flush_all()) fatal("Unable to flush the staged floppy image");
   arc_fault_boundary = false;

   frame->pixels = arc_platform_video_frame(&frame->width, &frame->height,
                                             &frame->pitch, &sequence);
   frame->field_completed = sequence != start_sequence;
   frame->elapsed_us = arc_timestamp_elapsed_cycles(start_tsc, tsc) /
                       (uint64_t)speed_mhz;
   if (!frame->elapsed_us) frame->elapsed_us = 1u;
   if (frame->field_completed)
   {
      double measured = vidc_get_refresh_rate();
      if (measured >= 20.0 && measured <= 200.0) arc_last_refresh = measured;
   }
   frame->refresh_hz = arc_last_refresh;
   return true;
}

size_t arc_engine_audio(int16_t *output, size_t capacity_frames)
{ return output ? arc_platform_audio_read(output, capacity_frames) : 0u; }
void arc_engine_cmos(uint8_t output[ARC_CMOS_PAYLOAD_SIZE])
{
   uint8_t machine[ARC_CMOS_MACHINE_SIZE];
   if (output && arc_machine_live)
   {
      cmos_get_image(machine, sizeof(machine));
      memcpy(output, machine + 16u, ARC_CMOS_PAYLOAD_SIZE);
   }
}
bool arc_engine_faulted(const char **message)
{ if (message) *message = arc_fault_text; return arc_fault; }
bool arc_engine_active(void) { return arc_active; }
