/* ArcDroid libretro VFS bridge. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arc_io.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct retro_vfs_interface *arc_io_vfs;
static void (*arc_io_write_observer)(const char *path);
void arc_io_set_write_observer(void (*observer)(const char *path))
{ arc_io_write_observer = observer; }

struct ArcIoFile {
   struct retro_vfs_file_handle *handle;
   bool writable;
   char *path;
   const uint8_t *memory;
   size_t memory_size;
   size_t memory_at;
};

typedef struct { const uint8_t *data; size_t size; } ArcMemorySource;
static ArcMemorySource arc_memory[2];
static const char *const arc_memory_names[2] = {
   ":arcdroid-memory-0:", ":arcdroid-memory-1:"
};

bool arc_io_set_memory_source(unsigned slot, const void *data, size_t size)
{
   if (slot >= 2u || (!data && size)) return false;
   arc_memory[slot].data = data; arc_memory[slot].size = size; return true;
}
void arc_io_clear_memory_sources(void) { memset(arc_memory, 0, sizeof(arc_memory)); }
static int arc_io_memory_slot(const char *path)
{ for (unsigned i=0;i<2u;++i) if (path && !strcmp(path,arc_memory_names[i])) return (int)i; return -1; }

void arc_io_set_vfs(struct retro_vfs_interface *vfs) { arc_io_vfs = vfs; }

bool arc_io_read_all_limit(const char *path, void **data, size_t *size,
                           size_t limit, bool *limit_exceeded)
{
   struct retro_vfs_file_handle *file;
   int64_t length, got;
   void *buffer;
   if (limit_exceeded) *limit_exceeded = false;
   if (!path || !data || !size) return false;
   int memory_slot = arc_io_memory_slot(path);
   if (memory_slot >= 0 && arc_memory[memory_slot].data)
   {
      if (arc_memory[memory_slot].size > limit)
      {
         if (limit_exceeded) *limit_exceeded = true;
         return false;
      }
      void *copy = malloc(arc_memory[memory_slot].size ? arc_memory[memory_slot].size : 1u);
      if (!copy) return false;
      memcpy(copy, arc_memory[memory_slot].data, arc_memory[memory_slot].size);
      *data = copy; *size = arc_memory[memory_slot].size; return true;
   }
   if (!arc_io_vfs || !arc_io_vfs->open ||
       !arc_io_vfs->size || !arc_io_vfs->read || !arc_io_vfs->close)
      return false;
   file = arc_io_vfs->open(path, RETRO_VFS_FILE_ACCESS_READ,
                           RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!file) return false;
   length = arc_io_vfs->size(file);
   if (length < 0 || (uint64_t)length > SIZE_MAX || (uint64_t)length > limit)
   {
      if (limit_exceeded && length >= 0 && (uint64_t)length > limit)
         *limit_exceeded = true;
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
   int closed = arc_io_vfs->close(file);
   if (got != length || closed != 0)
   {
      free(buffer);
      return false;
   }
   *data = buffer;
   *size = (size_t)length;
   return true;
}

bool arc_io_read_all(const char *path, void **data, size_t *size)
{
   return arc_io_read_all_limit(path, data, size, 16u * 1024u * 1024u, NULL);
}

bool arc_io_write_all_diagnostic(const char *path, const void *data, size_t size,
                                 const char **failure)
{
   struct retro_vfs_file_handle *file;
   int64_t wrote;
   if (failure) *failure = "vfs-unavailable";
   if (!arc_io_vfs || !path || !data || !arc_io_vfs->open ||
       !arc_io_vfs->write || !arc_io_vfs->close) return false;
   if (failure) *failure = "vfs-open-write";
   file = arc_io_vfs->open(path, RETRO_VFS_FILE_ACCESS_WRITE,
                           RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!file) return false;
   if (failure) *failure = "write";
   wrote = arc_io_vfs->write(file, data, size);
   if (wrote > 0 && arc_io_write_observer) arc_io_write_observer(path);
   if (wrote == (int64_t)size) {
      if (failure) *failure = "flush";
      wrote = arc_io_vfs->flush && arc_io_vfs->flush(file) == 0 ? wrote : -1;
   }
   int closed = arc_io_vfs->close(file);
   if (wrote == (int64_t)size && failure) *failure = "close";
   if (wrote == (int64_t)size && closed == 0 && failure) *failure = NULL;
   return wrote == (int64_t)size && closed == 0;
}

bool arc_io_write_all(const char *path, const void *data, size_t size)
{ return arc_io_write_all_diagnostic(path, data, size, NULL); }

bool arc_io_remove(const char *path)
{
   bool removed = arc_io_vfs && arc_io_vfs->remove && path &&
      arc_io_vfs->remove(path) == 0;
   if (removed && arc_io_write_observer) arc_io_write_observer(path);
   return removed;
}

bool arc_io_rename(const char *old_path, const char *new_path)
{
   bool renamed = arc_io_vfs && arc_io_vfs->rename && old_path && new_path &&
      arc_io_vfs->rename(old_path, new_path) == 0;
   if (renamed && arc_io_write_observer)
   { arc_io_write_observer(old_path); arc_io_write_observer(new_path); }
   return renamed;
}

bool arc_io_file_size(const char *path, size_t *size)
{
   int32_t length = 0;
   int result;
   if (!arc_io_vfs || !arc_io_vfs->stat || !path || !size) return false;
   result = arc_io_vfs->stat(path, &length);
   if (!(result & RETRO_VFS_STAT_IS_VALID) ||
       (result & RETRO_VFS_STAT_IS_DIRECTORY) || length < 0) return false;
   *size = (size_t)length;
   return true;
}

static bool arc_io_is_directory(const char *path)
{
   int32_t result;
   if (!arc_io_vfs || !arc_io_vfs->stat) return false;
   result = arc_io_vfs->stat(path, NULL);
   return result >= 0 && (result & RETRO_VFS_STAT_IS_DIRECTORY) != 0;
}
bool arc_io_existing_directory(const char *path)
{ return path && arc_io_is_directory(path); }

bool arc_io_make_path_diagnostic(const char *path, char *failed_path,
                                 size_t failed_path_size, const char **failure)
{
   char buffer[4096];
   size_t length;
   if (failure) *failure = "path-validation";
   if (failed_path && failed_path_size) failed_path[0] = '\0';
   if (!arc_io_vfs || !arc_io_vfs->mkdir) {
      if (failure) *failure = "vfs-unavailable";
      return false;
   }
   if (!path) return false;
   length = strlen(path);
   if (!length || length >= sizeof(buffer)) return false;
   memcpy(buffer, path, length + 1u);
   if (failure) *failure = "mkdir";
   for (size_t i = 1u; i < length; ++i)
      if (buffer[i] == '/' || buffer[i] == '\\')
      {
         char separator = buffer[i];
         if (i == 2u && buffer[1] == ':') continue;
         buffer[i] = '\0';
         if (buffer[0] && !arc_io_is_directory(buffer) && arc_io_vfs->mkdir(buffer) != 0) {
            if (failed_path && failed_path_size)
               snprintf(failed_path, failed_path_size, "%s", buffer);
            return false;
         }
         buffer[i] = separator;
      }
   if (arc_io_is_directory(buffer) || arc_io_vfs->mkdir(buffer) == 0) {
      if (failure) *failure = NULL;
      return true;
   }
   if (failed_path && failed_path_size)
      snprintf(failed_path, failed_path_size, "%s", buffer);
   return false;
}
bool arc_io_make_path(const char *path)
{ return arc_io_make_path_diagnostic(path, NULL, 0u, NULL); }

bool arc_io_directory_contains_diagnostic(const char *directory, const char *name,
                                          bool *contains, const char **failure)
{
   if (failure) *failure = "vfs-unavailable";
   if (contains) *contains = false;
   if (!arc_io_vfs || !arc_io_vfs->opendir || !arc_io_vfs->readdir ||
       !arc_io_vfs->dirent_get_name || !arc_io_vfs->closedir ||
       !directory || !name || !name[0] || !contains ||
       strchr(name, '/') || strchr(name, '\\')) return false;
   if (failure) *failure = "opendir";
   struct retro_vfs_dir_handle *dir = arc_io_vfs->opendir(directory, false);
   if (!dir) return false;
   while (arc_io_vfs->readdir(dir))
   {
      const char *entry = arc_io_vfs->dirent_get_name(dir);
      if (entry && !strcmp(entry, name))
      {
         *contains = true;
         break;
      }
   }
   if (arc_io_vfs->closedir(dir) != 0)
   {
      if (failure) *failure = "closedir";
      *contains = false;
      return false;
   }
   if (failure) *failure = NULL;
   return true;
}
bool arc_io_directory_contains(const char *directory, const char *name,
                               bool *contains)
{ return arc_io_directory_contains_diagnostic(directory, name, contains, NULL); }

bool arc_io_list_directory_diagnostic(const char *directory,
                                      ArcIoDirectoryVisit visit, void *opaque,
                                      const char **failure)
{
   if (failure) *failure = "vfs-unavailable";
   if (!arc_io_vfs || !arc_io_vfs->opendir || !arc_io_vfs->readdir ||
       !arc_io_vfs->dirent_get_name || !arc_io_vfs->dirent_is_dir ||
       !arc_io_vfs->closedir || !directory || !visit) return false;
   if (failure) *failure = "opendir";
   struct retro_vfs_dir_handle *dir = arc_io_vfs->opendir(directory, false);
   if (!dir) return false;
   bool complete = true;
   while (arc_io_vfs->readdir(dir))
   {
      const char *name = arc_io_vfs->dirent_get_name(dir);
      if (!name || !name[0] || !strcmp(name, ".") || !strcmp(name, ".."))
         continue;
      if (!visit(name, !arc_io_vfs->dirent_is_dir(dir), opaque))
      {
         if (failure) *failure = "visit";
         complete = false;
         break;
      }
   }
   int closed = arc_io_vfs->closedir(dir);
   if (closed != 0) {
      if (failure) *failure = "closedir";
      return false;
   }
   if (complete && failure) *failure = NULL;
   return complete;
}
bool arc_io_list_directory(const char *directory, ArcIoDirectoryVisit visit,
                           void *opaque)
{ return arc_io_list_directory_diagnostic(directory, visit, opaque, NULL); }

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

ArcIoFile *arc_io_open_file(const char *path, bool writable)
{
   int memory_slot = arc_io_memory_slot(path);
   if (memory_slot >= 0)
   {
      if (writable || !arc_memory[memory_slot].data) return NULL;
      ArcIoFile *file = calloc(1u, sizeof(*file));
      if (!file) return NULL;
      file->memory = arc_memory[memory_slot].data;
      file->memory_size = arc_memory[memory_slot].size;
      return file;
   }
   if (!arc_io_vfs || !arc_io_vfs->open || !path) return NULL;
   unsigned mode = writable ?
      RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING :
      RETRO_VFS_FILE_ACCESS_READ;
   struct retro_vfs_file_handle *handle = arc_io_vfs->open(
      path, mode, RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS);
   if (!handle) return NULL;
   ArcIoFile *file = malloc(sizeof(*file));
   if (!file)
   {
      arc_io_vfs->close(handle);
      return NULL;
   }
   memset(file, 0, sizeof(*file));
   file->handle = handle;
   file->writable = writable;
   if (writable)
   {
      file->path = malloc(strlen(path) + 1u);
      if (!file->path)
      { arc_io_vfs->close(handle); free(file); return NULL; }
      strcpy(file->path, path);
   }
   return file;
}

bool arc_io_close_file(ArcIoFile *file)
{
   if (!file) return false;
   if (file->memory) { free(file); return true; }
   int result = -1;
   if (arc_io_vfs && arc_io_vfs->close && file->handle)
      result = arc_io_vfs->close(file->handle);
   free(file->path);
   free(file);
   return result == 0;
}

int64_t arc_io_file_length(ArcIoFile *file)
{
   if (file && file->memory) return (int64_t)file->memory_size;
   return file && arc_io_vfs && arc_io_vfs->size ?
      arc_io_vfs->size(file->handle) : -1;
}

int64_t arc_io_seek_file(ArcIoFile *file, int64_t offset, int origin)
{
   if (file && file->memory)
   {
      int64_t base = origin == RETRO_VFS_SEEK_POSITION_START ? 0 :
         origin == RETRO_VFS_SEEK_POSITION_CURRENT ? (int64_t)file->memory_at :
         origin == RETRO_VFS_SEEK_POSITION_END ? (int64_t)file->memory_size : -1;
      if (base < 0 || offset < -base || offset > (int64_t)file->memory_size - base) return -1;
      file->memory_at = (size_t)(base + offset); return (int64_t)file->memory_at;
   }
   return file && arc_io_vfs && arc_io_vfs->seek ?
      arc_io_vfs->seek(file->handle, offset, origin) : -1;
}

size_t arc_io_read_file(ArcIoFile *file, void *data, size_t size)
{
   if (file && file->memory && data)
   {
      size_t left=file->memory_size-file->memory_at;if(size>left)size=left;
      memcpy(data,file->memory+file->memory_at,size);file->memory_at+=size;return size;
   }
   if (!file || !data || !arc_io_vfs || !arc_io_vfs->read) return 0u;
   int64_t count = arc_io_vfs->read(file->handle, data, size);
   return count < 0 ? 0u : (size_t)count;
}

size_t arc_io_write_file(ArcIoFile *file, const void *data, size_t size)
{
   if (!file || !data || file->memory || !file->writable ||
       !arc_io_vfs || !arc_io_vfs->write) return 0u;
   int64_t count = arc_io_vfs->write(file->handle, data, size);
   if (count > 0 && arc_io_write_observer) arc_io_write_observer(file->path);
   return count < 0 ? 0u : (size_t)count;
}

bool arc_io_flush_file(ArcIoFile *file)
{
   if (!file) return false;
   if (file->memory) return true;
   if (!file->writable) return true;
   return arc_io_vfs && (!arc_io_vfs->flush ||
                         arc_io_vfs->flush(file->handle) == 0);
}

bool arc_io_truncate_file(ArcIoFile *file, int64_t length)
{
   bool truncated = file && file->writable && !file->memory && length >= 0 &&
      arc_io_vfs && arc_io_vfs->truncate &&
      arc_io_vfs->truncate(file->handle, length) == 0;
   if (truncated && arc_io_write_observer) arc_io_write_observer(file->path);
   return truncated;
}
