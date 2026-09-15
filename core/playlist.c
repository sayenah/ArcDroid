/* ArcDroid playlist parser. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "playlist.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool arc_case_extension(const char *path, const char *extension)
{
   const char *dot = path ? strrchr(path, '.') : NULL;
   if (!dot) return false;
   ++dot;
   while (*dot && *extension)
      if (tolower((unsigned char)*dot++) != tolower((unsigned char)*extension++))
         return false;
   return !*dot && !*extension;
}

static void arc_playlist_error(unsigned line, unsigned *error_line,
                               char *error, size_t error_size,
                               const char *reason)
{
   if (error_line) *error_line = line;
   if (error && error_size)
   {
      if (line) snprintf(error, error_size, "playlist line %u: %s", line, reason);
      else snprintf(error, error_size, "playlist: %s", reason);
   }
}

static bool arc_utf8_valid(const uint8_t *data, size_t size)
{
   size_t i = 0u;
   while (i < size)
   {
      uint8_t a = data[i++];
      if (a < 0x80u) continue;
      unsigned count;
      uint32_t value;
      if (a >= 0xc2u && a <= 0xdfu) { count = 1u; value = a & 0x1fu; }
      else if (a >= 0xe0u && a <= 0xefu) { count = 2u; value = a & 0x0fu; }
      else if (a >= 0xf0u && a <= 0xf4u) { count = 3u; value = a & 7u; }
      else return false;
      if (i + count > size) return false;
      for (unsigned n = 0u; n < count; ++n)
      {
         uint8_t b = data[i++];
         if ((b & 0xc0u) != 0x80u) return false;
         value = (value << 6u) | (b & 0x3fu);
      }
      if ((count == 2u && value < 0x800u) ||
          (count == 3u && value < 0x10000u) || value > 0x10ffffu ||
          (value >= 0xd800u && value <= 0xdfffu)) return false;
   }
   return true;
}

static bool arc_has_forbidden_segment(const char *path, size_t length)
{
   size_t start = 0u;
   for (size_t i = 0u; i <= length; ++i)
      if (i == length || path[i] == '/' || path[i] == '\\')
      {
         size_t count = i - start;
         if ((count == 1u && path[start] == '.') ||
             (count == 2u && path[start] == '.' && path[start + 1u] == '.'))
            return true;
         start = i + 1u;
      }
   return false;
}

static bool arc_has_uri_scheme(const char *path, size_t length)
{
   if (!length || !isalpha((unsigned char)path[0])) return false;
   size_t i = 1u;
   while (i < length && (isalnum((unsigned char)path[i]) || path[i] == '+' ||
                         path[i] == '-' || path[i] == '.')) ++i;
   return i < length && path[i] == ':';
}

static bool arc_supported_media_path(const char *path)
{
   return arc_case_extension(path, "adf") || arc_case_extension(path, "adl") ||
          arc_case_extension(path, "jfd");
}

static bool arc_resolve_path(const char *playlist_path, const char *entry,
                             size_t entry_length, bool windows_host,
                             char output[ARC_MEDIA_PATH_CAPACITY],
                             const char **reason)
{
   bool saf = entry_length >= 6u && !memcmp(entry, "saf://", 6u);
   bool posix_absolute = !windows_host && entry_length && entry[0] == '/';
   bool drive_rooted = windows_host && entry_length >= 3u &&
      isalpha((unsigned char)entry[0]) && entry[1] == ':' &&
      (entry[2] == '/' || entry[2] == '\\');
   bool unc = windows_host && entry_length >= 2u && entry[0] == '\\' && entry[1] == '\\';
   if (windows_host && entry_length >= 2u && isalpha((unsigned char)entry[0]) &&
       entry[1] == ':' && !drive_rooted)
   { *reason = "drive-relative paths are unsupported"; return false; }
   if (windows_host && entry_length &&
       ((entry[0] == '\\' && !unc) || entry[0] == '/'))
   { *reason = "root-relative paths are unsupported"; return false; }
   if (arc_has_forbidden_segment(entry, entry_length))
   { *reason = "'.' and '..' path segments are unsupported"; return false; }
   if (!saf && arc_has_uri_scheme(entry, entry_length) && !drive_rooted)
   { *reason = "unsupported URI path"; return false; }

   size_t length = 0u;
   if (saf || posix_absolute || drive_rooted || unc)
   {
      if (entry_length >= ARC_MEDIA_PATH_CAPACITY)
      { *reason = "resolved path exceeds 4095 bytes"; return false; }
      memcpy(output, entry, entry_length);
      length = entry_length;
   }
   else
   {
      const char *slash = playlist_path ? strrchr(playlist_path, '/') : NULL;
      const char *backslash = playlist_path ? strrchr(playlist_path, '\\') : NULL;
      const char *separator = slash && backslash ? (slash > backslash ? slash : backslash) :
                              (slash ? slash : backslash);
      size_t directory = separator ? (size_t)(separator - playlist_path + 1) : 0u;
      if (directory + entry_length >= ARC_MEDIA_PATH_CAPACITY)
      { *reason = "resolved path exceeds 4095 bytes"; return false; }
      if (directory) memcpy(output, playlist_path, directory);
      memcpy(output + directory, entry, entry_length);
      length = directory + entry_length;
   }
   output[length] = '\0';
   if (!arc_supported_media_path(output))
   { *reason = arc_case_extension(output, "m3u") ? "nested playlists are unsupported" :
      "unsupported media extension"; return false; }
   return true;
}

bool arc_playlist_detect(const char *path, const uint8_t *data, size_t size)
{
   if (arc_case_extension(path, "m3u")) return true;
   if (!data) return false;
   size_t offset = size >= 3u && data[0] == 0xefu && data[1] == 0xbbu && data[2] == 0xbfu ? 3u : 0u;
   if (size - offset < 7u || memcmp(data + offset, "#EXTM3U", 7u)) return false;
   offset += 7u;
   while (offset < size && (data[offset] == ' ' || data[offset] == '\t')) ++offset;
   return offset == size || data[offset] == '\r' || data[offset] == '\n';
}

bool arc_playlist_parse(const char *playlist_path, const uint8_t *data,
                        size_t size, bool windows_host, ArcPlaylist *playlist,
                        unsigned *error_line, char *error, size_t error_size)
{
   if (playlist) memset(playlist, 0, sizeof(*playlist));
   if (error_line) *error_line = 0u;
   if (!playlist_path || !data || !playlist)
   { arc_playlist_error(0u, error_line, error, error_size, "invalid parser input"); return false; }
   if (size > ARC_PLAYLIST_MAX_BYTES)
   { arc_playlist_error(0u, error_line, error, error_size, "file exceeds 128 KiB"); return false; }
   if (memchr(data, '\0', size))
   { arc_playlist_error(0u, error_line, error, error_size, "NUL byte is not allowed"); return false; }
   if (!arc_utf8_valid(data, size))
   { arc_playlist_error(0u, error_line, error, error_size, "invalid UTF-8"); return false; }

   size_t offset = size >= 3u && data[0] == 0xefu && data[1] == 0xbbu && data[2] == 0xbfu ? 3u : 0u;
   unsigned line_number = 0u;
   char pending_label[ARC_MEDIA_LABEL_CAPACITY] = {0};
   bool have_label = false;
   while (offset < size)
   {
      size_t start = offset;
      while (offset < size && data[offset] != '\n' && data[offset] != '\r') ++offset;
      size_t length = offset - start;
      if (offset < size && data[offset] == '\r') ++offset;
      if (offset < size && data[offset] == '\n') ++offset;
      ++line_number;
      while (length && (data[start] == ' ' || data[start] == '\t'))
      { ++start; --length; }
      while (length && (data[start + length - 1u] == ' ' ||
                         data[start + length - 1u] == '\t')) --length;
      if (!length) continue;
      const char *line = (const char *)data + start;
      if (line[0] == '#')
      {
         static const char prefix[] = "#EXTINF:";
         if (length >= sizeof(prefix) - 1u && !memcmp(line, prefix, sizeof(prefix) - 1u))
         {
            const char *comma = memchr(line + sizeof(prefix) - 1u, ',',
                                       length - (sizeof(prefix) - 1u));
            if (!comma)
            { arc_playlist_error(line_number, error_line, error, error_size, "malformed #EXTINF"); return false; }
            size_t label_length = length - (size_t)(comma + 1 - line);
            if (label_length >= ARC_MEDIA_LABEL_CAPACITY)
            { arc_playlist_error(line_number, error_line, error, error_size, "label exceeds 127 bytes"); return false; }
            memcpy(pending_label, comma + 1, label_length);
            pending_label[label_length] = '\0';
            have_label = true;
         }
         continue;
      }
      if (playlist->count == ARC_PLAYLIST_MAX_ENTRIES)
      { arc_playlist_error(line_number, error_line, error, error_size, "playlist exceeds 32 entries"); return false; }
      ArcPlaylistEntry *entry = &playlist->entries[playlist->count];
      const char *reason = NULL;
      if (!arc_resolve_path(playlist_path, line, length, windows_host, entry->path, &reason))
      { arc_playlist_error(line_number, error_line, error, error_size, reason); return false; }
      if (have_label) memcpy(entry->label, pending_label, sizeof(entry->label));
      entry->line = line_number;
      ++playlist->count;
      have_label = false;
      pending_label[0] = '\0';
   }
   if (!playlist->count)
   { arc_playlist_error(0u, error_line, error, error_size, "playlist is empty"); return false; }
   return true;
}
