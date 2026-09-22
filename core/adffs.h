/* User-supplied ADFFS package support. GPL-2.0-only. */
#ifndef ARCDROID_ADFFS_H
#define ARCDROID_ADFFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arcdroid_core.h"

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
bool arc_adffs_launcher(unsigned disc_count, size_t maximum_decoded_size,
                        uint8_t *output, size_t capacity, size_t *size);
bool arc_adffs_stage1(uint32_t ram_size, uint8_t *output, size_t capacity,
                      size_t *size);
bool arc_adffs_carrier_build(const uint8_t *jfd, size_t jfd_size,
                             const char *name, ArcMediaGeometry geometry,
                             size_t carrier_capacity,
                             uint8_t output[ARC_ADFFS_DISC_SIZE], char *error,
                             size_t error_size);
bool arc_adffs_support_build(const ArcAdffsPackage *package,
                             const ArcAdffsSelection *selection,
                             unsigned disc_count,
                             size_t maximum_decoded_size,
                             uint8_t output[ARC_ADFFS_DISC_SIZE], char *error,
                             size_t error_size);

#endif
