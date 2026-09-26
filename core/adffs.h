/* User-supplied ADFFS package support. GPL-2.0-only. */
#ifndef ARCDROID_ADFFS_H
#define ARCDROID_ADFFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARC_ADFFS_DISC_SIZE (800u * 1024u)
#define ARC_ADFFS_CARRIER_CAPACITY (ARC_ADFFS_DISC_SIZE - 4096u)
#define ARC_ADFFS_MAX_MEMBERS 64u

typedef struct {
  char name[128];
  uint8_t *data;
  size_t size;
  uint32_t load, exec, attr;
} ArcAdffsMember;

typedef struct {
  ArcAdffsMember members[ARC_ADFFS_MAX_MEMBERS];
  unsigned count;
  bool verified;
  char profile[32];
  char version[64];
} ArcAdffsPackage;

typedef struct {
  bool member[ARC_ADFFS_MAX_MEMBERS];
  unsigned count;
  uint32_t ram_size;
} ArcAdffsSelection;

typedef struct {
  const char *name;
  const char *sha256[6];
  const char *version;
} ArcAdffsProfile;

#define ARC_INSTALL_MAX_DISCS 4u
#define ARC_INSTALL_MAX_VARIANTS 2u
typedef struct {
  unsigned number;
  size_t size;
  const char *sha1;
} ArcInstallDisc;
typedef struct {
  const char *key;
  const char *label;
  ArcInstallDisc discs[ARC_INSTALL_MAX_DISCS];
  unsigned disc_count;
  const char *app;
  unsigned data_disc;
  const char *data_file;
  const char *data_dir;
  bool image_in_app;
  bool eject;
  unsigned ram_kib;
  unsigned expected_tree_count;
  const char *expected_tree_sha256;
} ArcInstallVariant;
typedef struct {
  unsigned game_id;
  unsigned disc_count;
  unsigned revision;
  const char *script_sha256;
  unsigned io_mask; /* bit 0: WD1772, bit 1: New-IO */
  ArcInstallVariant variants[ARC_INSTALL_MAX_VARIANTS];
  unsigned variant_count;
  unsigned runnable_revisions[4];
  unsigned runnable_revision_count;
} ArcInstallProfile;
typedef struct {
  unsigned game_id, disc_number, disc_count;
  size_t decoded_size;
  char sha1[41];
} ArcInstallMedia;
typedef struct {
  const ArcInstallProfile *profile;
  const ArcInstallVariant *variant;
  unsigned media_index[ARC_INSTALL_MAX_DISCS];
  char binding[41];
  char storage_id[41];
} ArcInstallPlan;
typedef struct {
  unsigned track, side, sector;
} ArcInstallDoorbell;

const ArcInstallProfile *arc_install_profiles(size_t *count);
const ArcInstallProfile *arc_install_profile(unsigned game_id,
                                             unsigned disc_count);
/* Returns presentable variants; each plan is immutable after matching. */
unsigned arc_install_match(const ArcInstallProfile *profile,
                           const ArcInstallMedia *media, unsigned media_count,
                           ArcInstallPlan plans[ARC_INSTALL_MAX_VARIANTS],
                           const char **reason);
bool arc_install_script_matches(const ArcInstallProfile *profile,
                                const uint8_t *script, size_t size);
bool arc_install_revision_supported(const ArcInstallProfile *profile,
                                     unsigned revision);
bool arc_install_launcher(const ArcInstallPlan *plan, uint8_t *output,
                          size_t capacity, size_t *size);
bool arc_install_hd_launcher(const ArcInstallPlan *plan, bool run_plan,
                             uint8_t *output, size_t capacity, size_t *size);
bool arc_install_marker(const ArcInstallPlan *plan, uint8_t *output,
                        size_t capacity, size_t *size);
bool arc_install_carrier_build(const ArcInstallPlan *plan,
                               const uint8_t *const raw[ARC_INSTALL_MAX_DISCS],
                               const size_t sizes[ARC_INSTALL_MAX_DISCS],
                               uint8_t output[ARC_ADFFS_DISC_SIZE],
                               char *error, size_t error_size);
bool arc_install_hd_carrier_build(const ArcInstallPlan *plan,
                                  const uint8_t *const raw[ARC_INSTALL_MAX_DISCS],
                                  const size_t sizes[ARC_INSTALL_MAX_DISCS],
                                  uint8_t output[ARC_ADFFS_DISC_SIZE],
                                  ArcInstallDoorbell *doorbell,
                                  char *error, size_t error_size);
bool arc_install_support_build(const ArcAdffsPackage *package,
                               const ArcAdffsSelection *selection,
                               const ArcInstallPlan *plan,
                               uint8_t output[ARC_ADFFS_DISC_SIZE],
                               char *error, size_t error_size);
bool arc_install_hd_support_build(const ArcAdffsPackage *package,
                                  const ArcAdffsSelection *selection,
                                  const ArcInstallPlan *plan, bool run_plan,
                                  uint8_t output[ARC_ADFFS_DISC_SIZE],
                                  char *error, size_t error_size);

const ArcAdffsProfile *arc_adffs_profiles(size_t *count);
bool arc_adffs_package_parse(const uint8_t *archive, size_t archive_size,
                             ArcAdffsPackage *package, char *error,
                             size_t error_size);
void arc_adffs_package_free(ArcAdffsPackage *package);
const ArcAdffsMember *arc_adffs_member(const ArcAdffsPackage *package,
                                       const char *name);
bool arc_adffs_builtin_script(const ArcAdffsPackage *package, unsigned game_id,
                              unsigned disc_number, uint8_t **script,
                              size_t *script_size, char *error,
                              size_t error_size);
bool arc_adffs_select(const ArcAdffsPackage *package, const uint8_t *script,
                      size_t script_size, ArcAdffsSelection *selection,
                      char missing[128]);
bool arc_adffs_launcher(uint8_t *output, size_t capacity, size_t *size);
bool arc_adffs_stage1(uint32_t ram_size, uint8_t *output, size_t capacity,
                      size_t *size);
bool arc_adffs_carrier_build(const uint8_t *jfd, size_t jfd_size,
                             uint8_t output[ARC_ADFFS_DISC_SIZE], char *error,
                             size_t error_size);
bool arc_adffs_support_build(const ArcAdffsPackage *package,
                             const ArcAdffsSelection *selection,
                             uint8_t output[ARC_ADFFS_DISC_SIZE], char *error,
                             size_t error_size);

#endif
