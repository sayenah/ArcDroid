/* ArcDroid ZIP content selection. Copyright 2026 ArcDroid contributors.
 * GPL-2.0-only. */
#include "zip_content.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool zip_suffix(const char *path, size_t size) {
  return size >= 4u && path[size - 4u] == '.' &&
         tolower((unsigned char)path[size - 3u]) == 'z' &&
         tolower((unsigned char)path[size - 2u]) == 'i' &&
         tolower((unsigned char)path[size - 1u]) == 'p';
}

bool arc_zip_content_path(const char *content_path,
                          char archive_path[ARC_MEDIA_PATH_CAPACITY],
                          char selected_member[256]) {
  if (archive_path)
    archive_path[0] = '\0';
  if (selected_member)
    selected_member[0] = '\0';
  if (!content_path || !archive_path || !selected_member)
    return false;
  size_t length = strlen(content_path);
  if (!length || length >= ARC_MEDIA_PATH_CAPACITY)
    return false;
  size_t archive_length = length;
  const char *member = NULL;
  for (size_t i = 4u; i < length; ++i)
    if (content_path[i] == '#' && zip_suffix(content_path, i)) {
      archive_length = i;
      member = content_path + i + 1u;
      break;
    }
  if (!zip_suffix(content_path, archive_length))
    return false;
  if (member && (!member[0] || strlen(member) >= 256u))
    return false;
  memcpy(archive_path, content_path, archive_length);
  archive_path[archive_length] = '\0';
  if (member)
    strcpy(selected_member, member);
  return true;
}

static bool hidden_component(const char *name) {
  const char *component = name;
  while (*component) {
    const char *slash = strchr(component, '/');
    size_t length = slash ? (size_t)(slash - component) : strlen(component);
    if (component[0] == '.' ||
        (length == 8u && !memcmp(component, "__MACOSX", 8u)))
      return true;
    if (!slash)
      break;
    component = slash + 1u;
  }
  return false;
}

bool arc_zip_content_member(const ArcZipEntry *entry) {
  if (!entry || !entry->name[0])
    return false;
  size_t length = strlen(entry->name);
  if (entry->name[length - 1u] == '/' || hidden_component(entry->name) ||
      !arc_media_extension(entry->name))
    return false;
  if ((entry->version_made >> 8u) == 3u) {
    unsigned mode = entry->external_attributes >> 16u;
    if (mode && (mode & 0170000u) != 0100000u)
      return false;
  }
  return true;
}

static const ArcZipArchive *sort_archive;
static int compare_indices(const void *left, const void *right) {
  unsigned li = *(const unsigned *)left;
  unsigned ri = *(const unsigned *)right;
  return arc_natural_compare(sort_archive->entries[li].name,
                             sort_archive->entries[ri].name);
}

bool arc_zip_content_index(const ArcZipArchive *archive,
                           const char *selected_member,
                           ArcZipContent *content, char *error,
                           size_t error_size) {
  if (content)
    memset(content, 0, sizeof(*content));
  if (!archive || !content) {
    if (error && error_size)
      snprintf(error, error_size, "invalid ZIP content index");
    return false;
  }
  bool selected_found = !selected_member || !selected_member[0];
  for (unsigned i = 0u; i < archive->count; ++i) {
    const ArcZipEntry *entry = &archive->entries[i];
    if (!arc_zip_content_member(entry))
      continue;
    if (selected_member && selected_member[0] &&
        !strcmp(entry->name, selected_member))
      selected_found = true;
    ++content->qualifying_count;
    if (content->qualifying_count <= ARC_ZIP_CONTENT_MAX_IMAGES)
      content->entries[content->count++] = i;
  }
  if (!selected_found) {
    if (error && error_size)
      snprintf(error, error_size, "%s is not a supported disc image",
               selected_member);
    return false;
  }
  if (content->qualifying_count > ARC_ZIP_CONTENT_MAX_IMAGES) {
    if (error && error_size)
      snprintf(error, error_size,
               "ZIP contains more than 32 supported disc images");
    return false;
  }
  sort_archive = archive;
  if (content->count > 1u)
    qsort(content->entries, content->count, sizeof(content->entries[0]),
          compare_indices);
  sort_archive = NULL;
  return true;
}
