/* ArcDroid folder disc-set discovery. GPL-2.0-only. */
#ifndef ARCDROID_FOLDER_SET_H
#define ARCDROID_FOLDER_SET_H

#include "playlist.h"

#include <stdbool.h>
#include <stddef.h>

#define ARC_FOLDER_SET_MAX_SIBLINGS (ARC_PLAYLIST_MAX_ENTRIES - 1u)
#define ARC_FOLDER_SET_MAX_DIRECTORY 1021u

typedef enum {
   ARC_FOLDER_SET_NONE = 0,
   ARC_FOLDER_SET_READY,
   ARC_FOLDER_SET_DIRECTORY_TOO_LONG
} ArcFolderSetTrigger;

typedef enum {
   ARC_FOLDER_MEMBER_IGNORED = 0,
   ARC_FOLDER_MEMBER_ADDED,
   ARC_FOLDER_MEMBER_PATH_TOO_LONG,
   ARC_FOLDER_MEMBER_LIMIT_EXCEEDED
} ArcFolderMemberResult;

typedef struct {
   char directory[ARC_MEDIA_PATH_CAPACITY];
   char selected_name[ARC_MEDIA_PATH_CAPACITY];
   char siblings[ARC_FOLDER_SET_MAX_SIBLINGS][ARC_MEDIA_PATH_CAPACITY];
   unsigned count;
   unsigned qualified_count;
   unsigned path_omissions;
   char separator;
   bool windows_host;
   bool overflow;
} ArcFolderSet;

ArcFolderSetTrigger arc_folder_set_begin(const char *content_path,
                                         bool windows_host,
                                         ArcFolderSet *set);
ArcFolderMemberResult arc_folder_set_add(ArcFolderSet *set, const char *name,
                                         bool regular_file);
void arc_folder_set_sort(ArcFolderSet *set);
bool arc_folder_set_path(const ArcFolderSet *set, unsigned index,
                         char path[ARC_MEDIA_PATH_CAPACITY]);

#endif
