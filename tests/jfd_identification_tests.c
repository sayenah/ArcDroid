/* Captured-only FileCore proof for JFD identification IDs. GPL-2.0-only. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jfd.h"

static ArcJfdImage load(const char *root, const char *name)
{
   char path[4096], error[256];
   int n = snprintf(path, sizeof(path), "%s/jfd-identification/%s", root, name);
   assert(n > 0 && (size_t)n < sizeof(path));
   FILE *file = fopen(path, "rb");
   assert(file);
   assert(fseek(file, 0, SEEK_END) == 0);
   long length = ftell(file);
   assert(length > 0 && length <= ARC_JFD_MAX_DECODED);
   assert(fseek(file, 0, SEEK_SET) == 0);
   uint8_t *bytes = malloc((size_t)length);
   assert(bytes && fread(bytes, 1u, (size_t)length, file) == (size_t)length);
   assert(fclose(file) == 0);
   ArcJfdImage image;
   bool gzip = (size_t)length >= 2u && bytes[0] == 0x1fu &&
               bytes[1] == 0x8bu;
   bool okay = gzip ?
      arc_jfd_parse(bytes, (size_t)length, &image, error, sizeof(error)) :
      arc_jfd_parse_owned(bytes, (size_t)length, &image, error, sizeof(error));
   if (gzip) free(bytes);
   if (!okay) fprintf(stderr, "%s: %s\n", name, error);
   assert(okay);
   return image;
}

static void expect(const ArcJfdImage *image, const char *reason,
                   unsigned mask, unsigned maps, unsigned schedule)
{
   if (strcmp(image->identification_reason, reason) ||
       image->identification_mask != mask ||
       image->identification_maps != maps ||
       image->identification_schedule != schedule)
      fprintf(stderr, "expected %s/%u/%u/%u, got %s/%u/%u/%u\n",
              reason, mask, maps, schedule, image->identification_reason,
              image->identification_mask, image->identification_maps,
              image->identification_schedule);
   assert(!strcmp(image->identification_reason, reason));
   assert(image->identification_mask == mask);
   assert(image->identification_maps == maps);
   assert(image->identification_schedule == schedule);
}

int main(int argc, char **argv)
{
   assert(argc == 2);
   ArcJfdImage d = load(argv[1], "sparse-d-timed.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "presented", 0x18u, 1u, 5u);
   assert(d.identification_time[3] == 116352u &&
          d.identification_time[4] == 152704u);
   for (unsigned flags = 0u; flags < 4u; ++flags)
   {
      d.flags = flags;
      d.extended = flags != 0u;
      arc_jfd_prepare_identification(&d, true);
      expect(&d, "presented", 0x18u, 1u, 5u);
   }
   ArcJfdSector original = d.sectors[1];
   for (unsigned defect = 0u; defect < 9u; ++defect)
   {
      d.sectors[1] = original;
      switch (defect)
      {
         case 0u: d.sectors[1].id = 5u; break;
         case 1u: d.sectors[1].id = 2u; break;
         case 2u: d.sectors[1].size = 512u; break;
         case 3u: d.sectors[1].density = 4u; break;
         case 4u: d.sectors[1].flags = ARC_JFD_ID_CRC_BAD; break;
         case 5u: d.sectors[1].flags = ARC_JFD_DATA_CRC_BAD; break;
         case 6u: d.sectors[1].flags = ARC_JFD_SLOW_SECTOR; break;
         case 7u: d.sectors[1].data_offset = d.sectors[0].data_offset; break;
         default: d.sectors[1].data_offset = UINT32_MAX; break;
      }
      arc_jfd_prepare_identification(&d, true);
      expect(&d, "irregular", 0u, 1u, 5u);
   }
   d.sectors[1] = original;
   arc_jfd_prepare_identification(&d, true);
   ArcJfdOverlay *overlay = calloc(1u, sizeof(*overlay));
   assert(overlay);
   overlay->eligible = malloc(d.sector_count);
   assert(overlay->eligible);
   memset(overlay->eligible, 1, d.sector_count);
   overlay->visible = overlay->writable = true;
   d.overlay = overlay;
   assert(arc_jfd_plan_creation(&d, overlay) && overlay->pdd5_profile);
   assert(overlay->creation_time[0][3] == d.identification_time[3] &&
          overlay->creation_time[0][4] == d.identification_time[4]);
   overlay->writable = false;
   overlay->reason = "layout-unsupported";
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "presented", 0x18u, 1u, 5u);
   overlay->writable = true;
   for (unsigned id = 3u; id <= 4u; ++id)
   {
      ArcJfdView view;
      assert(arc_jfd_effective_sector(&d, 0u,
          d.tracks[0].sector_count + id, false, &view));
      assert(view.id_visible && !view.data && view.write_eligible &&
             view.created &&
             view.sector.id == id && view.sector.size == 1024u &&
             view.sector.density == 2u && !view.sector.flags);
      assert(!arc_jfd_effective_sector(&d, 1u,
          d.tracks[1].sector_count + id, false, &view));
   }
   ArcJfdView view;
   overlay->map_enabled = overlay->map_valid = true;
   overlay->allocation_mask[0] = 1u << 3u;
   assert(arc_jfd_effective_sector(&d, 0u, d.tracks[0].sector_count + 3u,
                                    false, &view) && view.data &&
          view.sector.time_us == d.identification_time[3]);
   overlay->created[0][3] = malloc(1024u);
   assert(overlay->created[0][3]);
   memset(overlay->created[0][3], 0x5au, 1024u);
   assert(arc_jfd_effective_sector(&d, 0u, d.tracks[0].sector_count + 3u,
                                    false, &view) && view.data[0] == 0x5au &&
          view.sector.time_us == d.identification_time[3]);
   overlay->visible = false;
   assert(arc_jfd_effective_sector(&d, 0u, d.tracks[0].sector_count + 3u,
                                    false, &view) && view.data &&
          view.data[0] == 0u);
   overlay->map_enabled = false;
   assert(arc_jfd_effective_sector(&d, 0u, d.tracks[0].sector_count + 3u,
                                    false, &view) && !view.data);
   overlay->visible = true;
   overlay->map_enabled = overlay->map_valid = false;
   d.sectors[1].flags = ARC_JFD_ID_CRC_BAD;
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "irregular", 0u, 1u, 5u);
   d.sectors[1].flags = 0u;
   d.sectors[1].data_offset = UINT32_MAX;
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "irregular", 0u, 1u, 5u);
   arc_jfd_free(&d);

   d = load(argv[1], "sparse-d-untimed.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "presented", 0x18u, 1u, 5u);
   assert(!d.identification_timed);
   arc_jfd_free(&d);
   d = load(argv[1], "sparse-d-unplaceable.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "unplaceable", 0u, 1u, 5u);
   arc_jfd_free(&d);
   d = load(argv[1], "sparse-d-outside.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "not-free", 0u, 1u, 5u);
   arc_jfd_free(&d);
   d = load(argv[1], "sparse-d-coincident.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "irregular", 0u, 1u, 5u);
   arc_jfd_free(&d);
   ArcJfdImage empty = {0};
   arc_jfd_prepare_identification(&empty, true);
   expect(&empty, "no-track", 0u, 0u, 0u);
   empty.track_count = 1u;
   arc_jfd_prepare_identification(&empty, true);
   expect(&empty, "no-track", 0u, 0u, 0u);
   d = load(argv[1], "complete-d.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "complete", 0u, 1u, 5u);
   arc_jfd_free(&d);
   d = load(argv[1], "legacy-l.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "unsupported-format", 0u, 0u, 0u);
   arc_jfd_free(&d);

   d = load(argv[1], "sparse-e-free.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "presented", 0x10u, 3u, 5u);
   assert(d.identification_format == 'E');
   arc_jfd_free(&d);
   d = load(argv[1], "sparse-e-allocated.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "not-free", 0u, 3u, 5u);
   arc_jfd_free(&d);
   d = load(argv[1], "sparse-e-backup.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "presented", 0x10u, 2u, 5u);
   arc_jfd_free(&d);
   d = load(argv[1], "sparse-e-conflict.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "map-conflict", 0u, 3u, 5u);
   arc_jfd_free(&d);
   d = load(argv[1], "sparse-e-bad-link.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "map-invalid", 0u, 0u, 0u);
   arc_jfd_free(&d);

   ArcJfdImage f = load(argv[1], "sparse-f-timed.jfd");
   arc_jfd_prepare_identification(&f, true);
   expect(&f, "presented", 0x6u, 3u, 10u);
   assert(f.identification_time[1] == ARC_JFD_F_SPACING_US &&
          f.identification_time[2] == ARC_JFD_F_SPACING_US * 2u);
   assert(arc_jfd_effective_sector(&f, 0u,
      f.tracks[0].sector_count + 1u, false, &view) &&
      view.id_visible && !view.data && view.sector.density == 4u);
   f.sectors[0].time_us = f.sectors[1].time_us;
   arc_jfd_prepare_identification(&f, true);
   expect(&f, "irregular", 0u, 3u, 10u);
   arc_jfd_free(&f);

   f = load(argv[1], "sparse-f-untimed.jfd");
   arc_jfd_prepare_identification(&f, true);
   expect(&f, "presented", 0x6u, 3u, 10u);
   assert(!f.identification_timed);
   arc_jfd_free(&f);

   f = load(argv[1], "sparse-f-one-zone.jfd");
   arc_jfd_prepare_identification(&f, true);
   expect(&f, "presented", 0x6u, 1u, 10u);
   arc_jfd_free(&f);

   f = load(argv[1], "sparse-f-backup.jfd");
   arc_jfd_prepare_identification(&f, true);
   expect(&f, "presented", 0x6u, 2u, 10u);
   arc_jfd_free(&f);
   f = load(argv[1], "sparse-f-mixed.jfd");
   arc_jfd_prepare_identification(&f, true);
   expect(&f, "map-invalid", 0u, 0u, 0u);
   arc_jfd_free(&f);
   f = load(argv[1], "sparse-f-allocated.jfd");
   arc_jfd_prepare_identification(&f, true);
   expect(&f, "not-free", 0u, 3u, 10u);
   arc_jfd_free(&f);
   f = load(argv[1], "sparse-f-conflict.jfd");
   arc_jfd_prepare_identification(&f, true);
   expect(&f, "map-conflict", 0u, 3u, 10u);
   arc_jfd_free(&f);
   f = load(argv[1], "sparse-f-bad-cross.jfd");
   arc_jfd_prepare_identification(&f, true);
   expect(&f, "map-invalid", 0u, 0u, 0u);
   arc_jfd_free(&f);
   f = load(argv[1], "sparse-f-past-end.jfd");
   arc_jfd_prepare_identification(&f, true);
   expect(&f, "map-invalid", 0u, 0u, 0u);
   arc_jfd_free(&f);
   d = load(argv[1], "../arcdroid-test-two-member.jfd");
   arc_jfd_prepare_identification(&d, true);
   expect(&d, "map-invalid", 0u, 0u, 0u);
   assert(d.identification_timed && d.tracks[0].sector_count == 1u &&
          d.sectors[d.tracks[0].first_sector].id == 0u);
   arc_jfd_free(&d);
   puts("jfd_identification_tests: all checks passed");
   return 0;
}
