/* Bounded reader and synthetic media for a user-supplied ADFFS package.
 * No ADFFS source or binary data is incorporated here. GPL-2.0-only. */
#include "adffs.h"
#include "arcdroid_core.h"
#include "zlib.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  ZIP_MAX = 8u * 1024u * 1024u,
  ZIP_ENTRIES = 1024u,
  ZIP_MEMBER = 2u * 1024u * 1024u,
  ZIP_USED_TOTAL = 4u * 1024u * 1024u,
  D_SECTOR = 256u,
  D_ROOT = 1024u,
  D_DIRECTORY = 2048u
};

static uint16_t le16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static void put24(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
}
static void fail(char *e, size_t n, const char *s) {
  if (e && n)
    snprintf(e, n, "%s", s);
}
static int foldcmp(const char *a, const char *b) {
  while (*a && *b) {
    int x = tolower((unsigned char)*a++), y = tolower((unsigned char)*b++);
    if (x != y)
      return x - y;
  }
  return (unsigned char)*a - (unsigned char)*b;
}
static bool foldprefix(const char *s, const char *prefix) {
  while (*prefix)
    if (tolower((unsigned char)*s++) != tolower((unsigned char)*prefix++))
      return false;
  return true;
}

typedef struct {
  char name[256];
  uint16_t flags, method;
  uint32_t crc, packed, size, local;
  const uint8_t *extra;
  uint16_t extra_size;
} Ze;
static voidpf zalloc_fn(voidpf opaque, uInt items, uInt size) {
  (void)opaque;
  if (size && items > SIZE_MAX / size)
    return Z_NULL;
  return calloc(items, size);
}
static void zfree_fn(voidpf opaque, voidpf p) {
  (void)opaque;
  free(p);
}

static bool safe_name(const char *s) {
  if (!s[0] || s[0] == '/' || s[0] == '\\' || strchr(s, '\\'))
    return false;
  const char *p = s;
  while (*p) {
    const char *q = strchr(p, '/');
    size_t n = q ? (size_t)(q - p) : strlen(p);
    if (!n || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.'))
      return false;
    if (!q)
      break;
    p = q + 1;
  }
  return true;
}

static bool used_name(const char *n) {
  static const char *const exact[] = {
      "!ADFFS/ADFFS",         "!ADFFS/SetVars",
      "!ADFFS/3rdParty/CLib", "!ADFFS/3rdParty/CallASWI",
      "!ADFFS/3rdParty/ZLib", "!ADFFS/3rdParty/BootCmds",
      "!ADFFS/obey",          "!ADFFS/extras/JoyMod",
      "!ADFFS/extras/LZWD",   "!ADFFS/extras/LZWD103"};
  for (unsigned i = 0; i < sizeof(exact) / sizeof(exact[0]); ++i)
    if (!foldcmp(n, exact[i]))
      return true;
  return foldprefix(n, "!ADFFS/trackers/") &&
         foldcmp(n, "!ADFFS/trackers/TimPlayer") &&
         foldcmp(n, "!ADFFS/trackers/!Help") && n[16];
}

static bool arc_extra(const uint8_t *p, size_t n, uint32_t *load,
                      uint32_t *exec, uint32_t *attr) {
  while (n >= 4u) {
    uint16_t id = le16(p), z = le16(p + 2);
    p += 4;
    n -= 4;
    if (z > n)
      return false;
    if (id == 0x4341u && z >= 16u && !memcmp(p, "ARC0", 4u)) {
      *load = le32(p + 4);
      *exec = le32(p + 8);
      *attr = le32(p + 12);
      return true;
    }
    p += z;
    n -= z;
  }
  return false;
}

static bool extra_has(const uint8_t *p, size_t n, uint16_t wanted) {
  while (n >= 4u) {
    uint16_t id = le16(p), z = le16(p + 2);
    p += 4;
    n -= 4;
    if (z > n)
      return false;
    if (id == wanted)
      return true;
    p += z;
    n -= z;
  }
  return false;
}

static bool zip_index(const uint8_t *a, size_t n, Ze **out, unsigned *count,
                      char *e, size_t en) {
  if (!a || n > ZIP_MAX || n < 22u) {
    fail(e, en, n > ZIP_MAX ? "archive exceeds 8 MiB" : "truncated archive");
    return false;
  }
  size_t low = n > 65557u ? n - 65557u : 0u, eo = n;
  for (size_t p = n - 22u;; --p) {
    if (le32(a + p) == 0x06054b50u) {
      eo = p;
      break;
    }
    if (p == low)
      break;
  }
  if (eo == n) {
    fail(e, en, "missing central directory");
    return false;
  }
  unsigned total = le16(a + eo + 10);
  uint32_t csize = le32(a + eo + 12), coff = le32(a + eo + 16);
  if (le16(a + eo + 4) || le16(a + eo + 6) || le16(a + eo + 8) != total ||
      total > ZIP_ENTRIES || total == 0xffffu || csize == 0xffffffffu ||
      coff == 0xffffffffu) {
    fail(e, en,
         (total == 0xffffu || csize == 0xffffffffu || coff == 0xffffffffu)
             ? "zip64 archive is unsupported"
         : total > ZIP_ENTRIES ? "archive has more than 1024 entries"
                               : "multi-disk archive is unsupported");
    return false;
  }
  if ((size_t)coff + csize > eo) {
    fail(e, en, "central directory is out of bounds");
    return false;
  }
  Ze *z = calloc(total ? total : 1u, sizeof(*z));
  if (!z) {
    fail(e, en, "out of memory");
    return false;
  }
  size_t p = coff;
  for (unsigned i = 0; i < total; ++i) {
    if (p + 46u > eo || le32(a + p) != 0x02014b50u) {
      fail(e, en, "truncated central entry");
      free(z);
      return false;
    }
    uint16_t nl = le16(a + p + 28), xl = le16(a + p + 30),
             cl = le16(a + p + 32);
    if (!nl || nl >= sizeof(z[i].name) || p + 46u + nl + xl + cl > eo) {
      fail(e, en, "invalid central entry length");
      free(z);
      return false;
    }
    memcpy(z[i].name, a + p + 46u, nl);
    z[i].name[nl] = 0;
    if (!safe_name(z[i].name)) {
      fail(e, en, "unsafe archive name");
      free(z);
      return false;
    }
    z[i].flags = le16(a + p + 8);
    z[i].method = le16(a + p + 10);
    z[i].crc = le32(a + p + 16);
    z[i].packed = le32(a + p + 20);
    z[i].size = le32(a + p + 24);
    z[i].extra = a + p + 46u + nl;
    z[i].extra_size = xl;
    z[i].local = le32(a + p + 42);
    if (extra_has(z[i].extra, z[i].extra_size, 1u)) {
      fail(e, en, "zip64 archive is unsupported");
      free(z);
      return false;
    }
    if (z[i].flags & 1u) {
      fail(e, en, "encrypted entry");
      free(z);
      return false;
    }
    if (z[i].method != 0u && z[i].method != 8u) {
      fail(e, en, "unsupported compression method");
      free(z);
      return false;
    }
    if (z[i].size > ZIP_MEMBER) {
      fail(e, en, "member exceeds 2 MiB");
      free(z);
      return false;
    }
    p += 46u + nl + xl + cl;
  }
  if (p != (size_t)coff + csize) {
    fail(e, en, "central directory size mismatch");
    free(z);
    return false;
  }
  *out = z;
  *count = total;
  return true;
}

static bool extract(const uint8_t *a, size_t n, const Ze *z, uint8_t **data,
                    char *e, size_t en) {
  size_t p = z->local;
  if (p + 30u > n || le32(a + p) != 0x04034b50u) {
    fail(e, en, "local header is missing");
    return false;
  }
  uint16_t flags = le16(a + p + 6), method = le16(a + p + 8),
           nl = le16(a + p + 26), xl = le16(a + p + 28);
  uint32_t crc = le32(a + p + 14), pk = le32(a + p + 18), sz = le32(a + p + 22);
  if (p + 30u + nl + xl > n || strlen(z->name) != nl ||
      memcmp(a + p + 30u, z->name, nl) || flags != z->flags ||
      method != z->method ||
      (!(flags & 8u) && (crc != z->crc || pk != z->packed || sz != z->size)) ||
      ((flags & 8u) && ((crc && crc != z->crc) || (pk && pk != z->packed) ||
                        (sz && sz != z->size)))) {
    fail(e, en, "local/central header mismatch");
    return false;
  }
  const uint8_t *local_extra = a + p + 30u + nl;
  if (xl != z->extra_size || memcmp(local_extra, z->extra, xl)) {
    fail(e, en, "local/central header mismatch");
    return false;
  }
  if (extra_has(local_extra, xl, 1u)) {
    fail(e, en, "zip64 archive is unsupported");
    return false;
  }
  const uint8_t *src = local_extra + xl;
  if (z->packed > n - (size_t)(src - a)) {
    fail(e, en, "truncated member data");
    return false;
  }
  if (flags & 8u) {
    size_t dd = (size_t)(src - a) + z->packed;
    if (dd + 12u > n) {
      fail(e, en, "truncated data descriptor");
      return false;
    }
    if (le32(a + dd) == 0x08074b50u) {
      dd += 4u;
      if (dd + 12u > n) {
        fail(e, en, "truncated data descriptor");
        return false;
      }
    }
    if (le32(a + dd) != z->crc || le32(a + dd + 4u) != z->packed ||
        le32(a + dd + 8u) != z->size) {
      fail(e, en, "data descriptor mismatch");
      return false;
    }
  }
  uint8_t *dst = malloc(z->size ? z->size : 1u);
  if (!dst) {
    fail(e, en, "out of memory");
    return false;
  }
  if (z->method == 0u) {
    if (z->packed != z->size) {
      free(dst);
      fail(e, en, "stored member size mismatch");
      return false;
    }
    memcpy(dst, src, z->size);
  } else {
    z_stream s;
    memset(&s, 0, sizeof(s));
    s.zalloc = zalloc_fn;
    s.zfree = zfree_fn;
    s.next_in = (Bytef *)(uintptr_t)src;
    s.avail_in = z->packed;
    s.next_out = dst;
    s.avail_out = z->size;
    if (inflateInit2(&s, -15) != Z_OK) {
      free(dst);
      fail(e, en, "inflater initialization failed");
      return false;
    }
    int r = inflate(&s, Z_FINISH);
    bool ok =
        r == Z_STREAM_END && s.total_in == z->packed && s.total_out == z->size;
    inflateEnd(&s);
    if (!ok) {
      free(dst);
      fail(e, en, "inflated-size mismatch");
      return false;
    }
  }
  if (arc_crc32(dst, z->size) != z->crc) {
    free(dst);
    fail(e, en, "member CRC32 mismatch");
    return false;
  }
  *data = dst;
  return true;
}

static bool obey_index(const ArcAdffsMember *obey, Ze **entries,
                       unsigned *count, char *e, size_t en) {
  if (!zip_index(obey->data, obey->size, entries, count, e, en))
    return false;
  for (unsigned i = 0; i < *count; ++i) {
    if ((*entries)[i].name[0] != 'F' || strlen((*entries)[i].name) != 8u) {
      fail(e, en, "invalid obey member name");
      free(*entries);
      return false;
    }
    for (unsigned k = 1; k < 8u; ++k)
      if (!isdigit((unsigned char)(*entries)[i].name[k])) {
        fail(e, en, "invalid obey member name");
        free(*entries);
        return false;
      }
    for (unsigned j = 0; j < i; ++j)
      if (!strcmp((*entries)[i].name, (*entries)[j].name)) {
        fail(e, en, "duplicate obey member name");
        free(*entries);
        return false;
      }
  }
  return true;
}

static const char *const runtime_names[6] = {
    "!ADFFS/ADFFS",         "!ADFFS/SetVars",
    "!ADFFS/3rdParty/CLib", "!ADFFS/3rdParty/CallASWI",
    "!ADFFS/3rdParty/ZLib", "!ADFFS/3rdParty/BootCmds"};

static void module_help_version(const ArcAdffsMember *module, char output[64]) {
  snprintf(output, 64u, "unknown");
  if (!module || module->size < 24u)
    return;
  uint32_t offset = le32(module->data + 20u);
  if (offset >= module->size)
    return;
  size_t at = 0u;
  while (offset < module->size && module->data[offset] && at + 1u < 64u) {
    unsigned char c = module->data[offset++];
    if (c == '\t')
      c = ' ';
    if (c < 0x20u || c > 0x7eu || c == '"' || c == '\\') {
      snprintf(output, 64u, "unknown");
      return;
    }
    output[at++] = (char)c;
  }
  if (!at || offset >= module->size || module->data[offset]) {
    snprintf(output, 64u, "unknown");
    return;
  }
  output[at] = 0;
}

bool arc_adffs_package_parse(const uint8_t *a, size_t n, ArcAdffsPackage *pkg,
                             char *e, size_t en) {
  if (!pkg)
    return false;
  memset(pkg, 0, sizeof(*pkg));
  Ze *z = NULL;
  unsigned zn = 0;
  if (!zip_index(a, n, &z, &zn, e, en))
    return false;
  size_t total = 0;
  for (unsigned i = 0; i < zn; ++i)
    if (used_name(z[i].name))
      for (unsigned j = 0; j < i; ++j)
        if (used_name(z[j].name) && !foldcmp(z[i].name, z[j].name)) {
          fail(e, en,
               !strcmp(z[i].name, z[j].name) ? "duplicate used name"
                                             : "case-colliding used names");
          free(z);
          return false;
        }
  for (unsigned i = 0; i < zn; ++i)
    if (used_name(z[i].name)) {
      if (pkg->count == ARC_ADFFS_MAX_MEMBERS ||
          z[i].size > ZIP_USED_TOTAL - total) {
        fail(e, en, "used members exceed limits");
        goto bad;
      }
      ArcAdffsMember *m = &pkg->members[pkg->count];
      if (!arc_extra(z[i].extra, z[i].extra_size, &m->load, &m->exec,
                     &m->attr)) {
        fail(e, en, "used member lacks ARC0 metadata");
        goto bad;
      }
      if (!extract(a, n, &z[i], &m->data, e, en))
        goto bad;
      snprintf(m->name, sizeof(m->name), "%s", z[i].name);
      m->size = z[i].size;
      total += m->size;
      ++pkg->count;
    }
  for (unsigned r = 0; r < 6u; ++r)
    if (!arc_adffs_member(pkg, runtime_names[r])) {
      fail(e, en, "missing runtime member");
      goto bad;
    }
  module_help_version(arc_adffs_member(pkg, runtime_names[0]), pkg->version);
  const ArcAdffsMember *obey = arc_adffs_member(pkg, "!ADFFS/obey");
  if (obey) {
    Ze *nested = NULL;
    unsigned nested_count = 0;
    if (!obey_index(obey, &nested, &nested_count, e, en))
      goto bad;
    free(nested);
  }
  size_t pc = 0;
  const ArcAdffsProfile *profiles = arc_adffs_profiles(&pc);
  for (size_t p = 0; p < pc; ++p) {
    bool match = true;
    for (unsigned r = 0; r < 6u; ++r) {
      char h[65];
      const ArcAdffsMember *m = arc_adffs_member(pkg, runtime_names[r]);
      arc_sha256_hex(m->data, m->size, h);
      if (strcmp(h, profiles[p].sha256[r]))
        match = false;
    }
    if (match) {
      pkg->verified = true;
      snprintf(pkg->profile, sizeof(pkg->profile), "%s", profiles[p].name);
      snprintf(pkg->version, sizeof(pkg->version), "%s", profiles[p].version);
      break;
    }
  }
  if (!pkg->verified) {
    snprintf(pkg->profile, sizeof(pkg->profile), "untested");
  }
  free(z);
  return true;
bad:
  free(z);
  arc_adffs_package_free(pkg);
  return false;
}

void arc_adffs_package_free(ArcAdffsPackage *p) {
  if (!p)
    return;
  for (unsigned i = 0; i < p->count; ++i)
    free(p->members[i].data);
  memset(p, 0, sizeof(*p));
}
const ArcAdffsMember *arc_adffs_member(const ArcAdffsPackage *p,
                                       const char *n) {
  if (p && n)
    for (unsigned i = 0; i < p->count; ++i)
      if (!strcmp(p->members[i].name, n))
        return &p->members[i];
  return NULL;
}

bool arc_adffs_builtin_script(const ArcAdffsPackage *p, unsigned game,
                              unsigned disc, uint8_t **script, size_t *size,
                              char *e, size_t en) {
  if (script)
    *script = NULL;
  if (size)
    *size = 0;
  const ArcAdffsMember *obey = arc_adffs_member(p, "!ADFFS/obey");
  if (!obey)
    return false;
  Ze *z = NULL;
  unsigned zn = 0;
  if (!obey_index(obey, &z, &zn, e, en))
    return false;
  char key[16];
  snprintf(key, sizeof(key), "F%05u%02u", game, disc);
  bool found = false;
  for (unsigned i = 0; i < zn; ++i) {
    if (!strcmp(z[i].name, key)) {
      if (!extract(obey->data, obey->size, &z[i], script, e, en)) {
        free(z);
        return false;
      }
      if (size)
        *size = z[i].size;
      found = true;
    }
  }
  free(z);
  return found;
}

bool arc_adffs_script_resources(const ArcAdffsPackage *p, const uint8_t *s,
                                size_t n, char missing[128]) {
  if (missing)
    missing[0] = 0;
  for (size_t i = 0; i + 6u <= n; ++i)
    if (tolower(s[i]) == 'a' && tolower(s[i + 1]) == 'd' &&
        tolower(s[i + 2]) == 'f' && tolower(s[i + 3]) == 'f' &&
        tolower(s[i + 4]) == 's' && s[i + 5] == ':') {
      char ref[128] = "!ADFFS/";
      size_t at = 7u, j = i + 6u;
      while (j < n && at + 1u < sizeof(ref)) {
        unsigned char c = s[j];
        if (!(isalnum(c) || c == '!' || c == '_' || c == '-' || c == '.'))
          break;
        ref[at++] = (char)(c == '.' ? '/' : c);
        ++j;
      }
      ref[at] = 0;
      if (at == 7u)
        continue;
      bool ok = false;
      if (foldprefix(ref, "!ADFFS/obey/")) {
        const ArcAdffsMember *obey = arc_adffs_member(p, "!ADFFS/obey");
        Ze *z = NULL;
        unsigned zn = 0;
        if (obey && obey_index(obey, &z, &zn, NULL, 0u)) {
          const char *key = ref + sizeof("!ADFFS/obey/") - 1u;
          for (unsigned k = 0; k < zn; ++k)
            if (!foldcmp(z[k].name, key))
              ok = true;
        }
        free(z);
      } else {
        char trial[128];
        snprintf(trial, sizeof(trial), "%s", ref);
        for (;;) {
          for (unsigned m = 0; m < p->count; ++m)
            if (!foldcmp(p->members[m].name, trial))
              ok = true;
          if (ok)
            break;
          char *slash = strrchr(trial, '/');
          if (!slash || slash < trial + 16u)
            break;
          *slash = 0;
        }
      }
      if (!ok) {
        if (missing)
          snprintf(missing, 128, "%s", ref);
        return false;
      }
      i = j ? j - 1u : i;
    }
  return true;
}

static const char launcher_text[] =
    "| ArcDroid ADFFS launcher\r"
    "Set ADFFS$Dir ADFS::1.$.!ADFFS\rSet ADFFS$Path ADFS::1.$.!ADFFS.\r"
    "Run ADFS::1.$.!ADFFS.SetVars\rSet ADFFS$FileSizeLimit 2048\r"
    "Set Alias$StoreCSD Set ADFFS$CSD <FileSwitch$CurrentFilingSystem>#<Dir>\r"
    "Set Alias$RestoreCSD FilingSystem <ADFFS$CSD>\rSet Alias$ADFClipScreen "
    "ADFDoNothing\rSet Alias$ADFScreenBorders ADFDoNothing\r"
    "Set ADFFS$JIT 0\rSet ADFFS$CRC 0\rUnset ADFFS$LCDGM\r"
    "RMEnsure SharedCLibrary 5.17 RMLoad "
    "ADFS::1.$.!ADFFS.3rdParty.CLib\rRMEnsure SharedCLibrary 5.17 Error "
    "ArcDroid ADFFS step CLib failed\r"
    "RMEnsure CallASWI 0 RMLoad ADFS::1.$.!ADFFS.3rdParty.CallASWI\rRMEnsure "
    "CallASWI 0 Error ArcDroid ADFFS step CallASWI failed\r"
    "RMEnsure ZLib 0 RMLoad ADFS::1.$.!ADFFS.3rdParty.ZLib\rRMEnsure ZLib 0 "
    "Error ArcDroid ADFFS step ZLib failed\r"
    "RMEnsure BootCmds 0 RMLoad ADFS::1.$.!ADFFS.3rdParty.BootCmds\rRMEnsure "
    "BootCmds 0 Error ArcDroid ADFFS step BootCmds failed\r"
    "RMEnsure ADFFS 0 RMLoad ADFS::1.$.!ADFFS.ADFFS\rRMEnsure ADFFS 0 Error "
    "ArcDroid ADFFS step ADFFS failed\r"
    "ADFMount ADFS::0.$.Game\rADFS\rNoDir\rNoLib\rNoURD\rDismount "
    ":0\rADFBootFloppy\r";
bool arc_adffs_launcher(uint8_t *o, size_t cap, size_t *sz) {
  size_t n = sizeof(launcher_text) - 1u;
  if (sz)
    *sz = n;
  if (!o)
    return true;
  if (cap < n)
    return false;
  memcpy(o, launcher_text, n);
  return true;
}

typedef struct Node Node;
struct Node {
  char name[11];
  bool dir;
  uint8_t *data;
  size_t size;
  uint32_t load, exec, attr;
  Node *parent;
  Node *child[77];
  unsigned count;
  uint32_t offset;
};
static int nodecmp(const void *aa, const void *bb) {
  const Node *a = *(Node *const *)aa, *b = *(Node *const *)bb;
  return foldcmp(a->name, b->name);
}
static uint32_t align1k(uint32_t v) { return (v + 1023u) & ~1023u; }
static uint32_t ror(uint32_t v, unsigned s) {
  return (v >> s) | (v << (32u - s));
}
static void finishdir(uint8_t *d, unsigned count, uint32_t parent) {
  size_t tail = 2007u;
  put24(d + tail + 3u, parent / 256u);
  uint32_t c = 0;
  size_t end = 5u + count * 26u, w = end & ~3u;
  for (size_t i = 0; i < w; i += 4)
    c = le32(d + i) ^ ror(c, 13);
  for (size_t i = w; i < end; ++i)
    c = d[i] ^ ror(c, 13);
  for (size_t i = tail + 1u; i < tail + 37u; i += 4)
    c = le32(d + i) ^ ror(c, 13);
  d[2047] = (uint8_t)(c ^ (c >> 8) ^ (c >> 16) ^ (c >> 24));
}
static void entry(uint8_t *d, unsigned i, const Node *n) {
  uint8_t *x = d + 5u + i * 26u;
  memset(x, 13, 10);
  memcpy(x, n->name, strlen(n->name));
  uint32_t load = n->load, exec = n->exec, attr = n->attr ? n->attr : 0x33u;
  if (n->dir)
    attr |= 8u;
  memcpy(x + 10, &load, 4);
  memcpy(x + 14, &exec, 4);
  uint32_t size = n->dir ? D_DIRECTORY : (uint32_t)n->size;
  memcpy(x + 18, &size, 4);
  put24(x + 22, n->offset / 256u);
  x[25] = (uint8_t)attr;
}
static uint8_t oldsum(const uint8_t *p) {
  unsigned c = 255;
  for (unsigned i = 255; i-- > 0;) {
    if (c > 255)
      c = (c + 1) & 255;
    c += p[i];
  }
  return (uint8_t)c;
}
static bool make_disc(Node *root, uint8_t *out, char *e, size_t en) {
  Node *all[ARC_ADFFS_MAX_MEMBERS + 16];
  unsigned ac = 0;
  all[ac++] = root;
  for (unsigned i = 0; i < ac; ++i)
    if (all[i]->dir) {
      qsort(all[i]->child, all[i]->count, sizeof(all[i]->child[0]), nodecmp);
      for (unsigned j = 0; j < all[i]->count; ++j)
        if (all[i]->child[j]->dir)
          all[ac++] = all[i]->child[j];
    }
  uint32_t pos = 3072u;
  root->offset = D_ROOT;
  for (unsigned i = 1; i < ac; ++i) {
    all[i]->offset = pos;
    pos += D_DIRECTORY;
  }
  for (unsigned i = 0; i < ac; ++i) {
    for (unsigned j = 0; j < all[i]->count; ++j)
      if (!all[i]->child[j]->dir) {
        pos = align1k(pos);
        all[i]->child[j]->offset = pos;
        if (all[i]->child[j]->size > ARC_ADFFS_DISC_SIZE - pos) {
          fail(e, en, "carried set does not fit support disc");
          return false;
        }
        pos += (uint32_t)all[i]->child[j]->size;
      }
  }
  uint32_t free_at = align1k(pos);
  if (free_at >= ARC_ADFFS_DISC_SIZE) {
    fail(e, en, "carried set does not fit support disc");
    return false;
  }
  memset(out, 0, ARC_ADFFS_DISC_SIZE);
  put24(out, free_at / 256u);
  put24(out + 252, ARC_ADFFS_DISC_SIZE / 256u);
  put24(out + 256, (ARC_ADFFS_DISC_SIZE - free_at) / 256u);
  memcpy(out + 247, "ArDFr", 5);
  memcpy(out + 502, "cDosd", 5);
  out[509] = 2;
  out[510] = 3;
  out[255] = oldsum(out);
  out[511] = oldsum(out + 256);
  for (unsigned i = 0; i < ac; ++i) {
    uint8_t *d = out + all[i]->offset;
    d[0] = 1;
    memcpy(d + 1, "Hugo", 4);
    for (unsigned j = 0; j < all[i]->count; ++j) {
      entry(d, j, all[i]->child[j]);
      if (!all[i]->child[j]->dir && all[i]->child[j]->size)
        memcpy(out + all[i]->child[j]->offset, all[i]->child[j]->data,
               all[i]->child[j]->size);
    }
    size_t t = 2007;
    d[t] = 0;
    memset(d + t + 25, 13, 10);
    memcpy(d + t + 25, all[i] == root ? "$" : all[i]->name,
           strlen(all[i] == root ? "$" : all[i]->name));
    d[t + 35] = 1;
    memcpy(d + t + 36, "Hugo", 4);
    finishdir(d, all[i]->count,
              all[i]->parent ? all[i]->parent->offset : root->offset);
  }
  return true;
}
static Node *childdir(Node *p, const char *name, Node *pool, unsigned *used) {
  for (unsigned i = 0; i < p->count; ++i)
    if (!foldcmp(p->child[i]->name, name))
      return p->child[i]->dir ? p->child[i] : NULL;
  if (strlen(name) > 10u || p->count >= 77u ||
      *used >= ARC_ADFFS_MAX_MEMBERS + 16u)
    return NULL;
  Node *n = &pool[(*used)++];
  memset(n, 0, sizeof(*n));
  n->dir = true;
  n->parent = p;
  snprintf(n->name, sizeof(n->name), "%s", name);
  p->child[p->count++] = n;
  return n;
}
static bool addpath(Node *root, Node *pool, unsigned *used, const char *path,
                    uint8_t *data, size_t size, uint32_t load, uint32_t exec,
                    uint32_t attr) {
  char b[128];
  if (strlen(path) >= sizeof(b))
    return false;
  strcpy(b, path);
  Node *p = root;
  char *part = b;
  for (;;) {
    char *slash = strchr(part, '/');
    if (!slash)
      break;
    *slash = 0;
    p = childdir(p, part, pool, used);
    if (!p)
      return false;
    part = slash + 1;
  }
  if (!part[0] || strlen(part) > 10u || p->count >= 77u ||
      *used >= ARC_ADFFS_MAX_MEMBERS + 16u)
    return false;
  for (unsigned i = 0; i < p->count; ++i)
    if (!foldcmp(p->child[i]->name, part))
      return false;
  Node *n = &pool[(*used)++];
  memset(n, 0, sizeof(*n));
  snprintf(n->name, sizeof(n->name), "%s", part);
  n->parent = p;
  n->data = data;
  n->size = size;
  n->load = load;
  n->exec = exec;
  n->attr = attr;
  p->child[p->count++] = n;
  return true;
}
bool arc_adffs_carrier_build(const uint8_t *jfd, size_t n,
                             uint8_t out[ARC_ADFFS_DISC_SIZE], char *e,
                             size_t en) {
  if (!jfd || n > ARC_ADFFS_CARRIER_CAPACITY) {
    fail(e, en, "JFD file does not fit carrier");
    return false;
  }
  Node pool[4] = {0};
  unsigned used = 1;
  Node *root = &pool[0];
  root->dir = true;
  strcpy(root->name, "$");
  if (!addpath(root, pool, &used, "Game", (uint8_t *)(uintptr_t)jfd, n,
               0xffffce00u, 0, 0x33u))
    return false;
  return make_disc(root, out, e, en);
}
bool arc_adffs_support_build(const ArcAdffsPackage *p,
                             uint8_t out[ARC_ADFFS_DISC_SIZE], char *e,
                             size_t en) {
  Node pool[ARC_ADFFS_MAX_MEMBERS + 16];
  memset(pool, 0, sizeof(pool));
  unsigned used = 1;
  Node *root = &pool[0];
  root->dir = true;
  strcpy(root->name, "$");
  static uint8_t boot[] = "%Desktop -File \"ADFS::1.$.ArcLaunch\"\r";
  size_t lsz;
  arc_adffs_launcher(NULL, 0, &lsz);
  if (!addpath(root, pool, &used, "!Boot", boot, sizeof(boot) - 1, 0xffffeb00u,
               0, 0x33u) ||
      !addpath(root, pool, &used, "ArcLaunch",
               (uint8_t *)(uintptr_t)launcher_text, lsz, 0xffffeb00u, 0,
               0x33u)) {
    fail(e, en, "support disc tree is invalid");
    return false;
  }
  for (unsigned i = 0; i < p->count; ++i)
    if (!addpath(root, pool, &used, p->members[i].name, p->members[i].data,
                 p->members[i].size, p->members[i].load, p->members[i].exec,
                 p->members[i].attr)) {
      fail(e, en, "package path cannot be represented on FileCore");
      return false;
    }
  return make_disc(root, out, e, en);
}
