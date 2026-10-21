/* ArcDroid libretro VFS bridge. Copyright 2026 ArcDroid contributors. GPL-2.0-only. */
#include "arc_io.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct retro_vfs_interface *arc_io_vfs;
/* Measurement changes no callback availability or return value. The frontend
 * and engine are confined to the libretro thread; scopes never nest. */
static struct retro_vfs_interface *arc_io_measured_vfs,arc_io_measure_proxy;
static ArcIoMeasurement arc_io_measurement;
static struct retro_vfs_file_handle * arc_io_measure_open(const char *p,unsigned mode,unsigned hint)
{ ++arc_io_measurement.operations;if((mode&RETRO_VFS_FILE_ACCESS_WRITE)&&!(mode&RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING))++arc_io_measurement.namespace_operations;return arc_io_measured_vfs->open(p,mode,hint); }
static int arc_io_measure_close(struct retro_vfs_file_handle *f)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->close(f); }
static int64_t arc_io_measure_size(struct retro_vfs_file_handle *f)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->size(f); }
static int64_t arc_io_measure_tell(struct retro_vfs_file_handle *f)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->tell(f); }
static int64_t arc_io_measure_seek(struct retro_vfs_file_handle *f,int64_t off,int origin)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->seek(f,off,origin); }
static int64_t arc_io_measure_read(struct retro_vfs_file_handle *f,void *p,uint64_t n)
{ ++arc_io_measurement.operations;arc_io_measurement.bytes+=n;return arc_io_measured_vfs->read(f,p,n); }
static int64_t arc_io_measure_write(struct retro_vfs_file_handle *f,const void *p,uint64_t n)
{ ++arc_io_measurement.operations;arc_io_measurement.bytes+=n;return arc_io_measured_vfs->write(f,p,n); }
static int arc_io_measure_flush(struct retro_vfs_file_handle *f)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->flush(f); }
static int arc_io_measure_remove(const char *p)
{ ++arc_io_measurement.operations;++arc_io_measurement.namespace_operations;return arc_io_measured_vfs->remove(p); }
static int arc_io_measure_rename(const char *p,const char *q)
{ ++arc_io_measurement.operations;++arc_io_measurement.namespace_operations;return arc_io_measured_vfs->rename(p,q); }
static int32_t arc_io_measure_stat(const char *p,int32_t *n)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->stat(p,n); }
static int arc_io_measure_mkdir(const char *p)
{ ++arc_io_measurement.operations;++arc_io_measurement.namespace_operations;return arc_io_measured_vfs->mkdir(p); }
static struct retro_vfs_dir_handle * arc_io_measure_opendir(const char *p,bool hidden)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->opendir(p,hidden); }
static bool arc_io_measure_readdir(struct retro_vfs_dir_handle *d)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->readdir(d); }
static const char * arc_io_measure_dirent_get_name(struct retro_vfs_dir_handle *d)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->dirent_get_name(d); }
static bool arc_io_measure_dirent_is_dir(struct retro_vfs_dir_handle *d)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->dirent_is_dir(d); }
static int arc_io_measure_closedir(struct retro_vfs_dir_handle *d)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->closedir(d); }
static int64_t arc_io_measure_truncate(struct retro_vfs_file_handle *f,int64_t n)
{ ++arc_io_measurement.operations;return arc_io_measured_vfs->truncate(f,n); }
void arc_io_measure_begin(void)
{
    memset(&arc_io_measurement,0,sizeof(arc_io_measurement));
    arc_io_measured_vfs=arc_io_vfs;
    if(!arc_io_vfs)return;
    arc_io_measure_proxy=*arc_io_vfs;
    if(arc_io_vfs->open)arc_io_measure_proxy.open=arc_io_measure_open;
    if(arc_io_vfs->close)arc_io_measure_proxy.close=arc_io_measure_close;
    if(arc_io_vfs->size)arc_io_measure_proxy.size=arc_io_measure_size;
    if(arc_io_vfs->tell)arc_io_measure_proxy.tell=arc_io_measure_tell;
    if(arc_io_vfs->seek)arc_io_measure_proxy.seek=arc_io_measure_seek;
    if(arc_io_vfs->read)arc_io_measure_proxy.read=arc_io_measure_read;
    if(arc_io_vfs->write)arc_io_measure_proxy.write=arc_io_measure_write;
    if(arc_io_vfs->flush)arc_io_measure_proxy.flush=arc_io_measure_flush;
    if(arc_io_vfs->remove)arc_io_measure_proxy.remove=arc_io_measure_remove;
    if(arc_io_vfs->rename)arc_io_measure_proxy.rename=arc_io_measure_rename;
    if(arc_io_vfs->stat)arc_io_measure_proxy.stat=arc_io_measure_stat;
    if(arc_io_vfs->mkdir)arc_io_measure_proxy.mkdir=arc_io_measure_mkdir;
    if(arc_io_vfs->opendir)arc_io_measure_proxy.opendir=arc_io_measure_opendir;
    if(arc_io_vfs->readdir)arc_io_measure_proxy.readdir=arc_io_measure_readdir;
    if(arc_io_vfs->dirent_get_name)arc_io_measure_proxy.dirent_get_name=arc_io_measure_dirent_get_name;
    if(arc_io_vfs->dirent_is_dir)arc_io_measure_proxy.dirent_is_dir=arc_io_measure_dirent_is_dir;
    if(arc_io_vfs->closedir)arc_io_measure_proxy.closedir=arc_io_measure_closedir;
    if(arc_io_vfs->truncate)arc_io_measure_proxy.truncate=arc_io_measure_truncate;
    arc_io_vfs=&arc_io_measure_proxy;
}
ArcIoMeasurement arc_io_measure_end(void)
{ arc_io_vfs=arc_io_measured_vfs;arc_io_measured_vfs=NULL;return arc_io_measurement; }

static void (*arc_io_write_observer)(const char *path);
void arc_io_set_write_observer(void (*observer)(const char *path))
{ arc_io_write_observer = observer; }

#if defined(ARCDROID_SAVE_STATES)
typedef struct ArcIoStateCache ArcIoStateCache;
static bool arc_io_state_cache_open(ArcIoFile *file);
static void arc_io_state_cache_close(ArcIoFile *file);
static void arc_io_state_cache_write(ArcIoFile *file,int64_t at,const void *data,size_t bytes);
#endif
struct ArcIoFile {
   struct retro_vfs_file_handle *handle;
   bool writable;
   bool owns_memory;
   char *path;
   const uint8_t *memory;
   size_t memory_size;
   size_t memory_at;
#if defined(ARCDROID_SAVE_STATES)
   int64_t state_at;
   ArcIoStateCache *state_cache;
   ArcIoFile *state_next;
#endif
};
#if defined(ARCDROID_RPC_LOCKSTEP)
void arc_io_lockstep_file(const ArcIoFile *file,ArcIoLockstepFile *out)
{
   _Static_assert(sizeof((ArcIoFile){NULL,false,NULL,NULL,0,0})==sizeof(ArcIoFile),
                  "classified VFS file members");
   *out=(ArcIoLockstepFile){0};
   if(file)*out=(ArcIoLockstepFile){true,file->writable,file->handle!=NULL,
      file->memory,file->memory_size,file->memory_at};
}
#endif

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

ArcIoMetadata arc_io_metadata(const char *path, size_t *size)
{
   int32_t length = -1;
   if (size) *size = 0;
   if (!path || !arc_io_vfs || !arc_io_vfs->stat) return ARC_IO_METADATA_UNKNOWN;
   int32_t flags = arc_io_vfs->stat(path, &length);
   if (flags < 0 || !(flags & RETRO_VFS_STAT_IS_VALID)) return ARC_IO_METADATA_UNKNOWN;
   if (flags & RETRO_VFS_STAT_IS_DIRECTORY) return ARC_IO_METADATA_DIRECTORY;
   if (flags & RETRO_VFS_STAT_IS_CHARACTER_SPECIAL) return ARC_IO_METADATA_OTHER;
   if (size && length < 0) return ARC_IO_METADATA_UNKNOWN;
   if (size) *size = (size_t)length;
   return ARC_IO_METADATA_FILE;
}
bool arc_io_read_available(void)
{
   return arc_io_vfs && arc_io_vfs->open && arc_io_vfs->size &&
      arc_io_vfs->read && arc_io_vfs->close;
}

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

static bool arc_io_is_directory_counted(const char *path, uint64_t *operations)
{
   int32_t result;
   if (!arc_io_vfs || !arc_io_vfs->stat) return false;
   if (operations) ++*operations;
   result = arc_io_vfs->stat(path, NULL);
   return result >= 0 && (result & RETRO_VFS_STAT_IS_DIRECTORY) != 0;
}
static bool arc_io_is_directory(const char *path)
{ return arc_io_is_directory_counted(path, NULL); }
bool arc_io_existing_directory(const char *path)
{ return path && arc_io_is_directory(path); }

ArcIoMkdirResult arc_io_mkdir(const char *path)
{
   if (!path || !*path || !arc_io_vfs || !arc_io_vfs->mkdir) return ARC_IO_MKDIR_ERROR;
   int result = arc_io_vfs->mkdir(path);
   return result == 0 ? ARC_IO_MKDIR_CREATED :
      result == -2 ? ARC_IO_MKDIR_EXISTS : ARC_IO_MKDIR_ERROR;
}

bool arc_io_make_path_step(const char *path, size_t *cursor, bool *done,
                          uint64_t *operations, uint64_t *namespace_operations)
{
   char prefix[4096]; size_t length = path ? strlen(path) : 0;
   *done = false;
   if (!length || length >= sizeof(prefix) || !arc_io_vfs || !arc_io_vfs->mkdir)
      return false;
   size_t at = *cursor ? *cursor : 1;
   while (at < length && !(path[at] == '/' || path[at] == '\\')) ++at;
   /* Same drive-prefix treatment as the synchronous bridge. */
   if (at == 2 && path[1] == ':') { *cursor = at + 1; return true; }
   memcpy(prefix, path, at); prefix[at] = 0;
   if (!arc_io_is_directory_counted(prefix, operations)) {
      ++*operations; ++*namespace_operations;
      int made=arc_io_vfs->mkdir(prefix);
      if(made!=0){
         if(made!=-2)return false;
         if(!arc_io_is_directory_counted(prefix,operations))return false;
      }
   }
   *cursor = at + 1; *done = at == length; return true;
}
struct retro_vfs_dir_handle *arc_io_open_directory(const char *path)
{
   if (!arc_io_vfs || !arc_io_vfs->opendir || !arc_io_vfs->readdir ||
       !arc_io_vfs->dirent_get_name || !arc_io_vfs->dirent_is_dir ||
       !arc_io_vfs->closedir) return NULL;
   return arc_io_vfs->opendir(path, false);
}
int arc_io_read_directory(struct retro_vfs_dir_handle *dir, const char **name,
                          bool *regular, uint64_t *operations)
{
   ++*operations;
   if (!arc_io_vfs->readdir(dir)) return 0;
   ++*operations; *name = arc_io_vfs->dirent_get_name(dir);
   if (!*name || !**name) return -1;
   ++*operations; *regular = !arc_io_vfs->dirent_is_dir(dir); return 1;
}
bool arc_io_close_directory(struct retro_vfs_dir_handle *dir)
{ return arc_io_vfs->closedir(dir) == 0; }

static bool arc_io_make_path_inner(const char *path, char *failed_path,
                                   size_t failed_path_size, const char **failure,
                                   uint64_t *operations)
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
         if (buffer[0] && !arc_io_is_directory_counted(buffer, operations) &&
             ((operations ? ++*operations : 0), arc_io_vfs->mkdir(buffer)) != 0) {
            if (failed_path && failed_path_size)
               snprintf(failed_path, failed_path_size, "%s", buffer);
            return false;
         }
         buffer[i] = separator;
      }
   if (arc_io_is_directory_counted(buffer, operations) ||
       ((operations ? ++*operations : 0), arc_io_vfs->mkdir(buffer)) == 0) {
      if (failure) *failure = NULL;
      return true;
   }
   if (failed_path && failed_path_size)
      snprintf(failed_path, failed_path_size, "%s", buffer);
   return false;
}
bool arc_io_make_path_diagnostic(const char *path, char *failed_path,
                                 size_t failed_path_size, const char **failure)
{ return arc_io_make_path_inner(path, failed_path, failed_path_size, failure, NULL); }
bool arc_io_make_path_counted(const char *path, uint64_t *operations)
{ return arc_io_make_path_inner(path, NULL, 0u, NULL, operations); }
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

static bool arc_io_list_directory_inner(const char *directory,
                                      ArcIoDirectoryVisit visit, void *opaque,
                                      const char **failure, uint64_t *operations)
{
   if (operations) *operations = 0;
   if (failure) *failure = "vfs-unavailable";
   if (!arc_io_vfs || !arc_io_vfs->opendir || !arc_io_vfs->readdir ||
       !arc_io_vfs->dirent_get_name || !arc_io_vfs->dirent_is_dir ||
       !arc_io_vfs->closedir || !directory || !visit) return false;
   if (failure) *failure = "opendir";
   if (operations) ++*operations;
   struct retro_vfs_dir_handle *dir = arc_io_vfs->opendir(directory, false);
   if (!dir) return false;
   bool complete = true;
   for (;;)
   {
      if (operations) ++*operations;
      if (!arc_io_vfs->readdir(dir)) break;
      if (operations) ++*operations;
      const char *name = arc_io_vfs->dirent_get_name(dir);
      if (!name || !name[0] || !strcmp(name, ".") || !strcmp(name, ".."))
         continue;
      if (operations) ++*operations;
      if (!visit(name, !arc_io_vfs->dirent_is_dir(dir), opaque))
      {
         if (failure) *failure = "visit";
         complete = false;
         break;
      }
   }
   if (operations) ++*operations;
   int closed = arc_io_vfs->closedir(dir);
   if (closed != 0) {
      if (failure) *failure = "closedir";
      return false;
   }
   if (complete && failure) *failure = NULL;
   return complete;
}
bool arc_io_list_directory_diagnostic(const char *directory,
                                      ArcIoDirectoryVisit visit, void *opaque,
                                      const char **failure)
{ return arc_io_list_directory_inner(directory, visit, opaque, failure, NULL); }
bool arc_io_list_directory_counted(const char *directory, ArcIoDirectoryVisit visit,
                                   void *opaque, uint64_t *operations)
{ return arc_io_list_directory_inner(directory, visit, opaque, NULL, operations); }
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

static ArcIoFile *arc_io_file_open(const char *path, bool writable, bool create,
                                  unsigned hints, uint64_t *operations)
{
   int memory_slot = arc_io_memory_slot(path);
   if (memory_slot >= 0)
   {
      if (writable || !arc_memory[memory_slot].data) return NULL;
      ArcIoFile *file = calloc(1u, sizeof(*file));
      if (!file) return NULL;
      file->path=malloc(strlen(path)+1u);
      if(!file->path){free(file);return NULL;}
      strcpy(file->path,path);
      file->memory = arc_memory[memory_slot].data;
      file->memory_size = arc_memory[memory_slot].size;
      return file;
   }
   if (!arc_io_vfs || !arc_io_vfs->open || !path) return NULL;
   unsigned mode = create ? RETRO_VFS_FILE_ACCESS_WRITE : writable ?
      RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING :
      RETRO_VFS_FILE_ACCESS_READ;
   struct retro_vfs_file_handle *handle = arc_io_vfs->open(
      path, mode, hints);
   if (!handle) return NULL;
   ArcIoFile *file = malloc(sizeof(*file));
   if (!file)
   {
      if (operations) ++*operations;
      arc_io_vfs->close(handle);
      return NULL;
   }
   memset(file, 0, sizeof(*file));
   file->handle = handle;
   file->writable = writable;
   {
      file->path = malloc(strlen(path) + 1u);
      if (!file->path)
      { if (operations) ++*operations; arc_io_vfs->close(handle); free(file); return NULL; }
      strcpy(file->path, path);
   }
   return file;
}

bool arc_io_flush_available(void) { return arc_io_vfs && arc_io_vfs->flush; }
ArcIoFile *arc_io_open_file(const char *path, bool writable)
{
   ArcIoFile *file=arc_io_file_open(path,writable,false,RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS,NULL);
#if defined(ARCDROID_SAVE_STATES)
   if(file&&!arc_io_state_cache_open(file)){arc_io_close_file(file);return NULL;}
#endif
   return file;
}
ArcIoFile *arc_io_create_file(const char *path)
{ return arc_io_file_open(path, true, true, RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS, NULL); }
ArcIoFile *arc_io_create_file_counted(const char *path, uint64_t *operations)
{ return arc_io_file_open(path, true, true, RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS, operations); }

bool arc_io_close_file(ArcIoFile *file)
{
   if (!file) return false;
#if defined(ARCDROID_SAVE_STATES)
   arc_io_state_cache_close(file);
#endif
   if (file->memory && !file->handle) { if(file->owns_memory)free((void*)file->memory);free(file->path);free(file); return true; }
   int result = -1;
   if (arc_io_vfs && arc_io_vfs->close && file->handle)
      result = arc_io_vfs->close(file->handle);
   if(file->owns_memory)free((void*)file->memory);
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

int64_t arc_io_tell_file(ArcIoFile *file)
{
   if (file && file->memory) return (int64_t)file->memory_at;
   return file && arc_io_vfs && arc_io_vfs->tell ?
      arc_io_vfs->tell(file->handle) : -1;
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
   if(!file||!arc_io_vfs||!arc_io_vfs->seek)return -1;
   int64_t result=arc_io_vfs->seek(file->handle,offset,origin);
#if defined(ARCDROID_SAVE_STATES)
   if(result>=0){
      int64_t base=origin==RETRO_VFS_SEEK_POSITION_START?0:
         origin==RETRO_VFS_SEEK_POSITION_CURRENT?file->state_at:arc_io_file_length(file);
      file->state_at=base+offset;
   }
#endif
   return result;
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
#if defined(ARCDROID_SAVE_STATES)
   if(count>0)file->state_at+=count;
#endif
   return count < 0 ? 0u : (size_t)count;
}

size_t arc_io_write_file(ArcIoFile *file, const void *data, size_t size)
{
   if (!file || !data || (file->memory && !file->handle) || !file->writable ||
       !arc_io_vfs || !arc_io_vfs->write) return 0u;
   if(file->memory) {
      if(size>file->memory_size-file->memory_at)return 0u;
      if(!arc_io_vfs->seek||arc_io_vfs->seek(file->handle,(int64_t)file->memory_at,RETRO_VFS_SEEK_POSITION_START)!=(int64_t)file->memory_at)return 0u;
   }
#if defined(ARCDROID_SAVE_STATES)
   int64_t state_at=file->memory?(int64_t)file->memory_at:file->state_at;
#endif
   int64_t count = arc_io_vfs->write(file->handle, data, size);
#if defined(ARCDROID_SAVE_STATES)
   if(count>0){arc_io_state_cache_write(file,state_at,data,(size_t)count);file->state_at=state_at+count;}
#endif
   if(count>0&&file->memory) {memcpy((uint8_t*)file->memory+file->memory_at,data,(size_t)count);file->memory_at+=(size_t)count;}
   if (count > 0 && arc_io_write_observer) arc_io_write_observer(file->path);
   return count < 0 ? 0u : (size_t)count;
}

bool arc_io_flush_file(ArcIoFile *file)
{
   if (!file) return false;
   if (file->memory && !file->handle) return true;
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

/* Explicit bounded streaming access; do not request frontend whole-file maps. */
ArcIoFile *arc_io_open_stream(const char *path, bool writable)
{ return arc_io_file_open(path, writable, false, RETRO_VFS_FILE_ACCESS_HINT_NONE, NULL); }
ArcIoFile *arc_io_open_stream_counted(const char *path, bool writable, uint64_t *operations)
{ return arc_io_file_open(path, writable, false, RETRO_VFS_FILE_ACCESS_HINT_NONE, operations); }

#if defined(ARCDROID_SAVE_STATES)
#include "arc_state.h"
#include "arc_state_io.h"
#define ARC_STATE_FILE_BINDINGS 68u
static struct { char path[4096]; bool writable; } arc_io_state_paths[ARC_STATE_FILE_BINDINGS];
typedef struct {char path[4096];void *data;size_t bytes;ArcDigest digest;} ArcIoStateBase;
static ArcIoStateBase arc_io_state_bases[ARC_STATE_FILE_BINDINGS];
void arc_io_state_clear(void)
{
   for(unsigned i=0;i<ARC_STATE_FILE_BINDINGS;++i)free(arc_io_state_bases[i].data);
   memset(arc_io_state_bases,0,sizeof(arc_io_state_bases));
   memset(arc_io_state_paths,0,sizeof(arc_io_state_paths));
}
void arc_io_state_base_path(unsigned index,const char *path)
{
   if(index>=ARC_STATE_FILE_BINDINGS||!path)return;
   ArcIoStateBase *b=&arc_io_state_bases[index];if(!strcmp(b->path,path))return;
   free(b->data);memset(b,0,sizeof(*b));snprintf(b->path,sizeof(b->path),"%s",path);
}
const void *arc_io_state_base(unsigned index,size_t *bytes,ArcDigest *digest)
{
   if(index>=ARC_STATE_FILE_BINDINGS)return NULL;
   ArcIoStateBase *b=&arc_io_state_bases[index];
   if(!b->data) {
      int slot=arc_io_memory_slot(arc_io_state_paths[index].path);
      if(slot>=0) {
         b->bytes=arc_memory[slot].size;b->data=malloc(b->bytes?b->bytes:1u);
         if(b->data)memcpy(b->data,arc_memory[slot].data,b->bytes);
      } else if(!b->path[0]||!arc_io_read_all(b->path,&b->data,&b->bytes))return NULL;
      if(!b->data)return NULL;
      b->digest=arc_digest(b->data,b->bytes);
   }
   if(bytes)*bytes=b->bytes;if(digest)*digest=b->digest;return b->data;
}
void arc_io_state_binding(unsigned index,const char *path,bool writable)
{
   if(index>=ARC_STATE_FILE_BINDINGS)return;
   snprintf(arc_io_state_paths[index].path,sizeof(arc_io_state_paths[index].path),"%s",path?path:"");
   arc_io_state_paths[index].writable=writable;
   arc_state_io_binding(index,path);
}
#include "arc_io_state_cache.inc"
/* Reset-time host paths are rebound through the current prepared media table. */
typedef struct { char *destination; size_t capacity; unsigned binding; } ArcIoStatePath;
static void arc_io_state_path_commit(void *opaque)
{
   ArcIoStatePath *p=opaque;
   snprintf(p->destination,p->capacity,"%s",p->binding<ARC_STATE_FILE_BINDINGS?arc_io_state_paths[p->binding].path:"");
   free(p);
}
void arc_io_state_path(ArcState *s,const char *name,char *path,size_t capacity)
{
   if(arc_state_pass(s)||arc_state_error(s)||arc_state_registering(s))return;
   uint64_t binding=UINT64_MAX;
   if(!arc_state_loading(s)&&path[0]) {
      for(unsigned i=0;i<ARC_STATE_FILE_BINDINGS;++i)
         if(!strcmp(path,arc_io_state_paths[i].path)){binding=i;break;}
      if(binding==UINT64_MAX){arc_state_fail(s,"unbound reset media");return;}
   }
   size_t bytes=0;const void *wire=arc_state_blob(s,name,&binding,sizeof(binding),sizeof(binding),&bytes);
   if(!wire||bytes!=sizeof(binding)){arc_state_fail(s,"reset media binding");return;}
   memcpy(&binding,wire,sizeof(binding));
   if(binding!=UINT64_MAX&&(binding>=ARC_STATE_FILE_BINDINGS||!arc_io_state_paths[binding].path[0]||strlen(arc_io_state_paths[binding].path)>=capacity)) {
      arc_state_fail(s,"reset media path");return;
   }
   if(arc_state_loading(s)) {
      ArcIoStatePath *change=malloc(sizeof(*change));if(!change){arc_state_fail(s,"allocation");return;}
      *change=(ArcIoStatePath){path,capacity,binding==UINT64_MAX?ARC_STATE_FILE_BINDINGS:(unsigned)binding};
      arc_state_action(s,arc_io_state_path_commit,arc_state_free,change);
   }
}
typedef struct { ArcIoFile *previous,*next; } ArcIoStateFileChange;
static void arc_io_state_file_commit(void *opaque)
{ ArcIoStateFileChange *c=opaque;if(c->previous)arc_io_close_file(c->previous);free(c); }
static void arc_io_state_file_rollback(void *opaque)
{ ArcIoStateFileChange *c=opaque;if(c->next)arc_io_close_file(c->next);free(c); }
void arc_io_state_file(ArcState *s,const char *name,ArcIoFile *file)
{
   if(arc_state_error(s))return;
   if(arc_state_pass(s)) {
      uint64_t metadata[5]={0};char key[192];size_t n;
      snprintf(key,sizeof(key),"%s.metadata",name);
      const void *wire=arc_state_blob(s,key,NULL,0,sizeof(metadata),&n);
      if(!wire||n!=sizeof(metadata)){arc_state_fail(s,"media metadata");return;}
      memcpy(metadata,wire,sizeof(metadata));ArcDigest digest={0};
      if(metadata[0]&&!arc_io_state_base((unsigned)metadata[1],NULL,&digest)){arc_state_fail(s,"media base");return;}
      snprintf(key,sizeof(key),"%s.base",name);arc_state_expect(s,key,digest.sha1,sizeof(digest.sha1));return;
   }
   uint64_t meta[5]={0};char key[192];size_t bytes=0;
   void *temporary=NULL;const void *contents=NULL;
   if(!arc_state_loading(s)&&file) {
      meta[0]=1;meta[1]=UINT64_MAX;meta[2]=file->writable;meta[3]=(uint64_t)arc_io_tell_file(file);
      for(unsigned i=0;i<ARC_STATE_FILE_BINDINGS;++i) {
         bool same=file->path&&arc_io_state_paths[i].path[0]&&!strcmp(file->path,arc_io_state_paths[i].path);
         int memory=arc_io_memory_slot(arc_io_state_paths[i].path);
         if(!file->handle&&memory>=0&&arc_memory[memory].data==file->memory)same=true;
         if(same){meta[1]=i;break;}
      }
      if(meta[1]==UINT64_MAX){arc_state_fail(s,"unbound media file");return;}
      if(file->state_cache){
         if(!arc_io_state_cache_refresh(file)){arc_state_fail(s,"dirty media read");return;}
         contents=file->state_cache->data;bytes=file->state_cache->bytes;
      }
      else if(file->memory){contents=file->memory;bytes=file->memory_size;}
      else if(!arc_io_read_all(file->path,&temporary,&bytes)){arc_state_fail(s,"media read");return;}
      else contents=temporary;
      meta[4]=bytes;
   }
   snprintf(key,sizeof(key),"%s.metadata",name);size_t n=0;
   const uint64_t *saved=arc_state_blob(s,key,meta,sizeof(meta),sizeof(meta),&n);
   if(!saved||n!=sizeof(meta)){free(temporary);arc_state_fail(s,"media metadata");return;}
   memcpy(meta,saved,sizeof(meta));
   if(meta[0]>1||(!meta[0]&&(meta[1]||meta[2]||meta[3]||meta[4]))||
      (meta[0]&&(meta[1]>=ARC_STATE_FILE_BINDINGS||meta[2]>1||meta[3]>meta[4]||meta[4]>16u*1024u*1024u||
       !arc_io_state_paths[meta[1]].path[0]||(meta[2]&&!arc_io_state_paths[meta[1]].writable)))) {
      free(temporary);arc_state_fail(s,"media binding");return;
   }
   bytes=(size_t)meta[4];size_t base_bytes=0;ArcDigest base_digest={0};
   const void *base=meta[0]?arc_io_state_base((unsigned)meta[1],&base_bytes,&base_digest):NULL;
   if(meta[0]&&(!base||base_bytes!=bytes)){free(temporary);arc_state_fail(s,"immutable media base");return;}
   snprintf(key,sizeof(key),"%s.base",name);arc_state_expect(s,key,base_digest.sha1,sizeof(base_digest.sha1));
   snprintf(key,sizeof(key),"%s.contents",name);
   const void *data;
   if(!arc_state_loading(s)&&file&&file->state_cache) {
      arc_io_state_cache_capture(s,key,file->state_cache);data=contents;
   } else data=arc_state_delta(s,key,contents,base,bytes);
   if(!arc_state_loading(s)&&temporary) {
      void *retained=arc_state_scratch(s,bytes);if(retained)memcpy(retained,contents,bytes);contents=retained;
   }
   snprintf(key,sizeof(key),"%s.view",name);arc_state_view(s,key,arc_state_loading(s)?data:contents,bytes,NULL);
   free(temporary);
   if(arc_state_error(s))return;
   ArcIoFile *next=file;
   if(arc_state_loading(s)) {
      next=NULL;
      if(meta[0]) {
         /* Logical medium, cache indexes and reopened handle are ownership,
          * independently bounded from the small delta on the wire. */
         size_t ownership=2u*bytes+((bytes+511u)/512u)*8u+sizeof(ArcIoFile)+sizeof(ArcIoStateCache)+4096u;
         if(!arc_state_ownership_resize(s,0,ownership))return;
         next=arc_io_file_open(arc_io_state_paths[meta[1]].path,meta[2]!=0,false,RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS,NULL);
         if(!next){arc_state_fail(s,"media reopen");return;}
         uint8_t *view=malloc(bytes?bytes:1u);
         if(!view){arc_io_close_file(next);arc_state_fail(s,"allocation");return;}
#if defined(ARCDROID_TEST_FAULTS)
         const char *drop=getenv("ARCDROID_SS1_DROP_OBJECT");
         if(drop&&!strcmp(drop,"media.raw-image")&&next->handle) {
            free(view);
            if(arc_io_seek_file(next,(int64_t)meta[3],RETRO_VFS_SEEK_POSITION_START)<0){arc_io_close_file(next);arc_state_fail(s,"media seek");return;}
         } else
#endif
         {memcpy(view,data,bytes);next->memory=view;next->owns_memory=true;next->memory_size=bytes;next->memory_at=(size_t)meta[3];if(!arc_io_state_cache_open(next)){arc_io_close_file(next);arc_state_fail(s,"media cache allocation");return;}}
      }
      ArcIoStateFileChange *change=malloc(sizeof(*change));
      if(!change){if(next)arc_io_close_file(next);arc_state_fail(s,"allocation");return;}
      change->previous=file;change->next=next;
      arc_state_action(s,arc_io_state_file_commit,arc_io_state_file_rollback,change);
   }
   arc_state_region(s,name,next,next?sizeof(*next):0);
   if(next)arc_state_target(s,next,ARC_STATE_REFERENCE_FILE);
}
#endif
