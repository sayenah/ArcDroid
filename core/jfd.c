/* ArcDroid hardened JFD parser. Copyright 2026 ArcDroid contributors.
 * GPL-2.0-only. Format behaviour was independently derived from the ADFFS
 * 2.84 specification and upstream Arculator; no MAME code is incorporated. */
#include "jfd.h"
#include "zlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t arc_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
          ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static void arc_jfd_error(char *error, size_t size, const char *reason)
{
   if (error && size) snprintf(error, size, "JFD: %s", reason);
}

static voidpf arc_zalloc(voidpf opaque, uInt items, uInt size)
{
   (void)opaque;
   if (size && items > SIZE_MAX / size) return Z_NULL;
   return calloc(items, size);
}

static void arc_zfree(voidpf opaque, voidpf address)
{
   (void)opaque;
   free(address);
}

static bool arc_gzip_decode(const uint8_t *input, size_t input_size,
                            uint8_t **output, size_t *output_size,
                            char *error, size_t error_size)
{
   if (input_size > ARC_JFD_MAX_COMPRESSED)
   { arc_jfd_error(error, error_size, "compressed input exceeds 8 MiB"); return false; }
   if (input_size < 18u)
   { arc_jfd_error(error, error_size, "truncated gzip stream"); return false; }
   uint8_t *decoded = malloc(ARC_JFD_MAX_DECODED);
   if (!decoded)
   { arc_jfd_error(error, error_size, "out of memory"); return false; }
   z_stream stream;
   memset(&stream, 0, sizeof(stream));
   stream.zalloc = arc_zalloc;
   stream.zfree = arc_zfree;
   int status = inflateInit2(&stream, 15 + 16);
   if (status != Z_OK)
   {
      free(decoded);
      arc_jfd_error(error, error_size, "gzip inflater initialization failed");
      return false;
   }
   size_t input_at = 0u;
   size_t decoded_size = 0u;
   while (input_at < input_size)
   {
      if (input_size - input_at < 18u || input[input_at] != 0x1fu ||
          input[input_at + 1u] != 0x8bu)
      {
         (void)inflateEnd(&stream);
         free(decoded);
         arc_jfd_error(error, error_size, input_at ?
                       "non-gzip trailing data" : "truncated gzip stream");
         return false;
      }
      stream.next_in = (Bytef *)(uintptr_t)(input + input_at);
      stream.avail_in = (uInt)(input_size - input_at);
      stream.next_out = decoded + decoded_size;
      stream.avail_out = (uInt)(ARC_JFD_MAX_DECODED - decoded_size);
      status = Z_OK;
      uLong previous_in = ~(uLong)0;
      uLong previous_out = ~(uLong)0;
      while (status == Z_OK)
      {
         previous_in = stream.total_in;
         previous_out = stream.total_out;
         status = inflate(&stream, Z_NO_FLUSH);
         if (status == Z_OK && previous_in == stream.total_in &&
             previous_out == stream.total_out)
         { status = Z_BUF_ERROR; break; }
         if (status == Z_OK && stream.avail_out == 0u) break;
      }
      size_t member_size = (size_t)stream.total_out;
      size_t consumed = (size_t)stream.total_in;
      if (status != Z_STREAM_END || consumed < 18u)
      {
         bool at_cap = stream.avail_out == 0u;
         (void)inflateEnd(&stream);
         free(decoded);
         arc_jfd_error(error, error_size, at_cap ?
                       "decoded output exceeds 8 MiB" :
                       "truncated or corrupt gzip stream");
         return false;
      }
      uint32_t expected_crc = arc_le32(input + input_at + consumed - 8u);
      uint32_t expected_size = arc_le32(input + input_at + consumed - 4u);
      uint32_t actual_crc = (uint32_t)crc32(0L, decoded + decoded_size,
                                            (uInt)member_size);
      if (actual_crc != expected_crc || expected_size != (uint32_t)member_size)
      {
         (void)inflateEnd(&stream);
         free(decoded);
         arc_jfd_error(error, error_size, actual_crc != expected_crc ?
                       "gzip CRC32 mismatch" : "gzip ISIZE mismatch");
         return false;
      }
      input_at += consumed;
      decoded_size += member_size;
      if (input_at < input_size && inflateReset2(&stream, 15 + 16) != Z_OK)
      {
         (void)inflateEnd(&stream);
         free(decoded);
         arc_jfd_error(error, error_size, "gzip inflater reset failed");
         return false;
      }
   }
   (void)inflateEnd(&stream);
   uint8_t *shrunk = realloc(decoded, decoded_size ? decoded_size : 1u);
   if (shrunk) decoded = shrunk;
   *output = decoded;
   *output_size = decoded_size;
   return true;
}

bool arc_jfd_has_signature(const uint8_t *input, size_t input_size)
{
   return input && ((input_size >= 4u && !memcmp(input, "JFDI", 4u)) ||
                    (input_size >= 2u && input[0] == 0x1fu && input[1] == 0x8bu));
}

void arc_jfd_free(ArcJfdImage *image)
{
   if (!image) return;
   free(image->decoded);
   free(image->sectors);
   memset(image, 0, sizeof(*image));
}

static bool arc_jfd_append_sector(ArcJfdImage *image, const ArcJfdSector *sector)
{
   if (image->sector_count == SIZE_MAX / sizeof(*image->sectors)) return false;
   ArcJfdSector *grown = realloc(image->sectors,
      (image->sector_count + 1u) * sizeof(*image->sectors));
   if (!grown) return false;
   image->sectors = grown;
   image->sectors[image->sector_count++] = *sector;
   return true;
}

static bool arc_jfd_parse_internal(const uint8_t *input, size_t input_size,
                                   ArcJfdImage *image, char *error,
                                   size_t error_size, bool take_plain_ownership)
{
   uint8_t *decoded = NULL;
   size_t decoded_size = 0u;
   if (!input || !image)
   {
      if (take_plain_ownership) free((void *)(uintptr_t)input);
      arc_jfd_error(error, error_size, "invalid parser input");
      return false;
   }
   memset(image, 0, sizeof(*image));
   if (input_size >= 2u && input[0] == 0x1fu && input[1] == 0x8bu)
   {
      if (take_plain_ownership)
      {
         free((void *)(uintptr_t)input);
         arc_jfd_error(error, error_size, "owned decoded input is gzip-wrapped");
         return false;
      }
      if (!arc_gzip_decode(input, input_size, &decoded, &decoded_size, error, error_size))
         return false;
   }
   else
   {
      if (input_size > ARC_JFD_MAX_DECODED)
      {
         if (take_plain_ownership) free((void *)(uintptr_t)input);
         arc_jfd_error(error, error_size, "decoded input exceeds 8 MiB");
         return false;
      }
      if (take_plain_ownership)
         decoded = (uint8_t *)(uintptr_t)input;
      else
      {
         decoded = malloc(input_size ? input_size : 1u);
         if (!decoded)
         { arc_jfd_error(error, error_size, "out of memory"); return false; }
         memcpy(decoded, input, input_size);
      }
      decoded_size = input_size;
   }
   image->decoded = decoded;
   image->decoded_size = decoded_size;
   if (decoded_size < 304u || memcmp(decoded, "JFDI", 4u))
   { arc_jfd_error(error, error_size, "missing JFDI signature or header is truncated"); goto reject; }
   if (arc_le32(decoded + 8u) != decoded_size)
   { arc_jfd_error(error, error_size, "declared decoded length mismatch"); goto reject; }
   uint32_t offset_track = arc_le32(decoded + 24u);
   uint32_t offset_sector = arc_le32(decoded + 28u);
   uint32_t offset_data = arc_le32(decoded + 32u);
   if (arc_le32(decoded + 36u) || arc_le32(decoded + 40u) || arc_le32(decoded + 44u))
   { arc_jfd_error(error, error_size, "delta tables are unsupported"); goto reject; }
   image->extended = offset_track >= 316u;
   if (offset_track < 304u || (offset_track & 3u) || (offset_sector & 3u) ||
       (offset_data & 3u) || offset_track >= offset_sector ||
       offset_sector > offset_data || offset_data > decoded_size)
   { arc_jfd_error(error, error_size, "invalid table offsets"); goto reject; }
   if ((offset_sector - offset_track) & 3u)
   { arc_jfd_error(error, error_size, "track table length is not aligned"); goto reject; }
   unsigned track_count = (offset_sector - offset_track) / 4u;
   if (track_count < 2u || track_count > ARC_JFD_MAX_TRACK_SIDES)
   { arc_jfd_error(error, error_size, "track-side count is outside 2..166"); goto reject; }
   image->track_count = track_count;
   image->sequence = arc_le32(decoded + 12u);
   image->disc_number = (uint16_t)(decoded[12] | ((uint16_t)decoded[13] << 8u));
   image->disc_count = (uint16_t)(decoded[14] | ((uint16_t)decoded[15] << 8u));
   image->game_id = arc_le32(decoded + 16u);
   if (image->extended)
   {
      if (decoded_size < 316u)
      { arc_jfd_error(error, error_size, "extended header is truncated"); goto reject; }
      image->flags = arc_le32(decoded + 304u);
      image->fps_times_two = decoded[308u];
      image->obey_length = arc_le32(decoded + 312u);
      image->ignored_header_fields = image->flags != 0u || decoded[309u] ||
         decoded[310u] || decoded[311u];
   }
   if (image->obey_length > decoded_size || offset_data > decoded_size - image->obey_length)
   { arc_jfd_error(error, error_size, "obey-file boundary is invalid"); goto reject; }
   size_t content_end = decoded_size - image->obey_length;
   size_t title_length = 0u;
   while (title_length < 256u && decoded[48u + title_length]) ++title_length;
   if (title_length > 255u) title_length = 255u;
   for (size_t i = 0u; i < title_length; ++i)
   {
      uint8_t c = decoded[48u + i];
      image->title[i] = c >= 0x20u && c <= 0x7eu ? (char)c : '?';
   }
   image->title[title_length] = '\0';

   for (unsigned track = 0u; track < track_count; ++track)
   {
      ArcJfdTrack *out_track = &image->tracks[track];
      out_track->first_sector = (uint32_t)image->sector_count;
      uint32_t relative = arc_le32(decoded + offset_track + track * 4u);
      if (relative == UINT32_MAX) continue;
      if ((relative & 3u) || relative >= offset_data - offset_sector)
      { arc_jfd_error(error, error_size, "track entry points outside the sector table"); goto reject; }
      size_t cursor = (size_t)offset_sector + relative;
      int timing_mode = -1;
      uint32_t synthetic_time = 0u;
      bool terminated = false;
      for (unsigned count = 0u; count < ARC_JFD_MAX_SECTORS_PER_TRACK; ++count)
      {
         if (cursor + 4u > offset_data)
         { arc_jfd_error(error, error_size, "unterminated sector list"); goto reject; }
         uint32_t header = arc_le32(decoded + cursor);
         if (header == UINT32_MAX) { terminated = true; break; }
         if (cursor + 8u > offset_data)
         { arc_jfd_error(error, error_size, "truncated sector descriptor"); goto reject; }
         unsigned time = header >> 24u;
         unsigned block_flags = (header >> 20u) & 0x0fu;
         unsigned density = (header >> 16u) & 0x0fu;
         unsigned id = (header >> 8u) & 0xffu;
         unsigned crc_flags = (header >> 4u) & 0x0fu;
         unsigned size_code = header & 0x0fu;
         bool timed = time != 0xffu;
         if (size_code > 3u)
         { arc_jfd_error(error, error_size, "unsupported sector size code"); goto reject; }
         if (density == 4u || density == 8u)
         { arc_jfd_error(error, error_size, "quad/octal density unsupported"); goto reject; }
         if (density != 1u && density != 2u)
         { arc_jfd_error(error, error_size, "invalid sector density"); goto reject; }
         if (block_flags & 1u)
         { arc_jfd_error(error, error_size, "DiscOp 3 whole-track data unsupported"); goto reject; }
         if (time > 200u && timed)
         { arc_jfd_error(error, error_size, "sector time exceeds one revolution"); goto reject; }
         if (timing_mode < 0) timing_mode = timed ? 1 : 0;
         else if (timing_mode != (timed ? 1 : 0))
         { arc_jfd_error(error, error_size, "track-side mixes timed and untimed sectors"); goto reject; }
         if (!timed)
            for (size_t previous = out_track->first_sector;
                 previous < image->sector_count; ++previous)
               if (image->sectors[previous].id == id &&
                   image->sectors[previous].density == density)
               { arc_jfd_error(error, error_size, "untimed duplicate sector ID"); goto reject; }
         ArcJfdSector sector;
         memset(&sector, 0, sizeof(sector));
         sector.time_us = timed ? (time == 200u ? 0u : time * 1000u) : synthetic_time;
         sector.data_offset = arc_le32(decoded + cursor + 4u);
         sector.size = (uint16_t)(128u << size_code);
         sector.id = (uint8_t)id;
         sector.density = (uint8_t)density;
         sector.flags = (crc_flags & 1u ? ARC_JFD_ID_CRC_BAD : 0u) |
                        (crc_flags & 2u ? ARC_JFD_DATA_CRC_BAD : 0u) |
                        (crc_flags & 8u ? ARC_JFD_SLOW_SECTOR : 0u);
         if (sector.data_offset != UINT32_MAX &&
             (sector.data_offset > content_end - offset_data ||
              sector.size > content_end - offset_data - sector.data_offset))
         { arc_jfd_error(error, error_size, "sector payload exceeds the data table"); goto reject; }
         if (sector.data_offset != UINT32_MAX) sector.data_offset += offset_data;
         if (!timed)
         {
            uint32_t cadence = density == 2u ? 32u : 64u;
            uint32_t duration = (112u + sector.size) * cadence;
            if (synthetic_time > 200000u - duration)
            { arc_jfd_error(error, error_size, "synthesised track schedule exceeds one revolution"); goto reject; }
            synthetic_time += duration;
         }
         if (!arc_jfd_append_sector(image, &sector))
         { arc_jfd_error(error, error_size, "out of memory"); goto reject; }
         ++out_track->sector_count;
         cursor += 8u;
      }
      if (!terminated)
      { arc_jfd_error(error, error_size, "sector descriptor cap reached before terminator"); goto reject; }
      out_track->timed = timing_mode > 0;
   }
   return true;
reject:
   arc_jfd_free(image);
   return false;
}

bool arc_jfd_parse(const uint8_t *input, size_t input_size, ArcJfdImage *image,
                   char *error, size_t error_size)
{
   return arc_jfd_parse_internal(input, input_size, image, error, error_size, false);
}

bool arc_jfd_parse_owned(uint8_t *decoded, size_t decoded_size,
                         ArcJfdImage *image, char *error, size_t error_size)
{
   return arc_jfd_parse_internal(decoded, decoded_size, image, error, error_size, true);
}
