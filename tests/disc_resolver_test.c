/* Bounded FileCore object resolver tests over authored images. GPL-2.0-only. */
#include "arcdroid_core.h"
#include "jfd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_image(const char *directory, const char *name, size_t *size)
{
   char path[1024];
   assert(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
   FILE *file = fopen(path, "rb");
   assert(file && !fseek(file, 0, SEEK_END));
   long end = ftell(file);
   assert(end > 0 && !fseek(file, 0, SEEK_SET));
   uint8_t *data = malloc((size_t)end);
   assert(data && fread(data, 1u, (size_t)end, file) == (size_t)end);
   assert(!fclose(file));
   *size = (size_t)end;
   return data;
}

static void raw(const char *directory, const char *name)
{
   size_t size;
   uint8_t *data = read_image(directory, name, &size);
   ArcMediaInfo media;
   ArcGuestName disc = {0};
   char error[256] = {0};
   assert(arc_media_inspect(name, data, size, &media, error, sizeof(error)));
   assert(arc_disc_resolve(data, size, &media, NULL, "$", &disc) == ARC_DISC_DIR);
   assert(disc.length > 0u && disc.length <= 10u);
   assert(arc_disc_resolve(data, size, &media, NULL, "$.!Demo", NULL) == ARC_DISC_DIR);
   assert(arc_disc_resolve(data, size, &media, NULL, "$.!demo.!run", NULL) == ARC_DISC_FILE);
   assert(arc_disc_resolve(data, size, &media, NULL, "$.!Demo.Absent", NULL) == ARC_DISC_ABSENT);
   assert(arc_disc_resolve(data, size, &media, NULL, "$.Absent", NULL) == ARC_DISC_ABSENT);
   assert(arc_disc_resolve(data, size, &media, NULL, "$.!Demo.!Run.Child", NULL) == ARC_DISC_ABSENT);
   assert(arc_disc_resolve(data, size, &media, NULL, "$.12345678901", NULL) == ARC_DISC_UNKNOWN);
   assert(arc_disc_resolve(data, 512u, &media, NULL, "$.!Demo.!Run", NULL) == ARC_DISC_UNKNOWN);
   free(data);
}
static void jfd(const char *directory, const char *name)
{
   size_t size;
   uint8_t *bytes = read_image(directory, name, &size);
   ArcJfdImage image = {0};
   char error[256] = {0};
   assert(arc_jfd_parse(bytes, size, &image, error, sizeof(error)));
   assert(arc_disc_resolve(NULL, 0u, NULL, &image, "$.!Demo", NULL) == ARC_DISC_DIR);
   assert(arc_disc_resolve(NULL, 0u, NULL, &image, "$.!Demo.!Run", NULL) == ARC_DISC_FILE);
   assert(arc_disc_resolve(NULL, 0u, NULL, &image, "$.Absent", NULL) == ARC_DISC_ABSENT);
   assert(arc_disc_resolve(NULL, 0u, NULL, &image, "$.12345678901", NULL) == ARC_DISC_UNKNOWN);
   assert(arc_disc_resolve(NULL, 0u, NULL, &image, "$.\xc1" "Demo", NULL) == ARC_DISC_ABSENT);
   assert(arc_disc_resolve(NULL, 0u, NULL, &image, "$.!Demo.12345678901", NULL) == ARC_DISC_UNKNOWN);
   /* A corrupt effective directory must remain UNKNOWN, never ABSENT. */
   size_t root_sector = image.sector_count;
   for (size_t i = 0u; i < image.sector_count; ++i)
      if (image.sectors[i].size >= 1024u &&
          image.sectors[i].data_offset + 1024u <= image.decoded_size &&
          !memcmp(image.decoded + image.sectors[i].data_offset + 1u, "Hugo", 4u) &&
          !memcmp(image.decoded + image.sectors[i].data_offset + 5u, "!Demo", 5u))
      { root_sector = i; break; }
   assert(root_sector < image.sector_count);
   size_t marker_offset = image.sectors[root_sector].data_offset + 1u;
   uint8_t marker = image.decoded[marker_offset];
   image.decoded[marker_offset] = 0u;
   assert(arc_disc_resolve(NULL, 0u, NULL, &image, "$.Absent", NULL) == ARC_DISC_UNKNOWN);
   image.decoded[marker_offset] = marker;
   arc_jfd_free(&image);
   free(bytes);
}
int main(int argc, char **argv)
{
   assert(argc == 2);
   const char *directory = argv[1];
   raw(directory, "launch-family/single-l.adl");
   raw(directory, "launch-family/single-d-hugo.adf");
   size_t deep_size;
   uint8_t *deep = read_image(directory, "launch-family/single-d-hugo.adf", &deep_size);
   ArcMediaInfo deep_media;
   char deep_error[256] = {0};
   assert(arc_media_inspect("deep.adf", deep, deep_size, &deep_media,
                            deep_error, sizeof(deep_error)));
   /* Make the !Demo directory point to itself for a bounded depth probe. */
   deep[1024u + 5u + 22u] = 0u;
   deep[1024u + 5u + 23u] = 4u;
   deep[1024u + 5u + 24u] = 0u;
   char deep_path[256] = "$";
   for (unsigned i = 0u; i < 33u; ++i)
      strcat(deep_path, ".!Demo");
   assert(arc_disc_resolve(deep, deep_size, &deep_media, NULL,
                           deep_path, NULL) == ARC_DISC_UNKNOWN);
   free(deep);
   raw(directory, "launch-family/single-e-hugo.adf");
   raw(directory, "hd-family/valid-f.adf");
   size_t high_size;
   uint8_t *high = read_image(directory, "hd-family/valid-f.adf", &high_size);
   ArcMediaInfo high_media;
   char high_error[256] = {0};
   assert(arc_media_inspect("valid-f.adf", high, high_size, &high_media,
                            high_error, sizeof(high_error)));
   size_t high_root = high_size;
   for (size_t i=0u; i+2048u<=high_size; i+=256u)
      if ((!memcmp(high+i+1u,"Hugo",4u) ||
           !memcmp(high+i+1u,"Nick",4u)) &&
          !memcmp(high+i+5u,"!Demo",5u))
      { high_root=i; break; }
   assert(high_root < high_size);
   high[high_root+5u]=0xc1u;
   assert(arc_disc_resolve(high,high_size,&high_media,NULL,
                           "$.\xc1" "Demo",NULL)==ARC_DISC_DIR);
   assert(arc_disc_resolve(high,high_size,&high_media,NULL,
                           "$.\xe1" "Demo",NULL)==ARC_DISC_ABSENT);
   free(high);
   jfd(directory, "launch-family/single-d-hugo.jfd");
   jfd(directory, "hd-family/valid-f.jfd");
   size_t size;
   uint8_t *bad = read_image(directory, "hd-family/corrupt-both-f.adf", &size);
   ArcMediaInfo media;
   char error[256] = {0};
   assert(arc_media_inspect("corrupt-both-f.adf", bad, size, &media,
                            error, sizeof(error)));
   assert(arc_disc_resolve(bad, size, &media, NULL, "$.!Demo", NULL) == ARC_DISC_UNKNOWN);
   free(bad);
   bad = read_image(directory, "launch-family/duplicate-root-file-d.adf", &size);
   assert(arc_media_inspect("duplicate-root-file-d.adf", bad, size, &media,
                            error, sizeof(error)));
   assert(arc_disc_resolve(bad, size, &media, NULL, "$.!Qarvox", NULL) == ARC_DISC_UNKNOWN);
   free(bad);
   return 0;
}
