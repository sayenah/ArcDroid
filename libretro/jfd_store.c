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
#define RECORD_SIZE 9u
#define CHECKSUM_SIZE 4u
#define STORE_VERSION 2u /* Kind/key records and P-DD5 schedule v1. Old slots stay untouched. */

typedef struct {
   uint8_t **data;
   uint8_t *created[ARC_JFD_PDD5_SLOTS];
} ArcJfdRecords;

typedef struct {
   char directory[4096];
   char slot[2][4096];
   char candidate[4096];
   uint8_t sha1[20];
   uint8_t sha256[32];
   unsigned image_number;
   unsigned active;
} ArcJfdStore;

typedef struct {
   uint32_t start;
   uint32_t end;
   size_t index;
} ArcJfdPayload;

static int payload_order(const void *left, const void *right)
{
   const ArcJfdPayload *a = left, *b = right;
   if (a->start != b->start) return a->start < b->start ? -1 : 1;
   if (a->end != b->end) return a->end < b->end ? -1 : 1;
   return 0;
}

static bool prepare_eligibility(const ArcJfdImage *image, uint8_t *eligible)
{
   ArcJfdPayload *payloads = malloc((image->sector_count ? image->sector_count : 1u) *
                                     sizeof(*payloads));
   if (!payloads) return false;
   size_t count = 0u;
   for (size_t i = 0u; i < image->sector_count; ++i)
   {
      const ArcJfdSector *sector = &image->sectors[i];
      if (sector->data_offset == UINT32_MAX) continue;
      eligible[i] = !(sector->flags & (ARC_JFD_ID_CRC_BAD |
                                      ARC_JFD_DATA_CRC_BAD | ARC_JFD_SLOW_SECTOR));
      payloads[count++] = (ArcJfdPayload){sector->data_offset,
         sector->data_offset + sector->size, i};
   }
   /* Each track has at most 256 descriptors by the parser contract. */
   for (unsigned t = 0u; t < image->track_count; ++t)
   {
      const ArcJfdTrack *track = &image->tracks[t];
      for (unsigned i = 0u; i < track->sector_count; ++i)
      {
         size_t first = (size_t)track->first_sector + i;
         for (unsigned j = i + 1u; j < track->sector_count; ++j)
         {
            size_t second = (size_t)track->first_sector + j;
            if (image->sectors[first].id == image->sectors[second].id &&
                image->sectors[first].density == image->sectors[second].density)
               eligible[first] = eligible[second] = 0u;
         }
      }
   }
   qsort(payloads, count, sizeof(*payloads), payload_order);
   uint32_t furthest_end = 0u;
   size_t furthest_index = 0u;
   for (size_t i = 0u; i < count; ++i)
   {
      const ArcJfdPayload *payload = &payloads[i];
      if (i && payload->start < furthest_end)
         eligible[payload->index] = eligible[furthest_index] = 0u;
      if (!i || payload->end > furthest_end)
      {
         furthest_end = payload->end;
         furthest_index = payload->index;
      }
   }
   free(payloads);
   return true;
}

static bool store_eligible(const ArcJfdImage *image, size_t index)
{
   return image->overlay && image->overlay->eligible &&
          index < image->sector_count && image->overlay->eligible[index];
}

static size_t created_slot(unsigned track_side, unsigned id)
{ return (size_t)track_side * ARC_JFD_PDD5_IDS + id; }

static bool creation_eligible(const ArcJfdImage *image,
                              unsigned track_side, unsigned id)
{
   return image->overlay && image->overlay->layout_supported &&
      image->overlay->pdd5_profile && track_side < ARC_JFD_PDD5_TRACKS &&
      id < ARC_JFD_PDD5_IDS &&
      (image->overlay->creation_mask[track_side] & (1u << id));
}

static ArcJfdRecords *records_new(size_t sectors)
{
   ArcJfdRecords *records = calloc(1u, sizeof(*records));
   if (records)
      records->data = calloc(sectors ? sectors : 1u, sizeof(*records->data));
   if (records && !records->data) { free(records); return NULL; }
   return records;
}

static void records_free(ArcJfdRecords *records, size_t sectors)
{
   if (!records) return;
   if (records->data)
      for (size_t i = 0u; i < sectors; ++i) free(records->data[i]);
   free(records->data);
   for (size_t i = 0u; i < ARC_JFD_PDD5_SLOTS; ++i)
      free(records->created[i]);
   free(records);
}

static void records_take(ArcJfdImage *image, ArcJfdRecords *records)
{
   ArcJfdOverlay *overlay = image->overlay;
   if (overlay->data)
   {
      for (size_t i = 0u; i < image->sector_count; ++i)
         free(overlay->data[i]);
      free(overlay->data);
   }
   overlay->data = records->data;
   records->data = NULL;
   for (unsigned t = 0u; t < ARC_JFD_PDD5_TRACKS; ++t)
      for (unsigned id = 0u; id < ARC_JFD_PDD5_IDS; ++id)
      {
         free(overlay->created[t][id]);
         overlay->created[t][id] = records->created[created_slot(t, id)];
         records->created[created_slot(t, id)] = NULL;
      }
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
static bool snapshot_parse(const ArcJfdImage *image, const ArcJfdStore *store,
                           const uint8_t *bytes, size_t length,
                           ArcJfdRecords **replacement, uint64_t *generation,
                           unsigned *record_count, const char **reason)
{
   *replacement = NULL;
   if (length < HEADER_SIZE + CHECKSUM_SIZE || length > STORE_CAP ||
       memcmp(bytes, "AJWO", 4u))
   { *reason = "overlay-invalid"; return false; }
   if (get32(bytes + 4u) != STORE_VERSION)
   { *reason = "overlay-invalid"; return false; }
   if (get64(bytes + 8u) != image->decoded_size ||
       memcmp(bytes + 16u, store->sha1, 20u) ||
       memcmp(bytes + 36u, store->sha256, 32u))
   { *reason = "overlay-mismatch"; return false; }
   uint64_t gen = get64(bytes + 68u);
   uint32_t count = get32(bytes + 76u);
   unsigned eligible_count = image->overlay->creation_capacity;
   for (size_t i = 0u; i < image->sector_count; ++i)
      if (store_eligible(image, i)) ++eligible_count;
   if (!gen || count > eligible_count ||
       get32(bytes + length - 4u) != arc_crc32(bytes, length - 4u))
   { *reason = "overlay-invalid"; return false; }
   ArcJfdRecords *records = records_new(image->sector_count);
   if (!records)
   { *reason = "overlay-unreadable"; return false; }
   size_t cursor = HEADER_SIZE, data_sum = 0u, eligible_sum = 0u;
   uint32_t last_key = 0u;
   for (size_t i = 0u; i < image->sector_count; ++i)
      if (store_eligible(image, i))
         eligible_sum += image->sectors[i].size;
   eligible_sum += (size_t)image->overlay->creation_capacity * 1024u;
   bool valid = true;
   for (uint32_t i = 0u; i < count && valid; ++i)
   {
      if (cursor > length - CHECKSUM_SIZE ||
          length - CHECKSUM_SIZE - cursor < RECORD_SIZE) { valid = false; break; }
      unsigned track_side = get16(bytes + cursor);
      unsigned kind = bytes[cursor + 2u];
      unsigned ordinal = get16(bytes + cursor + 3u);
      uint32_t key = (track_side << 16u) | (kind << 15u) | ordinal;
      if ((i && key <= last_key) || track_side >= image->track_count ||
          kind > 1u || (kind == 0u &&
          ordinal >= image->tracks[track_side].sector_count))
      { valid = false; break; }
      size_t index = kind == 0u ?
         (size_t)image->tracks[track_side].first_sector + ordinal : 0u;
      const ArcJfdSector *sector = kind == 0u ? &image->sectors[index] : NULL;
      unsigned size = get16(bytes + cursor + 7u);
      if ((kind == 0u && (!store_eligible(image, index) ||
           bytes[cursor + 5u] != sector->id ||
           bytes[cursor + 6u] != sector->density || size != sector->size)) ||
          (kind == 1u && (!creation_eligible(image, track_side, ordinal) ||
           bytes[cursor + 5u] != ordinal || bytes[cursor + 6u] != 2u ||
           size != 1024u)) ||
          size > length - CHECKSUM_SIZE - cursor - RECORD_SIZE ||
          size > eligible_sum || data_sum > eligible_sum - size)
      { valid = false; break; }
      uint8_t **target = kind == 0u ? &records->data[index] :
         &records->created[created_slot(track_side, ordinal)];
      *target = malloc(size);
      if (!*target) { *reason = "overlay-unreadable"; valid = false; break; }
      memcpy(*target, bytes + cursor + RECORD_SIZE, size);
      cursor += RECORD_SIZE + size;
      data_sum += size;
      last_key = key;
   }
   if (cursor != length - CHECKSUM_SIZE) valid = false;
   if (!valid)
   {
      records_free(records, image->sector_count);
      if (!*reason) *reason = "overlay-invalid";
      return false;
   }
   *replacement = records;
   *generation = gen;
   *record_count = count;
   return true;
}
static bool slot_present(const ArcJfdStore *store, unsigned slot, bool *present)
{
   const char *leaf = strrchr(store->slot[slot], '/');
   return leaf && arc_io_directory_contains(store->directory, leaf + 1u, present);
}
static bool snapshot_build(const ArcJfdImage *image, const ArcJfdStore *store,
                           bool create, size_t changed, const uint8_t *data,
                           uint8_t **output, size_t *length)
{
   size_t count = 0u, bytes = HEADER_SIZE + CHECKSUM_SIZE;
   for (size_t i = 0u; i < image->sector_count; ++i)
      if ((image->overlay->data && image->overlay->data[i]) ||
          (!create && i == changed))
      {
         if (!store_eligible(image, i) ||
             bytes > STORE_CAP - RECORD_SIZE - image->sectors[i].size) return false;
         bytes += RECORD_SIZE + image->sectors[i].size; ++count;
      }
   for (unsigned t = 0u; t < ARC_JFD_PDD5_TRACKS; ++t)
      for (unsigned id = 0u; id < ARC_JFD_PDD5_IDS; ++id)
         if (image->overlay->created[t][id] ||
             (create && changed == created_slot(t, id)))
         {
            if (!creation_eligible(image, t, id) ||
                bytes > STORE_CAP - RECORD_SIZE - 1024u) return false;
            bytes += RECORD_SIZE + 1024u; ++count;
         }
   uint8_t *snapshot = calloc(bytes, 1u);
   if (!snapshot) return false;
   memcpy(snapshot, "AJWO", 4u); put32(snapshot + 4u, STORE_VERSION);
   put64(snapshot + 8u, image->decoded_size);
   memcpy(snapshot + 16u, store->sha1, 20u);
   memcpy(snapshot + 36u, store->sha256, 32u);
   put64(snapshot + 68u, image->overlay->generation + 1u);
   put32(snapshot + 76u, (uint32_t)count);
   size_t cursor = HEADER_SIZE;
   for (unsigned t = 0u; t < image->track_count; ++t)
   {
      const ArcJfdTrack *track = &image->tracks[t];
      for (unsigned ordinal = 0u; ordinal < track->sector_count; ++ordinal)
      {
         size_t i = (size_t)track->first_sector + ordinal;
         const uint8_t *value = !create && i == changed ? data :
            image->overlay->data ? image->overlay->data[i] : NULL;
         if (!value) continue;
         const ArcJfdSector *sector = &image->sectors[i];
         put16(snapshot + cursor, (uint16_t)t);
         snapshot[cursor + 2u] = 0u;
         put16(snapshot + cursor + 3u, (uint16_t)ordinal);
         snapshot[cursor + 5u] = sector->id;
         snapshot[cursor + 6u] = sector->density;
         put16(snapshot + cursor + 7u, sector->size);
         memcpy(snapshot + cursor + RECORD_SIZE, value, sector->size);
         cursor += RECORD_SIZE + sector->size;
      }
      if (t >= ARC_JFD_PDD5_TRACKS) continue;
      for (unsigned id = 0u; id < ARC_JFD_PDD5_IDS; ++id)
      {
         const uint8_t *value = create && changed == created_slot(t, id) ?
            data : image->overlay->created[t][id];
         if (!value) continue;
         put16(snapshot + cursor, (uint16_t)t);
         snapshot[cursor + 2u] = 1u;
         put16(snapshot + cursor + 3u, (uint16_t)id);
         snapshot[cursor + 5u] = (uint8_t)id;
         snapshot[cursor + 6u] = 2u;
         put16(snapshot + cursor + 7u, 1024u);
         memcpy(snapshot + cursor + RECORD_SIZE, value, 1024u);
         cursor += RECORD_SIZE + 1024u;
      }
   }
   put32(snapshot + bytes - CHECKSUM_SIZE, arc_crc32(snapshot, bytes - CHECKSUM_SIZE));
   *output = snapshot; *length = bytes;
   return true;
}
static bool store_commit_common(ArcJfdImage *image, bool create, size_t index,
                                const uint8_t *data)
{
   ArcJfdOverlay *overlay = image->overlay;
   if (!overlay || !overlay->writable || overlay->failed || overlay->damaged)
      return false;
   ArcJfdStore *store = overlay->store;
   uint8_t *snapshot = NULL, *readback = NULL;
   ArcJfdRecords *checked = NULL;
   size_t length = 0u, read_size = 0u;
   uint64_t checked_generation = 0u;
   unsigned checked_count = 0u;
   const char *reason = NULL;
   unsigned inactive = 1u - store->active;
   bool present = false;
   bool okay = (create ? creation_eligible(image,
      (unsigned)(index / ARC_JFD_PDD5_IDS),
      (unsigned)(index % ARC_JFD_PDD5_IDS)) : store_eligible(image, index)) &&
      overlay->generation != UINT64_MAX &&
      snapshot_build(image, store, create, index, data, &snapshot, &length) &&
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
      arc_sha1_hex(readback, read_size, overlay->store_sha1);
      records_take(image, checked);
      overlay->generation = checked_generation;
      overlay->records = checked_count;
      store->active = inactive;
      /* The renamed snapshot is now the effective map. Recompute from the
       * committed records only; a failed write keeps the previous view. */
      arc_jfd_map_refresh(image);
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

static bool store_commit(ArcJfdImage *image, size_t index, const uint8_t *data)
{ return store_commit_common(image, false, index, data); }

static bool store_commit_created(ArcJfdImage *image, unsigned track_side,
                                  unsigned id, const uint8_t *data)
{
   return track_side < ARC_JFD_PDD5_TRACKS && id < ARC_JFD_PDD5_IDS &&
      store_commit_common(image, true, created_slot(track_side, id), data);
}

bool arc_jfd_store_load(ArcJfdImage *image, const char *save_directory,
                        bool writes_enabled, bool show_notice, unsigned image_number,
                        const char **reason)
{
   *reason = "no-flag";
   if (!image || !image->decoded) return false;
   bool eligible = image->extended && (image->flags & 2u) && !(image->flags & 1u);
   if (!eligible)
   {
      *reason = !image->extended ? "legacy-header" :
         (image->flags & 1u) ? "wp-flag" : "no-flag";
      image->overlay = calloc(1u, sizeof(*image->overlay));
      if (!image->overlay) return false;
      image->overlay->image_number = image_number;
      image->overlay->reason = *reason;
      return true;
   }
   *reason = "overlay-unreadable";
   ArcJfdOverlay *overlay = calloc(1u, sizeof(*overlay));
   ArcJfdStore *store = calloc(1u, sizeof(*store));
   if (!overlay || !store) { free(overlay); free(store); return false; }
   image->overlay = overlay; overlay->store = store;
   overlay->image_number = image_number;
   overlay->eligible = calloc(image->sector_count ? image->sector_count : 1u, 1u);
   if (!overlay->eligible) return false;
   if (!prepare_eligibility(image, overlay->eligible)) return false;
   if (!arc_jfd_plan_creation(image, overlay))
   {
      *reason = "layout-unsupported";
      overlay->reason = *reason;
      free(store);
      overlay->store = NULL;
      return true;
   }
   if (!save_directory) return false;
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
   ArcJfdRecords *records[2] = {NULL, NULL};
   uint64_t generation[2] = {0u, 0u};
   unsigned count[2] = {0u, 0u};
   char slot_sha1[2][41] = {{0}};
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
      if (valid[i]) arc_sha1_hex(bytes, size, slot_sha1[i]);
      free(bytes);
      if (!valid[i]) { damaged = true; *reason = slot_reason; }
   }
   if (present[0] && present[1] && valid[0] && valid[1] &&
       generation[0] == generation[1])
   { damaged = true; *reason = "overlay-invalid"; }
   unsigned newest = valid[1] && (!valid[0] || generation[1] > generation[0]) ? 1u : 0u;
   if (valid[newest])
   {
      memcpy(overlay->store_sha1, slot_sha1[newest], 41u);
      records_take(image, records[newest]);
      overlay->generation = generation[newest];
      overlay->records = count[newest];
      store->active = newest;
   }
   records_free(records[0], image->sector_count);
   records_free(records[1], image->sector_count);
   overlay->damaged = damaged;
   overlay->writable = eligible && writes_enabled && !damaged;
   overlay->visible = writes_enabled;
   overlay->commit = store_commit;
   overlay->commit_created = store_commit_created;
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
