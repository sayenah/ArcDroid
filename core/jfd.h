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

typedef struct {
   uint8_t *decoded;
   size_t decoded_size;
   ArcJfdSector *sectors;
   size_t sector_count;
   ArcJfdTrack tracks[ARC_JFD_MAX_TRACK_SIDES];
   unsigned track_count;
   char title[256];
   uint32_t sequence;
   uint32_t flags;
   uint32_t obey_length;
   uint8_t fps_times_two;
   bool extended;
   bool ignored_header_fields;
} ArcJfdImage;

bool arc_jfd_has_signature(const uint8_t *input, size_t input_size);
bool arc_jfd_parse(const uint8_t *input, size_t input_size, ArcJfdImage *image,
                   char *error, size_t error_size);
/* Consumes a malloc-owned, already decoded JFDI buffer on success or failure. */
bool arc_jfd_parse_owned(uint8_t *decoded, size_t decoded_size,
                         ArcJfdImage *image, char *error, size_t error_size);
void arc_jfd_free(ArcJfdImage *image);

#endif
