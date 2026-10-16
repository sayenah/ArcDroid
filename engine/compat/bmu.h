/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ARCDROID_BMU_STUB_H
#define ARCDROID_BMU_STUB_H
#include <stdint.h>
uint8_t bmu_read(int address);
void bmu_write(int address, uint8_t value);
#endif
