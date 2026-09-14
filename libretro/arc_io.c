/* ArcDroid libretro VFS bridge. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arc_io.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct retro_vfs_interface *arc_io_vfs;

void arc_io_set_vfs(struct retro_vfs_interface *vfs) { arc_io_vfs = vfs; }

bool arc_io_read_all(const char *path, void **data, size_t *size)
{
   struct retro_vfs_file_handle *file;
   int64_t length, got;
   void *buffer;
   if (!arc_io_vfs || !path || !data || !size || !arc_io_vfs->open ||
       !arc_io_vfs->size || !arc_io_vfs->read || !arc_io_vfs->close)
      return false;
   file = arc_io_vfs->open(path, RETRO_VFS_FILE_ACCESS_READ,
                           RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!file) return false;
   length = arc_io_vfs->size(file);
   if (length < 0 || (uint64_t)length > SIZE_MAX || (uint64_t)length > UINT64_C(16) * 1024u * 1024u)
   {
      arc_io_vfs->close(file);
      return false;
   }
   buffer = malloc((size_t)length ? (size_t)length : 1u);
   if (!buffer)
   {
      arc_io_vfs->close(file);
      return false;
   }
   got = arc_io_vfs->read(file, buffer, (uint64_t)length);
   arc_io_vfs->close(file);
   if (got != length)
   {
      free(buffer);
      return false;
   }
   *data = buffer;
   *size = (size_t)length;
   return true;
}

bool arc_io_write_all(const char *path, const void *data, size_t size)
{
   struct retro_vfs_file_handle *file;
   int64_t wrote;
   if (!arc_io_vfs || !path || !data || !arc_io_vfs->open ||
       !arc_io_vfs->write || !arc_io_vfs->close) return false;
   file = arc_io_vfs->open(path, RETRO_VFS_FILE_ACCESS_WRITE,
                           RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!file) return false;
   wrote = arc_io_vfs->write(file, data, size);
   if (wrote == (int64_t)size && arc_io_vfs->flush)
      wrote = arc_io_vfs->flush(file) == 0 ? wrote : -1;
   arc_io_vfs->close(file);
   return wrote == (int64_t)size;
}

static bool arc_io_is_directory(const char *path)
{
   int32_t result;
   if (!arc_io_vfs || !arc_io_vfs->stat) return false;
   result = arc_io_vfs->stat(path, NULL);
   return result >= 0 && (result & RETRO_VFS_STAT_IS_DIRECTORY) != 0;
}

bool arc_io_make_path(const char *path)
{
   char buffer[4096];
   size_t length;
   if (!arc_io_vfs || !arc_io_vfs->mkdir || !path) return false;
   length = strlen(path);
   if (!length || length >= sizeof(buffer)) return false;
   memcpy(buffer, path, length + 1u);
   for (size_t i = 1u; i < length; ++i)
      if (buffer[i] == '/' || buffer[i] == '\\')
      {
         char separator = buffer[i];
         if (i == 2u && buffer[1] == ':') continue;
         buffer[i] = '\0';
         if (buffer[0] && !arc_io_is_directory(buffer) && arc_io_vfs->mkdir(buffer) != 0)
            return false;
         buffer[i] = separator;
      }
   return arc_io_is_directory(buffer) || arc_io_vfs->mkdir(buffer) == 0;
}

static bool arc_io_walk_inner(const char *directory, unsigned depth,
                              ArcIoVisit visit, void *opaque)
{
   struct retro_vfs_dir_handle *dir;
   if (!arc_io_vfs || !arc_io_vfs->opendir || !arc_io_vfs->readdir ||
       !arc_io_vfs->dirent_get_name || !arc_io_vfs->dirent_is_dir ||
       !arc_io_vfs->closedir) return false;
   dir = arc_io_vfs->opendir(directory, false);
   if (!dir) return false;
   while (arc_io_vfs->readdir(dir))
   {
      const char *name = arc_io_vfs->dirent_get_name(dir);
      char path[4096];
      int written;
      if (!name || !name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
      written = snprintf(path, sizeof(path), "%s/%s", directory, name);
      if (written < 0 || (size_t)written >= sizeof(path))
      {
         arc_io_vfs->closedir(dir);
         return false;
      }
      if (arc_io_vfs->dirent_is_dir(dir))
      {
         if (depth && !arc_io_walk_inner(path, depth - 1u, visit, opaque))
         {
            arc_io_vfs->closedir(dir);
            return false;
         }
      }
      else if (!visit(path, opaque))
      {
         arc_io_vfs->closedir(dir);
         return false;
      }
   }
   arc_io_vfs->closedir(dir);
   return true;
}

bool arc_io_walk(const char *directory, unsigned max_depth,
                 ArcIoVisit visit, void *opaque)
{
   return directory && visit && arc_io_walk_inner(directory, max_depth, visit, opaque);
}
