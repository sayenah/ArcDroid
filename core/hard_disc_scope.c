/* Bounded read-only walker for the fixed ArcDroidHD new-map geometry.
 * Any uncertain map, directory or record leaves the disc untouched. GPL-2.0-only. */
#include "hard_disc_scope.h"
#include <limits.h>
#include <string.h>

enum { HD_ZONES = 5, HD_ZONE_BYTES = 512, HD_ZONE_BITS = 4064,
       HD_MAP_BYTES = HD_ZONES * HD_ZONE_BYTES,
       HD_MAP_ADDRESS = (2 * HD_ZONE_BITS - 480) * 1024,
       HD_READ_LIMIT = 65536, HD_RESOLVE_LIMIT = 256 };
typedef struct {
   ArcHdRead read;
   void *opaque;
   uint8_t map[HD_MAP_BYTES];
   size_t bytes;
   unsigned resolves;
} HdReader;
typedef struct {
   ArcGuestName name;
   uint32_t length;
   uint32_t address;
   bool directory;
} HdEntry;

static uint32_t u24(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u; }
static uint32_t u32(const uint8_t *p)
{ return u24(p) | (uint32_t)p[3] << 24u; }
static uint8_t check_zone(const uint8_t *block)
{
   unsigned s[4] = {0};
   for (int at = HD_ZONE_BYTES - 4; at > 0; at -= 4) {
      s[0] += block[at] + (s[3] >> 8u); s[3] &= 255u;
      s[1] += block[at+1] + (s[0] >> 8u); s[0] &= 255u;
      s[2] += block[at+2] + (s[1] >> 8u); s[1] &= 255u;
      s[3] += block[at+3] + (s[2] >> 8u); s[2] &= 255u;
   }
   s[0] += s[3] >> 8u;
   s[1] += block[1] + (s[0] >> 8u);
   s[2] += block[2] + (s[1] >> 8u);
   s[3] += block[3] + (s[2] >> 8u);
   return (uint8_t)(s[0] ^ s[1] ^ s[2] ^ s[3]);
}
static uint32_t bits(const uint8_t *p, unsigned at, unsigned count)
{
   uint32_t result = 0u;
   for (unsigned i = 0; i < count; ++i)
      result |= (uint32_t)((p[(at+i)>>3u] >> ((at+i)&7u))&1u) << i;
   return result;
}
static unsigned zone_end(unsigned zone)
{
   return zone == HD_ZONES - 1 ?
      32u + ARC_INSTALL_HD_BYTES / 1024u -
         ((HD_ZONES - 1u) * HD_ZONE_BITS - 480u) : 4096u;
}
static bool read_bytes(HdReader *r, size_t offset, uint8_t *out, size_t size)
{
   uint8_t sector[512];
   if (!r || !out || size > HD_READ_LIMIT - r->bytes ||
       offset > ARC_INSTALL_HD_BYTES ||
       size > ARC_INSTALL_HD_BYTES - offset) return false;
   while (size) {
      size_t within = offset % 512u;
      size_t amount = 512u - within;
      if (amount > size) amount = size;
      if (r->bytes > HD_READ_LIMIT - sizeof(sector)) return false;
      r->bytes += sizeof(sector);
      if (!r->read(r->opaque, (uint32_t)(offset / 512u), 1u, sector)) return false;
      memcpy(out, sector + within, amount);
      offset += amount; out += amount; size -= amount;
   }
   return true;
}
static bool map_valid(const uint8_t map[HD_MAP_BYTES],
                      const uint8_t record[60])
{
   uint8_t cross = 0u;
   for (unsigned z = 0u; z < HD_ZONES; ++z) {
      const uint8_t *p = map + z * HD_ZONE_BYTES;
      unsigned at = z ? 32u : 512u;
      unsigned end = zone_end(z);
      if (p[0] != check_zone(p)) return false;
      cross ^= p[3];
      /* FileCore advances the map copy's disc ID on writes; the boot-block
       * record keeps the formatter's original ID. All other fields agree. */
      if (z == 0u && (memcmp(p+4, record, 20u) ||
                      memcmp(p+4u+22u, record+22u, 38u))) return false;
      unsigned link = bits(p, 8u, 15u);
      unsigned free_at = link ? 8u + link : 0u;
      if (free_at && (free_at < at || free_at >= end)) return false;
      while (at < end) {
         if (at + 15u >= end || (free_at && free_at < at)) return false;
         unsigned id = bits(p, at, 15u);
         unsigned stop = at + 15u;
         while (stop < end && !bits(p, stop, 1u)) ++stop;
         if (stop >= end) return false;
         if (at == free_at) {
            free_at = id ? at + id : 0u;
            if (free_at && (free_at <= stop || free_at >= end)) return false;
         }
         at = stop + 1u;
      }
      if (free_at) return false;
   }
   return cross == 0xffu;
}
static bool init(HdReader *r)
{
   uint8_t boot[512], record[60], backup[HD_MAP_BYTES];
   if (!read_bytes(r, 0xc00u, boot, sizeof(boot)) ||
       u32(boot) != 0x20000000u) return false;
   unsigned sum = 0u;
   for (unsigned i = 0u; i < 511u; ++i) sum += boot[i];
   while (sum > 255u) sum = (sum & 255u) + (sum >> 8u);
   if ((uint8_t)sum != boot[511]) return false;
   memcpy(record, boot + 0x1c0u, sizeof(record));
   if (record[0] != 9u || record[1] != ARC_INSTALL_HD_SPT ||
       record[2] != ARC_INSTALL_HD_HEADS || record[3] != 0u ||
       record[4] != 15u || record[5] != 10u || record[9] != HD_ZONES ||
       record[10] != 32u || record[11] != 0u ||
       u32(record+12u) != 0x20bu ||
       u32(record+16u) != ARC_INSTALL_HD_BYTES ||
       record[20] != 0x44u || record[21] != 0x41u ||
       memcmp(record+22u, "ArcDroidHD", 10u)) return false;
   if (!read_bytes(r, HD_MAP_ADDRESS, r->map, HD_MAP_BYTES) ||
       !read_bytes(r, HD_MAP_ADDRESS + HD_MAP_BYTES, backup,
                   HD_MAP_BYTES) ||
       memcmp(r->map, backup, HD_MAP_BYTES) ||
       !map_valid(r->map, record)) return false;
   return true;
}
static bool resolve(HdReader *r, uint32_t address, size_t object_byte,
                     size_t *offset)
{
   if (++r->resolves > HD_RESOLVE_LIMIT || (address & 0xe0000000u)) return false;
   unsigned fragment = (address >> 8u) & 0x7fffu;
   unsigned shared = address & 0xffu;
   if (fragment < 2u || object_byte > SIZE_MAX -
       (size_t)(shared ? shared-1u : 0u)*512u) return false;
   size_t wanted = object_byte + (size_t)(shared ? shared-1u : 0u)*512u;
   unsigned ids_per_zone = HD_ZONE_BITS / 16u;
   if (fragment >= ids_per_zone * HD_ZONES) return false;
   unsigned first = fragment == 2u ? HD_ZONES / 2u : fragment / ids_per_zone;
   for (unsigned step = 0u; step < HD_ZONES; ++step) {
      unsigned z = (first + step) % HD_ZONES;
      const uint8_t *p = r->map + z * HD_ZONE_BYTES;
      unsigned at = z ? 32u : 512u;
      unsigned end = zone_end(z);
      unsigned link = bits(p, 8u, 15u);
      unsigned free_at = link ? 8u + link : 0u;
      size_t base = z ? HD_ZONE_BITS - 480u + (z-1u)*HD_ZONE_BITS : 0u;
      while (at < end) {
         unsigned id = bits(p, at, 15u);
         unsigned stop = at + 15u;
         while (stop < end && !bits(p, stop, 1u)) ++stop;
         if (stop >= end) return false;
         size_t length = stop + 1u - at;
         if (at == free_at) free_at = id ? at + id : 0u;
         else if (id == fragment) {
            size_t bit = base + at - (z ? 32u : 512u);
            if (bit >= ARC_INSTALL_HD_BYTES/1024u) return false;
            if (length > ARC_INSTALL_HD_BYTES/1024u - bit)
               length = ARC_INSTALL_HD_BYTES/1024u - bit;
            if (wanted < length*1024u) {
               *offset = bit*1024u + wanted;
               return *offset <= ARC_INSTALL_HD_BYTES - 512u;
            }
            wanted -= length*1024u;
         }
         at = stop + 1u;
      }
   }
   return false;
}
static bool object_read(HdReader *r, uint32_t address, uint8_t *out,
                        size_t length)
{
   size_t at = 0u;
   while (at < length) {
      size_t sector_offset;
      size_t amount = length - at;
      if (amount > 512u) amount = 512u;
      if (!resolve(r, address, at, &sector_offset) ||
          !read_bytes(r, sector_offset, out + at, amount)) return false;
      at += amount;
   }
   return true;
}
static uint32_t ror13(uint32_t value)
{ return (value >> 13u) | (value << 19u); }
static bool directory(HdReader *r, uint32_t address, HdEntry entries[77],
                       unsigned *count)
{
   uint8_t d[2048];
   if (!object_read(r, address, d, sizeof(d)) ||
       (memcmp(d+1, "Nick", 4u) && memcmp(d+1, "Hugo", 4u)) ||
       d[2007] || d[2008] || d[2009] || d[0] != d[2042] ||
       memcmp(d+1, d+2043, 4u)) return false;
   unsigned n = 0u;
   while (n < 77u && d[5u+n*26u]) ++n;
   size_t used = 5u + n*26u;
   size_t word_end = used & ~(size_t)3u;
   uint32_t check = 0u;
   for (size_t i = 0u; i < word_end; i += 4u)
      check = u32(d+i) ^ ror13(check);
   for (size_t i = word_end; i < used; ++i)
      check = d[i] ^ ror13(check);
   for (size_t i = 2008u; i < 2044u; i += 4u)
      check = u32(d+i) ^ ror13(check);
   if (d[2047] != (uint8_t)(check ^ (check>>8u) ^
                             (check>>16u) ^ (check>>24u))) return false;
   for (unsigned i = 0u; i < n; ++i) {
      const uint8_t *raw = d+5u+i*26u;
      HdEntry *e = &entries[i];
      memset(e, 0, sizeof(*e));
      unsigned length = 0u;
      for (unsigned j = 0u; j < 10u && raw[j] >= 0x20u; ++j)
         e->name.bytes[length++] = raw[j];
      if (!length) return false;
      e->name.length = (uint8_t)length;
      e->length = u32(raw+18u);
      e->address = u24(raw+22u);
      e->directory = (raw[25] & 8u) != 0u;
   }
   *count = n;
   return true;
}
static uint8_t fold(uint8_t c)
{ return c >= 'A' && c <= 'Z' ? (uint8_t)(c+32u) : c; }
static bool equal(const ArcGuestName *a, const ArcGuestName *b)
{
   if (a->length != b->length) return false;
   for (unsigned i=0u; i<a->length; ++i)
      if (fold(a->bytes[i]) != fold(b->bytes[i])) return false;
   return true;
}
static bool literal(const ArcGuestName *a, const char *s)
{
   size_t n = strlen(s);
   if (a->length != n) return false;
   for (size_t i=0u; i<n; ++i)
      if (fold(a->bytes[i]) != fold((uint8_t)s[i])) return false;
   return true;
}
static bool application(HdReader *r, const HdEntry *e, bool *valid)
{
   *valid = false;
   ArcLaunchCandidate candidate = {0};
   candidate.name = e->name;
   arc_launch_candidate_classify_counts(&candidate,0u,0u);
   if (!e->directory || !candidate.launchable || candidate.resource ||
       e->length != 2048u) return true;
   HdEntry children[77]; unsigned count;
   if (!directory(r, e->address, children, &count)) return false;
   unsigned runs = 0u, boots = 0u;
   for (unsigned i=0u; i<count; ++i) {
      if (children[i].directory) continue;
      if (literal(&children[i].name, "!Run")) ++runs;
      if (literal(&children[i].name, "!Boot")) ++boots;
   }
   arc_launch_candidate_classify_counts(&candidate,runs,boots);
   *valid = arc_launch_candidate_is_target(&candidate);
   return true;
}
static bool record_field(const uint8_t *data, size_t length, size_t *at,
                         const char *key, char *out, size_t capacity)
{
   size_t prefix=strlen(key), end=*at;
   if (*at > length || prefix > length-*at ||
       memcmp(data+*at,key,prefix)) return false;
   end+=prefix;
   size_t start=end;
   while (end < length && data[end] != '\r') {
      if (data[end] < 0x20u || data[end] == 0x7fu) return false;
      ++end;
   }
   size_t size=end-start;
   if (!size || size >= capacity || end == length) return false;
   memcpy(out,data+start,size);
   out[size]=0;
   *at=end+1u;
   return true;
}
static bool record_equal(const uint8_t *data, size_t size, size_t *at,
                         const char *key, const char *expected)
{
   char value[256];
   return expected && record_field(data,size,at,key,value,sizeof(value)) &&
          !strcmp(value,expected);
}
static ArcHdScopeState parse_record(const uint8_t *data, size_t size,
                                    const ArcHdExpected *expected,
                                    ArcHdScope *scope)
{
   if (size < 8u || memcmp(data, "ARCINST1", 8u))
      return ARC_HD_UNOWNED;
   if (size < 9u || data[8] != '\r' || size > 1024u ||
       data[size-1u] != '\r')
      return ARC_HD_FOREIGN;
   char state[32], attempt[20], product[16];
   size_t at=9u;
   if (!record_equal(data,size,&at,"revision=","1") ||
       !record_field(data,size,&at,"state=",state,sizeof(state)) ||
       !record_field(data,size,&at,"attempt=",attempt,sizeof(attempt)) ||
       !record_equal(data,size,&at,"source=",expected->source) ||
       !record_equal(data,size,&at,"media=",expected->media) ||
       !record_equal(data,size,&at,"fingerprint=",expected->fingerprint) ||
       !record_equal(data,size,&at,"content=",expected->content) ||
       !record_equal(data,size,&at,"os=",expected->os))
      return ARC_HD_FOREIGN;
   unsigned value = 0u;
   for (unsigned i=0u; attempt[i]; ++i) {
      if (attempt[i] < '0' || attempt[i] > '9' ||
          value > (UINT_MAX-(unsigned)(attempt[i]-'0'))/10u)
         return ARC_HD_FOREIGN;
      value = value*10u+(unsigned)(attempt[i]-'0');
   }
   if (!value) return ARC_HD_FOREIGN;
   scope->attempt = value;
   if (!strcmp(state,"pending"))
      return at == size ? ARC_HD_PENDING : ARC_HD_FOREIGN;
   if (!strcmp(state,"started"))
      return at == size ? ARC_HD_STARTED : ARC_HD_FOREIGN;
   if (!strcmp(state,"failed")) {
      char error[128];
      return record_field(data,size,&at,"error=",error,sizeof(error)) &&
         at == size ?
         ARC_HD_FAILED : ARC_HD_FOREIGN;
   }
   if (strcmp(state,"completed") ||
       !record_field(data,size,&at,"product=",product,sizeof(product)) ||
       at != size)
      return ARC_HD_FOREIGN;
   scope->product.length = (uint8_t)strlen(product);
   memcpy(scope->product.bytes, product, scope->product.length);
   ArcLaunchCandidate candidate={0};
   candidate.name=scope->product;
   arc_launch_candidate_classify_counts(&candidate,1u,0u);
   return arc_launch_candidate_is_target(&candidate) &&
          !equal(&scope->product,expected->copy) ?
          ARC_HD_COMPLETED : ARC_HD_FOREIGN;
}

ArcHdScope arc_hd_scope_read(ArcHdRead read, void *opaque,
                             const ArcHdExpected *expected)
{
   ArcHdScope scope = {0};
   scope.state = ARC_HD_INDETERMINATE;
   if (!read || !expected || !expected->copy) return scope;
   HdReader r = {0}; r.read=read; r.opaque=opaque;
   if (!init(&r)) return scope;
   HdEntry root[77]; unsigned root_count;
   if (!directory(&r, 0x20bu, root, &root_count)) return scope;
   const HdEntry *owned = NULL;
   for (unsigned i=0u; i<root_count; ++i)
      if (literal(&root[i].name,"ArcDroid")) {
         if (owned) return scope;
         owned=&root[i];
      }
   if (!owned) { scope.state=ARC_HD_ABSENT; return scope; }
   if (!owned->directory) { scope.state=ARC_HD_UNOWNED; return scope; }
   if (owned->length != 2048u) return scope;
   HdEntry entries[77]; unsigned count;
   if (!directory(&r, owned->address, entries, &count)) return scope;
   for (unsigned i=0u;i<count;++i)
      for (unsigned j=0u;j<i;++j)
         if (equal(&entries[i].name,&entries[j].name)) return scope;
   const HdEntry *record = NULL;
   for (unsigned i=0u; i<count; ++i) {
      if (literal(&entries[i].name,"Installed")) {
         if (record) return scope;
         record=&entries[i];
      }
      bool app;
      if (!application(&r,&entries[i],&app)) return scope;
      if (app) {
         ++scope.application_count;
         if (equal(&entries[i].name,expected->copy)) scope.copy_present=true;
         else { scope.product=entries[i].name; scope.product_present=true; }
      } else if (!literal(&entries[i].name,"Installed") &&
                 !literal(&entries[i].name,"Installed~")) ++scope.other_count;
   }
   if (!record) { scope.state=ARC_HD_UNOWNED; return scope; }
   if (record->directory) {
      scope.state=ARC_HD_FOREIGN; return scope;
   }
   if (record->length > 1024u) {
      uint8_t magic[8];
      if (!object_read(&r,record->address,magic,sizeof(magic))) return scope;
      scope.state=memcmp(magic,"ARCINST1",sizeof(magic)) ?
         ARC_HD_UNOWNED : ARC_HD_FOREIGN;
      return scope;
   }
   if (!record->length) {
      scope.state=ARC_HD_UNOWNED; return scope;
   }
   uint8_t text[1024];
   if (!object_read(&r,record->address,text,record->length)) return scope;
   scope.state=parse_record(text,record->length,expected,&scope);
   if (scope.state == ARC_HD_COMPLETED) {
      scope.product_present=false;
      for (unsigned i=0u; i<count; ++i)
         if (equal(&entries[i].name,&scope.product)) {
            bool app;
            if (!application(&r,&entries[i],&app)) {
               scope.state=ARC_HD_INDETERMINATE; return scope;
            }
            scope.product_present=app;
         }
   }
   return scope;
}

const char *arc_hd_scope_name(ArcHdScopeState state)
{
   static const char *const names[] = {
      "indeterminate", "absent", "unowned", "foreign", "pending",
      "started", "completed", "failed"
   };
   return state <= ARC_HD_FAILED ? names[state] : "indeterminate";
}
