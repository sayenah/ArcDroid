/* ArcDroid-authored hard-disc geometry and formatter. GPL-2.0-only. */
#ifndef ARCDROID_INSTALL_DISC_H
#define ARCDROID_INSTALL_DISC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ARC_INSTALL_HD_SECTOR 512u
#define ARC_INSTALL_HD_SPT 63u
#define ARC_INSTALL_HD_HEADS 16u
#define ARC_INSTALL_HD_CYLINDERS 32u
#define ARC_INSTALL_HD_SECTORS (ARC_INSTALL_HD_SPT * ARC_INSTALL_HD_HEADS * ARC_INSTALL_HD_CYLINDERS)
#define ARC_INSTALL_HD_BYTES (ARC_INSTALL_HD_SECTORS * ARC_INSTALL_HD_SECTOR)
#define ARC_INSTALL_HD_FORMAT_VERSION 2u
#define ARC_RPC_HD_SECTORS (63u * 16u * 256u)
#define ARC_RPC_HD_BYTES (ARC_RPC_HD_SECTORS * 512u)
/* Bounded authored formatter ranges; supported pairs: 32/10 and 256/13. */
bool arc_hd_format_range(unsigned cylinders, unsigned log_map_bit,
    uint64_t offset, uint8_t *output, size_t size);
bool arc_install_hd_format(uint8_t *output, size_t size);
#endif
