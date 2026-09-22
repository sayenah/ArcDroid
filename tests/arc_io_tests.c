/* ArcDroid VFS write-boundary tests. GPL-2.0-only. */
#include "arc_io.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct retro_vfs_file_handle {
  unsigned mode;
};

static struct retro_vfs_file_handle handle;
static unsigned writes;
static unsigned reads;

static struct retro_vfs_file_handle *test_open(const char *path, unsigned mode,
                                                unsigned hints) {
  (void)hints;
  if (!path || strcmp(path, "test-file"))
    return NULL;
  handle.mode = mode;
  return &handle;
}

static int test_close(struct retro_vfs_file_handle *file) {
  return file == &handle ? 0 : -1;
}

static int64_t test_size(struct retro_vfs_file_handle *file) {
  assert(file == &handle);
  return 3;
}

static int64_t test_read(struct retro_vfs_file_handle *file,
                         void *data, uint64_t size) {
  static const uint8_t contents[] = {4u, 5u, 6u};
  assert(file == &handle && data && size == sizeof(contents));
  memcpy(data, contents, sizeof(contents));
  ++reads;
  return (int64_t)sizeof(contents);
}

static int64_t test_write(struct retro_vfs_file_handle *file,
                          const void *data, uint64_t size) {
  assert(file == &handle && data);
  ++writes;
  return (int64_t)size;
}

int main(void) {
  static const uint8_t memory[] = {1u, 2u, 3u};
  struct retro_vfs_interface vfs;
  memset(&vfs, 0, sizeof(vfs));
  vfs.open = test_open;
  vfs.close = test_close;
  vfs.size = test_size;
  vfs.read = test_read;
  vfs.write = test_write;
  arc_io_set_vfs(&vfs);

  assert(arc_io_set_memory_source(0u, memory, sizeof(memory)));
  void *all = NULL;
  size_t all_size = 0u;
  bool too_large = false;
  assert(!arc_io_read_all_limit(":arcdroid-memory-0:", &all, &all_size,
                                2u, &too_large) && too_large && !all);
  assert(!arc_io_read_all_limit("test-file", &all, &all_size, 2u,
                                &too_large) && too_large && reads == 0u);
  assert(arc_io_read_all_limit("test-file", &all, &all_size, 3u,
                               &too_large) && !too_large && all_size == 3u &&
         !memcmp(all, "\x04\x05\x06", 3u) && reads == 1u);
  free(all);
  assert(!arc_io_open_file(":arcdroid-memory-0:", true));
  ArcIoFile *file = arc_io_open_file(":arcdroid-memory-0:", false);
  assert(file && arc_io_write_file(file, memory, sizeof(memory)) == 0u &&
         writes == 0u && arc_io_close_file(file));

  file = arc_io_open_file("test-file", false);
  assert(file && arc_io_write_file(file, memory, sizeof(memory)) == 0u &&
         writes == 0u && arc_io_close_file(file));

  file = arc_io_open_file("test-file", true);
  assert(file && arc_io_write_file(file, memory, sizeof(memory)) ==
                     sizeof(memory) &&
         writes == 1u && arc_io_close_file(file));
  arc_io_clear_memory_sources();
  puts("arc_io_tests: memory and read-only write guards passed");
  return 0;
}
