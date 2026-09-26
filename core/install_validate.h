/* ArcDroid-authored FileCore installed-tree validator. GPL-2.0-only. */
#ifndef ARCDROID_INSTALL_VALIDATE_H
#define ARCDROID_INSTALL_VALIDATE_H
#include "adffs.h"
#include "install_disc.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
   unsigned tree_count;
   char tree_sha256[65];
   char reason[128];
} ArcInstallValidation;
bool arc_install_hd_validate(const ArcInstallPlan *plan,
   const uint8_t *image, size_t size, ArcInstallValidation *result);
#endif
