/* Bounded reader and synthetic media for a user-supplied ADFFS package.
 * No ADFFS source or binary data is incorporated here. GPL-2.0-only. */
#include "adffs.h"
#include "arcdroid_core.h"
#include "zip.h"

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

static bool used_name(const char *n) {
  static const char *const exact[] = {
      "!ADFFS/ADFFS",         "!ADFFS/SetVars",
      "!ADFFS/3rdParty/CLib", "!ADFFS/3rdParty/CallASWI",
      "!ADFFS/3rdParty/ZLib", "!ADFFS/3rdParty/BootCmds",
      "!ADFFS/obey",          "!ADFFS/!RunTypes",
      "!ADFFS/!fixObey",      "!ADFFS/extras/JoyMod",
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

static bool obey_index(const ArcAdffsMember *obey, ArcZipArchive *archive,
                       char *e, size_t en) {
  const ArcZipLimits limits = {ZIP_MAX, ZIP_ENTRIES, ZIP_MEMBER};
  if (!arc_zip_index(obey->data, obey->size, &limits, archive, e, en))
    return false;
  for (unsigned i = 0; i < archive->count; ++i) {
    ArcZipEntry *entry = &archive->entries[i];
    size_t length = strlen(entry->name);
    if (!length || length > 10u) {
      fail(e, en, "invalid obey member name");
      arc_zip_free(archive);
      return false;
    }
    for (size_t k = 0; k < length; ++k)
      if (!isalnum((unsigned char)entry->name[k])) {
        fail(e, en, "invalid obey member name");
        arc_zip_free(archive);
        return false;
      }
    for (unsigned j = 0; j < i; ++j)
      if (!foldcmp(entry->name, archive->entries[j].name)) {
        fail(e, en, !strcmp(entry->name, archive->entries[j].name)
                        ? "duplicate obey member name"
                        : "case-colliding obey member names");
        arc_zip_free(archive);
        return false;
      }
    if (entry->status != ARC_ZIP_MEMBER_OK) {
      fail(e, en, arc_zip_member_status_reason(entry->status));
      arc_zip_free(archive);
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
  ArcZipArchive archive;
  const ArcZipLimits limits = {ZIP_MAX, ZIP_ENTRIES, ZIP_MEMBER};
  if (!arc_zip_index(a, n, &limits, &archive, e, en))
    return false;
  size_t total = 0;
  for (unsigned i = 0; i < archive.count; ++i) {
    ArcZipEntry *entry = &archive.entries[i];
    if (entry->status != ARC_ZIP_MEMBER_OK) {
      if (entry->status == ARC_ZIP_MEMBER_TOO_LARGE)
        fail(e, en, "member exceeds 2 MiB");
      else
        fail(e, en, "unsupported compression method");
      arc_zip_free(&archive);
      return false;
    }
    if (used_name(entry->name))
      for (unsigned j = 0; j < i; ++j)
        if (used_name(archive.entries[j].name) &&
            !foldcmp(entry->name, archive.entries[j].name)) {
          fail(e, en,
               !strcmp(entry->name, archive.entries[j].name)
                   ? "duplicate used name"
                   : "case-colliding used names");
          arc_zip_free(&archive);
          return false;
        }
  }
  for (unsigned i = 0; i < archive.count; ++i) {
    ArcZipEntry *entry = &archive.entries[i];
    if (used_name(entry->name)) {
      if (pkg->count == ARC_ADFFS_MAX_MEMBERS ||
          entry->size > ZIP_USED_TOTAL - total) {
        fail(e, en, "used members exceed limits");
        goto bad;
      }
      ArcAdffsMember *m = &pkg->members[pkg->count];
      if (!arc_extra(entry->central_extra, entry->central_extra_size,
                     &m->load, &m->exec, &m->attr)) {
        fail(e, en, "used member lacks ARC0 metadata");
        goto bad;
      }
      if (!arc_zip_extract(a, n, entry, &m->data, &m->size, e, en))
        goto bad;
      snprintf(m->name, sizeof(m->name), "%s", entry->name);
      total += m->size;
      ++pkg->count;
    }
  }
  for (unsigned r = 0; r < 6u; ++r)
    if (!arc_adffs_member(pkg, runtime_names[r])) {
      fail(e, en, "missing runtime member");
      goto bad;
    }
  static const char *const required_data[] = {
      "!ADFFS/obey", "!ADFFS/!RunTypes", "!ADFFS/!fixObey"};
  for (unsigned r = 0; r < sizeof(required_data) / sizeof(required_data[0]); ++r)
    if (!arc_adffs_member(pkg, required_data[r])) {
      char message[96];
      snprintf(message, sizeof(message), "missing required member %s",
               required_data[r]);
      fail(e, en, message);
      goto bad;
    }
  module_help_version(arc_adffs_member(pkg, runtime_names[0]), pkg->version);
  const ArcAdffsMember *obey = arc_adffs_member(pkg, "!ADFFS/obey");
  ArcZipArchive nested;
  if (!obey_index(obey, &nested, e, en))
    goto bad;
  arc_zip_free(&nested);
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
  arc_zip_free(&archive);
  return true;
bad:
  arc_zip_free(&archive);
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
  ArcZipArchive archive;
  if (!obey_index(obey, &archive, e, en))
    return false;
  char key[16];
  snprintf(key, sizeof(key), "F%05u%02u", game, disc);
  bool found = false;
  for (unsigned i = 0; i < archive.count; ++i) {
    ArcZipEntry *entry = &archive.entries[i];
    if (!strcmp(entry->name, key)) {
      if (!arc_zip_extract(obey->data, obey->size, entry, script, size, e,
                           en)) {
        arc_zip_free(&archive);
        return false;
      }
      found = true;
    }
  }
  arc_zip_free(&archive);
  return found;
}

typedef struct {
  const ArcAdffsPackage *package;
  const ArcAdffsMember *obey;
  ArcZipArchive obey_archive;
  unsigned obey_count;
  bool obey_visited[ZIP_ENTRIES];
  ArcAdffsSelection *selection;
  char *missing;
} SelectionContext;

static void select_member(SelectionContext *c, unsigned index) {
  if (!c->selection->member[index]) {
    c->selection->member[index] = true;
    ++c->selection->count;
  }
}

static bool set_missing(SelectionContext *c, const char *name) {
  if (c->missing)
    snprintf(c->missing, 128u, "%s", name);
  return false;
}

static bool select_script(SelectionContext *c, const uint8_t *s, size_t n,
                          unsigned depth);

static bool select_script_line(SelectionContext *c, const uint8_t *s, size_t n,
                               unsigned depth) {
  for (size_t i = 0; i + 6u <= n; ++i)
    if (tolower((unsigned char)s[i]) == 'a' &&
        tolower((unsigned char)s[i + 1]) == 'd' &&
        tolower((unsigned char)s[i + 2]) == 'f' &&
        tolower((unsigned char)s[i + 3]) == 'f' &&
        tolower((unsigned char)s[i + 4]) == 's' && s[i + 5] == ':') {
      char ref[128] = "!ADFFS/";
      size_t at = 7u, j = i + 6u;
      while (j < n && at + 1u < sizeof(ref)) {
        unsigned char ch = s[j];
        if (!(isalnum(ch) || ch == '!' || ch == '_' || ch == '-' || ch == '.'))
          break;
        ref[at++] = (char)(ch == '.' ? '/' : ch);
        ++j;
      }
      ref[at] = 0;
      if (at == 7u)
        continue;
      if (foldprefix(ref, "!ADFFS/obey/")) {
        const char *key = ref + sizeof("!ADFFS/obey/") - 1u;
        int found = -1;
        for (unsigned k = 0; k < c->obey_count; ++k)
          if (!foldcmp(c->obey_archive.entries[k].name, key)) {
            found = (int)k;
            break;
          }
        if (found < 0)
          return set_missing(c, ref);
        if (!c->obey_visited[found]) {
          if (depth >= 4u)
            return set_missing(c, ref);
          uint8_t *nested = NULL;
          size_t nested_size = 0u;
          if (!arc_zip_extract(c->obey->data, c->obey->size,
                               &c->obey_archive.entries[found], &nested,
                               &nested_size, NULL, 0u))
            return set_missing(c, ref);
          c->obey_visited[found] = true;
          bool ok = select_script(c, nested, nested_size, depth + 1u);
          free(nested);
          if (!ok)
            return false;
        }
      } else {
        char trial[128];
        snprintf(trial, sizeof(trial), "%s", ref);
        int found = -1;
        for (;;) {
          for (unsigned m = 0; m < c->package->count; ++m)
            if (!foldcmp(c->package->members[m].name, trial)) {
              found = (int)m;
              break;
            }
          if (found >= 0)
            break;
          char *slash = strrchr(trial, '/');
          if (!slash || slash <= trial + sizeof("!ADFFS/") - 1u)
            break;
          *slash = 0;
        }
        if (found < 0)
          return set_missing(c, ref);
        select_member(c, (unsigned)found);
      }
      i = j ? j - 1u : i;
    }
  return true;
}

static bool select_script(SelectionContext *c, const uint8_t *s, size_t n,
                          unsigned depth) {
  size_t line = 0u;
  while (line < n) {
    size_t end = line;
    while (end < n && s[end] != '\r' && s[end] != '\n')
      ++end;
    size_t first = line;
    while (first < end && isspace((unsigned char)s[first]))
      ++first;
    if (first < end && s[first] != '|' &&
        !select_script_line(c, s + first, end - first, depth))
      return false;
    line = end + (end < n ? 1u : 0u);
  }
  return true;
}

/* Loader contract: ADFFS$Dir, ADFFS$Path; SetVars establishes ADFFS$OSVersion,
 * ADFFS$CPUID, ADFFS$VIDC20, ADFFS$HighVectors, ADFFS$ARMv7, ADFFS$Overlays;
 * the launcher also establishes ADFFS$FileSizeLimit, ADFFS$JIT, ADFFS$CRC,
 * ADFFS$LCDGM and LCDGM$Status. Deliberate differences: no-op StoreCSD,
 * RestoreCSD, ADFClipScreen, ADFScreenBorders; no LCD game-modes, AutoVIDC,
 * VProtect, USBJoystick, DOSMap or filer start. */
static const char launcher_text[] =
    "| ArcDroid ADFFS RAM launcher\r"
    "Set ADFFS$Dir RAM::0.$.!ADFFS\rSet ADFFS$Path RAM::0.$.!ADFFS.\r"
    "Run RAM::0.$.!ADFFS.SetVars\rSetEval LCDGM$Status 0\r"
    "SetEval ADFFS$FileSizeLimit 2048\r"
    "Set Alias$StoreCSD ADFDoNothing %%*0\rSet Alias$RestoreCSD ADFDoNothing "
    "%%*0\rSet Alias$ADFClipScreen ADFDoNothing %%*0\rSet "
    "Alias$ADFScreenBorders ADFDoNothing %%*0\rSetEval ADFFS$JIT 0\r"
    "SetEval ADFFS$CRC 0\r"
    "Set ADFFS$LCDGM N\r"
    "RMEnsure SharedCLibrary 5.17 RMLoad RAM::0.$.!ADFFS.3rdParty.CLib\r"
    "RMEnsure SharedCLibrary 5.17 Error ArcDroid ADFFS step CLib failed\r"
    "RMEnsure CallASWI 0.02 RMLoad RAM::0.$.!ADFFS.3rdParty.CallASWI\r"
    "RMEnsure CallASWI 0.02 Error ArcDroid ADFFS step CallASWI failed\r"
    "RMEnsure ZLib 0.05 RMLoad RAM::0.$.!ADFFS.3rdParty.ZLib\r"
    "RMEnsure ZLib 0.05 Error ArcDroid ADFFS step ZLib failed\r"
    "RMEnsure BootCommands 0.00 RMLoad RAM::0.$.!ADFFS.3rdParty.BootCmds\r"
    "RMEnsure BootCommands 0.00 Error ArcDroid ADFFS step BootCommands failed\r"
    "RMEnsure ADFFS 0.00 RMLoad RAM::0.$.!ADFFS.ADFFS\r"
    "RMEnsure ADFFS 0.00 Error ArcDroid ADFFS step ADFFS failed\r"
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

bool arc_adffs_stage1(uint32_t ram_size, uint8_t *o, size_t cap, size_t *sz) {
  char text[512];
  int length = snprintf(
      text, sizeof(text),
      "ChangeDynamicArea -RamFsSize %uK\r"
      "Copy ADFS::1.$.!ADFFS RAM::0.$.!ADFFS ~C~VRFQ\r"
      "Copy ADFS::1.$.ArcStage2 RAM::0.$.ArcStage2 ~C~VFQ\r"
      "Filer_Run RAM::0.$.ArcStage2\r",
      ram_size / 1024u);
  if (length < 0 || (size_t)length >= sizeof(text))
    return false;
  if (sz)
    *sz = (size_t)length;
  if (!o)
    return true;
  if (cap < (size_t)length)
    return false;
  memcpy(o, text, (size_t)length);
  return true;
}

static bool selection_ram_size(const ArcAdffsPackage *p,
                               const ArcAdffsSelection *selection,
                               uint32_t *ram_size) {
  enum { MAX_DIRECTORIES = ARC_ADFFS_MAX_MEMBERS * 3u };
  char (*directories)[128] = calloc(MAX_DIRECTORIES, sizeof(*directories));
  if (!directories)
    return false;
  unsigned directory_count = 0u;
  uint32_t bytes = 64u * 1024u;
  for (unsigned i = 0; i < p->count; ++i) {
    if (!selection->member[i])
      continue;
    bytes += ((uint32_t)p->members[i].size + 1023u) & ~1023u;
    char path[128];
    snprintf(path, sizeof(path), "%s", p->members[i].name);
    for (char *slash = strchr(path, '/'); slash; slash = strchr(slash + 1, '/')) {
      char saved = *slash;
      *slash = 0;
      bool known = false;
      for (unsigned d = 0; d < directory_count; ++d)
        if (!foldcmp(directories[d], path)) {
          known = true;
          break;
        }
      if (!known && directory_count < MAX_DIRECTORIES)
        snprintf(directories[directory_count++], sizeof(directories[0]), "%s",
                 path);
      *slash = saved;
    }
  }
  bytes += directory_count * D_DIRECTORY;
  bytes += ((uint32_t)(sizeof(launcher_text) - 1u) + 1023u) & ~1023u;
  *ram_size = (bytes + 32767u) & ~32767u;
  free(directories);
  return true;
}

bool arc_adffs_select(const ArcAdffsPackage *p, const uint8_t *script,
                      size_t script_size, ArcAdffsSelection *selection,
                      char missing[128]) {
  if (!p || !script || !selection)
    return false;
  memset(selection, 0, sizeof(*selection));
  if (missing)
    missing[0] = 0;
  SelectionContext context;
  memset(&context, 0, sizeof(context));
  context.package = p;
  context.selection = selection;
  context.missing = missing;
  context.obey = arc_adffs_member(p, "!ADFFS/obey");
  if (!context.obey ||
      !obey_index(context.obey, &context.obey_archive, NULL, 0u))
    return false;
  context.obey_count = context.obey_archive.count;
  static const char *const baseline[] = {
      "!ADFFS/ADFFS",          "!ADFFS/SetVars",
      "!ADFFS/3rdParty/CLib",  "!ADFFS/3rdParty/CallASWI",
      "!ADFFS/3rdParty/ZLib",  "!ADFFS/3rdParty/BootCmds",
      "!ADFFS/obey",           "!ADFFS/!RunTypes",
      "!ADFFS/!fixObey"};
  for (unsigned r = 0; r < sizeof(baseline) / sizeof(baseline[0]); ++r) {
    bool found = false;
    for (unsigned i = 0; i < p->count; ++i)
      if (!strcmp(p->members[i].name, baseline[r])) {
        select_member(&context, i);
        found = true;
        break;
      }
    if (!found) {
      arc_zip_free(&context.obey_archive);
      return false;
    }
  }
  bool ok = select_script(&context, script, script_size, 0u);
  arc_zip_free(&context.obey_archive);
  if (!ok)
    return false;
  return selection_ram_size(p, selection, &selection->ram_size);
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
  Node *all[ARC_ADFFS_MAX_MEMBERS + 18];
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
        Node *file = all[i]->child[j];
        pos = align1k(pos);
        file->offset = pos;
        if (pos > ARC_ADFFS_DISC_SIZE ||
            file->size > ARC_ADFFS_DISC_SIZE - pos) {
          fail(e, en, "carried set does not fit support disc");
          return false;
        }
        pos += (uint32_t)file->size;
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
      *used >= ARC_ADFFS_MAX_MEMBERS + 18u)
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
      *used >= ARC_ADFFS_MAX_MEMBERS + 18u)
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
static bool support_build(const ArcAdffsPackage *p,
                          const ArcAdffsSelection *selection,
                          uint8_t out[ARC_ADFFS_DISC_SIZE], char *e,
                          size_t en) {
  if (!p || !selection) {
    fail(e, en, "support selection is missing");
    return false;
  }
  Node *pool = calloc(ARC_ADFFS_MAX_MEMBERS + 18u, sizeof(*pool));
  if (!pool) {
    fail(e, en, "out of memory");
    return false;
  }
  unsigned used = 1;
  Node *root = &pool[0];
  root->dir = true;
  strcpy(root->name, "$");
  static uint8_t boot[] = "%Desktop -File \"ADFS::1.$.ArcLaunch\"\r";
  static uint8_t desktop[] = "Filer_Run ADFS::1.$.ArcStage1\r";
  uint8_t stage1[512];
  size_t stage1_size;
  uint32_t ram_size = selection->ram_size;
  if (!arc_adffs_stage1(ram_size, stage1, sizeof(stage1),
                        &stage1_size)) {
    fail(e, en, "first-stage launcher is invalid");
    free(pool);
    return false;
  }
  size_t lsz;
  if (!arc_adffs_launcher(NULL, 0u, &lsz)) { free(pool); return false; }
  if (!addpath(root, pool, &used, "!Boot", boot, sizeof(boot) - 1, 0xffffeb00u,
               0, 0x33u) ||
      !addpath(root, pool, &used, "ArcLaunch", desktop,
               sizeof(desktop) - 1u, 0xffffeb00u, 0, 0x33u) ||
      !addpath(root, pool, &used, "ArcStage1", stage1, stage1_size,
               0xffffeb00u, 0, 0x33u) ||
      !addpath(root, pool, &used, "ArcStage2",
               (uint8_t *)(uintptr_t)launcher_text,
               lsz, 0xffffeb00u, 0, 0x33u)) {
    fail(e, en, "support disc tree is invalid");
    free(pool);
    return false;
  }
  for (unsigned i = 0; i < p->count; ++i)
    if (selection->member[i] &&
        !addpath(root, pool, &used, p->members[i].name, p->members[i].data,
                 p->members[i].size, p->members[i].load, p->members[i].exec,
                 p->members[i].attr)) {
      fail(e, en, "package path cannot be represented on FileCore");
      free(pool);
      return false;
    }
  bool ok = make_disc(root, out, e, en);
  free(pool);
  return ok;
}
bool arc_adffs_support_build(const ArcAdffsPackage *p,
                             const ArcAdffsSelection *selection,
                             uint8_t out[ARC_ADFFS_DISC_SIZE], char *e,
                             size_t en) {
  return support_build(p, selection, out, e, en);
}
