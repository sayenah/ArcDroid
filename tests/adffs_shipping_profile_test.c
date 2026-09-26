#include "adffs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const ArcInstallProfile *arc_test_install_profiles(size_t *count);

static bool same_text(const char *left, const char *right) {
  return left == right || (left && right && !strcmp(left, right));
}

static bool same_variant(const ArcInstallVariant *left,
                         const ArcInstallVariant *right) {
  if (!same_text(left->key, right->key) ||
      !same_text(left->label, right->label) ||
      left->disc_count != right->disc_count ||
      !same_text(left->app, right->app) ||
      left->data_disc != right->data_disc ||
      !same_text(left->data_file, right->data_file) ||
      !same_text(left->data_dir, right->data_dir) ||
      left->image_in_app != right->image_in_app ||
      left->eject != right->eject ||
      left->ram_kib != right->ram_kib ||
      left->expected_tree_count != right->expected_tree_count ||
      !same_text(left->expected_tree_sha256, right->expected_tree_sha256)) return false;
  for (unsigned d = 0u; d < ARC_INSTALL_MAX_DISCS; ++d)
    if (left->discs[d].number != right->discs[d].number ||
        left->discs[d].size != right->discs[d].size ||
        !same_text(left->discs[d].sha1, right->discs[d].sha1)) return false;
  return true;
}

static bool same_profile(const ArcInstallProfile *left,
                         const ArcInstallProfile *right) {
  if (left->game_id != right->game_id ||
      left->disc_count != right->disc_count ||
      left->revision != right->revision ||
      !same_text(left->script_sha256, right->script_sha256) ||
      left->io_mask != right->io_mask ||
      left->hd_io_mask != right->hd_io_mask ||
      left->variant_count != right->variant_count ||
      left->runnable_revision_count != right->runnable_revision_count) return false;
  for (unsigned r = 0u; r < left->runnable_revision_count; ++r)
    if (left->runnable_revisions[r] != right->runnable_revisions[r]) return false;
  for (unsigned v = 0u; v < ARC_INSTALL_MAX_VARIANTS; ++v)
    if (!same_variant(&left->variants[v], &right->variants[v])) return false;
  return true;
}

#define CHECK(expression)                                                       \
  do {                                                                          \
    if (!(expression)) {                                                        \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,        \
              #expression);                                                     \
      return EXIT_FAILURE;                                                      \
    }                                                                           \
  } while (0)
int main(void) {
  static const char *const expected[6] = {
      "23256f9c73ea49a6c51d21844b2d07eaaa235bea41258b7a92e05ff488e77cae",
      "266617cf3f8c4f541730d9704dd9f390b71e00318e96a128f4b3dc609571b831",
      "2935eaa8bf7111ee726496eb2764e56206bdeed0cb220e50c4ed33c4acf9d849",
      "a73f88eac988c9057d0b46117279166f4c2cfda4e92574f385b80d57440a27fa",
      "6a012e8fc0e480c2b5cb88b98f4249303c88e7d2f32bfe7b05c8e1fc3e668e07",
      "96bb091eb6fedc96a32da876454f5e9f8aa53426d141bd20b4763a15c0f0efdc"};
  size_t n = 0;
  const ArcAdffsProfile *p = arc_adffs_profiles(&n);
  CHECK(p && n == 1u && !strcmp(p[0].name, "adffs-2.88-ioc") &&
        !strcmp(p[0].version, "ADFFS 2.88 (28 Mar 2026) IOC"));
  for (unsigned i = 0; i < 6u; ++i)
    CHECK(!strcmp(p[0].sha256[i], expected[i]));
  size_t installs = 0u;
  const ArcInstallProfile *profiles = arc_install_profiles(&installs);
  CHECK(profiles && installs == 3u);
  size_t test_count = 0u;
  const ArcInstallProfile *test_profiles = arc_test_install_profiles(&test_count);
  CHECK(test_profiles && test_count == installs + 1u &&
        test_profiles[installs].game_id == 123u);
  for (size_t i = 0u; i < installs; ++i)
    CHECK(same_profile(&profiles[i], &test_profiles[i]));
  CHECK(profiles[0].variants[0].ram_kib == 2000u &&
        profiles[1].variants[0].ram_kib == 2000u &&
        profiles[1].variants[1].ram_kib == 2000u);
  CHECK(profiles[0].io_mask == 1u && profiles[0].hd_io_mask == 2u &&
        profiles[1].io_mask == 1u && profiles[1].hd_io_mask == 2u &&
        profiles[2].io_mask == 2u && profiles[2].hd_io_mask == 2u);
  for (size_t i = 0u; i < installs; ++i) {
    const ArcInstallProfile *profile = &profiles[i];
    CHECK(profile->game_id && profile->disc_count && profile->revision &&
          profile->io_mask && !(profile->io_mask & ~3u) &&
          profile->hd_io_mask == 2u &&
          strlen(profile->script_sha256) == 64u &&
          profile->variant_count >= 1u &&
          profile->variant_count <= ARC_INSTALL_MAX_VARIANTS);
    for (size_t earlier = 0u; earlier < i; ++earlier)
      CHECK(profiles[earlier].game_id != profile->game_id);
    for (unsigned v = 0u; v < profile->variant_count; ++v) {
      const ArcInstallVariant *variant = &profile->variants[v];
      CHECK(variant->key && variant->label && variant->app &&
            strlen(variant->app) <= 10u &&
            (!variant->data_file || strlen(variant->data_file) <= 10u) &&
            (!variant->data_dir || strlen(variant->data_dir) <= 10u) &&
            variant->disc_count >= 1u &&
            variant->disc_count <= ARC_INSTALL_MAX_DISCS &&
            variant->discs[0].number == 1u &&
            variant->ram_kib <= 4096u &&
            !strstr(variant->label, "ADFFS") &&
            !strstr(variant->app, "ADFFS"));
      for (unsigned old = 0u; old < v; ++old)
        CHECK(strcmp(variant->key, profile->variants[old].key));
      for (unsigned d = 0u; d < variant->disc_count; ++d) {
        CHECK(variant->discs[d].number >= 1u &&
              variant->discs[d].number <= profile->disc_count &&
              strlen(variant->discs[d].sha1) == 40u &&
              variant->discs[d].size <= 2u * 1024u * 1024u);
        for (unsigned old = 0u; old < d; ++old)
          CHECK(variant->discs[old].number < variant->discs[d].number);
      }
      if (variant->data_file || variant->data_dir) {
        bool found = false;
        for (unsigned d = 1u; d < variant->disc_count; ++d)
          if (variant->discs[d].number == variant->data_disc) found = true;
        CHECK(found);
      }
    }
  }
  return 0;
}
