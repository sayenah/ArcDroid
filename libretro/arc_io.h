/* ArcDroid libretro VFS bridge. GPL-2.0-only. */
#ifndef ARCDROID_ARC_IO_H
#define ARCDROID_ARC_IO_H

#include "libretro.h"
#include <stdbool.h>
#include <stddef.h>

typedef bool (*ArcIoVisit)(const char *path, void *opaque);
typedef bool (*ArcIoDirectoryVisit)(const char *name, bool regular_file,
                                    void *opaque);
typedef struct ArcIoFile ArcIoFile;

void arc_io_set_vfs(struct retro_vfs_interface *vfs);
bool arc_io_set_memory_source(unsigned slot, const void *data, size_t size);
void arc_io_clear_memory_sources(void);
bool arc_io_read_all(const char *path, void **data, size_t *size);
bool arc_io_read_all_limit(const char *path, void **data, size_t *size,
                           size_t limit, bool *limit_exceeded);
bool arc_io_write_all(const char *path, const void *data, size_t size);
bool arc_io_remove(const char *path);
bool arc_io_rename(const char *old_path, const char *new_path);
bool arc_io_file_size(const char *path, size_t *size);
bool arc_io_make_path(const char *path);
bool arc_io_directory_contains(const char *directory, const char *name,
                               bool *contains);
bool arc_io_list_directory(const char *directory, ArcIoDirectoryVisit visit,
                           void *opaque);
bool arc_io_walk(const char *directory, unsigned max_depth,
                 ArcIoVisit visit, void *opaque);
ArcIoFile *arc_io_open_file(const char *path, bool writable);
bool arc_io_close_file(ArcIoFile *file);
int64_t arc_io_file_length(ArcIoFile *file);
int64_t arc_io_seek_file(ArcIoFile *file, int64_t offset, int origin);
size_t arc_io_read_file(ArcIoFile *file, void *data, size_t size);
size_t arc_io_write_file(ArcIoFile *file, const void *data, size_t size);
bool arc_io_flush_file(ArcIoFile *file);

#endif
