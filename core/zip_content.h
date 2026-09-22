/* ArcDroid ZIP content selection. GPL-2.0-only. */
#ifndef ARCDROID_ZIP_CONTENT_H
#define ARCDROID_ZIP_CONTENT_H

#include "folder_set.h"
#include "zip.h"

#include <stdbool.h>
#include <stddef.h>

#define ARC_ZIP_CONTENT_MAX_IMAGES 32u
#define ARC_ZIP_CONTENT_ARCHIVE_MAX (32u * 1024u * 1024u)
#define ARC_ZIP_CONTENT_MEMBER_MAX (8u * 1024u * 1024u)
#define ARC_ZIP_CONTENT_LOGICAL_MAX (256u * 1024u * 1024u)

typedef struct {
  unsigned entries[ARC_ZIP_CONTENT_MAX_IMAGES];
  unsigned count;
  unsigned qualifying_count;
} ArcZipContent;

bool arc_zip_content_path(const char *content_path,
                          char archive_path[ARC_MEDIA_PATH_CAPACITY],
                          char selected_member[256]);
bool arc_zip_content_index(const ArcZipArchive *archive,
                           const char *selected_member,
                           ArcZipContent *content, char *error,
                           size_t error_size);
bool arc_zip_content_member(const ArcZipEntry *entry);

#endif
