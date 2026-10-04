/* ARCDROID-PROBE: marked copy of core/install_disc.h; larger bounded trial disc. GPL-2.0-only. */
#ifndef ARCDROID_INSTALL_DISC_H
#define ARCDROID_INSTALL_DISC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ARC_INSTALL_HD_SECTOR 512u
#define ARC_INSTALL_HD_SPT 63u
#define ARC_INSTALL_HD_HEADS 16u
#define ARC_INSTALL_HD_CYLINDERS 256u
#define ARC_INSTALL_HD_SECTORS (ARC_INSTALL_HD_SPT * ARC_INSTALL_HD_HEADS * ARC_INSTALL_HD_CYLINDERS)
#define ARC_INSTALL_HD_BYTES (ARC_INSTALL_HD_SECTORS * ARC_INSTALL_HD_SECTOR)
#define ARC_INSTALL_HD_FORMAT_VERSION 4u
bool arc_install_hd_format(uint8_t *output, size_t size);
#endif
