/* Deterministic tests for ZIP content indexing and extraction. GPL-2.0-only. */
#include "zip.h"
#include "zip_content.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_fixture(const char *root, const char *name, size_t *size) {
  char path[4096];
  int written = snprintf(path, sizeof(path), "%s/zip-content/%s", root, name);
  assert(written >= 0 && (size_t)written < sizeof(path));
  FILE *file = fopen(path, "rb");
  assert(file && fseek(file, 0, SEEK_END) == 0);
  long length = ftell(file);
  assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
  uint8_t *data = malloc(length ? (size_t)length : 1u);
  assert(data && fread(data, 1u, (size_t)length, file) == (size_t)length);
  assert(fclose(file) == 0);
  *size = (size_t)length;
  return data;
}

static ArcZipArchive index_fixture(const char *root, const char *name,
                                   uint8_t **bytes, size_t *size) {
  *bytes = read_fixture(root, name, size);
  ArcZipArchive archive;
  const ArcZipLimits limits = {ARC_ZIP_CONTENT_ARCHIVE_MAX, 1024u,
                               ARC_ZIP_CONTENT_MEMBER_MAX};
  char error[256] = {0};
  if (!arc_zip_index(*bytes, *size, &limits, &archive, error, sizeof(error))) {
    fprintf(stderr, "%s unexpectedly rejected: %s\n", name, error);
    abort();
  }
  return archive;
}

static void accept(const char *root, const char *name) {
  uint8_t *bytes;
  size_t size;
  ArcZipArchive archive = index_fixture(root, name, &bytes, &size);
  arc_zip_free(&archive);
  free(bytes);
}

static void reject(const char *root, const char *name, const char *fragment) {
  size_t size;
  uint8_t *bytes = read_fixture(root, name, &size);
  ArcZipArchive archive;
  const ArcZipLimits limits = {ARC_ZIP_CONTENT_ARCHIVE_MAX, 1024u,
                               ARC_ZIP_CONTENT_MEMBER_MAX};
  char error[256] = {0};
  assert(!arc_zip_index(bytes, size, &limits, &archive, error, sizeof(error)));
  if (!strstr(error, fragment)) {
    fprintf(stderr, "%s: expected '%s', got '%s'\n", name, fragment, error);
    abort();
  }
  free(bytes);
}

static void selection(const char *root) {
  uint8_t *bytes;
  size_t size;
  ArcZipArchive archive = index_fixture(root, "order.zip", &bytes, &size);
  ArcZipContent content;
  char error[256] = {0};
  assert(arc_zip_content_index(&archive, NULL, &content, error, sizeof(error)) &&
         content.count == 5u);
  static const char *const expected[] = {
      "a/Disc0.adf", "Disc1.adf", "Disc2.adf", "disc3.adf", "Disc10.adf"};
  for (unsigned i = 0u; i < content.count; ++i)
    assert(!strcmp(archive.entries[content.entries[i]].name, expected[i]));
  assert(arc_zip_content_index(&archive, "Disc10.adf", &content, error,
                               sizeof(error)) && content.count == 5u);
  assert(!arc_zip_content_index(&archive, "readme.txt", &content, error,
                                sizeof(error)) &&
         strstr(error, "not a supported disc image"));
  arc_zip_free(&archive);
  free(bytes);

  ArcZipArchive reversed = index_fixture(root, "order-reversed.zip", &bytes,
                                         &size);
  assert(arc_zip_content_index(&reversed, NULL, &content, error,
                               sizeof(error)) && content.count == 5u);
  for (unsigned i = 0u; i < content.count; ++i)
    assert(!strcmp(reversed.entries[content.entries[i]].name, expected[i]));
  arc_zip_free(&reversed);
  free(bytes);
}

static void member_failures(const char *root) {
  const char *fixtures[] = {"bad-crc.zip", "oversize-media.zip",
                            "unsupported-media.zip", "inflated-lie.zip"};
  for (unsigned fixture = 0u;
       fixture < sizeof(fixtures) / sizeof(fixtures[0]); ++fixture) {
    uint8_t *bytes;
    size_t size;
    ArcZipArchive archive = index_fixture(root, fixtures[fixture], &bytes, &size);
    ArcZipContent content;
    char error[256] = {0};
    assert(arc_zip_content_index(&archive, NULL, &content, error,
                                 sizeof(error)) && content.count >= 1u);
    unsigned failed = 0u, succeeded = 0u;
    for (unsigned i = 0u; i < content.count; ++i) {
      uint8_t *member = NULL;
      size_t member_size = 0u;
      if (arc_zip_extract(bytes, size,
                          &archive.entries[content.entries[i]], &member,
                          &member_size, error, sizeof(error)))
        ++succeeded;
      else
        ++failed;
      free(member);
    }
    assert(failed >= 1u && succeeded >= 1u);
    arc_zip_free(&archive);
    free(bytes);
  }
}

static void allocation_failure(const char *root) {
  uint8_t *bytes;
  size_t size;
  ArcZipArchive archive = index_fixture(root, "single.zip", &bytes, &size);
  uint8_t *member = NULL;
  size_t member_size = 0u;
  char error[80] = {0};
  arc_zip_test_fail_member_allocations(1u);
  assert(!arc_zip_extract(bytes, size, &archive.entries[0], &member,
                          &member_size, error, sizeof(error)) &&
         strstr(error, "out of memory"));
  arc_zip_free(&archive);
  free(bytes);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const char *accepted[] = {
      "stored.zip", "deflated.zip", "descriptor.zip", "infozip-extra.zip",
      "ditto-extra.zip", "utf8.zip", "nested.zip", "mixed.zip",
      "unsupported-nonmedia.zip", "oversize-nonmedia.zip",
      "false-eocd-comment.zip"};
  for (unsigned i = 0u; i < sizeof(accepted) / sizeof(accepted[0]); ++i)
    accept(argv[1], accepted[i]);
  const struct { const char *name; const char *reason; } rejected[] = {
      {"duplicate.zip", "duplicate"}, {"truncated.zip", "central"},
      {"entry-limit.zip", "more than 1024"},
      {"encrypted.zip", "encrypted"}, {"zip64.zip", "zip64"},
      {"zip64-extra.zip", "zip64"}, {"descriptor-mismatch.zip", "descriptor"},
      {"local-mismatch.zip", "local/central"},
      {"central-size-mismatch.zip", "central"}, {"unsafe.zip", "unsafe"},
      {"absolute.zip", "unsafe"}, {"backslash.zip", "unsafe"},
      {"dot-component.zip", "unsafe"}, {"nul-name.zip", "unsafe"},
      {"malformed-extra.zip", "malformed"},
      {"conflicting-arc0.zip", "ARC0"}, {"overlap.zip", "overlapping"},
      {"central-overlap.zip", "central directory"},
      {"entry-disk.zip", "multi-disk"}};
  for (unsigned i = 0u; i < sizeof(rejected) / sizeof(rejected[0]); ++i)
    reject(argv[1], rejected[i].name, rejected[i].reason);
  selection(argv[1]);
  member_failures(argv[1]);
  allocation_failure(argv[1]);
  char archive[ARC_MEDIA_PATH_CAPACITY], member[256];
  assert(arc_zip_content_path("C:\\Games\\Set.ZIP#Disc2.adf", archive, member) &&
         !strcmp(archive, "C:\\Games\\Set.ZIP") && !strcmp(member, "Disc2.adf"));
  assert(arc_zip_content_path("C:\\My Games\\Set One.zip#Dir/Disc 1.adf",
                              archive, member) &&
         !strcmp(archive, "C:\\My Games\\Set One.zip") &&
         !strcmp(member, "Dir/Disc 1.adf"));
  assert(arc_zip_content_path("C:\\Games\\Set.zip#", archive, member) &&
         !strcmp(archive, "C:\\Games\\Set.zip") && !member[0]);
  puts("zip-content: shared reader, selection, hostile mutants and allocation fault passed");
  return 0;
}
