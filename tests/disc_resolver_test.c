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
   arc_jfd_free(&image);
   free(bytes);
}
int main(int argc, char **argv)
{
   assert(argc == 2);
   const char *directory = argv[1];
   raw(directory, "launch-family/single-l.adl");
   raw(directory, "launch-family/single-d-hugo.adf");
   raw(directory, "launch-family/single-e-hugo.adf");
   raw(directory, "hd-family/valid-f.adf");
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
