/* ArcDroid playlist parser. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#ifndef ARCDROID_PLAYLIST_H
#define ARCDROID_PLAYLIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARC_PLAYLIST_MAX_ENTRIES 32u
#define ARC_PLAYLIST_MAX_BYTES (128u * 1024u)
#define ARC_MEDIA_PATH_CAPACITY 4096u
#define ARC_MEDIA_LABEL_CAPACITY 128u

typedef struct {
   char path[ARC_MEDIA_PATH_CAPACITY];
   char label[ARC_MEDIA_LABEL_CAPACITY];
   unsigned line;
} ArcPlaylistEntry;

typedef struct {
   ArcPlaylistEntry entries[ARC_PLAYLIST_MAX_ENTRIES];
   unsigned count;
} ArcPlaylist;

bool arc_playlist_detect(const char *path, const uint8_t *data, size_t size);
bool arc_playlist_parse(const char *playlist_path, const uint8_t *data,
                        size_t size, bool windows_host, ArcPlaylist *playlist,
                        unsigned *error_line, char *error, size_t error_size);

#endif
