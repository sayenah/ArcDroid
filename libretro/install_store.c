/* ArcDroid-authored install hard-disc store. GPL-2.0-only.
 * Committed manifests select immutable bases and append-only command journals.
 * The exact format is versioned and every persisted binding is checked. */
#include "install_store.h"
#include "arc_io.h"
#include "arcdroid_core.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MANIFEST_SIZE 276u
#define JOURNAL_HEADER 124u
#define RECORD_HEADER 80u
#define RECORD_TRAILER 16u
#define STORE_VERSION 1u
#define COMPACT_THRESHOLD (1024u * 1024u)
#define JOURNAL_CAP (8u * 1024u * 1024u)
#define MAX_COMMAND_SECTORS 256u

struct ArcInstallStore {
   char directory[4096];
   char id[41];
   char base_hash[65];
   char base_leaf[64];
   char journal_leaf[64];
   uint8_t *image;
   uint64_t generation, sequence;
   size_t journal_length;
   unsigned selected_slot;
   unsigned recipe_revision;
   ArcInstallStoreState state;
   bool writable, failed, incomplete_tail;
   ArcInstallStoreLog log;
   void *log_opaque;
};

typedef struct {
   char id[41];
   char base_hash[65];
   char base_leaf[64];
   char journal_leaf[64];
   uint64_t generation;
   uint32_t revision;
} Manifest;

typedef struct {
   bool ready[2];
   bool any;
   bool unknown;
   bool candidate_base;
   bool candidate_base_tmp;
   bool candidate_journal;
   bool candidate_journal_tmp;
   bool ready_tmp;
} Listing;

static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
   ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u); }
static uint64_t get64(const uint8_t *p)
{ return (uint64_t)get32(p) | ((uint64_t)get32(p + 4u) << 32u); }
static void put32(uint8_t *p, uint32_t n)
{ for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(n >> (8u * i)); }
static void put64(uint8_t *p, uint64_t n)
{ put32(p, (uint32_t)n); put32(p + 4u, (uint32_t)(n >> 32u)); }
static void set_error(char *error, size_t size, const char *reason)
{ if (error && size) snprintf(error, size, "%s", reason); }
static void report(ArcInstallStore *s, const char *reason)
{
   if (s->log) s->log(s->log_opaque, reason);
}
static void report_vfs_failure(ArcInstallStore *s, const char *phase,
                               const char *operation, const char *path)
{
   char message[256];
   const char *suffix = path ? strstr(path, "/arcdroid") : NULL;
   if (suffix) ++suffix;
   else suffix = path ? path : "(unknown)";
   snprintf(message, sizeof(message), "%s=failed op=%s path=%s", phase,
            operation ? operation : "unknown", suffix);
   report(s, message);
}
bool arc_install_store_native_path(const char *path)
{
   if (!path || !path[0]) return false;
   /* A drive-absolute Windows path is local; a drive-relative path and any
    * URI/scheme are ambiguous for a persistent store. */
   if (path[0] == '/' || (path[0] == '\\' && path[1] == '\\')) return true;
   if (((path[0] >= 'A' && path[0] <= 'Z') ||
        (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' &&
       (path[2] == '/' || path[2] == '\\')) return true;
   return strchr(path, ':') == NULL;
}
static bool full_path(const ArcInstallStore *s, const char *leaf,
                      char path[4096])
{
   int n = snprintf(path, 4096u, "%s/%s", s->directory, leaf);
   return n >= 0 && n < 4096;
}
static bool leaf_name(const char *type, uint64_t gen, bool tmp,
                      char output[64])
{
   int n = snprintf(output, 64u, "%s.g%" PRIu64 "%s", type, gen,
                    !strcmp(type, "disc") ? (tmp ? ".hdf.tmp" : ".hdf") :
                                              (tmp ? ".tmp" : ""));
   return n >= 0 && n < 64;
}
static bool owned_generation(const char *name)
{
   if (strlen(name) >= 64u) return false;
   const char *number = NULL;
   bool disc = !strncmp(name, "disc.g", 6u);
   if (disc) number = name + 6u;
   else if (!strncmp(name, "journal.g", 9u)) number = name + 9u;
   if (!number || *number < '0' || *number > '9') return false;
   if (*number == '0' && number[1] >= '0' && number[1] <= '9') return false;
   while (*number >= '0' && *number <= '9') ++number;
   return disc ? (!strcmp(number, ".hdf") || !strcmp(number, ".hdf.tmp")) :
                 (!*number || !strcmp(number, ".tmp"));
}
static bool list_visit(const char *name, bool regular, void *opaque)
{
   Listing *list = opaque;
   if (regular && (!strcmp(name, "rwprobe.tmp") ||
                   !strcmp(name, "rwprobe2.tmp"))) return true;
   list->any = true;
   if (!regular) { list->unknown = true; return true; }
   if (!strcmp(name, "ready.a")) list->ready[0] = true;
   else if (!strcmp(name, "ready.b")) list->ready[1] = true;
   else if (!strcmp(name, "ready.tmp")) list->ready_tmp = true;
   else if (!strcmp(name, "disc.g0.hdf")) list->candidate_base = true;
   else if (!strcmp(name, "disc.g0.hdf.tmp")) list->candidate_base_tmp = true;
   else if (!strcmp(name, "journal.g0")) list->candidate_journal = true;
   else if (!strcmp(name, "journal.g0.tmp")) list->candidate_journal_tmp = true;
   else if (!owned_generation(name)) list->unknown = true;
   return true;
}
static bool read_exact(const char *path, uint8_t *data, size_t length)
{
   ArcIoFile *file = arc_io_open_file(path, false);
   if (!file) return false;
   bool okay = arc_io_file_length(file) == (int64_t)length &&
      arc_io_read_file(file, data, length) == length;
   if (!arc_io_close_file(file)) okay = false;
   return okay;
}
static bool write_checked(const char *path, const uint8_t *data, size_t length)
{
   if (!arc_io_write_all(path, data, length)) return false;
   uint8_t *copy = malloc(length ? length : 1u);
   bool good = copy && read_exact(path, copy, length) &&
               !memcmp(copy, data, length);
   free(copy);
   return good;
}
static bool write_checked_diagnostic(const char *path, const uint8_t *data,
                                     size_t length, const char **failure)
{
   if (!arc_io_write_all_diagnostic(path, data, length, failure)) return false;
   uint8_t *copy = malloc(length ? length : 1u);
   if (!copy) { if (failure) *failure = "allocate-readback"; return false; }
   ArcIoFile *file = arc_io_open_file(path, false);
   if (!file) { free(copy); if (failure) *failure = "vfs-open-read"; return false; }
   if (failure) *failure = "size";
   bool okay = arc_io_file_length(file) == (int64_t)length;
   if (okay) {
      if (failure) *failure = "read";
      okay = arc_io_read_file(file, copy, length) == length;
   }
   bool closed = arc_io_close_file(file);
   if (okay && !closed && failure) *failure = "close(read)";
   okay = okay && closed;
   if (okay) {
      if (failure) *failure = "verify-readback";
      okay = !memcmp(copy, data, length);
   }
   free(copy);
   if (okay && failure) *failure = NULL;
   return okay;
}
static void manifest_encode(const ArcInstallStore *s, uint64_t generation,
                            const char *base, const char *hash,
                            const char *journal, uint8_t bytes[MANIFEST_SIZE])
{
   memset(bytes, 0, MANIFEST_SIZE);
   memcpy(bytes, "AIHD", 4u);
   put32(bytes + 4u, STORE_VERSION);
   memcpy(bytes + 8u, s->id, 40u);
   put32(bytes + 48u, ARC_INSTALL_HD_SECTOR);
   put32(bytes + 52u, ARC_INSTALL_HD_SPT);
   put32(bytes + 56u, ARC_INSTALL_HD_HEADS);
   put32(bytes + 60u, ARC_INSTALL_HD_CYLINDERS);
   put32(bytes + 64u, ARC_INSTALL_HD_FORMAT_VERSION);
   put32(bytes + 68u, s->recipe_revision);
   put64(bytes + 72u, generation);
   memcpy(bytes + 80u, base, strlen(base));
   memcpy(bytes + 144u, hash, 64u);
   memcpy(bytes + 208u, journal, strlen(journal));
   put32(bytes + 272u, arc_crc32(bytes, 272u));
}
static bool manifest_parse(const uint8_t *bytes, size_t length,
                           const ArcInstallStore *s, Manifest *out)
{
   if (length != MANIFEST_SIZE || memcmp(bytes, "AIHD", 4u) ||
       get32(bytes + 4u) != STORE_VERSION ||
       memcmp(bytes + 8u, s->id, 40u) ||
       get32(bytes + 48u) != ARC_INSTALL_HD_SECTOR ||
       get32(bytes + 52u) != ARC_INSTALL_HD_SPT ||
       get32(bytes + 56u) != ARC_INSTALL_HD_HEADS ||
       get32(bytes + 60u) != ARC_INSTALL_HD_CYLINDERS ||
       get32(bytes + 64u) != ARC_INSTALL_HD_FORMAT_VERSION ||
       get32(bytes + 272u) != arc_crc32(bytes, 272u)) return false;
   memset(out, 0, sizeof(*out));
   memcpy(out->id, bytes + 8u, 40u);
   memcpy(out->base_hash, bytes + 144u, 64u);
   memcpy(out->base_leaf, bytes + 80u, 64u);
   memcpy(out->journal_leaf, bytes + 208u, 64u);
   out->generation = get64(bytes + 72u);
   out->revision = get32(bytes + 68u);
   char base[64], journal[64];
   if (!memchr(bytes + 80u, 0, 64u) ||
       !memchr(bytes + 208u, 0, 64u) ||
       !leaf_name("disc", out->generation, false, base) ||
       !leaf_name("journal", out->generation, false, journal) ||
       strcmp(out->base_leaf, base) || strcmp(out->journal_leaf, journal))
      return false;
   for (unsigned i = 0u; i < 64u; ++i)
      if (!((out->base_hash[i] >= '0' && out->base_hash[i] <= '9') ||
            (out->base_hash[i] >= 'a' && out->base_hash[i] <= 'f')))
         return false;
   return true;
}
static void journal_header(const ArcInstallStore *s, uint64_t generation,
                           const char *hash, uint8_t bytes[JOURNAL_HEADER])
{
   memset(bytes, 0, JOURNAL_HEADER);
   memcpy(bytes, "AIJH", 4u); put32(bytes + 4u, STORE_VERSION);
   memcpy(bytes + 8u, s->id, 40u);
   put64(bytes + 48u, generation);
   memcpy(bytes + 56u, hash, 64u);
   put32(bytes + 120u, arc_crc32(bytes, 120u));
}
static bool journal_header_valid(const ArcInstallStore *s, const uint8_t *bytes,
                                  uint64_t generation, const char *hash)
{
   return !memcmp(bytes, "AIJH", 4u) &&
      get32(bytes + 4u) == STORE_VERSION &&
      !memcmp(bytes + 8u, s->id, 40u) &&
      get64(bytes + 48u) == generation &&
      !memcmp(bytes + 56u, hash, 64u) &&
      get32(bytes + 120u) == arc_crc32(bytes, 120u);
}
static bool header_only(ArcInstallStore *s, const char *leaf, bool tmp)
{
   char path[4096];
   if (!full_path(s, leaf, path)) return false;
   ArcIoFile *file = arc_io_open_file(path, false);
   if (!file) return false;
   int64_t n = arc_io_file_length(file);
   uint8_t header[JOURNAL_HEADER];
   bool okay = tmp ? n >= 0 && n <= (int64_t)JOURNAL_HEADER :
      n == (int64_t)JOURNAL_HEADER &&
      arc_io_read_file(file, header, sizeof(header)) == sizeof(header) &&
      !memcmp(header, "AIJH", 4u) &&
      get32(header + 4u) == STORE_VERSION &&
      !memcmp(header + 8u, s->id, 40u) &&
      get64(header + 48u) == 0u &&
      get32(header + 120u) == arc_crc32(header, 120u);
   if (!arc_io_close_file(file)) okay = false;
   return okay;
}
static bool read_manifest(ArcInstallStore *s, unsigned slot, Manifest *out)
{
   char path[4096];
   uint8_t bytes[MANIFEST_SIZE];
   if (!full_path(s, slot ? "ready.b" : "ready.a", path) ||
       !read_exact(path, bytes, sizeof(bytes))) return false;
   return manifest_parse(bytes, sizeof(bytes), s, out);
}
static bool load_base(ArcInstallStore *s, const Manifest *m)
{
   char path[4096], hash[65];
   if (!full_path(s, m->base_leaf, path) ||
       !read_exact(path, s->image, ARC_INSTALL_HD_BYTES)) return false;
   arc_sha256_hex(s->image, ARC_INSTALL_HD_BYTES, hash);
   return !strcmp(hash, m->base_hash);
}
static bool replay(ArcInstallStore *s, const Manifest *m, bool writable,
                   bool report_tail)
{
   char path[4096];
   uint8_t header[JOURNAL_HEADER];
   if (!full_path(s, m->journal_leaf, path)) return false;
   ArcIoFile *file = arc_io_open_file(path, false);
   if (!file) return false;
   int64_t length = arc_io_file_length(file);
   bool good = length >= (int64_t)JOURNAL_HEADER &&
      length <= (int64_t)(JOURNAL_CAP + RECORD_HEADER +
                           MAX_COMMAND_SECTORS * ARC_INSTALL_HD_SECTOR +
                           RECORD_TRAILER) &&
      arc_io_read_file(file, header, sizeof(header)) == sizeof(header) &&
      journal_header_valid(s, header, m->generation, m->base_hash);
   size_t at = JOURNAL_HEADER, complete = JOURNAL_HEADER;
   uint64_t sequence = 0u;
   uint8_t *payload = malloc(MAX_COMMAND_SECTORS * ARC_INSTALL_HD_SECTOR);
   if (!payload) good = false;
   while (good && at < (size_t)length) {
      if ((size_t)length - at < RECORD_HEADER) break;
      uint8_t r[RECORD_HEADER];
      if (arc_io_read_file(file, r, sizeof(r)) != sizeof(r)) { good = false; break; }
      at += sizeof(r);
      uint32_t lba = get32(r + 64u), count = get32(r + 68u);
      uint32_t payload_length = get32(r + 72u);
      if (memcmp(r, "AIJR", 4u) || get32(r + 4u) != STORE_VERSION ||
          memcmp(r + 8u, s->id, 40u) ||
          get64(r + 48u) != m->generation ||
          sequence == UINT64_MAX || get64(r + 56u) != sequence + 1u ||
          !count || count > MAX_COMMAND_SECTORS ||
          lba >= ARC_INSTALL_HD_SECTORS ||
          count > ARC_INSTALL_HD_SECTORS - lba ||
          payload_length != count * ARC_INSTALL_HD_SECTOR ||
          get32(r + 76u) != arc_crc32(r, 76u)) { good = false; break; }
      if ((size_t)length - at < payload_length + RECORD_TRAILER) break;
      uint8_t trailer[RECORD_TRAILER];
      if (arc_io_read_file(file, payload, payload_length) != payload_length ||
          arc_io_read_file(file, trailer, sizeof(trailer)) != sizeof(trailer)) {
         good = false; break;
      }
      at += payload_length + sizeof(trailer);
      if (get64(trailer) != sequence + 1u ||
          get32(trailer + 8u) != arc_crc32(payload, payload_length) ||
          memcmp(trailer + 12u, "AICM", 4u)) { good = false; break; }
      memcpy(s->image + (size_t)lba * ARC_INSTALL_HD_SECTOR,
             payload, payload_length);
      ++sequence;
      complete = at;
   }
   free(payload);
   if (!arc_io_close_file(file)) good = false;
   if (!good) return false;
   s->incomplete_tail = complete != (size_t)length;
   if (complete != (size_t)length) {
      if (report_tail)
         report(s, writable ? "journal-incomplete-truncate" :
                              "journal-incomplete-read-only");
      if (writable) {
         ArcIoFile *update = arc_io_open_file(path, true);
         good = update && arc_io_truncate_file(update, (int64_t)complete) &&
                         arc_io_flush_file(update);
         if (update && !arc_io_close_file(update)) good = false;
         if (!good) return false;
      }
   }
   s->journal_length = complete;
   s->sequence = sequence;
   return true;
}
static bool remove_if_present(ArcInstallStore *s, const char *leaf)
{
   bool present = false;
   char path[4096];
   return full_path(s, leaf, path) &&
      arc_io_directory_contains(s->directory, leaf, &present) &&
      (!present || arc_io_remove(path));
}
static bool remove_probe_leftover(ArcInstallStore *s, const char *leaf)
{
   bool present = false;
   char path[4096];
   const char *failure = NULL;
   if (!full_path(s, leaf, path)) {
      report_vfs_failure(s, "store-write-probe", "path-build", leaf);
      return false;
   }
   if (!arc_io_directory_contains_diagnostic(s->directory, leaf, &present,
                                              &failure)) {
      report_vfs_failure(s, "store-write-probe", failure, leaf);
      return false;
   }
   if (present && !arc_io_remove(path)) {
      report_vfs_failure(s, "store-write-probe", "remove", leaf);
      return false;
   }
   return true;
}
/* These names belong exclusively to the store.  A failed probe leaves only
 * owned temporaries, which never affect classification on the next load. */
static bool write_probe(ArcInstallStore *s)
{
   char first[4096], second[4096];
   static const uint8_t bytes[] = {'A', 'I', 'H', 'D'};
   const char *failure = NULL;
   if (!full_path(s, "rwprobe.tmp", first) ||
       !full_path(s, "rwprobe2.tmp", second)) {
      report_vfs_failure(s, "store-write-probe", "path-build", "rwprobe.tmp");
      return false;
   }
   if (!remove_probe_leftover(s, "rwprobe.tmp") ||
       !remove_probe_leftover(s, "rwprobe2.tmp")) return false;
   if (!arc_io_write_all_diagnostic(first, bytes, sizeof(bytes), &failure)) {
      report_vfs_failure(s, "store-write-probe", failure, "rwprobe.tmp");
      return false;
   }
   if (!arc_io_rename(first, second)) {
      report_vfs_failure(s, "store-write-probe", "rename",
                         "rwprobe.tmp->rwprobe2.tmp");
      return false;
   }
   if (!arc_io_remove(second)) {
      report_vfs_failure(s, "store-write-probe", "remove", "rwprobe2.tmp");
      return false;
   }
   report(s, "store-write-probe=passed");
   return true;
}
typedef struct {
   const Manifest *manifests;
   const Listing *listing;
   char **names;
   size_t count, capacity;
} Cleanup;
static bool cleanup_visit(const char *name, bool regular, void *opaque)
{
   Cleanup *c = opaque;
   if (!regular || (strcmp(name, "ready.tmp") &&
                    !owned_generation(name))) return true;
   for (unsigned slot = 0u; slot < 2u; ++slot)
      if (c->listing->ready[slot] &&
          (!strcmp(name, c->manifests[slot].base_leaf) ||
           !strcmp(name, c->manifests[slot].journal_leaf))) return true;
   if (c->count == c->capacity) {
      size_t next = c->capacity ? c->capacity * 2u : 8u;
      if (next < c->capacity || next > SIZE_MAX / sizeof(*c->names))
         return false;
      char **grown = realloc(c->names, next * sizeof(*c->names));
      if (!grown) return false;
      c->names = grown; c->capacity = next;
   }
   size_t length = strlen(name) + 1u;
   c->names[c->count] = malloc(length);
   if (!c->names[c->count]) return false;
   memcpy(c->names[c->count], name, length);
   ++c->count;
   return true;
}
static void cleanup_unused(ArcInstallStore *s, const Manifest manifests[2],
                           const Listing *listing)
{
   Cleanup c = {0};
   c.manifests = manifests; c.listing = listing;
   bool good = arc_io_list_directory(s->directory, cleanup_visit, &c);
   unsigned removed = 0u;
   if (good) for (size_t i = 0u; i < c.count; ++i) {
      if (remove_if_present(s, c.names[i])) ++removed;
      else good = false;
   }
   for (size_t i = 0u; i < c.count; ++i) free(c.names[i]);
   free(c.names);
   if (removed) report(s, "store-cleanup=owned-orphans-removed");
   if (!good) report(s, "store-cleanup=failed");
}
static bool create_base(ArcInstallStore *s)
{
   char tmp[4096], final[4096], hash[65];
   const char *failure = NULL;
   if (!arc_install_hd_format(s->image, ARC_INSTALL_HD_BYTES)) {
      report_vfs_failure(s, "store-create", "format", "disc.g0.hdf.tmp");
      return false;
   }
   if (!full_path(s, "disc.g0.hdf.tmp", tmp) ||
       !full_path(s, "disc.g0.hdf", final)) {
      report_vfs_failure(s, "store-create", "path-build", "disc.g0.hdf.tmp");
      return false;
   }
   if (!write_checked_diagnostic(tmp, s->image, ARC_INSTALL_HD_BYTES, &failure)) {
      report_vfs_failure(s, "store-create", failure, "disc.g0.hdf.tmp");
      return false;
   }
   arc_sha256_hex(s->image, ARC_INSTALL_HD_BYTES, hash);
   if (!arc_io_rename(tmp, final)) {
      report_vfs_failure(s, "store-create", "rename",
                         "disc.g0.hdf.tmp->disc.g0.hdf");
      return false;
   }
   strcpy(s->base_leaf, "disc.g0.hdf");
   strcpy(s->base_hash, hash);
   s->generation = 0u;
   s->state = ARC_INSTALL_STORE_UNPUBLISHED;
   report(s, "store-state=unpublished candidate=disc.g0.hdf");
   return true;
}
static bool journal_create(ArcInstallStore *s, uint64_t gen,
                           const char *hash)
{
   char tmp_leaf[64], final_leaf[64], tmp[4096], final[4096];
   uint8_t header[JOURNAL_HEADER], verify[JOURNAL_HEADER];
   if (!leaf_name("journal", gen, true, tmp_leaf) ||
       !leaf_name("journal", gen, false, final_leaf) ||
       !full_path(s, tmp_leaf, tmp) || !full_path(s, final_leaf, final))
      return false;
   journal_header(s, gen, hash, header);
   if (!write_checked(tmp, header, sizeof(header)) ||
       !read_exact(tmp, verify, sizeof(verify)) ||
       !journal_header_valid(s, verify, gen, hash) ||
       !arc_io_rename(tmp, final)) return false;
   return true;
}
static bool manifest_publish(ArcInstallStore *s, uint64_t gen,
                             const char *base, const char *hash,
                             const char *journal, unsigned slot)
{
   uint8_t bytes[MANIFEST_SIZE], checked[MANIFEST_SIZE];
   char tmp[4096], destination[4096];
   manifest_encode(s, gen, base, hash, journal, bytes);
   if (!full_path(s, "ready.tmp", tmp) ||
       !full_path(s, slot ? "ready.b" : "ready.a", destination) ||
       !write_checked(tmp, bytes, sizeof(bytes)) ||
       !read_exact(tmp, checked, sizeof(checked))) return false;
   Manifest parsed;
   if (!manifest_parse(checked, sizeof(checked), s, &parsed) ||
       !arc_io_rename(tmp, destination)) return false;
   return true;
}
static bool compact(ArcInstallStore *s)
{
   if (s->generation == UINT64_MAX) return false;
   uint64_t next = s->generation + 1u;
   char base_leaf[64], base_tmp_leaf[64], journal_leaf[64];
   char journal_tmp_leaf[64];
   char base[4096], tmp[4096], hash[65];
   if (!leaf_name("disc", next, false, base_leaf) ||
       !leaf_name("disc", next, true, base_tmp_leaf) ||
       !leaf_name("journal", next, false, journal_leaf) ||
       !leaf_name("journal", next, true, journal_tmp_leaf) ||
       !full_path(s, base_leaf, base) || !full_path(s, base_tmp_leaf, tmp) ||
       !remove_if_present(s, base_leaf) ||
       !remove_if_present(s, base_tmp_leaf) ||
       !remove_if_present(s, journal_leaf) ||
       !remove_if_present(s, journal_tmp_leaf) ||
       !remove_if_present(s, "ready.tmp") ||
       !write_checked(tmp, s->image, ARC_INSTALL_HD_BYTES)) return false;
   arc_sha256_hex(s->image, ARC_INSTALL_HD_BYTES, hash);
   if (!arc_io_rename(tmp, base) || !journal_create(s, next, hash)) return false;
   char old_base[64], old_journal[64];
   strcpy(old_base, s->base_leaf);
   strcpy(old_journal, s->journal_leaf);
   unsigned inactive = 1u - s->selected_slot;
   if (!remove_if_present(s, inactive ? "ready.b" : "ready.a"))
      return false;
   {
      char message[96];
      snprintf(message, sizeof(message),
               "store-compaction=manifest-publish-start generation=%" PRIu64,
               next);
      report(s, message);
   }
   if (!manifest_publish(s, next, base_leaf, hash, journal_leaf, inactive))
      return false;
   {
      char message[96];
      snprintf(message, sizeof(message),
               "store-compaction=manifest-renamed generation=%" PRIu64,
               next);
      report(s, message);
   }
   s->generation = next;
   s->selected_slot = inactive;
   s->sequence = 0u;
   s->journal_length = JOURNAL_HEADER;
   strcpy(s->base_leaf, base_leaf);
   strcpy(s->base_hash, hash);
   strcpy(s->journal_leaf, journal_leaf);
   report(s, "store-compaction=committed");
   if (!remove_if_present(s, old_base) ||
       !remove_if_present(s, old_journal))
      report(s, "store-compaction=old-generation-cleanup-failed");
   return true;
}
ArcInstallStore *arc_install_store_open(const char *save_dir,
   const ArcInstallPlan *plan, bool create_candidate, bool writable,
   ArcInstallStoreLog log, void *log_opaque, char *error, size_t error_size)
{
   if (!save_dir || !plan || !plan->profile || !plan->variant ||
       !plan->storage_id[0]) return NULL;
   ArcInstallStore *s = calloc(1u, sizeof(*s));
   if (!s) return NULL;
   s->image = malloc(ARC_INSTALL_HD_BYTES);
   if (!s->image) { free(s); return NULL; }
   s->log = log; s->log_opaque = log_opaque;
   s->recipe_revision = plan->profile->revision;
   strcpy(s->id, plan->storage_id);
   s->writable = writable;
   int n = snprintf(s->directory, sizeof(s->directory),
                    "%s/arcdroid/install/%s", save_dir, s->id);
   if (n < 0 || (size_t)n >= sizeof(s->directory)) goto damaged;
   Listing listing = {0};
   bool listed = arc_io_list_directory(s->directory, list_visit, &listing);
   if (!listed) {
      /* A present but unreadable directory is damage, not a new store. */
      if (arc_io_existing_directory(s->directory)) goto damaged;
      if (!create_candidate || !writable) {
         s->state = ARC_INSTALL_STORE_NEW;
         report(s, "store-state=new read-only");
         return s;
      }
      char failed_path[4096];
      const char *failure = NULL;
      if (!arc_io_make_path_diagnostic(s->directory, failed_path,
                                       sizeof(failed_path), &failure)) {
         report_vfs_failure(s, "store-create", failure,
                            failed_path[0] ? failed_path : s->directory);
         s->state = ARC_INSTALL_STORE_NEW;
         s->writable = false;
         report(s, "store-mode=read-only reason=directory-create-failed");
         set_error(error, error_size,
            "Persistent install saves need a native writable save directory; choose Install Saves: Temporary to use RAM.");
         return s;
      }
      listed = arc_io_list_directory_diagnostic(s->directory, list_visit,
                                                 &listing, &failure);
      if (!listed) {
         report_vfs_failure(s, "store-create", failure, s->directory);
         goto damaged;
      }
   }
   if (listing.unknown) goto damaged;
   if (listing.ready[0] || listing.ready[1]) {
      Manifest m[2] = {{0}};
      for (unsigned i = 0u; i < 2u; ++i)
         if (listing.ready[i] && !read_manifest(s, i, &m[i])) goto damaged;
      if (listing.ready[0] && listing.ready[1] &&
          m[0].generation == m[1].generation) goto damaged;
      unsigned selected = !listing.ready[0] ? 1u :
         !listing.ready[1] ? 0u :
         m[1].generation > m[0].generation ? 1u : 0u;
      const Manifest *chosen = &m[selected];
      bool requested_writable = writable;
      if (!arc_install_revision_supported(plan->profile,
                                           chosen->revision)) {
         set_error(error, error_size,
                   "Saved installation recipe revision is unsupported; preserve the install store.");
         goto damaged;
      }
      s->selected_slot = selected;
      s->generation = chosen->generation;
      strcpy(s->base_leaf, chosen->base_leaf);
      strcpy(s->journal_leaf, chosen->journal_leaf);
      strcpy(s->base_hash, chosen->base_hash);
      /* Validate the committed view before any host mutation, including the
       * probe. A damaged READY store must remain byte-for-byte untouched. */
      if (!load_base(s, chosen) ||
          !replay(s, chosen, false, !writable)) goto damaged;
      if (writable && !write_probe(s)) {
         s->writable = false;
         writable = false;
         report(s, "store-mode=read-only reason=write-probe-failed");
      }
      if (writable && !replay(s, chosen, true, true)) goto damaged;
      if (requested_writable && !writable && s->incomplete_tail)
         report(s, "journal-incomplete-read-only");
      s->state = ARC_INSTALL_STORE_READY;
      report(s, writable ? "store-state=ready writable" :
                           "store-state=ready read-only");
      if (writable) cleanup_unused(s, m, &listing);
      if (writable && s->journal_length > COMPACT_THRESHOLD && !compact(s))
         goto damaged;
      return s;
   }
   if (!listing.any) s->state = ARC_INSTALL_STORE_NEW;
   else if (listing.candidate_base || listing.candidate_base_tmp ||
            listing.candidate_journal || listing.candidate_journal_tmp ||
            listing.ready_tmp) {
      if (listing.candidate_journal && !header_only(s, "journal.g0", false))
         goto damaged;
      if (listing.candidate_journal_tmp &&
          !header_only(s, "journal.g0.tmp", true)) goto damaged;
      s->state = ARC_INSTALL_STORE_UNPUBLISHED;
   } else goto damaged;
   if (create_candidate && writable && !write_probe(s)) {
      s->writable = false;
      writable = false;
      report(s, "store-mode=read-only reason=write-probe-failed");
      set_error(error, error_size,
         "Persistent install saves need a native writable save directory; choose Install Saves: Temporary to use RAM.");
   }
   if (!create_candidate || !writable) {
      report(s, s->state == ARC_INSTALL_STORE_NEW ? "store-state=new read-only" :
                                                "store-state=unpublished read-only");
      return s;
   }
   if (s->state == ARC_INSTALL_STORE_UNPUBLISHED) {
      static const char *const owned[] = {
         "disc.g0.hdf", "disc.g0.hdf.tmp", "journal.g0",
         "journal.g0.tmp", "ready.tmp"};
      for (unsigned i = 0u; i < 5u; ++i)
         if (!remove_if_present(s, owned[i])) goto damaged;
      report(s, "store-state=unpublished cleaned");
   }
   if (!create_base(s)) goto damaged;
   return s;
damaged:
   s->state = ARC_INSTALL_STORE_DAMAGED;
   s->writable = false;
   report(s, "store-state=damaged");
   if (!error || !error[0]) set_error(error, error_size,
      "Saved installation is damaged; preserve the install directory and restore a backup.");
   return s;
}
void arc_install_store_close(ArcInstallStore *s)
{ if (s) { free(s->image); free(s); } }
ArcInstallStoreState arc_install_store_state(const ArcInstallStore *s)
{ return s ? s->state : ARC_INSTALL_STORE_DAMAGED; }
const uint8_t *arc_install_store_image(const ArcInstallStore *s)
{ return s ? s->image : NULL; }
const char *arc_install_store_id(const ArcInstallStore *s)
{ return s ? s->id : NULL; }
bool arc_install_store_writable(const ArcInstallStore *s)
{ return s && s->writable && !s->failed; }
bool arc_install_store_probe_writable(ArcInstallStore *s)
{
   if (!s || s->state != ARC_INSTALL_STORE_READY) return false;
   return write_probe(s);
}
bool arc_install_store_read(void *opaque, uint32_t lba, uint32_t count,
                            uint8_t *data)
{
   ArcInstallStore *s = opaque;
   if (!s || !data || !count || lba >= ARC_INSTALL_HD_SECTORS ||
       count > ARC_INSTALL_HD_SECTORS - lba ||
       (s->state != ARC_INSTALL_STORE_READY &&
        s->state != ARC_INSTALL_STORE_UNPUBLISHED)) return false;
   memcpy(data, s->image + (size_t)lba * ARC_INSTALL_HD_SECTOR,
          (size_t)count * ARC_INSTALL_HD_SECTOR);
   return true;
}
void arc_install_store_failed(void *opaque, const char *reason)
{
   ArcInstallStore *s = opaque;
   if (s) {
      s->failed = true;
      s->writable = false;
      report(s, reason ? reason : "ide-write-failed");
   }
}
bool arc_install_store_write(void *opaque, uint32_t lba, uint32_t count,
                             const uint8_t *data)
{
   ArcInstallStore *s = opaque;
   if (!arc_install_store_writable(s) || !data || !count ||
       count > MAX_COMMAND_SECTORS || lba >= ARC_INSTALL_HD_SECTORS ||
       count > ARC_INSTALL_HD_SECTORS - lba) return false;
   size_t payload_length = (size_t)count * ARC_INSTALL_HD_SECTOR;
   char path[4096];
   bool good = false;
   if (s->state == ARC_INSTALL_STORE_UNPUBLISHED) {
      if (!full_path(s, s->base_leaf, path)) goto failed;
      ArcIoFile *file = arc_io_open_file(path, true);
      good = file && arc_io_seek_file(file,
         (int64_t)lba * ARC_INSTALL_HD_SECTOR, RETRO_VFS_SEEK_POSITION_START) >= 0 &&
         arc_io_write_file(file, data, payload_length) == payload_length &&
         arc_io_flush_file(file);
      if (file && !arc_io_close_file(file)) good = false;
      if (!good) goto failed;
   } else if (s->state == ARC_INSTALL_STORE_READY) {
      if (s->sequence == UINT64_MAX ||
          s->journal_length > JOURNAL_CAP ||
          RECORD_HEADER + payload_length + RECORD_TRAILER >
            JOURNAL_CAP - s->journal_length) {
         arc_install_store_failed(s, "store-write-failed=journal-cap");
         return false;
      }
      uint8_t header[RECORD_HEADER] = {0}, trailer[RECORD_TRAILER] = {0};
      memcpy(header, "AIJR", 4u); put32(header + 4u, STORE_VERSION);
      memcpy(header + 8u, s->id, 40u);
      put64(header + 48u, s->generation);
      put64(header + 56u, s->sequence + 1u);
      put32(header + 64u, lba); put32(header + 68u, count);
      put32(header + 72u, (uint32_t)payload_length);
      put32(header + 76u, arc_crc32(header, 76u));
      put64(trailer, s->sequence + 1u);
      put32(trailer + 8u, arc_crc32(data, payload_length));
      memcpy(trailer + 12u, "AICM", 4u);
      if (!full_path(s, s->journal_leaf, path)) goto failed;
      ArcIoFile *file = arc_io_open_file(path, true);
      good = file && arc_io_seek_file(file, (int64_t)s->journal_length,
                 RETRO_VFS_SEEK_POSITION_START) >= 0 &&
         arc_io_write_file(file, header, sizeof(header)) == sizeof(header) &&
         arc_io_write_file(file, data, payload_length) == payload_length &&
         arc_io_write_file(file, trailer, sizeof(trailer)) == sizeof(trailer) &&
         arc_io_flush_file(file);
      if (file && !arc_io_close_file(file)) good = false;
      if (!good) goto failed;
      ++s->sequence;
      s->journal_length += sizeof(header) + payload_length + sizeof(trailer);
      char message[128];
      snprintf(message, sizeof(message), "journal-commit lba=%" PRIu32
                " count=%" PRIu32 " sequence=%" PRIu64,
                lba, count, s->sequence);
      report(s, message);
   } else return false;
   memcpy(s->image + (size_t)lba * ARC_INSTALL_HD_SECTOR,
          data, payload_length);
   if (s->state == ARC_INSTALL_STORE_UNPUBLISHED) {
      char message[96];
      snprintf(message, sizeof(message), "candidate-write lba=%" PRIu32
               " count=%" PRIu32, lba, count);
      report(s, message);
   }
   return true;
failed:
   arc_install_store_failed(s, "store-write-failed=io");
   return false;
}
bool arc_install_store_publish(ArcInstallStore *s,
   char *error, size_t error_size)
{
   if (!s || s->state != ARC_INSTALL_STORE_UNPUBLISHED ||
       !arc_install_store_writable(s)) return false;
   char path[4096], hash[65];
   uint8_t *readback = malloc(ARC_INSTALL_HD_BYTES);
   bool good = readback && full_path(s, s->base_leaf, path) &&
      read_exact(path, readback, ARC_INSTALL_HD_BYTES) &&
      !memcmp(readback, s->image, ARC_INSTALL_HD_BYTES);
   if (good) arc_sha256_hex(readback, ARC_INSTALL_HD_BYTES, hash);
   free(readback);
   if (!good || !journal_create(s, 0u, hash) ||
       !manifest_publish(s, 0u, "disc.g0.hdf", hash, "journal.g0", 0u)) {
      set_error(error, error_size, "Saved installation publication failed; retry installation.");
      report(s, "store-publication=failed");
      return false;
   }
   strcpy(s->base_hash, hash);
   strcpy(s->journal_leaf, "journal.g0");
   s->selected_slot = 0u;
   s->sequence = 0u;
   s->journal_length = JOURNAL_HEADER;
   s->state = ARC_INSTALL_STORE_READY;
   report(s, "store-publication=committed generation=0");
   return true;
}
