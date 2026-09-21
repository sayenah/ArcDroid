/* Deterministic tests for synthetic ADFFS packaging. GPL-2.0-only. */
#include "adffs.h"
#include "arcdroid_core.h"
#include "jfd.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *dir, const char *name, size_t *size) {
  char path[4096];
  snprintf(path, sizeof(path), "%s/adffs/%s", dir, name);
  FILE *f = fopen(path, "rb");
  assert(f && fseek(f, 0, SEEK_END) == 0);
  long n = ftell(f);
  assert(n >= 0 && fseek(f, 0, SEEK_SET) == 0);
  uint8_t *p = malloc((size_t)n ? (size_t)n : 1u);
  assert(p && fread(p, 1, (size_t)n, f) == (size_t)n && fclose(f) == 0);
  *size = (size_t)n;
  return p;
}
static uint32_t le24(const uint8_t *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}
static uint32_t le32t(const uint8_t *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
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
static uint32_t ror(uint32_t v, unsigned s) {
  return (v >> s) | (v << (32 - s));
}
static bool dircheck(const uint8_t *d) {
  unsigned count = 0;
  while (count < 77u && d[5u + count * 26u] != 0)
    ++count;
  size_t end = 5u + count * 26u, w = end & ~3u;
  uint32_t c = 0;
  for (size_t i = 0; i < w; i += 4)
    c = le32t(d + i) ^ ror(c, 13);
  for (size_t i = w; i < end; ++i)
    c = d[i] ^ ror(c, 13);
  for (size_t i = 2008; i < 2044; i += 4)
    c = le32t(d + i) ^ ror(c, 13);
  return d[2047] == (uint8_t)(c ^ (c >> 8) ^ (c >> 16) ^ (c >> 24));
}
static const uint8_t *find_entry(const uint8_t *d, const char *name) {
  for (unsigned i = 0; i < 77u && d[5u + i * 26u]; ++i) {
    const uint8_t *x = d + 5u + i * 26u;
    size_t n = 0;
    while (n < 10u && x[n] != 13u)
      ++n;
    if (n == strlen(name) && !memcmp(x, name, n))
      return x;
  }
  return NULL;
}
static void walk_directory(const uint8_t *disc, uint32_t sector,
                           uint32_t parent, unsigned depth,
                           uint32_t *used_end) {
  assert(depth < 16u && sector % 4u == 0u &&
         sector * 256u + 2048u <= ARC_ADFFS_DISC_SIZE);
  const uint8_t *d = disc + sector * 256u;
  assert(dircheck(d) && le24(d + 2010u) == parent);
  if (sector * 256u + 2048u > *used_end)
    *used_end = sector * 256u + 2048u;
  for (unsigned i = 0; i < 77u && d[5u + i * 26u]; ++i) {
    const uint8_t *x = d + 5u + i * 26u;
    uint32_t child = le24(x + 22u), size = le32t(x + 18u);
    assert(child % 4u == 0u && child * 256u <= ARC_ADFFS_DISC_SIZE);
    if (x[25] & 8u) {
      assert(size == 2048u);
      walk_directory(disc, child, sector, depth + 1u, used_end);
    } else {
      assert(child * 256u + size <= ARC_ADFFS_DISC_SIZE);
      if (child * 256u + size > *used_end)
        *used_end = child * 256u + size;
    }
  }
}
static void walk_disc(const uint8_t *disc) {
  assert(oldsum(disc) == disc[255] && oldsum(disc + 256) == disc[511]);
  uint32_t free_at = le24(disc), free_count = le24(disc + 256u),
           total = le24(disc + 252u);
  assert(total == ARC_ADFFS_DISC_SIZE / 256u && free_at + free_count == total &&
         free_at % 4u == 0u);
  uint32_t used_end = 0;
  walk_directory(disc, 4u, 4u, 0u, &used_end);
  assert(((used_end + 1023u) & ~1023u) == free_at * 256u);
}

static void reject_fixture(const char *dir, const char *name,
                           const char *word) {
  size_t n;
  uint8_t *a = read_file(dir, name, &n);
  ArcAdffsPackage p;
  char error[160] = {0};
  if (arc_adffs_package_parse(a, n, &p, error, sizeof(error))) {
    fprintf(stderr, "%s unexpectedly accepted\n", name);
    arc_adffs_package_free(&p);
    abort();
  }
  if (!strstr(error, word)) {
    fprintf(stderr, "%s: expected %s, got %s\n", name, word, error);
    abort();
  }
  free(a);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  char sha[65];
  arc_sha256_hex("abc", 3, sha);
  assert(!strcmp(
      sha, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  size_t zn;
  uint8_t *zip = read_file(argv[1], "synthetic.zip", &zn);
  ArcAdffsPackage p;
  char error[160] = {0};
  if (!arc_adffs_package_parse(zip, zn, &p, error, sizeof(error))) {
    fprintf(stderr, "synthetic parse: %s\n", error);
    abort();
  }
  assert(p.verified && !strcmp(p.profile, "synthetic-test") && p.count == 9u);
  for (unsigned variant = 0u; variant < 2u; ++variant) {
    size_t variant_size;
    uint8_t *variant_zip = read_file(
        argv[1], variant ? "descriptor.zip" : "stored.zip", &variant_size);
    ArcAdffsPackage variant_package;
    assert(arc_adffs_package_parse(variant_zip, variant_size, &variant_package,
                                   error, sizeof(error)) &&
           variant_package.verified);
    arc_adffs_package_free(&variant_package);
    free(variant_zip);
  }
  size_t untested_size;
  uint8_t *untested_zip = read_file(argv[1], "untested.zip", &untested_size);
  ArcAdffsPackage untested;
  assert(arc_adffs_package_parse(untested_zip, untested_size, &untested, error,
                                 sizeof(error)) &&
         !untested.verified && !strcmp(untested.profile, "untested") &&
         !strcmp(untested.version, "Synthetic 1.0"));
  arc_adffs_package_free(&untested);
  free(untested_zip);
  const ArcAdffsMember *runtime = arc_adffs_member(&p, "!ADFFS/ADFFS");
  assert(runtime && runtime->load == 0xfff00000u && runtime->attr == 0x33u);
  uint8_t *script = NULL;
  size_t script_size = 0;
  assert(arc_adffs_builtin_script(&p, 123u, 1u, &script, &script_size, error,
                                  sizeof(error)));
  assert(script_size >= 7u && !memcmp(script, "Set Arc", 7u));
  char missing[128];
  const char *r1 = "adffs:TRACKERS.qTMMODULE.extra",
             *r2 = "ADFFS:extras.Absent", *r3 = "ADFFS:obey.F0012301",
             *r4 = "ADFFS:obey.F9999901";
  assert(
      arc_adffs_script_resources(&p, (const uint8_t *)r1, strlen(r1), missing));
  assert(!arc_adffs_script_resources(&p, (const uint8_t *)r2, strlen(r2),
                                     missing) &&
         strstr(missing, "Absent"));
  assert(
      arc_adffs_script_resources(&p, (const uint8_t *)r3, strlen(r3), missing));
  assert(!arc_adffs_script_resources(&p, (const uint8_t *)r4, strlen(r4),
                                     missing) &&
         strstr(missing, "F9999901"));
  free(script);

  size_t jn;
  uint8_t *jfd = read_file(argv[1], "eligible.jfd", &jn);
  ArcJfdImage parsed_jfd;
  assert(arc_jfd_parse(jfd, jn, &parsed_jfd, error, sizeof(error)) &&
         parsed_jfd.disc_number == 1u && parsed_jfd.disc_count == 1u &&
         parsed_jfd.game_id == 123u && parsed_jfd.obey_length);
  arc_jfd_free(&parsed_jfd);
  uint8_t *carrier = malloc(ARC_ADFFS_DISC_SIZE),
          *carrier2 = malloc(ARC_ADFFS_DISC_SIZE),
          *support = malloc(ARC_ADFFS_DISC_SIZE);
  assert(carrier && carrier2 && support);
  assert(arc_adffs_carrier_build(jfd, jn, carrier, error, sizeof(error)));
  assert(arc_adffs_carrier_build(jfd, jn, carrier2, error, sizeof(error)) &&
         !memcmp(carrier, carrier2, ARC_ADFFS_DISC_SIZE));
  walk_disc(carrier);
  const uint8_t *root = carrier + 1024u;
  const uint8_t *game = find_entry(root, "Game");
  assert(game && !(le24(game + 22u) & 3u) && le32t(game + 18u) == jn &&
         !memcmp(carrier + le24(game + 22u) * 256u, jfd, jn));
  uint8_t *limit = malloc(ARC_ADFFS_CARRIER_CAPACITY + 1u);
  assert(limit);
  memset(limit, 0x5a, ARC_ADFFS_CARRIER_CAPACITY + 1u);
  assert(arc_adffs_carrier_build(limit, ARC_ADFFS_CARRIER_CAPACITY, carrier,
                                 error, sizeof(error)));
  assert(!arc_adffs_carrier_build(limit, ARC_ADFFS_CARRIER_CAPACITY + 1u,
                                  carrier, error, sizeof(error)));
  free(limit);
  assert(arc_adffs_support_build(&p, support, error, sizeof(error)));
  walk_disc(support);
  root = support + 1024u;
  const uint8_t *adffs = find_entry(root, "!ADFFS");
  assert(adffs && (adffs[25] & 8u) && !(le24(adffs + 22u) & 3u));
  const uint8_t *adir = support + le24(adffs + 22u) * 256u;
  assert(dircheck(adir));
  const uint8_t *module = find_entry(adir, "ADFFS");
  assert(module && le32t(module + 10u) == runtime->load &&
         le32t(module + 14u) == runtime->exec && module[25] == runtime->attr);
  size_t rzn;
  uint8_t *rzip = read_file(argv[1], "reordered.zip", &rzn);
  ArcAdffsPackage reordered;
  assert(arc_adffs_package_parse(rzip, rzn, &reordered, error, sizeof(error)) &&
         arc_adffs_support_build(&reordered, carrier2, error, sizeof(error)) &&
         !memcmp(support, carrier2, ARC_ADFFS_DISC_SIZE));
  arc_adffs_package_free(&reordered);
  free(rzip);
  static const char launcher_golden[] =
      "| ArcDroid ADFFS launcher\rSet ADFFS$Dir ADFS::1.$.!ADFFS\rSet "
      "ADFFS$Path ADFS::1.$.!ADFFS.\rRun ADFS::1.$.!ADFFS.SetVars\rSet "
      "ADFFS$FileSizeLimit 2048\rSet Alias$StoreCSD Set ADFFS$CSD "
      "<FileSwitch$CurrentFilingSystem>#<Dir>\rSet Alias$RestoreCSD "
      "FilingSystem <ADFFS$CSD>\rSet Alias$ADFClipScreen ADFDoNothing\rSet "
      "Alias$ADFScreenBorders ADFDoNothing\rSet ADFFS$JIT 0\rSet ADFFS$CRC "
      "0\rUnset ADFFS$LCDGM\rRMEnsure SharedCLibrary 5.17 RMLoad "
      "ADFS::1.$.!ADFFS.3rdParty.CLib\rRMEnsure SharedCLibrary 5.17 Error "
      "ArcDroid ADFFS step CLib failed\rRMEnsure CallASWI 0 RMLoad "
      "ADFS::1.$.!ADFFS.3rdParty.CallASWI\rRMEnsure CallASWI 0 Error ArcDroid "
      "ADFFS step CallASWI failed\rRMEnsure ZLib 0 RMLoad "
      "ADFS::1.$.!ADFFS.3rdParty.ZLib\rRMEnsure ZLib 0 Error ArcDroid ADFFS "
      "step ZLib failed\rRMEnsure BootCmds 0 RMLoad "
      "ADFS::1.$.!ADFFS.3rdParty.BootCmds\rRMEnsure BootCmds 0 Error ArcDroid "
      "ADFFS step BootCmds failed\rRMEnsure ADFFS 0 RMLoad "
      "ADFS::1.$.!ADFFS.ADFFS\rRMEnsure ADFFS 0 Error ArcDroid ADFFS step "
      "ADFFS failed\rADFMount "
      "ADFS::0.$.Game\rADFS\rNoDir\rNoLib\rNoURD\rDismount :0\rADFBootFloppy\r";
  size_t lsz;
  assert(arc_adffs_launcher(NULL, 0, &lsz));
  char *launch = calloc(1, lsz + 1u);
  assert(launch && arc_adffs_launcher((uint8_t *)launch, lsz, &lsz) &&
         lsz == sizeof(launcher_golden) - 1u &&
         !memcmp(launch, launcher_golden, lsz));
  assert(!strstr(launch, "Echo") && !strstr(launch, "RAM") &&
         !strstr(launch, "ArcFixture") &&
         !strstr(launch, "ArcDroid synthetic ADFFS") &&
         strstr(launch, "ADFFS$Dir ADFS::1.$.!ADFFS"));
  char *mount = strstr(launch, "ADFMount"), *drive0 = strstr(launch, "ADFS::0"),
       *dismount = strstr(launch, "Dismount :0"),
       *boot = strstr(launch, "ADFBootFloppy");
  assert(mount && drive0 == mount + 9 && dismount > mount && boot > dismount);
  free(launch);
  ArcMediaInfo info = {0};
  info.kind = ARC_MEDIA_ADF_800K;
  info.geometry = ARC_GEOMETRY_800K_D;
  ArcLaunchDiscovery discovery;
  assert(arc_launch_discover(carrier, ARC_ADFFS_DISC_SIZE, &info, NULL,
                             &discovery) &&
         discovery.readable);
  assert(arc_launch_discover(support, ARC_ADFFS_DISC_SIZE, &info, NULL,
                             &discovery) &&
         discovery.readable);
  ArcAdffsPackage huge = p;
  huge.count = ARC_ADFFS_MAX_MEMBERS;
  for (unsigned i = p.count; i < huge.count; ++i) {
    snprintf(huge.members[i].name, sizeof(huge.members[i].name),
             "!ADFFS/trackers/R%02u", i);
    huge.members[i].data = calloc(1, 20000u);
    huge.members[i].size = 20000u;
  }
  assert(!arc_adffs_support_build(&huge, support, error, sizeof(error)));
  for (unsigned i = p.count; i < huge.count; ++i)
    free(huge.members[i].data);
  arc_adffs_package_free(&p);
  free(zip);
  free(jfd);
  free(carrier);
  free(carrier2);
  free(support);

  reject_fixture(argv[1], "truncated.zip", "central");
  reject_fixture(argv[1], "bad-crc.zip", "CRC32");
  reject_fixture(argv[1], "inflated-lie.zip", "inflated-size");
  reject_fixture(argv[1], "entry-limit.zip", "1024");
  reject_fixture(argv[1], "oversize.zip", "2 MiB");
  reject_fixture(argv[1], "encrypted.zip", "encrypted");
  reject_fixture(argv[1], "zip64.zip", "zip64");
  reject_fixture(argv[1], "zip64-extra.zip", "zip64");
  reject_fixture(argv[1], "descriptor-mismatch.zip", "descriptor");
  reject_fixture(argv[1], "duplicate.zip", "duplicate");
  reject_fixture(argv[1], "case-collision.zip", "case-colliding");
  reject_fixture(argv[1], "local-mismatch.zip", "local/central");
  reject_fixture(argv[1], "missing-arc0.zip", "ARC0");
  reject_fixture(argv[1], "unsafe-name.zip", "unsafe");
  reject_fixture(argv[1], "absolute-name.zip", "unsafe");
  reject_fixture(argv[1], "backslash-name.zip", "unsafe");
  puts("ADFFS synthetic package and media tests passed");
  return 0;
}
