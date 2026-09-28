/* Project-authored native JFD quarantine parser and extent checks.
 * Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "jfd.h"
#include "arcdroid_core.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *fixture(const char *root, const char *name, size_t *size)
{
   char path[4096];
   int n = snprintf(path, sizeof(path), "%s/jfd-family/%s", root, name);
   assert(n > 0 && (size_t)n < sizeof(path));
   FILE *file = fopen(path, "rb");
   assert(file && fseek(file, 0, SEEK_END) == 0);
   long length = ftell(file);
   assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
   uint8_t *bytes = malloc((size_t)length);
   assert(bytes && fread(bytes, 1u, (size_t)length, file) == (size_t)length);
   assert(fclose(file) == 0);
   *size = (size_t)length;
   return bytes;
}

static ArcJfdImage load(const char *root, const char *name, bool owned)
{
   size_t size;
   uint8_t *bytes = fixture(root, name, &size);
   ArcJfdImage image;
   char error[160];
   bool okay = owned ? arc_jfd_parse_owned(bytes, size, &image, error,
                                          sizeof(error)) :
                        arc_jfd_parse(bytes, size, &image, error,
                                      sizeof(error));
   if (!owned) free(bytes);
   if (!okay) fprintf(stderr, "%s: %s\n", name, error);
   assert(okay);
   return image;
}

static void reject(const char *root, const char *name, const char *reason)
{
   for (unsigned entry = 0u; entry < 2u; ++entry)
   {
      size_t size;
      uint8_t *bytes = fixture(root, name, &size);
      ArcJfdImage image;
      char error[160];
      bool okay = entry ? arc_jfd_parse_owned(bytes, size, &image, error,
                                               sizeof(error)) :
                          arc_jfd_parse(bytes, size, &image, error,
                                        sizeof(error));
      if (!entry) free(bytes);
      if (okay || !strstr(error, reason))
         fprintf(stderr, "reject %s entry=%u okay=%d error=%s\n",
                 name, entry, okay, okay ? "none" : error);
      assert(!okay && strstr(error, reason));
   }
}

int main(int argc, char **argv)
{
   assert(argc == 2);
   static const struct { const char *file, *reason; } reasons[] = {
      {"reject-discop3.jfd", "whole-track"},
      {"reject-density8.jfd", "octal-density"},
      {"reject-mixed-time.jfd", "mixed-timing"},
      {"reject-untimed-duplicate.jfd", "duplicate-id"},
      {"reject-long-schedule.jfd", "long-schedule"},
      {"quarantine-late.jfd", "size-code"},
      {"schedule-over.jfd", "long-schedule"},
      {"quarantine-large-alias.jfd", "size-code"}
   };
   for (size_t i = 0u; i < sizeof(reasons) / sizeof(reasons[0]); ++i)
      for (unsigned owned = 0u; owned < 2u; ++owned)
      {
         ArcJfdImage image = load(argv[1], reasons[i].file, owned != 0u);
         assert(image.tracks[0].quarantine_reason &&
                !strcmp(image.tracks[0].quarantine_reason, reasons[i].reason));
         ArcJfdView view;
         for (unsigned candidate = 0u; candidate < image.tracks[0].sector_count +
                                           ARC_JFD_MAX_STANDARD_IDS; ++candidate)
            assert(!arc_jfd_effective_sector(&image, 0u, candidate, true, &view));
         arc_jfd_free(&image);
      }
   ArcJfdImage first_reason = load(argv[1], "quarantine-first.jfd", true);
   assert(!strcmp(first_reason.tracks[0].quarantine_reason,
                  "octal-density") &&
          !first_reason.tracks[1].quarantine_reason);
   arc_jfd_free(&first_reason);
   ArcJfdImage plain = load(argv[1], "quarantine-late.jfd", false);
   ArcJfdImage gzip = load(argv[1], "quarantine-late.gz", false);
   assert(plain.decoded_size == gzip.decoded_size &&
          !memcmp(plain.decoded, gzip.decoded, plain.decoded_size) &&
          plain.sector_count == gzip.sector_count);
   for (size_t i = 0u; i < plain.sector_count; ++i)
      assert(!memcmp(&plain.sectors[i], &gzip.sectors[i], sizeof(ArcJfdSector)));
   assert(plain.tracks[0].sector_count == 2u && plain.tracks[1].sector_count == 1u &&
          plain.sectors[0].id == 1u && plain.sectors[1].size == 2048u &&
          plain.sectors[2].id == 3u && plain.sectors[2].data_offset != UINT32_MAX);
   assert(!arc_jfd_sector_write_eligible(&plain, 0u) &&
          !arc_jfd_sector_write_eligible(&plain, 1u));
   ArcJfdView view;
   assert(!arc_jfd_effective_sector(&plain, 0u, 0u, false, &view) &&
          arc_jfd_effective_sector(&plain, 1u, 0u, false, &view) &&
          view.sector.id == 3u && view.data && view.data[0] == 0x43u);
   ArcMediaInfo info;
   char error[160];
   assert(arc_media_inspect("quarantine-late.jfd", plain.decoded,
                            plain.decoded_size, &info, error, sizeof(error)) &&
          info.kind == ARC_MEDIA_JFD);
   arc_jfd_free(&plain);
   arc_jfd_free(&gzip);

   ArcJfdImage guest = load(argv[1], "quarantine-guest.jfd", true);
   assert(guest.tracks[158].sector_count == 2u &&
          !strcmp(guest.tracks[158].quarantine_reason, "octal-density"));
   size_t readable = guest.tracks[158].first_sector;
   assert(guest.sectors[readable].id == 3u &&
          guest.sectors[readable].density == 2u &&
          guest.sectors[readable].size == 1024u);
   assert(!arc_jfd_effective_sector(&guest, 158u, 0u, false, &view));
   assert(!arc_jfd_sector_write_eligible(&guest, readable));
   guest.tracks[158].quarantine_reason = NULL;
   assert(arc_jfd_effective_sector(&guest, 158u, 0u, false, &view) &&
          view.sector.id == 3u && view.data && view.data[0] == 0x5au);
   assert(arc_jfd_sector_write_eligible(&guest, readable));
   arc_jfd_free(&guest);

   ArcJfdImage below = load(argv[1], "schedule-below.jfd", true);
   assert(!below.tracks[0].quarantine_reason && below.sector_count == 4u);
   arc_jfd_free(&below);
   const size_t side159 = 159u * 5120u;
   assert(!arc_jfd_pdd5_free_intersects(159u, side159 - 1u, 1u) &&
          arc_jfd_pdd5_free_intersects(159u, side159, 1u) &&
          arc_jfd_pdd5_free_intersects(159u, side159 + 5119u, 1u) &&
          !arc_jfd_pdd5_free_intersects(159u, side159 + 5120u, 1u) &&
          !arc_jfd_pdd5_free_intersects(160u, side159, 1u));
   ArcJfdImage large = load(argv[1], "quarantine-large-alias.jfd", true);
   assert(large.sectors[0].size == (128u << 15) &&
          large.sectors[0].data_offset != UINT32_MAX &&
          !arc_jfd_sector_write_eligible(&large, 0u) &&
          !arc_jfd_sector_write_eligible(&large, 1u));
   /* The supported sector becomes eligible at the exact end of the large
    * hidden payload. The source still contains those bytes. */
   large.sectors[1].data_offset = large.sectors[0].data_offset + large.sectors[0].size;
   assert(arc_jfd_sector_write_eligible(&large, 1u));
   arc_jfd_free(&large);
   ArcJfdImage quad = load(argv[1], "quarantine-quad.jfd", true);
   assert(!arc_jfd_high_density(&quad));
   arc_jfd_free(&quad);
   quad = load(argv[1], "quarantine-quad-usable.jfd", true);
   assert(arc_jfd_high_density(&quad));
   quad.sectors[quad.tracks[1].first_sector].data_offset = UINT32_MAX;
   quad.sectors[quad.tracks[1].first_sector].flags = ARC_JFD_ID_CRC_BAD;
   assert(arc_jfd_high_density(&quad));
   arc_jfd_free(&quad);
   reject(argv[1], "reject-delta.jfd", "delta");
   reject(argv[1], "quarantine-then-invalid-density.jfd", "density");
   reject(argv[1], "quarantine-then-invalid-time.jfd", "time");
   reject(argv[1], "quarantine-then-invalid-time-254.jfd", "time");
   reject(argv[1], "quarantine-then-bad-payload.jfd", "payload");
   reject(argv[1], "quarantine-same-side-invalid.jfd", "density");
   reject(argv[1], "quarantine-large-out-of-bounds.jfd", "payload");
   puts("jfd-quarantine: parser, quarantine and extents passed");
   return 0;
}
