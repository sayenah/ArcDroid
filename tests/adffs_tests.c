/* Deterministic tests for synthetic ADFFS packaging. GPL-2.0-only. */
#include "adffs.h"
#include "arcdroid_core.h"
#include "jfd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_failed(const char *file, int line, const char *expression) {
  fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
  fflush(stderr);
  exit(EXIT_FAILURE);
}
#define CHECK(expression)                                                       \
  do {                                                                          \
    if (!(expression))                                                          \
      check_failed(__FILE__, __LINE__, #expression);                            \
  } while (0)
#define assert(expression) CHECK(expression)

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
static bool selected_member(const ArcAdffsPackage *package,
                            const ArcAdffsSelection *selection,
                            const char *name) {
  for (unsigned i = 0; i < package->count; ++i)
    if (!strcmp(package->members[i].name, name))
      return selection->member[i];
  return false;
}
static unsigned substring_count(const char *text, const char *needle) {
  unsigned count = 0u;
  size_t length = strlen(needle);
  for (const char *at = text; (at = strstr(at, needle)); at += length)
    ++count;
  return count;
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
  assert(p.verified && !strcmp(p.profile, "synthetic-test") && p.count == 13u);
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
  ArcAdffsSelection selection;
  const char *r1 = "adffs:TRACKERS.qTMMODULE.extra",
             *r2 = "ADFFS:extras.Absent", *r3 = "ADFFS:obey.F0012301",
             *r4 = "ADFFS:obey.F9999901";
  assert(arc_adffs_select(&p, (const uint8_t *)r1, strlen(r1), &selection,
                          missing) &&
         selection.count == 10u);
  assert(!arc_adffs_select(&p, (const uint8_t *)r2, strlen(r2), &selection,
                           missing) &&
         strstr(missing, "Absent"));
  assert(arc_adffs_select(&p, (const uint8_t *)r3, strlen(r3), &selection,
                          missing));
  assert(!arc_adffs_select(&p, (const uint8_t *)r4, strlen(r4), &selection,
                           missing) &&
         strstr(missing, "F9999901"));
  const char *chain = "adffs:ObEy.cHaInA";
  assert(arc_adffs_select(&p, (const uint8_t *)chain, strlen(chain), &selection,
                          missing) &&
         selection.count == 11u &&
         selected_member(&p, &selection, "!ADFFS/extras/JoyMod") &&
         selected_member(&p, &selection, "!ADFFS/trackers/CaseTrack") &&
         !selected_member(&p, &selection, "!ADFFS/trackers/QTMModule") &&
         !selected_member(&p, &selection, "!ADFFS/trackers/Huge"));
  const char *cycle = "ADFFS:obey.CycleA";
  assert(arc_adffs_select(&p, (const uint8_t *)cycle, strlen(cycle), &selection,
                          missing) &&
         selection.count == 9u);
  const char *depth = "ADFFS:obey.Depth1";
  assert(!arc_adffs_select(&p, (const uint8_t *)depth, strlen(depth),
                           &selection, missing) &&
         strstr(missing, "Depth5"));
  assert(arc_adffs_select(&p, script, script_size, &selection, missing) &&
         selection.count == 10u &&
         selected_member(&p, &selection, "!ADFFS/trackers/QTMModule") &&
         !selected_member(&p, &selection, "!ADFFS/extras/JoyMod"));
  uint32_t expected_ram = 64u * 1024u;
  for (unsigned i = 0; i < p.count; ++i)
    if (selection.member[i])
      expected_ram += ((uint32_t)p.members[i].size + 1023u) & ~1023u;
  size_t launcher_size = 0u;
  assert(arc_adffs_launcher(NULL, 0u, &launcher_size));
  expected_ram += 3u * 2048u;
  expected_ram += ((uint32_t)launcher_size + 1023u) & ~1023u;
  expected_ram = (expected_ram + 32767u) & ~32767u;
  assert(selection.ram_size == expected_ram && !(selection.ram_size & 32767u));
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
  assert(arc_adffs_support_build(&p, &selection, support, error, sizeof(error)));
  walk_disc(support);
  root = support + 1024u;
  const uint8_t *adffs = find_entry(root, "!ADFFS");
  assert(adffs && (adffs[25] & 8u) && (adffs[25] & 2u) &&
         !(le24(adffs + 22u) & 3u));
  const uint8_t *adir = support + le24(adffs + 22u) * 256u;
  assert(dircheck(adir));
  const uint8_t *module = find_entry(adir, "ADFFS");
  assert(module && le32t(module + 10u) == runtime->load &&
         le32t(module + 14u) == runtime->exec && module[25] == runtime->attr);
  assert(find_entry(adir, "obey") && find_entry(adir, "!RunTypes") &&
         find_entry(adir, "!fixObey") && !find_entry(adir, "extras"));
  const uint8_t *trackers = find_entry(adir, "trackers");
  assert(trackers && (trackers[25] & 8u));
  const uint8_t *trackers_dir = support + le24(trackers + 22u) * 256u;
  assert(find_entry(trackers_dir, "QTMModule") &&
         !find_entry(trackers_dir, "CaseTrack") &&
         !find_entry(trackers_dir, "Huge"));
  size_t rzn;
  uint8_t *rzip = read_file(argv[1], "reordered.zip", &rzn);
  ArcAdffsPackage reordered;
  uint8_t *reordered_script = NULL;
  size_t reordered_script_size = 0u;
  ArcAdffsSelection reordered_selection;
  assert(arc_adffs_package_parse(rzip, rzn, &reordered, error, sizeof(error)) &&
         arc_adffs_builtin_script(&reordered, 123u, 1u, &reordered_script,
                                  &reordered_script_size, error,
                                  sizeof(error)) &&
         arc_adffs_select(&reordered, reordered_script, reordered_script_size,
                          &reordered_selection, missing) &&
         arc_adffs_support_build(&reordered, &reordered_selection, carrier2,
                                 error, sizeof(error)) &&
         !memcmp(support, carrier2, ARC_ADFFS_DISC_SIZE));
  free(reordered_script);
  arc_adffs_package_free(&reordered);
  free(rzip);
  static const char launcher_golden[] =
      "| ArcDroid ADFFS RAM launcher\rSet ADFFS$Dir RAM::0.$.!ADFFS\rSet "
      "ADFFS$Path RAM::0.$.!ADFFS.\rRun RAM::0.$.!ADFFS.SetVars\rSetEval "
      "ADFFS$FileSizeLimit 2048\rSet Alias$StoreCSD ADFDoNothing %%*0\rSet "
      "Alias$RestoreCSD ADFDoNothing %%*0\rSet Alias$ADFClipScreen "
      "ADFDoNothing %%*0\rSet Alias$ADFScreenBorders ADFDoNothing %%*0\r"
      "SetEval ADFFS$JIT 0\rSetEval ADFFS$CRC 0\rSet ADFFS$LCDGM N\rRMEnsure "
      "SharedCLibrary 5.17 RMLoad RAM::0.$.!ADFFS.3rdParty.CLib\rRMEnsure "
      "SharedCLibrary 5.17 Error ArcDroid ADFFS step CLib failed\rRMEnsure "
      "CallASWI 0.02 RMLoad RAM::0.$.!ADFFS.3rdParty.CallASWI\rRMEnsure "
      "CallASWI 0.02 Error ArcDroid ADFFS step CallASWI failed\rRMEnsure "
      "ZLib 0.05 RMLoad RAM::0.$.!ADFFS.3rdParty.ZLib\rRMEnsure ZLib 0.05 "
      "Error ArcDroid ADFFS step ZLib failed\rRMEnsure BootCommands 0.00 "
      "RMLoad RAM::0.$.!ADFFS.3rdParty.BootCmds\rRMEnsure BootCommands 0.00 "
      "Error ArcDroid ADFFS step BootCommands failed\rRMEnsure ADFFS 0.00 "
      "RMLoad RAM::0.$.!ADFFS.ADFFS\rRMEnsure ADFFS 0.00 Error ArcDroid "
      "ADFFS step ADFFS failed\rADFMount "
      "ADFS::0.$.Game\rADFS\rNoDir\rNoLib\rNoURD\rDismount :0\rADFBootFloppy\r";
  size_t lsz;
  assert(arc_adffs_launcher(NULL, 0, &lsz));
  char *launch = calloc(1, lsz + 1u);
  assert(launch && arc_adffs_launcher((uint8_t *)launch, lsz, &lsz) &&
         lsz == sizeof(launcher_golden) - 1u &&
         !memcmp(launch, launcher_golden, lsz));
  assert(!strstr(launch, "Echo") && !strstr(launch, "ADFS::1") &&
         !strstr(launch, "ArcFixture") &&
         !strstr(launch, "ArcDroid synthetic ADFFS") &&
         strstr(launch, "ADFFS$Dir RAM::0.$.!ADFFS") &&
         strstr(launch, "SetEval ADFFS$FileSizeLimit 2048") &&
         strstr(launch, "SetEval ADFFS$JIT 0") &&
         strstr(launch, "SetEval ADFFS$CRC 0") &&
         strstr(launch, "Set ADFFS$LCDGM N"));
  assert(substring_count(launch, "ADFDoNothing %%*0\r") == 4u &&
         strstr(launch, "Set Alias$StoreCSD ADFDoNothing %%*0\r") &&
         strstr(launch, "Set Alias$RestoreCSD ADFDoNothing %%*0\r") &&
         strstr(launch, "Set Alias$ADFClipScreen ADFDoNothing %%*0\r") &&
         strstr(launch, "Set Alias$ADFScreenBorders ADFDoNothing %%*0\r"));
  char *mount = strstr(launch, "ADFMount"), *drive0 = strstr(launch, "ADFS::0"),
       *dismount = strstr(launch, "Dismount :0"),
       *boot = strstr(launch, "ADFBootFloppy");
  assert(mount && drive0 == mount + 9 && dismount > mount && boot > dismount);
  size_t stage1_size = 0u;
  assert(arc_adffs_stage1(selection.ram_size, NULL, 0u, &stage1_size));
  char *stage1 = calloc(1u, stage1_size + 1u);
  assert(stage1 && arc_adffs_stage1(selection.ram_size, (uint8_t *)stage1,
                                    stage1_size, &stage1_size));
  char stage1_golden[512];
  int stage1_golden_size = snprintf(
      stage1_golden, sizeof(stage1_golden),
      "ChangeDynamicArea -RamFsSize %uK\r"
      "Copy ADFS::1.$.!ADFFS RAM::0.$.!ADFFS ~C~VRFQ\r"
      "Copy ADFS::1.$.ArcStage2 RAM::0.$.ArcStage2 ~C~VFQ\r"
      "Filer_Run RAM::0.$.ArcStage2\r",
      selection.ram_size / 1024u);
  assert(stage1_golden_size > 0 &&
         stage1_size == (size_t)stage1_golden_size &&
         !memcmp(stage1, stage1_golden, stage1_size));
  char ram_size_command[64];
  int ram_size_command_length =
      snprintf(ram_size_command, sizeof(ram_size_command),
               "ChangeDynamicArea -RamFsSize %uK\r",
               selection.ram_size / 1024u);
  assert(!(selection.ram_size % 1024u) && ram_size_command_length > 0 &&
         substring_count(stage1, "ChangeDynamicArea -RamFsSize ") == 1u &&
         strstr(stage1, ram_size_command));
  assert(!strstr(stage1, "Echo") && !strstr(stage1, "RMEnsure") &&
         !strstr(stage1, "RMLoad") && !strstr(stage1, "ADFMount") &&
         !strstr(stage1, "Confirm") && !strstr(stage1, "Verbose") &&
         !strstr(stage1, "Recurse") && !strstr(stage1, "Force") &&
         !strstr(stage1, "Quiet"));
  const uint8_t *desktop_entry = find_entry(root, "ArcLaunch");
  const uint8_t *stage1_entry = find_entry(root, "ArcStage1");
  const uint8_t *stage2_entry = find_entry(root, "ArcStage2");
  static const char desktop_golden[] = "Filer_Run ADFS::1.$.ArcStage1\r";
  assert(desktop_entry && stage1_entry && stage2_entry &&
         le32t(desktop_entry + 18u) == sizeof(desktop_golden) - 1u &&
         !memcmp(support + le24(desktop_entry + 22u) * 256u, desktop_golden,
                 sizeof(desktop_golden) - 1u) &&
         le32t(stage1_entry + 18u) == stage1_size &&
         !memcmp(support + le24(stage1_entry + 22u) * 256u, stage1,
                 stage1_size) &&
         le32t(stage2_entry + 18u) == lsz &&
         !memcmp(support + le24(stage2_entry + 22u) * 256u, launch, lsz));
  free(stage1);
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
  const char *huge_reference = "ADFFS:trackers.Huge";
  ArcAdffsSelection huge_selection;
  assert(arc_adffs_select(&p, (const uint8_t *)huge_reference,
                          strlen(huge_reference), &huge_selection, missing) &&
         !arc_adffs_support_build(&p, &huge_selection, support, error,
                                  sizeof(error)) &&
         strstr(error, "does not fit"));
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
  reject_fixture(argv[1], "missing-obey.zip", "!ADFFS/obey");
  reject_fixture(argv[1], "missing-runtypes.zip", "!ADFFS/!RunTypes");
  reject_fixture(argv[1], "missing-fixobey.zip", "!ADFFS/!fixObey");
  reject_fixture(argv[1], "invalid-obey-name.zip", "obey member name");
  puts("ADFFS synthetic package and media tests passed");
  return 0;
}
