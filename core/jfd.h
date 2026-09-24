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

enum {
   ARC_JFD_ID_CRC_BAD = 1u,
   ARC_JFD_DATA_CRC_BAD = 2u,
   ARC_JFD_SLOW_SECTOR = 4u
};

typedef struct {
   uint32_t time_us;
   uint32_t data_offset;
   uint16_t size;
   uint8_t id;
   uint8_t density;
   uint8_t flags;
} ArcJfdSector;

typedef struct {
   uint32_t first_sector;
   uint16_t sector_count;
   bool timed;
} ArcJfdTrack;

struct ArcJfdImage;
typedef struct ArcJfdOverlay {
   uint8_t **data;                 /* One owned replacement per descriptor. */
   uint8_t *eligible;
   uint64_t generation;
   unsigned records;
   bool writable;
   bool visible;
   bool damaged;
   bool failed;
   const char *reason;             /* Static diagnostic reason from preparation. */
   bool (*commit)(struct ArcJfdImage *, size_t, const uint8_t *);
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
} ArcJfdImage;

static inline bool arc_jfd_sector_write_eligible(const ArcJfdImage *image,
                                                  size_t index)
{
   if (!image || index >= image->sector_count) return false;
   const ArcJfdSector *sector = &image->sectors[index];
   if (sector->data_offset == UINT32_MAX ||
       (sector->flags & (ARC_JFD_ID_CRC_BAD | ARC_JFD_DATA_CRC_BAD |
                         ARC_JFD_SLOW_SECTOR))) return false;
   size_t track_side = 0u;
   while (track_side < image->track_count &&
          index >= (size_t)image->tracks[track_side].first_sector +
                   image->tracks[track_side].sector_count) ++track_side;
   if (track_side == image->track_count) return false;
   const ArcJfdTrack *track = &image->tracks[track_side];
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
