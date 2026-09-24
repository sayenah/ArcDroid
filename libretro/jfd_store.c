/* ArcDroid native JFD change store. Copyright 2026 ArcDroid contributors.
 * GPL-2.0-only. Complete, checksummed two-slot snapshots; source JFDs are
 * never opened for writing. A rename into the inactive slot is the commit. */
#include "jfd_store.h"
#include "arc_io.h"
#include "arcdroid_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORE_CAP (9u * 1024u * 1024u)
#define HEADER_SIZE 80u
#define RECORD_SIZE 8u
#define CHECKSUM_SIZE 4u

typedef struct {
   char directory[4096];
   char slot[2][4096];
   char candidate[4096];
   uint8_t sha1[20];
   uint8_t sha256[32];
   unsigned image_number;
   unsigned active;
} ArcJfdStore;

static bool store_eligible(const ArcJfdImage *image, size_t index)
{
   return image->overlay && image->overlay->eligible &&
          index < image->sector_count && image->overlay->eligible[index];
}

static uint16_t get16(const uint8_t *p)
{ return (uint16_t)(p[0] | ((uint16_t)p[1] << 8u)); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
         ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u); }
static uint64_t get64(const uint8_t *p)
{ return (uint64_t)get32(p) | ((uint64_t)get32(p + 4u) << 32u); }
static void put16(uint8_t *p, uint16_t n)
{ p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8u); }
static void put32(uint8_t *p, uint32_t n)
{ for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(n >> (8u * i)); }
static void put64(uint8_t *p, uint64_t n)
{ put32(p, (uint32_t)n); put32(p + 4u, (uint32_t)(n >> 32u)); }
static bool hex_bytes(const char *hex, uint8_t *output, size_t count)
{
   for (size_t i = 0u; i < count; ++i)
   {
      unsigned a, b;
      char pair[3] = {hex[i * 2u], hex[i * 2u + 1u], 0};
      if (sscanf(pair, "%1x%1x", &a, &b) != 2) return false;
      output[i] = (uint8_t)((a << 4u) | b);
   }
   return true;
}
static bool sector_key(const ArcJfdImage *image, size_t index,
                       uint16_t *track_side, uint16_t *ordinal)
{
   for (unsigned t = 0u; t < image->track_count; ++t)
   {
      const ArcJfdTrack *track = &image->tracks[t];
      if (index >= track->first_sector &&
          index - track->first_sector < track->sector_count)
      { *track_side = (uint16_t)t;
        *ordinal = (uint16_t)(index - track->first_sector); return true; }
   }
   return false;
}
static bool snapshot_parse(const ArcJfdImage *image, const ArcJfdStore *store,
                           const uint8_t *bytes, size_t length,
                           uint8_t ***replacement, uint64_t *generation,
                           unsigned *record_count, const char **reason)
{
   *replacement = NULL;
   if (length < HEADER_SIZE + CHECKSUM_SIZE || length > STORE_CAP ||
       memcmp(bytes, "AJWO", 4u))
   { *reason = "overlay-invalid"; return false; }
   if (get32(bytes + 4u) != 1u)
   { *reason = "overlay-invalid"; return false; }
   if (get64(bytes + 8u) != image->decoded_size ||
       memcmp(bytes + 16u, store->sha1, 20u) ||
       memcmp(bytes + 36u, store->sha256, 32u))
   { *reason = "overlay-mismatch"; return false; }
   uint64_t gen = get64(bytes + 68u);
   uint32_t count = get32(bytes + 76u);
   if (!gen || count > image->sector_count ||
       get32(bytes + length - 4u) != arc_crc32(bytes, length - 4u))
   { *reason = "overlay-invalid"; return false; }
   uint8_t **records = calloc(image->sector_count ? image->sector_count : 1u,
                              sizeof(*records));
   if (!records)
   { *reason = "overlay-unreadable"; return false; }
   size_t cursor = HEADER_SIZE, data_sum = 0u, eligible_sum = 0u;
   uint32_t last_key = 0u;
   for (size_t i = 0u; i < image->sector_count; ++i)
      if (store_eligible(image, i))
         eligible_sum += image->sectors[i].size;
   bool valid = true;
   for (uint32_t i = 0u; i < count && valid; ++i)
   {
      if (cursor > length - CHECKSUM_SIZE ||
          length - CHECKSUM_SIZE - cursor < RECORD_SIZE) { valid = false; break; }
      unsigned track_side = get16(bytes + cursor);
      unsigned ordinal = get16(bytes + cursor + 2u);
      uint32_t key = (track_side << 16u) | ordinal;
      if ((i && key <= last_key) || track_side >= image->track_count ||
          ordinal >= image->tracks[track_side].sector_count)
      { valid = false; break; }
      size_t index = (size_t)image->tracks[track_side].first_sector + ordinal;
      const ArcJfdSector *sector = &image->sectors[index];
      unsigned size = get16(bytes + cursor + 6u);
      if (!store_eligible(image, index) ||
          bytes[cursor + 4u] != sector->id ||
          bytes[cursor + 5u] != sector->density || size != sector->size ||
          size > length - CHECKSUM_SIZE - cursor - RECORD_SIZE ||
          size > eligible_sum || data_sum > eligible_sum - size)
      { valid = false; break; }
      records[index] = malloc(size);
      if (!records[index]) { *reason = "overlay-unreadable"; valid = false; break; }
      memcpy(records[index], bytes + cursor + RECORD_SIZE, size);
      cursor += RECORD_SIZE + size;
      data_sum += size;
      last_key = key;
   }
   if (cursor != length - CHECKSUM_SIZE) valid = false;
   if (!valid)
   {
      for (size_t i = 0u; i < image->sector_count; ++i) free(records[i]);
      free(records);
      if (!*reason) *reason = "overlay-invalid";
      return false;
   }
   *replacement = records;
   *generation = gen;
   *record_count = count;
   return true;
}
static void records_free(uint8_t **records, size_t count)
{ if (records) { for (size_t i = 0u; i < count; ++i) free(records[i]); free(records); } }
static bool slot_present(const ArcJfdStore *store, unsigned slot, bool *present)
{
   const char *leaf = strrchr(store->slot[slot], '/');
   return leaf && arc_io_directory_contains(store->directory, leaf + 1u, present);
}
static bool snapshot_build(const ArcJfdImage *image, const ArcJfdStore *store,
                           size_t changed, const uint8_t *data,
                           uint8_t **output, size_t *length)
{
   size_t count = 0u, bytes = HEADER_SIZE + CHECKSUM_SIZE;
   for (size_t i = 0u; i < image->sector_count; ++i)
      if ((image->overlay->data && image->overlay->data[i]) || i == changed)
      {
         if (!store_eligible(image, i) ||
             bytes > STORE_CAP - RECORD_SIZE - image->sectors[i].size) return false;
         bytes += RECORD_SIZE + image->sectors[i].size; ++count;
      }
   uint8_t *snapshot = calloc(bytes, 1u);
   if (!snapshot) return false;
   memcpy(snapshot, "AJWO", 4u); put32(snapshot + 4u, 1u);
   put64(snapshot + 8u, image->decoded_size);
   memcpy(snapshot + 16u, store->sha1, 20u);
   memcpy(snapshot + 36u, store->sha256, 32u);
   put64(snapshot + 68u, image->overlay->generation + 1u);
   put32(snapshot + 76u, (uint32_t)count);
   size_t cursor = HEADER_SIZE;
   for (size_t i = 0u; i < image->sector_count; ++i)
   {
      const uint8_t *value = i == changed ? data :
         image->overlay->data ? image->overlay->data[i] : NULL;
      if (!value) continue;
      uint16_t track_side, ordinal;
      if (!sector_key(image, i, &track_side, &ordinal)) { free(snapshot); return false; }
      const ArcJfdSector *sector = &image->sectors[i];
      put16(snapshot + cursor, track_side);
      put16(snapshot + cursor + 2u, ordinal);
      snapshot[cursor + 4u] = sector->id;
      snapshot[cursor + 5u] = sector->density;
      put16(snapshot + cursor + 6u, sector->size);
      memcpy(snapshot + cursor + RECORD_SIZE, value, sector->size);
      cursor += RECORD_SIZE + sector->size;
   }
   put32(snapshot + bytes - CHECKSUM_SIZE, arc_crc32(snapshot, bytes - CHECKSUM_SIZE));
   *output = snapshot; *length = bytes;
   return true;
}
static bool store_commit(ArcJfdImage *image, size_t index, const uint8_t *data)
{
   ArcJfdOverlay *overlay = image->overlay;
   if (!overlay || !overlay->writable || overlay->failed || overlay->damaged)
      return false;
   ArcJfdStore *store = overlay->store;
   uint8_t *snapshot = NULL, *readback = NULL, **checked = NULL;
   size_t length = 0u, read_size = 0u;
   uint64_t checked_generation = 0u;
   unsigned checked_count = 0u;
   const char *reason = NULL;
   unsigned inactive = 1u - store->active;
   bool present = false;
   bool okay = store_eligible(image, index) &&
      overlay->generation != UINT64_MAX &&
      snapshot_build(image, store, index, data, &snapshot, &length) &&
      slot_present(store, inactive, &present);
   if (okay)
   {
      /* A stale candidate is never a generation. Remove it before opening. */
      bool candidate_present = false;
      const char *leaf = strrchr(store->candidate, '/');
      okay = leaf && arc_io_directory_contains(store->directory, leaf + 1u,
                                                &candidate_present) &&
         (!candidate_present || arc_io_remove(store->candidate));
   }
   if (okay) okay = arc_io_write_all(store->candidate, snapshot, length);
   if (okay) okay = arc_io_read_all_limit(store->candidate,
                    (void **)&readback, &read_size, STORE_CAP, NULL) &&
      read_size == length && !memcmp(readback, snapshot, length) &&
      snapshot_parse(image, store, readback, read_size, &checked,
                     &checked_generation, &checked_count, &reason) &&
      checked_generation == overlay->generation + 1u;
   if (okay && present) okay = arc_io_remove(store->slot[inactive]);
   if (okay) okay = arc_io_rename(store->candidate, store->slot[inactive]);
   if (okay)
   {
      uint8_t **old = overlay->data;
      overlay->data = checked;
      overlay->generation = checked_generation;
      overlay->records = checked_count;
      store->active = inactive;
      records_free(old, image->sector_count);
      checked = NULL;
      arc_jfd_store_log_commit(store->image_number, checked_generation, index);
   }
   else
   {
      overlay->writable = false;
      overlay->failed = true;
      arc_jfd_store_notice("Disc changes can no longer be saved this session.", true);
   }
   free(snapshot); free(readback); records_free(checked, image->sector_count);
   return okay;
}

bool arc_jfd_store_load(ArcJfdImage *image, const char *save_directory,
                        bool writes_enabled, bool show_notice, unsigned image_number,
                        const char **reason)
{
   *reason = "no-flag";
   if (!image || !image->decoded || !save_directory) return false;
   ArcJfdOverlay *overlay = calloc(1u, sizeof(*overlay));
   ArcJfdStore *store = calloc(1u, sizeof(*store));
   if (!overlay || !store) { free(overlay); free(store); return false; }
   image->overlay = overlay; overlay->store = store;
   overlay->eligible = calloc(image->sector_count ? image->sector_count : 1u, 1u);
   if (!overlay->eligible) return false;
   for (size_t i = 0u; i < image->sector_count; ++i)
      overlay->eligible[i] = arc_jfd_sector_write_eligible(image, i);
   store->image_number = image_number;
   char sha1_hex[41], sha256_hex[65];
   arc_sha1_hex(image->decoded, image->decoded_size, sha1_hex);
   arc_sha256_hex(image->decoded, image->decoded_size, sha256_hex);
   if (!hex_bytes(sha1_hex, store->sha1, 20u) ||
       !hex_bytes(sha256_hex, store->sha256, 32u)) return false;
   int n = snprintf(store->directory, sizeof(store->directory),
                    "%s/arcdroid/jfd", save_directory);
   if (n < 0 || (size_t)n >= sizeof(store->directory) ||
       !arc_io_make_path(store->directory))
   { *reason = "overlay-unreadable"; return false; }
   n = snprintf(store->slot[0], sizeof(store->slot[0]),
                "%s/%s.a", store->directory, sha1_hex);
   if (n < 0 || (size_t)n >= sizeof(store->slot[0])) return false;
   n = snprintf(store->slot[1], sizeof(store->slot[1]),
                "%s/%s.b", store->directory, sha1_hex);
   if (n < 0 || (size_t)n >= sizeof(store->slot[1])) return false;
   n = snprintf(store->candidate, sizeof(store->candidate),
                "%s/%s.tmp", store->directory, sha1_hex);
   if (n < 0 || (size_t)n >= sizeof(store->candidate)) return false;
   bool present[2] = {false, false}, valid[2] = {false, false};
   uint8_t **records[2] = {NULL, NULL};
   uint64_t generation[2] = {0u, 0u};
   unsigned count[2] = {0u, 0u};
   bool damaged = false;
   for (unsigned i = 0u; i < 2u; ++i)
   {
      const char *slot_reason = NULL;
      if (!slot_present(store, i, &present[i]))
      { damaged = true; *reason = "overlay-unreadable"; continue; }
      if (!present[i]) continue;
      uint8_t *bytes = NULL;
      size_t size = 0u;
      if (arc_io_read_all_limit(store->slot[i], (void **)&bytes, &size,
                                STORE_CAP, NULL))
         valid[i] = snapshot_parse(image, store, bytes, size, &records[i],
                                   &generation[i], &count[i], &slot_reason);
      else slot_reason = "overlay-unreadable";
      free(bytes);
      if (!valid[i]) { damaged = true; *reason = slot_reason; }
   }
   if (present[0] && present[1] && valid[0] && valid[1] &&
       generation[0] == generation[1])
   { damaged = true; *reason = "overlay-invalid"; }
   unsigned newest = valid[1] && (!valid[0] || generation[1] > generation[0]) ? 1u : 0u;
   if (valid[newest])
   {
      overlay->data = records[newest]; records[newest] = NULL;
      overlay->generation = generation[newest];
      overlay->records = count[newest];
      store->active = newest;
   }
   records_free(records[0], image->sector_count);
   records_free(records[1], image->sector_count);
   overlay->damaged = damaged;
   bool eligible = image->extended && (image->flags & 2u) && !(image->flags & 1u);
   overlay->writable = eligible && writes_enabled && !damaged;
   overlay->visible = writes_enabled;
   overlay->commit = store_commit;
   if (damaged)
   {
      if (show_notice) arc_jfd_store_notice(
         "Disc writes are disabled because a saved disc-change file is damaged.", true);
   }
   else if (!writes_enabled && overlay->generation && show_notice)
      arc_jfd_store_notice("Saved disc changes are hidden while Disk Writes is Off.", false);
   if (!damaged)
      *reason = !image->extended ? "legacy-header" :
         (image->flags & 1u) ? "wp-flag" :
         !(image->flags & 2u) ? "no-flag" :
         !writes_enabled ? "disk-writes-off" : "rw-flag";
   return true;
}
