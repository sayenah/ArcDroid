/* ArcDroid boot-resource populator, D-167. GPL-2.0-only.
 * Player packages are streamed through VFS; only the unpublished candidate is
 * written. Existing production routes do not call this module. */
#include "rpc_boot.h"
#include "rpc_boot_assets.h"
#include "rpc_boot_filecore.h"
#include "rpc_boot_zip.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct ArcRpcBoot {
  ArcRpcHdStore *store;
  ArcBootZip zip[2];
  ArcBootNode nodes[ARC_BOOT_NODES];
  ArcBootLayout layout;
  ArcRpcBootStats stats;
  ArcSha256 hash;
  char paths[2][1024], error[320];
  uint8_t block[ARC_BOOT_QUANTUM + 512], directory[2048];
  unsigned phase, package, entry, used, object, asset, part;
  uint32_t offset, pending;
  uint64_t store_bytes, store_operations;
  bool candidate, cancel;
};
_Static_assert(sizeof(ArcRpcBoot) + 262144 <= 8388608,
               "boot population memory cap");
static bool terminal(unsigned phase) { return phase >= ARC_RPC_BOOT_DONE; }
static bool fail(ArcRpcBoot *b, const char *reason) {
  if (reason && !b->error[0])
    snprintf(b->error, sizeof(b->error), "RiscPC boot population: %s", reason);
  for (unsigned p = 0; p < 2; ++p) {
    arc_boot_zip_extract_end(&b->zip[p]);
    (void)arc_boot_zip_close(&b->zip[p]);
  }
  if (b->candidate) {
    arc_riscpc_hd_population_abort(b->store);
    b->candidate = false;
  }
  b->phase = b->cancel ? ARC_RPC_BOOT_CANCELLED : ARC_RPC_BOOT_FAILED;
  return false;
}
static bool named_failure(ArcRpcBoot *b, const char *reason, const char *path) {
  snprintf(b->error, sizeof(b->error), "Boot population %s: %.250s", reason,
           path);
  return false;
}
static unsigned find_child(ArcRpcBoot *b, unsigned parent, const char *name) {
  ArcBootNode *d = &b->nodes[parent];
  for (unsigned i = 0; i < d->count; ++i)
    if (!arc_boot_name_compare(b->nodes[d->child[i]].name, name))
      return d->child[i];
  return UINT32_MAX;
}
static bool legal_name(const char *name) {
  size_t n = strlen(name);
  if (!n || n > 10)
    return false;
  for (size_t i = 0; i < n; ++i) {
    unsigned c = (uint8_t)name[i];
    if (c < 33 || c == 127 || strchr(".:*#$&@^%\\", (int)c))
      return false;
  }
  return true;
}
static bool numeric(const char *s, const char *end) {
  unsigned value = 0;
  if (s == end)
    return false;
  for (; s < end; ++s) {
    if (*s < '0' || *s > '9')
      return false;
    if (value < 500)
      value = value * 10 + (unsigned)(*s - '0');
  }
  return value >= 500;
}
static bool hook(const char *name, bool exclude) {
  size_t n = strlen(name);
  if (n < 7 || (name[0] != 'R' && name[0] != 'r') ||
      (name[1] != 'O' && name[1] != 'o') ||
      arc_boot_name_compare(name + n - 4, "Hook"))
    return false;
  const char *end = name + n - 4;
  if (name + 2 == end)
    return false;
  if (exclude)
    return numeric(name + 2, end);
  for (const char *p = name + 2; p < end; ++p)
    if (*p < '0' || *p > '9')
      return false;
  return true;
}
/* Dot separators are unambiguous because dots in each ZIP component have
 * already been mapped to RISC OS slash characters. */
static bool excluded(char parts[64][256], unsigned count) {
  if (count >= 4 && !arc_boot_name_compare(parts[0], "!Boot") &&
      !arc_boot_name_compare(parts[1], "Resources") &&
      !arc_boot_name_compare(parts[2], "!System") &&
      numeric(parts[3], parts[3] + strlen(parts[3])))
    return true;
  return count >= 2 && !arc_boot_name_compare(parts[0], "!Boot") &&
         hook(parts[1], true);
}
static bool path_for_entry(ArcRpcBoot *b, unsigned p, ArcBootZipEntry *a,
                           char out[256], bool *skip) {
  char parts[64][256];
  unsigned count = 0;
  const char *at = a->name;
  *skip = false;
  while (*at) {
    const char *end = strchr(at, '/');
    if (!end)
      end = at + strlen(at);
    size_t len = (size_t)(end - at);
    if (!len || count == 64 || len >= 256)
      return named_failure(b, "invalid depth", a->name);
    memcpy(parts[count], at, len);
    parts[count][len] = 0;
    for (unsigned i = 0; i < len; ++i)
      if (parts[count][i] == '.')
        parts[count][i] = '/';
    ++count;
    at = *end ? end + 1 : end;
  }
  if (!count)
    return named_failure(b, "empty path", a->name);
  unsigned first = 0;
  if (p == 0 && !arc_boot_name_compare(parts[0], "HardDisc4"))
    first = 1;
  else if (p == 1 && !arc_boot_name_compare(parts[0], "!System")) {
    if (count > 60)
      return named_failure(b, "depth cap", a->name);
    memmove(parts + 3, parts, count * sizeof(parts[0]));
    strcpy(parts[0], "!Boot");
    strcpy(parts[1], "Resources");
    strcpy(parts[2], "!System");
    /* Original !System now occurs twice; remove its duplicate component. */
    memmove(parts + 3, parts + 4, (count - 1) * sizeof(parts[0]));
    count += 2;
  } else if (p == 1 && !arc_boot_name_compare(parts[0], "!SysMerge")) {
    *skip = true;
    return true;
  } else if (count == 1 && !a->directory) {
    if (count > 60)
      return false;
    strcpy(parts[2], parts[0]);
    strcpy(parts[0], "RelDocs");
    strcpy(parts[1], p ? "PlingSys" : "HardDisc4");
    count = 3;
  } else
    return named_failure(b, "unrecognised package structure", a->name);
  if (first) {
    memmove(parts, parts + first, (count - first) * sizeof(parts[0]));
    count -= first;
  }
  if (!count) {
    if (!a->directory)
      return named_failure(b, "archive root is a file", a->name);
    *skip = true;
    return true;
  }
  if (excluded(parts, count)) {
    *skip = true;
    return true;
  }
  if (count >= 2 && !arc_boot_name_compare(parts[0], "!Boot") &&
      (!arc_boot_name_compare(parts[1], "!Boot") ||
       !arc_boot_name_compare(parts[1], "!Run") ||
       !arc_boot_name_compare(parts[1], "PreDesktop") ||
       hook(parts[1], false))) {
    if (count == 64)
      return named_failure(b, "depth cap", a->name);
    memmove(parts + 2, parts + 1, (count - 1) * sizeof(parts[0]));
    strcpy(parts[1], "Inactive");
    ++count;
  }
  if (count > ARC_BOOT_DEPTH)
    return named_failure(b, "depth cap", a->name);
  size_t length = 0;
  out[0] = 0;
  for (unsigned i = 0; i < count; ++i) {
    if (!legal_name(parts[i]))
      return named_failure(b, "unrepresentable name", a->name);
    size_t n = strlen(parts[i]);
    if (length + n + (i ? 1 : 0) >= 256)
      return named_failure(b, "path cap", a->name);
    if (i)
      out[length++] = '.';
    memcpy(out + length, parts[i], n);
    length += n;
    out[length] = 0;
  }
  return true;
}
static bool same_file(const ArcBootNode *a, const ArcBootZipEntry *z) {
  if (a->size != z->size || memcmp(a->hash, z->hash, 32) || a->attr != z->attr)
    return false;
  bool typed = (a->load & 0xfff00000u) == 0xfff00000u &&
               (z->load & 0xfff00000u) == 0xfff00000u;
  return typed ? ((a->load >> 8) & 4095) == ((z->load >> 8) & 4095)
               : (a->load == z->load && a->exec == z->exec);
}
static bool add(ArcRpcBoot *b, const char *path, bool directory,
                unsigned source, unsigned member, const ArcBootZipEntry *z,
                unsigned asset) {
  char copy[256];
  snprintf(copy, sizeof(copy), "%s", path);
  char *part = copy;
  unsigned parent = 0;
  for (unsigned depth = 0;; ++depth) {
    char *end = strchr(part, '.');
    if (end)
      *end = 0;
    bool dir = end || directory;
    if (depth >= ARC_BOOT_DEPTH || !legal_name(part))
      return named_failure(b, "unrepresentable path", path);
    unsigned index = find_child(b, parent, part);
    if (index == UINT32_MAX) {
      ArcBootNode *d = &b->nodes[parent];
      if (b->used == ARC_BOOT_NODES || d->count == 77)
        return named_failure(b, "node or directory cap", path);
      index = b->used++;
      ArcBootNode *a = &b->nodes[index];
      memset(a, 0, sizeof(*a));
      strcpy(a->name, part);
      a->parent = parent;
      a->directory = dir;
      a->attr = 3;
      a->size = dir ? 2048 : 0;
      d->child[d->count++] = index;
    } else if (strcmp(b->nodes[index].name, part))
      return named_failure(b, "case collision", path);
    ArcBootNode *a = &b->nodes[index];
    if (a->directory != dir)
      return named_failure(b, "file/directory collision", path);
    if (source < 2)
      a->supplied |= (uint8_t)(1u << source);
    if (end) {
      parent = index;
      part = end + 1;
      continue;
    }
    if (dir) {
      if (z) {
        if (a->explicit_meta & (1u << source))
          return named_failure(b, "duplicate directory", path);
        if (source == 0 || !(a->supplied & 1)) {
          a->load = z->load;
          a->exec = z->exec;
          a->attr = z->attr;
        }
        a->explicit_meta |= (uint8_t)(1u << source);
      }
      return true;
    }
    if (a->supplied != (uint8_t)(1u << source) && source < 2) {
      if (!z || !same_file(a, z))
        return named_failure(b, "differing overlap", path);
      uint64_t old = ((uint64_t)(a->load & 255) << 32) | a->exec,
               newer = ((uint64_t)(z->load & 255) << 32) | z->exec;
      if ((a->load & 0xfff00000u) == 0xfff00000u && newer > old) {
        a->load = z->load;
        a->exec = z->exec;
      }
      return true;
    }
    if (a->authored || a->size || a->explicit_meta)
      return named_failure(b, "duplicate destination", path);
    a->source = source;
    a->member = member;
    a->asset = asset;
    if (z) {
      a->size = z->size;
      a->load = z->load;
      a->exec = z->exec;
      a->attr = z->attr;
      memcpy(a->hash, z->hash, 32);
      a->explicit_meta = (uint8_t)(1u << source);
    } else {
      const ArcBootAsset *s = &arc_boot_assets[asset];
      a->authored = true;
      a->size = (uint32_t)s->size;
      a->load = 0xfff00000u | (s->type << 8);
      a->attr = 3;
      ArcSha256 h;
      arc_sha256_init(&h);
      arc_sha256_update(&h, s->data, s->size);
      arc_sha256_final(&h, a->hash);
    }
    return true;
  }
}
static unsigned find_path(ArcRpcBoot *b, const char *path) {
  char copy[256];
  snprintf(copy, sizeof(copy), "%s", path);
  unsigned at = 0;
  for (char *p = copy; p;) {
    char *end = strchr(p, '.');
    if (end)
      *end = 0;
    at = find_child(b, at, p);
    if (at == UINT32_MAX)
      return at;
    p = end ? end + 1 : NULL;
  }
  return at;
}
static bool bootstrap(ArcRpcBoot *b) {
  static const char *const required[] = {
      "!Boot.Inactive.!Boot", "!Boot.Inactive.!Run", "!Boot.Themes.!Sprites",
      "!Boot.Resources.!System.SysPaths", "!Boot.Resources.!System.!Boot"};
  unsigned system = find_path(b, "!Boot.Resources.!System");
  if (system == UINT32_MAX || !b->nodes[system].directory ||
      b->nodes[system].supplied != 3)
    return fail(b, "missing required System tree from one package");
  for (unsigned i = 0; i < sizeof(required) / sizeof(*required); ++i) {
    unsigned at = find_path(b, required[i]);
    if (at == UINT32_MAX || b->nodes[at].directory)
      return named_failure(b, "missing required bootstrap member", required[i]);
  }
  return true;
}
static bool candidate_io(ArcRpcBoot *b, ArcBootNode *node, uint32_t offset,
                         uint8_t *data, unsigned n, bool read) {
  while (n) {
    uint32_t at, take;
    if (!arc_boot_layout_resolve(&b->layout, node->address, offset, n, &at,
                                 &take) ||
        at % 512 || take % 512)
      return false;
    if (!(read ? arc_riscpc_hd_population_read(b->store, at / 512, take / 512,
                                               data)
               : arc_riscpc_hd_population_write(b->store, at / 512, take / 512,
                                                data)))
      return false;
    n -= take;
    offset += take;
    data += take;
  }
  return true;
}
static bool format_part(ArcRpcBoot *b, bool read) {
  uint32_t at, bytes;
  const uint8_t *expected;
  if (b->part == 0) {
    arc_boot_layout_boot(b->block);
    at = 3072;
    bytes = 512;
    expected = b->block;
  } else {
    at = ARC_BOOT_MAP_AT + (b->part - 1) * 2560;
    bytes = 2560;
    expected = b->layout.map;
  }
  if (read) {
    if (!arc_riscpc_hd_population_read(b->store, at / 512, bytes / 512,
                                       b->zip[0].output) ||
        memcmp(b->zip[0].output, expected, bytes))
      return false;
  } else if (!arc_riscpc_hd_population_write(b->store, at / 512, bytes / 512,
                                             expected))
    return false;
  return true;
}
static bool object_step(ArcRpcBoot *b, bool read) {
  ArcBootNode *node = &b->nodes[b->object];
  if (node->directory) {
    arc_boot_layout_nick(b->nodes, b->object, b->directory);
    if (read) {
      if (!candidate_io(b, node, 0, b->block, 2048, true) ||
          memcmp(b->block, b->directory, 2048))
        return false;
    } else if (!candidate_io(b, node, 0, b->directory, 2048, false))
      return false;
    ++b->object;
    return true;
  }
  if (!node->size) {
    ++b->object;
    return true;
  }
  if (read) {
    unsigned bytes = node->size - b->offset;
    if (bytes > ARC_BOOT_QUANTUM)
      bytes = ARC_BOOT_QUANTUM;
    if (!b->offset)
      arc_sha256_init(&b->hash);
    if (!candidate_io(b, node, b->offset, b->block, (bytes + 511) & ~511u,
                      true))
      return false;
    arc_sha256_update(&b->hash, b->block, bytes);
    b->offset += bytes;
    if (b->offset == node->size) {
      uint8_t digest[32];
      arc_sha256_final(&b->hash, digest);
      if (memcmp(digest, node->hash, 32))
        return false;
      ++b->object;
      b->offset = 0;
    }
    return true;
  }
  size_t bytes;
  bool done;
  if (node->authored) {
    const ArcBootAsset *a = &arc_boot_assets[node->asset];
    bytes = a->size;
    memcpy(b->block + b->pending, a->data, bytes);
    done = true;
  } else {
    ArcBootZip *z = &b->zip[node->source];
    if (!z->extracting_active && !arc_boot_zip_extract_begin(z, node->member))
      return false;
    if (!arc_boot_zip_extract_step(z, &bytes, &done, b->error,
                                   sizeof(b->error)))
      return false;
    memcpy(b->block + b->pending, z->output, bytes);
    if (done) {
      uint8_t digest[32];
      arc_sha256_final(&z->hash, digest);
      arc_boot_zip_extract_end(z);
      if (memcmp(digest, node->hash, 32))
        return false;
    }
  }
  b->pending += (unsigned)bytes;
  /* A deflate input quantum may produce a partial sector before stream end.
   * Commit complete sectors; retain at most 511 bytes across work units. */
  unsigned payload = done ? b->pending : b->pending & ~511u;
  unsigned sectors = done ? (payload + 511u) & ~511u : payload;
  if (sectors) {
    memset(b->block + payload, 0, sectors - payload);
    if (!candidate_io(b, node, b->offset, b->block, sectors, false))
      return false;
    b->offset += payload;
  }
  b->pending -= payload;
  if (b->pending)
    memmove(b->block, b->block + payload, b->pending);
  if (done) {
    if (b->offset != node->size)
      return false;
    ++b->object;
    b->offset = 0;
  }
  return true;
}
static bool unit(ArcRpcBoot *b) {
  ArcBootZip *z = &b->zip[b->package];
  if (b->cancel)
    return fail(b, "cancelled");
  switch (b->phase) {
  case ARC_RPC_BOOT_OPEN:
    if (!arc_boot_zip_open(z, b->paths[b->package], b->error, sizeof(b->error)))
      return false;
    arc_sha256_init(&b->hash);
    b->offset = 0;
    b->phase = ARC_RPC_BOOT_HASH;
    return true;
  case ARC_RPC_BOOT_HASH:
  case ARC_RPC_BOOT_REHASH: {
    unsigned bytes = z->length - b->offset;
    if (bytes > ARC_BOOT_QUANTUM)
      bytes = ARC_BOOT_QUANTUM;
    if (bytes && !arc_boot_zip_read(z, b->offset, b->block, bytes))
      return false;
    arc_sha256_update(&b->hash, b->block, bytes);
    if (b->phase == ARC_RPC_BOOT_HASH)
      arc_boot_zip_pin(z, b->offset, b->block, bytes);
    b->offset += bytes;
    if (b->offset < z->length)
      return true;
    uint8_t digest[32];
    arc_sha256_final(&b->hash, digest);
    b->offset = 0;
    if (b->phase == ARC_RPC_BOOT_HASH) {
      memcpy(b->stats.packages[b->package], digest, 32);
      z->pinned = true;
      b->phase = ARC_RPC_BOOT_INDEX;
      b->entry = 0;
    } else {
      if (memcmp(digest, b->stats.packages[b->package], 32))
        return fail(b, "package changed during population");
      if (!arc_boot_zip_close(z))
        return false;
      if (++b->package == 2) {
        b->phase = ARC_RPC_BOOT_PUBLISH;
        b->package = 0;
      } else {
        z = &b->zip[b->package];
        if (!arc_boot_zip_open(z, b->paths[b->package], b->error,
                               sizeof(b->error)))
          return false;
        arc_sha256_init(&b->hash);
      }
    }
    return true;
  }
  case ARC_RPC_BOOT_INDEX:
    if (!b->entry) {
      if (!arc_boot_zip_directory(z, b->error, sizeof(b->error)))
        return false;
      b->entry = 1;
      return true;
    }
    if (z->indexed < z->count)
      return arc_boot_zip_index_step(z, b->error, sizeof(b->error));
    if (b->zip[0].expanded + b->zip[1].expanded > ARC_BOOT_EXPANDED_CAP)
      return fail(b, "combined expanded byte cap");
    b->phase = ARC_RPC_BOOT_PREFLIGHT;
    b->entry = 0;
    return true;
  case ARC_RPC_BOOT_PREFLIGHT: {
    if (b->entry == z->count) {
      if (++b->package < 2)
        b->phase = ARC_RPC_BOOT_OPEN;
      else {
        b->phase = ARC_RPC_BOOT_TREE;
        b->package = b->entry = 0;
      }
      return true;
    }
    if (!z->extracting_active && !arc_boot_zip_extract_begin(z, b->entry))
      return false;
    size_t bytes;
    bool done;
    if (!arc_boot_zip_extract_step(z, &bytes, &done, b->error,
                                   sizeof(b->error)))
      return false;
    if (done) {
      arc_sha256_final(&z->hash, z->entries[b->entry].hash);
      arc_boot_zip_extract_end(z);
      ++b->entry;
    }
    return true;
  }
  case ARC_RPC_BOOT_TREE: {
    if (b->entry == z->count) {
      if (++b->package < 2) {
        b->entry = 0;
        return true;
      }
      b->package = 0;
      if (!bootstrap(b))
        return false;
      b->phase = ARC_RPC_BOOT_ASSETS;
      return true;
    }
    ArcBootZipEntry *a = &z->entries[b->entry];
    char path[256];
    bool skip;
    if (!path_for_entry(b, b->package, a, path, &skip))
      return false;
    if (!skip && !add(b, path, a->directory, b->package, b->entry, a, 0))
      return false;
    ++b->entry;
    return true;
  }
  case ARC_RPC_BOOT_ASSETS:
    if (b->asset < ARC_BOOT_ASSET_COUNT) {
      const ArcBootAsset *a = &arc_boot_assets[b->asset];
      if (find_path(b, a->path) != UINT32_MAX)
        return named_failure(b, "authored asset collision", a->path);
      if (!add(b, a->path, false, 2, 0, NULL, b->asset++))
        return false;
      return true;
    }
    arc_boot_layout_init(&b->layout);
    b->phase = ARC_RPC_BOOT_ALLOCATE;
    b->object = 0;
    return true;
  case ARC_RPC_BOOT_ALLOCATE:
    if (b->object < b->used) {
      ArcBootNode *node = &b->nodes[b->object];
      if (node->directory &&
          !arc_boot_layout_directory(&b->layout, b->nodes, b->object))
        return fail(b, "FileCore capacity exhausted");
      ++b->object;
      return true;
    }
    arc_boot_layout_finish(&b->layout);
    if (!arc_riscpc_hd_population_begin(b->store))
      return fail(b, "store is not available for offline creation");
    b->candidate = true;
    b->phase = ARC_RPC_BOOT_FORMAT;
    b->part = 0;
    return true;
  case ARC_RPC_BOOT_FORMAT:
    if (b->part < 3) {
      if (!format_part(b, false))
        return false;
      ++b->part;
      return true;
    }
    b->phase = ARC_RPC_BOOT_WRITE;
    b->object = b->offset = 0;
    return true;
  case ARC_RPC_BOOT_WRITE:
    if (b->object < b->used)
      return object_step(b, false);
    b->phase = ARC_RPC_BOOT_VALIDATE;
    b->object = b->offset = b->part = 0;
    return true;
  case ARC_RPC_BOOT_VALIDATE:
    if (b->part < 3) {
      if (!format_part(b, true))
        return false;
      ++b->part;
      return true;
    }
    if (b->object < b->used)
      return object_step(b, true);
    for (unsigned p = 0; p < 2; ++p)
      if (!arc_boot_zip_close(&b->zip[p]))
        return false;
    b->package = 0;
    b->phase = ARC_RPC_BOOT_RECEIPT;
    return true;
  case ARC_RPC_BOOT_RECEIPT: {
    ArcRpcHdReceipt r = {0};
    memcpy(r.packages, b->stats.packages, 64);
    for (unsigned i = 0; i < b->used; ++i)
      if (!b->nodes[i].directory) {
        ++r.files;
        r.bytes += b->nodes[i].size;
      }
    b->stats.files = r.files;
    b->stats.file_bytes = r.bytes;
    if (!arc_riscpc_hd_population_receipt(b->store, &r))
      return false;
    b->phase = ARC_RPC_BOOT_SEAL;
    return true;
  }
  case ARC_RPC_BOOT_SEAL: {
    bool done;
    if (!arc_riscpc_hd_population_seal_step(b->store, &done))
      return false;
    if (done) {
      b->package = b->offset = 0;
      if (!arc_boot_zip_open(&b->zip[0], b->paths[0], b->error,
                             sizeof(b->error)))
        return false;
      arc_sha256_init(&b->hash);
      b->phase = ARC_RPC_BOOT_REHASH;
    }
    return true;
  }
  case ARC_RPC_BOOT_PUBLISH:
    if (!arc_riscpc_hd_population_publish(b->store))
      return false;
    b->candidate = false;
    b->phase = ARC_RPC_BOOT_DONE;
    return true;
  default:
    return false;
  }
}
static void counts(ArcRpcBoot *b, uint64_t *bytes, uint64_t *ops) {
  ArcRpcHdStats s;
  arc_riscpc_hd_stats(b->store, &s);
  *bytes = s.bytes_read + s.bytes_written - b->store_bytes;
  *ops = s.operations - b->store_operations;
  for (unsigned p = 0; p < 2; ++p) {
    *bytes += b->zip[p].io_bytes;
    *ops += b->zip[p].operations;
  }
}
ArcRpcBoot *arc_riscpc_boot_begin(ArcRpcHdStore *store, const char *system,
                                  char *error, size_t n) {
  if (!store) {
    if (error && n)
      snprintf(error, n, "Boot population requires a store");
    return NULL;
  }
  ArcRpcBoot *b = calloc(1, sizeof(*b));
  if (!b) {
    if (error && n)
      snprintf(error, n, "Boot population allocation failed");
    return NULL;
  }
  b->store = store;
  ArcRpcHdStats s;
  arc_riscpc_hd_stats(store, &s);
  b->store_bytes = s.bytes_read + s.bytes_written;
  b->store_operations = s.operations;
  b->stats.memory_high = sizeof(*b);
  b->used = 1;
  strcpy(b->nodes[0].name, "$");
  b->nodes[0].directory = true;
  b->nodes[0].size = 2048;
  b->nodes[0].attr = 3;
  if (s.published) {
    ArcRpcHdReceipt r;
    if (arc_riscpc_hd_receipt(store, &r)) {
      b->phase = ARC_RPC_BOOT_DONE;
      memcpy(b->stats.packages, r.packages, 64);
      b->stats.files = r.files;
      b->stats.file_bytes = r.bytes;
    } else {
      b->phase = ARC_RPC_BOOT_LEGACY;
      strcpy(b->error, "The existing RiscPC hard disc has no creation receipt; "
                       "it was left unchanged.");
    }
    return b;
  }
  if (s.read_only) {
    fail(b, "store is read-only");
    return b;
  }
  if (!system || !*system) {
    fail(b, "system directory is missing");
    return b;
  }
  const char *names[] = {"HardDisc4.zip", "PlingSystem.zip"};
  for (unsigned p = 0; p < 2; ++p) {
    int length = snprintf(b->paths[p], sizeof(b->paths[p]),
                          "%s/arcdroid/riscpc/boot/%s", system, names[p]);
    if (length < 0 || (size_t)length >= sizeof(b->paths[p])) {
      fail(b, "package path is too long");
      break;
    }
  }
  return b;
}
ArcRpcBootState arc_riscpc_boot_step(ArcRpcBoot *b, unsigned units) {
  if (!b)
    return ARC_RPC_BOOT_FAILED;
  if (units > 32)
    units = 32;
  for (unsigned i = 0; i < units && !terminal(b->phase); ++i) {
    uint64_t bytes, ops, after_bytes, after_ops;
    counts(b, &bytes, &ops);
    bool ok = unit(b);
    if (!ok && !terminal(b->phase))
      fail(b, "I/O, validation or allocation failure");
    counts(b, &after_bytes, &after_ops);
    ++b->stats.units;
    if (after_bytes - bytes > b->stats.max_unit_io_bytes)
      b->stats.max_unit_io_bytes = after_bytes - bytes;
    if (after_ops - ops > b->stats.max_unit_operations)
      b->stats.max_unit_operations = after_ops - ops;
    b->stats.io_bytes = after_bytes;
    b->stats.operations = after_ops;
    uint64_t memory = sizeof(*b) + b->zip[0].zhigh + b->zip[1].zhigh;
    if (memory > b->stats.memory_high)
      b->stats.memory_high = memory;
  }
  return (ArcRpcBootState)b->phase;
}
void arc_riscpc_boot_cancel(ArcRpcBoot *b) {
  if (b && !terminal(b->phase))
    b->cancel = true;
}
void arc_riscpc_boot_free(ArcRpcBoot *b) {
  if (b) {
    if (!terminal(b->phase)) {
      b->cancel = true;
      fail(b, "cancelled");
    }
    free(b);
  }
}
void arc_riscpc_boot_stats(const ArcRpcBoot *b, ArcRpcBootStats *out) {
  *out = b->stats;
  out->phase = b->phase;
  out->nodes = b->used;
  out->entries = b->zip[0].count + b->zip[1].count;
  out->allocated_bytes = b->layout.allocated_bytes;
  static const char *const hashes[] = {
      "28613a8ee25bf8a207d10409f48b8f7f1af06d7bfcee527a69e2f7d2d15547b5",
      "0259063da9eb3eb85a5ebed9fc37400ab5c66417a886939f3e8c9ddd8dbb9019"};
  for (unsigned p = 0; p < 2; ++p) {
    char hex[65];
    for (unsigned i = 0; i < 32; ++i)
      snprintf(hex + 2 * i, 3, "%02x", out->packages[p][i]);
    out->qualified[p] = !strcmp(hex, hashes[p]);
  }
}
const char *arc_riscpc_boot_notice(const ArcRpcBoot *b) {
  return b && b->error[0] ? b->error : NULL;
}
