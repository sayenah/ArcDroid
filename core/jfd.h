/* ArcDroid JFD parser. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#ifndef ARCDROID_JFD_H
#define ARCDROID_JFD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARC_JFD_MAX_COMPRESSED (8u * 1024u * 1024u)
#define ARC_JFD_MAX_DECODED (8u * 1024u * 1024u)
#define ARC_JFD_MAX_TRACK_SIDES 166u
#define ARC_JFD_MAX_SECTORS_PER_TRACK 256u
#define ARC_JFD_PDD5_TRACKS 160u
#define ARC_JFD_PDD5_IDS 5u
#define ARC_JFD_PDD5_SLOTS (ARC_JFD_PDD5_TRACKS * ARC_JFD_PDD5_IDS)
#define ARC_JFD_PDD5_SPACING_US 36352u
#define ARC_JFD_F_SPACING_US 18176u
#define ARC_JFD_MAX_STANDARD_IDS 10u

static inline bool arc_jfd_pdd5_free_intersects(unsigned track_side,
                                                size_t first, size_t length)
{
   return track_side < ARC_JFD_PDD5_TRACKS && length <= SIZE_MAX - first &&
      first < ((size_t)track_side + 1u) * 5120u &&
      (size_t)track_side * 5120u < first + length;
}

enum {
   ARC_JFD_ID_CRC_BAD = 1u,
   ARC_JFD_DATA_CRC_BAD = 2u,
   ARC_JFD_SLOW_SECTOR = 4u
};

typedef struct {
   uint32_t time_us;
   uint32_t data_offset;
   uint32_t size; /* Full 128 << size_code payload extent, even if hidden. */
   uint8_t id;
   uint8_t density;
   uint8_t flags;
} ArcJfdSector;

typedef struct {
   uint32_t first_sector;
   uint16_t sector_count;
   bool timed;
   const char *quarantine_reason; /* First native-model limitation on this side. */
} ArcJfdTrack;

struct ArcJfdImage;
typedef struct ArcJfdOverlay {
   uint8_t **data;                 /* One owned replacement per descriptor. */
   uint8_t *created[ARC_JFD_PDD5_TRACKS][ARC_JFD_PDD5_IDS];
   uint32_t creation_time[ARC_JFD_PDD5_TRACKS][ARC_JFD_PDD5_IDS];
   uint8_t creation_mask[ARC_JFD_PDD5_TRACKS];
   uint8_t *eligible;
   unsigned image_number;
   unsigned creation_capacity;
   bool pdd5_profile;
   bool layout_supported;
   uint64_t generation;
   unsigned records;
   char store_sha1[41]; /* Validated active snapshot; empty without a store. */
   bool writable;
   bool visible;
   uint8_t allocation_mask[ARC_JFD_PDD5_TRACKS];
   uint8_t probe_mask[ARC_JFD_PDD5_TRACKS]; /* Mounted lifetime only. */
   uint8_t map_record[60];
   uint8_t map_kind;              /* 0 unavailable, 1 old, 2 one-zone new. */
   bool map_enabled;              /* Successful writable native preparation. */
   bool map_valid;                /* At least one valid effective copy. */
   bool map_unavailable_logged;
   bool damaged;
   bool failed;
   const char *reason;             /* Static diagnostic reason from preparation. */
   bool (*commit)(struct ArcJfdImage *, size_t, const uint8_t *);
   bool (*commit_created)(struct ArcJfdImage *, unsigned, unsigned,
                          const uint8_t *);
   void (*log_refusal)(unsigned, unsigned, unsigned, unsigned, const char *);
   void (*log_map_unavailable)(unsigned);
   void *store;
} ArcJfdOverlay;

typedef struct ArcJfdImage {
   uint8_t *decoded;
   size_t decoded_size;
   ArcJfdSector *sectors;
   size_t sector_count;
   ArcJfdTrack tracks[ARC_JFD_MAX_TRACK_SIDES];
   unsigned track_count;
   char title[256];
   uint32_t sequence;
   uint16_t disc_number;
   uint16_t disc_count;
   uint32_t game_id;
   uint32_t flags;
   uint32_t obey_length;
   uint8_t fps_times_two;
   bool extended;
   bool ignored_header_fields;
   ArcJfdOverlay *overlay;
   /* Derived only from captured identification-track payloads at preparation. */
   uint16_t identification_mask;
   uint32_t identification_time[ARC_JFD_MAX_STANDARD_IDS];
   uint8_t identification_schedule;
   uint8_t identification_maps;
   char identification_format;
   bool identification_timed;
   const char *identification_basis;
   const char *identification_reason;
} ArcJfdImage;

typedef struct {
   ArcJfdSector sector;
   const uint8_t *data;
   size_t key; /* Base descriptor index, or P-DD5 sector ID for a creation. */
   bool created;
   bool id_visible;
   bool write_eligible;
} ArcJfdView;

static inline bool arc_jfd_high_density(const ArcJfdImage *image)
{
   if (!image) return false;
   for (unsigned t = 0u; t < image->track_count; ++t)
      if (!image->tracks[t].quarantine_reason)
         for (unsigned i = 0u; i < image->tracks[t].sector_count; ++i)
            if (image->sectors[image->tracks[t].first_sector + i].density == 4u)
               return true;
   return false;
}

/* P-DD5 placement version 1, derived from the immutable base. Each
 * track-side's descriptors retain their ordinals; candidates follow in ID order. */
bool arc_jfd_plan_creation(const ArcJfdImage *image, ArcJfdOverlay *overlay);
bool arc_jfd_place_slots(const ArcJfdImage *image, unsigned track_side,
                         unsigned ids, unsigned spacing, uint16_t captured,
                         uint32_t positions[ARC_JFD_MAX_STANDARD_IDS]);
void arc_jfd_prepare_identification(ArcJfdImage *image, bool native);
bool arc_jfd_effective_sector(const ArcJfdImage *image, unsigned track_side,
                              unsigned candidate, bool prospective,
                              ArcJfdView *view);
bool arc_jfd_base_free_tracks(const ArcJfdImage *image,
                              bool free_track[ARC_JFD_PDD5_TRACKS]);
void arc_jfd_map_prepare(ArcJfdImage *image);
void arc_jfd_map_refresh(ArcJfdImage *image);
bool arc_jfd_map_write_veto(const ArcJfdImage *image, size_t index,
                            const uint8_t *pending);
bool arc_jfd_map_target(const ArcJfdImage *image, size_t index);

static inline bool arc_jfd_sector_write_eligible(const ArcJfdImage *image,
                                                  size_t index)
{
   if (!image || index >= image->sector_count) return false;
   const ArcJfdSector *sector = &image->sectors[index];
   if (sector->data_offset == UINT32_MAX || sector->size > 1024u ||
       (sector->flags & (ARC_JFD_ID_CRC_BAD | ARC_JFD_DATA_CRC_BAD |
                         ARC_JFD_SLOW_SECTOR))) return false;
   size_t track_side = 0u;
   while (track_side < image->track_count &&
          index >= (size_t)image->tracks[track_side].first_sector +
                   image->tracks[track_side].sector_count) ++track_side;
   if (track_side == image->track_count) return false;
   const ArcJfdTrack *track = &image->tracks[track_side];
   if (track->quarantine_reason) return false;
   for (unsigned i = 0u; i < track->sector_count; ++i)
   {
      size_t other_index = (size_t)track->first_sector + i;
      if (other_index == index) continue;
      const ArcJfdSector *other = &image->sectors[other_index];
      if (other->id == sector->id && other->density == sector->density)
         return false;
   }
   for (size_t i = 0u; i < image->sector_count; ++i)
   {
      if (i == index) continue;
      const ArcJfdSector *other = &image->sectors[i];
      if (other->data_offset != UINT32_MAX &&
          (uint64_t)sector->data_offset < (uint64_t)other->data_offset + other->size &&
          (uint64_t)other->data_offset < (uint64_t)sector->data_offset + sector->size)
         return false;
   }
   return true;
}

bool arc_jfd_has_signature(const uint8_t *input, size_t input_size);
bool arc_jfd_parse(const uint8_t *input, size_t input_size, ArcJfdImage *image,
                   char *error, size_t error_size);
/* Consumes a malloc-owned, already decoded JFDI buffer on success or failure. */
bool arc_jfd_parse_owned(uint8_t *decoded, size_t decoded_size,
                         ArcJfdImage *image, char *error, size_t error_size);
void arc_jfd_free(ArcJfdImage *image);

#endif
