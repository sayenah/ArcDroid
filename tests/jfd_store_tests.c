/* ArcDroid bounded JFD overlay and VFS failure tests. GPL-2.0-only. */
#include "arc_io.h"
#include "arcdroid_core.h"
#include "jfd_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

struct retro_vfs_file_handle { unsigned slot, mode; size_t at; };
struct retro_vfs_dir_handle { unsigned at, current; };
typedef struct { char path[256]; uint8_t *bytes; size_t size; bool exists; } File;
static File files[3];
static unsigned fault, notices, commits, directory_probes;
static int slot_unreadable = -1;
static bool ineligible_second;
enum { NO_FAULT, WRITE_FAULT, FLUSH_FAULT, CLOSE_FAULT, READ_FAULT,
       REMOVE_FAULT, RENAME_FAULT };
static bool trip(unsigned kind) { if (fault != kind) return false; fault = 0u; return true; }
static int file_index(const char *path)
{
   size_t n = strlen(path);
   if (n >= 2u && !strcmp(path + n - 2u, ".a")) return 0;
   if (n >= 2u && !strcmp(path + n - 2u, ".b")) return 1;
   if (n >= 4u && !strcmp(path + n - 4u, ".tmp")) return 2;
   return -1;
}
static struct retro_vfs_file_handle *vopen(const char *p, unsigned mode, unsigned hint)
{
   (void)hint;
   int i = file_index(p);
   if (i < 0 || (!(mode & RETRO_VFS_FILE_ACCESS_WRITE) && !files[i].exists)) return NULL;
   if (!(mode & RETRO_VFS_FILE_ACCESS_WRITE) && i == slot_unreadable)
      return NULL;
   struct retro_vfs_file_handle *h = calloc(1u, sizeof(*h)); assert(h);
   h->slot = (unsigned)i; h->mode = mode;
   if (mode & RETRO_VFS_FILE_ACCESS_WRITE)
   { free(files[i].bytes); files[i].bytes = NULL; files[i].size = 0u;
     files[i].exists = true; snprintf(files[i].path, sizeof(files[i].path), "%s", p); }
   return h;
}
static int vclose(struct retro_vfs_file_handle *h)
{ bool failed = h->mode & RETRO_VFS_FILE_ACCESS_WRITE ? trip(CLOSE_FAULT) : false;
  free(h); return failed ? -1 : 0; }
static int64_t vsize(struct retro_vfs_file_handle *h) { return (int64_t)files[h->slot].size; }
static int64_t vread(struct retro_vfs_file_handle *h, void *out, uint64_t n)
{
   if (h->slot == 2u && trip(READ_FAULT) && n) --n;
   if (n > files[h->slot].size - h->at) n = files[h->slot].size - h->at;
   memcpy(out, files[h->slot].bytes + h->at, (size_t)n);
   h->at += (size_t)n; return (int64_t)n;
}
static int64_t vwrite(struct retro_vfs_file_handle *h, const void *in, uint64_t n)
{
   if (trip(WRITE_FAULT) && n) --n;
   File *f = &files[h->slot];
   uint8_t *grown = realloc(f->bytes, h->at + (size_t)n); assert(grown || !n);
   f->bytes = grown;
   memcpy(f->bytes + h->at, in, (size_t)n);
   h->at += (size_t)n; if (h->at > f->size) f->size = h->at;
   return (int64_t)n;
}
static int vflush(struct retro_vfs_file_handle *h)
{ return h->mode & RETRO_VFS_FILE_ACCESS_WRITE && !trip(FLUSH_FAULT) ? 0 : -1; }
static int vremove(const char *p)
{
   int i = file_index(p);
   if (i < 0 || !files[i].exists || (i != 2 && trip(REMOVE_FAULT))) return -1;
   free(files[i].bytes); memset(&files[i], 0, sizeof(files[i])); return 0;
}
static int vrename(const char *from, const char *to)
{
   int a = file_index(from), b = file_index(to);
   if (a < 0 || b < 0 || !files[a].exists || files[b].exists || trip(RENAME_FAULT)) return -1;
   files[b] = files[a]; memset(&files[a], 0, sizeof(files[a]));
   snprintf(files[b].path, sizeof(files[b].path), "%s", to); return 0;
}
static int32_t vstat(const char *p, int32_t *size)
{
   int i = file_index(p);
   if (i < 0) { ++directory_probes; if (size) *size = 0; return RETRO_VFS_STAT_IS_VALID | RETRO_VFS_STAT_IS_DIRECTORY; }
   if (!files[i].exists) return 0;
   if (size) *size = (int32_t)files[i].size;
   return RETRO_VFS_STAT_IS_VALID;
}
static int vmkdir(const char *p) { (void)p; return 0; }
static struct retro_vfs_dir_handle *vopendir(const char *p, bool hidden)
{ (void)p; (void)hidden; return calloc(1u, sizeof(struct retro_vfs_dir_handle)); }
static bool vreaddir(struct retro_vfs_dir_handle *h)
{ while (h->at < 3u) { unsigned i = h->at++; if (files[i].exists) { h->current = i; return true; } } return false; }
static const char *vname(struct retro_vfs_dir_handle *h)
{ const char *p = strrchr(files[h->current].path, '/'); return p ? p + 1 : files[h->current].path; }
static bool visdir(struct retro_vfs_dir_handle *h) { (void)h; return false; }
static int vclosedir(struct retro_vfs_dir_handle *h) { free(h); return 0; }
static struct retro_vfs_interface vfs = {
   .open = vopen, .close = vclose, .size = vsize, .read = vread,
   .write = vwrite, .flush = vflush, .remove = vremove, .rename = vrename,
   .stat = vstat, .mkdir = vmkdir, .opendir = vopendir, .readdir = vreaddir,
   .dirent_get_name = vname, .dirent_is_dir = visdir, .closedir = vclosedir
};
void arc_jfd_store_notice(const char *message, bool warning)
{ (void)message; (void)warning; ++notices; }
void arc_jfd_store_log_commit(unsigned image_number, uint64_t generation,
                              size_t sector_index)
{ assert(image_number == 1u && generation && sector_index < 2u); ++commits; }
static ArcJfdImage image_new(void)
{
   ArcJfdImage image = {0};
   image.decoded = calloc(2048u, 1u); assert(image.decoded);
   image.decoded_size = 2048u;
   image.sectors = calloc(2u, sizeof(*image.sectors)); assert(image.sectors);
   image.sector_count = 2u; image.track_count = 2u;
   image.tracks[0].sector_count = 2u;
   image.sectors[0] = (ArcJfdSector){0u, 256u, 128u, 0u, 2u, 0u};
   image.sectors[1] = (ArcJfdSector){1000u, 512u, 128u, 1u, 2u, 0u};
   if (ineligible_second) image.sectors[1].flags = ARC_JFD_SLOW_SECTOR;
   image.extended = true; image.flags = 2u;
   return image;
}
static ArcJfdImage load(bool writable)
{
   ArcJfdImage image = image_new(); const char *reason = NULL;
   assert(arc_jfd_store_load(&image, "save", writable, true, 1u, &reason));
   assert(reason);
   return image;
}
static void clear_files(void)
{ for (unsigned i = 0u; i < 3u; ++i) { free(files[i].bytes); memset(&files[i], 0, sizeof(files[i])); } }
static void fresh_process_generation(uint64_t expected, uint8_t value)
{
#if !defined(_WIN32)
   pid_t child = fork(); assert(child >= 0);
   if (child == 0)
   {
      ArcJfdImage reloaded = load(true);
      bool okay = reloaded.overlay->generation == expected &&
         reloaded.overlay->data && reloaded.overlay->data[0] &&
         reloaded.overlay->data[0][0] == value;
      arc_jfd_free(&reloaded);
      _exit(okay ? 0 : 1);
   }
   int status = 0;
   assert(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0);
#else
   ArcJfdImage reloaded = load(true);
   assert(reloaded.overlay->generation == expected &&
          reloaded.overlay->data[0][0] == value);
   arc_jfd_free(&reloaded);
#endif
}
static void fix_checksum(File *file)
{
   uint32_t crc = arc_crc32(file->bytes, file->size - 4u);
   for (unsigned i = 0u; i < 4u; ++i)
      file->bytes[file->size - 4u + i] = (uint8_t)(crc >> (8u * i));
}
static void malformed_slot(unsigned active, size_t offset, bool recalculate,
                           uint64_t older_generation)
{
   File *file = &files[active];
   assert(file->exists && offset < file->size);
   uint8_t *original = malloc(file->size); assert(original);
   memcpy(original, file->bytes, file->size);
   file->bytes[offset] ^= 0x01u;
   if (recalculate) fix_checksum(file);
   uint8_t *mutated = malloc(file->size); assert(mutated);
   memcpy(mutated, file->bytes, file->size);
   unsigned before = notices;
   ArcJfdImage image = load(true);
   assert(image.overlay->damaged && !image.overlay->writable &&
          image.overlay->generation == older_generation &&
          notices == before + 1u &&
          !memcmp(file->bytes, mutated, file->size));
   arc_jfd_free(&image);
   memcpy(file->bytes, original, file->size);
   free(mutated); free(original);
}
int main(void)
{
   arc_io_set_vfs(&vfs);
   uint8_t first[128], second[128], third[128];
   memset(first, 0x11, sizeof(first)); memset(second, 0x22, sizeof(second));
   memset(third, 0x33, sizeof(third));
   ArcJfdImage image = load(true);
   assert(image.overlay->writable && image.overlay->generation == 0u);
   assert(image.overlay->commit(&image, 0u, first));
   assert(image.overlay->generation == 1u && image.overlay->records == 1u);
   arc_jfd_free(&image);
   fresh_process_generation(1u, 0x11u);
   image = load(true);
   assert(image.overlay->generation == 1u && image.overlay->data[0][0] == 0x11u);
   assert(image.overlay->commit(&image, 0u, second));
   arc_jfd_free(&image);
   image = load(true);
   assert(image.overlay->generation == 2u && image.overlay->data[0][0] == 0x22u);
   assert(files[0].exists && files[1].exists && commits == 2u);
   arc_jfd_free(&image);
   fresh_process_generation(2u, 0x22u);
   for (unsigned failure = WRITE_FAULT; failure <= RENAME_FAULT; ++failure)
   {
      image = load(true); fault = failure; unsigned before = notices;
      assert(!image.overlay->commit(&image, 0u, third));
      assert(image.overlay->failed && !image.overlay->writable && notices == before + 1u);
      assert(image.overlay->data[0][0] == 0x22u);
      assert(!image.overlay->commit(&image, 0u, third));
      arc_jfd_free(&image);
      fresh_process_generation(2u, 0x22u);
      image = load(true);
      assert(image.overlay->generation == 2u && image.overlay->data[0][0] == 0x22u);
      arc_jfd_free(&image);
   }
   /* An interrupted candidate is ignored. A corrupted committed newest slot
    * leaves both slots untouched, but exposes the valid older view. */
   image = load(true);
   assert(image.overlay->commit(&image, 0u, third));
   assert(image.overlay->commit(&image, 0u, second));
   uint64_t latest = image.overlay->generation;
   unsigned active = files[1].exists && files[1].bytes[68] == (uint8_t)latest ? 1u : 0u;
   uint8_t saved = files[active].bytes[0];
   files[active].bytes[0] ^= 0x01u;
   arc_jfd_free(&image);
   unsigned before = notices;
   image = load(true);
   assert(image.overlay->damaged && !image.overlay->writable &&
          image.overlay->generation == latest - 1u && notices == before + 1u);
   assert(files[active].bytes[0] == (uint8_t)(saved ^ 0x01u));
   arc_jfd_free(&image);
   files[active].bytes[0] = saved;
   image = load(false);
   assert(!image.overlay->writable && !image.overlay->visible &&
          image.overlay->generation == latest);
   arc_jfd_free(&image);
   image = load(true);
   assert(image.overlay->commit(&image, 1u, first));
   latest = image.overlay->generation;
   arc_jfd_free(&image);
   active = files[1].exists && files[1].bytes[68] == (uint8_t)latest ? 1u : 0u;
   /* Each field is checked independently with a valid checksum so malformed
    * bindings cannot be dismissed as checksum-only failures. */
   for (size_t offset = 0u; offset <= 86u; ++offset)
   {
      if (offset == 0u || offset == 4u || offset == 8u || offset == 16u ||
          offset == 36u || offset == 68u || offset == 76u ||
          offset == 80u || offset == 82u || offset == 84u ||
          offset == 85u || offset == 86u)
         malformed_slot(active, offset, true, latest - 1u);
   }
   malformed_slot(active, files[active].size - 1u, false, latest - 1u);
   /* Duplicate and descending record keys are independently invalid. */
   File *newest = &files[active];
   uint8_t *original = malloc(newest->size); assert(original);
   memcpy(original, newest->bytes, newest->size);
   newest->bytes[216u + 2u] = 0u;
   fix_checksum(newest);
   image = load(true);
   assert(image.overlay->damaged && image.overlay->generation == latest - 1u);
   arc_jfd_free(&image);
   memcpy(newest->bytes, original, newest->size);
   free(original);
   /* A correctly checksummed suffix is still an invalid snapshot. */
   size_t original_size = newest->size;
   uint8_t *extended = realloc(newest->bytes, original_size + 1u);
   assert(extended);
   newest->bytes = extended;
   memmove(extended + original_size - 3u, extended + original_size - 4u, 4u);
   extended[original_size - 4u] = 0xa5u;
   newest->size = original_size + 1u;
   fix_checksum(newest);
   image = load(true);
   assert(image.overlay->damaged && image.overlay->generation == latest - 1u);
   arc_jfd_free(&image);
   newest->size = original_size;
   memmove(extended + original_size - 4u, extended + original_size - 3u, 4u);
   fix_checksum(newest);
   /* Exercise the full parser under ASan/UBSan with reproducible mutations
    * whose checksums are repaired so validation reaches record fields. */
   uint8_t *baseline = malloc(newest->size); assert(baseline);
   memcpy(baseline, newest->bytes, newest->size);
   uint32_t seed = UINT32_C(0x4a464431);
   for (unsigned iteration = 0u; iteration < 512u; ++iteration)
   {
      seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
      size_t position = seed % (newest->size - 4u);
      newest->bytes[position] ^= (uint8_t)(1u << ((seed >> 16u) & 7u));
      fix_checksum(newest);
      image = load(true);
      arc_jfd_free(&image);
      memcpy(newest->bytes, baseline, newest->size);
   }
   free(baseline);
   slot_unreadable = (int)active;
   image = load(true);
   assert(image.overlay->damaged && !image.overlay->writable &&
          image.overlay->generation == latest - 1u);
   arc_jfd_free(&image);
   slot_unreadable = -1;
   ineligible_second = true;
   image = load(true);
   assert(image.overlay->damaged && !image.overlay->writable &&
          image.overlay->generation == latest - 1u);
   arc_jfd_free(&image);
   ineligible_second = false;
   /* An equal-generation pair is damaged even if both snapshots parse. */
   unsigned older = 1u - active;
   memcpy(newest->bytes + 68u, files[older].bytes + 68u, 8u);
   fix_checksum(newest);
   image = load(true);
   assert(image.overlay->damaged && !image.overlay->writable);
   arc_jfd_free(&image);
   clear_files();
   static const struct { bool extended; uint32_t flags; const char *reason; }
      excluded[] = {{false, 2u, "legacy-header"},
                    {true, 3u, "wp-flag"}, {true, 0u, "no-flag"}};
   for (size_t i = 0u; i < sizeof(excluded) / sizeof(excluded[0]); ++i)
   {
      ArcJfdImage excluded_image = image_new();
      excluded_image.extended = excluded[i].extended;
      excluded_image.flags = excluded[i].flags;
      const char *excluded_reason = NULL;
      unsigned before = directory_probes;
      assert(arc_jfd_store_load(&excluded_image, NULL, true, true, 1u,
                                &excluded_reason));
      assert(!strcmp(excluded_reason, excluded[i].reason) &&
             directory_probes == before && excluded_image.overlay &&
             !excluded_image.overlay->writable);
      arc_jfd_free(&excluded_image);
   }
   ArcJfdImage shared = image_new();
   shared.sectors[1].data_offset = shared.sectors[0].data_offset + 64u;
   const char *shared_reason = NULL;
   assert(arc_jfd_store_load(&shared, "save", true, false, 1u, &shared_reason));
   assert(!shared.overlay->eligible[0] && !shared.overlay->eligible[1]);
   arc_jfd_free(&shared);
   ArcJfdImage duplicate = image_new();
   duplicate.sectors[1].id = duplicate.sectors[0].id;
   const char *duplicate_reason = NULL;
   assert(arc_jfd_store_load(&duplicate, "save", true, false, 1u,
                             &duplicate_reason));
   assert(!duplicate.overlay->eligible[0] && !duplicate.overlay->eligible[1]);
   arc_jfd_free(&duplicate);
   /* Maximum permitted descriptor count must not trigger pairwise payload
    * scans on every native load. CPU time avoids shared-host scheduling noise. */
   ArcJfdImage dense = {0};
   dense.decoded_size = ARC_JFD_MAX_DECODED;
   dense.decoded = calloc(dense.decoded_size, 1u);
   dense.sector_count = ARC_JFD_MAX_TRACK_SIDES * ARC_JFD_MAX_SECTORS_PER_TRACK;
   dense.sectors = calloc(dense.sector_count, sizeof(*dense.sectors));
   assert(dense.decoded && dense.sectors);
   dense.track_count = ARC_JFD_MAX_TRACK_SIDES;
   dense.extended = true; dense.flags = 2u;
   for (unsigned track = 0u; track < dense.track_count; ++track)
   {
      dense.tracks[track].first_sector = track * ARC_JFD_MAX_SECTORS_PER_TRACK;
      dense.tracks[track].sector_count = ARC_JFD_MAX_SECTORS_PER_TRACK;
      for (unsigned sector = 0u; sector < ARC_JFD_MAX_SECTORS_PER_TRACK; ++sector)
      {
         size_t index = dense.tracks[track].first_sector + sector;
         dense.sectors[index].data_offset = (uint32_t)(index * 128u);
         dense.sectors[index].size = 128u;
         dense.sectors[index].id = (uint8_t)sector;
         dense.sectors[index].density = 2u;
      }
   }
   const char *dense_reason = NULL;
   clock_t started = clock();
   assert(arc_jfd_store_load(&dense, "save", true, false, 1u, &dense_reason));
   double cpu_seconds = (double)(clock() - started) / CLOCKS_PER_SEC;
   assert(cpu_seconds < 2.0 && dense.overlay->eligible[0] &&
          dense.overlay->eligible[dense.sector_count - 1u]);
   arc_jfd_free(&dense);
   puts("jfd_store_tests: generations, malformed slots, recovery, failures and Off passed");
   return 0;
}
