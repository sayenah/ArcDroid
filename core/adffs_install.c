/* Exact-identity install planning. GPL-2.0-only. */
#include "adffs.h"
#include "arcdroid_core.h"

#include <stdio.h>
#include <string.h>

const ArcInstallProfile *arc_install_profile(unsigned game_id,
                                             unsigned disc_count) {
  size_t count = 0u;
  const ArcInstallProfile *profiles = arc_install_profiles(&count);
  for (size_t i = 0u; i < count; ++i)
    if (profiles[i].game_id == game_id &&
        profiles[i].disc_count == disc_count) return &profiles[i];
  return NULL;
}

bool arc_install_revision_supported(const ArcInstallProfile *profile,
                                     unsigned revision) {
  if (!profile) return false;
  for (unsigned i = 0u; i < profile->runnable_revision_count; ++i)
    if (profile->runnable_revisions[i] == revision) return true;
  return false;
}

bool arc_install_script_matches(const ArcInstallProfile *profile,
                                const uint8_t *script, size_t size) {
  char digest[65];
  if (!profile || !script) return false;
  arc_sha256_hex(script, size, digest);
  return !strcmp(profile->script_sha256, digest);
}

unsigned arc_install_match(const ArcInstallProfile *profile,
                           const ArcInstallMedia *media, unsigned media_count,
                           ArcInstallPlan plans[ARC_INSTALL_MAX_VARIANTS],
                           const char **reason) {
  unsigned present = 0u;
  const char *why = "incomplete-set";
  if (reason) *reason = why;
  if (!profile || !media || !plans ||
      profile->variant_count > ARC_INSTALL_MAX_VARIANTS) return 0u;
  for (unsigned v = 0u; v < profile->variant_count; ++v) {
    const ArcInstallVariant *variant = &profile->variants[v];
    ArcInstallPlan plan = {0};
    plan.profile = profile;
    plan.variant = variant;
    bool good = true;
    char identity[512];
    int length = snprintf(identity, sizeof(identity), "%u:%u:%s",
                          profile->game_id, profile->revision, variant->key);
    char storage_identity[512];
    int storage_length = snprintf(storage_identity, sizeof(storage_identity),
                                  "install-v1|F%05u|%s", profile->game_id,
                                  variant->key);
    for (unsigned d = 0u; good && d < variant->disc_count; ++d) {
      const ArcInstallDisc *required = &variant->discs[d];
      unsigned matches = 0u;
      bool exact = false;
      for (unsigned i = 0u; i < media_count; ++i)
        if (media[i].game_id == profile->game_id &&
            media[i].disc_count == profile->disc_count &&
            media[i].disc_number == required->number) {
          ++matches;
          if (media[i].decoded_size == required->size &&
              !strcmp(media[i].sha1, required->sha1)) {
            plan.media_index[d] = i;
            exact = true;
          } else why = "identity";
        }
      if (matches != 1u || !exact) {
        if (matches > 1u) why = "duplicate-disc";
        good = false;
      }
      if (required->size > 2u * 1024u * 1024u) {
        why = "decoded-too-large";
        good = false;
      }
      if (good && length > 0 && (size_t)length < sizeof(identity))
        length += snprintf(identity + length, sizeof(identity) - (size_t)length,
                           ":%u:%s", required->number, required->sha1);
      if (good && storage_length > 0 &&
          (size_t)storage_length < sizeof(storage_identity))
        storage_length += snprintf(storage_identity + storage_length,
             sizeof(storage_identity) - (size_t)storage_length,
             "|%u=%s", required->number, required->sha1);
    }
    if (!good || length < 0 || (size_t)length >= sizeof(identity) ||
        storage_length < 0 ||
        (size_t)storage_length >= sizeof(storage_identity)) continue;
    arc_sha1_hex(identity, (size_t)length, plan.binding);
    arc_sha1_hex(storage_identity, (size_t)storage_length, plan.storage_id);
    plans[present++] = plan;
  }
  if (reason) *reason = present ? NULL : why;
  return present;
}
