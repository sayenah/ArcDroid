/* Independent, bounded FileCore new-map reader for install publication.
 * Project-authored under GPL-2.0-only. It reads the candidate memory image;
 * supplied package and support-disc bytes never reach a host store. */
#include "install_validate.h"
#include "arcdroid_core.h"
#include "jfd.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZONES 5u
#define ZONE_BYTES 1024u
#define ZONE_BITS 8160u
#define MAP_AT ((2u * ZONE_BITS - 480u) * 1024u)
#define MAP_SIZE (ZONES * ZONE_BYTES)
#define MAX_OBJECTS 512u
#define MAX_DEPTH 16u
#define MAX_FILE (8u * 1024u * 1024u)

typedef struct {
   const uint8_t *image;
   const uint8_t *map;
   uint8_t id_length, bpmb_log2;
   char *lines[MAX_OBJECTS];
   unsigned count;
   bool image_found;
   bool run_found;
   const ArcInstallPlan *plan;
   ArcInstallValidation *result;
} Reader;
typedef struct {
   char name[11];
   uint32_t load, exec, length, address;
   uint8_t attr;
   bool directory;
} Entry;
static uint32_t get24(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
   ((uint32_t)p[2] << 16u); }
static uint32_t get32(const uint8_t *p)
{ return get24(p) | ((uint32_t)p[3] << 24u); }
static uint32_t ror13(uint32_t n)
{ return (n >> 13u) | (n << 19u); }
static void reason(Reader *r, const char *why)
{ snprintf(r->result->reason, sizeof(r->result->reason), "%s", why); }
static unsigned bits(const uint8_t *p, unsigned at, unsigned width)
{
   unsigned value = 0u;
   for (unsigned i = 0u; i < width; ++i)
      value |= ((p[(at + i) >> 3u] >> ((at + i) & 7u)) & 1u) << i;
   return value;
}
static unsigned zone_end(unsigned zone)
{ return zone < 2u ? 8192u : zone == 2u ? 320u : 32u; }
static uint8_t zone_check(const uint8_t *block)
{
   unsigned sum[4] = {0u, 0u, 0u, 0u};
   for (int at = 1020; at > 0; at -= 4) {
      sum[0] += block[at] + (sum[3] >> 8u); sum[3] &= 255u;
      sum[1] += block[at + 1] + (sum[0] >> 8u); sum[0] &= 255u;
      sum[2] += block[at + 2] + (sum[1] >> 8u); sum[1] &= 255u;
      sum[3] += block[at + 3] + (sum[2] >> 8u); sum[2] &= 255u;
   }
   sum[0] += sum[3] >> 8u;
   sum[1] += block[1] + (sum[0] >> 8u);
   sum[2] += block[2] + (sum[1] >> 8u);
   sum[3] += block[3] + (sum[2] >> 8u);
   return (uint8_t)(sum[0] ^ sum[1] ^ sum[2] ^ sum[3]);
}
static bool map_valid(const uint8_t *map, const uint8_t *boot)
{
   uint8_t cross = 0u;
   for (unsigned z = 0u; z < ZONES; ++z) {
      const uint8_t *block = map + z * ZONE_BYTES;
      if (block[0] != zone_check(block)) return false;
      cross ^= block[3];
      if (!z && memcmp(block + 4u, boot, 20u)) return false;
      unsigned start = z ? 32u : 512u;
      unsigned end = zone_end(z);
      unsigned link = bits(block, 8u, boot[4]);
      unsigned free_at = link ? 8u + link : 0u;
      if (free_at && (free_at < start || free_at >= end)) return false;
      while (start < end) {
         if (start + boot[4] >= end || (free_at && free_at < start))
            return false;
         unsigned id = bits(block, start, boot[4]);
         unsigned stop = start + boot[4];
         while (stop < end && !bits(block, stop, 1u)) ++stop;
         if (stop >= end) return false;
         if (free_at == start) {
            free_at = id ? start + id : 0u;
            if (free_at && free_at <= stop) return false;
         }
         start = stop + 1u;
      }
      if (free_at) return false;
   }
   return cross == 0xffu;
}
static bool boot_valid(const uint8_t *image)
{
   const uint8_t *boot = image + 0xdc0u;
   if (get32(image + 0xc00u) != 0x20000000u ||
       boot[0] != 9u || boot[1] != 63u || boot[2] != 16u ||
       boot[3] != 0u || boot[4] != 15u || boot[5] != 10u ||
       boot[9] != ZONES || boot[10] != 32u || boot[11] ||
       (get32(boot + 12u) & 0x00ffffffu) != 0x20bu ||
       get32(boot + 16u) != ARC_INSTALL_HD_BYTES) return false;
   unsigned sum = 0u;
   for (unsigned i = 0u; i < 511u; ++i) sum += image[0xc00u + i];
   while (sum > 255u) sum = (sum & 255u) + (sum >> 8u);
   return image[0xdffu] == (uint8_t)sum;
}
static bool map_resolve(const Reader *r, uint32_t address,
                        size_t sector, size_t *offset)
{
   unsigned fragment = (address >> 8u) & 0x7fffu;
   unsigned shared = address & 255u;
   if ((address & 0xe0000000u) || fragment < 2u ||
       sector > SIZE_MAX - (shared ? shared - 1u : 0u)) return false;
   size_t wanted = sector + (shared ? shared - 1u : 0u);
   unsigned ids_per_zone = ZONE_BITS / (r->id_length + 1u);
   if (!ids_per_zone || fragment >= ids_per_zone * ZONES) return false;
   unsigned first = fragment == 2u ? ZONES / 2u : fragment / ids_per_zone;
   for (unsigned step = 0u; step < ZONES; ++step) {
      unsigned z = (first + step) % ZONES;
      const uint8_t *block = r->map + z * ZONE_BYTES;
      unsigned start = z ? 32u : 512u;
      unsigned end = zone_end(z);
      unsigned link = bits(block, 8u, r->id_length);
      unsigned free_at = link ? 8u + link : 0u;
      size_t zone_base = z ? (size_t)ZONE_BITS - 480u +
                               (size_t)(z - 1u) * ZONE_BITS : 0u;
      while (start < end) {
         unsigned id = bits(block, start, r->id_length);
         unsigned stop = start + r->id_length;
         while (stop < end && !bits(block, stop, 1u)) ++stop;
         if (stop >= end) return false;
         size_t length = (size_t)stop + 1u - start;
         if (start == free_at) free_at = id ? start + id : 0u;
         else if (id == fragment) {
            size_t allocation = zone_base + start - (z ? 32u : 512u);
            if (allocation >= ARC_INSTALL_HD_BYTES / 1024u) return false;
            if (length > ARC_INSTALL_HD_BYTES / 1024u - allocation)
               length = ARC_INSTALL_HD_BYTES / 1024u - allocation;
            if (wanted < length) {
               *offset = (allocation + wanted) * 1024u;
               return *offset <= ARC_INSTALL_HD_BYTES - 1024u;
            }
            wanted -= length;
         }
         start = stop + 1u;
      }
   }
   return false;
}
static bool object_read(const Reader *r, uint32_t address,
                        size_t at, uint8_t *out, size_t length)
{
   while (length) {
      size_t sector_offset, in_sector = at % 1024u;
      size_t amount = 1024u - in_sector;
      if (amount > length) amount = length;
      if (!map_resolve(r, address, at / 1024u, &sector_offset)) return false;
      memcpy(out, r->image + sector_offset + in_sector, amount);
      out += amount; at += amount; length -= amount;
   }
   return true;
}
static bool directory_check(const uint8_t *d, unsigned count)
{
   uint32_t check = 0u;
   unsigned used = 5u + count * 26u;
   for (unsigned at = 0u; at < (used & ~3u); at += 4u)
      check = get32(d + at) ^ ror13(check);
   for (unsigned at = used & ~3u; at < used; ++at)
      check = d[at] ^ ror13(check);
   for (unsigned at = 2008u; at < 2044u; at += 4u)
      check = get32(d + at) ^ ror13(check);
   return d[2047u] == (uint8_t)(check ^ (check >> 8u) ^
                                 (check >> 16u) ^ (check >> 24u));
}
static bool directory_read(Reader *r, uint32_t address, Entry entries[77],
                           unsigned *count, bool root)
{
   uint8_t d[2048];
   if (!object_read(r, address, 0u, d, sizeof(d))) return false;
   if (d[0] != d[2042u] ||
       (memcmp(d + 1u, "Nick", 4u) && memcmp(d + 1u, "Hugo", 4u)) ||
       memcmp(d + 1u, d + 2043u, 4u) ||
       (root && memcmp(d + 1u, "Nick", 4u)) ||
       d[2007u] || d[2008u] || d[2009u]) return false;
   unsigned used = 0u;
   for (unsigned i = 0u; i < 77u; ++i) {
      const uint8_t *raw = d + 5u + i * 26u;
      if (!raw[0]) break;
      Entry *e = &entries[used];
      memset(e, 0, sizeof(*e));
      unsigned n = 0u;
      for (unsigned k = 0u; k < 10u; ++k) {
         uint8_t ch = raw[k];
         if (ch < 0x20u) break;
         if (ch == '.' || ch == '/' || ch == '\\') return false;
         e->name[n++] = (char)ch;
      }
      if (!n) return false;
      e->load = get32(raw + 10u);
      e->exec = get32(raw + 14u);
      e->length = get32(raw + 18u);
      e->address = get24(raw + 22u);
      e->attr = raw[25u];
      e->directory = (e->attr & 8u) != 0u;
      ++used;
   }
   if (!directory_check(d, used)) return false;
   *count = used;
   return true;
}
static bool same_name(const char *a, const char *b)
{
   while (*a && *b) {
      char x = *a++, y = *b++;
      if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
      if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
      if (x != y) return false;
   }
   return *a == *b;
}
static const Entry *find(const Entry *entries, unsigned count, const char *name)
{
   const Entry *found = NULL;
   for (unsigned i = 0u; i < count; ++i)
      if (same_name(entries[i].name, name)) {
         if (found) return NULL;
         found = &entries[i];
      }
   return found;
}
static bool add_line(Reader *r, const Entry *e, const char *path,
                     const uint8_t *data)
{
   if (r->count >= MAX_OBJECTS) return false;
   char line[512], sha1[41];
   if (e->directory)
      snprintf(line, sizeof(line), "D %s a%02x\n", path, e->attr);
   else {
      arc_sha1_hex(data, e->length, sha1);
      if ((e->load & 0xfff00000u) == 0xfff00000u)
         snprintf(line, sizeof(line), "F %s %" PRIu32 " %s t%03x a%02x\n",
                  path, e->length, sha1, (e->load >> 8u) & 0xfffu, e->attr);
      else
         snprintf(line, sizeof(line), "F %s %" PRIu32
                   " %s l%08xe%08x a%02x\n", path, e->length, sha1,
                  e->load, e->exec, e->attr);
   }
   size_t n = strlen(line);
   if (n >= sizeof(line)) return false;
   r->lines[r->count] = malloc(n + 1u);
   if (!r->lines[r->count]) return false;
   memcpy(r->lines[r->count], line, n + 1u);
   ++r->count;
   return true;
}
static bool validate_image(Reader *r, const Entry *e)
{
   if (e->directory || e->length > MAX_FILE) return false;
   uint8_t *bytes = malloc(e->length ? e->length : 1u);
   if (!bytes) return false;
   bool okay = object_read(r, e->address, 0u, bytes, e->length);
   if (okay) {
      ArcJfdImage decoded = {0};
      char error[128] = {0};
      okay = arc_jfd_parse(bytes, e->length, &decoded, error, sizeof(error));
      if (okay) {
         char hash[41];
         arc_sha1_hex(decoded.decoded, decoded.decoded_size, hash);
         const ArcInstallDisc *expected = &r->plan->variant->discs[0];
         okay = decoded.decoded_size == expected->size &&
                !strcmp(hash, expected->sha1);
         arc_jfd_free(&decoded);
      }
   }
   free(bytes);
   return okay;
}
static bool walk(Reader *r, uint32_t address, const char *parent,
                 unsigned depth)
{
   if (depth >= MAX_DEPTH) return false;
   Entry entries[77];
   unsigned count = 0u;
   if (!directory_read(r, address, entries, &count, false)) return false;
   for (unsigned i = 0u; i < count; ++i) {
      const Entry *e = &entries[i];
      char path[256];
      int n = snprintf(path, sizeof(path), "%s.%s", parent, e->name);
      if (n < 0 || (size_t)n >= sizeof(path)) return false;
      char embedded[16];
      snprintf(embedded, sizeof(embedded), "F%05u01",
               r->plan->profile->game_id);
      if (depth == 0u && r->plan->variant->image_in_app &&
          same_name(e->name, embedded)) {
         if (r->image_found || !validate_image(r, e)) return false;
         r->image_found = true;
         continue;
      }
      if (depth == 0u && same_name(e->name, "!Run")) {
         char expected[40];
         int length = snprintf(expected, sizeof(expected),
                               "ADFBootFloppy F%05u01\r",
                               r->plan->profile->game_id);
         uint8_t run[40];
         if (r->run_found || e->directory || length < 0 ||
             (size_t)length >= sizeof(expected) ||
             e->length != (uint32_t)length ||
             (e->load & 0xfff00000u) != 0xfff00000u ||
             ((e->load >> 8u) & 0xfffu) != 0xfebu ||
             !object_read(r, e->address, 0u, run, (size_t)length) ||
             memcmp(run, expected, (size_t)length)) return false;
         r->run_found = true;
      }
      if (e->directory) {
         if (e->length != 2048u || !add_line(r, e, path, NULL) ||
             !walk(r, e->address, path, depth + 1u)) return false;
      } else {
         if (e->length > MAX_FILE) return false;
         uint8_t *data = malloc(e->length ? e->length : 1u);
         if (!data) return false;
         bool okay = object_read(r, e->address, 0u, data, e->length) &&
                     add_line(r, e, path, data);
         free(data);
         if (!okay) return false;
      }
   }
   return true;
}
static int line_order(const void *a, const void *b)
{ return strcmp(*(const char *const *)a, *(const char *const *)b); }

bool arc_install_hd_validate(const ArcInstallPlan *plan,
   const uint8_t *image, size_t size, ArcInstallValidation *result)
{
   if (!result) return false;
   memset(result, 0, sizeof(*result));
   Reader r = {0};
   r.image = image; r.plan = plan; r.result = result;
   if (!plan || !plan->profile || !plan->variant || !image ||
       size != ARC_INSTALL_HD_BYTES || !boot_valid(image)) {
      reason(&r, "invalid-boot-record"); return false;
   }
   const uint8_t *boot = image + 0xdc0u;
   r.id_length = boot[4]; r.bpmb_log2 = boot[5];
   const uint8_t *first = image + MAP_AT;
   const uint8_t *second = first + MAP_SIZE;
   if (!map_valid(first, boot) || !map_valid(second, boot) ||
       memcmp(first, second, MAP_SIZE)) {
      reason(&r, "invalid-filecore-map"); return false;
   }
   r.map = first;
   Entry root[77]; unsigned root_count = 0u;
   if (!directory_read(&r, 0x20bu, root, &root_count, true)) {
      reason(&r, "invalid-root-directory"); return false;
   }
   const Entry *marker = find(root, root_count, "ArcReady");
   const Entry *app = find(root, root_count, plan->variant->app);
   uint8_t expected[96]; size_t expected_size = 0u;
   if (!marker || marker->directory ||
       !arc_install_marker(plan, expected, sizeof(expected), &expected_size) ||
       marker->length != expected_size) {
      reason(&r, "marker-missing-or-size"); return false;
   }
   uint8_t actual[96];
   if (!object_read(&r, marker->address, 0u, actual, expected_size) ||
       memcmp(actual, expected, expected_size)) {
      reason(&r, "marker-content"); return false;
   }
   if (!app || !app->directory || app->length != 2048u) {
      reason(&r, "application-directory-missing"); return false;
   }
   char prefix[32];
   snprintf(prefix, sizeof(prefix), "$.%s", app->name);
   bool okay = walk(&r, app->address, prefix, 0u);
   if (okay && !r.run_found) okay = false;
   if (okay && plan->variant->image_in_app && !r.image_found) okay = false;
   if (!okay) reason(&r, "application-tree-invalid");
   if (okay) {
      qsort(r.lines, r.count, sizeof(r.lines[0]), line_order);
      size_t total = 0u;
      for (unsigned i = 0u; i < r.count; ++i) total += strlen(r.lines[i]);
      char *listing = malloc(total ? total : 1u);
      if (!listing) okay = false;
      else {
         size_t at = 0u;
         for (unsigned i = 0u; i < r.count; ++i) {
            size_t n = strlen(r.lines[i]);
            memcpy(listing + at, r.lines[i], n); at += n;
         }
         arc_sha256_hex(listing, total, result->tree_sha256);
         free(listing);
         result->tree_count = r.count;
         if (r.count != plan->variant->expected_tree_count ||
             strcmp(result->tree_sha256,
                    plan->variant->expected_tree_sha256)) {
            reason(&r, "application-tree-mismatch"); okay = false;
         }
      }
   }
   for (unsigned i = 0u; i < r.count; ++i) free(r.lines[i]);
   if (!okay && !result->reason[0]) reason(&r, "validation-failed");
   if (okay) reason(&r, "complete");
   return okay;
}
