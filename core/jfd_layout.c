/* ArcDroid P-DD5 creation plan and effective JFD sector view.
 * Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "jfd.h"

#include <limits.h>
#include <string.h>

#define REVOLUTION_US 200000u
#define PDD5_ALL 0x1fu

/* A sector occupies its complete DD transfer interval. Test the periodic
 * copies on either side of the candidate so index-wrap overlaps count. */
static bool overlap(uint64_t a, uint64_t b)
{
   return a < b + ARC_JFD_PDD5_SPACING_US &&
          b < a + ARC_JFD_PDD5_SPACING_US;
}

static bool schedule_timed(const ArcJfdImage *image, ArcJfdOverlay *overlay,
                           unsigned track_side, uint8_t captured)
{
   const ArcJfdTrack *track = &image->tracks[track_side];
   uint32_t occupied[ARC_JFD_PDD5_IDS];
   unsigned count = 0u;
   for (unsigned i = 0u; i < track->sector_count; ++i)
      occupied[count++] = image->sectors[track->first_sector + i].time_us;
   for (unsigned i = 0u; i < count; ++i)
      for (unsigned j = i + 1u; j < count; ++j)
         for (int shift = -1; shift <= 1; ++shift)
            if (overlap(occupied[i] + REVOLUTION_US,
                        (uint64_t)occupied[j] + (shift + 1) * REVOLUTION_US))
               return false;

   /* Version 1 placement: missing IDs ascend. The nearest lower ID already
    * present in the base or plan is the predecessor. Search from its end,
    * jumping over occupied intervals, for at most one revolution. */
   for (unsigned id = 0u; id < ARC_JFD_PDD5_IDS; ++id)
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
                          ARC_JFD_PDD5_SPACING_US;
            break;
         }
         if (overlay->creation_mask[track_side] & (1u << prior))
         {
            start = overlay->creation_time[track_side][prior] +
                    ARC_JFD_PDD5_SPACING_US;
            break;
         }
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
                  if (other + ARC_JFD_PDD5_SPACING_US > (int64_t)start)
                     advance = (uint64_t)(other + ARC_JFD_PDD5_SPACING_US);
               }
               else if (overlap(start, (uint64_t)other) &&
                        (uint64_t)other + ARC_JFD_PDD5_SPACING_US > advance)
                  advance = (uint64_t)other + ARC_JFD_PDD5_SPACING_US;
            }
         }
         if (advance == start)
         {
            uint32_t phase = (uint32_t)(start % REVOLUTION_US);
            overlay->creation_time[track_side][id] = phase;
            overlay->creation_mask[track_side] |= (uint8_t)(1u << id);
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
   unsigned compatible = 0u, nonempty = 0u, full = 0u;
   bool irregular_missing = false, omitted_regular = false;
   bool irregular[ARC_JFD_PDD5_TRACKS] = {false};
   uint8_t masks[ARC_JFD_PDD5_TRACKS] = {0};
   unsigned tracks = image->track_count < ARC_JFD_PDD5_TRACKS ?
                     image->track_count : ARC_JFD_PDD5_TRACKS;
   for (unsigned t = 0u; t < tracks; ++t)
   {
      const ArcJfdTrack *track = &image->tracks[t];
      if (track->sector_count) ++nonempty;
      for (unsigned i = 0u; i < track->sector_count; ++i)
      {
         size_t index = (size_t)track->first_sector + i;
         const ArcJfdSector *sector = &image->sectors[index];
         if (sector->id >= ARC_JFD_PDD5_IDS || sector->size != 1024u ||
             sector->density != 2u || !overlay->eligible[index] ||
             (masks[t] & (1u << sector->id))) irregular[t] = true;
         if (sector->id < ARC_JFD_PDD5_IDS)
            masks[t] |= (uint8_t)(1u << sector->id);
      }
      if (irregular[t])
      {
         if (masks[t] != PDD5_ALL) irregular_missing = true;
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
         if (irregular[t] && masks[t] != PDD5_ALL && free_track[t])
            return false;
   }
   if (overlay->pdd5_profile)
      for (unsigned t = 0u; t < tracks; ++t)
      {
         const ArcJfdTrack *track = &image->tracks[t];
         if (irregular[t] || masks[t] == PDD5_ALL) continue;
         if (track->timed)
         {
            if (!schedule_timed(image, overlay, t, masks[t])) return false;
         }
         else
         {
            uint32_t next = track->sector_count ?
               image->sectors[track->first_sector + track->sector_count - 1u].time_us +
               ARC_JFD_PDD5_SPACING_US : 0u;
            for (unsigned id = 0u; id < ARC_JFD_PDD5_IDS; ++id)
               if (!(masks[t] & (1u << id)))
               {
                  if (next > REVOLUTION_US - ARC_JFD_PDD5_SPACING_US)
                     return false;
                  overlay->creation_time[t][id] = next;
                  overlay->creation_mask[t] |= (uint8_t)(1u << id);
                  next += ARC_JFD_PDD5_SPACING_US;
               }
         }
         for (unsigned id = 0u; id < ARC_JFD_PDD5_IDS; ++id)
            if (overlay->creation_mask[t] & (1u << id))
               ++overlay->creation_capacity;
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
   if (candidate < track->sector_count)
   {
      size_t index = (size_t)track->first_sector + candidate;
      view->sector = image->sectors[index];
      view->key = index;
      if (view->sector.data_offset != UINT32_MAX)
         view->data = overlay && overlay->visible && overlay->data &&
            overlay->data[index] ? overlay->data[index] :
            image->decoded + view->sector.data_offset;
      return true;
   }
   unsigned id = candidate - track->sector_count;
   if (track_side >= ARC_JFD_PDD5_TRACKS || id >= ARC_JFD_PDD5_IDS ||
       !overlay || !(overlay->creation_mask[track_side] & (1u << id)))
      return false;
   const uint8_t *data = overlay->visible ? overlay->created[track_side][id] : NULL;
   if (!data && !(prospective && overlay->writable)) return false;
   view->sector.time_us = overlay->creation_time[track_side][id];
   view->sector.data_offset = UINT32_MAX;
   view->sector.size = 1024u;
   view->sector.id = (uint8_t)id;
   view->sector.density = 2u;
   view->data = data;
   view->key = id;
   view->created = true;
   return true;
}
