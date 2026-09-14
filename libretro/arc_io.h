/* ArcDroid libretro VFS bridge. GPL-2.0-only. */
#ifndef ARCDROID_ARC_IO_H
#define ARCDROID_ARC_IO_H

#include "libretro.h"
#include <stdbool.h>
#include <stddef.h>

typedef bool (*ArcIoVisit)(const char *path, void *opaque);

void arc_io_set_vfs(struct retro_vfs_interface *vfs);
bool arc_io_read_all(const char *path, void **data, size_t *size);
bool arc_io_write_all(const char *path, const void *data, size_t size);
bool arc_io_make_path(const char *path);
bool arc_io_walk(const char *directory, unsigned max_depth,
                 ArcIoVisit visit, void *opaque);

#endif
