/* ArcDroid-owned Arculator session owner. GPL-2.0-only. */
#include "engine_backend.h"
#include "arcdroid_platform.h"

#include "arc.h"
#include "arm.h"
#include "cmos.h"
#include "config.h"
#include "disc.h"
#include "disc_adf.h"
#include "disc_jfd.h"
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
#include "82c711.h"
#include "82c711_fdc.h"
#include "ide.h"
#include "arc_io.h"

#if defined(ARCDROID_TEST_FAULTS)
#include "libretro.h"
#endif

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool arc_arculator_start(const uint8_t *firmware, size_t firmware_size,
                      const ArcEngineMedia *media,
                      const ArcEngineMedia *helper,
                      const ArcMachineDescriptor *machine,
                      ArcOsClass os,
                      const ArcIdeBackend *ide_backend,
                      const uint8_t cmos[ARC_CMOS_MACHINE_SIZE],
                      const uint8_t locked_raw_mask[32],
                      ArcEngineCmosIgnoredCallback ignored_callback,
                      void *ignored_opaque,
                      char *error, size_t error_size);
static void arc_arculator_set_log_callback(ArcEngineLogCallback callback, void *opaque);
static bool arc_arculator_reset(char *error, size_t error_size);
static void arc_arculator_stop(void);
static bool arc_arculator_run_frame(ArcInputState *input, ArcEngineFrame *frame);
static void arc_arculator_power_on_arm(ArcInputState *input, bool armed);
static bool arc_arculator_power_on_take_release(uint64_t *at_us, bool *boot_action);
static void arc_arculator_boot_watch_arm(void);
static void arc_arculator_boot_watch_cancel(void);
static ArcBootWatchState arc_arculator_boot_watch_state(void);
static bool arc_arculator_launch_outcome_arm(const char *command);
static void arc_arculator_launch_outcome_cancel(const char *reason);
static void arc_arculator_launch_outcome_refused(unsigned drive);
static void arc_arculator_launch_outcome_snapshot(ArcLaunchOutcome *outcome);
static void arc_arculator_error_snapshot(ArcErrorBox *box);
static bool arc_arculator_error_request(ArcErrorAnswer answer);
static void arc_arculator_error_discard(void);
static void arc_arculator_disc_configure(bool enabled, const char *inserted_name,
   ArcDiscResolveCallback resolve, ArcDiscObservationCallback observe,
   void *opaque);
static void arc_arculator_disc_clear(void);
static void arc_arculator_disc_snapshot(ArcDiscRequest *out);
static void arc_arculator_disc_a_notified(void);
static uint64_t arc_arculator_disc_time_us(void);
static bool arc_arculator_disc_controller_idle(void);
static void arc_arculator_adf_refusal_callback_set(
   void (*callback)(unsigned, unsigned, unsigned, unsigned));
static size_t arc_arculator_audio(int16_t *output, size_t capacity_frames);
static void arc_arculator_audio_stats(uint64_t *underrun_frames, uint64_t *overrun_frames);
static bool arc_arculator_media_eject(void);
static bool arc_arculator_media_insert(const ArcEngineMedia *media,
                             char *error, size_t error_size);
static bool arc_arculator_get_av_info(unsigned *width, unsigned *height, double *refresh_hz);
static void arc_arculator_cmos(uint8_t output[ARC_CMOS_PAYLOAD_SIZE]);
static bool arc_arculator_faulted(const char **message);
static bool arc_arculator_active(void);

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
static ArcBootWatch arc_boot_watch;
static ArcLaunchOutcome arc_launch_outcome;
static ArcErrorBox arc_error_box;
static ArcDiscRequest arc_disc_request;
#if defined(ARCDROID_TEST_FAULTS)
static unsigned arc_test_disc_resolve_calls;
static unsigned arc_test_disc_eject_count;
static bool arc_test_disc_hold_busy;
unsigned arc_engine_test_disc_resolve_calls(void)
{ return arc_test_disc_resolve_calls; }
unsigned arc_engine_test_disc_eject_count(void)
{ return arc_test_disc_eject_count; }
#endif
bool arc_engine_disc_watch_armed;
static bool arc_disc_enabled;
static char arc_disc_inserted_name[11];
static ArcDiscResolveCallback arc_disc_resolve_callback;
static ArcDiscObservationCallback arc_disc_observe_callback;
static void *arc_disc_opaque;
static uint64_t arc_engine_disc_now(void)
{ return arc_active && speed_mhz > 0 ?
   (tsc >> 32u) / (uint64_t)speed_mhz : 0u; }
static uint64_t arc_arculator_disc_time_us(void) { return arc_engine_disc_now(); }
static bool arc_engine_guest_byte(uint32_t address, uint8_t *byte)
{
   if (address >= 0x04000000u) return false;
   uint8_t status = memstat[address >> 12u];
   if (!((status >= 1u && status <= 3u) || status == 5u) ||
       !mempoint[address >> 12u]) return false;
   *byte = mempoint[address >> 12u][address];
   return true;
}
static bool arc_engine_guest_string(uint32_t address, char *out, size_t capacity)
{
   if (!out || capacity < 2u) return false;
   for (size_t i = 0u; i < capacity; ++i)
   {
      uint8_t byte;
      if (address > UINT32_MAX - i ||
          !arc_engine_guest_byte(address + (uint32_t)i, &byte)) return false;
      out[i] = (char)byte;
      if (byte < 0x20u)
      { out[i] = 0; return i != 0u; }
   }
   return false;
}
static void arc_arculator_disc_configure(bool enabled, const char *inserted_name,
   ArcDiscResolveCallback resolve, ArcDiscObservationCallback observe,
   void *opaque)
{
   arc_disc_enabled = enabled;
   arc_disc_resolve_callback = resolve;
   arc_disc_observe_callback = observe;
   arc_disc_opaque = opaque;
   arc_disc_inserted_name[0] = 0;
   if (inserted_name && strlen(inserted_name) <= 10u)
      strcpy(arc_disc_inserted_name, inserted_name);
   arc_arculator_disc_clear();
}
static void arc_arculator_disc_clear(void)
{
   arc_disc_request_clear(&arc_disc_request);
   arc_engine_disc_watch_armed = false;
}
static void arc_arculator_disc_snapshot(ArcDiscRequest *out)
{ if (out) *out = arc_disc_request; }
static void arc_arculator_disc_a_notified(void)
{ arc_disc_request.a_notified = true; }
void arc_engine_disc_poll(uint32_t address, uint32_t mode, uint32_t sp,
                           uint32_t r0, uint32_t flags)
{
   uint32_t error_number = 0u;
   bool readable = true;
   if (mode == 0u && address == arc_disc_request.continuation &&
       (flags & 0x10000000u) && arc_disc_request.operation != ARC_DISC_CATALOGUE)
   {
      for (unsigned i = 0u; i < 4u; ++i)
      {
         uint8_t byte;
         if (r0 > UINT32_MAX - i ||
             !arc_engine_guest_byte(r0 + i, &byte)) { readable = false; break; }
         error_number |= (uint32_t)byte << (8u * i);
      }
   }
   ArcDiscResult result = arc_disc_request_outcome(&arc_disc_request,
      address, mode, sp, r0, (flags & 0x10000000u) != 0u,
      readable, error_number);
   arc_engine_disc_watch_armed = arc_disc_request.armed;
   if (result.discarded) rpclog("Disc request: discarded watch reason=%s\n", result.reason);
   if (result.needs_resolution && arc_disc_resolve_callback)
   {
#if defined(ARCDROID_TEST_FAULTS)
      ++arc_test_disc_resolve_calls;
#endif
      unsigned candidate = arc_disc_resolve_callback(arc_disc_opaque,
         arc_disc_request.object, arc_disc_request.operation);
      result = arc_disc_request_resolved(&arc_disc_request, candidate,
                                        arc_engine_disc_now());
   }
   if (result.observed && arc_disc_observe_callback)
      if (arc_disc_observe_callback(arc_disc_opaque, arc_disc_request.object,
            result.candidate, result.observation, result.trigger))
         arc_disc_request_retry(&arc_disc_request);
}
static const char *arc_error_result(ArcErrorAnswer answer)
{ return answer == ARC_ERROR_OK ? "ok" : "cancel"; }
static void arc_engine_error_end(void)
{
   if (arc_error_box.stage != ARC_ERROR_OFF)
   {
      if (arc_error_box.pending != ARC_ERROR_NONE)
         rpclog("Error box: answer g=%llu result=%s key=%u state=discarded\n",
                (unsigned long long)arc_error_box.generation,
                arc_error_result(arc_error_box.pending), arc_error_box.key);
      rpclog("Error box: ended g=%llu\n",
             (unsigned long long)arc_error_box.generation);
   }
   arc_error_end(&arc_error_box);
}
static void arc_arculator_error_snapshot(ArcErrorBox *box)
{ if (box) *box = arc_error_box; }
#if defined(ARCDROID_TEST_FAULTS)
void arc_engine_test_disc_error_ready(bool ready)
{
   arc_error_box.stage = ready ? ARC_ERROR_READY : ARC_ERROR_OFF;
   arc_error_box.pending = ARC_ERROR_NONE;
}
#endif
static bool arc_arculator_error_request(ArcErrorAnswer answer)
{
   if (!arc_error_request(&arc_error_box, answer)) return false;
   rpclog("Error box: answer g=%llu result=%s key=%u state=pending\n",
          (unsigned long long)arc_error_box.generation,
          arc_error_result(answer), arc_error_box.key);
   return true;
}
static void arc_arculator_error_discard(void)
{
   if (arc_error_box.pending != ARC_ERROR_NONE)
      rpclog("Error box: answer g=%llu result=%s key=%u state=discarded\n",
             (unsigned long long)arc_error_box.generation,
             arc_error_result(arc_error_box.pending), arc_error_box.key);
   arc_error_discard(&arc_error_box);
}
void arc_engine_error_mode(uint32_t old_mode, uint32_t new_mode)
{
   /* A physical dismissal returns to the user caller before our eligible
    * read. Ending here discards pending pad intent without touching input. */
   if (new_mode == 0u && old_mode != 0u) arc_engine_error_end();
}
bool arc_engine_error_poll(uint32_t address, uint32_t mode,
                           uint32_t sp, uint32_t lr, uint8_t *key)
{
   if (!key || !arc_active || arc_fault || address >= 0x04000000u ||
       memstat[address >> 12u] != 5u ||
       !arc_error_consume(&arc_error_box, address, mode, sp, lr, key))
      return false;
   rpclog("Error box: answer g=%llu result=%s key=%u state=consumed\n",
          (unsigned long long)arc_error_box.generation,
          arc_error_result(arc_error_box.consumed), *key);
   return true;
}
/* ArcDroid power-on Shift observes the boot SWI independently of Slice 14. */
static bool arc_power_on_armed;
static bool arc_power_on_release_pending;
static bool arc_power_on_boot_reason;
static ArcInputState *arc_power_on_input;
static uint64_t arc_power_on_start_tsc;
static uint64_t arc_power_on_release_us;

static uint64_t arc_engine_power_on_elapsed_us(void)
{
   return arc_active && speed_mhz > 0 ?
      arc_timestamp_elapsed_cycles(arc_power_on_start_tsc, tsc) /
         (uint64_t)speed_mhz : 0u;
}
static void arc_engine_power_on_release(bool boot_action)
{
   if (!arc_power_on_armed) return;
   arc_power_on_armed = false;
   arc_power_on_release_pending = true;
   arc_power_on_boot_reason = boot_action;
   arc_power_on_release_us = arc_engine_power_on_elapsed_us();
   arc_input_source_key(arc_power_on_input, ARC_INPUT_SOURCE_POWER_ON,
                        RETROK_LSHIFT, false, true);
}
static void arc_arculator_power_on_arm(ArcInputState *input, bool armed)
{
   arc_power_on_input = input;
   arc_power_on_armed = armed && arc_active;
   arc_power_on_release_pending = false;
   arc_power_on_start_tsc = tsc;
}
static bool arc_arculator_power_on_take_release(uint64_t *at_us, bool *boot_action)
{
   if (!arc_power_on_release_pending) return false;
   arc_power_on_release_pending = false;
   if (at_us) *at_us = arc_power_on_release_us;
   if (boot_action) *boot_action = arc_power_on_boot_reason;
   return true;
}

static void arc_arculator_boot_watch_arm(void)
{ if (arc_active) arc_boot_watch_arm(&arc_boot_watch, (tsc >> 32u) / (uint64_t)speed_mhz); }
static void arc_arculator_boot_watch_cancel(void)
{ arc_boot_watch_cancel(&arc_boot_watch); }
static ArcBootWatchState arc_arculator_boot_watch_state(void)
{ return arc_boot_watch.state; }
static bool arc_arculator_launch_outcome_arm(const char *command)
{
   return arc_active && speed_mhz > 0 &&
      arc_outcome_arm(&arc_launch_outcome, command,
                      (tsc >> 32u) / (uint64_t)speed_mhz);
}
static void arc_arculator_launch_outcome_cancel(const char *reason)
{ arc_outcome_cancel(&arc_launch_outcome, reason); }
static void arc_arculator_launch_outcome_refused(unsigned drive)
{ arc_outcome_refused(&arc_launch_outcome, drive); }
static void arc_arculator_launch_outcome_snapshot(ArcLaunchOutcome *outcome)
{ if (outcome) *outcome = arc_launch_outcome; }
static void (*arc_adf_refusal_callback)(unsigned, unsigned, unsigned, unsigned);
void arc_engine_adf_refused(unsigned drive, unsigned track,
                           unsigned side, unsigned sector)
{
   if (arc_adf_refusal_callback)
      arc_adf_refusal_callback(drive, track, side, sector);
}
static void arc_arculator_adf_refusal_callback_set(
   void (*callback)(unsigned, unsigned, unsigned, unsigned))
{ arc_adf_refusal_callback = callback; }

static bool arc_engine_command_read(uint32_t address, char output[257])
{
   if (address >= 0x04000000u) return false;
   for (unsigned i = 0u; i <= 256u; ++i)
   {
      uint32_t at = address + i;
      uint8_t status = at < 0x04000000u ? memstat[at >> 12u] : 0u;
      if (status < 1u || status > 3u ||
          !mempoint[at >> 12u]) return false;
      unsigned c = mempoint[at >> 12u][at];
      if (!c) { output[i] = 0; return i != 0u; }
      if (c < 0x20u || c > 0x7eu || i == 256u) return false;
      output[i] = (char)c;
   }
   return false;
}
static bool arc_engine_block_first(uint32_t address, uint8_t *first)
{
   if (address >= 0x04000000u) return false;
   uint8_t status = memstat[address >> 12u];
   if (status < 1u || status > 3u || !mempoint[address >> 12u]) return false;
   *first = mempoint[address >> 12u][address];
   return true;
}
#if defined(ARCDROID_TEST_FAULTS)
void arc_engine_test_boot_watch_snapshot(ArcBootWatch *out)
{ if (out) *out = arc_boot_watch; }
bool arc_engine_test_guest_word(uint32_t address, uint32_t *out)
{
   if (!out || address > 0x03fffffcu || (address & 3u)) return false;
   uint32_t value = 0u;
   for (unsigned i = 0u; i < 4u; ++i)
   {
      uint32_t at = address + i;
      uint8_t status = memstat[at >> 12u];
      if (status < 1u || status > 3u || !mempoint[at >> 12u]) return false;
      value |= (uint32_t)mempoint[at >> 12u][at] << (8u * i);
   }
   *out = value;
   return true;
}
#endif
void arc_engine_boot_watch_swi(uint32_t swi, uint32_t r0, uint32_t r1,
                               uint32_t r2, uint32_t r3, uint32_t address,
                               uint32_t mode)
{
   bool in_rom = address < 0x04000000u &&
      memstat[address >> 12u] == 5u;
   if (arc_disc_enabled && arc_active && !arc_fault)
   {
      /* The authored reset ROM can still execute through the first-page ROM
       * alias. This additional origin is private to the disc observer. */
      bool disc_in_rom = in_rom ||
         (address < 0x1000u && memstat[0] == 1u &&
          mempoint[0] == (uint8_t *)rom);
      char name[256];
      uint32_t normalized = swi & ~0x20000u;
      ArcDiscOperation operation;
      bool lookup = arc_disc_request_operation(normalized, r0, &operation);
      bool upcall = normalized == 0x33u && (r0 == 1u || r0 == 2u);
      bool readable = (lookup || upcall) &&
         arc_engine_guest_string(upcall ? r2 : r1, name,
                                 upcall ? 12u : sizeof(name));
      ArcDiscSwi event = {swi, r0, r1, r3, address, mode, armregs[13],
                          disc_in_rom, readable, readable ? name : NULL,
                          UINT_MAX, arc_engine_disc_now()};
      arc_disc_request_swi(&arc_disc_request, &event, arc_disc_inserted_name);
      arc_engine_disc_watch_armed = arc_disc_request.armed;
   }
   uint8_t block_first = 0u;
   bool block_valid = (swi == 7u || swi == 0x20007u) && r0 == 21u &&
      arc_engine_block_first(r1, &block_first);
   ArcErrorStage before = arc_error_box.stage;
   uint64_t before_generation = arc_error_box.generation;
   ArcErrorAnswer old_pending = arc_error_box.pending;
   uint8_t old_key = arc_error_box.key;
   const char *ineligible = arc_error_swi(&arc_error_box, swi, r0, r1, r2,
      address, mode, armregs[13], armregs[14], in_rom,
      block_valid, block_first);
   if (before != ARC_ERROR_OFF &&
       (arc_error_box.stage == ARC_ERROR_OFF ||
        arc_error_box.generation != before_generation ||
        swi == 0x400dfu || swi == 0x600dfu))
   {
      if (old_pending != ARC_ERROR_NONE)
         rpclog("Error box: answer g=%llu result=%s key=%u state=discarded\n",
                (unsigned long long)before_generation,
                arc_error_result(old_pending), old_key);
      rpclog("Error box: ended g=%llu\n", (unsigned long long)before_generation);
   }
   if (ineligible)
      rpclog("Error box: ineligible reason=%s\n", ineligible);
   if (before != ARC_ERROR_READY && arc_error_box.stage == ARC_ERROR_READY)
      rpclog("Error box: ready g=%llu buttons=%s default=%s os=%s\n",
             (unsigned long long)arc_error_box.generation,
             arc_error_box.ok && arc_error_box.cancel ? "ok,cancel" :
             arc_error_box.ok ? "ok" : "cancel",
             arc_error_box.default_cancel ? "cancel" : "ok",
             arc_error_box.os == ARC_OS_RISCOS_310 ? "3.10" :
             arc_error_box.os == ARC_OS_RISCOS_311 ? "3.11" : "3.19");
   /* ArcDroid patch: the power-on release must work with the observer off. */
   if ((swi == 0x29u || swi == 0x20029u) && r0 == 15u)
      arc_engine_power_on_release(true);
   if (arc_launch_outcome.state != ARC_OUTCOME_OFF)
   {
      char command[257];
      const char *start_command = NULL;
      if (((swi & 0x00ffffffu) & ~0x00020000u) == 0x400deu &&
          arc_engine_command_read(r0, command)) start_command = command;
      arc_outcome_swi(&arc_launch_outcome, swi, r0, r1, r2, in_rom,
                      start_command,
                      (tsc >> 32u) / (uint64_t)speed_mhz);
   }
   if (arc_boot_watch.state != ARC_BOOT_WATCH_ARMED) return;
   arc_boot_watch_observe(&arc_boot_watch, swi, r0, r1, r2, r3,
                          address, mode, in_rom,
                          (tsc >> 32u) / (uint64_t)speed_mhz);
}
static bool arc_fault_boundary;
static bool arc_machine_live;
static jmp_buf arc_fault_jump;
static char arc_fault_text[512];
static uint8_t *arc_saved_firmware;
static size_t arc_saved_firmware_size;
static char arc_saved_media[4096];
static bool arc_saved_media_writable;
static ArcMediaKind arc_saved_media_kind;
static ArcMediaGeometry arc_saved_media_geometry;
static const ArcJfdImage *arc_saved_jfd;
static const uint8_t *arc_saved_media_data;
static size_t arc_saved_media_size;
static size_t arc_saved_media_extent;

static bool arc_engine_extent_valid(const ArcEngineMedia *media)
{
   if (!media || media->effective_extent == ARC_MEDIA_EXTENT_UNRESTRICTED)
      return true;
   if (media->kind == ARC_MEDIA_JFD) return false;
   size_t sector = media->geometry == ARC_GEOMETRY_320K ||
                   media->geometry == ARC_GEOMETRY_640K_L ? 256u : 1024u;
   size_t geometry = media->geometry == ARC_GEOMETRY_320K ? 327680u :
      media->geometry == ARC_GEOMETRY_640K_L ? 655360u :
      media->geometry == ARC_GEOMETRY_800K_D ||
      media->geometry == ARC_GEOMETRY_800K_E ? 819200u :
      media->geometry == ARC_GEOMETRY_1600K_F ? 1638400u : 0u;
   size_t backing = 0u;
   if (media->data) backing = media->size;
   else if (!media->path || !arc_io_file_size(media->path, &backing)) return false;
   return geometry && media->effective_extent % sector == 0u &&
          media->effective_extent <= geometry &&
          media->effective_extent <= backing && backing >= geometry;
}
static char arc_saved_helper[4096];
static ArcMediaGeometry arc_saved_helper_geometry;
static const uint8_t *arc_saved_helper_data;
static size_t arc_saved_helper_size;
static uint8_t arc_saved_cmos[ARC_CMOS_MACHINE_SIZE];
static uint8_t arc_locked_cmos_mask[32];
static ArcMachine arc_saved_machine = ARC_MACHINE_A3000;
static double arc_last_refresh = 50.0;
static ArcEngineLogCallback arc_log_callback;
static void *arc_log_opaque;
static bool arc_helper_media_valid(const ArcEngineMedia *helper)
{
   return !helper ||
      (((helper->path && helper->path[0]) || helper->data) && !helper->jfd &&
       ((helper->kind == ARC_MEDIA_ADL_640K &&
         helper->geometry == ARC_GEOMETRY_640K_L) ||
        (helper->kind == ARC_MEDIA_ADF_800K &&
         helper->geometry == ARC_GEOMETRY_800K_D)));
}
#if defined(ARCDROID_TEST_FAULTS)
static bool arc_test_vidc_noncompleting;
unsigned arc_ide_test_accesses(void);

void arc_engine_test_set_vidc_noncompleting(bool enabled)
{
   arc_test_vidc_noncompleting = enabled;
}

bool arc_engine_test_helper_media_valid(ArcMediaKind kind,
                                        ArcMediaGeometry geometry)
{
   static const uint8_t data = 0u;
   ArcEngineMedia helper;
   memset(&helper, 0, sizeof(helper));
   helper.data = &data;
   helper.size = 1u;
   helper.kind = kind;
   helper.geometry = geometry;
   return arc_helper_media_valid(&helper);
}

RETRO_API bool arc_engine_test_drive_media(unsigned drive, bool *write_protected,
                                 char digest[41])
{
   const char *path = drive == 0u ? arc_saved_media :
                      drive == 1u ? arc_saved_helper : NULL;
   if (!path || !path[0] || drive_funcs[drive] == NULL) return false;
   if (write_protected) *write_protected = writeprot[drive] != 0;
   if (digest)
   {
      void *data = NULL;
      size_t size = 0u;
      if (!arc_io_read_all(path, &data, &size)) return false;
      ArcDigest found = arc_digest(data, size);
      memcpy(digest, found.sha1, sizeof(found.sha1));
      free(data);
   }
   return true;
}
size_t arc_engine_test_media_extent(void)
{ return arc_saved_media_extent; }

bool arc_engine_test_cmos_image(uint8_t output[ARC_CMOS_MACHINE_SIZE])
{
   if (!output || !arc_machine_live) return false;
   cmos_get_image(output, ARC_CMOS_MACHINE_SIZE);
   return true;
}

bool arc_engine_test_cmos_write(unsigned offset, uint8_t value)
{
   uint8_t image[ARC_CMOS_MACHINE_SIZE];
   if (!arc_machine_live || offset >= sizeof(image)) return false;
   cmos_get_image(image, sizeof(image));
   image[offset] = value;
   cmos_set_image(image, sizeof(image));
   return true;
}

static void arc_test_cmos_bus_byte(uint8_t value)
{
   for (unsigned bit = 0u; bit < 8u; ++bit)
   {
      int level = (value & (0x80u >> bit)) != 0u;
      i2c_change(0, level);
      i2c_change(1, level);
   }
   i2c_change(0, 1);
   i2c_change(1, 1);
   i2c_change(0, 0);
}

bool arc_engine_test_cmos_bus_write(unsigned raw, uint8_t value)
{
   if (!arc_machine_live || raw > 255u) return false;
   i2c_change(1, 1);
   i2c_change(1, 0);
   arc_test_cmos_bus_byte(0xa0u);
   arc_test_cmos_bus_byte((uint8_t)raw);
   arc_test_cmos_bus_byte(value);
   i2c_change(0, 0);
   i2c_change(1, 0);
   i2c_change(1, 1);
   return true;
}

bool arc_engine_test_newio(unsigned *commands, unsigned *read_data,
                           unsigned *read_success, unsigned *ide_accesses,
                           uint8_t *device_id)
{
   const ArcMachineDescriptor *descriptor = arc_machine_descriptor(arc_saved_machine);
   if (!arc_machine_live || !descriptor || descriptor->io != ARC_IO_NEW_82C711)
      return false;
   c82c711_fdc_test_counts(commands, read_data, read_success);
   if (ide_accesses) *ide_accesses = arc_ide_test_accesses();
   if (device_id) *device_id = ioeb_read(0x50u);
   return true;
}

bool arc_engine_test_arm2_swp_undefined(void)
{
   return arc_machine_live && arc_saved_machine == ARC_MACHINE_A3000 &&
          readmemb(0x0002fff0u) == 0xa2u;
}

static bool arc_test_newio_live(void)
{
   const ArcMachineDescriptor *descriptor = arc_machine_descriptor(arc_saved_machine);
   return arc_machine_live && descriptor && descriptor->io == ARC_IO_NEW_82C711;
}

static bool arc_test_advance_timer(void)
{
   if (timer_target == UINT32_MAX) return false;
   tsc = (uint64_t)timer_target << 32u;
   timer_process();
   return true;
}

bool arc_engine_test_newio_dir(bool seek, uint8_t *value)
{
   if (!value || !arc_test_newio_live()) return false;
   c82c711_fdc_write(2u, 0x1cu, NULL);
   if (!seek)
   {
      *value = c82c711_fdc_read(7u, NULL);
      return true;
   }

   unsigned new_track = disc_get_current_track(0) ? 0u : 1u;
   c82c711_fdc_write(5u, 0x0fu, NULL);
   c82c711_fdc_write(5u, 0u, NULL);
   c82c711_fdc_write(5u, (uint8_t)new_track, NULL);
   *value = c82c711_fdc_read(7u, NULL);
   for (unsigned step = 0u; step < 100000u && !(ioc.irqb & IOC_IRQB_DISC_IRQ); ++step)
      if (!arc_test_advance_timer()) return false;
   if (!(ioc.irqb & IOC_IRQB_DISC_IRQ)) return false;
   (void)c82c711_fdc_read(4u, NULL);
   c82c711_fdc_write(5u, 0x08u, NULL);
   for (unsigned step = 0u; step < 100000u &&
        (c82c711_fdc_read(4u, NULL) & 0xf0u) != 0xd0u; ++step)
      if (!arc_test_advance_timer()) return false;
   if ((c82c711_fdc_read(4u, NULL) & 0xf0u) != 0xd0u) return false;
   (void)c82c711_fdc_read(5u, NULL);
   (void)c82c711_fdc_read(5u, NULL);
   return true;
}

bool arc_engine_test_newio_read_start(unsigned drive, uint8_t size_code)
{
   if (!arc_test_newio_live() || drive >= 2u || !drive_funcs[drive]) return false;
   bool hd = drive == 0u && arc_saved_media_geometry == ARC_GEOMETRY_1600K_F;
   const uint8_t params[8] = {
      (uint8_t)drive, 0u, 0u, hd ? 3u : 0u, size_code,
      hd ? 3u : 0u, 0x1bu, 0xffu
   };
   /* Leave any guest command phase deterministically before the harness owns
    * the controller, then consume all four reset-status reports. */
   c82c711_fdc_write(2u, (uint8_t)drive, NULL);
   c82c711_fdc_write(2u, (uint8_t)(0x0cu | (0x10u << drive) | drive), NULL);
   for (unsigned step = 0u; step < 100000u && !(ioc.irqb & IOC_IRQB_DISC_IRQ); ++step)
      if (!arc_test_advance_timer()) return false;
   if (!(ioc.irqb & IOC_IRQB_DISC_IRQ)) return false;
   (void)c82c711_fdc_read(4u, NULL);
   for (unsigned reset_drive = 0u; reset_drive < 4u; ++reset_drive)
   {
      c82c711_fdc_write(5u, 0x08u, NULL);
      (void)c82c711_fdc_read(5u, NULL);
      (void)c82c711_fdc_read(5u, NULL);
   }
   c82c711_fdc_write(7u, hd ? 0u : 1u, NULL);
   c82c711_fdc_write(5u, 0x06u, NULL);
   for (unsigned index = 0u; index < sizeof(params); ++index)
      c82c711_fdc_write(5u, params[index], NULL);
   return true;
}

bool arc_engine_test_newio_result(uint8_t result[7], bool *fiq_asserted)
{
   if (!result || !arc_test_newio_live() || !(ioc.irqb & IOC_IRQB_DISC_IRQ))
      return false;
   if (fiq_asserted) *fiq_asserted = (ioc.fiq & IOC_FIQ_DISC_DATA) != 0u;
   (void)c82c711_fdc_read(4u, NULL);
   for (unsigned index = 0u; index < 7u; ++index)
      result[index] = c82c711_fdc_read(5u, NULL);
   return true;
}

bool arc_engine_test_newio_read_finish(size_t bytes, uint32_t *signature,
                                       uint8_t result[7])
{
   if (!bytes || !signature || !result || !arc_test_newio_live()) return false;
   size_t transferred = 0u;
   uint32_t found = UINT32_C(2166136261);
   for (unsigned step = 0u; step < 1000000u && !(ioc.irqb & IOC_IRQB_DISC_IRQ); ++step)
   {
      if (!arc_test_advance_timer()) return false;
      if (ioc.fiq & IOC_FIQ_DISC_DATA)
      {
         if (transferred >= bytes) return false;
         uint8_t value = c82c711_fdc_dmaread(transferred + 1u == bytes, NULL);
         found = (found ^ value) * UINT32_C(16777619);
         ++transferred;
      }
   }
   bool fiq_asserted = false;
   if (transferred != bytes ||
       !arc_engine_test_newio_result(result, &fiq_asserted) || fiq_asserted)
      return false;
   *signature = found;
   return true;
}

bool arc_engine_test_joystick(unsigned port, uint8_t *value)
{
   if (!arc_machine_live || !value || port >= 2u) return false;
   *value = ioeb_read(0x78u | (port ? 4u : 0u));
   return true;
}

bool arc_engine_test_ioeb_clock(unsigned clock)
{
   const ArcMachineDescriptor *descriptor = arc_machine_descriptor(arc_saved_machine);
   if (!arc_machine_live || !descriptor || descriptor->io != ARC_IO_NEW_82C711 ||
       clock > 2u) return false;
   ioeb_write(0x48u, (uint8_t)clock);
   return true;
}
unsigned arc_engine_test_vidc_clock(void) { return (unsigned)vidc_getclock(); }
#endif

static void arc_latch_fault(const char *format, va_list arguments)
{
   arc_engine_error_end();
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

static void arc_arculator_set_log_callback(ArcEngineLogCallback callback, void *opaque)
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
   if ((cpu != ARC_CPU_ARM2 && cpu != ARC_CPU_ARM250) || memc < 0 || memc >= (int)(sizeof(memc_speed) / sizeof(memc_speed[0])))
   {
      fatal("Unsupported Slice 1 CPU/MEMC selection");
      return;
   }
   arm_cpu_type = cpu;
   memc_type = memc;
   arm_mem_speed = memc_speed[memc];
   arm_cpu_speed = arm_mem_speed;
   memc_is_memc1 = memc1[memc];
   arm_has_swp = cpu == ARC_CPU_ARM250;
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
   closeide(&ide_internal);
   if (fdctype == FDC_82C711) c82c711_fdc_shutdown();
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
   const ArcMachineDescriptor *descriptor = arc_machine_descriptor(arc_saved_machine);
   arc_fault = false;
   arc_fault_text[0] = '\0';
   arc_fault_boundary = true;
   /* Own partial state before the first allocation so fault unwinding runs
    * the same idempotent teardown used by a complete machine. */
   arc_machine_live = true;
   if (setjmp(arc_fault_jump))
   {
      arc_fault_boundary = false;
      arc_machine_destroy();
      if (failure && failure_size)
         snprintf(failure, failure_size, "Arculator initialization fault: %s", arc_fault_text);
      return false;
   }

   if (!descriptor) fatal("Invalid machine descriptor");
   arm_cpu_type = descriptor->cpu;
   memc_type = descriptor->memc;
   memsize = (int)descriptor->memory_kib;
   fdctype = descriptor->io == ARC_IO_NEW_82C711 ? FDC_82C711 : FDC_WD1770;
   monitor_type = descriptor->monitor;
   machine_type = MACHINE_TYPE_NORMAL;
   display_mode = DISPLAY_MODE_NO_BORDERS;
   dblscan = 1;
   fullborders = noborders = fullscreen = 0;
   support_rom_enabled = 0;
   soundena = 1;
   stereo = 1;
   sound_filter = 0;
   strcpy(machine, descriptor->engine_name);
   strcpy(joystick_if, descriptor->id == ARC_MACHINE_A3010 ? "a3010" : "");

   if (!video_renderer_init(NULL))
      fatal("Unable to allocate the headless video surface");
   initvid();
   arc_set_cpu(descriptor->cpu, descriptor->memc);
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
   c82c711_init();
   disc_init();
   disc_reset();
   adf_init();
   jfd_init();
   wd1770_reset();
   c82c711_fdc_init();
   cmos_init();
   cmos_set_image(arc_saved_cmos, sizeof(arc_saved_cmos));
   ds2401_init();
   joystick_init();
   joystick_if_init();
   ioeb_init();
   arc_io_clear_memory_sources();
   if (arc_saved_media_data) arc_io_set_memory_source(0u, arc_saved_media_data, arc_saved_media_size);
   if (arc_saved_helper_data) arc_io_set_memory_source(1u, arc_saved_helper_data, arc_saved_helper_size);
   adf_set_writable(0, arc_saved_media_writable);
   if (arc_saved_media[0])
   {
      bool mounted = arc_saved_media_kind == ARC_MEDIA_JFD ?
         disc_load_jfd(0, arc_saved_jfd) :
         disc_load_raw_extent(0, arc_saved_media, arc_saved_media_geometry,
                       arc_saved_media_extent);
      if (!mounted)
         fatal("The selected floppy image could not be mounted");
   }
   if (arc_saved_helper[0])
   {
      adf_set_writable(1, 0);
      if (!disc_load_raw_extent(1, arc_saved_helper, arc_saved_helper_geometry,
                         ARC_MEDIA_EXTENT_UNRESTRICTED))
         fatal("The launch helper image could not be mounted");
   }
   ioc_discchange(0);
   ioc_discchange(1);

   arc_platform_video_reset();
   arc_platform_audio_reset();
   arc_fault_boundary = false;
   arc_last_refresh = 50.0;
   adf_set_refusal_callback(arc_engine_adf_refused);
   arc_active = true;
   return true;
}

static bool arc_arculator_start(const uint8_t *firmware, size_t firmware_size,
                      const ArcEngineMedia *media,
                      const ArcEngineMedia *helper,
                      const ArcMachineDescriptor *descriptor,
                      ArcOsClass os,
                      const ArcIdeBackend *ide_backend,
                      const uint8_t cmos[ARC_CMOS_MACHINE_SIZE],
                      const uint8_t locked_raw_mask[32],
                      ArcEngineCmosIgnoredCallback ignored_callback,
                      void *ignored_opaque,
                      char *failure, size_t failure_size)
{
   arc_arculator_stop();
   arc_error_box.os = os;
   if (!firmware || (firmware_size != 524288u && firmware_size != 2097152u) ||
       !cmos || !locked_raw_mask || !descriptor || !arc_helper_media_valid(helper) ||
       !arc_engine_extent_valid(media))
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
   const char *media_path = media && media->data ? ":arcdroid-memory-0:" :
                            media && media->path ? media->path : "";
   if (strlen(media_path) >= sizeof(arc_saved_media))
   {
      arc_arculator_stop();
      if (failure && failure_size) snprintf(failure, failure_size, "Staged media path is too long");
      return false;
   }
   strcpy(arc_saved_media, media_path);
   arc_saved_media_writable = media ? media->writable : false;
   arc_saved_media_kind = media ? media->kind : ARC_MEDIA_INVALID;
   arc_saved_media_geometry = media ? media->geometry : ARC_GEOMETRY_NONE;
   arc_saved_jfd = media ? media->jfd : NULL;
   arc_saved_media_data = media ? media->data : NULL;
   arc_saved_media_size = media ? media->size : 0u;
   arc_saved_media_extent = media ? media->effective_extent : ARC_MEDIA_EXTENT_UNRESTRICTED;
   const char *helper_path = helper && helper->data ? ":arcdroid-memory-1:" :
                             helper && helper->path ? helper->path : "";
   if (strlen(helper_path) >= sizeof(arc_saved_helper))
   {
      arc_arculator_stop();
      if (failure && failure_size) snprintf(failure, failure_size, "Launch helper path is too long");
      return false;
   }
   strcpy(arc_saved_helper, helper_path);
   arc_saved_helper_geometry = helper ? helper->geometry : ARC_GEOMETRY_NONE;
   arc_saved_helper_data = helper ? helper->data : NULL;
   arc_saved_helper_size = helper ? helper->size : 0u;
   memcpy(arc_saved_cmos, cmos, sizeof(arc_saved_cmos));
   memcpy(arc_locked_cmos_mask, locked_raw_mask, sizeof(arc_locked_cmos_mask));
   cmos_set_write_guard(arc_locked_cmos_mask, ignored_callback, ignored_opaque);
   arc_saved_machine = descriptor->id;
   ide_set_backend(descriptor->ide_capable ? ide_backend : NULL);
   if (!arc_machine_create(failure, failure_size))
   {
      arc_arculator_stop();
      return false;
   }
   return true;
}

static bool arc_arculator_reset(char *failure, size_t failure_size)
{
   arc_engine_error_end();
   arc_boot_watch_clear(&arc_boot_watch);
   arc_outcome_clear(&arc_launch_outcome);
   if (!arc_saved_firmware) return false;
   cmos_get_image(arc_saved_cmos, sizeof(arc_saved_cmos));
   arc_machine_destroy();
   return arc_machine_create(failure, failure_size);
}

static void arc_arculator_stop(void)
{
   arc_engine_error_end();
   arc_power_on_armed = false;
   arc_power_on_release_pending = false;
   arc_power_on_input = NULL;
   arc_boot_watch_clear(&arc_boot_watch);
   arc_outcome_clear(&arc_launch_outcome);
   if (arc_machine_live)
      cmos_get_image(arc_saved_cmos, sizeof(arc_saved_cmos));
   arc_machine_destroy();
   cmos_set_write_guard(NULL, NULL, NULL);
   memset(arc_locked_cmos_mask, 0, sizeof(arc_locked_cmos_mask));
   ide_set_backend(NULL);
   free(arc_saved_firmware);
   arc_saved_firmware = NULL;
   arc_saved_firmware_size = 0u;
   arc_saved_media[0] = '\0';
   arc_saved_media_writable = false;
   arc_saved_media_kind = ARC_MEDIA_INVALID;
   arc_saved_media_geometry = ARC_GEOMETRY_NONE;
   arc_saved_jfd = NULL;
   arc_saved_media_data = NULL;
   arc_saved_media_size = 0u;
   arc_saved_media_extent = ARC_MEDIA_EXTENT_UNRESTRICTED;
   arc_saved_helper[0] = '\0';
   arc_saved_helper_geometry = ARC_GEOMETRY_NONE;
   arc_saved_helper_data = NULL;
   arc_saved_helper_size = 0u;
   arc_io_clear_memory_sources();
   arc_active = false;
   arc_error_box.os = ARC_OS_UNKNOWN;
   adf_set_refusal_callback(NULL);
   arc_fault_boundary = false;
#if defined(ARCDROID_TEST_FAULTS)
   arc_test_vidc_noncompleting = false;
#endif
}

static bool arc_arculator_run_frame(ArcInputState *input, ArcEngineFrame *frame)
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
      /* ArcDroid patch: count CPU time, including incomplete video fields. */
      if (arc_power_on_armed && arc_engine_power_on_elapsed_us() >= UINT64_C(1000000))
         arc_engine_power_on_release(false);
      (void)arc_platform_video_frame(NULL, NULL, NULL, &sequence);
   } while (
#if defined(ARCDROID_TEST_FAULTS)
            (arc_test_vidc_noncompleting || sequence == start_sequence) &&
#else
            sequence == start_sequence &&
#endif
            arc_timestamp_elapsed_cycles(start_tsc, tsc) < maximum_cycles);
   if (adf_flush_all()) fatal("Unable to flush the staged floppy image");
   arc_fault_boundary = false;
   arc_boot_watch_frame(&arc_boot_watch, (tsc >> 32u) / (uint64_t)speed_mhz);
   arc_outcome_frame(&arc_launch_outcome, (tsc >> 32u) / (uint64_t)speed_mhz);

   frame->pixels = arc_platform_video_frame(&frame->width, &frame->height,
                                             &frame->pitch, &sequence);
   frame->field_completed = sequence != start_sequence;
#if defined(ARCDROID_TEST_FAULTS)
   if (arc_test_vidc_noncompleting) frame->field_completed = false;
#endif
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

static size_t arc_arculator_audio(int16_t *output, size_t capacity_frames)
{ return output ? arc_platform_audio_read(output, capacity_frames) : 0u; }
static void arc_arculator_audio_stats(uint64_t *underrun_frames, uint64_t *overrun_frames)
{ arc_platform_audio_stats(underrun_frames, overrun_frames); }

static bool arc_arculator_media_eject(void)
{
   if (!arc_machine_live) return false;
#if defined(ARCDROID_TEST_FAULTS)
   ++arc_test_disc_eject_count;
#endif
   arc_arculator_error_discard();
   wd1770_media_change();
   c82c711_fdc_media_change(0u);
   disc_close(0);
   arc_saved_media[0] = '\0';
   arc_saved_media_kind = ARC_MEDIA_INVALID;
   arc_saved_media_geometry = ARC_GEOMETRY_NONE;
   arc_saved_media_extent = ARC_MEDIA_EXTENT_UNRESTRICTED;
   arc_saved_jfd = NULL;
   ioc_discchange(0);
   return true;
}

static bool arc_arculator_disc_controller_idle(void)
{
#if defined(ARCDROID_TEST_FAULTS)
   if (arc_test_disc_hold_busy) return false;
#endif
   if (!arc_active || arc_fault || !arc_machine_live || !fdc_timer ||
       timer_is_enabled(fdc_timer) || fdc_overridden) return false;
   return fdctype == FDC_82C711 ? c82c711_fdc_idle() : wd1770_idle();
}

#if defined(ARCDROID_TEST_FAULTS)
void arc_engine_test_disc_hold_busy(bool busy)
{ arc_test_disc_hold_busy = busy; }
bool arc_engine_test_disc_timer_busy(bool busy)
{
   if (!fdc_timer) return false;
   if (busy) timer_enable(fdc_timer);
   else timer_disable(fdc_timer);
   return timer_is_enabled(fdc_timer) == busy;
}
static bool arc_test_fail_next_media_insert;
void arc_engine_test_fail_next_media_insert(void)
{ arc_test_fail_next_media_insert = true; }
#endif

static bool arc_arculator_media_insert(const ArcEngineMedia *media,
                             char *failure, size_t failure_size)
{
   arc_arculator_error_discard();
   const char *path = media && media->data ? ":arcdroid-memory-0:" :
                      media && media->path ? media->path : "";
   if (!arc_machine_live || !media || !path[0] ||
       strlen(path) >= sizeof(arc_saved_media) ||
       (media->kind == ARC_MEDIA_JFD && !media->jfd) ||
       !arc_engine_extent_valid(media)) return false;
#if defined(ARCDROID_TEST_FAULTS)
   if (arc_test_fail_next_media_insert)
   { arc_test_fail_next_media_insert = false; return false; }
#endif
   /* Defence in depth for callers that replace a mounted image directly. */
   wd1770_media_change();
   c82c711_fdc_media_change(0u);
   disc_close(0);
   if (media->data) arc_io_set_memory_source(0u, media->data, media->size);
   adf_set_writable(0, media->writable);
   bool mounted = media->kind == ARC_MEDIA_JFD ?
      disc_load_jfd(0, media->jfd) :
      disc_load_raw_extent(0, (char *)path, media->geometry, media->effective_extent);
   if (!mounted)
   {
      arc_saved_media[0] = '\0';
      arc_saved_media_kind = ARC_MEDIA_INVALID;
      arc_saved_media_geometry = ARC_GEOMETRY_NONE;
      arc_saved_jfd = NULL;
      if (failure && failure_size)
         snprintf(failure, failure_size, "The selected floppy image could not be mounted");
      return false;
   }
   strcpy(arc_saved_media, path);
   arc_saved_media_writable = media->writable;
   arc_saved_media_kind = media->kind;
   arc_saved_media_geometry = media->geometry;
   arc_saved_jfd = media->jfd;
   arc_saved_media_data = media->data;
   arc_saved_media_size = media->size;
   arc_saved_media_extent = media->effective_extent;
   ioc_discchange(0);
   return true;
}

static bool arc_arculator_get_av_info(unsigned *width, unsigned *height, double *refresh_hz)
{
   uint64_t sequence;
   if (!arc_machine_live) return false;
   (void)arc_platform_video_frame(width, height, NULL, &sequence);
   if (refresh_hz) *refresh_hz = arc_last_refresh;
   return true;
}
static void arc_arculator_cmos(uint8_t output[ARC_CMOS_PAYLOAD_SIZE])
{
   uint8_t machine[ARC_CMOS_MACHINE_SIZE];
   if (output && arc_machine_live)
   {
      cmos_get_image(machine, sizeof(machine));
      memcpy(output, machine + 16u, ARC_CMOS_PAYLOAD_SIZE);
   }
}
static bool arc_arculator_faulted(const char **message)
{ if (message) *message = arc_fault_text; return arc_fault; }
static bool arc_arculator_active(void) { return arc_active; }

static ArcEngineCapabilities arc_arculator_capabilities(void)
{
   const ArcMachineDescriptor *machine = arc_machine_descriptor(arc_saved_machine);
   ArcEngineCapabilities capabilities = {
      .av = true, .keyboard = true, .mouse = true, .reset = true,
      .boot_watch = true, .launch_outcome = true, .error_box = true,
      .disc_request = true, .floppy = true, .helper = true,
      .ide = machine && machine->ide_capable,
      .native_joystick = machine && machine->joystick_ports,
      .power_on_input = true, .cmos_persistence = true, .serialisation = false
   };
   return capabilities;
}

const ArcEngineBackendOps arc_arculator_backend = {
   .capabilities = arc_arculator_capabilities,
   .start = arc_arculator_start,
   .set_log_callback = arc_arculator_set_log_callback,
   .reset = arc_arculator_reset,
   .stop = arc_arculator_stop,
   .run_frame = arc_arculator_run_frame,
   .power_on_arm = arc_arculator_power_on_arm,
   .power_on_take_release = arc_arculator_power_on_take_release,
   .boot_watch_arm = arc_arculator_boot_watch_arm,
   .boot_watch_cancel = arc_arculator_boot_watch_cancel,
   .boot_watch_state = arc_arculator_boot_watch_state,
   .launch_outcome_arm = arc_arculator_launch_outcome_arm,
   .launch_outcome_cancel = arc_arculator_launch_outcome_cancel,
   .launch_outcome_refused = arc_arculator_launch_outcome_refused,
   .launch_outcome_snapshot = arc_arculator_launch_outcome_snapshot,
   .error_snapshot = arc_arculator_error_snapshot,
   .error_request = arc_arculator_error_request,
   .error_discard = arc_arculator_error_discard,
   .disc_configure = arc_arculator_disc_configure,
   .disc_clear = arc_arculator_disc_clear,
   .disc_snapshot = arc_arculator_disc_snapshot,
   .disc_a_notified = arc_arculator_disc_a_notified,
   .disc_time_us = arc_arculator_disc_time_us,
   .disc_controller_idle = arc_arculator_disc_controller_idle,
   .adf_refusal_callback_set = arc_arculator_adf_refusal_callback_set,
   .audio = arc_arculator_audio,
   .audio_stats = arc_arculator_audio_stats,
   .media_eject = arc_arculator_media_eject,
   .media_insert = arc_arculator_media_insert,
   .get_av_info = arc_arculator_get_av_info,
   .cmos = arc_arculator_cmos,
   .faulted = arc_arculator_faulted,
   .active = arc_arculator_active,
};
