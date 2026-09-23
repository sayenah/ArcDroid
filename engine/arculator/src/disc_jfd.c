/* ArcDroid JFD engine reader. Copyright 2026 ArcDroid contributors.
 * GPL-2.0-only. This is an ArcDroid-authored replacement for Arculator's
 * incomplete JFD reader; Sarah Walker and Arculator contributors are credited
 * for the surrounding floppy integration. No MAME code is incorporated. */
#include "disc_jfd.h"

#include "arc.h"
#include "disc.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum {
   JFD_IDLE = 0,
   JFD_FIND,
   JFD_READ_DATA,
   JFD_READ_ADDRESS,
   JFD_DATA_CRC_ERROR
} JfdState;

static disc_funcs_t jfd_disc_funcs;
static const ArcJfdImage *jfd_image;
static JfdState jfd_state;
static unsigned jfd_track;
static unsigned jfd_side;
static unsigned jfd_density;
static unsigned jfd_sector_id;
static unsigned jfd_transfer_pos;
static size_t jfd_selected;
static uint32_t jfd_phase_us;
static unsigned jfd_search_indexes;
static uint8_t jfd_address[6];

static void jfd_stop(void)
{
   jfd_state = JFD_IDLE;
   jfd_transfer_pos = 0u;
   jfd_selected = 0u;
   jfd_search_indexes = 0u;
}

static void jfd_close(int drive)
{
   (void)drive;
   jfd_stop();
   jfd_image = NULL;
   jfd_phase_us = 0u;
}

void jfd_init(void)
{
   jfd_image = NULL;
   jfd_phase_us = 0u;
   jfd_track = jfd_side = jfd_density = jfd_sector_id = 0u;
   jfd_stop();
}

bool jfd_load(int drive, const ArcJfdImage *image)
{
   if (drive != 0 || !image || !image->decoded || !image->track_count) return false;
   jfd_init();
   jfd_image = image;
   writeprot[drive] = 1;
   drive_funcs[drive] = &jfd_disc_funcs;
   rpclog("Loaded validated JFD descriptors: tracksides=%u sectors=%u\n",
          image->track_count, (unsigned)image->sector_count);
   return true;
}

void jfd_motor_start(void) { jfd_phase_us = 0u; }

static void jfd_seek(int drive, int track)
{
   (void)drive;
   (void)track;
   jfd_stop();
}

static bool jfd_begin(int drive, int track, int side, int density)
{
   if (drive != 0 || !jfd_image || track < 0 || side < 0 || side > 1 || density < 0)
      return false;
   jfd_track = (unsigned)track;
   jfd_side = (unsigned)side;
   jfd_density = fdc_funcs && fdc_funcs->sectorid ?
      (density == 2 ? 4u : density == 3 ? 8u : 2u) :
      (density ? 2u : 1u);
   jfd_transfer_pos = 0u;
   return true;
}

static void jfd_readsector(int drive, int sector, int track, int side, int density)
{
   jfd_stop();
   if (!jfd_begin(drive, track, side, density)) return;
   jfd_sector_id = (unsigned)sector & 0xffu;
   jfd_state = JFD_FIND;
}

static void jfd_writesector(int drive, int sector, int track, int side, int density)
{
   (void)sector;
   jfd_stop();
   if (!jfd_begin(drive, track, side, density)) return;
   fdc_funcs->writeprotect(fdc_p);
}

static void jfd_readaddress(int drive, int track, int side, int density)
{
   jfd_stop();
   if (!jfd_begin(drive, track, side, density)) return;
   jfd_sector_id = UINT_MAX;
   jfd_state = JFD_FIND;
}

static void jfd_format(int drive, int track, int side, int density)
{
   jfd_stop();
   if (!jfd_begin(drive, track, side, density)) return;
   fdc_funcs->writeprotect(fdc_p);
}

static uint16_t jfd_crc_byte(uint16_t crc, uint8_t value)
{
   crc ^= (uint16_t)value << 8u;
   for (unsigned bit = 0u; bit < 8u; ++bit)
      crc = (uint16_t)((crc << 1u) ^ (crc & 0x8000u ? 0x1021u : 0u));
   return crc;
}

static void jfd_make_address(const ArcJfdSector *sector)
{
   jfd_address[0] = (uint8_t)jfd_track;
   jfd_address[1] = (uint8_t)jfd_side;
   jfd_address[2] = sector->id;
   unsigned code = 0u;
   while ((128u << code) < sector->size && code < 3u) ++code;
   jfd_address[3] = (uint8_t)code;
   uint16_t crc = UINT16_C(0xcdb4);
   crc = jfd_crc_byte(crc, 0xfeu);
   for (unsigned i = 0u; i < 4u; ++i) crc = jfd_crc_byte(crc, jfd_address[i]);
   jfd_address[4] = (uint8_t)(crc >> 8u);
   jfd_address[5] = (uint8_t)crc;
}

static bool jfd_interval_contains(uint32_t old_phase, uint32_t new_phase,
                                  uint32_t time)
{
   return new_phase > old_phase ? time >= old_phase && time < new_phase :
                                  time >= old_phase || time < new_phase;
}

static bool jfd_find_sector(uint32_t old_phase, uint32_t new_phase, size_t *selected)
{
   size_t track_side = (size_t)jfd_track * 2u + jfd_side;
   if (!jfd_image || track_side >= jfd_image->track_count) return false;
   const ArcJfdTrack *track = &jfd_image->tracks[track_side];
   uint32_t best_delta = UINT32_MAX;
   bool found = false;
   for (unsigned i = 0u; i < track->sector_count; ++i)
   {
      size_t index = (size_t)track->first_sector + i;
      const ArcJfdSector *sector = &jfd_image->sectors[index];
      if (sector->density != jfd_density ||
          (jfd_sector_id != UINT_MAX && sector->id != jfd_sector_id) ||
          !jfd_interval_contains(old_phase, new_phase, sector->time_us)) continue;
      uint32_t delta = sector->time_us >= old_phase ? sector->time_us - old_phase :
                       200000u - old_phase + sector->time_us;
      if (!found || delta < best_delta)
      {
         found = true;
         best_delta = delta;
         *selected = index;
      }
   }
   return found;
}

static void jfd_poll(void)
{
   uint32_t old_phase = jfd_phase_us;
   uint32_t new_phase = old_phase + disc_poll_period_us();
   bool wrapped = new_phase >= 200000u;
   if (wrapped) new_phase -= 200000u;
   jfd_phase_us = new_phase;
   if (wrapped)
   {
      if (jfd_state == JFD_FIND && fdc_funcs->sectorid)
         ++jfd_search_indexes;
      fdc_funcs->indexpulse(fdc_p);
      if (jfd_state == JFD_FIND && fdc_funcs->sectorid &&
          jfd_search_indexes >= 2u)
      {
         jfd_stop();
         fdc_funcs->notfound(fdc_p);
         return;
      }
   }

   if (jfd_state == JFD_DATA_CRC_ERROR)
   {
      jfd_stop();
      fdc_funcs->datacrcerror(fdc_p);
      return;
   }
   if (jfd_state == JFD_FIND)
   {
      size_t selected;
      if (!jfd_find_sector(old_phase, new_phase, &selected)) return;
      const ArcJfdSector *sector = &jfd_image->sectors[selected];
      if (jfd_sector_id == UINT_MAX)
      {
         if (sector->flags & ARC_JFD_ID_CRC_BAD)
         {
            jfd_stop();
            fdc_funcs->headercrcerror(fdc_p);
            return;
         }
         jfd_make_address(sector);
         if (fdc_funcs->sectorid)
         {
            jfd_stop();
            fdc_funcs->sectorid(jfd_address[0], jfd_address[1],
               jfd_address[2], jfd_address[3], jfd_address[4],
               jfd_address[5], fdc_p);
         }
         else
         {
            jfd_selected = selected;
            jfd_transfer_pos = 0u;
            jfd_state = JFD_READ_ADDRESS;
         }
      }
      else if (sector->flags & ARC_JFD_ID_CRC_BAD)
      {
         jfd_stop();
         fdc_funcs->headercrcerror(fdc_p);
      }
      else if (sector->data_offset != UINT32_MAX)
      {
         jfd_selected = selected;
         jfd_transfer_pos = 0u;
         jfd_state = JFD_READ_DATA;
      }
      /* An ID-only sector remains searchable until controller RNF timeout. */
      return;
   }
   if (jfd_state == JFD_READ_ADDRESS)
   {
      fdc_funcs->data(jfd_address[jfd_transfer_pos++], fdc_p);
      if (jfd_state != JFD_READ_ADDRESS) return;
      if (jfd_transfer_pos == sizeof(jfd_address))
      {
         jfd_stop();
         fdc_funcs->finishread(fdc_p);
      }
      return;
   }
   if (jfd_state == JFD_READ_DATA)
   {
      const ArcJfdSector *sector = &jfd_image->sectors[jfd_selected];
      fdc_funcs->data(jfd_image->decoded[sector->data_offset + jfd_transfer_pos], fdc_p);
      if (jfd_state != JFD_READ_DATA) return;
      ++jfd_transfer_pos;
      if (jfd_transfer_pos == sector->size)
      {
         bool bad_crc = (sector->flags & ARC_JFD_DATA_CRC_BAD) != 0u;
         if (bad_crc) jfd_state = JFD_DATA_CRC_ERROR;
         else
         {
            jfd_stop();
            fdc_funcs->finishread(fdc_p);
         }
      }
   }
}

static disc_funcs_t jfd_disc_funcs = {
   .seek = jfd_seek,
   .readsector = jfd_readsector,
   .writesector = jfd_writesector,
   .readaddress = jfd_readaddress,
   .format = jfd_format,
   .stop = jfd_stop,
   .poll = jfd_poll,
   .close = jfd_close
};
