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
{ assert(image_number == 1u && generation &&
         sector_index < ARC_JFD_PDD5_SLOTS); ++commits; }
static uint8_t old_map_checksum(const uint8_t *block)
{
   unsigned checksum = 0xffu;
   for (unsigned index = 255u; index-- > 0u;)
   {
      if (checksum > 0xffu) checksum = (checksum + 1u) & 0xffu;
      checksum += block[index];
   }
   return (uint8_t)checksum;
}
static ArcJfdImage image_new(void)
{
   ArcJfdImage image = {0};
   image.decoded = calloc(32768u, 1u); assert(image.decoded);
   image.decoded_size = 32768u;
   /* A valid fully allocated old map lets the eligibility-drift test reach
    * slot validation even when one P-DD5 descriptor becomes non-writable. */
   uint8_t *map = image.decoded + 1024u;
   map[252] = 0x80u; map[253] = 0x0cu; /* 3200 logical sectors */
   map[255] = old_map_checksum(map);
   map[511] = old_map_checksum(map + 256u);
   image.sectors = calloc(15u, sizeof(*image.sectors)); assert(image.sectors);
   image.sector_count = 15u; image.track_count = 160u;
   for (unsigned track = 0u; track < 3u; ++track)
   {
      image.tracks[track].first_sector = track * 5u;
      image.tracks[track].sector_count = 5u;
      for (unsigned id = 0u; id < 5u; ++id)
      {
         unsigned index = track * 5u + id;
         image.sectors[index] = (ArcJfdSector){id * ARC_JFD_PDD5_SPACING_US,
            1024u + index * 1024u, 1024u, (uint8_t)id, 2u, 0u};
      }
   }
   for (unsigned track = 3u; track < 160u; ++track)
      image.tracks[track].first_sector = 15u;
   if (ineligible_second) image.sectors[1].flags = ARC_JFD_SLOW_SECTOR;
   image.extended = true; image.flags = 2u;
   return image;
}
static ArcJfdImage creation_fixture(const char *root, const char *name)
{
   char path[4096], error[160];
   int n = snprintf(path, sizeof(path), "%s/jfd-creation/%s", root, name);
   assert(n > 0 && (size_t)n < sizeof(path));
   FILE *file = fopen(path, "rb");
   assert(file && fseek(file, 0, SEEK_END) == 0);
   long length = ftell(file);
   assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
   uint8_t *bytes = malloc((size_t)length);
   assert(bytes && fread(bytes, 1u, (size_t)length, file) == (size_t)length);
   assert(fclose(file) == 0);
   ArcJfdImage image;
   bool parsed = arc_jfd_parse_owned(bytes, (size_t)length, &image,
                                     error, sizeof(error));
   if (!parsed) fprintf(stderr, "%s: %s\n", name, error);
   assert(parsed);
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
static void expect_creation_layout(const char *root, const char *name,
                                   bool writable, bool rounded)
{
   clear_files();
   ArcJfdImage image = creation_fixture(root, name);
   const char *reason = NULL;
   unsigned probes = directory_probes;
   assert(arc_jfd_store_load(&image, "save", true, false, 1u, &reason));
   if (image.overlay->writable != writable)
      fprintf(stderr, "%s: writable=%d reason=%s\n",
              name, image.overlay->writable, reason);
   assert(image.overlay->writable == writable &&
          !strcmp(reason, writable ? "rw-flag" : "layout-unsupported"));
   if (!writable) assert(directory_probes == probes);
   if (rounded)
      assert(image.overlay->creation_mask[1] == 0x10u &&
             image.overlay->creation_time[1][4] == 146352u);
   arc_jfd_free(&image);
   clear_files();
}
static void test_layout_low(const char *root, const char *kind)
{
   if (!strcmp(kind, "short"))
   {
      expect_creation_layout(root, "short-full.jfd", false, false);
      expect_creation_layout(root, "short-partial.jfd", false, false);
   }
   else if (!strcmp(kind, "irregular"))
   {
      expect_creation_layout(root, "irregular-id-only-free.jfd", false, false);
      expect_creation_layout(root, "irregular-bad-crc-free.jfd", false, false);
      expect_creation_layout(root, "irregular-id-only-allocated.jfd", true, false);
      expect_creation_layout(root, "irregular-bad-crc-allocated.jfd", true, false);
      expect_creation_layout(root, "irregular-all-ids.jfd", true, false);
   }
   else if (!strcmp(kind, "timing"))
   {
      expect_creation_layout(root, "timed-rounded.jfd", true, true);
      expect_creation_layout(root, "unschedulable.jfd", false, false);
   }
   else assert(!"unknown JFD gate test case");
}
int main(int argc, char **argv)
{
   assert(argc == 2 || argc == 3);
   arc_io_set_vfs(&vfs);
   if (argc == 3)
   {
      test_layout_low(argv[1], argv[2]);
      return 0;
   }
   test_layout_low(argv[1], "short");
   test_layout_low(argv[1], "irregular");
   test_layout_low(argv[1], "timing");
   uint8_t first[1024], second[1024], third[1024];
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
   for (size_t offset = 0u; offset <= 88u; ++offset)
   {
      if (offset == 0u || offset == 4u || offset == 8u || offset == 16u ||
          offset == 36u || offset == 68u || offset == 76u ||
          offset == 80u || offset == 82u || offset == 83u ||
          offset == 85u || offset == 86u || offset == 87u ||
          offset == 88u)
         malformed_slot(active, offset, true, latest - 1u);
   }
   malformed_slot(active, files[active].size - 1u, false, latest - 1u);
   /* Duplicate and descending record keys are independently invalid. */
   File *newest = &files[active];
   uint8_t *original = malloc(newest->size); assert(original);
   memcpy(original, newest->bytes, newest->size);
   newest->bytes[80u + 9u + 1024u + 3u] = 0u;
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
   /* Every irregular neighbour remains non-creating, including otherwise
    * complete track-sides with a defective or shared extra descriptor. */
   for (unsigned variant = 0u; variant < 6u; ++variant)
   {
      ArcJfdImage neighbour = creation_fixture(argv[1], "irregular-all-ids.jfd");
      size_t extra = neighbour.tracks[1].first_sector + 5u;
      assert(neighbour.sectors[extra].id == 9u);
      switch (variant)
      {
         case 0u: neighbour.sectors[extra].data_offset = UINT32_MAX; break;
         case 1u: neighbour.sectors[extra].flags = ARC_JFD_ID_CRC_BAD; break;
         case 2u: neighbour.sectors[extra].flags = ARC_JFD_DATA_CRC_BAD; break;
         case 3u: neighbour.sectors[extra].flags = ARC_JFD_SLOW_SECTOR; break;
         case 4u: neighbour.sectors[extra].data_offset =
                     neighbour.sectors[extra - 1u].data_offset; break;
         default: neighbour.sectors[extra].id = 0u; break;
      }
      const char *why = NULL;
      assert(arc_jfd_store_load(&neighbour, "save", true, false, 1u, &why));
      /* Shared payload with ID 4 and duplicate ID 0 leave a P-DD5 ID
       * non-writable; the captured map here cannot justify that gap. */
      if (variant >= 4u)
         assert(!neighbour.overlay->writable &&
                !strcmp(why, "layout-unsupported") &&
                !neighbour.overlay->eligible[extra]);
      else
         assert(neighbour.overlay->writable &&
                neighbour.overlay->pdd5_profile &&
                neighbour.overlay->creation_mask[1] == 0u &&
                !neighbour.overlay->eligible[extra]);
      ArcJfdView extra_view;
      assert(!arc_jfd_effective_sector(&neighbour, 1u, 6u, true,
                                        &extra_view));
      arc_jfd_free(&neighbour);
      clear_files();
   }
   /* C-11's gate and all placements derive from the immutable decoded JFD.
    * The position of ID 3 is the same before and after writing ID 1. */
   static const struct {
      const char *name; bool writable; const char *reason;
   } layouts[] = {
      {"partial-untimed.jfd", true, "rw-flag"},
      {"partial-edge.jfd", true, "rw-flag"},
      {"partial-timed.jfd", true, "rw-flag"},
      {"boundary.jfd", true, "rw-flag"},
      {"unschedulable.jfd", false, "layout-unsupported"},
      {"nonmatching.jfd", false, "layout-unsupported"},
      {"half-vote.jfd", false, "layout-unsupported"},
      {"irregular-free.jfd", false, "layout-unsupported"},
      {"irregular-free-sequence.jfd", false, "layout-unsupported"},
      {"irregular-newmap-free.jfd", false, "layout-unsupported"},
      {"irregular-allocated.jfd", true, "rw-flag"},
      {"irregular-all-ids.jfd", true, "rw-flag"}
   };
   for (size_t i = 0u; i < sizeof(layouts) / sizeof(layouts[0]); ++i)
   {
      clear_files();
      ArcJfdImage sample = creation_fixture(argv[1], layouts[i].name);
      const char *why = NULL;
      unsigned probes = directory_probes;
      assert(arc_jfd_store_load(&sample, "save", true, false, 1u, &why));
      if (sample.overlay->writable != layouts[i].writable ||
          strcmp(why, layouts[i].reason))
         fprintf(stderr, "%s: writable=%d reason=%s\n",
                 layouts[i].name, sample.overlay->writable, why);
      assert(sample.overlay->writable == layouts[i].writable &&
             !strcmp(why, layouts[i].reason));
      if (!layouts[i].writable) assert(directory_probes == probes);
      if (!strcmp(layouts[i].name, "irregular-newmap-free.jfd"))
      {
         bool free_track[ARC_JFD_PDD5_TRACKS];
         assert(arc_jfd_base_free_tracks(&sample, free_track) &&
                free_track[159]);
      }
      if (i == 0u)
      {
         assert(sample.overlay->creation_time[1][1] == 109056u &&
                sample.overlay->creation_time[1][3] == 145408u &&
                sample.overlay->creation_mask[159] == 0x0au &&
                sample.overlay->creation_mask[2] == 0x1fu &&
                sample.overlay->creation_time[2][4] == 145408u &&
                sample.track_count == 166u);
         ArcJfdView view;
         assert(!arc_jfd_effective_sector(&sample, 160u, 1u, true, &view) &&
                !arc_jfd_effective_sector(&sample, 165u, 1u, true, &view));
      }
      if (i == 1u)
         assert(sample.overlay->creation_mask[1] == 0x11u &&
                sample.overlay->creation_time[1][0] == 109056u &&
                sample.overlay->creation_time[1][4] == 145408u);
      if (i == 2u)
         assert(sample.overlay->creation_time[1][1] == 36352u &&
                sample.overlay->creation_time[1][3] == 116352u);
      if (i == 3u)
         assert(sample.overlay->creation_time[1][1] == 26352u);
      if (!strcmp(layouts[i].name, "irregular-allocated.jfd"))
         assert(sample.overlay->creation_mask[159] == 0u);
      arc_jfd_free(&sample);
   }
   /* The immutable layout gate determines its C-10 reason even when no save
    * directory is available; no slot lookup or directory creation occurs. */
   clear_files();
   ArcJfdImage no_save_layout = creation_fixture(argv[1], "nonmatching.jfd");
   const char *no_save_reason = NULL;
   unsigned no_save_probes = directory_probes;
   assert(arc_jfd_store_load(&no_save_layout, NULL, true, false, 1u,
                             &no_save_reason) &&
          !strcmp(no_save_reason, "layout-unsupported") &&
          no_save_layout.overlay && !no_save_layout.overlay->writable &&
          directory_probes == no_save_probes);
   arc_jfd_free(&no_save_layout);
   clear_files();
   ArcJfdImage outside = creation_fixture(argv[1], "irregular-allocated.jfd");
   const char *outside_reason = NULL;
   assert(arc_jfd_store_load(&outside, "save", true, false, 1u,
                             &outside_reason) && outside.overlay->writable &&
          outside.overlay->creation_mask[159] == 0u);
   uint8_t outside_pattern[1024]; memset(outside_pattern, 0x59, 1024u);
   assert(outside.overlay->commit(&outside, 0u, outside_pattern) &&
          outside.overlay->commit_created(&outside, 2u, 1u,
                                          outside_pattern));
   arc_jfd_free(&outside);
   outside = creation_fixture(argv[1], "irregular-allocated.jfd");
   assert(arc_jfd_store_load(&outside, "save", true, false, 1u,
                             &outside_reason) &&
          outside.overlay->data[0][0] == 0x59u &&
          outside.overlay->created[2][1][0] == 0x59u &&
          !outside.overlay->created[159][0]);
   outside.overlay->virtual_reads = true;
   ArcJfdView irregular_view;
   assert(!arc_jfd_effective_sector(&outside, 159u,
          outside.tracks[159].sector_count, false, &irregular_view));
   arc_jfd_free(&outside);
   clear_files();
   ArcJfdImage launch_full = creation_fixture(argv[1], "launch-full.jfd");
   ArcJfdImage launch_partial = creation_fixture(argv[1], "launch-partial.jfd");
   const char *launch_reason = NULL;
   assert(arc_jfd_store_load(&launch_partial, "save", true, false, 1u,
                             &launch_reason));
   ArcLaunchDiscovery discovery;
   assert(arc_launch_discover(NULL, 0u, NULL, &launch_partial, &discovery) &&
          !discovery.readable);
   assert(launch_partial.overlay->commit_created(&launch_partial, 0u, 1u,
          launch_full.decoded + launch_full.sectors[1].data_offset));
   assert(arc_launch_discover(NULL, 0u, NULL, &launch_partial, &discovery) &&
          discovery.readable && discovery.application_count == 1u);
   arc_jfd_free(&launch_partial);
   launch_partial = creation_fixture(argv[1], "launch-partial.jfd");
   assert(arc_jfd_store_load(&launch_partial, "save", true, false, 1u,
                             &launch_reason) &&
          arc_launch_discover(NULL, 0u, NULL, &launch_partial, &discovery) &&
          discovery.application_count == 1u);
   arc_jfd_free(&launch_partial);
   launch_partial = creation_fixture(argv[1], "launch-partial.jfd");
   assert(arc_jfd_store_load(&launch_partial, "save", false, false, 1u,
                             &launch_reason) &&
          arc_launch_discover(NULL, 0u, NULL, &launch_partial, &discovery) &&
          !discovery.readable);
   arc_jfd_free(&launch_partial);
   arc_jfd_free(&launch_full);
   clear_files();
   ArcJfdImage created = creation_fixture(argv[1], "partial-untimed.jfd");
   const char *creation_reason = NULL;
   assert(arc_jfd_store_load(&created, "save", true, false, 1u,
                             &creation_reason));
   ArcJfdView view;
   uint64_t blank_generation = created.overlay->generation;
   assert(!created.overlay->virtual_reads &&
          !arc_jfd_effective_sector(&created, 1u, 4u, false, &view));
   created.overlay->virtual_reads = true; /* successful native preparation */
   assert(!arc_jfd_effective_sector(&created, 1u,
          created.tracks[1].sector_count + 7u, false, &view));
   assert(arc_jfd_effective_sector(&created, 1u, 4u, false, &view) &&
          view.created && view.sector.id == 1u && view.sector.size == 1024u &&
          view.sector.flags == 0u && view.data);
   for (unsigned i = 0u; i < 1024u; ++i) assert(view.data[i] == 0u);
   assert(created.overlay->generation == blank_generation &&
          created.overlay->records == 0u);
   ArcDigest identity_before = arc_digest(created.decoded, created.decoded_size);
   uint8_t pattern[1024]; memset(pattern, 0x6au, sizeof(pattern));
   assert(created.overlay->commit_created(&created, 1u, 3u, pattern));
   assert(arc_jfd_effective_sector(&created, 1u, 6u, false, &view) &&
          view.created && view.sector.id == 3u && view.data[0] == 0x6au);
   memset(pattern, 0x15u, sizeof(pattern));
   assert(created.overlay->commit_created(&created, 1u, 1u, pattern));
   assert(created.overlay->generation == 2u && created.overlay->records == 2u);
   ArcDigest identity_after = arc_digest(created.decoded, created.decoded_size);
   assert(!strcmp(identity_before.sha1, identity_after.sha1) &&
          identity_before.crc32 == identity_after.crc32);
   uint8_t *first_order = malloc(files[0].size); assert(first_order);
   size_t first_order_size = files[0].size;
   memcpy(first_order, files[0].bytes, first_order_size);
   arc_jfd_free(&created);
   created = creation_fixture(argv[1], "partial-untimed.jfd");
   assert(arc_jfd_store_load(&created, "save", true, false, 1u,
                             &creation_reason) &&
          created.overlay->created[1][1][0] == 0x15u &&
          created.overlay->created[1][3][0] == 0x6au);
   assert(!created.overlay->virtual_reads &&
          !arc_jfd_effective_sector(&created, 1u, 3u, false, &view));
   created.overlay->virtual_reads = true;
   assert(arc_jfd_effective_sector(&created, 1u, 4u, false, &view) &&
          view.data[0] == 0x15u &&
          arc_jfd_effective_sector(&created, 1u, 6u, false, &view) &&
          view.data[0] == 0x6au);
   arc_jfd_free(&created);
   created = creation_fixture(argv[1], "partial-untimed.jfd");
   assert(arc_jfd_store_load(&created, "save", false, false, 1u,
                             &creation_reason) &&
          !created.overlay->writable && !created.overlay->visible &&
          !created.overlay->virtual_reads &&
          !arc_jfd_effective_sector(&created, 1u, 3u, false, &view) &&
          !arc_jfd_effective_sector(&created, 1u, 4u, false, &view));
   arc_jfd_free(&created);
   clear_files();
   created = creation_fixture(argv[1], "partial-untimed.jfd");
   assert(arc_jfd_store_load(&created, "save", true, false, 1u,
                             &creation_reason));
   assert(created.overlay->commit_created(&created, 1u, 1u, pattern));
   memset(pattern, 0x6au, sizeof(pattern));
   assert(created.overlay->commit_created(&created, 1u, 3u, pattern));
   assert(files[0].size == first_order_size &&
          !memcmp(files[0].bytes, first_order, first_order_size));
   free(first_order);
   /* The created-kind parser is fuzzed with repaired checksums so the
    * mutation reaches kind, key and payload validation under ASan/UBSan. */
   size_t created_size = files[0].size;
   uint8_t *created_baseline = malloc(created_size); assert(created_baseline);
   memcpy(created_baseline, files[0].bytes, created_size);
   uint32_t created_seed = UINT32_C(0x43524541);
   for (unsigned iteration = 0u; iteration < 256u; ++iteration)
   {
      created_seed = created_seed * UINT32_C(1664525) +
                     UINT32_C(1013904223);
      size_t position = created_seed % (created_size - 4u);
      files[0].bytes[position] ^= (uint8_t)(1u << ((created_seed >> 16u) & 7u));
      fix_checksum(&files[0]);
      ArcJfdImage fuzzed = creation_fixture(argv[1], "partial-untimed.jfd");
      const char *fuzz_reason = NULL;
      assert(arc_jfd_store_load(&fuzzed, "save", true, false, 1u,
                                &fuzz_reason));
      arc_jfd_free(&fuzzed);
      memcpy(files[0].bytes, created_baseline, created_size);
   }
   files[0].bytes[76u] = 0xffu; fix_checksum(&files[0]);
   ArcJfdImage overbound = creation_fixture(argv[1], "partial-untimed.jfd");
   const char *bound_reason = NULL;
   assert(arc_jfd_store_load(&overbound, "save", true, false, 1u,
                             &bound_reason) && overbound.overlay->damaged &&
          !overbound.overlay->writable);
   arc_jfd_free(&overbound);
   memcpy(files[0].bytes, created_baseline, created_size);
   free(created_baseline);
   fault = RENAME_FAULT;
   created.overlay->virtual_reads = true;
   assert(!created.overlay->commit_created(&created, 1u, 1u, pattern) &&
          created.overlay->generation == 2u &&
          created.overlay->created[1][1][0] == 0x15u &&
          created.overlay->virtual_reads &&
          arc_jfd_effective_sector(&created, 1u, 4u, false, &view) &&
          view.data[0] == 0x15u);
   arc_jfd_free(&created);
   created = creation_fixture(argv[1], "partial-untimed.jfd");
   assert(arc_jfd_store_load(&created, "save", true, false, 1u,
                             &creation_reason) &&
          created.overlay->created[1][1][0] == 0x15u);
   arc_jfd_free(&created);
   /* Created-kind/key corruption and the superseded development version
    * leave committed slots untouched and protect the session. */
   uint8_t saved_kind = files[0].bytes[82u];
   files[0].bytes[82u] = 2u; fix_checksum(&files[0]);
   created = creation_fixture(argv[1], "partial-untimed.jfd");
   assert(arc_jfd_store_load(&created, "save", true, false, 1u,
                             &creation_reason) && created.overlay->damaged &&
          !created.overlay->virtual_reads &&
          !arc_jfd_effective_sector(&created, 1u, 3u, false, &view));
   arc_jfd_free(&created);
   files[0].bytes[82u] = saved_kind; fix_checksum(&files[0]);
   files[0].bytes[4u] = 1u; fix_checksum(&files[0]);
   created = creation_fixture(argv[1], "partial-untimed.jfd");
   assert(arc_jfd_store_load(&created, "save", true, false, 1u,
                             &creation_reason) && created.overlay->damaged &&
          !created.overlay->writable);
   arc_jfd_free(&created);
   clear_files();
   /* A first created record cannot become visible if its publish rename
    * fails. The next load sees no generation at all. */
   created = creation_fixture(argv[1], "partial-untimed.jfd");
   assert(arc_jfd_store_load(&created, "save", true, false, 1u,
                             &creation_reason));
   created.overlay->virtual_reads = true;
   fault = RENAME_FAULT;
   assert(!created.overlay->commit_created(&created, 1u, 1u, pattern) &&
          !created.overlay->created[1][1] && !files[0].exists &&
          !files[1].exists && created.overlay->virtual_reads &&
          arc_jfd_effective_sector(&created, 1u, 4u, false, &view) &&
          view.data[0] == 0u && created.overlay->generation == 0u);
   arc_jfd_free(&created);
   clear_files();
   created = creation_fixture(argv[1], "partial-untimed.jfd");
   assert(arc_jfd_store_load(&created, "save", true, false, 1u,
                             &creation_reason) &&
          created.overlay->generation == 0u &&
          !created.overlay->created[1][1]);
   arc_jfd_free(&created);
   clear_files();
   puts("jfd_store_tests: generations, malformed slots, recovery, failures and Off passed");
   return 0;
}
