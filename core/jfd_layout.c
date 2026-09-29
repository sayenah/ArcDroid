/* ArcDroid P-DD5 creation plan and effective JFD sector view.
 * Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "jfd.h"

#include <limits.h>
#include <string.h>

#define REVOLUTION_US 200000u
#define PDD5_ALL 0x1fu

/* D-077: a scheduled but uncommitted formatted sector has immutable zeros.
 * No overlay record is allocated until a successful guest write. */
static const uint8_t formatted_blank[1024] = {0};

/* A sector occupies its complete DD transfer interval. Test the periodic
 * copies on either side of the candidate so index-wrap overlaps count. */
static bool overlap(uint64_t a, uint64_t b, unsigned spacing)
{
   return a < b + spacing && b < a + spacing;
}

bool arc_jfd_place_slots(const ArcJfdImage *image, unsigned track_side,
                         unsigned ids, unsigned spacing, uint16_t captured,
                         uint32_t positions[ARC_JFD_MAX_STANDARD_IDS])
{
   if (!image || !positions || track_side >= image->track_count ||
       ids > ARC_JFD_MAX_STANDARD_IDS || !spacing) return false;
   const ArcJfdTrack *track = &image->tracks[track_side];
   if (track->quarantine_reason) return false;
   uint32_t occupied[ARC_JFD_MAX_SECTORS_PER_TRACK + ARC_JFD_MAX_STANDARD_IDS];
   unsigned count = 0u;
   for (unsigned i = 0u; i < track->sector_count; ++i)
      occupied[count++] = image->sectors[track->first_sector + i].time_us;
   if (!track->timed)
   {
      uint32_t next = count ? occupied[count - 1u] + spacing : 0u;
      for (unsigned id = 0u; id < ids; ++id)
         if (!(captured & (1u << id)))
         {
            if (next > REVOLUTION_US - spacing) return false;
            positions[id] = next;
            next += spacing;
         }
      return true;
   }
   /* JASPP stores captured starts to whole milliseconds. Keep those starts
    * even when quantisation makes their transfer intervals overlap; only
    * newly placed sectors must avoid every occupied interval. */

   /* Version 1 placement: missing IDs ascend. The nearest lower ID already
    * present in the base or plan is the predecessor. Search from its end,
    * jumping over occupied intervals, for at most one revolution. */
   for (unsigned id = 0u; id < ids; ++id)
   {
      if (captured & (1u << id)) continue;
      uint64_t start = 0u;
      for (int prior = (int)id - 1; prior >= 0; --prior)
      {
         if (captured & (1u << prior))
         {
            for (unsigned i = 0u; i < track->sector_count; ++i)
               if (image->sectors[track->first_sector + i].id == prior)
                  start = image->sectors[track->first_sector + i].time_us +
                          spacing;
            break;
         }
         start = positions[prior] + spacing;
         break;
      }
      uint64_t limit = start + REVOLUTION_US;
      bool placed = false;
      while (start < limit)
      {
         uint64_t advance = start;
         for (unsigned i = 0u; i < count; ++i)
         {
            uint64_t base = occupied[i];
            for (int cycle = -1; cycle <= 2; ++cycle)
            {
               int64_t other = (int64_t)base +
                               (int64_t)cycle * REVOLUTION_US;
               if (other < 0)
               {
                  if (other + spacing > (int64_t)start)
                     advance = (uint64_t)(other + spacing);
               }
               else if (overlap(start, (uint64_t)other, spacing) &&
                        (uint64_t)other + spacing > advance)
                  advance = (uint64_t)other + spacing;
            }
         }
         if (advance == start)
         {
            uint32_t phase = (uint32_t)(start % REVOLUTION_US);
            positions[id] = phase;
            occupied[count++] = phase;
            placed = true;
            break;
         }
         start = advance;
      }
      if (!placed) return false;
   }
   return true;
}

bool arc_jfd_plan_creation(const ArcJfdImage *image, ArcJfdOverlay *overlay)
{
   if (!image || !overlay || !overlay->eligible) return false;
   /* The file has no descriptor table for missing track-sides, so they cannot
    * be created even if the captured sides otherwise establish the profile. */
   if (image->track_count < ARC_JFD_PDD5_TRACKS) return false;
   unsigned compatible = 0u, nonempty = 0u, full = 0u;
   bool irregular_missing = false, omitted_regular = false;
   bool irregular[ARC_JFD_PDD5_TRACKS] = {false};
   uint8_t masks[ARC_JFD_PDD5_TRACKS] = {0};
   uint8_t writable_masks[ARC_JFD_PDD5_TRACKS] = {0};
   unsigned tracks = ARC_JFD_PDD5_TRACKS;
   for (unsigned t = 0u; t < tracks; ++t)
   {
      const ArcJfdTrack *track = &image->tracks[t];
      if (track->sector_count) ++nonempty;
      if (track->quarantine_reason)
      {
         irregular[t] = true;
         irregular_missing = true;
         continue;
      }
      for (unsigned i = 0u; i < track->sector_count; ++i)
      {
         size_t index = (size_t)track->first_sector + i;
         const ArcJfdSector *sector = &image->sectors[index];
         uint8_t bit = sector->id < ARC_JFD_PDD5_IDS ?
                       (uint8_t)(1u << sector->id) : 0u;
         bool duplicate = (masks[t] & bit) != 0u;
         if (sector->id >= ARC_JFD_PDD5_IDS || sector->size != 1024u ||
             sector->density != 2u || !overlay->eligible[index] ||
             duplicate) irregular[t] = true;
         if (bit)
         {
            masks[t] |= bit;
            if (duplicate)
               writable_masks[t] &= (uint8_t)~bit;
            else if (sector->size == 1024u && sector->density == 2u &&
                     overlay->eligible[index])
               writable_masks[t] |= bit;
         }
      }
      if (irregular[t])
      {
         if (writable_masks[t] != PDD5_ALL) irregular_missing = true;
      }
      else
      {
         if (track->sector_count) ++compatible;
         if (masks[t] == PDD5_ALL) ++full;
         else omitted_regular = true;
      }
   }
   overlay->pdd5_profile = image->track_count >= ARC_JFD_PDD5_TRACKS &&
      full && compatible * 2u > nonempty;
   if (omitted_regular && !overlay->pdd5_profile) return false;
   if (irregular_missing)
   {
      bool free_track[ARC_JFD_PDD5_TRACKS] = {false};
      if (!arc_jfd_base_free_tracks(image, free_track)) return false;
      for (unsigned t = 0u; t < tracks; ++t)
         if (irregular[t] && writable_masks[t] != PDD5_ALL && free_track[t])
            return false;
   }
   if (overlay->pdd5_profile)
      for (unsigned t = 0u; t < tracks; ++t)
      {
         if (irregular[t] || masks[t] == PDD5_ALL) continue;
         uint32_t positions[ARC_JFD_MAX_STANDARD_IDS] = {0};
         if (!arc_jfd_place_slots(image, t, ARC_JFD_PDD5_IDS,
                                  ARC_JFD_PDD5_SPACING_US, masks[t], positions))
            return false;
         for (unsigned id = 0u; id < ARC_JFD_PDD5_IDS; ++id)
            if (!(masks[t] & (1u << id)))
            {
               overlay->creation_time[t][id] = positions[id];
               overlay->creation_mask[t] |= (uint8_t)(1u << id);
               ++overlay->creation_capacity;
            }
      }
   overlay->layout_supported = true;
   return true;
}

bool arc_jfd_effective_sector(const ArcJfdImage *image, unsigned track_side,
                              unsigned candidate, bool prospective,
                              ArcJfdView *view)
{
   if (!image || !view || track_side >= image->track_count) return false;
   const ArcJfdTrack *track = &image->tracks[track_side];
   const ArcJfdOverlay *overlay = image->overlay;
   memset(view, 0, sizeof(*view));
   if (track->quarantine_reason) return false;
   if (candidate < track->sector_count)
   {
      size_t index = (size_t)track->first_sector + candidate;
      view->sector = image->sectors[index];
      view->key = index;
      view->id_visible = true;
      view->write_eligible = overlay && overlay->writable &&
         (overlay->eligible ? overlay->eligible[index] :
          arc_jfd_sector_write_eligible(image, index));
      if (view->sector.data_offset != UINT32_MAX)
         view->data = overlay && overlay->visible && overlay->data &&
            overlay->data[index] ? overlay->data[index] :
            image->decoded + view->sector.data_offset;
      return true;
   }
   unsigned id = candidate - track->sector_count;
   bool presented = track_side == 0u && id < image->identification_schedule &&
      (image->identification_mask & (1u << id));
   bool creatable = overlay && track_side < ARC_JFD_PDD5_TRACKS &&
      id < ARC_JFD_PDD5_IDS &&
      (overlay->creation_mask[track_side] & (1u << id));
   bool file_data = track_side < ARC_JFD_PDD5_TRACKS &&
      id < ARC_JFD_PDD5_IDS &&
      (image->file_data.mask[track_side] & (1u << id));
   if (id >= ARC_JFD_MAX_STANDARD_IDS || (!presented && !creatable && !file_data))
      return false;
   const uint8_t *data = creatable && overlay->visible ?
      overlay->created[track_side][id] : NULL;
   if (!data && creatable && overlay->map_enabled && overlay->map_valid &&
       (overlay->allocation_mask[track_side] & (1u << id)) &&
       !(overlay->probe_mask[track_side] & (1u << id)))
      data = formatted_blank;
   if (!data && file_data) data = formatted_blank;
   if (!data && !presented && !file_data &&
       !(prospective && creatable && overlay->writable))
      return false;
   view->sector.time_us = presented ? image->identification_time[id] :
      creatable ? overlay->creation_time[track_side][id] :
      image->file_data.time[track_side][id];
   view->sector.data_offset = UINT32_MAX;
   view->sector.size = 1024u;
   view->sector.id = (uint8_t)id;
   view->sector.density = image->identification_schedule == 10u && presented ?
      4u : 2u;
   view->data = data;
   view->key = id;
   view->created = creatable;
   view->id_visible = true;
   view->write_eligible = creatable && overlay->writable;
   return true;
}
