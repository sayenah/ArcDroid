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
  if (member && strlen(member) >= 256u)
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

static unsigned char ascii_fold(unsigned char value) {
  return value >= 'A' && value <= 'Z'
             ? (unsigned char)(value + ('a' - 'A'))
             : value;
}

static bool name_equal(const char *left, size_t left_size, const char *right,
                       size_t right_size) {
  if (left_size != right_size)
    return false;
  for (size_t i = 0u; i < left_size; ++i)
    if (ascii_fold((unsigned char)left[i]) !=
        ascii_fold((unsigned char)right[i]))
      return false;
  return true;
}

static const char *path_leaf(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  const char *backslash = path ? strrchr(path, '\\') : NULL;
  return slash && backslash ? (slash > backslash ? slash : backslash) + 1u
                            : slash      ? slash + 1u
                            : backslash  ? backslash + 1u
                                         : path;
}

static void leaf_stem(const char *path, const char **stem, size_t *stem_size) {
  const char *leaf = path_leaf(path);
  const char *dot = leaf ? strrchr(leaf, '.') : NULL;
  *stem = leaf;
  *stem_size = dot && dot > leaf ? (size_t)(dot - leaf) : strlen(leaf);
}

static bool disc_separator(unsigned char value) {
  return value == ' ' || value == '\t' || value == '.' || value == '_' ||
         value == '-' || value == '(' || value == ')' || value == '[' ||
         value == ']';
}

static bool disc_token(const char *stem, size_t stem_size, size_t at,
                       bool *is_one, size_t *next) {
  if (at && !disc_separator((unsigned char)stem[at - 1u]))
    return false;
  if (at + 4u > stem_size)
    return false;
  bool token = ascii_fold((unsigned char)stem[at]) == 'd' &&
               ascii_fold((unsigned char)stem[at + 1u]) == 'i' &&
               ascii_fold((unsigned char)stem[at + 2u]) == 's' &&
               (ascii_fold((unsigned char)stem[at + 3u]) == 'k' ||
                ascii_fold((unsigned char)stem[at + 3u]) == 'c');
  if (!token)
    return false;
  size_t digit = at + 4u;
  while (digit < stem_size &&
         disc_separator((unsigned char)stem[digit]))
    ++digit;
  size_t digit_end = digit;
  while (digit_end < stem_size && stem[digit_end] >= '0' &&
         stem[digit_end] <= '9')
    ++digit_end;
  if (digit_end == digit ||
      (digit_end < stem_size &&
       !disc_separator((unsigned char)stem[digit_end])))
    return false;
  size_t significant = digit;
  while (significant < digit_end && stem[significant] == '0')
    ++significant;
  *is_one = digit_end - significant == 1u && stem[significant] == '1';
  *next = digit_end;
  return true;
}

static bool explicit_disc_one(const char *name) {
  const char *stem = NULL;
  size_t stem_size = 0u;
  leaf_stem(name, &stem, &stem_size);
  bool one = false;
  bool other = false;
  for (size_t at = 0u; at < stem_size; ++at) {
    bool token_is_one = false;
    size_t next = at + 1u;
    if (!disc_token(stem, stem_size, at, &token_is_one, &next))
      continue;
    one |= token_is_one;
    other |= !token_is_one;
    at = next - 1u;
  }
  return one && !other;
}

static void archive_key(const char *archive_path, const char **key,
                        size_t *key_size) {
  const char *leaf = path_leaf(archive_path);
  size_t size = strlen(leaf);
  if (size >= 4u && leaf[size - 4u] == '.' &&
      ascii_fold((unsigned char)leaf[size - 3u]) == 'z' &&
      ascii_fold((unsigned char)leaf[size - 2u]) == 'i' &&
      ascii_fold((unsigned char)leaf[size - 1u]) == 'p')
    size -= 4u;
  const char *dot = NULL;
  for (size_t i = 0u; i < size; ++i)
    if (leaf[i] == '.')
      dot = leaf + i;
  if (dot) {
    size_t extension_size = size - (size_t)(dot - leaf);
    bool adf_or_adl = extension_size == 4u &&
                      ascii_fold((unsigned char)dot[1]) == 'a' &&
                      ascii_fold((unsigned char)dot[2]) == 'd' &&
                      (ascii_fold((unsigned char)dot[3]) == 'f' ||
                       ascii_fold((unsigned char)dot[3]) == 'l');
    bool jfd = extension_size == 4u &&
               ascii_fold((unsigned char)dot[1]) == 'j' &&
               ascii_fold((unsigned char)dot[2]) == 'f' &&
               ascii_fold((unsigned char)dot[3]) == 'd';
    if (adf_or_adl || jfd)
      size = (size_t)(dot - leaf);
  }
  *key = leaf;
  *key_size = size;
}

static bool zip_name_match(const char *member, const char *archive_path) {
  const char *member_stem = NULL;
  const char *key = NULL;
  size_t member_size = 0u;
  size_t key_size = 0u;
  leaf_stem(member, &member_stem, &member_size);
  archive_key(archive_path, &key, &key_size);
  return name_equal(member_stem, member_size, key, key_size);
}

static bool common_parent(const ArcZipArchive *archive,
                          const ArcZipContent *content, const char **parent_leaf,
                          size_t *parent_leaf_size) {
  const char *first = archive->entries[content->entries[0]].name;
  const char *slash = strrchr(first, '/');
  if (!slash || slash == first)
    return false;
  size_t size = (size_t)(slash - first);
  const char *leaf = first;
  for (const char *at = first; at < slash; ++at)
    if (*at == '/')
      leaf = at + 1u;
  for (unsigned i = 1u; i < content->count; ++i) {
    const char *name = archive->entries[content->entries[i]].name;
    const char *member_slash = strrchr(name, '/');
    if (!member_slash || (size_t)(member_slash - name) != size ||
        memcmp(name, first, size))
      return false;
  }
  *parent_leaf = leaf;
  *parent_leaf_size = (size_t)(slash - leaf);
  return true;
}

static bool folder_shape_candidate(const char *member, const char *parent_leaf,
                                   size_t parent_leaf_size) {
  const char *leaf = path_leaf(member);
  return name_equal(leaf, strlen(leaf), parent_leaf, parent_leaf_size);
}

const char *arc_zip_content_rule_name(ArcZipContentRule rule) {
  static const char *const names[] = {"disc1", "name", "folder", "order"};
  return rule <= ARC_ZIP_RULE_ORDER ? names[rule] : "order";
}

bool arc_zip_content_index(const ArcZipArchive *archive,
                           const char *archive_path,
                           const char *selected_member,
                           ArcZipContent *content, char *error,
                           size_t error_size) {
  if (content) {
    memset(content, 0, sizeof(*content));
    content->rule = ARC_ZIP_RULE_ORDER;
  }
  if (!archive || !archive_path || !content) {
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
  unsigned preferred = 0u;
  content->rule = ARC_ZIP_RULE_DISC1;
  for (unsigned i = 0u; i < content->count; ++i)
    if (explicit_disc_one(archive->entries[content->entries[i]].name)) {
      if (!content->candidate_count)
        preferred = i;
      ++content->candidate_count;
    }
  if (!content->candidate_count) {
    content->rule = ARC_ZIP_RULE_NAME;
    for (unsigned i = 0u; i < content->count; ++i)
      if (zip_name_match(archive->entries[content->entries[i]].name,
                         archive_path)) {
        if (!content->candidate_count)
          preferred = i;
        ++content->candidate_count;
      }
  }
  if (!content->candidate_count) {
    const char *parent_leaf = NULL;
    size_t parent_leaf_size = 0u;
    content->rule = ARC_ZIP_RULE_FOLDER;
    if (content->count &&
        common_parent(archive, content, &parent_leaf, &parent_leaf_size))
      for (unsigned i = 0u; i < content->count; ++i)
        if (folder_shape_candidate(
                archive->entries[content->entries[i]].name, parent_leaf,
                parent_leaf_size)) {
          if (!content->candidate_count)
            preferred = i;
          ++content->candidate_count;
        }
  }
  if (!content->candidate_count) {
    content->rule = ARC_ZIP_RULE_ORDER;
    content->candidate_count = content->count;
    preferred = 0u;
  }
  if (content->count) {
    content->preferred_index = content->entries[preferred];
    unsigned preferred_entry = content->entries[preferred];
    memmove(content->entries + 1u, content->entries,
            preferred * sizeof(content->entries[0]));
    content->entries[0] = preferred_entry;
  }
  return true;
}
