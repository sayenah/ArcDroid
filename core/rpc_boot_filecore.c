/* ArcDroid RPC FileCore writer, GPL-2.0-only.
 * Geometry is fixed by R2c; no guest or package bytes are incorporated here.
 * Checksum encoding adapted from core/install_disc.c and core/adffs.c.
 * The test-side reader shares none of these routines. */
#include "rpc_boot_filecore.h"
#include "install_disc.h"
#include <string.h>
static void put(uint8_t *p, uint32_t v, unsigned n) {
  for (unsigned i = 0; i < n; ++i)
    p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get(const uint8_t *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static unsigned fold(unsigned c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
int arc_boot_name_compare(const char *a, const char *b) {
  while (*a && *b) {
    unsigned x = fold((uint8_t)*a++), y = fold((uint8_t)*b++);
    if (x != y)
      return x < y ? -1 : 1;
  }
  return (uint8_t)*a - (uint8_t)*b;
}
static unsigned limit(unsigned zone) { return zone == 4 ? 384 : 4096; }
static void bits(uint8_t *p, unsigned at, unsigned n, unsigned value) {
  for (unsigned i = 0; i < n; ++i)
    if (value & (1u << i))
      p[(at + i) / 8] |= (uint8_t)(1u << ((at + i) % 8));
}
static uint8_t zone_check(const uint8_t *p) {
  unsigned a = 0, b = 0, c = 0, d = 0;
  for (int i = 508; i > 0; i -= 4) {
    a += p[i] + (d >> 8);
    d &= 255;
    b += p[i + 1] + (a >> 8);
    a &= 255;
    c += p[i + 2] + (b >> 8);
    b &= 255;
    d += p[i + 3] + (c >> 8);
    c &= 255;
  }
  a += d >> 8;
  b += p[1] + (a >> 8);
  c += p[2] + (b >> 8);
  d += p[3] + (c >> 8);
  return (uint8_t)(a ^ b ^ c ^ d);
}
void arc_boot_layout_init(ArcBootLayout *l) {
  memset(l, 0, sizeof(*l));
  (void)arc_hd_format_range(256, 13, ARC_BOOT_MAP_AT, l->map, 2560);
  l->map[11] = 3; /* map copy of disc-record boot option */
  l->cursor[0] = 528;
  l->cursor[1] = 32;
  l->cursor[2] = 48;
  l->cursor[3] = 32;
  l->cursor[4] = 32;
  l->serial[0] = 3;
  l->extents[l->count++] = (ArcBootExtent){0, 512, 16, 2, 0};
  l->extents[l->count++] = (ArcBootExtent){2, 32, 16, 2, ARC_BOOT_MAP_AT};
}
static bool extent(ArcBootLayout *l, unsigned zone, unsigned units,
                   unsigned id) {
  if (units < 16 || l->count == ARC_BOOT_EXTENTS ||
      units > limit(zone) - l->cursor[zone])
    return false;
  unsigned start = l->cursor[zone];
  l->extents[l->count++] = (ArcBootExtent){
      zone, start, units, id, (zone * 4064u - 480u + start - 32u) * 8192u};
  l->cursor[zone] += units;
  l->allocated_bytes += (uint64_t)units * 8192;
  return true;
}
static bool allocate(ArcBootLayout *l, uint32_t bytes, unsigned *id) {
  unsigned units = (bytes + 8191) / 8192;
  if (units < 16)
    units = 16;
  unsigned zone = 0;
  while (zone < 5 && limit(zone) - l->cursor[zone] < 16)
    ++zone;
  if (zone == 5 || l->serial[zone] >= 254)
    return false;
  *id = zone * 254 + l->serial[zone]++;
  for (unsigned turn = 0; turn < 5 && units; ++turn) {
    unsigned z = (zone + turn) % 5, free = limit(z) - l->cursor[z];
    if (free < 16)
      continue;
    unsigned take = units < free ? units : free;
    if (take < 16)
      take = 16;
    if (free - take < 16)
      take = free;
    if (!extent(l, z, take, *id))
      return false;
    units = units > take ? units - take : 0;
  }
  return units == 0;
}
bool arc_boot_layout_directory(ArcBootLayout *l, ArcBootNode *nodes,
                               unsigned di) {
  ArcBootNode *d = &nodes[di];
  if (!d->directory || d->count > 77)
    return false;
  /* Directory entries are FileCore ordered regardless of archive order. */
  for (unsigned i = 1; i < d->count; ++i) {
    unsigned value = d->child[i], j = i;
    while (j && arc_boot_name_compare(nodes[value].name,
                                      nodes[d->child[j - 1]].name) < 0) {
      d->child[j] = d->child[j - 1];
      --j;
    }
    d->child[j] = value;
  }
  unsigned order[77], count = 0;
  for (unsigned i = 0; i < d->count; ++i)
    if (!nodes[d->child[i]].directory && nodes[d->child[i]].size)
      order[count++] = d->child[i];
  for (unsigned i = 1; i < count; ++i) {
    unsigned v = order[i], j = i;
    while (j) {
      ArcBootNode *a = &nodes[v], *b = &nodes[order[j - 1]];
      unsigned as = (a->size + 511) / 512, bs = (b->size + 511) / 512;
      if (as < bs || (as == bs && arc_boot_name_compare(a->name, b->name) >= 0))
        break;
      order[j] = order[j - 1];
      --j;
    }
    order[j] = v;
  }
  unsigned used[78] = {4}, ids[78] = {0}, bins = 1;
  /* Root stays in reserved object 2. Its sibling-only first window retains
   * four padding sectors, matching the conservative capacity tally. */
  if (di)
    d->address = 0;
  else
    d->address = 0x20b;
  if (di || count) {
    if (!allocate(l, 131072, &ids[0]))
      return false;
    if (di)
      d->address = (ids[0] << 8) | 1;
  }
  for (unsigned i = 0; i < count; ++i) {
    ArcBootNode *f = &nodes[order[i]];
    unsigned sectors = (f->size + 511) / 512;
    if (sectors > 251) {
      unsigned id;
      if (!allocate(l, f->size, &id))
        return false;
      f->address = id << 8;
      continue;
    }
    unsigned bin = 0;
    while (bin < bins && used[bin] + sectors > 255)
      ++bin;
    if (bin == bins) {
      if (bins == 78 || !allocate(l, 131072, &ids[bins]))
        return false;
      used[bins++] = 0;
    }
    f->address = (ids[bin] << 8) | (used[bin] + 1);
    used[bin] += sectors;
  }
  return true;
}
void arc_boot_layout_finish(ArcBootLayout *l) {
  uint8_t record[60];
  memcpy(record, l->map + 4, 60);
  memset(l->map, 0, sizeof(l->map));
  memcpy(l->map + 4, record, 60);
  for (unsigned z = 0; z < 5; ++z) {
    uint8_t *p = l->map + z * 512;
    bits(p, 23, 1, 1);
    p[3] = z ? 0 : 255;
    for (unsigned i = 0; i < l->count; ++i) {
      ArcBootExtent *a = &l->extents[i];
      if (a->zone == z) {
        bits(p, a->first, 15, a->id);
        bits(p, a->first + a->units - 1, 1, 1);
      }
    }
    if (l->cursor[z] < limit(z)) {
      bits(p, 8, 15, l->cursor[z] - 8);
      bits(p, limit(z) - 1, 1, 1);
    }
    if (z == 4) {
      bits(p, 384, 15, 1);
      bits(p, 4095, 1, 1);
    }
    p[0] = zone_check(p);
  }
}
void arc_boot_layout_boot(uint8_t out[512]) {
  (void)arc_hd_format_range(256, 13, 3072, out, 512);
  out[455] = 3;
  unsigned sum = 0;
  for (unsigned i = 0; i < 511; ++i)
    sum += out[i];
  while (sum > 255)
    sum = (sum & 255) + (sum >> 8);
  out[511] = (uint8_t)sum;
}
static uint32_t rotate(uint32_t v) { return (v >> 13) | (v << 19); }
void arc_boot_layout_nick(const ArcBootNode *nodes, unsigned di,
                          uint8_t out[2048]) {
  const ArcBootNode *d = &nodes[di];
  memset(out, 0, 2048);
  out[0] = out[2042] = 1;
  memcpy(out + 1, "Nick", 4);
  memcpy(out + 2043, "Nick", 4);
  memset(out + 2013, 13, 19);
  memset(out + 2032, 13, 10);
  size_t names = strlen(d->name);
  memcpy(out + 2013, d->name, names);
  memcpy(out + 2032, d->name, names);
  put(out + 2010, nodes[d->parent].address, 3);
  for (unsigned i = 0; i < d->count; ++i) {
    const ArcBootNode *a = &nodes[d->child[i]];
    uint8_t *p = out + 5 + i * 26;
    memset(p, 13, 10);
    memcpy(p, a->name, strlen(a->name));
    put(p + 10, a->load, 4);
    put(p + 14, a->exec, 4);
    put(p + 18, a->directory ? 2048 : a->size, 4);
    put(p + 22, a->address, 3);
    /* OS_File lock bit 3 becomes Nick lock bit 2; Nick bit 3 is directory. */
    p[25] = (uint8_t)((a->attr & 0x33u) | ((a->attr & 8u) >> 1) |
                      (a->directory ? 8u : 0));
  }
  unsigned end = 5 + d->count * 26;
  uint32_t check = 0;
  for (unsigned i = 0; i < (end & ~3u); i += 4)
    check = get(out + i) ^ rotate(check);
  for (unsigned i = end & ~3u; i < end; ++i)
    check = out[i] ^ rotate(check);
  for (unsigned i = 2008; i < 2044; i += 4)
    check = get(out + i) ^ rotate(check);
  out[2047] = (uint8_t)(check ^ (check >> 8) ^ (check >> 16) ^ (check >> 24));
}
bool arc_boot_layout_resolve(const ArcBootLayout *l, uint32_t address,
                             uint32_t offset, uint32_t request,
                             uint32_t *physical, uint32_t *bytes) {
  unsigned id = address >> 8, zone = id == 2 ? 2 : id / 254;
  if (id < 2 || zone >= 5)
    return false;
  uint64_t skip =
      offset + (uint64_t)((address & 255) ? (address & 255) - 1 : 0) * 512;
  for (unsigned turn = 0; turn < 5; ++turn) {
    unsigned z = (zone + turn) % 5;
    for (unsigned i = 0; i < l->count; ++i) {
      const ArcBootExtent *a = &l->extents[i];
      if (a->zone != z || a->id != id)
        continue;
      uint32_t size = a->units * 8192;
      if (skip >= size) {
        skip -= size;
        continue;
      }
      *physical = a->physical + (uint32_t)skip;
      *bytes = size - (uint32_t)skip;
      if (*bytes > request)
        *bytes = request;
      return true;
    }
  }
  return false;
}
